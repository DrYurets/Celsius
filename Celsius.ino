#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_SHT31.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <esp_sleep.h>
#include <driver/adc.h>

#define WIFI_SSID       "WiFi_SSID"
#define WIFI_PASSWORD   "WiFi_Password"
#define I2C_SDA         8
#define I2C_SCL         9
#define OLED_ADDR       0x3C
#define SHT31_ADDR      0x44
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   32
#define LED_PIN         0
#define BAT_PIN         3            // GPIO 3
#define SLEEP_US        950000UL     // 0,95 с

#define NIGHT_START_H   0
#define NIGHT_END_H     7
#define SYNC_DAYS       4
#define SYNC_PERIOD_SEC (SYNC_DAYS*24UL*3600UL)

// ---------- батарея ----------
#define BAT_V_MAX       4.2f
#define BAT_V_MIN       3.0f
#define BAT_STEPS       5

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_SHT31 sht31 = Adafruit_SHT31();
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3 * 3600, 60000);

static uint32_t lastSyncEpoch = 0;
static uint8_t  lastMin       = 99;
static bool     sensorOK      = false;
static float    tempC         = 22.0;
static float    hum           = 50.0;

// ---------- утилиты ----------
bool isNight(int h) { return h >= NIGHT_START_H && h < NIGHT_END_H; }

void setBrightness(uint8_t br) {
  display.ssd1306_command(0x81);
  display.ssd1306_command(br);
}

float readBattery() {
  uint32_t mv = analogReadMilliVolts(BAT_PIN);
  return mv * 2.0f / 1000.0f;
}

void drawBattery(uint8_t bars) {
  for (uint8_t i = 0; i < bars; i++) {
    uint8_t x = 2 + i * 6;
    display.fillRect(x, 0, 4, 2, SSD1306_WHITE);
  }
}

void drawClock(int d, int mo, int h, int m, uint8_t batBars) {
  display.clearDisplay();
  drawBattery(batBars);
  display.setTextSize(1);
  display.setCursor(0, 8);   display.printf("%02d.%02d", d, mo);
  display.drawLine(0, 23, 128, 23, SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(5, 36);  display.printf("%02d", h);
  display.setCursor(5, 65);  display.printf("%02d", m);
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
  Serial.begin(115200);
  delay(100);
  analogSetPinAttenuation(BAT_PIN, ADC_11db);   // 0…2,5 В
  pinMode(BAT_PIN, INPUT);                         // АЦП
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
  timeClient.update();
  time_t local = timeClient.getEpochTime();
  struct tm ti; localtime_r(&local, &ti);
  uint8_t min = ti.tm_min;

  if (min == 0 && (local - lastSyncEpoch) >= SYNC_PERIOD_SEC) ntpSync();

  if (min != lastMin) {
    lastMin = min;

    if (sensorOK) {
      float t = sht31.readTemperature(), h = sht31.readHumidity();
      if (!isnan(t) && !isnan(h)) { tempC = t; hum = h; }
    }

    float vBat = readBattery();
    uint16_t rawADC = analogRead(BAT_PIN);
    Serial.printf("Raw ADC: %d, Calculated Vbat: %.3f V\n", rawADC, vBat);

    int mappedValue = map(
        (int)(vBat * 100),
        (int)(BAT_V_MIN * 100),
        (int)(BAT_V_MAX * 100),
        0,
        BAT_STEPS
    );
    
    uint8_t batBars = constrain(mappedValue, 0, BAT_STEPS);

    Serial.printf("Mapped Value: %d, Final BatBars: %d\n", mappedValue, batBars);

    bool night = isNight(ti.tm_hour);
    setBrightness(night ? 0 : 1);

    if (!night) {
      drawClock(ti.tm_mday, ti.tm_mon + 1, ti.tm_hour, min, batBars);
      Serial.printf("... %02d:%02d  %.1f°C  %.0f%%  %d.%02d V, Bars: %d\n",
                    ti.tm_hour, min, tempC, hum, (int)vBat, (int)(vBat * 100) % 100, batBars);
    } else {
      Serial.printf("... DIMMED ... V, Bars: %d\n", batBars);
    }

    if (min == 0 && !night) {
      digitalWrite(LED_PIN, HIGH); delay(50);
      digitalWrite(LED_PIN, LOW);  delay(50);
      digitalWrite(LED_PIN, HIGH); delay(50);
      digitalWrite(LED_PIN, LOW);
    }
  }

  esp_sleep_enable_timer_wakeup(SLEEP_US);
  esp_light_sleep_start();
}