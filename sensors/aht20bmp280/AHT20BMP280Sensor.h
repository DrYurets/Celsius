#ifndef CELSIUS_AHT20_BMP280_SENSOR_H
#define CELSIUS_AHT20_BMP280_SENSOR_H

#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>

inline bool initAHT20Sensor(Adafruit_AHTX0 &aht20, TwoWire *wire, uint8_t address) {
  return aht20.begin(wire, address);
}

inline bool initBMP280Optional(Adafruit_BMP280 &bmp280, uint8_t primaryAddr, uint8_t secondaryAddr) {
  if (bmp280.begin(primaryAddr)) {
    return true;
  }
  return bmp280.begin(secondaryAddr);
}

inline bool readAHT20Sensor(Adafruit_AHTX0 &aht20, float &tempC, float &humPct) {
  sensors_event_t humidityEvent;
  sensors_event_t tempEvent;
  aht20.getEvent(&humidityEvent, &tempEvent);
  if (isnan(tempEvent.temperature) || isnan(humidityEvent.relative_humidity)) {
    return false;
  }
  tempC = tempEvent.temperature;
  humPct = humidityEvent.relative_humidity;
  return true;
}

#endif
