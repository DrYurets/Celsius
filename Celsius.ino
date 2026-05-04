/*
* Celsius Clock (ESP32-C3)
* https://github.com/DrYurets/Celsius/tree/aht20bmp280
* 
* Date: 22.04.2026
* Copyright (c) 2026 DrYurets
*/

#include <Wire.h>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <GyverOLED.h>
#include <Adafruit_SHT31.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_HTU21DF.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <esp_sleep.h>
#include <esp_ota_ops.h>
#include <driver/adc.h>
#include <driver/gpio.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <Update.h>
#include <AutoOTA.h>
#include <ArduinoJson.h>
#include "sensors/bmi160/BMI160Motion.h"
#include "sensors/SensorTypes.h"
#include "sensors/SensorManager.h"
#include "WeatherAPI.h"
#include "WeatherDetailScreens.h"

#define AP_SSID "CelsiusClock"
#define AP_PASSWORD "12345678"
#define ROM_VERSION "A1.2.9"
#define EEPROM_SSID_ADDR 0
#define EEPROM_PASS_ADDR 64
#define EEPROM_SETTINGS_ADDR 128
#define EEPROM_SIZE 2048
#define I2C_SDA 8
#define I2C_SCL 9
#define OLED_ADDR 0x3C
#define SSD1306_WHITE 1
#define SSD1306_SWITCHCAPVCC 0
#define SHT31_ADDR 0x44
#define AHT20_ADDR 0x38
#define BMP280_ADDR 0x76
#define BMP280_ADDR_ALT 0x77
#define SCREEN_WIDTH 128   // физическое разрешение OLED (SSD1306 128×64)
#define SCREEN_HEIGHT 64   // в drawClock используется setRotation(1) → логически 64×128
#define LED_PIN 0
#define SETUP_BUTTON_PIN 1
#define WEATHER_BUTTON_PIN 4
#define BMI160_INT1_PIN 5
#define BAT_PIN 3          // GPIO 3
#define SLEEP_US 950000UL  // 0,95 с

#define NIGHT_START_H 23
#define NIGHT_END_H 7

// Источник погоды фиксирован: Open-Meteo
// 0 = Open-Meteo /v1/forecast (JSON: объект "current", поле temperature_2m)
#define WEATHER_SOURCE_OPEN_METEO 0
#define UI_LANG_RU 0
#define UI_LANG_EN 1
#define DEFAULT_TIMEZONE_MINUTES 180
#define WEEKDAY_MASK_ALL 0x7F
#define WEEKDAY_MASK_WORKDAYS 0x1F
#define WEATHER_API_URL_BUF_SIZE 768  // запас для расширенного Open-Meteo URL (current+hourly+daily)
#define DEFAULT_WEATHER_LAT 53.92f
#define DEFAULT_WEATHER_LON 30.35f

// ---------- батарея ----------
#define BAT_V_MAX 3.4f
#define BAT_V_MIN 2.8f
#define BAT_STEPS 5
#define BATTERY_RECHECK_SEC (15UL * 60UL)
#define OTA_MIN_BATTERY_V 3.05f
#define SHOW_DEBUG_CODES 0
#define AUTOOTA_ENABLED 1
#define AUTOOTA_BRANCH "128x64"  // OTA channel: branch with matching hardware layout
#define AUTOOTA_MANIFEST_URL "https://raw.githubusercontent.com/DrYurets/Celsius/" AUTOOTA_BRANCH "/project.json"
#define AUTOOTA_CHECK_INTERVAL_HOURS_DEFAULT 24
#define AUTOOTA_CHECK_INTERVAL_HOURS_MIN 1
#define AUTOOTA_CHECK_INTERVAL_HOURS_MAX 168
#define AUTOOTA_MIN_BATTERY_V 3.20f

// ---------- коды сообщений ----------
#define CODE_WIFI_CONNECT "Wi-Fi connecting ..."      // подключение к Wi-Fi
#define CODE_WIFI_FAIL "Wi-Fi connection FAIL"        // Wi-Fi недоступен
#define CODE_NTP_SYNC "NTP sync"                      // процесс синхронизации NTP
#define CODE_NTP_OK "NTP sync ok"                     // время успешно синхронизировано
#define CODE_NTP_ERROR "NTP error"                    // ошибка NTP
#define CODE_SENSOR_OK "Indoor sensor found"          // выбранный датчик температуры/влажности найден
#define CODE_SENSOR_MISSING "Indoor sensor missing"   // выбранный датчик температуры/влажности отсутствует
#define CODE_FIRST_SYNC "First sync"                  // первая синхронизация
#define CODE_SETUP_DONE "Setup done"                  // завершение setup
#define CODE_MEASURE_INFO "Battery Measure info"      // минутное измерение батареи
#define CODE_CONFIG_MODE "Setup mode"                 // режим настройки активирован
#define CODE_CONFIG_AP_START "Access point start"     // точка доступа запущена
#define CODE_CONFIG_SAVED "Settings saved"            // настройки сохранены
#define CODE_CPU_FREQ "CPU frequency"                 // частота процессора
#define CODE_WIFI_CONFIG_OK "WiFi settings ok"        // настройки WiFi найдены
#define CODE_WIFI_CONFIG_MISS "WiFi settings miss"    // настройки WiFi не найдены
#define CODE_WIFI_CONFIG_ERR "WiFi settings error"    // ошибка чтения настроек
#define CODE_CONFIG_RESET "Settings reset"            // сброс настроек
#define CODE_WEATHER_FETCH "Weather fetch"            // получение данных о погоде
#define CODE_WEATHER_OK "Weather ok"                  // данные о погоде получены успешно
#define CODE_WEATHER_ERROR "Weather error"            // ошибка получения данных о погоде
#define CODE_WEATHER_WIFI_FAIL "Weather WiFi fail"    // WiFi недоступен для получения погоды
#define CODE_WEATHER_HTTP_START "Weather HTTP start"  // начало HTTP запроса
#define CODE_WEATHER_HTTP_CODE "Weather HTTP code"    // код ответа HTTP
#define CODE_WEATHER_HTTP_ERROR "Weather HTTP err"    // ошибка HTTP запроса
#define CODE_WEATHER_JSON_ERROR "Weather JSON err"    // ошибка парсинга JSON
#define CODE_WEATHER_NO_DATA "Weather no data"        // нет данных в ответе
#define CODE_BMI160_OK "BMI160 ok"
#define CODE_BMI160_ERR "BMI160 fail"

class OledDisplayCompat {
 public:
  OledDisplayCompat() : oled_(OLED_ADDR) {}

