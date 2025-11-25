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
#include <WebServer.h>
#include <EEPROM.h>

#define AP_SSID "CelsiusClock"
#define AP_PASSWORD "12345678"
#define EEPROM_SIZE 256
#define EEPROM_SSID_ADDR 0
#define EEPROM_PASS_ADDR 64
#define EEPROM_SETTINGS_ADDR 128
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
#define CODE_WIFI_CONNECT "A1"     // подключение к Wi-Fi
#define CODE_WIFI_FAIL "A2"        // Wi-Fi недоступен
#define CODE_NTP_SYNC "B1"         // процесс синхронизации NTP
#define CODE_NTP_OK "B2"           // время успешно синхронизировано
#define CODE_NTP_ERROR "B3"        // ошибка NTP
#define CODE_SENSOR_OK "C1"        // датчик SHT31 найден
#define CODE_SENSOR_MISSING "C2"   // датчик SHT31 отсутствует
#define CODE_FIRST_SYNC "D1"       // первая синхронизация
#define CODE_SETUP_DONE "D2"       // завершение setup
#define CODE_MEASURE_INFO "E1"     // минутное измерение батареи
#define CODE_CONFIG_MODE "F1"      // режим настройки активирован
#define CODE_CONFIG_AP_START "F2"  // точка доступа запущена
#define CODE_CONFIG_SAVED "F3"     // настройки сохранены
#define CODE_CPU_FREQ "G1"         // частота процессора
#define CODE_WIFI_CONFIG_OK "H1"   // настройки WiFi найдены
#define CODE_WIFI_CONFIG_MISS "H2"  // настройки WiFi не найдены
#define CODE_WIFI_CONFIG_ERR "H3"   // ошибка чтения настроек
#define CODE_CONFIG_RESET "I1"      // сброс настроек

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_SHT31 sht31 = Adafruit_SHT31();
WiFiUDP ntpUDP;
WebServer server(80);
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

static char wifiSSID[64] = "";
static char wifiPassword[64] = "";
static bool configMode = false;

// Структура настроек устройства
struct DeviceSettings {
  bool showDebugCodes;
  bool showDate;
  bool showWeekday;
  bool timeFormat24h;
  bool hourlyBlink;
  uint8_t nightStartH;
  uint8_t nightStartM;
  uint8_t nightEndH;
  uint8_t nightEndM;
  bool weekdayLanguageRu;  // true = Russian, false = English
};

static DeviceSettings settings = {
  .showDebugCodes = false,
  .showDate = true,
  .showWeekday = true,
  .timeFormat24h = true,
  .hourlyBlink = true,
  .nightStartH = 23,
  .nightStartM = 0,
  .nightEndH = 7,
  .nightEndM = 0,
  .weekdayLanguageRu = true
};

static bool sensorOK = false;
static float tempC = 22.0;
static float hum = 50.0;
static bool displayOn = true;

