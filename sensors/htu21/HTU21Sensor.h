#ifndef CELSIUS_HTU21_SENSOR_H
#define CELSIUS_HTU21_SENSOR_H

#include <Adafruit_HTU21DF.h>

inline bool initHTU21Sensor(Adafruit_HTU21DF &htu21) {
  return htu21.begin();
}

inline bool readHTU21Sensor(Adafruit_HTU21DF &htu21, float &tempC, float &humPct) {
  float t = htu21.readTemperature();
  float h = htu21.readHumidity();
  if (isnan(t) || isnan(h)) {
    return false;
  }
  tempC = t;
  humPct = h;
  return true;
}

#endif