  bool begin(int, int) {
    oled_.init();
    return true;
  }
  void clearDisplay() { oled_.clear(); }
  void display() { oled_.update(); }
  void setTextSize(uint8_t size) { oled_.setScale(constrain((int)size, 1, 4)); }
  void setTextColor(uint16_t) {}
  void setRotation(uint8_t) {}
  void setCursor(int16_t x, int16_t y) { oled_.setCursorXY(x, y); }
  int16_t width() const { return SCREEN_WIDTH; }

  size_t print(const char *s) { return oled_.print(s); }
  size_t print(const String &s) { return oled_.print(s); }
  size_t print(char c) { return oled_.print(c); }
  size_t print(int v) { return oled_.print(v); }
  size_t print(unsigned int v) { return oled_.print(v); }
  size_t print(long v) { return oled_.print(v); }
  size_t print(unsigned long v) { return oled_.print(v); }
  size_t print(float v) { return oled_.print(v); }
  size_t println(const char *s) { return oled_.println(s); }
  size_t println(const String &s) { return oled_.println(s); }
  size_t println(int v) { return oled_.println(v); }
  size_t println(unsigned int v) { return oled_.println(v); }
  template <typename T>
  size_t print(const T &v) { return oled_.print(v); }
  template <typename T>
  size_t println(const T &v) { return oled_.println(v); }

  int printf(const char *fmt, ...) {
    char buf[64];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    oled_.print(buf);
    return n;
  }

  void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t) {
    oled_.rect(x, y, x + w - 1, y + h - 1, OLED_STROKE);
  }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t) {
    oled_.rect(x, y, x + w - 1, y + h - 1, OLED_FILL);
  }
  void drawBitmap(int16_t x, int16_t y, const uint8_t *bmp, int16_t w, int16_t h, uint16_t) {
    oled_.drawBitmap(x, y, bmp, w, h, BITMAP_NORMAL, BUF_ADD);
  }

  void ssd1306_command(uint8_t cmd) {
    if (waitContrast_) {
      oled_.setContrast(cmd);
      waitContrast_ = false;
      return;
    }
    if (cmd == 0x81) waitContrast_ = true;
    else if (cmd == 0xAF) oled_.setPower(true);
    else if (cmd == 0xAE) oled_.setPower(false);
  }

 private:
  GyverOLED<SSD1306_128x64, OLED_BUFFER> oled_;
  bool waitContrast_ = false;
};

OledDisplayCompat display;
Adafruit_SHT31 sht31 = Adafruit_SHT31();
Adafruit_AHTX0 aht20;
Adafruit_BMP280 bmp280;
Adafruit_HTU21DF htu21 = Adafruit_HTU21DF();
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
RTC_DATA_ATTR bool displayBackupValid = false;
RTC_DATA_ATTR int32_t driftCorrectionMs = 0;
RTC_DATA_ATTR time_t lastSyncLocalEpoch = 0;
RTC_DATA_ATTR time_t lastAutoOtaCheckEpoch = 0;

static char wifiSSID[64] = "";
static char wifiPassword[64] = "";
static bool configMode = false;
static bool otaUpdateStarted = false;
static bool otaUpdateSuccess = false;
static String otaUpdateError;
AutoOTA autoOta(ROM_VERSION, AUTOOTA_MANIFEST_URL);

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
  bool weekdayLanguageRu;        // true = Russian, false = English
  uint8_t uiLanguage;            // 0=Russian, 1=English (веб-панель/будущая локализация)
  int16_t timezoneMinutes;       // часовой пояс в минутах (например GMT+3 = 180)
  int32_t timeCorrectionPerDay;  // коррекция времени в секундах в сутки (положительное = ускорение, отрицательное = замедление)
  uint8_t syncDays;              // количество суток между синхронизациями NTP
  bool weatherEnabled;           // включить получение погоды
  uint8_t weatherSource;         // legacy field, always Open-Meteo
  float weatherLatitude;         // широта для Open-Meteo
  float weatherLongitude;        // долгота для Open-Meteo
  char weatherApiUrl[WEATHER_API_URL_BUF_SIZE];  // URL API для получения погоды
  uint8_t weatherUpdateHours;    // периодичность обновления погоды в часах
  uint8_t weatherScreenSeconds;  // длительность экрана деталей погоды по кнопке
  uint8_t tempSensorType;        // выбранный датчик температуры/влажности
  bool bmi160Enabled;            // использовать ли BMI160 для wake/детекта движения
  bool autoOtaEnabled;           // автоматическая проверка и установка OTA
  uint16_t autoOtaCheckHours;    // период проверки AutoOTA в часах
  uint8_t activeWeekdaysMask;    // биты 0..6 = ПН..ВС: 1=часы работают в этот день
};

static_assert(EEPROM_SIZE >= (EEPROM_SETTINGS_ADDR + sizeof(DeviceSettings)),
              "EEPROM_SIZE is too small for DeviceSettings");

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
  .weekdayLanguageRu = true,
  .uiLanguage = UI_LANG_RU,
  .timezoneMinutes = DEFAULT_TIMEZONE_MINUTES,
  .timeCorrectionPerDay = 0,
  .syncDays = 1,
  .weatherEnabled = true,
  .weatherSource = WEATHER_SOURCE_OPEN_METEO,
  .weatherLatitude = DEFAULT_WEATHER_LAT,
  .weatherLongitude = DEFAULT_WEATHER_LON,
  .weatherApiUrl = "https://api.open-meteo.com/v1/forecast?latitude=53.92&longitude=30.35&daily=weather_code,sunrise,sunset&hourly=temperature_2m,relative_humidity_2m,surface_pressure,apparent_temperature,wind_speed_10m,weather_code,precipitation_probability,precipitation,wind_direction_10m&current=temperature_2m,relative_humidity_2m,apparent_temperature,precipitation,weather_code,surface_pressure,wind_speed_10m,wind_direction_10m&timezone=Europe%2FMoscow&past_days=0&forecast_days=4&wind_speed_unit=ms",
  .weatherUpdateHours = 1,
  .weatherScreenSeconds = 10,
  .tempSensorType = TEMP_SENSOR_SHT31,
  .bmi160Enabled = false,
  .autoOtaEnabled = true,
  .autoOtaCheckHours = AUTOOTA_CHECK_INTERVAL_HOURS_DEFAULT,
  .activeWeekdaysMask = WEEKDAY_MASK_ALL
};

static bool parseFloatQueryParam(const char *url, const char *key, float &outValue) {
  if (!url || !key) {
    return false;
  }
  const char *found = strstr(url, key);
  if (!found) {
    return false;
  }
  found += strlen(key);
  char *endPtr = nullptr;
  float value = strtof(found, &endPtr);
  if (endPtr == found || !isfinite(value)) {
    return false;
  }
  outValue = value;
  return true;
}

static long getTimezoneOffsetSeconds() {
  return (long)settings.timezoneMinutes * 60L;
}

