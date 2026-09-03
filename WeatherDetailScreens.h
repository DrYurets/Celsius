#ifndef WEATHER_DETAIL_SCREENS_H
#define WEATHER_DETAIL_SCREENS_H

#include <cmath>
#include <cstdio>
#include <time.h>
#include "Meteocons.h"

/** Количество страниц подробной погоды (GPIO4). */
constexpr uint8_t kWeatherDetailScreenCount = 7;
/** Погода + экран статуса (версия / NTP / погода). */
constexpr uint8_t kDetailScreenCount = kWeatherDetailScreenCount + 1;

/** 16-румбовая роза; deg — метеорологические градусы (0 = север, по часовой). */
inline const char *windDirectionLabel16Ru(float deg) {
  if (isnan(deg)) {
    return "--";
  }
  const float d = fmodf(deg + 360.0f, 360.0f);
  // Ближайший из 16 секторов по 22.5°; % 16 в C++ для отрицательных step даёт отрицательный остаток → безопасное приведение.
  long step = lroundf(d / 22.5f);
  int idx = (int)(step % 16);
  if (idx < 0) {
    idx += 16;
  }
  static const char *const L[] = {
    "С", "ССВ", "СВ", "ВСВ", "В", "ВЮВ", "ЮВ", "ЮЮВ",
    "Ю", "ЮЮЗ", "ЮЗ", "ЗЮЗ", "З", "ЗСЗ", "СЗ", "ССЗ"
  };
  return L[idx];
}

template <typename DisplayT>
inline void drawDegreeMark(DisplayT &display, int16_t x, int16_t y) {
  display.fillRect(x + 1, y, 1, 1, 1);
  display.fillRect(x, y + 1, 1, 1, 1);
  display.fillRect(x + 2, y + 1, 1, 1, 1);
  display.fillRect(x + 1, y + 2, 1, 1, 1);
}

template <typename DisplayT>
inline void drawPixel(DisplayT &display, int16_t x, int16_t y) {
  display.fillRect(x, y, 1, 1, 1);
}

