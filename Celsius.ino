#include <Wire.h>
#include <cstring>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_SHT31.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <esp_sleep.h>
#include <driver/adc.h>

#define WIFI_SSID "WiFi_SSID"
#define WIFI_PASSWORD "WiFi_Password"
#define I2C_SDA 8
#define I2C_SCL 9
#define OLED_ADDR 0x3C
#define SHT31_ADDR 0x44
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define LED_PIN 0
#define BAT_PIN 3          // GPIO 3
#define SLEEP_US 950000UL  // 0,95 с

#define NIGHT_START_H 23
#define NIGHT_END_H 7
#define SYNC_DAYS 4
#define SYNC_PERIOD_SEC (SYNC_DAYS * 24UL * 3600UL)

// ---------- батарея ----------
#define BAT_V_MAX 4.0f
#define BAT_V_MIN 3.0f
#define BAT_STEPS 5
#define BATTERY_RECHECK_SEC (15UL * 60UL)
#define OLED_BUFFER_SIZE ((SCREEN_WIDTH * SCREEN_HEIGHT) / 8)

#define SHOW_DEBUG_CODES 0

// ---------- коды сообщений ----------
#define CODE_WIFI_CONNECT "A1"    // подключение к Wi-Fi
#define CODE_WIFI_FAIL "A2"       // Wi-Fi недоступен
#define CODE_NTP_SYNC "B1"        // процесс синхронизации NTP
#define CODE_NTP_OK "B2"          // время успешно синхронизировано
#define CODE_NTP_ERROR "B3"       // ошибка NTP
#define CODE_SENSOR_OK "C1"       // датчик SHT31 найден
#define CODE_SENSOR_MISSING "C2"  // датчик SHT31 отсутствует
#define CODE_FIRST_SYNC "D1"      // первая синхронизация
#define CODE_SETUP_DONE "D2"      // завершение setup
#define CODE_MEASURE_INFO "E1"    // минутное измерение батареи

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_SHT31 sht31 = Adafruit_SHT31();
WiFiUDP ntpUDP;
const char *ntpServers[] = {
  "time2.google.com",
  "ntp1.vniiftri.ru",
  "0.pool.ntp.org",
  "pool.ntp.org",
  "ntp.nsu.ru",
  "time.google.com",
  "time1.facebook.com",
  "time1.google.com",
  "time.aws.com",
  "amazon.pool.ntp.org",
  "time.facebook.com",
  "time.cloudflare.com",
  "time.windows.com",
  "time2.facebook.com",
};
const size_t ntpServerCount = sizeof(ntpServers) / sizeof(ntpServers[0]);
RTC_DATA_ATTR size_t ntpServerIndex = 0;
NTPClient timeClient(ntpUDP, ntpServers[0], 3 * 3600, 60000);

RTC_DATA_ATTR time_t lastSyncEpoch = 0;
RTC_DATA_ATTR time_t storedEpoch = 0;
RTC_DATA_ATTR float storedVBat = BAT_V_MAX;
RTC_DATA_ATTR uint8_t storedBatBars = BAT_STEPS;
RTC_DATA_ATTR time_t lastBatCheckEpoch = 0;
RTC_DATA_ATTR uint16_t storedRawAdc = 0;
RTC_DATA_ATTR uint8_t displayBackup[OLED_BUFFER_SIZE];
RTC_DATA_ATTR bool displayBackupValid = false;
RTC_DATA_ATTR int32_t driftCorrectionMs = 0;
RTC_DATA_ATTR time_t lastSyncLocalEpoch = 0;

static bool sensorOK = false;
static float tempC = 22.0;
static float hum = 50.0;
static bool displayOn = true;

// ---------- утилиты ----------
bool isNight(int h) {
  if (NIGHT_START_H < NIGHT_END_H) {
    return h >= NIGHT_START_H && h < NIGHT_END_H;
  }
  return h >= NIGHT_START_H || h < NIGHT_END_H;
}

bool hasValidTime(time_t epoch) {
  return epoch > 100000;
}

