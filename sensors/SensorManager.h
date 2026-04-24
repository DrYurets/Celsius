#ifndef CELSIUS_SENSOR_MANAGER_H
#define CELSIUS_SENSOR_MANAGER_H

#include <Adafruit_SHT31.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_HTU21DF.h>

#include "SensorTypes.h"
#include "sht31/SHT31Sensor.h"
#include "aht20bmp280/AHT20BMP280Sensor.h"
#include "aht21/AHT21Sensor.h"
#include "htu21/HTU21Sensor.h"

struct IndoorSensorInitResult {
  bool sensorOk;
  bool bmpOk;
};

inline const char *indoorSensorName(uint8_t sensorType) {
  switch (sensorType) {
    case TEMP_SENSOR_SHT31: return "SHT31";
    case TEMP_SENSOR_AHT20_BMP280: return "AHT20+BMP280";
    case TEMP_SENSOR_AHT21: return "AHT21";
    case TEMP_SENSOR_HTU21: return "HTU21";
    default: return "UNKNOWN";
  }
}

inline IndoorSensorInitResult initSelectedIndoorSensor(
  uint8_t sensorType,
  Adafruit_SHT31 &sht31,
  Adafruit_AHTX0 &aht20,
  Adafruit_BMP280 &bmp280,
  Adafruit_HTU21DF &htu21,
  TwoWire *wire,
  uint8_t sht31Addr,
  uint8_t ahtAddr,
  uint8_t bmpPrimaryAddr,
  uint8_t bmpSecondaryAddr) {
  IndoorSensorInitResult result{false, false};
  switch (sensorType) {
    case TEMP_SENSOR_SHT31:
      result.sensorOk = initSHT31Sensor(sht31, sht31Addr);
      break;
    case TEMP_SENSOR_AHT20_BMP280:
      result.sensorOk = initAHT20Sensor(aht20, wire, ahtAddr);
      result.bmpOk = initBMP280Optional(bmp280, bmpPrimaryAddr, bmpSecondaryAddr);
      break;
    case TEMP_SENSOR_AHT21:
      result.sensorOk = initAHT21Sensor(aht20, wire, ahtAddr);
      break;
    case TEMP_SENSOR_HTU21:
      result.sensorOk = initHTU21Sensor(htu21);
      break;
    default:
      result.sensorOk = initSHT31Sensor(sht31, sht31Addr);
      break;
  }
  return result;
}

inline bool readSelectedIndoorSensor(
  uint8_t sensorType,
  Adafruit_SHT31 &sht31,
  Adafruit_AHTX0 &aht20,
  Adafruit_HTU21DF &htu21,
  float &tempC,
  float &humPct) {
  switch (sensorType) {
    case TEMP_SENSOR_SHT31: return readSHT31Sensor(sht31, tempC, humPct);
    case TEMP_SENSOR_AHT20_BMP280: return readAHT20Sensor(aht20, tempC, humPct);
    case TEMP_SENSOR_AHT21: return readAHT21Sensor(aht20, tempC, humPct);
    case TEMP_SENSOR_HTU21: return readHTU21Sensor(htu21, tempC, humPct);
    default: return false;
  }
}

#endif
