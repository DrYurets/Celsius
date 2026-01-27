#ifndef WEATHER_API_H
#define WEATHER_API_H

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// https://narodmon.com/appdoc - документация по API narodmon.ru

// Переменные для хранения температуры
RTC_DATA_ATTR float outdoorTemperature = NAN;
RTC_DATA_ATTR float previousOutdoorTemperature = NAN;
RTC_DATA_ATTR time_t lastWeatherUpdate = 0;

// Функция для получения температуры с API narodmon.ru
bool fetchOutdoorTemperature(const char* apiUrl) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;  // WiFi не подключен
  }

  HTTPClient http;
  String url = String(apiUrl);
  
  http.begin(url);
  http.setTimeout(5000);  // Таймаут 5 секунд
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    http.end();
    
    // Парсинг JSON
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error && doc.containsKey("sensors") && doc["sensors"].is<JsonArray>()) {
      JsonArray sensors = doc["sensors"].as<JsonArray>();
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
            }
          }
        }
        
        // Вычисляем среднее и округляем до целого
        if (count > 0) {
          float avgTemp = sum / count;
          float roundedTemp = round(avgTemp);
          
          // Сохраняем предыдущее значение
          previousOutdoorTemperature = outdoorTemperature;
          // Обновляем текущее значение (округленное)
          outdoorTemperature = roundedTemp;
          lastWeatherUpdate = time(nullptr);
          return true;
        }
      }
    }
  }
  
  http.end();
  return false;
}

// Функция для проверки необходимости обновления
bool shouldUpdateWeather(time_t currentTime, uint8_t updateHours) {
  if (lastWeatherUpdate == 0) {
    return true;  // Первое обновление
  }
  uint32_t updatePeriodSec = (uint32_t)updateHours * 3600UL;
  return (currentTime - lastWeatherUpdate) >= updatePeriodSec;
}

// Функция для конвертации температуры в Фаренгейт
float convertToFahrenheit(float celsius) {
  if (isnan(celsius)) return NAN;
  return (celsius * 9.0 / 5.0) + 32.0;
}

// Функция для получения изменения температуры
float getTemperatureChange() {
  if (isnan(outdoorTemperature) || isnan(previousOutdoorTemperature)) {
    return NAN;
  }
  return outdoorTemperature - previousOutdoorTemperature;
}

#endif
