#ifndef WEATHER_DISPLAY_H
#define WEATHER_DISPLAY_H

#include <Adafruit_SSD1306.h>

// Ветка main: SSD1306 128×32 + setRotation(1) → логически ~32×128 (узкий портрет).
// При display.width() >= 64 — компактные многострочные строки «в ширину»;
// иначе — одна колонка коротких меток (T, FL, H, W, P).

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

  const int w = display.width();
  const char deg = (char)247;

  if (w >= 64) {
    display.setCursor(0, 0);
    display.print("WEATHER ");
    display.print(sourceIsOpenWeather ? "OWM" : "NRD");

    display.setCursor(0, 10);
    display.print("T:");
    if (isnan(outdoorTemp)) {
      display.print("--");
    } else {
      display.print(outdoorTemp, 0);
      display.print(deg);
    }
    display.print(" FL:");
    if (isnan(feelsLikeC)) {
      display.print("--");
    } else {
      display.print(feelsLikeC, 0);
      display.print(deg);
    }

    display.setCursor(0, 22);
    display.print("H:");
    if (isnan(humidityPct)) {
      display.print("--");
    } else {
      display.print((int)humidityPct);
    }
    display.print("% W:");
    if (isnan(windSpeedMs)) {
      display.print("--");
    } else {
      display.print(windSpeedMs, 1);
    }

    display.setCursor(0, 34);
    display.print("P:");
    if (isnan(pressureHpa)) {
      display.print("--");
    } else {
      int mmHg = (int)(pressureHpa * 0.750062f + 0.5f);
      display.print(mmHg);
      display.print("mm");
    }
  } else {
    const int dy = 10;
    int y = 0;
    display.setCursor(0, y);
    display.print(sourceIsOpenWeather ? "OWM" : "NRD");
    y += dy;

    display.setCursor(0, y);
    display.print("T:");
    if (isnan(outdoorTemp)) {
      display.print("--");
    } else {
      display.print(outdoorTemp, 0);
      display.print(deg);
    }
    y += dy;

    display.setCursor(0, y);
    display.print("FL:");
    if (isnan(feelsLikeC)) {
      display.print("--");
    } else {
      display.print(feelsLikeC, 0);
      display.print(deg);
    }
    y += dy;

    display.setCursor(0, y);
    display.print("H:");
    if (isnan(humidityPct)) {
      display.print("--");
    } else {
      display.print((int)humidityPct);
    }
    display.print("%");
    y += dy;

    display.setCursor(0, y);
    display.print("W:");
    if (isnan(windSpeedMs)) {
      display.print("--");
    } else {
      display.print(windSpeedMs, 1);
    }
    y += dy;

    display.setCursor(0, y);
    display.print("P:");
    if (isnan(pressureHpa)) {
      display.print("--");
    } else {
      int mmHg = (int)(pressureHpa * 0.750062f + 0.5f);
      display.print(mmHg);
    }
  }

  display.display();
}

#endif
