#ifndef WEATHER_API_H
#define WEATHER_API_H

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// https://narodmon.com/appdoc - документация по API narodmon.ru

// Объявление функции логирования (определена в основном файле)
void logToDisplay(const char *code, const char *detail = nullptr, uint16_t holdMs = 1000);

// Переменные для хранения температуры
RTC_DATA_ATTR float outdoorTemperature = NAN;
RTC_DATA_ATTR float previousOutdoorTemperature = NAN;
RTC_DATA_ATTR time_t lastWeatherUpdate = 0;

// Функция для получения температуры с API narodmon.ru
bool fetchOutdoorTemperature(const char* apiUrl) {
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
    
    // Парсинг JSON
    DynamicJsonDocument doc(512);
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

    if (!doc.containsKey("sensors")) {
      logToDisplay("Weather no data", "No 'sensors' key");
      Serial.println("[Weather] Error: No 'sensors' key in JSON");
      return false;
    }

    if (!doc["sensors"].is<JsonArray>()) {
      logToDisplay("Weather no data", "Sensors not array");
      Serial.println("[Weather] Error: 'sensors' is not an array");
      return false;
    }
    
    JsonArray sensors = doc["sensors"].as<JsonArray>();
    snprintf(detail, sizeof(detail), "Sensors=%d", sensors.size());
    logToDisplay("Weather HTTP code", detail);
    Serial.printf("[Weather] Sensors in array: %d\n", sensors.size());

    if (sensors.size() > 0) {
      float sum = 0.0;
      int count = 0;

      // Собираем значения со всех датчиков
      for (JsonObject sensor : sensors) {
        if (sensor.containsKey("value")) {
          float value = sensor["value"].as<float>();
          if (!isnan(value)) {
            sum += value;
            count++;
            Serial.printf("[Weather] Sensor value: %.2f\n", value);
          }
        }
      }

      snprintf(detail, sizeof(detail), "Values=%d", count);
      logToDisplay("Weather HTTP code", detail);
      Serial.printf("[Weather] Valid values count: %d\n", count);

      // Вычисляем среднее и округляем до целого
      if (count > 0) {
        float avgTemp = sum / count;
        float roundedTemp = round(avgTemp);

        // Сохраняем предыдущее значение
        previousOutdoorTemperature = outdoorTemperature;
        // Обновляем текущее значение (округленное)
        outdoorTemperature = roundedTemp;
        lastWeatherUpdate = time(nullptr);

        Serial.printf("[Weather] Avg=%.2f -> Outdoor temp: %.0f C\n", avgTemp, roundedTemp);

        return true;
      } else {
        logToDisplay("Weather no data", "No valid values");
        Serial.println("[Weather] Error: No valid temperature values");
        return false;
      }
    } else {
      logToDisplay("Weather no data", "Sensors array empty");
      Serial.println("[Weather] Error: Sensors array is empty");
      return false;
    }
  } else {
    snprintf(detail, sizeof(detail), "HTTP err=%d", httpCode);
    logToDisplay("Weather HTTP err", detail);
    Serial.printf("[Weather] HTTP error: %d\n", httpCode);
    http.end();
    return false;
  }
}

// Функция для проверки необходимости обновления
bool shouldUpdateWeather(time_t currentTime, uint8_t updateHours) {
  if (lastWeatherUpdate == 0) {
    return true;  // Первое обновление
  }
  // Минимум 1 час, иначе при updateHours==0 (например из старого EEPROM) обновлялось бы каждый цикл (каждую минуту)
  if (updateHours == 0 || updateHours > 24) {
    updateHours = 1;
  }
  uint32_t updatePeriodSec = (uint32_t)updateHours * 3600UL;
  // Если последнее обновление было успешным (температура валидна), используем полный период (часы)
  // Если последнее обновление было неудачным (температура NaN), используем меньший период (5 минут) для повторных попыток
  uint32_t period = isnan(outdoorTemperature) ? 300UL : updatePeriodSec;  // 300 секунд = 5 минут для повторных попыток
  return (currentTime - lastWeatherUpdate) >= period;
}

// Функция для получения изменения температуры
float getTemperatureChange() {
  if (isnan(outdoorTemperature) || isnan(previousOutdoorTemperature)) {
    return NAN;
  }
  return outdoorTemperature - previousOutdoorTemperature;
}

#endif
