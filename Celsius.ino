#include <Wire.h>
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

#define NIGHT_START_H 0
#define NIGHT_END_H 7
#define SYNC_DAYS 4
#define SYNC_PERIOD_SEC (SYNC_DAYS * 24UL * 3600UL)

// ---------- батарея ----------
#define BAT_V_MAX 4.0f
#define BAT_V_MIN 3.0f
#define BAT_STEPS 5

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
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3 * 3600, 60000);

static uint32_t lastSyncEpoch = 0;
static uint8_t lastMin = 99;
static bool sensorOK = false;
static float tempC = 22.0;
static float hum = 50.0;
static bool displayOn = true;

// ---------- утилиты ----------
bool isNight(int h) {
  return h >= NIGHT_START_H && h < NIGHT_END_H;
}

bool hasValidTime(time_t epoch) {
  return epoch > 100000;
}

void setDisplayState(bool on) {
  if (displayOn == on) {
    return;
  }
  display.ssd1306_command(on ? 0xAF : 0xAE);
  displayOn = on;
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
    case 5:
      display.drawBitmap(x, y, font5x8[0], 8, 8, SSD1306_WHITE);  // П
      display.setCursor(x + 10, y);
      display.print("T");
      break;
    case 6:  // СБ
      display.setCursor(x, y);
      display.print("C");
      display.drawBitmap(x + 10, y, font5x8[2], 8, 8, SSD1306_WHITE);  // Б
      break;
    case 0:  // ВС
      display.setCursor(x, y);
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

void logToDisplay(const char *code, const char *detail = nullptr, uint16_t holdMs = 1000) {
  setDisplayState(true);
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
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; ++i) {
    delay(500);
  }
  if (WiFi.status() != WL_CONNECTED) {
    logToDisplay(CODE_WIFI_FAIL);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
  }
  logToDisplay(CODE_NTP_SYNC, nullptr, 0);
  timeClient.begin();
  bool ok = timeClient.forceUpdate();
  if (ok) {
    lastSyncEpoch = timeClient.getEpochTime();
    logToDisplay(CODE_NTP_OK);
  } else {
    logToDisplay(CODE_NTP_ERROR);
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  return ok;
}

void setup() {
  Serial.begin(115200);
  setCpuFrequencyMhz(80);
  delay(100);
  analogSetPinAttenuation(BAT_PIN, ADC_11db);  // 0…2,5 В
  pinMode(BAT_PIN, INPUT);                     // АЦП
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
  display.display();

  sensorOK = sht31.begin(SHT31_ADDR);
  logToDisplay(sensorOK ? CODE_SENSOR_OK : CODE_SENSOR_MISSING);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  lastSyncEpoch = 0;

  if (lastSyncEpoch == 0) {
    logToDisplay(CODE_FIRST_SYNC);
    ntpSync();
  }

  logToDisplay(CODE_SETUP_DONE, nullptr, 800);
}

void loop() {
  timeClient.update();
  time_t local = timeClient.getEpochTime();
  bool timeValid = hasValidTime(local);
  struct tm ti;
  localtime_r(&local, &ti);
  uint8_t min = ti.tm_min;

  if (min == 0 && (local - lastSyncEpoch) >= SYNC_PERIOD_SEC) ntpSync();

  if (min != lastMin) {
    lastMin = min;
    if (lastSyncEpoch == 0) {
      ntpSync();
    }

    if (sensorOK) {
      float t = sht31.readTemperature(), h = sht31.readHumidity();
      if (!isnan(t) && !isnan(h)) {
        tempC = t;
        hum = h;
      }
    }

    float vBat = readBattery();
    uint16_t rawADC = analogRead(BAT_PIN);

    int mappedValue = map(
      (int)(vBat * 100),
      (int)(BAT_V_MIN * 100),
      (int)(BAT_V_MAX * 100),
      0,
      BAT_STEPS);
    uint8_t batBars = constrain(mappedValue, 0, BAT_STEPS);

#if SHOW_DEBUG_CODES
    char detail[32];
    snprintf(detail, sizeof(detail), "ADC%u V%.2f B%d/%d", rawADC, vBat, batBars, BAT_STEPS);
    logToDisplay(CODE_MEASURE_INFO, detail, 600);
#endif

    bool night = timeValid && isNight(ti.tm_hour);

    if (!timeValid) {
      logToDisplay(CODE_NTP_ERROR, "Wait NTP", 0);
    } else if (!night) {
      setDisplayState(true);
      drawClock(ti.tm_mday, ti.tm_mon + 1, ti.tm_hour, min, batBars, ti.tm_wday);
    } else {
      display.clearDisplay();
      display.display();
      setDisplayState(false);
    }

    if (min == 0 && !night) {
      digitalWrite(LED_PIN, HIGH);
      delay(80);
      digitalWrite(LED_PIN, LOW);
    }
  }

  esp_sleep_enable_timer_wakeup(SLEEP_US);
  esp_light_sleep_start();
}