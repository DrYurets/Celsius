#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_SHT31.h>
#include <WiFi.h>
#include <time.h>  // Стандартная библиотека времени ESP32
#include <esp_sleep.h>
#include <driver/adc.h>
#include <esp_wifi.h>

#define WIFI_SSID "WiFi_SSID"
#define WIFI_PASSWORD "WiFi_Password"

// Настройки времени (UTC+3 для Москвы, измените при необходимости)
#define GMT_OFFSET_SEC (3 * 3600)
#define DAYLIGHT_OFFSET_SEC 0
#define NTP_SERVER1 "pool.ntp.org"
#define NTP_SERVER2 "time.nist.gov"

#define I2C_SDA 8
#define I2C_SCL 9
#define OLED_ADDR 0x3C
#define SHT31_ADDR 0x44
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define LED_PIN 0
#define BAT_PIN 3          // GPIO 3

#define NIGHT_START_H 0
#define NIGHT_END_H 7
#define SYNC_DAYS 4
#define SYNC_PERIOD_SEC (SYNC_DAYS * 24UL * 3600UL)
#define SYNC_RETRY_SEC (30UL * 60UL)

#define WIFI_CONNECT_TIMEOUT_MS 10000UL
#define WIFI_MAX_STATUS_POLLS 20

// ---------- батарея ----------
#define BAT_V_MAX 420  // 4.20V
#define BAT_V_MIN 300  // 3.00V
#define BAT_SAFE_WIFI 340 // Минимальное напряжение для включения WiFi (3.40V)
#define BAT_STEPS 5
#define LOW_BAT_LED_BARS 2

#define SENSOR_MAX_FAILS 3

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_SHT31 sht31 = Adafruit_SHT31();

// Глобальные переменные состояния
static bool sensorOK = false;
static float tempC = 22.0;
static float hum = 50.0;
static uint8_t sensorFailCount = 0;

RTC_DATA_ATTR time_t lastSyncTime = 0;
RTC_DATA_ATTR time_t lastSyncAttempt = 0;
RTC_DATA_ATTR time_t cachedEpoch = 0;
RTC_DATA_ATTR uint32_t lastSleepDuration = 60;
RTC_DATA_ATTR bool hasValidTime = false;

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

// ---------- утилиты ----------
bool isNight(int h) {
  return h >= NIGHT_START_H && h < NIGHT_END_H;
}

float readBattery() {
  adc_power_on();
  uint32_t mv = analogReadMilliVolts(BAT_PIN);
  adc_power_off();
  return mv * 2.0f / 1000.0f;  // делитель 1:1 → Вольты
}

void drawDayShort(uint8_t wday, int16_t x, int16_t y) {
  wday = wday % 7;

  switch (wday) {
    case 0:  // ВС
      display.setCursor(x, y);
      display.print("BC");
      break;
    case 1:                                                       // ПН
      display.drawBitmap(x, y, font5x8[0], 8, 8, SSD1306_WHITE);  // П
      display.setCursor(x + 10, y);
      display.print("H");
      break;
    case 2:  // ВТ
      display.setCursor(x, y);
      display.print("BT");
      break;
    case 3:  // CP
      display.setCursor(x, y);
      display.print("CP");
      break;
    case 4:  // ЧТ
      display.drawBitmap(x, y, font5x8[1], 8, 8, SSD1306_WHITE);
      display.setCursor(x + 10, y);
      display.print("T");
      break;
    case 5:                                                       // ПТ
      display.drawBitmap(x, y, font5x8[0], 8, 8, SSD1306_WHITE);  // П
      display.setCursor(x + 10, y);
      display.print("T");
      break;
    case 6:  // СБ
      display.setCursor(x, y);
      display.print("C");
      display.drawBitmap(x + 10, y, font5x8[2], 8, 8, SSD1306_WHITE);  // Б
      break;
  }
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

  display.drawLine(0, 38, 128, 38, SSD1306_WHITE);

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
}

void setDisplayState(bool on) {
  static bool displayEnabled = true;
  if (displayEnabled == on) {
    return;
  }
  display.ssd1306_command(on ? 0xAF : 0xAE);
  displayEnabled = on;
}

bool ntpSync() {
  time_t attemptEpoch = cachedEpoch;
  if (attemptEpoch == 0) {
    time(&attemptEpoch);
  }
  lastSyncAttempt = attemptEpoch;

  // Проверка батареи перед включением WiFi
  if ((int)(readBattery() * 100) < BAT_SAFE_WIFI) {
    return false;
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startAttempt) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
  }

  bool success = false;
  if (WiFi.status() == WL_CONNECTED) {
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER1, NTP_SERVER2);
    for (int i = 0; i < WIFI_MAX_STATUS_POLLS; i++) {
      time_t now;
      time(&now);
      struct tm timeinfo;
      localtime_r(&now, &timeinfo);
      if (timeinfo.tm_year > (2020 - 1900)) {
        cachedEpoch = now;
        hasValidTime = true;
        lastSyncTime = now;
        success = true;
        break;
      }
      delay(400);
    }
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  btStop();  // гарантируем что BT выключен после WiFi
  return success;
}