// ---------- утилиты ----------
bool isNight(int h, int m = 0) {
  int startMinutes = settings.nightStartH * 60 + settings.nightStartM;
  int endMinutes = settings.nightEndH * 60 + settings.nightEndM;
  int currentMinutes = h * 60 + m;
  
  if (startMinutes < endMinutes) {
    return currentMinutes >= startMinutes && currentMinutes < endMinutes;
  }
  return currentMinutes >= startMinutes || currentMinutes < endMinutes;
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

void logToDisplay(const char *code, const char *detail = nullptr, uint16_t holdMs = 1000) {
  if (!settings.showDebugCodes) {
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


// ---------- EEPROM функции ----------
void loadWiFiConfig() {
  EEPROM.begin(EEPROM_SIZE);
  wifiSSID[0] = '\0';
  wifiPassword[0] = '\0';
  
  // Читаем SSID
  for (int i = 0; i < 63; i++) {
    char c = (char)EEPROM.read(EEPROM_SSID_ADDR + i);
    wifiSSID[i] = c;
    if (c == '\0') break;
  }
  wifiSSID[63] = '\0';
  
  // Читаем пароль
  for (int i = 0; i < 63; i++) {
    char c = (char)EEPROM.read(EEPROM_PASS_ADDR + i);
    wifiPassword[i] = c;
    if (c == '\0') break;
  }
  wifiPassword[63] = '\0';
  
  EEPROM.end();
}

void saveWiFiConfig(const char *ssid, const char *password) {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < 63; i++) {
    EEPROM.write(EEPROM_SSID_ADDR + i, ssid[i]);
    if (ssid[i] == '\0') break;
  }
  EEPROM.write(EEPROM_SSID_ADDR + 63, '\0');
  for (int i = 0; i < 63; i++) {
    EEPROM.write(EEPROM_PASS_ADDR + i, password[i]);
    if (password[i] == '\0') break;
  }
  EEPROM.write(EEPROM_PASS_ADDR + 63, '\0');
  EEPROM.commit();
  EEPROM.end();
}

bool hasWiFiConfig() {
  loadWiFiConfig();
  // Проверяем, что SSID не пустой и имеет разумную длину (1-32 символа для SSID)
  size_t ssidLen = strlen(wifiSSID);
  return (ssidLen > 0 && ssidLen < 33);
}

void clearWiFiConfig() {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < 64; i++) {
    EEPROM.write(EEPROM_SSID_ADDR + i, 0);
    EEPROM.write(EEPROM_PASS_ADDR + i, 0);
  }
  EEPROM.commit();
  EEPROM.end();
  wifiSSID[0] = '\0';
  wifiPassword[0] = '\0';
}

void loadSettings() {
  EEPROM.begin(EEPROM_SIZE);
  uint8_t *data = (uint8_t*)&settings;
  for (size_t i = 0; i < sizeof(DeviceSettings); i++) {
    data[i] = EEPROM.read(EEPROM_SETTINGS_ADDR + i);
  }
  EEPROM.end();
  
  // Проверка валидности (магическое число)
  if (settings.nightStartH > 23 || settings.nightEndH > 23 || 
      settings.nightStartM > 59 || settings.nightEndM > 59) {
    // Настройки невалидны, используем значения по умолчанию
    settings.showDebugCodes = false;
    settings.showDate = true;
    settings.showWeekday = true;
    settings.timeFormat24h = true;
    settings.hourlyBlink = true;
    settings.nightStartH = 23;
    settings.nightStartM = 0;
    settings.nightEndH = 7;
    settings.nightEndM = 0;
    settings.weekdayLanguageRu = true;
  }
}

void saveSettings() {
  EEPROM.begin(EEPROM_SIZE);
  uint8_t *data = (uint8_t*)&settings;
  for (size_t i = 0; i < sizeof(DeviceSettings); i++) {
    EEPROM.write(EEPROM_SETTINGS_ADDR + i, data[i]);
  }
  EEPROM.commit();
  EEPROM.end();
}

// ---------- веб-сервер функции ----------
String getConfigPage() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Celsius Clock - Setup</title>";
  html += "<style>";
  html += "body { font-family: Arial; margin: 20px; background: #1a1a1a; color: #fff; }";
  html += ".container { max-width: 400px; margin: 0 auto; background: #2a2a2a; padding: 20px; border-radius: 10px; }";
  html += "h1 { text-align: center; color: #4CAF50; }";
  html += "input[type=text], input[type=password], input[type=number] { width: 100%; padding: 10px; margin: 10px 0; border: 1px solid #555; border-radius: 5px; background: #333; color: #fff; box-sizing: border-box; }";
  html += "input[type=checkbox] { width: 20px; height: 20px; margin-right: 10px; }";
  html += "button { width: 100%; padding: 12px; background: #4CAF50; color: white; border: none; border-radius: 5px; font-size: 16px; cursor: pointer; }";
  html += "button:hover { background: #45a049; }";
  html += "label { display: block; margin-top: 10px; }";
  html += ".checkbox-label { display: flex; align-items: center; margin: 10px 0; }";
  html += ".time-group { display: flex; gap: 10px; }";
  html += ".time-group input { width: 48%; }";
  html += "h2 { color: #4CAF50; margin-top: 20px; margin-bottom: 10px; font-size: 18px; }";
  html += "</style></head><body>";
  html += "<div class='container'>";
  html += "<h1>Celsius Clock Setup</h1>";
  html += "<form method='POST' action='/save'>";
  
  html += "<h2>WiFi Settings</h2>";
  html += "<label>WiFi SSID:</label>";
  html += "<input type='text' name='ssid' value='" + String(wifiSSID) + "' required>";
  html += "<label>WiFi Password:</label>";
  html += "<input type='password' name='password' value='" + String(wifiPassword) + "' required>";
  
  html += "<h2>Display Settings</h2>";
  html += "<div class='checkbox-label'><input type='checkbox' name='showDebugCodes' " + String(settings.showDebugCodes ? "checked" : "") + "><label>Show debug codes</label></div>";
  html += "<div class='checkbox-label'><input type='checkbox' name='showDate' " + String(settings.showDate ? "checked" : "") + "><label>Show date</label></div>";
  html += "<div class='checkbox-label'><input type='checkbox' name='showWeekday' " + String(settings.showWeekday ? "checked" : "") + "><label>Show weekday</label></div>";
  html += "<div class='checkbox-label'><input type='checkbox' name='timeFormat24h' " + String(settings.timeFormat24h ? "checked" : "") + "><label>24-hour format</label></div>";
  html += "<div class='checkbox-label'><input type='checkbox' name='hourlyBlink' " + String(settings.hourlyBlink ? "checked" : "") + "><label>Hourly LED blink</label></div>";
  html += "<div class='checkbox-label'><input type='checkbox' name='weekdayLanguageRu' " + String(settings.weekdayLanguageRu ? "checked" : "") + "><label>Weekday in Russian</label></div>";
  
  html += "<h2>Night Mode</h2>";
  html += "<label>Night start time:</label>";
  html += "<div class='time-group'>";
  html += "<input type='number' name='nightStartH' min='0' max='23' value='" + String(settings.nightStartH) + "' required>";
  html += "<input type='number' name='nightStartM' min='0' max='59' value='" + String(settings.nightStartM) + "' required>";
  html += "</div>";
  html += "<label>Night end time:</label>";
  html += "<div class='time-group'>";
  html += "<input type='number' name='nightEndH' min='0' max='23' value='" + String(settings.nightEndH) + "' required>";
  html += "<input type='number' name='nightEndM' min='0' max='59' value='" + String(settings.nightEndM) + "' required>";
  html += "</div>";
  
  html += "<button type='submit' style='margin-top: 20px;'>Save and Reset</button>";
  html += "</form>";
  html += "<hr style='margin: 20px 0; border-color: #555;'>";
  html += "<form method='POST' action='/reset' style='margin-top: 20px;'>";
  html += "<button type='submit' style='background: #f44336;'>Reset Settings</button>";
  html += "</form>";
  html += "</div></body></html>";
  return html;
}