static bool isValidLatitude(float lat) {
  return isfinite(lat) && lat >= -90.0f && lat <= 90.0f;
}

static bool isValidLongitude(float lon) {
  return isfinite(lon) && lon >= -180.0f && lon <= 180.0f;
}

static void rebuildOpenMeteoUrlFromCoordinates() {
  snprintf(settings.weatherApiUrl,
           WEATHER_API_URL_BUF_SIZE,
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&daily=weather_code,sunrise,sunset&hourly=temperature_2m,relative_humidity_2m,surface_pressure,apparent_temperature,wind_speed_10m,weather_code,precipitation_probability,precipitation,wind_direction_10m&current=temperature_2m,relative_humidity_2m,apparent_temperature,precipitation,weather_code,surface_pressure,wind_speed_10m,wind_direction_10m&timezone=Europe%%2FMoscow&past_days=0&forecast_days=4&wind_speed_unit=ms",
           settings.weatherLatitude,
           settings.weatherLongitude);
}

static bool sensorOK = false;
static float tempC = 22.0;
static float hum = 50.0;
static bool indoorBmpOk = false;
static bool displayOn = true;
static bool wokeByWeatherButton = false;
static bool wokeByMotionSensor = false;

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

uint8_t weekdayBitFromTmWday(int tmWday) {
  // tm_wday: 0=Sun,1=Mon,...,6=Sat -> mask bit: 0=Mon,...,6=Sun
  if (tmWday == 0) {
    return (1U << 6);
  }
  return (1U << (tmWday - 1));
}

bool isWeekdayEnabled(int tmWday) {
  uint8_t dayBit = weekdayBitFromTmWday(tmWday);
  return (settings.activeWeekdaysMask & dayBit) != 0;
}

bool hasValidTime(time_t epoch) {
  return epoch > 100000;
}

