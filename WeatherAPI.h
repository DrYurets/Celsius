#ifndef WEATHER_API_H
#define WEATHER_API_H

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <cmath>
#include <time.h>

// Open-Meteo Forecast API: https://open-meteo.com/en/docs

// Объявление функции логирования (определена в основном файле)
void logToDisplay(const char *code, const char *detail = nullptr, uint16_t holdMs = 1000);

// В JSON число может быть int или float; as<double>() в ArduinoJson приводит и то и другое надёжнее, чем только as<float>().
static inline float weatherJsonFloatOrNan(JsonObject obj, const char *key) {
  if (!obj.containsKey(key)) {
    return NAN;
  }
  JsonVariant v = obj[key];
  if (v.isNull()) {
    return NAN;
  }
  return (float)v.as<double>();
}

static inline int32_t weatherJsonIntOrNeg1(JsonObject obj, const char *key) {
  if (!obj.containsKey(key)) {
    return -1;
  }
  JsonVariant v = obj[key];
  if (v.isNull()) {
    return -1;
  }
  return v.as<int32_t>();
}

// Переменные для хранения температуры
RTC_DATA_ATTR float outdoorTemperature = NAN;
RTC_DATA_ATTR float previousOutdoorTemperature = NAN;
RTC_DATA_ATTR time_t lastNetworkUpdate = 0;   // последний WiFi-сеанс (NTP + опционально погода)
RTC_DATA_ATTR bool lastNetworkNtpOk = true;   // короткий интервал повтора при ошибке NTP
/** Локальный epoch последней успешной загрузки погоды (0 = ещё не было). */
RTC_DATA_ATTR time_t lastSuccessfulWeatherLocalEpoch = 0;
RTC_DATA_ATTR float weatherPressureHpa = NAN;
RTC_DATA_ATTR float weatherHumidityPct = NAN;
RTC_DATA_ATTR float weatherWindSpeedMs = NAN;
RTC_DATA_ATTR float weatherWindDirectionDeg = NAN;
RTC_DATA_ATTR float weatherFeelsLikeC = NAN;
// Open-Meteo WMO weather_code (current.weather_code)
RTC_DATA_ATTR int32_t weatherWmoCode = -1;
RTC_DATA_ATTR float weatherPrecipProbabilityPct = NAN;
RTC_DATA_ATTR int32_t weatherDailyWmoCode[4] = { -1, -1, -1, -1 };
RTC_DATA_ATTR float weatherDailyTempMaxC[4] = { NAN, NAN, NAN, NAN };
RTC_DATA_ATTR float weatherDailyTempMinC[4] = { NAN, NAN, NAN, NAN };
RTC_DATA_ATTR float weatherDailyWindDayMs[4] = { NAN, NAN, NAN, NAN };
RTC_DATA_ATTR float weatherDailyPrecipDayPct[4] = { NAN, NAN, NAN, NAN };
RTC_DATA_ATTR float weatherNearestNightMinC = NAN;
RTC_DATA_ATTR int32_t weatherNearestNightWmoCode = -1;
/** Восход/закат на локальную дату weatherSunLocalDate (YYYYMMDD); часы/минуты wall-clock, -1 = нет данных. */
RTC_DATA_ATTR uint32_t weatherSunLocalDate = 0;
RTC_DATA_ATTR int8_t weatherSunriseHour = -1;
RTC_DATA_ATTR int8_t weatherSunriseMin = -1;
RTC_DATA_ATTR int8_t weatherSunsetHour = -1;
RTC_DATA_ATTR int8_t weatherSunsetMin = -1;
/** Кеш hourly для главного экрана: от текущего часа API ≥24 слотов; на экране — 3 часа после часа на часах. */
constexpr int kWeatherHourlyAheadCount = 3;
constexpr int kWeatherHourlyCacheCount = 24;
RTC_DATA_ATTR int8_t weatherHourlyHour[kWeatherHourlyCacheCount];
RTC_DATA_ATTR float weatherHourlyTempC[kWeatherHourlyCacheCount];
RTC_DATA_ATTR float weatherHourlyPrecipPct[kWeatherHourlyCacheCount];
RTC_DATA_ATTR int32_t weatherHourlyWmoCode[kWeatherHourlyCacheCount];
RTC_DATA_ATTR uint8_t weatherHourlyValidCount = 0;

