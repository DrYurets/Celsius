/*
* Celsius Clock
* ROM version: A1.4.13
* https://github.com/DrYurets/Celsius/tree/128x128
* Date: 05.08.2026
* Copyright (c) 2026 DrYurets
*/

#include <Wire.h>
#include <cmath>
#include <cstdarg>
#include <cstring>
#define SH110X_NO_SPLASH
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <Adafruit_SHT31.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_HTU21DF.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_ota_ops.h>
#include <driver/adc.h>
#include <driver/gpio.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <Update.h>
#include <AutoOTA.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "sensors/bmi160/BMI160Motion.h"
#include "sensors/SensorTypes.h"
#include "sensors/SensorManager.h"
#include "WeatherAPI.h"
#include "WeatherDetailScreens.h"
#include "SyncProgress.h"

#define AP_SSID "CelsiusClock"
#define AP_PASSWORD "12345678"
// SoftAP gateway/IP shown on OLED and used by clients after joining CelsiusClock.
#define AP_IP_ADDR IPAddress(192, 168, 4, 1)
#define AP_IP_STR "192.168.4.1"
#define ROM_VERSION "A1.4.13"
#define EEPROM_SSID_ADDR 0
#define EEPROM_PASS_ADDR 64
#define EEPROM_SETTINGS_ADDR 128
#define EEPROM_SIZE 2048
#define I2C_SDA 8
#define I2C_SCL 9
#define OLED_ADDR 0x3C
#define SSD1306_WHITE 1
#define SSD1306_BLACK 0
#define SSD1306_SWITCHCAPVCC 0
#define SHT31_ADDR 0x44
#define AHT20_ADDR 0x38
#define BMP280_ADDR 0x76
#define BMP280_ADDR_ALT 0x77
#define SCREEN_WIDTH 128   // GME128128-01-IIC / SH1107
#define SCREEN_HEIGHT 128
#define LED_PIN 0
#define SETUP_BUTTON_PIN 1
#define WEATHER_BUTTON_PIN 4
#define OTA_BUTTON_PIN LED_PIN  // GPIO0: LED + кнопка сброса (boot) + OTA-info
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
#define AUTOOTA_BRANCH "128x128"  // OTA channel: branch with matching hardware layout
#define AUTOOTA_MANIFEST_URL "https://raw.githubusercontent.com/DrYurets/Celsius/" AUTOOTA_BRANCH "/project.json"
#define AUTOOTA_CHECK_INTERVAL_HOURS_DEFAULT 24
#define AUTOOTA_CHECK_INTERVAL_HOURS_MIN 1
#define AUTOOTA_CHECK_INTERVAL_HOURS_MAX 168
#define AUTOOTA_MIN_BATTERY_V 3.20f

// 1 = раз в runCycle печать в Serial по BMI160 (ось, порог); выключите после проверки.
#ifndef BMI160_ORIENT_DIAG_SERIAL
#define BMI160_ORIENT_DIAG_SERIAL 1
#endif

#ifndef BMI160_ORIENT_INVERT
#define BMI160_ORIENT_INVERT 1
#endif

// переворот корпуса менял знак у Y, а алгоритм смотрел на Z и не реагировал как ожидалось.
#ifndef BMI160_ORIENT_AXIS
#define BMI160_ORIENT_AXIS 1
#endif

// 1 = перед esp_deep_sleep перевести BMI160 accel в suspend (если датчик остаётся на 3V3 — меньше утечка).
#ifndef BMI160_SUSPEND_BEFORE_DSLEEP
#define BMI160_SUSPEND_BEFORE_DSLEEP 1
#endif

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
  // SH1107: Adafruit_SH110X (буфер/графика) + U8g2_for_Adafruit_GFX (UTF-8/кириллица).
  OledDisplayCompat()
      : oled_(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1, 400000, 100000) {}

  bool begin(int, int addr) {
    if (!oled_.begin((uint8_t)addr, true)) {
      return false;
    }
    oled_.clearDisplay();
    u8g_.begin(oled_);
    u8g_.setFontMode(1);  // transparent
    u8g_.setFontDirection(0);
    u8g_.setForegroundColor(SH110X_WHITE);
    u8g_.setBackgroundColor(SH110X_BLACK);
    setTextSize(1);
    oled_.display();
    return true;
  }

  void clearDisplay() { oled_.clearDisplay(); }
  void display() { oled_.display(); }

  void setTextSize(uint8_t size) {
    textSize_ = (uint8_t)constrain((int)size, 1, 4);
    applyFont();
  }
  void setTextColor(uint16_t) {
    u8g_.setForegroundColor(SH110X_WHITE);
  }
  void setRotation(uint8_t r) { oled_.setRotation(r); }

  /** 180° через Adafruit_GFX setRotation(2). */
  void setUpsideDown(bool upsideDown) { oled_.setRotation(upsideDown ? 2 : 0); }

  /** Курсор в координатах «верхний левый» (как у Adafruit/Gyver). */
  void setCursor(int16_t x, int16_t y) {
    cursorX_ = x;
    cursorY_ = y;
    syncU8gCursor();
  }

  int16_t width() const { return SCREEN_WIDTH; }
  int16_t height() const { return SCREEN_HEIGHT; }

  int16_t getTextWidth(const char *s) {
    applyFont();
    return u8g_.getUTF8Width(s ? s : "");
  }

  size_t print(const char *s) { return drawText(s, false); }
  size_t print(const String &s) { return drawText(s.c_str(), false); }
  size_t print(char c) {
    char buf[2] = {c, '\0'};
    return drawText(buf, false);
  }
  size_t print(int v) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", v);
    return drawText(buf, false);
  }
  size_t print(unsigned int v) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%u", v);
    return drawText(buf, false);
  }
  size_t print(long v) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%ld", v);
    return drawText(buf, false);
  }
  size_t print(unsigned long v) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%lu", v);
    return drawText(buf, false);
  }
  size_t print(float v) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%g", (double)v);
    return drawText(buf, false);
  }
  size_t println(const char *s) { return drawText(s, true); }
  size_t println(const String &s) { return drawText(s.c_str(), true); }
  size_t println(int v) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", v);
    return drawText(buf, true);
  }
  size_t println(unsigned int v) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%u", v);
    return drawText(buf, true);
  }
  template <typename T>
  size_t print(const T &v) {
    String s(v);
    return drawText(s.c_str(), false);
  }
  template <typename T>
  size_t println(const T &v) {
    String s(v);
    return drawText(s.c_str(), true);
  }

  int printf(const char *fmt, ...) {
    char buf[64];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    drawText(buf, false);
    return n;
  }

  void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t) {
    if (w <= 0 || h <= 0) {
      return;
    }
    oled_.drawRect(x, y, w, h, SH110X_WHITE);
  }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (w <= 0 || h <= 0) {
      return;
    }
    oled_.fillRect(x, y, w, h, color ? SH110X_WHITE : SH110X_BLACK);
  }
  void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
    if (w <= 0 || h <= 0) {
      return;
    }
    oled_.drawRoundRect(x, y, w, h, r, color ? SH110X_WHITE : SH110X_BLACK);
  }
  void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
    if (w <= 0 || h <= 0) {
      return;
    }
    oled_.fillRoundRect(x, y, w, h, r, color ? SH110X_WHITE : SH110X_BLACK);
  }
  void drawBitmap(int16_t x, int16_t y, const uint8_t *bmp, int16_t w, int16_t h, uint16_t) {
    if (!bmp || w <= 0 || h <= 0) {
      return;
    }
    oled_.drawBitmap(x, y, bmp, w, h, SH110X_WHITE);
  }

  void ssd1306_command(uint8_t cmd) {
    if (waitContrast_) {
      oled_.setContrast(cmd);
      waitContrast_ = false;
      return;
    }
    if (cmd == 0x81) {
      waitContrast_ = true;
    } else if (cmd == SH110X_DISPLAYON) {
      oled_.oled_command(SH110X_DISPLAYON);
    } else if (cmd == SH110X_DISPLAYOFF) {
      oled_.oled_command(SH110X_DISPLAYOFF);
    }
  }

 private:
  void applyFont() {
    switch (textSize_) {
      case 1:
        u8g_.setFont(u8g2_font_6x13_t_cyrillic);
        break;
      case 2:
        u8g_.setFont(u8g2_font_10x20_t_cyrillic);
        break;
      case 3:
        u8g_.setFont(u8g2_font_logisoso22_tn);
        break;
      default:
        u8g_.setFont(u8g2_font_logisoso32_tn);
        break;
    }
  }

  void syncU8gCursor() {
    applyFont();
    // U8g2: Y = baseline; снаружи держим top-left как у Adafruit.
    u8g_.setCursor(cursorX_, (int16_t)(cursorY_ + u8g_.getFontAscent()));
  }

  int16_t lineHeight() {
    applyFont();
    return (int16_t)(u8g_.getFontAscent() - u8g_.getFontDescent() + 1);
  }

  size_t drawText(const char *s, bool newline) {
    if (!s) {
      s = "";
    }
    syncU8gCursor();
    size_t n = u8g_.print(s);
    cursorX_ = u8g_.getCursorX();
    if (newline) {
      cursorX_ = 0;
      cursorY_ = (int16_t)(cursorY_ + lineHeight());
      syncU8gCursor();
    }
    return n;
  }

  Adafruit_SH1107 oled_;
  U8G2_FOR_ADAFRUIT_GFX u8g_;
  int16_t cursorX_ = 0;
  int16_t cursorY_ = 0;
  uint8_t textSize_ = 1;
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
RTC_DATA_ATTR bool displayUpsideDown = false;  // переворот OLED при «вверх ногами» (BMI160)
RTC_DATA_ATTR bool otaUpdateAvailable = false;
RTC_DATA_ATTR char otaAvailableVersion[24] = "";
RTC_DATA_ATTR char otaAvailableDate[20] = "";
RTC_DATA_ATTR char otaAvailableMessage[512] = "";

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
  uint8_t weatherUpdateHours;    // интервал NTP + погоды в часах (активный режим)
  uint8_t weatherScreenSeconds;  // длительность экрана деталей погоды по кнопке
  uint8_t tempSensorType;        // выбранный датчик температуры/влажности
  bool bmi160Enabled;            // BMI160: ориентация экрана по акселерометру (раз в минуту в runCycle)
  bool autoOtaEnabled;           // автоматическая проверка и установка OTA
  uint16_t autoOtaCheckHours;    // период проверки AutoOTA в часах
  uint8_t activeWeekdaysMask;    // биты 0..6 = ПН..ВС: 1=часы работают в этот день
  bool showSyncProgress;         // OLED: экран шагов NTP/погоды вместо главных часов (по умолч. выкл.)
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
  .weatherApiUrl = "https://api.open-meteo.com/v1/forecast?latitude=53.92&longitude=30.35&daily=weather_code,sunrise,sunset&hourly=temperature_2m,relative_humidity_2m,surface_pressure,apparent_temperature,wind_speed_10m,weather_code,precipitation_probability,precipitation,wind_direction_10m&current=temperature_2m,relative_humidity_2m,apparent_temperature,precipitation,weather_code,surface_pressure,wind_speed_10m,wind_direction_10m&timezone=Etc%2FGMT-3&past_days=0&forecast_days=4&wind_speed_unit=ms",
  .weatherUpdateHours = 1,
  .weatherScreenSeconds = 10,
  .tempSensorType = TEMP_SENSOR_SHT31,
  .bmi160Enabled = true,
  .autoOtaEnabled = true,
  .autoOtaCheckHours = AUTOOTA_CHECK_INTERVAL_HOURS_DEFAULT,
  .activeWeekdaysMask = WEEKDAY_MASK_ALL,
  .showSyncProgress = false
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

