#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_SHT31.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <esp_sleep.h>

// ===== Настройки =====
#define WIFI_SSID     "WiFi_SSID"
#define WIFI_PASSWORD "WiFi_Password"
#define I2C_SDA       8
#define I2C_SCL       9
#define OLED_ADDR     0x3C
#define SHT31_ADDR    0x44
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 32
#define LED_PIN       0          // вместо LED_BUILTIN

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_SHT31 sht31 = Adafruit_SHT31();
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3 * 3600, 60000);

static time_t baseTime = 1747306800UL;   // 15 May 2025 12:00 MSK
static uint32_t lastMinute = 99;
static float tempC = 22.0, hum = 50.0;
static bool sensorOK = false;

// ================================================================
void drawClock(int day, int month, int hour, int minute)
{
  display.clearDisplay();

  // Date
  display.setTextSize(1);
  display.setCursor(0, 7);
  display.printf("%02d.%02d", day, month);
  display.drawLine(0, 20, 128, 20, SSD1306_WHITE);

  // Time
  display.setTextSize(2);
  display.setCursor(5, 30); display.printf("%02d", hour);
  display.setCursor(5, 52); display.printf("%02d", minute);
  display.drawLine(0, 95, 128, 95, SSD1306_WHITE);

  // Sensor
  display.setTextSize(1);
  display.setCursor(5, 105);
  //display.printf("%d%sC", (int)tempC);
  display.print((int)tempC);
  display.print((char)247); 
  display.print("C");

  display.setCursor(5, 120);
  display.printf("%d%%", (int)hum);

  display.ssd1306_command(0x81); // максимальная контрастность
  display.ssd1306_command(0xFF);
  display.display();
}
// ================================================================
void setup()
{
  Serial.begin(115200);
  delay(100);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("❌ OLED init failed");
    // СВЕТОДИОДНАЯ ИНДИКАЦИЯ ОШИБКИ (была в вечном цикле)
    pinMode(LED_PIN, OUTPUT);
    for (;;) {
      digitalWrite(LED_PIN, HIGH); delay(200);
      digitalWrite(LED_PIN, LOW);  delay(200);
    }
  }
  display.setRotation(1);
  display.setTextColor(SSD1306_WHITE);
  display.clearDisplay(); display.display();

  // SHT31
  sensorOK = sht31.begin(SHT31_ADDR);
  Serial.printf("🌡️ SHT31: %s\n", sensorOK ? "OK" : "MISSING");

  // Wi-Fi & NTP
  Serial.print("📶 Connecting ");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; ++i) {
    delay(500); Serial.print('.');
  }
  if (WiFi.status() == WL_CONNECTED) {
    timeClient.begin();
    if (timeClient.update()) {
      baseTime = timeClient.getEpochTime();
      Serial.println("\n✅ NTP synced");
    } else {
      Serial.println("\n⚠️ NTP failed – using fallback");
    }
    WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
  } else {
    Serial.println("\n❌ Wi-Fi failed – using fallback");
  }

  pinMode(LED_PIN, OUTPUT);
  Serial.println("✅ Setup complete");
}
// ================================================================
void loop()
{
  static uint32_t lastMin = 99;
  timeClient.update();               // достаточно вызвать каждую секунду
  time_t local = timeClient.getEpochTime();

  struct tm ti;
  gmtime_r(&local, &ti);
  int minute = ti.tm_min;

  if (minute != lastMin) {                // каждую новую минуту
    lastMin = minute;

    if (sensorOK) {
      float t = sht31.readTemperature();
      float h = sht31.readHumidity();
      if (!isnan(t) && !isnan(h)) { tempC = t; hum = h; }
    }

    drawClock(ti.tm_mday, ti.tm_mon + 1, ti.tm_hour, minute);
    Serial.printf("🕗 %02d:%02d  %.1f°C  %.0f%%\n",
                  ti.tm_hour, minute, tempC, hum);

    // мигаем светодиодом в начале часа
    if (minute == 0) {
      digitalWrite(LED_PIN, HIGH);
      delay(50);
      digitalWrite(LED_PIN, LOW);
    }
  }

  // Light-sleep ~950 мс
  esp_sleep_enable_timer_wakeup(950000);
  esp_light_sleep_start();
}