static inline void clearWeatherHourlyCache() {
  weatherHourlyValidCount = 0;
  for (int k = 0; k < kWeatherHourlyCacheCount; ++k) {
    weatherHourlyHour[k] = -1;
    weatherHourlyTempC[k] = NAN;
    weatherHourlyPrecipPct[k] = NAN;
    weatherHourlyWmoCode[k] = -1;
  }
}

static inline int weatherHourFromIso(const String &iso) {
  // YYYY-MM-DDTHH:MM...
  if (iso.length() < 13) {
    return -1;
  }
  int hour = iso.substring(11, 13).toInt();
  if (hour < 0 || hour > 23) {
    return -1;
  }
  return hour;
}

/** YYYY-MM-DDTHH:MM… → hour/min; false если формат битый. */
static inline bool weatherHmFromIso(const String &iso, int8_t &hour, int8_t &min) {
  hour = -1;
  min = -1;
  if (iso.length() < 16) {
    return false;
  }
  int h = iso.substring(11, 13).toInt();
  int m = iso.substring(14, 16).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) {
    return false;
  }
  hour = (int8_t)h;
  min = (int8_t)m;
  return true;
}

static inline uint32_t weatherLocalDateKeyFromTm(const struct tm &ti) {
  return (uint32_t)(ti.tm_year + 1900) * 10000UL +
         (uint32_t)(ti.tm_mon + 1) * 100UL +
         (uint32_t)ti.tm_mday;
}

static inline uint32_t weatherLocalDateKeyFromIsoDate(const String &isoDate) {
  // YYYY-MM-DD…
  if (isoDate.length() < 10) {
    return 0;
  }
  int y = isoDate.substring(0, 4).toInt();
  int mo = isoDate.substring(5, 7).toInt();
  int d = isoDate.substring(8, 10).toInt();
  if (y < 2000 || mo < 1 || mo > 12 || d < 1 || d > 31) {
    return 0;
  }
  return (uint32_t)y * 10000UL + (uint32_t)mo * 100UL + (uint32_t)d;
}

static inline bool weatherSunTimesValidForLocal(time_t local) {
  if (weatherSunriseHour < 0 || weatherSunsetHour < 0 || weatherSunLocalDate == 0) {
    return false;
  }
  struct tm ti = {};
  localtime_r(&local, &ti);
  return weatherSunLocalDate == weatherLocalDateKeyFromTm(ti);
}

/** Нужны свежие sunrise/sunset на текущие локальные сутки. */
static inline bool needSunTimesRefresh(time_t local, bool weatherEnabled) {
  if (!weatherEnabled || local <= 0) {
    return false;
  }
  return !weatherSunTimesValidForLocal(local);
}

/** Три колонки: clockHour+1, +2, +3 из кеша (сдвигается при смене часа на часах). */
static inline void weatherHourlyAheadForClock(int clockHour,
                                             int8_t outHour[kWeatherHourlyAheadCount],
                                             float outTemp[kWeatherHourlyAheadCount],
                                             float outPop[kWeatherHourlyAheadCount],
                                             int32_t outWmo[kWeatherHourlyAheadCount]) {
  for (int k = 0; k < kWeatherHourlyAheadCount; ++k) {
    outHour[k] = -1;
    outTemp[k] = NAN;
    outPop[k] = NAN;
    outWmo[k] = -1;
  }
  if (weatherHourlyValidCount == 0 || clockHour < 0 || clockHour > 23) {
    return;
  }

  int base = -1;
  for (uint8_t i = 0; i < weatherHourlyValidCount; ++i) {
    if (weatherHourlyHour[i] == (int8_t)clockHour) {
      base = (int)i;
      break;
    }
  }

  int startIdx = -1;
  if (base >= 0) {
    startIdx = base + 1;
  } else {
    const int8_t want = (int8_t)((clockHour + 1) % 24);
    for (uint8_t i = 0; i < weatherHourlyValidCount; ++i) {
      if (weatherHourlyHour[i] == want) {
        startIdx = (int)i;
        break;
      }
    }
  }
  if (startIdx < 0) {
    return;
  }

  for (int k = 0; k < kWeatherHourlyAheadCount; ++k) {
    const int idx = startIdx + k;
    if (idx >= (int)weatherHourlyValidCount) {
      break;
    }
    outHour[k] = weatherHourlyHour[idx];
    outTemp[k] = weatherHourlyTempC[idx];
    outPop[k] = weatherHourlyPrecipPct[idx];
    outWmo[k] = weatherHourlyWmoCode[idx];
  }
}