void handleRoot() {
  server.send(200, "text/html", getConfigPage());
}

void handleReset() {
  clearWiFiConfig();
  logToDisplay(CODE_CONFIG_RESET);
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Settings reset</title>";
  html += "<style>body { font-family: Arial; text-align: center; margin-top: 50px; background: #1a1a1a; color: #fff; }";
  html += ".message { background: #2a2a2a; padding: 20px; border-radius: 10px; max-width: 400px; margin: 0 auto; }";
  html += "h1 { color: #f44336; }</style></head><body>";
  html += "<div class='message'><h1>Settings Reset!</h1>";
  html += "<p>Resetting...</p></div></body></html>";
  server.send(200, "text/html", html);
  
  delay(2000);
  ESP.restart();
}

void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("password")) {
    String ssid = server.arg("ssid");
    String password = server.arg("password");

    // Обрезаем пробелы
    ssid.trim();
    password.trim();
    
    if (ssid.length() == 0 || ssid.length() >= 64 || password.length() >= 64) {
      server.send(400, "text/plain", "Error: Invalid SSID or password length");
      return;
    }

    ssid.toCharArray(wifiSSID, 64);
    password.toCharArray(wifiPassword, 64);
    
    // Убеждаемся, что строки заканчиваются нулем
    wifiSSID[63] = '\0';
    wifiPassword[63] = '\0';
    
    // Обработка настроек устройства
    settings.showDebugCodes = server.hasArg("showDebugCodes");
    settings.showDate = server.hasArg("showDate");
    settings.showWeekday = server.hasArg("showWeekday");
    settings.timeFormat24h = server.hasArg("timeFormat24h");
    settings.hourlyBlink = server.hasArg("hourlyBlink");
    settings.weekdayLanguageRu = server.hasArg("weekdayLanguageRu");
    
    if (server.hasArg("nightStartH")) {
      int h = server.arg("nightStartH").toInt();
      int m = server.arg("nightStartM").toInt();
      if (h >= 0 && h <= 23 && m >= 0 && m <= 59) {
        settings.nightStartH = h;
        settings.nightStartM = m;
      }
    }
    
    if (server.hasArg("nightEndH")) {
      int h = server.arg("nightEndH").toInt();
      int m = server.arg("nightEndM").toInt();
      if (h >= 0 && h <= 23 && m >= 0 && m <= 59) {
        settings.nightEndH = h;
        settings.nightEndM = m;
      }
    }
    
    saveWiFiConfig(wifiSSID, wifiPassword);
    saveSettings();
    
    // Проверяем, что настройки сохранились
    loadWiFiConfig();
    
    if (strlen(wifiSSID) == 0 || strcmp(wifiSSID, ssid.c_str()) != 0) {
      char detail[32];
      snprintf(detail, sizeof(detail), "len=%d", strlen(wifiSSID));
      logToDisplay(CODE_WIFI_CONFIG_ERR, detail);
      server.send(500, "text/plain", "Error: Failed to save settings");
      delay(2000);
      return;
    }
    
    logToDisplay(CODE_CONFIG_SAVED);

    String html = "<!DOCTYPE html><html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>Settings saved</title>";
    html += "<style>body { font-family: Arial; text-align: center; margin-top: 50px; background: #1a1a1a; color: #fff; }";
    html += ".message { background: #2a2a2a; padding: 20px; border-radius: 10px; max-width: 400px; margin: 0 auto; }";
    html += "h1 { color: #4CAF50; }</style></head><body>";
    html += "<div class='message'><h1>Saved!</h1>";
    html += "<p>Resetting...</p></div></body></html>";
    server.send(200, "text/html", html);

    // Увеличиваем задержку перед перезагрузкой, чтобы EEPROM.commit() точно завершился
    delay(2000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Error: SSID or password missed");
  }
}

void updateConfigModeDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Setup mode");
  display.setCursor(0, 32);
  display.print("SSID: ");
  display.println(AP_SSID);
  display.setCursor(0, 64);
  display.print("IP: ");
  display.println(WiFi.softAPIP());
  display.display();
}

void startConfigMode() {
  logToDisplay(CODE_CONFIG_MODE);
  configMode = true;
  setCpuMaxPerformance();

  char detail[32];
  snprintf(detail, sizeof(detail), "%d MHz", getCpuFrequencyMhz());
  logToDisplay(CODE_CPU_FREQ, detail);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  snprintf(detail, sizeof(detail), "%s", WiFi.softAPIP().toString().c_str());
  logToDisplay(CODE_CONFIG_AP_START, detail);

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/reset", HTTP_POST, handleReset);
  server.begin();

  updateConfigModeDisplay();
}

void setCpuLowPower() {
  setCpuFrequencyMhz(40);
}

void setCpuPerformance() {
  setCpuFrequencyMhz(80);
}

void setCpuMaxPerformance() {
  setCpuFrequencyMhz(160);
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

  if (settings.weekdayLanguageRu) {
    // Русский язык
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
      case 5:                                                       // ПТ
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
  } else {
    // Английский язык
    const char* days[] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
    display.setCursor(x + 4, y);
    display.print(days[wday]);
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
  
  int yPos = 8;
  
  if (settings.showDate) {
    display.setTextSize(1);
    display.setCursor(0, yPos);
    display.printf("%02d.%02d", d, mo);
    yPos += 14;
  }

  if (settings.showWeekday) {
    drawDayShort(wday, 6, yPos);
    yPos += 14;
  }

  display.drawLine(0, 36, 128, 36, SSD1306_WHITE);

  // Формат времени
  int displayH = h;
  if (!settings.timeFormat24h) {
    displayH = h % 12;
    if (displayH == 0) displayH = 12;
  }

  display.setTextSize(2);
  display.setCursor(5, 48);
  display.printf("%02d", displayH);
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

bool ntpSync() {
  logToDisplay(CODE_WIFI_CONNECT, nullptr, 0);
  setCpuPerformance();

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.begin(wifiSSID, wifiPassword);

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

  bool night = timeValid && isNight(ti.tm_hour, ti.tm_min);

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

  if (timeValid && (ti.tm_min == 0) && !night && settings.hourlyBlink) {
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
  
  // Сразу гасим светодиод, чтобы избежать вспышки при пробуждении
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  setCpuLowPower();
  delay(100);
  
  // Проверка сброса настроек: если LED_PIN (GPIO 0) замкнут на землю при старте
  // Кратковременно переключаем на вход для проверки
  pinMode(LED_PIN, INPUT_PULLUP);
  delayMicroseconds(100);  // Минимальная задержка для стабилизации
  bool resetRequested = (digitalRead(LED_PIN) == LOW);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  if (resetRequested) {
    // GPIO был замкнут на землю - сбрасываем настройки
    clearWiFiConfig();
    logToDisplay(CODE_CONFIG_RESET, "GPIO reset");
    delay(2000);
  }
  
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

  // Загрузка настроек устройства
  loadSettings();

  // Проверка настроек WiFi
  if (!hasWiFiConfig()) {
    char detail[32];
    snprintf(detail, sizeof(detail), "len=%d", strlen(wifiSSID));
    logToDisplay(CODE_WIFI_CONFIG_MISS, detail);
    // Режим настройки - запускаем веб-сервер
    startConfigMode();
    return;  // Не переходим в обычный режим
  }
  
  char detail[32];
  snprintf(detail, sizeof(detail), "%s", wifiSSID);
  logToDisplay(CODE_WIFI_CONFIG_OK, detail);
  
  // Настройки есть - продолжаем в обычном режиме
  // WiFi подключение будет происходить при необходимости (синхронизация NTP)
  // Не переходим в режим настройки, даже если WiFi временно недоступен

  // Обычный режим работы
  uint32_t sleepSeconds = runCycle();
  enterDeepSleep(sleepSeconds);
}

void loop() {
  if (configMode) {
    // Обработка запросов веб-сервера в режиме настройки
    server.handleClient();
    delay(10);
  } else {
    // Не используется: устройство просыпается из deep sleep и сразу выполняет setup()
  }
}