/** Open-Meteo timezone=… from settings.timezoneMinutes (Etc/GMT has inverted sign). */
static void formatOpenMeteoTimezoneParam(char *out, size_t outSize, int16_t tzMinutes) {
  if (!out || outSize == 0) {
    return;
  }
  if ((tzMinutes % 60) != 0) {
    // Получасовые пояса: пусть API выберет TZ по lat/lon
    snprintf(out, outSize, "auto");
    return;
  }
  const int hours = (int)tzMinutes / 60;
  if (hours == 0) {
    snprintf(out, outSize, "UTC");
  } else if (hours > 0) {
    // UTC+N → Etc/GMT-N
    snprintf(out, outSize, "Etc/GMT-%d", hours);
  } else {
    // UTC-N → Etc/GMT+N (в URL '+' → %2B)
    snprintf(out, outSize, "Etc/GMT+%d", -hours);
  }
}

static bool isValidLatitude(float lat) {
  return isfinite(lat) && lat >= -90.0f && lat <= 90.0f;
}

static bool isValidLongitude(float lon) {
  return isfinite(lon) && lon >= -180.0f && lon <= 180.0f;
}

static void rebuildOpenMeteoUrlFromCoordinates() {
  char tz[24];
  formatOpenMeteoTimezoneParam(tz, sizeof(tz), settings.timezoneMinutes);
  char tzEnc[40];
  if (strcmp(tz, "auto") == 0 || strcmp(tz, "UTC") == 0) {
    snprintf(tzEnc, sizeof(tzEnc), "%s", tz);
  } else if (strncmp(tz, "Etc/GMT+", 8) == 0) {
    snprintf(tzEnc, sizeof(tzEnc), "Etc%%2FGMT%%2B%s", tz + 8);
  } else if (strncmp(tz, "Etc/GMT-", 8) == 0) {
    snprintf(tzEnc, sizeof(tzEnc), "Etc%%2FGMT-%s", tz + 8);
  } else {
    snprintf(tzEnc, sizeof(tzEnc), "auto");
  }
  snprintf(settings.weatherApiUrl,
           WEATHER_API_URL_BUF_SIZE,
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&daily=weather_code,sunrise,sunset&hourly=temperature_2m,relative_humidity_2m,surface_pressure,apparent_temperature,wind_speed_10m,weather_code,precipitation_probability,precipitation,wind_direction_10m&current=temperature_2m,relative_humidity_2m,apparent_temperature,precipitation,weather_code,surface_pressure,wind_speed_10m,wind_direction_10m&timezone=%s&past_days=0&forecast_days=4&wind_speed_unit=ms",
           settings.weatherLatitude,
           settings.weatherLongitude,
           tzEnc);
}

/** Секунды до конца ночного режима (local wall clock). */
static uint32_t secondsUntilNightEnd(int hour, int min, int sec) {
  const int nowSec = hour * 3600 + min * 60 + sec;
  const int startSec = (int)settings.nightStartH * 3600 + (int)settings.nightStartM * 60;
  const int endSec = (int)settings.nightEndH * 3600 + (int)settings.nightEndM * 60;
  int delta;
  if (startSec < endSec) {
    // Ночь в пределах суток (напр. 01:00–07:00)
    delta = endSec - nowSec;
  } else {
    // Ночь через полночь (напр. 23:00–07:00)
    if (nowSec >= startSec) {
      delta = (24 * 3600 - nowSec) + endSec;
    } else {
      delta = endSec - nowSec;
    }
  }
  if (delta <= 0) {
    delta = 60;
  }
  return (uint32_t)delta;
}

static bool sensorOK = false;
static float tempC = 22.0;
static float hum = 50.0;
static bool indoorBmpOk = false;
static bool displayOn = true;
static bool wokeByWeatherButton = false;
static bool wokeByOtaButton = false;
static bool bmi160Ready = false;
static bool runManualOtaInstall();
static bool otaInfoButtonLongHoldConfirm(uint32_t holdMs, uint32_t alreadyHeldMs);
static bool factoryResetHoldConfirm(uint32_t holdMs, uint32_t alreadyHeldMs);
static void performFactoryResetAndReboot(const char *reason);
/** -1 = пропуск, 0 = ошибка проверки, 1 = проверка ок (с update или без). */
static int tryAutoOtaUpdate(time_t local, bool timeValid, bool night, bool workdayEnabled);

static constexpr uint32_t kFactoryResetBootHoldMs = 2000UL;
static constexpr uint32_t kFactoryResetRuntimeHoldMs = 5000UL;
static constexpr uint32_t kOtaShortClickMaxMs = 800UL;

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

static bool readDebouncedLow(uint8_t pin) {
  // Простая фильтрация дребезга: 5 быстрых чтений
  uint8_t lowCount = 0;
  for (uint8_t i = 0; i < 5; i++) {
    if (digitalRead(pin) == LOW) {
      lowCount++;
    }
    delay(2);
  }
  return lowCount >= 4;
}

bool isWeatherButtonPressed() {
  return readDebouncedLow(WEATHER_BUTTON_PIN);
}

/** GPIO0 совмещён с LED: перед чтением кнопки — INPUT_PULLUP. */
static void otaButtonPrepareInput() {
  pinMode(OTA_BUTTON_PIN, INPUT_PULLUP);
  gpio_pullup_en((gpio_num_t)OTA_BUTTON_PIN);
  gpio_pulldown_dis((gpio_num_t)OTA_BUTTON_PIN);
}

static void ledPrepareOutputOff() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
}

