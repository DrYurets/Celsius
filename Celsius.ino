#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_SHT31.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h> 

// WiFi network name (SSID)
const char* ssid = "WiFi_SSID";
// WiFi network password
const char* password = "WiFi_Password";

// OLED display width in pixels
#define SCREEN_WIDTH 128
// OLED display height in pixels
#define SCREEN_HEIGHT 32
// I2C address of the OLED display (common: 0x3C or 0x3D)
#define OLED_ADDR 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

Adafruit_SHT31 sht31 = Adafruit_SHT31();

// Timezone offset in seconds from UTC (e.g., GMT+3 = 3*3600, GMT-5 = -5*3600)
#define TIMEZONE_OFFSET_SEC (3 * 3600)
// Interval for forced NTP updates (e.g., once an hour)
#define NTP_UPDATE_INTERVAL_MS (3600000UL) // 1 hour

WiFiUDP ntpUDP;
// NTP server, timezone offset (0 for UTC), update interval (ms for NTPClient internal time)
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000); 

// SDA pin for I2C communication
#define I2C_SDA 8
// SCL pin for I2C communication
#define I2C_SCL 9
// Pin connected to the indicator LED
#define LED_PIN 0

// Start of night mode: Hour * 3600 (e.g., 22:00 = 22*3600 sec from 00:00)
#define NIGHT_START_SEC (22 * 3600)
// End of night mode: Hour * 3600 + Minute * 60 (e.g., 07:30 = 7*3600 + 30*60 sec)
#define NIGHT_END_SEC (7 * 3600 + 30 * 60)

// Maximum display brightness (0-255)
#define BRIGHTNESS_MAX 10
// Brightness level during night mode (0-255)
#define BRIGHTNESS_NIGHT 1 

// I2C address of the SHT31 sensor (common: 0x44 or 0x45)
#define SHT31_ADDR 0x44
// Interval in milliseconds between sensor readings
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
int lastDisplayedDay = -1;
int lastDisplayedMonth = -1;

bool isNightMode = false;
unsigned long lastNtpUpdateTime = 0; 

// Forward declarations
void updateDisplayBrightness();
bool isNightTime(int hours, int minutes);
void readSensor();
void updateDisplay(int hours, int minutes, int seconds, int day, int month);

void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);

  Serial.print("Connecting to WiFi ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  int wifiTimeout = 0;
  const int maxWifiAttempts = 30;
  while (WiFi.status() != WL_CONNECTED && wifiTimeout < maxWifiAttempts) {
    delay(500);
    Serial.print(".");
    wifiTimeout++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
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
  } else {
    Serial.println("SHT31 sensor found and initialized");
  }

  configTime(TIMEZONE_OFFSET_SEC, 0, "pool.ntp.org");
  
  timeClient.begin();
  if (WiFi.status() == WL_CONNECTED) {
    delay(1000); 
    unsigned long ntpStart = millis();
    if (timeClient.forceUpdate()) { 
      Serial.println("NTP time updated successfully");
      lastNtpUpdateTime = millis();
    } else {
      Serial.println("Failed to update NTP time");
    }
    Serial.print("Time taken for NTP update: ");
    Serial.print(millis() - ntpStart);
    Serial.println(" ms");
  }

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  updateDisplayBrightness(); 
  Serial.println("Setup complete.");
}

void updateDisplayBrightness() {
  int currentHour = lastDisplayedHours; 
  int currentMinute = lastDisplayedMinutes; 
  
  if (currentHour == -1 || currentMinute == -1) return;

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
  if (NIGHT_START_SEC > NIGHT_END_SEC) {
    return (currentSecs >= NIGHT_START_SEC || currentSecs < NIGHT_END_SEC);
  }
  else {
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
            Serial.print("Sensor read: ");
            Serial.print(cachedTempC);
            Serial.print(" C, ");
            Serial.print(cachedHum);
            Serial.println(" %");
        } else {
            Serial.println("Invalid sensor data, keeping cached values.");
        }
    }
}

void updateDisplay(int hours, int minutes, int seconds, int day, int month) {
  if (seconds == lastDisplayedSeconds && minutes == lastDisplayedMinutes && 
      hours == lastDisplayedHours && day == lastDisplayedDay && month == lastDisplayedMonth) {
      return; 
  }

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

  display.setTextSize(1);
  display.setCursor(0, 7); 
  display.println(dateStr);

  display.drawLine(0, 20, SCREEN_WIDTH - 1, 20, SSD1306_WHITE);

  display.setTextSize(2); 
  display.setCursor(5, 30);
  display.println(hStr);

  display.setCursor(5, 52);
  display.println(mStr);

  display.setCursor(5, 72);
  display.println(sStr);

  display.drawLine(0, 95, SCREEN_WIDTH - 1, 95, SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(5, 105);
  if (sensorAvailable) {
      display.print((int)cachedTempC);
      display.print((char)247); 
      display.print("C");
  } else {
      display.println("--.-C"); 
  }
  
  display.setCursor(5, 120);
  if (sensorAvailable) {
      display.print((int)cachedHum);
      display.print("%");
  } else {
      display.println("---%"); 
  }

  display.display();

  lastDisplayedSeconds = seconds;
  lastDisplayedMinutes = minutes;
  lastDisplayedHours = hours;
  lastDisplayedDay = day;
  lastDisplayedMonth = month;
}


void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    timeClient.update(); 
    if (millis() - lastNtpUpdateTime >= NTP_UPDATE_INTERVAL_MS) {
        if (timeClient.forceUpdate()) { 
            Serial.println("Forced NTP update successful.");
            lastNtpUpdateTime = millis();
        } else {
            Serial.println("Forced NTP update failed.");
        }
    }
  }

  time_t currentEpochTimeUTC = timeClient.getEpochTime();

  struct tm *localTimeinfo = localtime(&currentEpochTimeUTC);
  
  int hours = -1;
  int minutes = -1;
  int seconds = -1;
  int day = -1;
  int month = -1;

  if (localTimeinfo != NULL) {
    hours = localTimeinfo->tm_hour;
    minutes = localTimeinfo->tm_min;
    seconds = localTimeinfo->tm_sec;
    day = localTimeinfo->tm_mday;
    month = localTimeinfo->tm_mon + 1; 
  } else {
      Serial.println("Error converting UTC epoch time to local date structure.");
  }

  if (hours != lastDisplayedHours) {
      updateDisplayBrightness();
  }

  if (sensorAvailable) {
      readSensor();
  }

  if (day != -1 && month != -1 && hours != -1) { 
      updateDisplay(hours, minutes, seconds, day, month);

      if (minutes == 0 && seconds == 0) {
          if (!hourBlinked) {
              if (!isNightMode) { 
                  digitalWrite(LED_PIN, HIGH);
                  ledOffTime = millis() + 20; 
                  Serial.println("Hourly blink! (Not in night mode)");
              } else {
                  Serial.println("Hourly time reached, but skipping blink in night mode.");
              }
              hourBlinked = true;
          }
      } else {
          hourBlinked = false;
      }
  } else {
      Serial.println("Skipping display update due to invalid date/time from NTP.");
      hourBlinked = false; 
  }

  if (digitalRead(LED_PIN) == HIGH && millis() >= ledOffTime) {
    digitalWrite(LED_PIN, LOW);
  }

  yield(); 
}