#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_SHT31.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>

const char* ssid = "WiFi_SSID";
const char* password = "WiFi_Password";

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_ADDR 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

Adafruit_SHT31 sht31 = Adafruit_SHT31();

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3 * 3600, 60000); // GMT+3

#define I2C_SDA 8
#define I2C_SCL 9
#define LED_PIN 0

#define NIGHT_START_SEC (22 * 3600) // 22:00
#define NIGHT_END_SEC (7 * 3600 + 30 * 60) // 07:30
#define BRIGHTNESS_MAX 255
#define BRIGHTNESS_NIGHT 128 // 50%
#define SHT31_ADDR 0x44
#define SENS_READ_INTERVAL 2000

bool hourBlinked = false;
unsigned long ledOffTime = 0;
unsigned long lastSensorRead = 0;
float cachedTempC = 0.0;
float cachedHum = 0.0;
bool sensorAvailable = false;
int lastDisplayedSeconds = -1;
int lastDisplayedMinutes = -1;
int lastDisplayedHours = -1;
int lastDisplayedDate = -1;
bool isNightMode = false;

void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);

  WiFi.begin(ssid, password);
  int wifiTimeout = 0;
  while (WiFi.status() != WL_CONNECTED && wifiTimeout < 30) {
    delay(500);
    Serial.print(".");
    wifiTimeout++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
  } else {
    Serial.println("\nWiFi connection failed - continuing anyway");
  }

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;) yield();
  }
  display.clearDisplay();
  display.setRotation(1);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.display();

  sensorAvailable = sht31.begin(SHT31_ADDR);
  if (!sensorAvailable) {
    Serial.println("Couldn't find SHT31 - continuing anyway");
  }

  timeClient.begin();
  if (WiFi.status() == WL_CONNECTED) {
    delay(1000);
    timeClient.forceUpdate();
  }

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  updateDisplayBrightness();
}

void updateDisplayBrightness() {
  int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();
  bool newNightMode = isNightTime(currentHour, currentMinute);

  if (newNightMode != isNightMode) {
    isNightMode = newNightMode;
    uint8_t contrast = newNightMode ? BRIGHTNESS_NIGHT : BRIGHTNESS_MAX;
    display.ssd1306_command(SSD1306_SETCONTRAST);
    display.ssd1306_command(contrast);
    Serial.print("Brightness updated. New mode: ");
    Serial.println(newNightMode ? "Night" : "Day");
  }
}

bool isNightTime(int hours, int minutes) {
  int currentSecs = hours * 3600 + minutes * 60;
  if (NIGHT_START_SEC <= NIGHT_END_SEC) {
    return (currentSecs >= NIGHT_START_SEC || currentSecs < NIGHT_END_SEC);
  } else {
    return (currentSecs >= NIGHT_START_SEC && currentSecs < NIGHT_END_SEC);
  }
}

void readSensor() {
    if (millis() - lastSensorRead >= SENS_READ_INTERVAL) {
        lastSensorRead = millis();
        float temp = sht31.readTemperature();
        float hum = sht31.readHumidity();

        if (!isnan(temp) && !isnan(hum) && temp >= -40 && temp <= 125 && hum >= 0 && hum <= 100) {
            cachedTempC = temp;
            cachedHum = hum;
        }
    }
}

void updateDisplay(int hours, int minutes, int seconds, int day, int month) {
  char hStr[3], mStr[3], sStr[3], dateStr[6];
  sprintf(hStr, "%02d", hours);
  sprintf(mStr, "%02d", minutes);
  sprintf(sStr, "%02d", seconds);
  sprintf(dateStr, "%02d.%02d", day, month);

  display.clearDisplay();

  for (int i = 0; i < 5; i++) {
    int xStart = i * 7;
    display.drawLine(xStart, 0, xStart + 3, 0, SSD1306_WHITE);
  }

  display.setCursor(0, 7);
  display.println(dateStr);
  display.drawLine(0, 20, 128, 20, SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(5, 30);
  display.println(hStr);
  display.setCursor(5, 52);
  display.println(mStr);
  display.setCursor(5, 72);
  display.println(sStr);

  display.drawLine(0, 95, 128, 95, SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(5, 105);
  display.print((int)cachedTempC);
  display.print((char)247);
  display.print("C");
  display.setCursor(5, 120);
  display.print((int)cachedHum);
  display.print("%");

  display.display();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    timeClient.update();
  }

  int hours = timeClient.getHours();
  int minutes = timeClient.getMinutes();
  int seconds = timeClient.getSeconds();

  time_t epochTime = timeClient.getEpochTime() + (3 * 3600);
  struct tm *timeinfo = localtime(&epochTime);
  if (timeinfo != NULL) {
    int day = timeinfo->tm_mday;
    int month = timeinfo->tm_mon + 1;

    if (hours != lastDisplayedHours) {
        updateDisplayBrightness();
        lastDisplayedHours = hours;
    }

    if (seconds != lastDisplayedSeconds) {
        if (sensorAvailable) {
            readSensor();
        }
        updateDisplay(hours, minutes, seconds, day, month);
        lastDisplayedSeconds = seconds;
        lastDisplayedMinutes = minutes;
        int currentDate = day * 100 + month;
        if (currentDate != lastDisplayedDate) {
            lastDisplayedDate = currentDate;
        }
    }
  }

  if (minutes == 0 && seconds == 0) {
    if (!hourBlinked) {
      if (!isNightMode) {
        digitalWrite(LED_PIN, HIGH);
        ledOffTime = millis() + 20;
      }
      hourBlinked = true;
    }
  } else {
    hourBlinked = false;
  }

  if (digitalRead(LED_PIN) == HIGH && millis() >= ledOffTime) {
    digitalWrite(LED_PIN, LOW);
  }

  yield();
}