static inline bool isLeapYear(int y) {
  return ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
}

static inline bool nextDateIso10(const String &isoDate, String &nextDate) {
  if (isoDate.length() < 10) return false;
  int y = isoDate.substring(0, 4).toInt();
  int m = isoDate.substring(5, 7).toInt();
  int d = isoDate.substring(8, 10).toInt();
  if (y < 1970 || m < 1 || m > 12 || d < 1 || d > 31) return false;
  static const uint8_t mdays[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
  int dim = mdays[m - 1];
  if (m == 2 && isLeapYear(y)) dim = 29;
  d++;
  if (d > dim) {
    d = 1;
    m++;
    if (m > 12) {
      m = 1;
      y++;
    }
  }
  char buf[11];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d);
  nextDate = String(buf);
  return true;
}

bool fetchOutdoorTemperature(const char* apiUrl, uint8_t weatherSource) {
  (void)weatherSource;  // источник фиксирован: Open-Meteo
  // Проверка WiFi (должна быть выполнена перед вызовом, но оставляем для безопасности)
  wl_status_t wifiStatus = WiFi.status();
  if (wifiStatus != WL_CONNECTED) {
    char detail[32];
    snprintf(detail, sizeof(detail), "WiFi not conn %d", wifiStatus);
    logToDisplay("Weather HTTP err", detail);
    Serial.printf("[Weather] Error: WiFi not connected (status=%d)\n", wifiStatus);
    return false;  // WiFi не подключен
  }

  HTTPClient http;
  String url = String(apiUrl);
  
  char detail[64];
  snprintf(detail, sizeof(detail), "URL len=%d", url.length());
  logToDisplay("Weather HTTP start", detail);
  Serial.println("[Weather] Request: " + url);

  // Проверка доступности WiFi перед началом запроса
  if (WiFi.status() != WL_CONNECTED) {
    snprintf(detail, sizeof(detail), "WiFi lost %d", WiFi.status());
    logToDisplay("Weather HTTP err", detail);
    Serial.printf("[Weather] Error: WiFi lost (status=%d)\n", WiFi.status());
    return false;
  }

  // Проверка IP адреса устройства (для диагностики)
  IPAddress deviceIP = WiFi.localIP();
  if (deviceIP[0] == 0) {
    logToDisplay("Weather HTTP err", "No device IP");
    Serial.println("[Weather] Error: No device IP");
    return false;
  }
  
  snprintf(detail, sizeof(detail), "Device IP=%d.%d.%d.%d", deviceIP[0], deviceIP[1], deviceIP[2], deviceIP[3]);
  logToDisplay("Weather HTTP start", detail);
  Serial.printf("[Weather] Device IP: %d.%d.%d.%d\n", deviceIP[0], deviceIP[1], deviceIP[2], deviceIP[3]);

  // HTTPClient сам разрешает DNS при подключении
  http.begin(url);
  
  http.setTimeout(20000);  // Увеличенный таймаут до 20 секунд
  http.setConnectTimeout(15000);  // Таймаут подключения 15 секунд
  http.setReuse(true);  // Переиспользование соединения
  
  int httpCode = http.GET();

  snprintf(detail, sizeof(detail), "Code=%d", httpCode);
  logToDisplay("Weather HTTP code", detail);
  Serial.printf("[Weather] HTTP code: %d\n", httpCode);

  // Детальная диагностика ошибки -1
  if (httpCode == -1) {
    logToDisplay("Weather HTTP err", "Connection failed");
    Serial.println("[Weather] Error: Connection failed (HTTP -1)");

    // Попытка получить размер ответа для диагностики
    int contentLength = http.getSize();
    snprintf(detail, sizeof(detail), "Size=%d", contentLength);
    logToDisplay("Weather HTTP err", detail);
    
    // Проверка строки ошибки
    String errorString = http.errorToString(httpCode);
    if (errorString.length() > 0) {
      char errDetail[64];
      errorString.toCharArray(errDetail, 64);
      logToDisplay("Weather HTTP err", errDetail);
      Serial.println("[Weather] " + errorString);
    }

    http.end();
    return false;
  }
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    http.end();

    snprintf(detail, sizeof(detail), "Len=%d", payload.length());
    logToDisplay("Weather HTTP code", detail);
    Serial.printf("[Weather] Response length: %d\n", payload.length());
    Serial.println("[Weather] Response body: " + payload);

    // Показываем первые символы ответа для диагностики
    if (payload.length() > 0) {
      String preview = payload.substring(0, min(50, (int)payload.length()));
      char previewDetail[64];
      snprintf(previewDetail, sizeof(previewDetail), "Resp: %s", preview.c_str());
      logToDisplay("Weather HTTP code", previewDetail);
    }
    
    // URL теперь может включать current+hourly+daily на 3 дня, поэтому буфер JSON заметно больше.
    DynamicJsonDocument doc(65536);
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
      snprintf(detail, sizeof(detail), "Err=%s", error.c_str());
      logToDisplay("Weather JSON err", detail);
      Serial.printf("[Weather] JSON parse error: %s\n", error.c_str());
      return false;
    }
    
    // Показываем все ключи в корневом объекте для диагностики
    JsonObject root = doc.as<JsonObject>();
    String keys = "";
    for (JsonPair kv : root) {
      if (keys.length() > 0) keys += ",";
      keys += kv.key().c_str();
    }
    if (keys.length() > 0) {
      char keysDetail[64];
      snprintf(keysDetail, sizeof(keysDetail), "Keys: %s", keys.c_str());
      logToDisplay("Weather HTTP code", keysDetail);
      Serial.println("[Weather] JSON keys: " + keys);
    }

    // Open-Meteo: https://open-meteo.com/en/docs — блок "current"
    if (!doc.containsKey("current") || !doc["current"].is<JsonObject>()) {
      logToDisplay("Weather no data", "No 'current' object");
      Serial.println("[Weather] Error: No 'current' object in JSON");
      return false;
    }
    JsonObject cur = doc["current"].as<JsonObject>();
    if (!cur.containsKey("temperature_2m")) {
      logToDisplay("Weather no data", "No temperature_2m");
      Serial.println("[Weather] Error: No 'temperature_2m' in current");
      return false;
    }

    float temp = cur["temperature_2m"].as<float>();
    if (isnan(temp)) {
      logToDisplay("Weather no data", "Bad temperature_2m");
      Serial.println("[Weather] Error: temperature_2m is NaN");
      return false;
    }

    previousOutdoorTemperature = outdoorTemperature;
    outdoorTemperature = round(temp);
    weatherFeelsLikeC =
      (cur.containsKey("apparent_temperature") && !cur["apparent_temperature"].isNull())
        ? cur["apparent_temperature"].as<float>()
        : NAN;
    weatherHumidityPct =
      (cur.containsKey("relative_humidity_2m") && !cur["relative_humidity_2m"].isNull())
        ? cur["relative_humidity_2m"].as<float>()
        : NAN;
    weatherPressureHpa =
      (cur.containsKey("surface_pressure") && !cur["surface_pressure"].isNull())
        ? cur["surface_pressure"].as<float>()
        : NAN;
    if (cur.containsKey("wind_speed_10m") && !cur["wind_speed_10m"].isNull()) {
      float w = cur["wind_speed_10m"].as<float>();
      bool useMs = false;
      if (doc.containsKey("current_units") && doc["current_units"].is<JsonObject>()) {
        JsonObject cu = doc["current_units"].as<JsonObject>();
        if (cu.containsKey("wind_speed_10m")) {
          String u = cu["wind_speed_10m"].as<String>();
          if (u.indexOf("m/s") >= 0) {
            useMs = true;
          }
        }
      }
      weatherWindSpeedMs = useMs ? w : (w / 3.6f);
    } else {
      weatherWindSpeedMs = NAN;
    }
    weatherWindDirectionDeg = weatherJsonFloatOrNan(cur, "wind_direction_10m");
    weatherWmoCode = weatherJsonIntOrNeg1(cur, "weather_code");
    weatherPrecipProbabilityPct = NAN;
    clearWeatherHourlyCache();

    weatherNearestNightMinC = NAN;
    weatherNearestNightWmoCode = -1;

    if (doc.containsKey("hourly") && doc["hourly"].is<JsonObject>() &&
        cur.containsKey("time") && !cur["time"].isNull()) {
      JsonObject hourly = doc["hourly"].as<JsonObject>();
      size_t currentHourIdx = (size_t)-1;
      if (hourly.containsKey("time") && hourly["time"].is<JsonArray>() &&
          hourly.containsKey("precipitation_probability") && hourly["precipitation_probability"].is<JsonArray>()) {
        JsonArray times = hourly["time"].as<JsonArray>();
        JsonArray probs = hourly["precipitation_probability"].as<JsonArray>();
        String currentIso = cur["time"].as<String>();
        String currentHour = currentIso;
        if (currentHour.length() >= 13) {
          currentHour = currentHour.substring(0, 13);
        }
        size_t n = times.size();
        if (probs.size() < n) n = probs.size();
        for (size_t i = 0; i < n; ++i) {
          JsonVariant t = times[i];
          if (!t.isNull()) {
            String hourlyIso = t.as<String>();
            bool exactMatch = (currentIso == hourlyIso);
            bool sameHourMatch = (!exactMatch && currentHour.length() >= 13 && hourlyIso.length() >= 13 &&
                                  currentHour == hourlyIso.substring(0, 13));
            if (!exactMatch && !sameHourMatch) {
              continue;
            }
            currentHourIdx = i;
            if (!probs[i].isNull()) {
              weatherPrecipProbabilityPct = probs[i].as<float>();
            }
            break;
          }
        }
      }

      // Кеш ~24 часов от текущего часа API (включая текущий) — сдвиг колонок по часу на часах.
      if (currentHourIdx != (size_t)-1 &&
          hourly.containsKey("time") && hourly["time"].is<JsonArray>() &&
          hourly.containsKey("temperature_2m") && hourly["temperature_2m"].is<JsonArray>()) {
        JsonArray hTime = hourly["time"].as<JsonArray>();
        JsonArray hTemp = hourly["temperature_2m"].as<JsonArray>();
        JsonArray hPop;
        JsonArray hCode;
        bool hasPop = hourly.containsKey("precipitation_probability") &&
                      hourly["precipitation_probability"].is<JsonArray>();
        bool hasCode = hourly.containsKey("weather_code") && hourly["weather_code"].is<JsonArray>();
        if (hasPop) {
          hPop = hourly["precipitation_probability"].as<JsonArray>();
        }
        if (hasCode) {
          hCode = hourly["weather_code"].as<JsonArray>();
        }
        size_t n = hTime.size();
        if (hTemp.size() < n) n = hTemp.size();
        if (hasPop && hPop.size() < n) n = hPop.size();
        if (hasCode && hCode.size() < n) n = hCode.size();
        weatherHourlyValidCount = 0;
        for (int k = 0; k < kWeatherHourlyCacheCount; ++k) {
          size_t idx = currentHourIdx + (size_t)k;
          if (idx >= n) {
            break;
          }
          String iso = hTime[idx].as<String>();
          weatherHourlyHour[k] = (int8_t)weatherHourFromIso(iso);
          weatherHourlyTempC[k] = hTemp[idx].isNull() ? NAN : hTemp[idx].as<float>();
          weatherHourlyPrecipPct[k] =
              (hasPop && !hPop[idx].isNull()) ? hPop[idx].as<float>() : NAN;
          weatherHourlyWmoCode[k] =
              (hasCode && !hCode[idx].isNull()) ? hCode[idx].as<int32_t>() : -1;
          weatherHourlyValidCount = (uint8_t)(k + 1);
        }
      }

      // Минимум ближайшей ночи: от 21:00 сегодняшнего дня до 08:59 следующего.
      if (hourly.containsKey("time") && hourly["time"].is<JsonArray>() &&
          hourly.containsKey("temperature_2m") && hourly["temperature_2m"].is<JsonArray>()) {
        JsonArray hTime = hourly["time"].as<JsonArray>();
        JsonArray hTemp = hourly["temperature_2m"].as<JsonArray>();
        JsonArray hCode;
        bool hasCode = hourly.containsKey("weather_code") && hourly["weather_code"].is<JsonArray>();
        if (hasCode) hCode = hourly["weather_code"].as<JsonArray>();

        String curIso = cur["time"].as<String>(); // YYYY-MM-DDTHH:MM
        if (curIso.length() >= 13) {
          String d0 = curIso.substring(0, 10);
          String d1;
          if (nextDateIso10(d0, d1)) {
            size_t n = hTime.size();
            if (hTemp.size() < n) n = hTemp.size();
            if (hasCode && hCode.size() < n) n = hCode.size();
            for (size_t i = 0; i < n; ++i) {
              if (hTime[i].isNull() || hTemp[i].isNull()) continue;
              String tIso = hTime[i].as<String>();
              if (tIso.length() < 13) continue;
              String d = tIso.substring(0, 10);
              int hh = tIso.substring(11, 13).toInt();
              bool inNight = ((d == d0 && hh >= 21) || (d == d1 && hh <= 8));
              if (!inNight) continue;
              float t = hTemp[i].as<float>();
              if (isnan(weatherNearestNightMinC) || t < weatherNearestNightMinC) {
                weatherNearestNightMinC = t;
                if (hasCode && !hCode[i].isNull()) weatherNearestNightWmoCode = hCode[i].as<int32_t>();
              }
            }
          }
        }
      }
    }

    for (uint8_t i = 0; i < 4; ++i) {
      weatherDailyWmoCode[i] = -1;
      weatherDailyTempMaxC[i] = NAN;
      weatherDailyTempMinC[i] = NAN;
      weatherDailyWindDayMs[i] = NAN;
      weatherDailyPrecipDayPct[i] = NAN;
    }
    if (doc.containsKey("daily") && doc["daily"].is<JsonObject>()) {
      JsonObject daily = doc["daily"].as<JsonObject>();
      if (daily.containsKey("weather_code") && daily["weather_code"].is<JsonArray>()) {
        JsonArray a = daily["weather_code"].as<JsonArray>();
        for (uint8_t i = 0; i < 4 && i < a.size(); ++i) {
          if (!a[i].isNull()) weatherDailyWmoCode[i] = a[i].as<int32_t>();
        }
      }
      // sunrise/sunset: берём слот daily на «сегодня» по daily.time (обычно [0]).
      if (daily.containsKey("sunrise") && daily["sunrise"].is<JsonArray>() &&
          daily.containsKey("sunset") && daily["sunset"].is<JsonArray>() &&
          daily.containsKey("time") && daily["time"].is<JsonArray>()) {
        JsonArray dayTime = daily["time"].as<JsonArray>();
        JsonArray sunr = daily["sunrise"].as<JsonArray>();
        JsonArray suns = daily["sunset"].as<JsonArray>();
        int pick = -1;
        // Предпочитаем индекс 0; если есть несколько дней — всё равно берём первый валидный.
        for (size_t di = 0; di < dayTime.size() && di < sunr.size() && di < suns.size(); ++di) {
          if (dayTime[di].isNull() || sunr[di].isNull() || suns[di].isNull()) {
            continue;
          }
          pick = (int)di;
          break;
        }
        if (pick >= 0) {
          String dayIso = dayTime[pick].as<String>();
          String riseIso = sunr[pick].as<String>();
          String setIso = suns[pick].as<String>();
          int8_t rh = -1, rm = -1, sh = -1, sm = -1;
          uint32_t ymd = weatherLocalDateKeyFromIsoDate(dayIso);
          if (ymd != 0 && weatherHmFromIso(riseIso, rh, rm) && weatherHmFromIso(setIso, sh, sm)) {
            weatherSunLocalDate = ymd;
            weatherSunriseHour = rh;
            weatherSunriseMin = rm;
            weatherSunsetHour = sh;
            weatherSunsetMin = sm;
            Serial.printf("[Weather] sunrise=%02d:%02d sunset=%02d:%02d date=%lu\n",
                          (int)weatherSunriseHour,
                          (int)weatherSunriseMin,
                          (int)weatherSunsetHour,
                          (int)weatherSunsetMin,
                          (unsigned long)weatherSunLocalDate);
          }
        }
      }
      if (daily.containsKey("temperature_2m_max") && daily["temperature_2m_max"].is<JsonArray>()) {
        JsonArray a = daily["temperature_2m_max"].as<JsonArray>();
        for (uint8_t i = 0; i < 4 && i < a.size(); ++i) if (!a[i].isNull()) weatherDailyTempMaxC[i] = a[i].as<float>();
      }
      if (daily.containsKey("temperature_2m_min") && daily["temperature_2m_min"].is<JsonArray>()) {
        JsonArray a = daily["temperature_2m_min"].as<JsonArray>();
        for (uint8_t i = 0; i < 4 && i < a.size(); ++i) if (!a[i].isNull()) weatherDailyTempMinC[i] = a[i].as<float>();
      }

      // Если в daily не запрошены temperature_2m_max/min, вычисляем их из hourly.temperature_2m по датам daily.time.
      bool needDerivedTemps = false;
      for (uint8_t i = 0; i < 4; ++i) {
        if (isnan(weatherDailyTempMaxC[i]) || isnan(weatherDailyTempMinC[i])) {
          needDerivedTemps = true;
          break;
        }
      }
      if (needDerivedTemps &&
          daily.containsKey("time") && daily["time"].is<JsonArray>() &&
          doc.containsKey("hourly") && doc["hourly"].is<JsonObject>()) {
        JsonArray dayTime = daily["time"].as<JsonArray>();
        JsonObject hourly = doc["hourly"].as<JsonObject>();
        if (hourly.containsKey("time") && hourly["time"].is<JsonArray>() &&
            hourly.containsKey("temperature_2m") && hourly["temperature_2m"].is<JsonArray>()) {
          JsonArray hTime = hourly["time"].as<JsonArray>();
          JsonArray hTemp = hourly["temperature_2m"].as<JsonArray>();
          size_t hn = hTime.size();
          if (hTemp.size() < hn) hn = hTemp.size();

          for (uint8_t di = 0; di < 4 && di < dayTime.size(); ++di) {
            if (dayTime[di].isNull()) continue;
            String dayIso = dayTime[di].as<String>(); // YYYY-MM-DD
            if (dayIso.length() < 10) continue;
            float minT = NAN, maxT = NAN;
            for (size_t hi = 0; hi < hn; ++hi) {
              if (hTime[hi].isNull() || hTemp[hi].isNull()) continue;
              String hIso = hTime[hi].as<String>(); // YYYY-MM-DDTHH:MM
              if (hIso.length() < 10 || hIso.substring(0, 10) != dayIso) continue;
              float t = hTemp[hi].as<float>();
              if (isnan(minT) || t < minT) minT = t;
              if (isnan(maxT) || t > maxT) maxT = t;
            }
            if (!isnan(minT)) weatherDailyTempMinC[di] = minT;
            if (!isnan(maxT)) weatherDailyTempMaxC[di] = maxT;
          }
        }
      }

      // Дневные показатели (09:00..18:59): средняя скорость ветра и max вероятность осадков.
      if (daily.containsKey("time") && daily["time"].is<JsonArray>() &&
          doc.containsKey("hourly") && doc["hourly"].is<JsonObject>()) {
        JsonArray dayTime = daily["time"].as<JsonArray>();
        JsonObject hourly = doc["hourly"].as<JsonObject>();
        if (hourly.containsKey("time") && hourly["time"].is<JsonArray>() &&
            hourly.containsKey("wind_speed_10m") && hourly["wind_speed_10m"].is<JsonArray>() &&
            hourly.containsKey("precipitation_probability") && hourly["precipitation_probability"].is<JsonArray>()) {
          JsonArray hTime = hourly["time"].as<JsonArray>();
          JsonArray hWind = hourly["wind_speed_10m"].as<JsonArray>();
          JsonArray hPop = hourly["precipitation_probability"].as<JsonArray>();
          size_t hn = hTime.size();
          if (hWind.size() < hn) hn = hWind.size();
          if (hPop.size() < hn) hn = hPop.size();

          for (uint8_t di = 0; di < 4 && di < dayTime.size(); ++di) {
            if (dayTime[di].isNull()) continue;
            String dayIso = dayTime[di].as<String>(); // YYYY-MM-DD
            if (dayIso.length() < 10) continue;
            float windSum = 0.0f;
            uint16_t windCnt = 0;
            float popMax = NAN;
            for (size_t hi = 0; hi < hn; ++hi) {
              if (hTime[hi].isNull()) continue;
              String hIso = hTime[hi].as<String>();
              if (hIso.length() < 13 || hIso.substring(0, 10) != dayIso) continue;
              int hh = hIso.substring(11, 13).toInt();
              if (hh < 9 || hh > 18) continue;

              if (!hWind[hi].isNull()) {
                windSum += hWind[hi].as<float>();
                windCnt++;
              }
              if (!hPop[hi].isNull()) {
                float p = hPop[hi].as<float>();
                if (isnan(popMax) || p > popMax) popMax = p;
              }
            }
            if (windCnt > 0) weatherDailyWindDayMs[di] = windSum / (float)windCnt;
            if (!isnan(popMax)) weatherDailyPrecipDayPct[di] = popMax;
          }
        }
      }
    }

    Serial.printf("[Weather] Open-Meteo T=%.2f -> Outdoor temp: %.0f C, wind_dir=%.1f°, WMO=%ld, PoP=%.0f%%\n",
                  temp,
                  outdoorTemperature,
                  weatherWindDirectionDeg,
                  (long)weatherWmoCode,
                  weatherPrecipProbabilityPct);
    return true;
  } else {
    snprintf(detail, sizeof(detail), "HTTP err=%d", httpCode);
    logToDisplay("Weather HTTP err", detail);
    Serial.printf("[Weather] HTTP error: %d\n", httpCode);
    http.end();
    return false;
  }
}

