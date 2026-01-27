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
bool fetchOutdoorTemperature() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;  // WiFi не подключен
  }

  HTTPClient http;
  String url = "http://api.narodmon.ru/?cmd=sensorsValues&api_key=xcHX1858McCHS&sensors=32277,61922&lang=ru&uuid=00f3694f782462152b5a548b2af0f2c4";
  
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

// Функция для проверки необходимости обновления (каждый час)
bool shouldUpdateWeather(time_t currentTime) {
  if (lastWeatherUpdate == 0) {
    return true;  // Первое обновление
  }
  return (currentTime - lastWeatherUpdate) >= 3600;  // 3600 секунд = 1 час
}

// Функция для получения изменения температуры
float getTemperatureChange() {
  if (isnan(outdoorTemperature) || isnan(previousOutdoorTemperature)) {
    return NAN;
  }
  return outdoorTemperature - previousOutdoorTemperature;
}

#endif