/** Краткий sample: PULLUP → read → сразу OUTPUT LOW (иначе LED «вполнакала» на всё окно). */
static bool otaButtonSampleLow() {
  otaButtonPrepareInput();
  delayMicroseconds(40);
  const bool low = (digitalRead(OTA_BUTTON_PIN) == LOW);
  ledPrepareOutputOff();
  return low;
}

bool isOtaInfoButtonPressed() {
  otaButtonPrepareInput();
  const bool pressed = readDebouncedLow(OTA_BUTTON_PIN);
  // Не оставлять PULLUP: через него LED на GPIO0 горит «вполнакала».
  ledPrepareOutputOff();
  return pressed;
}

static void fetchOtaManifestMeta(String *releaseDate, String *whatsNew) {
  if (releaseDate) {
    *releaseDate = "";
  }
  if (whatsNew) {
    *whatsNew = "";
  }
  HTTPClient http;
  if (!http.begin(AUTOOTA_MANIFEST_URL)) {
    return;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return;
  }
  String body = http.getString();
  http.end();

  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    return;
  }
  if (releaseDate && doc["releaseDate"].is<const char *>()) {
    *releaseDate = String((const char *)doc["releaseDate"]);
  }
  // Поддержка двух имен: новое поле whatsNew и старое notes.
  if (whatsNew) {
    if (doc["whatsNew"].is<const char *>()) {
      *whatsNew = String((const char *)doc["whatsNew"]);
    } else if (doc["notes"].is<const char *>()) {
      *whatsNew = String((const char *)doc["notes"]);
    }
  }
}

static void showOtaUpdateInfoScreen() {
  otaButtonPrepareInput();
  const char *msg = otaAvailableMessage[0] ? otaAvailableMessage : "(no details)";
  const size_t msgLen = strnlen(msg, sizeof(otaAvailableMessage) - 1);

  // UTF-8 safe wrap: режем по числу глифов, а не по байтам.
  constexpr uint8_t kGlyphsPerLine = 21;  // 21*6px = 126px, почти вся ширина 128px
  constexpr uint8_t kMaxWrappedLines = 32;
  char wrapped[kMaxWrappedLines][48];
  uint8_t wrappedCount = 0;
  {
    size_t i = 0;
    uint8_t lineGlyphs = 0;
    uint8_t lineBytes = 0;
    auto flushLine = [&]() {
      if (wrappedCount >= kMaxWrappedLines) return;
      wrapped[wrappedCount][lineBytes] = '\0';
      wrappedCount++;
      lineGlyphs = 0;
      lineBytes = 0;
    };

    while (i < msgLen && wrappedCount < kMaxWrappedLines) {
      uint8_t c = (uint8_t)msg[i];
      size_t chLen = 1;
      if ((c & 0x80) == 0x00) chLen = 1;
      else if ((c & 0xE0) == 0xC0) chLen = 2;
      else if ((c & 0xF0) == 0xE0) chLen = 3;
      else if ((c & 0xF8) == 0xF0) chLen = 4;
      if (i + chLen > msgLen) chLen = 1;

      if (lineGlyphs >= kGlyphsPerLine || (lineBytes + chLen + 1U) >= sizeof(wrapped[0])) {
        flushLine();
        if (wrappedCount >= kMaxWrappedLines) break;
      }

      for (size_t j = 0; j < chLen; j++) {
        wrapped[wrappedCount][lineBytes++] = msg[i + j];
      }
      lineGlyphs++;
      i += chLen;
    }
    if (lineBytes > 0 && wrappedCount < kMaxWrappedLines) {
      flushLine();
    }
    if (wrappedCount == 0) {
      strlcpy(wrapped[0], "(no details)", sizeof(wrapped[0]));
      wrappedCount = 1;
    }
  }

  const uint8_t firstPageLines = 5;
  const uint8_t nextPageLines = 8;
  const int16_t firstPageLineStep = 12;
  const int16_t nextPageLineStep = 13;
  uint8_t totalPages = 1;
  if (wrappedCount > firstPageLines) {
    uint8_t rem = (uint8_t)(wrappedCount - firstPageLines);
    totalPages = (uint8_t)(1U + (rem + nextPageLines - 1U) / nextPageLines);
  }
  uint8_t page = 0;
  bool prevLow = (digitalRead(OTA_BUTTON_PIN) == LOW);
  uint32_t lastActionMs = millis();
  uint32_t pressStartMs = 0;
  bool pressActive = false;
  constexpr uint32_t kShortPressMaxMs = 400UL;
  constexpr uint32_t kHoldInstallMs = 2000UL;

  auto drawPage = [&](uint8_t pageIdx) {
    uint8_t lineFrom = 0;
    uint8_t rowCount = 0;
    int16_t textY0 = 0;
    int16_t lineStep = firstPageLineStep;
    applyDisplayOrientation();
    display.clearDisplay();
    display.setTextSize(1);

    if (pageIdx == 0) {
      display.setCursor(0, 0);
      display.print(ROM_VERSION);
      display.print(" > ");
      display.println(otaAvailableVersion[0] ? otaAvailableVersion : "unknown");
      display.setCursor(0, 10);
      display.print("Date: ");
      display.println(otaAvailableDate[0] ? otaAvailableDate : "-");
      display.setCursor(0, 24);
      display.print("GPIO0: лист / hold");
      lineFrom = 0;
      rowCount = firstPageLines;
      textY0 = 40;
      lineStep = firstPageLineStep;
    } else {
      lineFrom = (uint8_t)(firstPageLines + (pageIdx - 1U) * nextPageLines);
      rowCount = nextPageLines;
      textY0 = 0;
      lineStep = nextPageLineStep;
    }

    for (uint8_t row = 0; row < rowCount; row++) {
      uint8_t lineIdx = (uint8_t)(lineFrom + row);
      if (lineIdx >= wrappedCount) {
        break;
      }
      display.setCursor(0, textY0 + row * lineStep);
      display.println(wrapped[lineIdx]);
    }
    display.display();
  };

  drawPage(page);
  while ((uint32_t)(millis() - lastActionMs) < 9000UL) {
    bool low = (digitalRead(OTA_BUTTON_PIN) == LOW);
    if (low && !prevLow) {
      pressActive = true;
      pressStartMs = millis();
    }

    if (low && pressActive) {
      uint32_t held = (uint32_t)(millis() - pressStartMs);
      if (held >= kShortPressMaxMs) {
        // Долгое удержание: прогрессбар до kHoldInstallMs (учитываем уже удержанное).
        if (otaInfoButtonLongHoldConfirm(kHoldInstallMs, held)) {
          ledPrepareOutputOff();
          runManualOtaInstall();
          return;
        }
        // Отпустили до конца — вернуть текущую страницу.
        pressActive = false;
        drawPage(page);
        lastActionMs = millis();
      }
    }

    if (!low && prevLow && pressActive) {
      uint32_t held = (uint32_t)(millis() - pressStartMs);
      pressActive = false;
      if (held < kShortPressMaxMs) {
        page = (uint8_t)((page + 1U) % totalPages);
        drawPage(page);
        lastActionMs = millis();
      }
    }

    if (!low) {
      pressActive = false;
    }
    prevLow = low;
    delay(20);
  }
  ledPrepareOutputOff();
}

/** STA connect: channel 0 = любой (не фиксировать 15 — это не TX power). */
static bool connectWifiSta(uint32_t timeoutMs = 30000UL) {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(80);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_15dBm);
  WiFi.begin(wifiSSID, wifiPassword);  // channel 0: сканировать все каналы
  uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < timeoutMs) {
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}

static bool ensureWiFiConnectedForOta() {
  return connectWifiSta(30000UL);
}

