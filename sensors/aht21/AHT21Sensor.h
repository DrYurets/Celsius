#ifndef CELSIUS_AHT21_SENSOR_H
#define CELSIUS_AHT21_SENSOR_H

#include <Adafruit_AHTX0.h>

inline bool initAHT21Sensor(Adafruit_AHTX0 &aht21, TwoWire *wire, uint8_t address) {
  return aht21.begin(wire, address);
}

inline bool readAHT21Sensor(Adafruit_AHTX0 &aht21, float &tempC, float &humPct) {
  sensors_event_t humidityEvent;
  sensors_event_t tempEvent;
  aht21.getEvent(&humidityEvent, &tempEvent);
  if (isnan(tempEvent.temperature) || isnan(humidityEvent.relative_humidity)) {
    return false;
  }
  tempC = tempEvent.temperature;
  humPct = humidityEvent.relative_humidity;
  return true;
}

#endif
