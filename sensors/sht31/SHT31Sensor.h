#ifndef CELSIUS_SHT31_SENSOR_H
#define CELSIUS_SHT31_SENSOR_H

#include <Adafruit_SHT31.h>

inline bool initSHT31Sensor(Adafruit_SHT31 &sht31, uint8_t address) {
  return sht31.begin(address);
}

inline bool readSHT31Sensor(Adafruit_SHT31 &sht31, float &tempC, float &humPct) {
  float t = sht31.readTemperature();
  float h = sht31.readHumidity();
  if (isnan(t) || isnan(h)) {
    return false;
  }
  tempC = t;
  humPct = h;
  return true;
}

#endif