static bool runManualOtaInstall() {
  const uint32_t t0 = millis();
  setCpuPerformance();
  applyDisplayOrientation();
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("OTA install...");
  display.setCursor(0, 12);
  display.println("WiFi connect");
  display.display();

  if (!ensureWiFiConnectedForOta()) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("OTA failed");
    display.setCursor(0, 12);
    display.println("WiFi error");
    display.display();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    setCpuLowPower();
    return false;
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("OTA install...");
  display.setCursor(0, 12);
  display.println("Check update");
  char tbuf0[20];
  snprintf(tbuf0, sizeof(tbuf0), "t=%lus", (unsigned long)((millis() - t0) / 1000UL));
  display.setCursor(0, 24);
  display.println(tbuf0);
  display.display();

  String newVersion;
  String notes;
  String binPath;
  bool hasInfo = autoOta.checkUpdate(&newVersion, &notes, &binPath);
  if (!hasInfo || !autoOta.hasUpdate() || !isRemoteFirmwareNewer(newVersion.c_str())) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("No update");
    display.display();
    clearOtaUpdateAvailable();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    setCpuLowPower();
    return false;
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Installing...");
  display.setCursor(0, 12);
  display.println(newVersion);
  char tbuf[20];
  snprintf(tbuf, sizeof(tbuf), "t=%lus", (unsigned long)((millis() - t0) / 1000UL));
  display.setCursor(0, 24);
  display.println(tbuf);
  display.drawRect(0, 112, 128, 7, SSD1306_WHITE);
  display.fillRect(1, 113, 126, 5, SSD1306_WHITE);
  display.display();

  bool ok = autoOta.updateNow();
  if (!ok && autoOta.hasError()) {
    Serial.printf("[AutoOTA] manual updateNow error: %d\n", (int)autoOta.getError());
  }
  if (!ok) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Install failed");
    display.display();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    setCpuLowPower();
    return false;
  }

  // При успехе updateNow() обычно перезагружает устройство. На случай возврата:
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Install done");
  display.setCursor(0, 12);
  display.println("Rebooting...");
  display.display();
  delay(1200);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  setCpuLowPower();
  return true;
}

static bool otaInfoButtonLongHoldConfirm(uint32_t holdMs, uint32_t alreadyHeldMs) {
  const uint32_t start = millis();
  const int16_t barX = 0;
  const int16_t barY = 112;
  const int16_t barW = 128;
  const int16_t barH = 7;
  if (alreadyHeldMs >= holdMs) {
    return digitalRead(OTA_BUTTON_PIN) == LOW;
  }
  const uint32_t needMore = holdMs - alreadyHeldMs;
  while ((uint32_t)(millis() - start) < needMore) {
    if (digitalRead(OTA_BUTTON_PIN) != LOW) {
      return false;
    }
    uint32_t elapsed = alreadyHeldMs + (uint32_t)(millis() - start);
    uint32_t filled = (elapsed >= holdMs) ? (uint32_t)(barW - 2) : ((uint32_t)(barW - 2) * elapsed) / holdMs;
    uint32_t leftMs = (elapsed >= holdMs) ? 0UL : (holdMs - elapsed);
    applyDisplayOrientation();
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("Install ");
    display.println(otaAvailableVersion[0] ? otaAvailableVersion : "update");
    char timerBuf[20];
    snprintf(timerBuf, sizeof(timerBuf), "Hold %lu.%lus",
             (unsigned long)(leftMs / 1000UL),
             (unsigned long)((leftMs % 1000UL) / 100UL));
    display.setCursor(0, 12);
    display.println(timerBuf);
    display.drawRect(barX, barY, barW, barH, SSD1306_WHITE);
    if (filled > 0) {
      display.fillRect(barX + 1, barY + 1, (int16_t)filled, barH - 2, SSD1306_WHITE);
    }
    display.display();
    delay(30);
  }
  return true;
}

static bool factoryResetHoldConfirm(uint32_t holdMs, uint32_t alreadyHeldMs) {
  const uint32_t start = millis();
  const int16_t barX = 0;
  const int16_t barY = 112;
  const int16_t barW = 128;
  const int16_t barH = 7;
  if (alreadyHeldMs >= holdMs) {
    return digitalRead(OTA_BUTTON_PIN) == LOW;
  }
  const uint32_t needMore = holdMs - alreadyHeldMs;
  while ((uint32_t)(millis() - start) < needMore) {
    if (digitalRead(OTA_BUTTON_PIN) != LOW) {
      return false;
    }
    uint32_t elapsed = alreadyHeldMs + (uint32_t)(millis() - start);
    uint32_t filled = (elapsed >= holdMs) ? (uint32_t)(barW - 2) : ((uint32_t)(barW - 2) * elapsed) / holdMs;
    uint32_t leftMs = (elapsed >= holdMs) ? 0UL : (holdMs - elapsed);
    applyDisplayOrientation();
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Settings reset");
    display.setCursor(0, 14);
    display.println("Hold GPIO0...");
    char timerBuf[20];
    snprintf(timerBuf, sizeof(timerBuf), "%lu.%lus left",
             (unsigned long)(leftMs / 1000UL),
             (unsigned long)((leftMs % 1000UL) / 100UL));
    display.setCursor(0, 28);
    display.println(timerBuf);
    display.drawRect(barX, barY, barW, barH, SSD1306_WHITE);
    if (filled > 0) {
      display.fillRect(barX + 1, barY + 1, (int16_t)filled, barH - 2, SSD1306_WHITE);
    }
    display.display();
    delay(30);
  }
  return true;
}

static void performFactoryResetAndReboot(const char *reason) {
  clearWiFiConfig();
  Serial.printf("Config reset (%s)\n", reason ? reason : "GPIO0");
  applyDisplayOrientation();
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(CODE_CONFIG_RESET);
  display.setCursor(0, 14);
  display.println("WiFi cleared");
  display.setCursor(0, 28);
  display.println("Rebooting...");
  display.display();
  ledPrepareOutputOff();
  delay(1200);
  ESP.restart();
}

void applyDisplayOrientation() {
  display.setUpsideDown(displayUpsideDown);
}

void updateDisplayOrientationFromBmi160() {
  if (!settings.bmi160Enabled || !bmi160Ready) {
#if BMI160_ORIENT_DIAG_SERIAL
    static uint32_t s_lastSkipLogMs;
    if (settings.bmi160Enabled && !bmi160Ready &&
        (uint32_t)(millis() - s_lastSkipLogMs) > 120000UL) {
      s_lastSkipLogMs = millis();
      Serial.println("[BMI160] orient: bmi160Ready=false (нет чипа / ошибка init)");
    }
#endif
    return;
  }
  BMI160AccelSample s{};
  if (!readBMI160Accel(s)) {
#if BMI160_ORIENT_DIAG_SERIAL
    Serial.println("[BMI160] orient: readBMI160Accel failed (I2C?)");
#endif
    return;
  }

  const int thr = 2200;
#if BMI160_ORIENT_AXIS == 0
  const int16_t gAxis = s.x;
  const char axisCh = 'X';
#elif BMI160_ORIENT_AXIS == 1
  const int16_t gAxis = s.y;
  const char axisCh = 'Y';
#elif BMI160_ORIENT_AXIS == 2
  const int16_t gAxis = s.z;
  const char axisCh = 'Z';
#else
#error BMI160_ORIENT_AXIS must be 0, 1, or 2
#endif

  const int ag = abs((int)gAxis);
  const bool thrOk = ag >= thr;
#if BMI160_ORIENT_DIAG_SERIAL
  Serial.printf("[BMI160] orient: ax=%d ay=%d az=%d axis=%c g=%d |g|=%d thr=%d ok=%d flipNow=%d\n",
                (int)s.x,
                (int)s.y,
                (int)s.z,
                axisCh,
                (int)gAxis,
                ag,
                thr,
                (int)thrOk,
                (int)displayUpsideDown);
#endif
  if (!thrOk) {
    return;
  }

  bool upside = (gAxis < 0);
#if BMI160_ORIENT_INVERT
  upside = !upside;
#endif
  if (upside != displayUpsideDown) {
    displayUpsideDown = upside;
    applyDisplayOrientation();
    Serial.printf("[BMI160] orient CHANGED upsideDown=%d (axis=%c g=%d)\n", (int)upside, axisCh, (int)gAxis);
  }
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
  // Только overlay при чтении: storedEpoch двигаем без этой поправки (иначе двойной учёт).
  int64_t correctionSeconds = ((int64_t)elapsed * (int64_t)settings.timeCorrectionPerDay) / 86400LL;
  return baseEpoch + (time_t)correctionSeconds;
}