time_t applyDriftCorrection(time_t baseEpoch, time_t referenceEpoch) {
  if (driftCorrectionMs == 0 || referenceEpoch == 0) {
    return baseEpoch;
  }
  time_t elapsed = baseEpoch - referenceEpoch;
  if (elapsed <= 0) {
    return baseEpoch;
  }
  int64_t correctionSeconds = ((int64_t)elapsed * driftCorrectionMs) / 1000000LL;
  return baseEpoch + (time_t)correctionSeconds;
}

void setCpuLowPower() {
  setCpuFrequencyMhz(40);
}

void setCpuPerformance() {
  setCpuFrequencyMhz(80);
}

void setDisplayState(bool on) {
  if (displayOn == on) {
    return;
  }
  display.ssd1306_command(on ? 0xAF : 0xAE);
  displayOn = on;
}

bool readSHT31SingleShot(float &t, float &h) {
  Wire.beginTransmission(SHT31_ADDR);
  Wire.write(0x24);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) {
    return false;
  }
  delay(15);
  if (Wire.requestFrom(SHT31_ADDR, (uint8_t)6) != 6) {
    return false;
  }
  uint8_t data[6];
  for (uint8_t i = 0; i < 6; ++i) {
    data[i] = Wire.read();
  }
  uint16_t rawT = ((uint16_t)data[0] << 8) | data[1];
  uint16_t rawH = ((uint16_t)data[3] << 8) | data[4];
  t = -45.0f + 175.0f * (float)rawT / 65535.0f;
  h = 100.0f * (float)rawH / 65535.0f;
  return true;
}

void sht31SoftReset() {
  Wire.beginTransmission(SHT31_ADDR);
  Wire.write(0x30);
  Wire.write(0xA2);
  Wire.endTransmission();
  delay(2);
}

const uint8_t font5x8[][8] = {
  { 0b11111,
    0b10001,
    0b10001,
    0b10001,
    0b10001,
    0b10001,
    0b10001,
    0b00000 },  // П
  {
    0b10001,
    0b10001,
    0b10001,
    0b01111,
    0b00001,
    0b00001,
    0b00001,
    0b00000 },  // Ч
  {
    0b11111,
    0b10000,
    0b10000,
    0b11110,
    0b10001,
    0b10001,
    0b11110,
    0b00000 }  // Б
};

void drawDayShort(uint8_t wday, int16_t x, int16_t y) {
  wday = wday % 7;

  switch (wday) {
    case 1:                                                       // ПН
      display.drawBitmap(x, y, font5x8[0], 8, 8, SSD1306_WHITE);  // П
      display.setCursor(x + 10, y);
      display.print("H");
      break;
    case 2:  // ВТ
      display.setCursor(x + 4, y);
      display.print("BT");
      break;
    case 3:  // CP
      display.setCursor(x + 4, y);
      display.print("CP");
      break;
    case 4:  // ЧТ
      display.drawBitmap(x, y, font5x8[1], 8, 8, SSD1306_WHITE);
      display.setCursor(x + 10, y);
      display.print("T");
      break;
    case 5:                                                       // ПТ - ok
      display.drawBitmap(x, y, font5x8[0], 8, 8, SSD1306_WHITE);  // П
      display.setCursor(x + 10, y);
      display.print("T");
      break;
    case 6:  // СБ
      display.setCursor(x + 4, y);
      display.print("C");
      display.drawBitmap(x + 6, y, font5x8[2], 8, 8, SSD1306_WHITE);  // Б
      break;
    case 0:  // ВС
      display.setCursor(x + 4, y);
      display.print("BC");
      break;
  }
}

void setBrightness(uint8_t br) {
  display.ssd1306_command(0x81);
  display.ssd1306_command(br);
}

float readBattery() {
  uint32_t mv = analogReadMilliVolts(BAT_PIN);
  return mv * 2.0f / 1000.0f;  // делитель 1:1 → Вольты
}

void drawBattery(uint8_t bars) {
  for (uint8_t i = 0; i < bars; i++) {
    uint8_t x = 2 + i * 6;
    display.fillRect(x, 0, 4, 2, SSD1306_WHITE);
  }
}