bool isWeatherButtonPressed() {
  // Простая фильтрация дребезга: 5 быстрых чтений
  uint8_t lowCount = 0;
  for (uint8_t i = 0; i < 5; i++) {
    if (digitalRead(WEATHER_BUTTON_PIN) == LOW) {
      lowCount++;
    }
    delay(2);
  }
  return lowCount >= 4;
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

time_t applyTimeCorrection(time_t baseEpoch, time_t referenceEpoch) {
  if (settings.timeCorrectionPerDay == 0 || referenceEpoch == 0) {
    return baseEpoch;
  }
  time_t elapsed = baseEpoch - referenceEpoch;
  if (elapsed <= 0) {
    return baseEpoch;
  }
  // Применяем коррекцию: за каждую секунду реального времени добавляем/вычитаем
  // timeCorrectionPerDay / 86400 секунд
  int64_t correctionSeconds = ((int64_t)elapsed * (int64_t)settings.timeCorrectionPerDay) / 86400LL;
  return baseEpoch + (time_t)correctionSeconds;
}

void logToDisplay(const char *code, const char *detail, uint16_t holdMs) {
  if (!settings.showDebugCodes) {
    return;
  }
  setDisplayState(true);
  setBrightness(0x01);
  display.setRotation(0);  // Горизонтальная ориентация для дебаг-кодов
  display.clearDisplay();
  display.setTextSize(1);
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
  display.setRotation(1);  // Возвращаем вертикальную ориентацию
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
  uint8_t *data = (uint8_t *)&settings;
  for (size_t i = 0; i < sizeof(DeviceSettings); i++) {
    data[i] = EEPROM.read(EEPROM_SETTINGS_ADDR + i);
  }
  EEPROM.end();

  // Источник погоды фиксирован: Open-Meteo
  settings.weatherSource = WEATHER_SOURCE_OPEN_METEO;
  if (settings.uiLanguage != UI_LANG_RU && settings.uiLanguage != UI_LANG_EN) {
    settings.uiLanguage = UI_LANG_RU;
  }

  // Проверка валидности (магическое число)
  if (settings.nightStartH > 23 || settings.nightEndH > 23 || settings.nightStartM > 59 || settings.nightEndM > 59 || settings.syncDays == 0 || settings.syncDays > 30) {
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
    settings.uiLanguage = UI_LANG_RU;
    settings.timezoneMinutes = DEFAULT_TIMEZONE_MINUTES;
    settings.timeCorrectionPerDay = 0;
    settings.syncDays = 1;
    settings.weatherEnabled = true;
    settings.weatherSource = WEATHER_SOURCE_OPEN_METEO;
    settings.weatherLatitude = DEFAULT_WEATHER_LAT;
    settings.weatherLongitude = DEFAULT_WEATHER_LON;
    rebuildOpenMeteoUrlFromCoordinates();
    settings.weatherUpdateHours = 1;
    settings.weatherScreenSeconds = 10;
    settings.tempSensorType = TEMP_SENSOR_SHT31;
    settings.bmi160Enabled = false;
    settings.autoOtaEnabled = true;
    settings.autoOtaCheckHours = AUTOOTA_CHECK_INTERVAL_HOURS_DEFAULT;
    settings.activeWeekdaysMask = WEEKDAY_MASK_ALL;
  }

  // Если координаты в EEPROM невалидны (старая версия), пробуем извлечь их из сохраненного URL.
  if (!isValidLatitude(settings.weatherLatitude) || !isValidLongitude(settings.weatherLongitude)) {
    float parsedLat = NAN;
    float parsedLon = NAN;
    bool latOk = parseFloatQueryParam(settings.weatherApiUrl, "latitude=", parsedLat);
    bool lonOk = parseFloatQueryParam(settings.weatherApiUrl, "longitude=", parsedLon);
    settings.weatherLatitude = (latOk && isValidLatitude(parsedLat)) ? parsedLat : DEFAULT_WEATHER_LAT;
    settings.weatherLongitude = (lonOk && isValidLongitude(parsedLon)) ? parsedLon : DEFAULT_WEATHER_LON;
  }

  // Валидация URL (на случай повреждения EEPROM/старой прошивки/обрезки)
  settings.weatherApiUrl[WEATHER_API_URL_BUF_SIZE - 1] = '\0';
  size_t urlLen = strnlen(settings.weatherApiUrl, WEATHER_API_URL_BUF_SIZE);
  const char *expectedNeedle = "open-meteo.com/v1/forecast";

  bool urlInvalid = (urlLen == 0 || urlLen >= (WEATHER_API_URL_BUF_SIZE - 1) ||
                     strncmp(settings.weatherApiUrl, "http", 4) != 0 ||
                     strstr(settings.weatherApiUrl, expectedNeedle) == nullptr);

  if (urlInvalid) rebuildOpenMeteoUrlFromCoordinates();

  // URL всегда синхронизируем с координатами, чтобы запрос соответствовал настройкам геопозиции.
  rebuildOpenMeteoUrlFromCoordinates();

  // Валидация weatherUpdateHours
  if (settings.weatherUpdateHours == 0 || settings.weatherUpdateHours > 24) {
    settings.weatherUpdateHours = 1;
  }
  if (settings.weatherScreenSeconds == 0 || settings.weatherScreenSeconds > 60) {
    settings.weatherScreenSeconds = 10;
  }
  if (settings.timezoneMinutes < -720 || settings.timezoneMinutes > 840) {
    settings.timezoneMinutes = DEFAULT_TIMEZONE_MINUTES;
  }
  if (settings.tempSensorType > TEMP_SENSOR_HTU21) {
    settings.tempSensorType = TEMP_SENSOR_SHT31;
  }
  settings.bmi160Enabled = settings.bmi160Enabled ? true : false;
  if (settings.autoOtaCheckHours < AUTOOTA_CHECK_INTERVAL_HOURS_MIN ||
      settings.autoOtaCheckHours > AUTOOTA_CHECK_INTERVAL_HOURS_MAX) {
    settings.autoOtaCheckHours = AUTOOTA_CHECK_INTERVAL_HOURS_DEFAULT;
  }
  settings.autoOtaEnabled = settings.autoOtaEnabled ? true : false;
  settings.activeWeekdaysMask &= WEEKDAY_MASK_ALL;
}

void saveSettings() {
  EEPROM.begin(EEPROM_SIZE);
  uint8_t *data = (uint8_t *)&settings;
  for (size_t i = 0; i < sizeof(DeviceSettings); i++) {
    EEPROM.write(EEPROM_SETTINGS_ADDR + i, data[i]);
  }
  EEPROM.commit();
  EEPROM.end();
}

bool exportSettingsToJson(String &outJson) {
  DynamicJsonDocument doc(4096);
  doc["schemaVersion"] = 1;
  doc["romVersion"] = ROM_VERSION;

  JsonObject wifi = doc.createNestedObject("wifi");
  wifi["includeCredentials"] = false;

  JsonObject displayCfg = doc.createNestedObject("display");
  displayCfg["showDebugCodes"] = settings.showDebugCodes;
  displayCfg["showDate"] = settings.showDate;
  displayCfg["showWeekday"] = settings.showWeekday;
  displayCfg["timeFormat24h"] = settings.timeFormat24h;
  displayCfg["hourlyBlink"] = settings.hourlyBlink;
  displayCfg["weekdayLanguageRu"] = settings.weekdayLanguageRu;
  displayCfg["uiLanguage"] = (settings.uiLanguage == UI_LANG_EN) ? "en" : "ru";

  JsonObject night = doc.createNestedObject("nightMode");
  night["startH"] = settings.nightStartH;
  night["startM"] = settings.nightStartM;
  night["endH"] = settings.nightEndH;
  night["endM"] = settings.nightEndM;

  JsonObject timeCfg = doc.createNestedObject("time");
  timeCfg["timezoneMinutes"] = settings.timezoneMinutes;
  timeCfg["timeCorrectionPerDay"] = settings.timeCorrectionPerDay;
  timeCfg["activeWeekdaysMask"] = settings.activeWeekdaysMask;

  JsonObject sensors = doc.createNestedObject("sensors");
  sensors["tempSensorType"] = tempSensorTypeToFormValue(settings.tempSensorType);
  sensors["bmi160Enabled"] = settings.bmi160Enabled;

  JsonObject weather = doc.createNestedObject("weather");
  weather["enabled"] = settings.weatherEnabled;
  weather["latitude"] = settings.weatherLatitude;
  weather["longitude"] = settings.weatherLongitude;
  weather["updateHours"] = settings.weatherUpdateHours;
  weather["screenSeconds"] = settings.weatherScreenSeconds;

  JsonObject autoOta = doc.createNestedObject("autoOta");
  autoOta["enabled"] = settings.autoOtaEnabled;
  autoOta["checkHours"] = settings.autoOtaCheckHours;

  outJson = "";
  serializeJsonPretty(doc, outJson);
  return outJson.length() > 0;
}

bool importSettingsFromJson(const String &json, String &error) {
  DynamicJsonDocument doc(4096);
  DeserializationError parseError = deserializeJson(doc, json);
  if (parseError) {
    error = String("JSON parse error: ") + parseError.c_str();
    return false;
  }

  DeviceSettings newSettings = settings;
  // Security policy: WiFi credentials are never imported from JSON.

  JsonObject displayCfg = doc["display"];
  if (!displayCfg.isNull()) {
    if (displayCfg.containsKey("showDebugCodes")) newSettings.showDebugCodes = displayCfg["showDebugCodes"];
    if (displayCfg.containsKey("showDate")) newSettings.showDate = displayCfg["showDate"];
    if (displayCfg.containsKey("showWeekday")) newSettings.showWeekday = displayCfg["showWeekday"];
    if (displayCfg.containsKey("timeFormat24h")) newSettings.timeFormat24h = displayCfg["timeFormat24h"];
    if (displayCfg.containsKey("hourlyBlink")) newSettings.hourlyBlink = displayCfg["hourlyBlink"];
    if (displayCfg.containsKey("weekdayLanguageRu")) newSettings.weekdayLanguageRu = displayCfg["weekdayLanguageRu"];
    const char *lang = displayCfg["uiLanguage"] | nullptr;
    if (lang) {
      newSettings.uiLanguage = (strcmp(lang, "en") == 0) ? UI_LANG_EN : UI_LANG_RU;
    }
  }

  JsonObject night = doc["nightMode"];
  if (!night.isNull()) {
    if (night.containsKey("startH")) newSettings.nightStartH = night["startH"];
    if (night.containsKey("startM")) newSettings.nightStartM = night["startM"];
    if (night.containsKey("endH")) newSettings.nightEndH = night["endH"];
    if (night.containsKey("endM")) newSettings.nightEndM = night["endM"];
  }

  JsonObject timeCfg = doc["time"];
  if (!timeCfg.isNull()) {
    if (timeCfg.containsKey("timezoneMinutes")) newSettings.timezoneMinutes = timeCfg["timezoneMinutes"];
    if (timeCfg.containsKey("timeCorrectionPerDay")) newSettings.timeCorrectionPerDay = timeCfg["timeCorrectionPerDay"];
    if (timeCfg.containsKey("activeWeekdaysMask")) newSettings.activeWeekdaysMask = timeCfg["activeWeekdaysMask"];
  }

  JsonObject sensors = doc["sensors"];
  if (!sensors.isNull()) {
    const char *sensorType = sensors["tempSensorType"] | nullptr;
    if (sensorType) {
      newSettings.tempSensorType = parseTempSensorType(String(sensorType));
    }
    if (sensors.containsKey("bmi160Enabled")) newSettings.bmi160Enabled = sensors["bmi160Enabled"];
  }

  JsonObject weather = doc["weather"];
  if (!weather.isNull()) {
    if (weather.containsKey("enabled")) newSettings.weatherEnabled = weather["enabled"];
    if (weather.containsKey("latitude")) newSettings.weatherLatitude = weather["latitude"];
    if (weather.containsKey("longitude")) newSettings.weatherLongitude = weather["longitude"];
    if (weather.containsKey("updateHours")) newSettings.weatherUpdateHours = weather["updateHours"];
    if (weather.containsKey("screenSeconds")) newSettings.weatherScreenSeconds = weather["screenSeconds"];
  }

  JsonObject autoOta = doc["autoOta"];
  if (!autoOta.isNull()) {
    if (autoOta.containsKey("enabled")) newSettings.autoOtaEnabled = autoOta["enabled"];
    if (autoOta.containsKey("checkHours")) newSettings.autoOtaCheckHours = autoOta["checkHours"];
  }

  if (newSettings.nightStartH > 23 || newSettings.nightStartM > 59 ||
      newSettings.nightEndH > 23 || newSettings.nightEndM > 59) {
    error = "Invalid night mode time";
    return false;
  }
  if (newSettings.weatherUpdateHours < 1 || newSettings.weatherUpdateHours > 24) {
    error = "weather.updateHours must be 1..24";
    return false;
  }
  if (newSettings.weatherScreenSeconds < 1 || newSettings.weatherScreenSeconds > 60) {
    error = "weather.screenSeconds must be 1..60";
    return false;
  }
  if (newSettings.timezoneMinutes < -720 || newSettings.timezoneMinutes > 840) {
    error = "time.timezoneMinutes must be -720..840";
    return false;
  }
  if (newSettings.autoOtaCheckHours < AUTOOTA_CHECK_INTERVAL_HOURS_MIN ||
      newSettings.autoOtaCheckHours > AUTOOTA_CHECK_INTERVAL_HOURS_MAX) {
    error = "autoOta.checkHours out of range";
    return false;
  }
  if (!isValidLatitude(newSettings.weatherLatitude) || !isValidLongitude(newSettings.weatherLongitude)) {
    error = "Invalid weather coordinates";
    return false;
  }

  newSettings.activeWeekdaysMask &= WEEKDAY_MASK_ALL;
  if (newSettings.activeWeekdaysMask == 0) {
    newSettings.activeWeekdaysMask = WEEKDAY_MASK_ALL;
  }

  settings = newSettings;
  rebuildOpenMeteoUrlFromCoordinates();
  saveSettings();

  error = "";
  return true;
}

// ---------- веб-сервер функции ----------
#include "WebConfigServer.h"

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

bool readIndoorSensors(float &t, float &h) {
  if (!sensorOK) {
    return false;
  }
  float tRead = NAN;
  float hRead = NAN;
  bool readOk = readSelectedIndoorSensor(settings.tempSensorType, sht31, aht20, htu21, tRead, hRead);
  if (!readOk || isnan(tRead) || isnan(hRead)) {
    return false;
  }
  t = tRead;
  h = hRead;
  return true;
}

void drawDayShort(uint8_t wday, int16_t x, int16_t y) {
  wday = wday % 7;
  display.setCursor(x + 4, y);

  const char *days[7];
  if (settings.weekdayLanguageRu) {
    static const char *ruDays[] = { "ВС", "ПН", "ВТ", "СР", "ЧТ", "ПТ", "СБ" };
    memcpy(days, ruDays, sizeof(days));
  } else {
    static const char *enDays[] = { "SU", "MO", "TU", "WE", "TH", "FR", "SA" };
    memcpy(days, enDays, sizeof(days));
  }
  display.print(days[wday]);
}

void setBrightness(uint8_t br) {
  display.ssd1306_command(0x81);
  display.ssd1306_command(br);
}

static void drawDegreeMark(int16_t x, int16_t y) {
  display.fillRect(x + 1, y, 1, 1, SSD1306_WHITE);
  display.fillRect(x, y + 1, 1, 1, SSD1306_WHITE);
  display.fillRect(x + 2, y + 1, 1, 1, SSD1306_WHITE);
  display.fillRect(x + 1, y + 2, 1, 1, SSD1306_WHITE);
}

float readBattery() {
  uint32_t mv = analogReadMilliVolts(BAT_PIN);
  return mv * 2.0f / 1000.0f;  // делитель 1:1 → Вольты
}

static bool shouldCheckAutoOta(time_t local, bool timeValid, bool night, bool workdayEnabled) {
#if AUTOOTA_ENABLED
  if (!settings.autoOtaEnabled || !timeValid || night || !workdayEnabled) {
    return false;
  }
  uint32_t intervalSec = (uint32_t)settings.autoOtaCheckHours * 3600UL;
  if (intervalSec == 0) {
    intervalSec = (uint32_t)AUTOOTA_CHECK_INTERVAL_HOURS_DEFAULT * 3600UL;
  }
  if (lastAutoOtaCheckEpoch == 0) {
    return true;
  }
  time_t delta = local - lastAutoOtaCheckEpoch;
  if (delta < 0) {
    return true;
  }
  return delta >= (time_t)intervalSec;
#else
  (void)local;
  (void)timeValid;
  (void)night;
  (void)workdayEnabled;
  return false;
#endif
}

static void tryAutoOtaUpdate(time_t local, bool timeValid, bool night, bool workdayEnabled) {
#if AUTOOTA_ENABLED
  if (!shouldCheckAutoOta(local, timeValid, night, workdayEnabled)) {
    return;
  }
  lastAutoOtaCheckEpoch = local;

  float vBat = readBattery();
  if (vBat < AUTOOTA_MIN_BATTERY_V) {
    Serial.printf("[AutoOTA] Skipped: battery %.2fV < %.2fV\n", vBat, AUTOOTA_MIN_BATTERY_V);
    return;
  }

  String newVersion;
  String notes;
  String binPath;
  bool hasInfo = autoOta.checkUpdate(&newVersion, &notes, &binPath);
  if (!hasInfo) {
    if (autoOta.hasError()) {
      Serial.printf("[AutoOTA] checkUpdate error: %d\n", (int)autoOta.getError());
    } else {
      Serial.println("[AutoOTA] checkUpdate: no update info");
    }
    return;
  }
  if (!autoOta.hasUpdate()) {
    Serial.println("[AutoOTA] No update");
    return;
  }

  Serial.printf("[AutoOTA] Update found: %s -> %s\n", ROM_VERSION, newVersion.c_str());
  if (!notes.isEmpty()) {
    Serial.printf("[AutoOTA] Notes: %s\n", notes.c_str());
  }
  Serial.printf("[AutoOTA] Bin: %s\n", binPath.c_str());
  bool ok = autoOta.updateNow();
  if (!ok && autoOta.hasError()) {
    Serial.printf("[AutoOTA] updateNow error: %d\n", (int)autoOta.getError());
  }
#else
  (void)local;
  (void)timeValid;
  (void)night;
  (void)workdayEnabled;
#endif
}

// Иконка батареи (как на телефоне): контур + заполнение по уровню заряда, в правом верхнем углу
void drawBattery(uint8_t bars) {
  const int16_t bodyW = 20;
  const int16_t bodyH = 10;
  const int16_t tabW = 2;
  const int16_t tabH = 4;
  int16_t right = (int16_t)display.width();
  int16_t bx = right - tabW - bodyW;  // левый край корпуса

  // контур корпуса
  display.drawRect(bx, 0, bodyW, bodyH, SSD1306_WHITE);
  // контур «носика» (плюс) справа по центру
  display.drawRect(right - tabW, (bodyH - tabH) / 2, tabW, tabH, SSD1306_WHITE);

  // заполнение по уровню (0..BAT_STEPS)
  if (bars > 0) {
    int16_t innerW = bodyW - 2;
    int16_t fillW = (int16_t)((uint32_t)bars * innerW / BAT_STEPS);
    if (fillW > 0) {
      display.fillRect(bx + 1, 1, fillW, bodyH - 2, SSD1306_WHITE);
    }
  }
}

/*
* Вывод на экран
*/

void drawClock(int d, int mo, int h, int m, uint8_t batBars, uint8_t wday) {
  display.clearDisplay();
  display.setTextSize(1);

  const int16_t topY = 0;
  // 1) День недели — левый верхний угол
  if (settings.showWeekday) {
    drawDayShort(wday, 0, topY);
  }
  // 2) Дата (число.месяц) — по центру верхней строки (шрифт 1: ~6 px на символ)
  if (settings.showDate) {
    char dateBuf[8];
    snprintf(dateBuf, sizeof(dateBuf), "%02d.%02d", d, mo);
    const int16_t charW = 6;
    const int16_t textW = charW * (int16_t)strlen(dateBuf);
    const int16_t dateX = ((int16_t)display.width() - textW) / 2;
    display.setCursor(dateX, topY);
    display.print(dateBuf);
  }
  // 3) Индикатор заряда — вверху справа (как раньше)
  drawBattery(batBars);

  //display.drawLine(0, 20, 128, 20, SSD1306_WHITE); // разделительная линия

  // Формат времени
  int displayH = h;
  if (!settings.timeFormat24h) {
    displayH = h % 12;
    if (displayH == 0) displayH = 12;
  }

  display.setTextSize(4);

  // Подготовим строку времени для расчета ширины
  char lBuf[3], rBuf[3];
  snprintf(lBuf, sizeof(lBuf), "%02d", displayH);
  snprintf(rBuf, sizeof(rBuf), "%02d", m);

  // Ширина символа шрифта размером 4: 6*4=24px
  int charW = 6 * 4; // 24 px
  int colonSpace = 16; // место между часами и минутами, чтобы хватило для квадратиков
  int lLen = strlen(lBuf);
  int rLen = strlen(rBuf);
  int fullW = (lLen + rLen) * charW + colonSpace;

  int screenW = (int)display.width();
  int startX = (screenW - fullW) / 2;
  int y = 20;

  // Нарисовать часы
  display.setCursor(startX, y);
  display.print(lBuf);

  // Нарисовать "двоеточие" из двух квадратов 3x3px с разносом 8px
  int colonX = startX + lLen * charW + (colonSpace - 3) / 2;
  int colonYtop = y + 6;          // немного ниже верхнего края цифр
  int colonYbot = colonYtop + 3 + 8; // нижний квадрат с промежутком 8px

  display.fillRect(colonX, colonYtop, 3, 3, SSD1306_WHITE);
  display.fillRect(colonX, colonYbot, 3, 3, SSD1306_WHITE);

  // Нарисовать минуты
  int minX = startX + lLen * charW + colonSpace;
  display.setCursor(minX, y);
  display.print(rBuf);

  //display.drawLine(0, 48, 128, 48, SSD1306_WHITE);

  display.setTextSize(1);
  // Наружная температура (если включена и доступна)
  if (!isnan(outdoorTemperature)) {
    int16_t outX = 12;
    if (outdoorTemperature > 0) {
      outX = 12;
    } else {
      outX = 9;  // оставляем место для минуса
    }
    char outBuf[8];
    snprintf(outBuf, sizeof(outBuf), "%d", (int)outdoorTemperature);
    display.setCursor(outX, 56);
    display.print(outBuf);
    drawDegreeMark(outX + (int16_t)strlen(outBuf) * 6 + 1, 55);
  }
  display.setCursor(52, 56);
  char inBuf[8];
  snprintf(inBuf, sizeof(inBuf), "%d", (int)tempC);
  display.print(inBuf);  // температура внутри
  drawDegreeMark(52 + (int16_t)strlen(inBuf) * 6 + 1, 55);
  display.setCursor(78, 56);
  display.printf("%d%%", (int)hum);  // влажность
  display.display();
  displayBackupValid = false;
}

bool ntpSync() {
  logToDisplay(CODE_WIFI_CONNECT, nullptr, 0);
  setCpuPerformance();

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_15dBm);
  WiFi.begin(wifiSSID, wifiPassword, 15);

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
  timeClient.setTimeOffset(getTimezoneOffsetSeconds());
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

// NTP sync, когда WiFi уже подключен (используется в погодном цикле)
static bool ntpSyncOverConnectedWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  logToDisplay(CODE_NTP_SYNC, nullptr, 0);
  timeClient.begin();
  timeClient.setTimeOffset(getTimezoneOffsetSeconds());
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
  return ok;
}

