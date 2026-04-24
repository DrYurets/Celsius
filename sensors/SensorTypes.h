#ifndef CELSIUS_SENSOR_TYPES_H
#define CELSIUS_SENSOR_TYPES_H

#include <Arduino.h>

#define TEMP_SENSOR_SHT31 0
#define TEMP_SENSOR_AHT20_BMP280 1
#define TEMP_SENSOR_AHT21 2
#define TEMP_SENSOR_HTU21 3

inline uint8_t parseTempSensorType(const String &sensorType) {
  if (sensorType == "aht20bmp280") {
    return TEMP_SENSOR_AHT20_BMP280;
  }
  if (sensorType == "aht21") {
    return TEMP_SENSOR_AHT21;
  }
  if (sensorType == "htu21") {
    return TEMP_SENSOR_HTU21;
  }
  return TEMP_SENSOR_SHT31;
}

inline const char *tempSensorTypeToFormValue(uint8_t sensorType) {
  switch (sensorType) {
    case TEMP_SENSOR_AHT20_BMP280: return "aht20bmp280";
    case TEMP_SENSOR_AHT21: return "aht21";
    case TEMP_SENSOR_HTU21: return "htu21";
    case TEMP_SENSOR_SHT31:
    default:
      return "sht31";
  }
}

#endif
