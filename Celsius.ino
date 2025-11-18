// ------------------------------------------------------------------
// Celsius Clock  →  35-40 суток, LIGHT-SLEEP 950 мс
// Яркость 0 ночью, 1 днём – экран не гаснет полностью
// ------------------------------------------------------------------
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_SHT31.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <esp_sleep.h>

#define WIFI_SSID       "WiFi_SSID"
#define WIFI_PASSWORD   "WiFi_Password"
#define I2C_SDA         8
#define I2C_SCL         9
#define OLED_ADDR       0x3C
#define SHT31_ADDR      0x44
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   32
#define LED_PIN         0

#define NIGHT_START_H   0
#define NIGHT_END_H     7
#define SYNC_DAYS       4
#define SYNC_PERIOD_SEC (SYNC_DAYS*24UL*3600UL)
#define SLEEP_US        950000UL   // 0,95 с

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_SHT31 sht31 = Adafruit_SHT31();
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3 * 3600, 60000);

static uint32_t lastSyncEpoch = 0;
static uint8_t  lastMin       = 99;

static bool sensorOK = false;
static float tempC   = 22.0;
static float hum     = 50.0;

bool isNight(int h) { return h >= NIGHT_START_H && h < NIGHT_END_H; }

void setBrightness(uint8_t br) {   // 0 = почти чёрный, 1 = минимум
  display.ssd1306_command(0x81);
  display.ssd1306_command(br);
}

void drawClock(int d, int mo, int h, int m) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 7);   display.printf("%02d.%02d", d, mo);
  display.drawLine(0, 20, 128, 20, SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(5, 30);  display.printf("%02d", h);
  display.setCursor(5, 52);  display.printf("%02d", m);
  display.drawLine(0, 95, 128, 95, SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(5, 105);
  display.print((int)tempC); display.print((char)247); display.print("C");
  display.setCursor(5, 120); display.printf("%d%%", (int)hum);
  display.display();
}

bool ntpSync() {
  Serial.print("NTP sync... ");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; ++i) {
    delay(500); Serial.print('.');
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(" fail");
    WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
    return false;
  }
  timeClient.begin();
  bool ok = timeClient.forceUpdate();
  if (ok) {
    lastSyncEpoch = timeClient.getEpochTime();
    Serial.println(" OK");
  } else {
    Serial.println(" error");
  }
  WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
  return ok;
}

void setup() {
  Serial.begin(115200); delay(100);
  Wire.begin(I2C_SDA, I2C_SCL); Wire.setClock(100000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    pinMode(LED_PIN, OUTPUT);
    for (;;) { digitalWrite(LED_PIN, HIGH); delay(200); digitalWrite(LED_PIN, LOW); delay(200); }
  }
  display.setRotation(1); display.setTextColor(SSD1306_WHITE);
  display.clearDisplay(); display.display();

  sensorOK = sht31.begin(SHT31_ADDR);
  Serial.printf("SHT31: %s\n", sensorOK ? "OK" : "MISSING");

  pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, LOW);

  if (lastSyncEpoch == 0) {
    Serial.println("Initial sync...");
    ntpSync();
  }
  Serial.println("Setup complete");
}

void loop() {
  timeClient.update();               // можно вызывать всегда (Wi-Fi выключен)
  time_t local = timeClient.getEpochTime();
  struct tm ti; gmtime_r(&local, &ti);
  uint8_t min = ti.tm_min;

  // синхронизация 1 раз в 4 суток в 00 мин
  if (min == 0 && (local - lastSyncEpoch) >= SYNC_PERIOD_SEC) ntpSync();

  if (min != lastMin) {
    lastMin = min;

    if (sensorOK) {
      float t = sht31.readTemperature(), h = sht31.readHumidity();
      if (!isnan(t) && !isnan(h)) { tempC = t; hum = h; }
    }

    bool night = isNight(ti.tm_hour);

    // ★ ЯРКОСТЬ: 0 ночью, 1 днём – экран не гаснет, просто «чёрный»
    setBrightness(night ? 0 : 1);

    if (!night) {
      drawClock(ti.tm_mday, ti.tm_mon + 1, ti.tm_hour, min);
      Serial.printf("🕗 %02d:%02d  %.1f°C  %.0f%%\n", ti.tm_hour, min, tempC, hum);
    } else {
      Serial.printf("🌙 %02d:%02d  DIMMED\n", ti.tm_hour, min);
    }

    // светодиод только в 00 мин и только днём
    if (min == 0 && !night) {
      digitalWrite(LED_PIN, HIGH); delay(50);
      digitalWrite(LED_PIN, LOW);  delay(50);
      digitalWrite(LED_PIN, HIGH); delay(50);
      digitalWrite(LED_PIN, LOW);
    }
  }

  esp_sleep_enable_timer_wakeup(SLEEP_US);
  esp_light_sleep_start();   // ← не гасит OLED, просто спит 0,95 с
}
// ------------------------------------------------------------------