uint32_t runCycle() {
  uint32_t cycleStartMs = millis();
  time_t local = applyDriftCorrection(storedEpoch, lastSyncLocalEpoch);
  local = applyTimeCorrection(local, lastSyncLocalEpoch);
  bool timeValid = hasValidTime(local);
  struct tm ti = {};
  if (timeValid) {
    localtime_r(&local, &ti);
  }

  if (!timeValid) {
    if (!timeValid) {
      logToDisplay(CODE_FIRST_SYNC);
    }
    if (ntpSync()) {
      local = applyDriftCorrection(storedEpoch, lastSyncLocalEpoch);
      local = applyTimeCorrection(local, lastSyncLocalEpoch);
      timeValid = hasValidTime(local);
      if (timeValid) {
        localtime_r(&local, &ti);
      }
    }
  }

  if (sensorOK) {
    float t = tempC;
    float h = hum;
    if (readIndoorSensors(t, h)) {
      tempC = t;
      hum = h;
    }
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
  bool workdayEnabled = timeValid ? isWeekdayEnabled(ti.tm_wday) : true;

  // Обновление данных о погоде (только если включено, не ночной режим и прошло достаточно времени)
  if (timeValid && workdayEnabled && !night && shouldUpdateWeather(local, settings.weatherUpdateHours)) {
    logToDisplay(CODE_WEATHER_FETCH);
    setCpuPerformance();

    // Проверяем текущий статус WiFi
    wl_status_t wifiStatus = WiFi.status();
    char detail[32];
    snprintf(detail, sizeof(detail), "WiFi st=%d", wifiStatus);
    logToDisplay(CODE_WEATHER_FETCH, detail);

    // Подключаемся к WiFi, если не подключены
    if (wifiStatus != WL_CONNECTED) {
      WiFi.persistent(false);
      WiFi.mode(WIFI_STA);
      WiFi.setSleep(false);
      WiFi.begin(wifiSSID, wifiPassword, 15);

      unsigned long startAttempt = millis();
      while (WiFi.status() != WL_CONNECTED && (millis() - startAttempt) < 15000UL) {
        delay(200);
        wifiStatus = WiFi.status();
        if ((millis() - startAttempt) % 2000 < 200) {  // Показываем статус каждые 2 секунды
          snprintf(detail, sizeof(detail), "Connecting %d", wifiStatus);
          logToDisplay(CODE_WEATHER_FETCH, detail);
        }
      }
    }

    wifiStatus = WiFi.status();
    snprintf(detail, sizeof(detail), "Final st=%d", wifiStatus);
    logToDisplay(CODE_WEATHER_FETCH, detail);

    if (wifiStatus == WL_CONNECTED) {
      ntpSyncOverConnectedWiFi();
      local = applyDriftCorrection(storedEpoch, lastSyncLocalEpoch);
      local = applyTimeCorrection(local, lastSyncLocalEpoch);
      timeValid = hasValidTime(local);
      if (timeValid) {
        localtime_r(&local, &ti);
      }
      tryAutoOtaUpdate(local, timeValid, night, workdayEnabled);
      bool success = fetchOutdoorTemperature(settings.weatherApiUrl, WEATHER_SOURCE_OPEN_METEO);
      if (success) {
        lastWeatherUpdate = local;  // та же шкала, что в shouldUpdateWeather() (не time(nullptr) — libc не синхронизирован с storedEpoch)
        snprintf(detail, sizeof(detail), "T=%d", (int)outdoorTemperature);
        logToDisplay(CODE_WEATHER_OK, detail);
      } else {
        logToDisplay(CODE_WEATHER_ERROR);
        // Обновляем время последней попытки даже при ошибке, чтобы не пытаться каждую минуту
        // shouldUpdateWeather() использует меньший интервал (5 минут) для повторных попыток при ошибке
        lastWeatherUpdate = local;
      }
    } else {
      snprintf(detail, sizeof(detail), "Status=%d", wifiStatus);
      logToDisplay(CODE_WEATHER_WIFI_FAIL, detail);
      // Обновляем время последней попытки даже при ошибке WiFi
      lastWeatherUpdate = local;
    }

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    setCpuLowPower();
  }

  if (!timeValid) {
    logToDisplay(CODE_NTP_ERROR, "Wait NTP", 0);
  } else if (!night && workdayEnabled) {
    setDisplayState(true);
    setBrightness(0x01);

    // Показываем детали погоды либо после wakeup по кнопке,
    // либо если кнопку (GPIO4) держат замкнутой прямо сейчас.
    bool weatherButtonPressedNow = isWeatherButtonPressed();
    if (wokeByWeatherButton || wokeByMotionSensor || weatherButtonPressedNow) {
      uint32_t detailEndMs = millis() + (uint32_t)settings.weatherScreenSeconds * 1000UL;
      uint8_t detailPage = 0;
      bool prevBtnLow = (digitalRead(WEATHER_BUTTON_PIN) == LOW);
      drawWeatherDetailScreen(display,
                              detailPage,
                              outdoorTemperature,
                              weatherFeelsLikeC,
                              weatherPressureHpa,
                              weatherHumidityPct,
                              weatherWindSpeedMs,
                              weatherWindDirectionDeg,
                              weatherWmoCode,
                              weatherPrecipProbabilityPct,
                              weatherDailyWmoCode,
                              weatherDailyTempMaxC,
                              weatherDailyTempMinC,
                              weatherDailyWindDayMs,
                              weatherDailyPrecipDayPct,
                              (uint8_t)ti.tm_wday,
                              weatherNearestNightMinC,
                              weatherNearestNightWmoCode);
      while ((int32_t)(millis() - detailEndMs) < 0) {
        bool low = (digitalRead(WEATHER_BUTTON_PIN) == LOW);
        if (low && !prevBtnLow) {
          detailPage = (detailPage + 1) % kWeatherDetailScreenCount;
          detailEndMs = millis() + (uint32_t)settings.weatherScreenSeconds * 1000UL;
          drawWeatherDetailScreen(display,
                                  detailPage,
                                  outdoorTemperature,
                                  weatherFeelsLikeC,
                                  weatherPressureHpa,
                                  weatherHumidityPct,
                                  weatherWindSpeedMs,
                                  weatherWindDirectionDeg,
                                  weatherWmoCode,
                                  weatherPrecipProbabilityPct,
                                  weatherDailyWmoCode,
                                  weatherDailyTempMaxC,
                                  weatherDailyTempMinC,
                                  weatherDailyWindDayMs,
                                  weatherDailyPrecipDayPct,
                                  (uint8_t)ti.tm_wday,
                                  weatherNearestNightMinC,
                                  weatherNearestNightWmoCode);
          delay(45);
        }
        prevBtnLow = low;
        delay(12);
      }
    }

    drawClock(ti.tm_mday, ti.tm_mon + 1, ti.tm_hour, ti.tm_min, batBars, ti.tm_wday);
  } else {
    display.clearDisplay();
    display.display();
    setDisplayState(false);
  }

  if (timeValid && workdayEnabled && (ti.tm_min == 0) && !night && settings.hourlyBlink) {
    digitalWrite(LED_PIN, HIGH);
    delay(80);
    digitalWrite(LED_PIN, LOW);
  }

  uint32_t sleepSeconds = 60;
  if (timeValid) {
    if (!workdayEnabled) {
      sleepSeconds = (uint32_t)((23 - ti.tm_hour) * 3600 + (59 - ti.tm_min) * 60 + (60 - ti.tm_sec));
      if (sleepSeconds == 0) {
        sleepSeconds = 60;
      }
    } else {
      int secToNextMinute = 60 - ti.tm_sec;
      if (secToNextMinute <= 0) {
        secToNextMinute = 60;
      }
      sleepSeconds = (uint32_t)secToNextMinute;
    }
    uint32_t activeSeconds = ((millis() - cycleStartMs) + 500) / 1000;
    uint32_t elapsedSeconds = activeSeconds + sleepSeconds;

    // Применяем коррекцию времени к storedEpoch
    if (settings.timeCorrectionPerDay != 0 && lastSyncLocalEpoch > 0) {
      // Вычисляем коррекцию для прошедшего времени
      int64_t correctionSeconds = ((int64_t)elapsedSeconds * (int64_t)settings.timeCorrectionPerDay) / 86400LL;
      storedEpoch = storedEpoch + elapsedSeconds + (time_t)correctionSeconds;
    } else {
      storedEpoch = storedEpoch + elapsedSeconds;
    }
  } else {
    sleepSeconds = 30;
  }

  return sleepSeconds;
}

void enterDeepSleep(uint32_t sleepSeconds) {
  if (sleepSeconds == 0) {
    sleepSeconds = 60;
  }
  // Разрешаем пробуждение:
  // - GPIO4 (кнопка) по LOW
  // - GPIO5 (BMI160 INT1) по HIGH
  pinMode(WEATHER_BUTTON_PIN, INPUT_PULLUP);
  gpio_pullup_en((gpio_num_t)WEATHER_BUTTON_PIN);
  gpio_pulldown_dis((gpio_num_t)WEATHER_BUTTON_PIN);
  esp_deep_sleep_enable_gpio_wakeup((1ULL << WEATHER_BUTTON_PIN), ESP_GPIO_WAKEUP_GPIO_LOW);
  if (settings.bmi160Enabled) {
    pinMode(BMI160_INT1_PIN, INPUT_PULLDOWN);
    gpio_pullup_dis((gpio_num_t)BMI160_INT1_PIN);
    gpio_pulldown_en((gpio_num_t)BMI160_INT1_PIN);
    esp_deep_sleep_enable_gpio_wakeup((1ULL << BMI160_INT1_PIN), ESP_GPIO_WAKEUP_GPIO_HIGH);
  }
  esp_sleep_enable_timer_wakeup((uint64_t)sleepSeconds * 1000000ULL);
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(9600);
  esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
  wokeByWeatherButton = false;
  wokeByMotionSensor = false;
  if (wakeCause == ESP_SLEEP_WAKEUP_GPIO) {
    uint64_t wakeMask = esp_sleep_get_gpio_wakeup_status();
    wokeByWeatherButton = (wakeMask & (1ULL << WEATHER_BUTTON_PIN)) != 0;
    wokeByMotionSensor = (wakeMask & (1ULL << BMI160_INT1_PIN)) != 0;
  }

  // Сразу гасим светодиод, чтобы избежать вспышки при пробуждении
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  setCpuLowPower();
  delay(100);

  // Проверка сброса настроек: если LED_PIN (GPIO 0) замкнут на землю при старте
  // Кратковременно переключаем на вход для проверки
  pinMode(LED_PIN, INPUT_PULLUP);
  pinMode(SETUP_BUTTON_PIN, INPUT_PULLUP);
  delayMicroseconds(100);  // Минимальная задержка для стабилизации
  bool resetRequested = (digitalRead(LED_PIN) == LOW);
  bool setupModeRequested = (digitalRead(SETUP_BUTTON_PIN) == LOW);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  if (resetRequested) {
    // GPIO был замкнут на землю - сбрасываем настройки
    clearWiFiConfig();
    Serial.println("Config reset by GPIO0 at boot");
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
  display.setRotation(0);
  display.setTextColor(SSD1306_WHITE);
  display.clearDisplay();
  setBrightness(0x01);
  display.display();

  // Загрузка настроек устройства
  loadSettings();

  if (settings.tempSensorType > TEMP_SENSOR_HTU21) {
    settings.tempSensorType = TEMP_SENSOR_SHT31;
  }
  IndoorSensorInitResult sensorInit = initSelectedIndoorSensor(settings.tempSensorType,
                                                               sht31,
                                                               aht20,
                                                               bmp280,
                                                               htu21,
                                                               &Wire,
                                                               SHT31_ADDR,
                                                               AHT20_ADDR,
                                                               BMP280_ADDR,
                                                               BMP280_ADDR_ALT);
  sensorOK = sensorInit.sensorOk;
  indoorBmpOk = sensorInit.bmpOk;
  Serial.printf("Sensor %s=%d, BMP280=%d\n",
                indoorSensorName(settings.tempSensorType),
                (int)sensorOK,
                (int)indoorBmpOk);
  logToDisplay(sensorOK ? CODE_SENSOR_OK : CODE_SENSOR_MISSING);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  pinMode(WEATHER_BUTTON_PIN, INPUT_PULLUP);
  gpio_pullup_en((gpio_num_t)WEATHER_BUTTON_PIN);
  gpio_pulldown_dis((gpio_num_t)WEATHER_BUTTON_PIN);
  Serial.printf("Weather button GPIO%d state=%d\n", WEATHER_BUTTON_PIN, digitalRead(WEATHER_BUTTON_PIN));
  if (settings.bmi160Enabled) {
    pinMode(BMI160_INT1_PIN, INPUT_PULLDOWN);
    gpio_pullup_dis((gpio_num_t)BMI160_INT1_PIN);
    gpio_pulldown_en((gpio_num_t)BMI160_INT1_PIN);
    bool bmi160OK = initBMI160MotionWake();
    logToDisplay(bmi160OK ? CODE_BMI160_OK : CODE_BMI160_ERR);
    Serial.printf("BMI160 INT1 GPIO%d state=%d, wakeByMotion=%d\n",
                  BMI160_INT1_PIN,
                  digitalRead(BMI160_INT1_PIN),
                  (int)wokeByMotionSensor);
  } else {
    wokeByMotionSensor = false;
    Serial.println("BMI160 is disabled in settings");
  }

  if (setupModeRequested) {
    logToDisplay(CODE_CONFIG_MODE, "GPIO1 forced");
    startConfigMode();
    return;
  }

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

  // Если время ещё невалидно, пробуем первичную синхронизацию.
  // При неудаче сразу переходим в setup mode, чтобы пользователь мог
  // переподключить устройство к актуальной WiFi сети.
  if (!hasValidTime(storedEpoch)) {
    if (!ntpSync()) {
      logToDisplay(CODE_WIFI_FAIL, "open setup");
      startConfigMode();
      return;
    }
  }

  // Настройки есть - продолжаем в обычном режиме.
  // Дальнейшее WiFi подключение будет происходить при необходимости.

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