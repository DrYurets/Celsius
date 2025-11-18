#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_SHT31.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <esp_sleep.h>

// ---------- настройки ----------
#define WIFI_SSID       "WiFi_SSID"
#define WIFI_PASSWORD   "WiFi_Password"
#define I2C_SDA         8
#define I2C_SCL         9
#define OLED_ADDR       0x3C
#define SHT31_ADDR      0x44
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   32
#define LED_PIN         0

// ночь 00:00-07:00   (0 часов начало, 7 часов конец)
#define NIGHT_START_H   0
#define NIGHT_END_H     7

// синхронизация 1 раз в 4 суток
#define SYNC_DAYS       4
#define SYNC_PERIOD_SEC (SYNC_DAYS*24UL*3600UL)

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_SHT31 sht31 = Adafruit_SHT31();
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3 * 3600, 60000);

static bool sensorOK = false;
static float tempC = 22.0, hum = 50.0;
static uint32_t lastSyncEpoch = 0;
static uint8_t lastMin = 99;

// ---------- ночь ----------
bool isNight(int h) { return h >= NIGHT_START_H && h < NIGHT_END_H; }
void oledOff() { display.ssd1306_command(0xAE); }   // DISPLAY OFF
void oledOn () { display.ssd1306_command(0xAF); }   // DISPLAY ON

// ---------- рисуем экран (яркость = 1) ----------
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

  display.ssd1306_command(0x81);   // SETCONTRAST
  display.ssd1306_command(0x01);   // 1 / 255
  display.display();
}

// ---------- Wi-Fi + NTP ----------
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

// ---------- setup ----------
void setup() {
  Serial.begin(115200); delay(100);
  Wire.begin(I2C_SDA, I2C_SCL); Wire.setClock(100000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    pinMode(LED_PIN, OUTPUT);
    for (;;) {
      digitalWrite(LED_PIN, HIGH); delay(200);
      digitalWrite(LED_PIN, LOW);  delay(200);
    }
  }
  display.setRotation(1); display.setTextColor(SSD1306_WHITE);
  display.clearDisplay(); display.display();

  sensorOK = sht31.begin(SHT31_ADDR);
  Serial.printf("SHT31: %s\n", sensorOK ? "OK" : "MISSING");

  pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, LOW);

  // первичная синхронизация
  Serial.println("Initial sync...");
  ntpSync();
  Serial.println("Setup complete");
}

// ---------- main loop ----------
void loop() {
  // 1. берём время (без .update() - Wi-Fi выключен)
  time_t local = timeClient.getEpochTime();
  struct tm ti; gmtime_r(&local, &ti);
  uint8_t min = ti.tm_min;

  // 2. синхронизация 1 раз в 4 суток в 00 мин
  if (min == 0 && (local - lastSyncEpoch) >= SYNC_PERIOD_SEC) ntpSync();

  // 3. обработка минуты
  if (min != lastMin) {
    lastMin = min;

    if (sensorOK) {
      float t = sht31.readTemperature(), h = sht31.readHumidity();
      if (!isnan(t) && !isnan(h)) { tempC = t; hum = h; }
    }

    bool night = isNight(ti.tm_hour);
    if (night) {
      oledOff();
      Serial.printf("🌙 %02d:%02d  OLED OFF\n", ti.tm_hour, min);
    } else {
      oledOn();
      drawClock(ti.tm_mday, ti.tm_mon + 1, ti.tm_hour, min);
      Serial.printf("🕗 %02d:%02d  %.1f°C  %.0f%%\n", ti.tm_hour, min, tempC, hum);
    }

    // светодиод только в 00 мин и только днём
    if (min == 0 && !night) {
      digitalWrite(LED_PIN, HIGH); delay(50);
      digitalWrite(LED_PIN, LOW);  delay(50);
      digitalWrite(LED_PIN, HIGH); delay(50);
      digitalWrite(LED_PIN, LOW);
    }
  }

  esp_sleep_enable_timer_wakeup(950000);
  esp_light_sleep_start();
}