template <typename DisplayT>
inline void drawLineBresenham(DisplayT &display, int x0, int y0, int x1, int y1) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (true) {
    drawPixel(display, x0, y0);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

inline void formatSignedTemp(char *out, size_t outSize, float t) {
  if (isnan(t)) {
    snprintf(out, outSize, "--");
    return;
  }
  char sign = (t > 0.0f) ? '+' : '\0';
  float absT = fabsf(t);
  int whole = (int)absT;
  int frac = (int)lroundf((absT - whole) * 10.0f);
  if (frac >= 10) {
    whole += 1;
    frac = 0;
  }
  if (sign) snprintf(out, outSize, "%c%d,%d", sign, whole, frac);
  else snprintf(out, outSize, "%d,%d", (t < 0.0f ? -whole : whole), frac);
}

inline void formatSignedIntTemp(char *out, size_t outSize, float t) {
  if (isnan(t)) {
    snprintf(out, outSize, "--");
    return;
  }
  int v = (int)lroundf(t);
  if (v > 0) snprintf(out, outSize, "+%d", v);
  else snprintf(out, outSize, "%d", v);
}

inline uint8_t utf8GlyphCount(const char *s) {
  uint8_t n = 0;
  const uint8_t *p = (const uint8_t *)s;
  while (*p) {
    // считаем только стартовые байты UTF-8 символов
    if ((*p & 0xC0) != 0x80) n++;
    p++;
  }
  return n;
}

inline const char *weekdayShortRuByTmWday(uint8_t wday) {
  static const char *const kRu[7] = { "ВС", "ПН", "ВТ", "СР", "ЧТ", "ПТ", "СБ" };
  return kRu[wday % 7];
}

template <typename DisplayT>
inline void drawWeatherIcon(DisplayT &display,
                            int16_t x,
                            int16_t y,
                            int16_t pixelScale,
                            int32_t wmoCode,
                            float windSpeedMs = NAN,
                            bool night = false) {
  MeteoconGlyph glyph = meteoconByWmoAndWind(wmoCode, windSpeedMs, night);
  if (pixelScale < 1) pixelScale = 1;
  const uint8_t rowBytes = (uint8_t)((glyph.width + 7) / 8);
  // Рисуем bitmap вручную с целочисленным масштабом,
  // чтобы сохранить "пиксельную" чёткость без дробных искажений.
  for (int yy = 0; yy < glyph.height; ++yy) {
    const uint8_t *row = glyph.rows + (yy * rowBytes);
    for (int xx = 0; xx < glyph.width; ++xx) {
      uint8_t b = row[xx / 8];
      bool on = (b & (0x80 >> (xx % 8))) != 0;
      if (!on) continue;
      int16_t px = x + (int16_t)(xx * pixelScale);
      int16_t py = y + (int16_t)(yy * pixelScale);
      display.fillRect(px, py, pixelScale, pixelScale, 1);
    }
  }
}

/**
 * Одна страница подробной погоды (128×128, SH1107).
 * screenIndex: 0..6.
 */
template <typename DisplayT>
inline void drawWeatherDetailScreen(DisplayT &display,
                                    uint8_t screenIndex,
                                    float outdoorTemp,
                                    float feelsLikeC,
                                    float pressureHpa,
                                    float humidityPct,
                                    float windSpeedMs,
                                    float windDirDeg,
                                    int32_t currentWmoCode,
                                    float precipProbabilityPct,
                                    const int32_t *dailyWmoCode,
                                    const float *dailyTempMaxC,
                                    const float *dailyTempMinC,
                                    const float *dailyWindDayMs,
                                    const float *dailyPrecipDayPct,
                                    uint8_t baseTmWday,
                                    float nearestNightMinC,
                                    int32_t nearestNightWmoCode) {
  display.clearDisplay();
  display.setTextColor(1);

  display.setTextSize(1);
  display.setCursor(116, 116);  // номер экрана
  display.print((int)(screenIndex + 1));

  switch (screenIndex) {
    case 0: {
      display.setTextSize(1);
      display.setCursor(0, 4);
      display.print("Наружная температура");

      char tBuf[16];
      formatSignedTemp(tBuf, sizeof(tBuf), outdoorTemp);
      display.setTextSize(2);
      display.setCursor(0, 24);
      display.print(tBuf);
      int16_t xAfterT = (int16_t)strlen(tBuf) * 10;
      drawDegreeMark(display, xAfterT + 1, 26);
      display.setCursor(xAfterT + 7, 24);
      display.print("С");

      display.setTextSize(1);
      display.setCursor(0, 64);
      display.print("Ощущается как");
      char fBuf[16];
      formatSignedTemp(fBuf, sizeof(fBuf), feelsLikeC);
      display.setTextSize(2);
      display.setCursor(0, 84);
      display.print(fBuf);
      int16_t xAfterF = (int16_t)strlen(fBuf) * 10;
      drawDegreeMark(display, xAfterF + 1, 86);
      display.setCursor(xAfterF + 7, 84);
      display.print("С");
      break;
    }
    case 1: {
      display.setCursor(0, 4);
      display.print("Скорость ветра");
      display.setTextSize(2);
      display.setCursor(10, 24);
      if (isnan(windSpeedMs)) {
        display.print("--");
      } else {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f", windSpeedMs);
        display.print(buf);
        display.print("м/с");
      }
      display.setTextSize(1);
      display.setCursor(0, 64);
      display.print("Направление ветра");
      {
        char dirLabel[16];
        char degBuf[8];
        if (!isnan(windDirDeg)) {
          int ideg = (int)lroundf(fmodf(windDirDeg + 360.0f, 360.0f));
          snprintf(dirLabel, sizeof(dirLabel), "%s", windDirectionLabel16Ru(windDirDeg));
          snprintf(degBuf, sizeof(degBuf), "%d", ideg);
        } else {
          snprintf(dirLabel, sizeof(dirLabel), "--");
          degBuf[0] = '\0';
        }
        constexpr int16_t kWindDirTextY = 84;
        display.setTextSize(2);
        display.setCursor(0, kWindDirTextY);
        display.print(dirLabel);
        if (!isnan(windDirDeg)) {
          display.print(" ");
          display.print(degBuf);
        }
        if (!isnan(windDirDeg)) {
          const int16_t chW = 10;
          int16_t xAfter = (int16_t)utf8GlyphCount(dirLabel) * chW + chW + (int16_t)strlen(degBuf) * chW;
          drawDegreeMark(display, xAfter + 1, kWindDirTextY + 2);
        }
      }

      if (!isnan(windDirDeg)) {
        const int16_t cx = 106;
        const int16_t cy = 28;
        const float rad = windDirDeg * 0.0174532925f;
        const float vx = -sinf(rad);
        const float vy = cosf(rad);
        const float halfLen = 14.0f;
        const int16_t tailX = cx - (int16_t)lroundf(vx * halfLen);
        const int16_t tailY = cy - (int16_t)lroundf(vy * halfLen);
        const int16_t tipX = cx + (int16_t)lroundf(vx * halfLen);
        const int16_t tipY = cy + (int16_t)lroundf(vy * halfLen);
        drawLineBresenham(display, tailX, tailY, tipX, tipY);
        const float bx = -vx;
        const float by = -vy;
        const float p1x = bx * 6.0f + vy * 4.0f;
        const float p1y = by * 6.0f - vx * 4.0f;
        const float p2x = bx * 6.0f - vy * 4.0f;
        const float p2y = by * 6.0f + vx * 4.0f;
        drawLineBresenham(display, tipX, tipY, tipX + (int16_t)lroundf(p1x), tipY + (int16_t)lroundf(p1y));
        drawLineBresenham(display, tipX, tipY, tipX + (int16_t)lroundf(p2x), tipY + (int16_t)lroundf(p2y));
      }
      break;
    }
    case 2:
    {
      display.setCursor(0, 4);
      display.print("Влажность воздуха");
      display.setTextSize(2);
      display.setCursor(0, 24);
      if (isnan(humidityPct)) {
        display.print("--");
      } else {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", (int)lroundf(humidityPct));
        display.print(buf);
      }
      display.setTextSize(1);
      display.setCursor(32, 30);
      display.print("%");

      display.setCursor(0, 64);
      display.print("Атмосферное давление");
      display.setTextSize(2);
      display.setCursor(0, 84);
      if (isnan(pressureHpa)) {
        display.print("--");
      } else {
        const float mmHg = pressureHpa * 0.750062f;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", (int)lroundf(mmHg));
        display.print(buf);
      }
      display.setTextSize(1);
      display.setCursor(48, 90);
      display.print("мм рт.ст");
      break;
    }
    case 3: {
      display.setCursor(0, 4);
      display.print("Текущая погода");
      drawWeatherIcon(display, 7, 22, 1, currentWmoCode, windSpeedMs, false);
      display.setCursor(42, 28);
      display.print("Осадки: ");
      if (isnan(precipProbabilityPct)) {
        display.print("--");
      } else {
        display.print((int)lroundf(precipProbabilityPct));
        display.print("%");
      }

      float nightMin = nearestNightMinC;
      int32_t nightWmo = nearestNightWmoCode;
      display.setCursor(42, 56);
      display.print("Ночью:");
      int16_t nightIconX = 86;
      display.setCursor(42, 84);
      if (isnan(nightMin)) {
        display.print("--");
        nightIconX = 58;
      } else {
        char nBuf[12];
        formatSignedIntTemp(nBuf, sizeof(nBuf), nightMin);
        display.print(nBuf);
        int16_t xAfter = 42 + (int16_t)strlen(nBuf) * 6;
        drawDegreeMark(display, xAfter + 1, 84);
        display.setCursor(xAfter + 7, 84);
        display.print("С");
        nightIconX = xAfter + 16;
      }
      drawWeatherIcon(display, nightIconX, 76, 1, nightWmo, NAN, true);
      break;
    }
    case 4:
    case 5:
    default: {
      uint8_t srcIdx = (uint8_t)(screenIndex - 3);
      uint8_t wd = (uint8_t)((baseTmWday + srcIdx) % 7);

      display.setCursor(0, 4);
      display.print("Прогноз: ");
      display.print(weekdayShortRuByTmWday(wd));

      int32_t code = dailyWmoCode ? dailyWmoCode[srcIdx] : -1;
      float dayWind = dailyWindDayMs ? dailyWindDayMs[srcIdx] : NAN;
      float dayPrecip = dailyPrecipDayPct ? dailyPrecipDayPct[srcIdx] : NAN;
      drawWeatherIcon(display, 5, 28, 1, code, dayWind, false);

      char dayBuf[8];
      char nightBuf[8];
      formatSignedIntTemp(dayBuf, sizeof(dayBuf), (dailyTempMaxC ? dailyTempMaxC[srcIdx] : NAN));
      formatSignedIntTemp(nightBuf, sizeof(nightBuf), (dailyTempMinC ? dailyTempMinC[srcIdx] : NAN));

      display.setCursor(40, 32);
      display.print("День/ночь");
      display.setCursor(40, 52);
      display.print(dayBuf);
      display.print("/");
      display.print(nightBuf);

      display.setCursor(40, 76);
      display.print("Ветер: ");
      if (isnan(dayWind)) display.print("--");
      else {
        char wbuf[8];
        snprintf(wbuf, sizeof(wbuf), "%.1f", dayWind);
        display.print(wbuf);
      }
      display.print("м/с");

      display.setCursor(40, 96);
      display.print("Осадки: ");
      if (isnan(dayPrecip)) display.print("--%");
      else {
        display.print((int)lroundf(dayPrecip));
        display.print("%");
      }
      break;
    }
  }

  display.display();
}

/** Экран статуса после страниц погоды: версия прошивки, последняя успешная синхронизация NTP и погоды. */
template <typename DisplayT>
inline void drawSyncStatusScreen(DisplayT &display,
                                 const char *romVersion,
                                 time_t lastNtpLocalEpoch,
                                 time_t lastWeatherLocalEpoch,
                                 uint8_t screenNumber) {
  display.clearDisplay();
  display.setTextColor(1);
  display.setTextSize(1);

  display.setCursor(116, 116);
  display.print((int)screenNumber);

  display.setCursor(0, 4);
  display.print("Статус");

  display.setCursor(0, 24);
  display.print("Версия:");
  display.setCursor(0, 40);
  display.print(romVersion ? romVersion : "--");

  auto formatLocalStamp = [](time_t epoch, char *dateBuf, size_t dateSize, char *timeBuf, size_t timeSize) {
    if (epoch <= 0) {
      snprintf(dateBuf, dateSize, "--.--.----");
      snprintf(timeBuf, timeSize, "--:--");
      return;
    }
    struct tm ti = {};
    localtime_r(&epoch, &ti);
    snprintf(dateBuf, dateSize, "%02d.%02d.%04d", ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
    snprintf(timeBuf, timeSize, "%02d:%02d", ti.tm_hour, ti.tm_min);
  };

  char dateBuf[12];
  char timeBuf[8];

  display.setCursor(0, 60);
  display.print("NTP:");
  formatLocalStamp(lastNtpLocalEpoch, dateBuf, sizeof(dateBuf), timeBuf, sizeof(timeBuf));
  display.setCursor(0, 74);
  display.print(dateBuf);
  display.print(" ");
  display.print(timeBuf);

  display.setCursor(0, 94);
  display.print("Погода:");
  formatLocalStamp(lastWeatherLocalEpoch, dateBuf, sizeof(dateBuf), timeBuf, sizeof(timeBuf));
  display.setCursor(0, 108);
  display.print(dateBuf);
  display.print(" ");
  display.print(timeBuf);

  // Восход/закат из RTC (если уже есть на сегодня) — в правой колонке статуса.
  if (weatherSunriseHour >= 0 && weatherSunsetHour >= 0) {
    char sunBuf[12];
    snprintf(sunBuf, sizeof(sunBuf), "%02d:%02d", (int)weatherSunriseHour, (int)weatherSunriseMin);
    display.setCursor(72, 60);
    display.print("Восх:");
    display.setCursor(72, 74);
    display.print(sunBuf);
    snprintf(sunBuf, sizeof(sunBuf), "%02d:%02d", (int)weatherSunsetHour, (int)weatherSunsetMin);
    display.setCursor(72, 94);
    display.print("Зак:");
    display.setCursor(72, 108);
    display.print(sunBuf);
  }

  display.display();
}

#endif