bool ensureLocalTime(struct tm &ti) {
  if (getLocalTime(&ti, 0)) {
    time_t nowEpoch = mktime(&ti);
    if (nowEpoch > 0) {
      cachedEpoch = nowEpoch;
      hasValidTime = true;
      if (lastSyncTime == 0) {
        lastSyncTime = nowEpoch;
      }
    }
    return true;
  }

  if (!hasValidTime) {
    if (ntpSync() && getLocalTime(&ti, 0)) {
      time_t nowEpoch = mktime(&ti);
      if (nowEpoch > 0) {
        cachedEpoch = nowEpoch;
        hasValidTime = true;
        lastSyncTime = nowEpoch;
      }
      return true;
    }
    return false;
  }

  if (cachedEpoch > 0) {
    cachedEpoch += lastSleepDuration;
    localtime_r(&cachedEpoch, &ti);
    return true;
  }

  return false;
}

void lightSleepSeconds(uint32_t seconds) {
  if (seconds == 0) {
    seconds = 1;
  }
  lastSleepDuration = seconds;
  Serial.flush();
  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  esp_light_sleep_start();
}

void setup() {
  // 1. Отключаем BT для экономии
  btStop();

  Serial.begin(115200);
  setCpuFrequencyMhz(80);
  delay(100);

  // 2. Периферия
  analogSetPinAttenuation(BAT_PIN, ADC_11db);
  pinMode(BAT_PIN, INPUT);
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);

  // ВАЖНО: Инициализация дисплея
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("SSD1306 allocation failed"));
    // Если дисплея нет, нет смысла продолжать, уходим в глубокий сон
    esp_deep_sleep_start();
  }

  display.setRotation(1);
  display.setTextColor(SSD1306_WHITE);
  display.clearDisplay();
  display.display();
  display.ssd1306_command(0x81);  // SETCONTRAST
  display.ssd1306_command(0x01);  // 1/255
  setDisplayState(true);

  sensorOK = sht31.begin(SHT31_ADDR);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // 3. Настройка времени
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER1, NTP_SERVER2);

  struct tm timeinfo;
  if (!ensureLocalTime(timeinfo)) {
    Serial.println(F("Time not available yet"));
  }
}

void loop() {
  struct tm ti;

  if (!ensureLocalTime(ti)) {
    // Времени нет совсем — делаем краткий сон и повторяем
    lightSleepSeconds(10);
    return;
  }

  // --- Чтение датчиков и батареи ---
  if (!sensorOK) {
    sensorOK = sht31.begin(SHT31_ADDR);
  }

  if (sensorOK) {
    float t = sht31.readTemperature();
    float h = sht31.readHumidity();
    if (!isnan(t) && !isnan(h)) {
      tempC = t;
      hum = h;
      sensorFailCount = 0;
    } else {
      sensorFailCount++;
      if (sensorFailCount >= SENSOR_MAX_FAILS) {
        sensorOK = sht31.begin(SHT31_ADDR);
        sensorFailCount = 0;
      }
    }
  }

  float vBat = readBattery();
  int mappedValue = map(
      (int)(vBat * 100),
      BAT_V_MIN,
      BAT_V_MAX,
      0,
      BAT_STEPS);
  uint8_t batBars = constrain(mappedValue, 0, BAT_STEPS);

  // --- Отображение ---
  bool night = isNight(ti.tm_hour);
  if (!night) {
    setDisplayState(true);
    drawClock(ti.tm_mday, ti.tm_mon + 1, ti.tm_hour, ti.tm_min, batBars, ti.tm_wday);
  } else {
    display.clearDisplay();
    display.display();
    setDisplayState(false);
  }

  // --- Светодиодная индикация (каждый час) ---
  if (ti.tm_min == 0 && !night && batBars > LOW_BAT_LED_BARS) {
    digitalWrite(LED_PIN, HIGH);
    delay(40);
    digitalWrite(LED_PIN, LOW);
    delay(40);
    digitalWrite(LED_PIN, HIGH);
    delay(40);
    digitalWrite(LED_PIN, LOW);
  }

  // --- Проверка необходимости синхронизации NTP ---
  time_t nowEpoch = cachedEpoch;
  if (nowEpoch > 0 &&
      (nowEpoch - lastSyncTime) >= SYNC_PERIOD_SEC &&
      (nowEpoch - lastSyncAttempt) >= SYNC_RETRY_SEC) {
    ntpSync();
  }

  // --- Умный сон до следующей минуты ---
  int secondsToSleep = 60 - ti.tm_sec;
  if (secondsToSleep <= 0) {
    secondsToSleep = 1;
  }
  lightSleepSeconds(secondsToSleep);
}