void logToDisplay(const char *code, const char *detail, uint16_t holdMs) {
  if (!settings.showDebugCodes) {
    return;
  }
  setDisplayState(true);
  setBrightness(0x01);
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
    settings.bmi160Enabled = true;
    settings.autoOtaEnabled = true;
    settings.autoOtaCheckHours = AUTOOTA_CHECK_INTERVAL_HOURS_DEFAULT;
    settings.activeWeekdaysMask = WEEKDAY_MASK_ALL;
    settings.showSyncProgress = false;
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
  // Новый bool в конце struct: старый EEPROM даёт 0xFF → не считать включённым.
  settings.showSyncProgress = (*(const uint8_t *)&settings.showSyncProgress == 1);

  displayUpsideDown = !!displayUpsideDown;
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
  displayCfg["showSyncProgress"] = settings.showSyncProgress;

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
    if (displayCfg.containsKey("showSyncProgress")) newSettings.showSyncProgress = displayCfg["showSyncProgress"];
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

static void clearOtaUpdateAvailable() {
  otaUpdateAvailable = false;
  otaAvailableVersion[0] = '\0';
  otaAvailableDate[0] = '\0';
  otaAvailableMessage[0] = '\0';
}

/** Разбор A1.4.10 / 1.4.9 → major.minor.patch (префикс-буквы пропускаются). */
static bool parseFirmwareVersionTriplet(const char *s, int &maj, int &minv, int &pat) {
  maj = minv = pat = 0;
  if (!s || !*s) {
    return false;
  }
  while (*s && (*s < '0' || *s > '9')) {
    ++s;
  }
  if (!*s) {
    return false;
  }
  maj = atoi(s);
  const char *dot1 = strchr(s, '.');
  if (!dot1) {
    return true;
  }
  minv = atoi(dot1 + 1);
  const char *dot2 = strchr(dot1 + 1, '.');
  if (!dot2) {
    return true;
  }
  pat = atoi(dot2 + 1);
  return true;
}

/** >0 remote новее, 0 равно, <0 remote старше. Числовое сравнение, не strcmp. */
static int compareFirmwareVersions(const char *remote, const char *local) {
  int rMaj = 0, rMin = 0, rPat = 0, lMaj = 0, lMin = 0, lPat = 0;
  const bool rOk = parseFirmwareVersionTriplet(remote, rMaj, rMin, rPat);
  const bool lOk = parseFirmwareVersionTriplet(local, lMaj, lMin, lPat);
  if (rOk && lOk) {
    if (rMaj != lMaj) {
      return rMaj - lMaj;
    }
    if (rMin != lMin) {
      return rMin - lMin;
    }
    return rPat - lPat;
  }
  if (!remote) {
    remote = "";
  }
  if (!local) {
    local = "";
  }
  return strcmp(remote, local);
}

static bool isRemoteFirmwareNewer(const char *remote) {
  return compareFirmwareVersions(remote, ROM_VERSION) > 0;
}

/** Сбросить RTC-флаг, если «доступная» версия не новее текущей (AutoOTA считает update любую ≠). */
static void sanitizeOtaUpdateFlag() {
  if (!otaUpdateAvailable) {
    return;
  }
  if (!otaAvailableVersion[0] || !isRemoteFirmwareNewer(otaAvailableVersion)) {
    clearOtaUpdateAvailable();
  }
}

/** -1 = пропуск, 0 = ошибка проверки, 1 = проверка ок (с update или без). */
static int tryAutoOtaUpdate(time_t local, bool timeValid, bool night, bool workdayEnabled) {
#if AUTOOTA_ENABLED
  if (!shouldCheckAutoOta(local, timeValid, night, workdayEnabled)) {
    return -1;
  }
  lastAutoOtaCheckEpoch = local;

  float vBat = readBattery();
  if (vBat < AUTOOTA_MIN_BATTERY_V) {
    Serial.printf("[AutoOTA] Skipped: battery %.2fV < %.2fV\n", vBat, AUTOOTA_MIN_BATTERY_V);
    return -1;
  }

  String newVersion;
  String notes;
  String binPath;
  bool hasInfo = autoOta.checkUpdate(&newVersion, &notes, &binPath);
  if (!hasInfo) {
    if (autoOta.hasError()) {
      Serial.printf("[AutoOTA] checkUpdate error: %d\n", (int)autoOta.getError());
      return 0;
    }
    Serial.println("[AutoOTA] checkUpdate: no update info");
    return 1;
  }
  if (!autoOta.hasUpdate()) {
    Serial.println("[AutoOTA] No update");
    clearOtaUpdateAvailable();
    return 1;
  }

  // Библиотека AutoOTA: любая version != текущей → update (в т.ч. откат A1.4.9 при локальной A1.4.10).
  if (!isRemoteFirmwareNewer(newVersion.c_str())) {
    Serial.printf("[AutoOTA] Ignore non-newer remote %s (current %s)\n",
                  newVersion.c_str(), ROM_VERSION);
    clearOtaUpdateAvailable();
    return 1;
  }

  Serial.printf("[AutoOTA] Update found: %s -> %s\n", ROM_VERSION, newVersion.c_str());
  if (!notes.isEmpty()) {
    Serial.printf("[AutoOTA] Notes: %s\n", notes.c_str());
  }
  Serial.printf("[AutoOTA] Bin: %s\n", binPath.c_str());
  otaUpdateAvailable = true;
  strlcpy(otaAvailableVersion, newVersion.c_str(), sizeof(otaAvailableVersion));
  String releaseDate;
  String whatsNew;
  fetchOtaManifestMeta(&releaseDate, &whatsNew);
  strlcpy(otaAvailableDate, releaseDate.c_str(), sizeof(otaAvailableDate));
  strlcpy(otaAvailableMessage, whatsNew.c_str(), sizeof(otaAvailableMessage));
  Serial.println("[AutoOTA] Update is available (manual confirm pending)");
  return 1;
#else
  (void)local;
  (void)timeValid;
  (void)night;
  (void)workdayEnabled;
  return -1;
#endif
}

// Иконка батареи (как на телефоне): скруглённый корпус, заливка без пустых углов
void drawBattery(uint8_t bars) {
  const int16_t bodyW = 20;
  const int16_t bodyH = 10;
  const int16_t tabW = 2;
  const int16_t tabH = 4;
  const int16_t radius = 2;
  const int16_t r2 = radius * radius;
  int16_t right = (int16_t)display.width();
  int16_t bx = right - tabW - bodyW;
  const int16_t tabY = (bodyH - tabH) / 2;

  const int16_t innerW = bodyW - 2;
  const int16_t fillLimit =
      (bars >= BAT_STEPS) ? bodyW
                          : (int16_t)(1 + (int16_t)((uint32_t)bars * innerW / BAT_STEPS));

  // Заливка тем же скруглением, что и контур (без «дыр» в углах)
  if (bars > 0) {
    for (int16_t py = 0; py < bodyH; ++py) {
      for (int16_t px = 0; px < bodyW; ++px) {
        bool inside = true;
        if (px < radius && py < radius) {
          const int16_t dx = (int16_t)(px - radius);
          const int16_t dy = (int16_t)(py - radius);
          inside = (dx * dx + dy * dy) <= r2;
        } else if (px >= bodyW - radius && py < radius) {
          const int16_t dx = (int16_t)(px - (bodyW - 1 - radius));
          const int16_t dy = (int16_t)(py - radius);
          inside = (dx * dx + dy * dy) <= r2;
        } else if (px < radius && py >= bodyH - radius) {
          const int16_t dx = (int16_t)(px - radius);
          const int16_t dy = (int16_t)(py - (bodyH - 1 - radius));
          inside = (dx * dx + dy * dy) <= r2;
        } else if (px >= bodyW - radius && py >= bodyH - radius) {
          const int16_t dx = (int16_t)(px - (bodyW - 1 - radius));
          const int16_t dy = (int16_t)(py - (bodyH - 1 - radius));
          inside = (dx * dx + dy * dy) <= r2;
        }
        if (!inside || px >= fillLimit) {
          continue;
        }
        display.fillRect((int16_t)(bx + px), py, 1, 1, SSD1306_WHITE);
      }
    }
  }

  display.drawRoundRect(bx, 0, bodyW, bodyH, radius, SSD1306_WHITE);
  display.fillRect(right - tabW, tabY, tabW, tabH, SSD1306_WHITE);
}

static void drawOtaAvailableIcon(int16_t x) {
  const int16_t y = 1;
  display.drawRect(x, y + 4, 10, 6, SSD1306_WHITE);
  display.fillRect(x + 4, y, 2, 5, SSD1306_WHITE);
  display.fillRect(x + 3, y + 3, 4, 2, SSD1306_WHITE);
  display.fillRect(x + 4, y - 1, 1, 1, SSD1306_WHITE);
}

/*
* Вывод на экран
*/

static void formatSignedIntCompact(char *out, size_t outSize, float t) {
  if (isnan(t) || !out || outSize == 0) {
    if (out && outSize) {
      snprintf(out, outSize, "--");
    }
    return;
  }
  int v = (int)lroundf(t);
  if (v > 0) {
    snprintf(out, outSize, "+%d", v);
  } else {
    snprintf(out, outSize, "%d", v);
  }
}

void drawClock(int d, int mo, int h, int m, uint8_t batBars, uint8_t wday) {
  sanitizeOtaUpdateFlag();
  updateDisplayOrientationFromBmi160();
  applyDisplayOrientation();
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  const int16_t topY = 0;
  const int16_t screenW = (int16_t)display.width();
  const int16_t batReserve = 22;
  int16_t rightLimit = (int16_t)(screenW - batReserve);

  // Верх: день недели → дата → indoor T/RH → OTA → батарея
  int16_t x = 0;
  if (settings.showWeekday) {
    const char *label;
    if (settings.weekdayLanguageRu) {
      static const char *ruDays[] = { "ВС", "ПН", "ВТ", "СР", "ЧТ", "ПТ", "СБ" };
      label = ruDays[wday % 7];
    } else {
      static const char *enDays[] = { "SU", "MO", "TU", "WE", "TH", "FR", "SA" };
      label = enDays[wday % 7];
    }
    drawDayShort(wday, 0, topY);
    x = 4 + display.getTextWidth(label) + 4;
  }
  if (settings.showDate) {
    char dateBuf[8];
    snprintf(dateBuf, sizeof(dateBuf), "%02d.%02d", d, mo);
    display.setCursor(x, topY);
    display.print(dateBuf);
    x = (int16_t)(x + display.getTextWidth(dateBuf) + 4);
  }

  if (otaUpdateAvailable) {
    rightLimit = (int16_t)(rightLimit - 14);
    drawOtaAvailableIcon(rightLimit);
  }
  drawBattery(batBars);

  // Indoor: температура и влажность между датой и OTA/батареей
  {
    char tBuf[8];
    snprintf(tBuf, sizeof(tBuf), "%d", (int)tempC);
    char hBuf[8];
    snprintf(hBuf, sizeof(hBuf), "%d%%", (int)hum);
    const int16_t degW = 4;
    const int16_t gap = 3;
    const int16_t needW =
        (int16_t)(display.getTextWidth(tBuf) + degW + gap + display.getTextWidth(hBuf));
    if (x + needW <= rightLimit) {
      display.setCursor(x, topY);
      display.print(tBuf);
      const int16_t degX = (int16_t)(x + display.getTextWidth(tBuf) + 1);
      drawDegreeMark(degX, topY);
      display.setCursor((int16_t)(degX + degW + gap - 1), topY);
      display.print(hBuf);
    }
  }

  // Крупное время (+ иконка текущей погоды справа, если есть данные)
  int displayH = h;
  if (!settings.timeFormat24h) {
    displayH = h % 12;
    if (displayH == 0) displayH = 12;
  }

  const bool showWeather = settings.weatherEnabled && !isnan(outdoorTemperature);
  const int16_t timeY = 22;   // только HH:MM
  const int16_t iconY = 16;   // иконка текущей погоды (без сдвига времени)
  const int16_t iconSize = 21;  // meteocon scale 1
  const int16_t iconGap = 3;
  const int16_t iconX = showWeather ? (int16_t)(screenW - iconSize - 2) : screenW;
  const int16_t timeAreaW = showWeather ? (int16_t)(iconX - iconGap) : screenW;

  display.setTextSize(4);
  char lBuf[3], rBuf[3];
  snprintf(lBuf, sizeof(lBuf), "%02d", displayH);
  snprintf(rBuf, sizeof(rBuf), "%02d", m);

  const int colonSpace = 12;
  const int16_t fullW = (int16_t)(display.getTextWidth(lBuf) + display.getTextWidth(rBuf) + colonSpace);
  int startX = ((int)timeAreaW - fullW) / 2;
  if (startX < 0) {
    startX = 0;
  }

  display.setCursor(startX, timeY);
  display.print(lBuf);

  const int colonX = startX + display.getTextWidth(lBuf) + (colonSpace - 3) / 2;
  const int colonYtop = timeY + 8;
  const int colonYbot = colonYtop + 3 + 10;
  display.fillRect(colonX, colonYtop, 3, 3, SSD1306_WHITE);
  display.fillRect(colonX, colonYbot, 3, 3, SSD1306_WHITE);

  const int minX = startX + display.getTextWidth(lBuf) + colonSpace;
  display.setCursor(minX, timeY);
  display.print(rBuf);

  if (showWeather) {
    const bool nightIcon = (h < NIGHT_END_H || h >= NIGHT_START_H);
    drawWeatherIcon(display, iconX, iconY, 1, weatherWmoCode, weatherWindSpeedMs, nightIcon);

    // Под иконкой: температура, затем вероятность осадков
    display.setTextSize(1);
    char outBuf[8];
    formatSignedIntCompact(outBuf, sizeof(outBuf), outdoorTemperature);
    int16_t tw = display.getTextWidth(outBuf);
    const int16_t underIconY = (int16_t)(iconY + iconSize + 1);
    int16_t tX = (int16_t)(iconX + (iconSize - tw - 4) / 2);
    if (tX < 0) {
      tX = iconX;
    }
    display.setCursor(tX, underIconY);
    display.print(outBuf);
    if (!isnan(outdoorTemperature)) {
      drawDegreeMark((int16_t)(tX + tw + 1), underIconY);
    }

    char popBuf[8];
    bool showPop = false;
    if (!isnan(weatherPrecipProbabilityPct)) {
      const int popPct = (int)lroundf(weatherPrecipProbabilityPct);
      if (popPct > 0) {
        snprintf(popBuf, sizeof(popBuf), "%d%%", popPct);
        showPop = true;
      }
    }
    if (showPop) {
      tw = display.getTextWidth(popBuf);
      tX = (int16_t)(iconX + (iconSize - tw) / 2);
      display.setCursor(tX, (int16_t)(underIconY + 12));
      display.print(popBuf);
    }

    // Три колонки: час → иконка → T → PoP (всегда h+1, h+2, h+3)
    int8_t colHour[kWeatherHourlyAheadCount];
    float colTemp[kWeatherHourlyAheadCount];
    float colPop[kWeatherHourlyAheadCount];
    int32_t colWmo[kWeatherHourlyAheadCount];
    weatherHourlyAheadForClock(h, colHour, colTemp, colPop, colWmo);

    const int16_t colY0 = 66;
    const int16_t colW = screenW / 3;
    for (int k = 0; k < kWeatherHourlyAheadCount; ++k) {
      const int16_t colLeft = (int16_t)(k * colW);
      const int16_t colCenter = (int16_t)(colLeft + colW / 2);
      const int16_t iconColX = (int16_t)(colCenter - iconSize / 2);

      char hourBuf[4];
      if (colHour[k] < 0) {
        snprintf(hourBuf, sizeof(hourBuf), "--");
      } else {
        snprintf(hourBuf, sizeof(hourBuf), "%02d", (int)colHour[k]);
      }
      display.setTextSize(1);
      tw = display.getTextWidth(hourBuf);
      // Две ° после часа → визуально «HH°°» ≈ прогноз на HH:00
      constexpr int16_t kHourDegW = 3;
      constexpr int16_t kHourDegGap = 1;
      const int16_t hourMarksW =
          (colHour[k] >= 0) ? (int16_t)(1 + kHourDegW + kHourDegGap + kHourDegW) : 0;
      const int16_t hourTotalW = (int16_t)(tw + hourMarksW);
      const int16_t hourX = (int16_t)(colCenter - hourTotalW / 2);
      display.setCursor(hourX, colY0);
      display.print(hourBuf);
      if (colHour[k] >= 0) {
        const int16_t d0 = (int16_t)(hourX + tw + 1);
        drawDegreeMark(d0, colY0);
        drawDegreeMark((int16_t)(d0 + kHourDegW + kHourDegGap), colY0);
      }

      const int16_t iconY = (int16_t)(colY0 + 12);
      const int colH = (colHour[k] >= 0) ? (int)colHour[k] : ((h + 1 + k) % 24);
      const bool colNight = (colH < NIGHT_END_H || colH >= NIGHT_START_H);
      if (colWmo[k] >= 0) {
        drawWeatherIcon(display, iconColX, iconY, 1, colWmo[k], NAN, colNight);
      }

      const int16_t tempY = (int16_t)(iconY + iconSize + 1);
      char tCol[8];
      formatSignedIntCompact(tCol, sizeof(tCol), colTemp[k]);
      tw = display.getTextWidth(tCol);
      const int16_t txCol = (int16_t)(colCenter - (tw + 4) / 2);
      display.setCursor(txCol, tempY);
      display.print(tCol);
      if (!isnan(colTemp[k])) {
        drawDegreeMark((int16_t)(txCol + tw + 1), tempY);
      }

      if (!isnan(colPop[k])) {
        const int popPct = (int)lroundf(colPop[k]);
        if (popPct > 0) {
          char pCol[8];
          snprintf(pCol, sizeof(pCol), "%d%%", popPct);
          tw = display.getTextWidth(pCol);
          display.setCursor((int16_t)(colCenter - tw / 2), (int16_t)(tempY + 12));
          display.print(pCol);
        }
      }
    }
  }

  display.display();
  displayBackupValid = false;
}

bool ntpSync() {
  logToDisplay(CODE_WIFI_CONNECT, nullptr, 0);
  setCpuPerformance();

  if (!connectWifiSta(30000UL)) {
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

static SyncProgressState syncProgress = {};

static const char *syncStepMark(SyncStepStatus st) {
  switch (st) {
    case SYNC_STEP_RUN: return "...";
    case SYNC_STEP_OK: return "OK";
    case SYNC_STEP_FAIL: return "ERR";
    case SYNC_STEP_SKIP: return "-";
    default: return " ";
  }
}

static void drawSyncProgressScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 2);
  display.print("Синхронизация");

  display.setCursor(0, 22);
  display.print("WiFi");
  display.setCursor(88, 22);
  display.print(syncStepMark(syncProgress.wifi));

  display.setCursor(0, 40);
  display.print("NTP");
  display.setCursor(88, 40);
  display.print(syncStepMark(syncProgress.ntp));

  display.setCursor(0, 58);
  display.print("OTA");
  display.setCursor(88, 58);
  display.print(syncStepMark(syncProgress.ota));

  display.setCursor(0, 76);
  display.print("Погода");
  display.setCursor(88, 76);
  display.print(syncStepMark(syncProgress.weather));

  if (syncProgress.finished) {
    display.setCursor(0, 100);
    display.print(syncProgress.overallOk ? "Готово" : "Ошибка");
  }

  display.display();
}

static bool syncProgressEnabled() {
  return settings.showSyncProgress;
}

static void syncProgressBegin() {
  if (!syncProgressEnabled()) {
    return;
  }
  syncProgress = {};
  syncProgress.wifi = SYNC_STEP_WAIT;
  syncProgress.ntp = SYNC_STEP_WAIT;
  syncProgress.ota = SYNC_STEP_WAIT;
  syncProgress.weather = settings.weatherEnabled ? SYNC_STEP_WAIT : SYNC_STEP_SKIP;
  setDisplayState(true);
  setBrightness(0x01);
  updateDisplayOrientationFromBmi160();
  applyDisplayOrientation();
  drawSyncProgressScreen();
}

static void syncProgressSet(SyncStepStatus *field, SyncStepStatus st) {
  if (!syncProgressEnabled() || !field) {
    return;
  }
  *field = st;
  drawSyncProgressScreen();
}

static void syncProgressFinish(bool overallOk) {
  if (!syncProgressEnabled()) {
    return;
  }
  syncProgress.finished = true;
  syncProgress.overallOk = overallOk;
  drawSyncProgressScreen();
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

  // Ориентацию по BMI160 читаем не здесь, а непосредственно перед отрисовкой: до drawClock()
  // может уйти много времени на WiFi/NTP/погоду — иначе переворот «после пробуждения» не попадает в выборку.

  // NTP + погода по одному интервалу (weatherUpdateHours), только в активном дневном режиме
  if (timeValid && workdayEnabled && !night && shouldUpdateNetwork(local, settings.weatherUpdateHours, settings.weatherEnabled)) {
    logToDisplay(CODE_WEATHER_FETCH);
    setCpuPerformance();
    syncProgressBegin();

    char detail[32];
    syncProgressSet(&syncProgress.wifi, SYNC_STEP_RUN);
    const bool wifiOk = connectWifiSta(30000UL);
    wl_status_t wifiStatus = WiFi.status();
    syncProgressSet(&syncProgress.wifi, wifiOk ? SYNC_STEP_OK : SYNC_STEP_FAIL);
    snprintf(detail, sizeof(detail), "Final st=%d", (int)wifiStatus);
    logToDisplay(CODE_WEATHER_FETCH, detail);
    Serial.printf("[Net] STA connect %s (st=%d)\n", wifiOk ? "ok" : "fail", (int)wifiStatus);

    bool ntpOk = false;
    bool weatherOk = !settings.weatherEnabled;
    int otaRc = -1;
    if (wifiOk) {
      syncProgressSet(&syncProgress.ntp, SYNC_STEP_RUN);
      ntpOk = ntpSyncOverConnectedWiFi();
      syncProgressSet(&syncProgress.ntp, ntpOk ? SYNC_STEP_OK : SYNC_STEP_FAIL);
      local = applyDriftCorrection(storedEpoch, lastSyncLocalEpoch);
      local = applyTimeCorrection(local, lastSyncLocalEpoch);
      timeValid = hasValidTime(local);
      if (timeValid) {
        localtime_r(&local, &ti);
      }

      syncProgressSet(&syncProgress.ota, SYNC_STEP_RUN);
      otaRc = tryAutoOtaUpdate(local, timeValid, night, workdayEnabled);
      if (otaRc < 0) {
        syncProgressSet(&syncProgress.ota, SYNC_STEP_SKIP);
      } else {
        syncProgressSet(&syncProgress.ota, (otaRc > 0) ? SYNC_STEP_OK : SYNC_STEP_FAIL);
      }

      if (settings.weatherEnabled) {
        syncProgressSet(&syncProgress.weather, SYNC_STEP_RUN);
        weatherOk = fetchOutdoorTemperature(settings.weatherApiUrl, WEATHER_SOURCE_OPEN_METEO);
        syncProgressSet(&syncProgress.weather, weatherOk ? SYNC_STEP_OK : SYNC_STEP_FAIL);
        if (weatherOk) {
          lastSuccessfulWeatherLocalEpoch = local;
          snprintf(detail, sizeof(detail), "T=%d", (int)outdoorTemperature);
          logToDisplay(CODE_WEATHER_OK, detail);
        } else {
          logToDisplay(CODE_WEATHER_ERROR);
        }
      }
      Serial.printf("[Net] NTP=%d weather=%d ota=%d\n", (int)ntpOk, (int)weatherOk, otaRc);
    } else {
      syncProgressSet(&syncProgress.ntp, SYNC_STEP_SKIP);
      syncProgressSet(&syncProgress.ota, SYNC_STEP_SKIP);
      if (settings.weatherEnabled) {
        syncProgressSet(&syncProgress.weather, SYNC_STEP_SKIP);
      }
      snprintf(detail, sizeof(detail), "Status=%d", (int)wifiStatus);
      logToDisplay(CODE_WEATHER_WIFI_FAIL, detail);
    }

    lastNetworkUpdate = local;
    // Короткий retry, если NTP или погода не удались (кеш T на экране может оставаться валидным).
    lastNetworkNtpOk = ntpOk && weatherOk;

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    setCpuLowPower();

    const bool overallOk = wifiOk && ntpOk && weatherOk && (otaRc != 0);
    syncProgressFinish(overallOk);
  }

  if (!timeValid) {
    logToDisplay(CODE_NTP_ERROR, "Wait NTP", 0);
  } else if (!night && workdayEnabled) {
    setDisplayState(true);
    setBrightness(0x01);
    updateDisplayOrientationFromBmi160();
    applyDisplayOrientation();

    bool weatherButtonPressedNow = isWeatherButtonPressed();
    bool otaButtonPressedNow = isOtaInfoButtonPressed();
    if (otaUpdateAvailable && (wokeByOtaButton || otaButtonPressedNow)) {
      showOtaUpdateInfoScreen();
    } else if (wokeByWeatherButton || weatherButtonPressedNow) {
      // Страницы погоды (0..6) + статус (7): версия, последняя NTP/погода.
      uint32_t detailEndMs = millis() + (uint32_t)settings.weatherScreenSeconds * 1000UL;
      uint8_t detailPage = 0;
      bool prevBtnLow = (digitalRead(WEATHER_BUTTON_PIN) == LOW);
      auto redrawDetailPage = [&](uint8_t page) {
        if (page < kWeatherDetailScreenCount) {
          drawWeatherDetailScreen(display,
                                  page,
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
        } else {
          drawSyncStatusScreen(display,
                               ROM_VERSION,
                               lastSyncLocalEpoch,
                               lastSuccessfulWeatherLocalEpoch,
                               (uint8_t)(page + 1));
        }
      };
      redrawDetailPage(detailPage);
      while ((int32_t)(millis() - detailEndMs) < 0) {
        bool low = (digitalRead(WEATHER_BUTTON_PIN) == LOW);
        if (low && !prevBtnLow) {
          detailPage = (detailPage + 1) % kDetailScreenCount;
          detailEndMs = millis() + (uint32_t)settings.weatherScreenSeconds * 1000UL;
          redrawDetailPage(detailPage);
          delay(45);
        }
        prevBtnLow = low;
        delay(12);
      }
    }

    drawClock(ti.tm_mday, ti.tm_mon + 1, ti.tm_hour, ti.tm_min, batBars, ti.tm_wday);
    // Если пользователь перевернул часы сразу после пробуждения,
    // второе чтение BMI160 может зафиксировать изменение знака и перерисовать картинку.
    if (settings.bmi160Enabled && bmi160Ready) {
      const bool upsideAfterDraw = displayUpsideDown;
      updateDisplayOrientationFromBmi160();
      if (displayUpsideDown != upsideAfterDraw) {
        drawClock(ti.tm_mday, ti.tm_mon + 1, ti.tm_hour, ti.tm_min, batBars, ti.tm_wday);
      }
    }

    // GPIO0: короткое нажатие → OTA (если есть), удержание ~5 с → сброс WiFi.
    // Sample импульсами (не держим PULLUP — иначе LED горит вполнакала всё окно).
    {
      const uint32_t idleWaitMs = otaUpdateAvailable ? 2500UL : 400UL;
      const uint32_t waitEnd = millis() + idleWaitMs;
      uint32_t pressAt = 0;
      bool holding = false;
      bool prevLow = otaButtonSampleLow();
      if (prevLow) {
        holding = true;
        pressAt = millis();
      }
      bool didAction = false;
      while (!didAction) {
        const uint32_t now = millis();
        const bool low = otaButtonSampleLow();
        if (low && !prevLow) {
          holding = true;
          pressAt = now;
        }
        if (holding && low) {
          const uint32_t held = (uint32_t)(now - pressAt);
          if (held >= 800UL) {
            otaButtonPrepareInput();  // continuous read на экране сброса
            if (factoryResetHoldConfirm(kFactoryResetRuntimeHoldMs, held)) {
              performFactoryResetAndReboot("GPIO0 hold after clock");
            }
            ledPrepareOutputOff();
            drawClock(ti.tm_mday, ti.tm_mon + 1, ti.tm_hour, ti.tm_min, batBars, ti.tm_wday);
            didAction = true;
            break;
          }
        }
        if (!low && prevLow && holding) {
          const uint32_t held = (uint32_t)(now - pressAt);
          holding = false;
          if (held < kOtaShortClickMaxMs && otaUpdateAvailable) {
            showOtaUpdateInfoScreen();
          }
          didAction = true;
          break;
        }
        prevLow = low;
        if (!holding && !low && (int32_t)(now - waitEnd) >= 0) {
          break;
        }
        delay(20);
      }
      ledPrepareOutputOff();
    }
  } else {
    display.clearDisplay();
    display.display();
    setDisplayState(false);
  }

  if (timeValid && workdayEnabled && (ti.tm_min == 0) && !night && settings.hourlyBlink) {
    ledPrepareOutputOff();
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
    } else if (night) {
      // Не будить каждую минуту ночью — сон до nightEnd (GPIO4 всё ещё будит).
      sleepSeconds = secondsUntilNightEnd(ti.tm_hour, ti.tm_min, ti.tm_sec);
    } else {
      int secToNextMinute = 60 - ti.tm_sec;
      if (secToNextMinute <= 0) {
        secToNextMinute = 60;
      }
      sleepSeconds = (uint32_t)secToNextMinute;
    }
    uint32_t activeSeconds = ((millis() - cycleStartMs) + 500) / 1000;
    uint32_t elapsedSeconds = activeSeconds + sleepSeconds;

    // storedEpoch — «сырое» RTC-время; ручная коррекция только в applyTimeCorrection().
    storedEpoch = storedEpoch + elapsedSeconds;
  } else {
    sleepSeconds = 30;
  }

  return sleepSeconds;
}

void enterDeepSleep(uint32_t sleepSeconds) {
  if (sleepSeconds == 0) {
    sleepSeconds = 60;
  }
#if BMI160_SUSPEND_BEFORE_DSLEEP
  if (settings.bmi160Enabled && bmi160Ready) {
    if (bmi160SuspendAccelForSleep()) {
#if BMI160_ORIENT_DIAG_SERIAL
      Serial.println("[BMI160] accel suspend before deep sleep");
#endif
    }
  }
#endif
  // Пробуждение только GPIO4 (погода). GPIO0 = LED: в сне держим OUTPUT LOW,
  // иначе INPUT_PULLUP подсвечивает светодиод всю минуту сна.
  // OTA-кнопка читается в активной фазе (таймер / после иконки обновления).
  pinMode(WEATHER_BUTTON_PIN, INPUT_PULLUP);
  gpio_pullup_en((gpio_num_t)WEATHER_BUTTON_PIN);
  gpio_pulldown_dis((gpio_num_t)WEATHER_BUTTON_PIN);
  ledPrepareOutputOff();
  esp_deep_sleep_enable_gpio_wakeup((1ULL << WEATHER_BUTTON_PIN), ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_sleep_enable_timer_wakeup((uint64_t)sleepSeconds * 1000000ULL);
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(9600);
  esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
  esp_reset_reason_t resetReason = esp_reset_reason();
  wokeByWeatherButton = false;
  wokeByOtaButton = false;
  if (wakeCause == ESP_SLEEP_WAKEUP_GPIO) {
    uint64_t wakeMask = esp_sleep_get_gpio_wakeup_status();
    wokeByWeatherButton = (wakeMask & (1ULL << WEATHER_BUTTON_PIN)) != 0;
    wokeByOtaButton = (wakeMask & (1ULL << OTA_BUTTON_PIN)) != 0;
  }

  // GPIO0 = LED + кнопка: до проверки сброса не переводим в OUTPUT (иначе ложный LOW
  // из‑за LED/ёмкости → clearWiFiConfig → вечный SoftAP после Save/restart).
  otaButtonPrepareInput();
  pinMode(SETUP_BUTTON_PIN, INPUT_PULLUP);
  delay(20);

  // Сброс WiFi: не после ESP.restart() и не после deep sleep (ложный SoftAP / LED).
  // Иначе — холодный старт / USB / EXT и т.п. при удержании GPIO0 ~2 с.
  const bool fromDeepSleep =
      (wakeCause == ESP_SLEEP_WAKEUP_GPIO) ||
      (wakeCause == ESP_SLEEP_WAKEUP_TIMER) ||
      (resetReason == ESP_RST_DEEPSLEEP);
  const bool fromSoftRestart = (resetReason == ESP_RST_SW);
  const bool allowFactoryReset = !fromDeepSleep && !fromSoftRestart && !wokeByOtaButton;
  bool resetRequested = false;
  if (allowFactoryReset && digitalRead(OTA_BUTTON_PIN) == LOW) {
    resetRequested = true;
    const uint32_t holdStart = millis();
    while ((uint32_t)(millis() - holdStart) < kFactoryResetBootHoldMs) {
      if (digitalRead(OTA_BUTTON_PIN) != LOW) {
        resetRequested = false;
        break;
      }
      delay(10);
    }
  }
  bool setupModeRequested = (digitalRead(SETUP_BUTTON_PIN) == LOW);
  ledPrepareOutputOff();

  setCpuLowPower();

  if (resetRequested) {
    clearWiFiConfig();
    Serial.println("Config reset by GPIO0 hold at boot");
    delay(500);
  }
  Serial.printf("Boot: wake=%d rst=%d otaWake=%d resetCfg=%d setupBtn=%d\n",
                (int)wakeCause,
                (int)resetReason,
                (int)wokeByOtaButton,
                (int)resetRequested,
                (int)setupModeRequested);

  analogSetPinAttenuation(BAT_PIN, ADC_11db);
  pinMode(BAT_PIN, INPUT);
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
#if defined(ARDUINO_ARCH_ESP32) && defined(WIRE_HAS_TIMEOUT)
  Wire.setTimeOut(50);
#endif

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
  applyDisplayOrientation();

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
  otaButtonPrepareInput();
  Serial.printf("OTA/LED button GPIO%d state=%d\n", OTA_BUTTON_PIN, digitalRead(OTA_BUTTON_PIN));
  ledPrepareOutputOff();
  if (settings.bmi160Enabled) {
    pinMode(BMI160_INT1_PIN, INPUT_PULLDOWN);
    gpio_pullup_dis((gpio_num_t)BMI160_INT1_PIN);
    gpio_pulldown_en((gpio_num_t)BMI160_INT1_PIN);
    bmi160Ready = initBMI160Sensor();
    if (bmi160Ready) {
      updateDisplayOrientationFromBmi160();
    }
    logToDisplay(bmi160Ready ? CODE_BMI160_OK : CODE_BMI160_ERR);
    Serial.printf("BMI160 INT1 GPIO%d state=%d ready=%d\n",
                  BMI160_INT1_PIN,
                  digitalRead(BMI160_INT1_PIN),
                  (int)bmi160Ready);
  } else {
    bmi160Ready = false;
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