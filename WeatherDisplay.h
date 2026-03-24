#ifndef WEATHER_DISPLAY_H
#define WEATHER_DISPLAY_H

#include <Adafruit_SSD1306.h>

inline void drawWeatherInfoScreen(Adafruit_SSD1306 &display,
                                  float outdoorTemp,
                                  float feelsLikeC,
                                  float pressureHpa,
                                  float humidityPct,
                                  float windSpeedMs,
                                  bool sourceIsOpenWeather) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Layout под 128x64 (альбомная ориентация).
  // Две колонки, чтобы уместить все параметры без выхода за 64 px по высоте.
  display.setCursor(0, 0);
  display.print("WEATHER");
  display.setCursor(74, 0);
  display.print("SRC:");
  display.print(sourceIsOpenWeather ? "OWM" : "NRD");

  // Левая колонка
  display.setCursor(0, 16);
  display.print("T: ");
  if (isnan(outdoorTemp)) {
    display.print("--");
  } else {
    display.print(outdoorTemp, 1);
    display.print((char)247);
    display.print("C");
  }

  display.setCursor(0, 30);
  display.print("FL: ");
  if (isnan(feelsLikeC)) {
    display.print("--");
  } else {
    display.print(feelsLikeC, 1);
    display.print((char)247);
    display.print("C");
  }

  display.setCursor(0, 44);
  display.print("H: ");
  if (isnan(humidityPct)) {
    display.print("--");
  } else {
    display.print((int)humidityPct);
    display.print("%");
  }

  // Правая колонка
  display.setCursor(64, 16);
  display.print("W: ");
  if (isnan(windSpeedMs)) {
    display.print("--");
  } else {
    display.print(windSpeedMs, 1);
    display.print(" m/s");
  }

  display.setCursor(64, 30);
  display.print("P: ");
  if (isnan(pressureHpa)) {
    display.print("--");
  } else {
    float pressureMmHg = pressureHpa * 0.750062f;
    display.print((int)roundf(pressureMmHg));
    display.print(" mm");
  }

  display.display();
}

#endif