void drawClock(int d, int mo, int h, int m, uint8_t batBars, uint8_t wday) {
  display.clearDisplay();
  drawBattery(batBars);
  display.setTextSize(1);
  display.setCursor(0, 8);
  display.printf("%02d.%02d", d, mo);

  drawDayShort(wday, 6, 22);

  display.drawLine(0, 36, 128, 36, SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(5, 48);
  display.printf("%02d", h);
  display.setCursor(5, 73);
  display.printf("%02d", m);

  display.drawLine(0, 98, 128, 98, SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(6, 107);
  display.print((int)tempC);
  display.print((char)247);
  display.print("C");
  display.setCursor(9, 120);
  display.printf("%d%%", (int)hum);
  display.display();
  memcpy(displayBackup, display.getBuffer(), OLED_BUFFER_SIZE);
  displayBackupValid = true;
}

void logToDisplay(const char *code, const char *detail = nullptr, uint16_t holdMs = 1000) {
  if (!SHOW_DEBUG_CODES) {
    return;
  }
  setDisplayState(true);
  setBrightness(0x01);
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.print(code);
  if (detail != nullptr) {
    display.setTextSize(1);
    display.setCursor(0, 24);
    display.print(detail);
  }
  display.display();
  if (holdMs > 0) {
    delay(holdMs);
  }
}

bool ntpSync() {
  logToDisplay(CODE_WIFI_CONNECT, nullptr, 0);
  setCpuPerformance();

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startAttempt) < 30000UL) {
    delay(250);
  }
  if (WiFi.status() != WL_CONNECTED) {
    logToDisplay(CODE_WIFI_FAIL);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    setCpuLowPower();
    return false;
  }
  logToDisplay(CODE_NTP_SYNC, nullptr, 0);
  timeClient.begin();
  timeClient.setPoolServerName(ntpServers[ntpServerIndex]);
  bool ok = timeClient.forceUpdate();
  if (ok) {
    time_t ntpEpoch = timeClient.getEpochTime();
    time_t localRawEpoch = storedEpoch;
    
    if (lastSyncEpoch > 0 && lastSyncLocalEpoch > 0 && localRawEpoch > lastSyncLocalEpoch) {
      time_t ntpElapsed = ntpEpoch - lastSyncEpoch;
      time_t localElapsed = localRawEpoch - lastSyncLocalEpoch;
      if (ntpElapsed > 3600) {
        int64_t driftMs = ((int64_t)(ntpElapsed - localElapsed) * 1000000LL) / (int64_t)ntpElapsed;
        driftCorrectionMs = (int32_t)driftMs;
      }
    }
    
    lastSyncEpoch = ntpEpoch;
    lastSyncLocalEpoch = ntpEpoch;
    storedEpoch = ntpEpoch;
    logToDisplay(CODE_NTP_OK);
  } else {
    logToDisplay(CODE_NTP_ERROR);
    ntpServerIndex = (ntpServerIndex + 1) % ntpServerCount;
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  setCpuLowPower();
  return ok;
}

uint32_t runCycle() {
  uint32_t cycleStartMs = millis();
  time_t local = applyDriftCorrection(storedEpoch, lastSyncLocalEpoch);
  bool timeValid = hasValidTime(local);
  struct tm ti = {};
  if (timeValid) {
    localtime_r(&local, &ti);
  }

  bool needSync = !timeValid;
  if (timeValid && lastSyncLocalEpoch > 0 && (storedEpoch - lastSyncLocalEpoch) >= SYNC_PERIOD_SEC) {
    needSync = true;
  }

  if (needSync) {
    if (!timeValid) {
      logToDisplay(CODE_FIRST_SYNC);
    }
    if (ntpSync()) {
      local = applyDriftCorrection(storedEpoch, lastSyncLocalEpoch);
      timeValid = hasValidTime(local);
      if (timeValid) {
        localtime_r(&local, &ti);
      }
    }
  }

  if (sensorOK) {
    float t = tempC;
    float h = hum;
    if (readSHT31SingleShot(t, h) && !isnan(t) && !isnan(h)) {
      tempC = t;
      hum = h;
    }
    sht31.heater(false);
    sht31SoftReset();
  }

  float vBat = storedVBat;
  uint8_t batBars = storedBatBars;
  bool needBatteryUpdate = !timeValid || (lastBatCheckEpoch == 0);
  if (timeValid && lastBatCheckEpoch != 0) {
    needBatteryUpdate = (local - lastBatCheckEpoch) >= BATTERY_RECHECK_SEC;
  }

  if (needBatteryUpdate) {
    float measured = readBattery();
    uint16_t rawADC = analogRead(BAT_PIN);
    storedRawAdc = rawADC;

    int mappedValue = map(
      (int)(measured * 100),
      (int)(BAT_V_MIN * 100),
      (int)(BAT_V_MAX * 100),
      0,
      BAT_STEPS);

    storedVBat = measured;
    storedBatBars = constrain(mappedValue, 0, BAT_STEPS);
    vBat = storedVBat;
    batBars = storedBatBars;
    if (timeValid) {
      lastBatCheckEpoch = local;
    }
  } else {
    vBat = storedVBat;
    batBars = storedBatBars;
  }

#if SHOW_DEBUG_CODES
  char detail[32];
  snprintf(detail, sizeof(detail), "ADC%u V%.2f B%d/%d", storedRawAdc, vBat, batBars, BAT_STEPS);
  logToDisplay(CODE_MEASURE_INFO, detail, 400);
#endif

  bool night = timeValid && isNight(ti.tm_hour);

  if (!timeValid) {
    logToDisplay(CODE_NTP_ERROR, "Wait NTP", 0);
  } else if (!night) {
    setDisplayState(true);
    setBrightness(0x01);
    drawClock(ti.tm_mday, ti.tm_mon + 1, ti.tm_hour, ti.tm_min, batBars, ti.tm_wday);
  } else {
    display.clearDisplay();
    display.display();
    setDisplayState(false);
  }

  if (timeValid && (ti.tm_min == 0) && !night) {
    digitalWrite(LED_PIN, HIGH);
    delay(80);
    digitalWrite(LED_PIN, LOW);
  }

  uint32_t sleepSeconds = 60;
  if (timeValid) {
    int secToNextMinute = 60 - ti.tm_sec;
    if (secToNextMinute <= 0) {
      secToNextMinute = 60;
    }
    sleepSeconds = (uint32_t)secToNextMinute;
    uint32_t activeSeconds = ((millis() - cycleStartMs) + 500) / 1000;
    storedEpoch = storedEpoch + activeSeconds + sleepSeconds;
  } else {
    sleepSeconds = 30;
  }

  return sleepSeconds;
}

void enterDeepSleep(uint32_t sleepSeconds) {
  if (sleepSeconds == 0) {
    sleepSeconds = 60;
  }
  esp_sleep_enable_timer_wakeup((uint64_t)sleepSeconds * 1000000ULL);
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(9600);
  Serial.println("Setup started");
  setCpuLowPower();
  delay(100);
  analogSetPinAttenuation(BAT_PIN, ADC_11db);
  pinMode(BAT_PIN, INPUT);
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    pinMode(LED_PIN, OUTPUT);
    for (;;) {
      digitalWrite(LED_PIN, HIGH);
      delay(200);
      digitalWrite(LED_PIN, LOW);
      delay(200);
    }
  }
  display.setRotation(1);
  display.setTextColor(SSD1306_WHITE);
  display.clearDisplay();
  setBrightness(0x01);
  if (displayBackupValid) {
    memcpy(display.getBuffer(), displayBackup, OLED_BUFFER_SIZE);
    display.display();
  } else {
    display.display();
  }

  sensorOK = sht31.begin(SHT31_ADDR);

  logToDisplay(sensorOK ? CODE_SENSOR_OK : CODE_SENSOR_MISSING);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  uint32_t sleepSeconds = runCycle();
  enterDeepSleep(sleepSeconds);
}

void loop() {
  // Не используется: устройство просыпается из deep sleep и сразу выполняет setup()
}