// Интервал NTP + погоды в активном режиме, updateHours — из админки (1..24 ч).
// lastNetworkNtpOk=false → повтор через ~5 мин (WiFi/NTP/погода не удались в прошлом сеансе).
bool shouldUpdateNetwork(time_t currentTime, uint8_t updateHours, bool weatherEnabled) {
  if (lastNetworkUpdate == 0) {
    return true;
  }
  if (updateHours == 0 || updateHours > 24) {
    updateHours = 1;
  }
  uint32_t updatePeriodSec = (uint32_t)updateHours * 3600UL;
  bool weatherNeverOk = weatherEnabled && (lastSuccessfulWeatherLocalEpoch == 0 || isnan(outdoorTemperature));
  bool needShortRetry = !lastNetworkNtpOk || weatherNeverOk;
  uint32_t period = needShortRetry ? 300UL : updatePeriodSec;
  time_t delta = currentTime - lastNetworkUpdate;
  if (delta < 0) {
    return true;
  }
  return (uint32_t)delta >= period;
}

// Функция для получения изменения температуры
float getTemperatureChange() {
  if (isnan(outdoorTemperature) || isnan(previousOutdoorTemperature)) {
    return NAN;
  }
  return outdoorTemperature - previousOutdoorTemperature;
}

#endif
