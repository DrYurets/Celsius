#ifndef WEATHER_DETAIL_SCREENS_H
#define WEATHER_DETAIL_SCREENS_H

#include <cmath>
#include <cstdio>
#include "Meteocons.h"

/** Количество страниц подробной погоды (GPIO4). */
constexpr uint8_t kWeatherDetailScreenCount = 5;

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
inline void drawWeatherIcon(DisplayT &display, int16_t x, int16_t y, int16_t size, int32_t wmoCode) {
  const uint8_t *bmp = meteoconByWmo(wmoCode);
  if (size < 1) size = 1;
  // Рисуем bitmap вручную, чтобы избежать искажений от разных форматов drawBitmap в драйверах.
  for (int yy = 0; yy < 16; ++yy) {
    uint8_t b0 = bmp[yy * 2 + 0];
    uint8_t b1 = bmp[yy * 2 + 1];
    for (int xx = 0; xx < 16; ++xx) {
      bool on = (xx < 8) ? (b0 & (0x80 >> xx)) : (b1 & (0x80 >> (xx - 8)));
      if (!on) continue;
      int16_t px = x + (int16_t)((xx * size) / 16);
      int16_t py = y + (int16_t)((yy * size) / 16);
      int16_t nx = x + (int16_t)(((xx + 1) * size) / 16);
      int16_t ny = y + (int16_t)(((yy + 1) * size) / 16);
      int16_t w = nx - px;
      int16_t h = ny - py;
      if (w < 1) w = 1;
      if (h < 1) h = 1;
      display.fillRect(px, py, w, h, 1);
    }
  }
}

/**
 * Одна страница подробной погоды (128×64, альбомная ориентация).
 * screenIndex: 0..4.
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
  display.setCursor(116, 56); // номер экрана
  display.print((int)(screenIndex + 1));
  

  switch (screenIndex) {
    case 0: {
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.print("Наружная температура");

      char tBuf[16];
      formatSignedTemp(tBuf, sizeof(tBuf), outdoorTemp);
      display.setTextSize(2);
      display.setCursor(0, 12);
      display.print(tBuf);
      int16_t xAfterT = (int16_t)strlen(tBuf) * 12;
      drawDegreeMark(display, xAfterT + 1, 14);
      display.setCursor(xAfterT + 7, 12);
      display.print("С");

      display.setTextSize(1);
      display.setCursor(0, 32);
      display.print("Ощущается как");
      char fBuf[16];
      formatSignedTemp(fBuf, sizeof(fBuf), feelsLikeC);
      display.setTextSize(2);
      display.setCursor(0, 44);
      display.print(fBuf);
      int16_t xAfterF = (int16_t)strlen(fBuf) * 12;
      drawDegreeMark(display, xAfterF + 1, 46);
      display.setCursor(xAfterF + 7, 44);
      display.print("С");
      break;
    }
    case 1: {
      display.setCursor(0, 0);
      display.print("Скорость ветра");
      display.setTextSize(2);
      display.setCursor(10, 12);
      if (isnan(windSpeedMs)) {
        display.print("--");
      } else {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f", windSpeedMs);
        display.print(buf);
        display.print("м/с");
      }
      display.setTextSize(1);
      display.setCursor(0, 32);
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
        display.setCursor(0, 44);
        display.print(dirLabel);
        if (!isnan(windDirDeg)) {
          display.print(" ");
          display.print(degBuf);
        }
        if (!isnan(windDirDeg)) {
          int16_t glyphs = (int16_t)utf8GlyphCount(dirLabel) + 1 + (int16_t)strlen(degBuf);
          int16_t xAfter = glyphs * 6;
          drawDegreeMark(display, xAfter + 1, 44);
        }
      }

      if (!isnan(windDirDeg)) {
        const int16_t cx = 102;
        const int16_t cy = 12;
        const float rad = windDirDeg * 0.0174532925f;
        const float vx = -sinf(rad);
        const float vy = cosf(rad);
        const int16_t tipX = cx + (int16_t)lroundf(vx * 24.0f);
        const int16_t tipY = cy + (int16_t)lroundf(vy * 24.0f);
        drawLineBresenham(display, cx, cy, tipX, tipY);
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
      display.setCursor(0, 0);
      display.print("Влажность воздуха");
      display.setTextSize(2);
      display.setCursor(0, 12);
      if (isnan(humidityPct)) {
        display.print("--");
      } else {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", (int)lroundf(humidityPct));
        display.print(buf);
      }
      display.setTextSize(1);
      display.setCursor(28, 18);
      display.print("%");

      display.setCursor(0, 32);
      display.print("Атмосферное давление");
      display.setTextSize(2);
      display.setCursor(0, 44);
      if (isnan(pressureHpa)) {
        display.print("--");
      } else {
        const float mmHg = pressureHpa * 0.750062f;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", (int)lroundf(mmHg));
        display.print(buf);
      }
      display.setTextSize(1);
      display.setCursor(42, 50);
      display.print("мм рт.ст");
      break;
    }
    case 3: {
      display.setCursor(0, 0);
      display.print("Текущая погода");
      drawWeatherIcon(display, 6, 14, 28, currentWmoCode);
      display.setCursor(42, 15);
      display.print("Осадки: ");
      if (isnan(precipProbabilityPct)) {
        display.print("--");
      } else {
        display.print((int)lroundf(precipProbabilityPct));
        display.print("%");
      }

      float nightMin = nearestNightMinC;
      int32_t nightWmo = nearestNightWmoCode;
      display.setCursor(42, 31);
      display.print("Ночью:");
      int16_t nightIconX = 86;
      display.setCursor(42, 48);
      if (isnan(nightMin)) {
        display.print("--");
        nightIconX = 58;
      } else {
        char nBuf[12];
        formatSignedIntTemp(nBuf, sizeof(nBuf), nightMin);
        display.print(nBuf);
        int16_t xAfter = 42 + (int16_t)strlen(nBuf) * 6;
        drawDegreeMark(display, xAfter + 1, 48);
        display.setCursor(xAfter + 7, 48);
        display.print("С");
        nightIconX = xAfter + 16;
      }
      drawWeatherIcon(display, nightIconX, 44, 16, nightWmo);
      break;
    }
    case 4:
    default: {
      const int16_t colX[3] = { 0, 43, 86 };
      for (uint8_t i = 0; i < 3; ++i) {
        int16_t x = colX[i];
        uint8_t srcIdx = (uint8_t)(i + 1); // прогноз именно на 3 дня вперед: завтра, послезавтра, +3
        uint8_t wd = (uint8_t)((baseTmWday + srcIdx) % 7);
        display.setCursor(x + 7, 0);
        display.print(weekdayShortRuByTmWday(wd));

        int32_t code = dailyWmoCode ? dailyWmoCode[srcIdx] : -1;
        drawWeatherIcon(display, x + 4, 7, 22, code);

        display.setCursor(x, 30);
        char dayBuf[8];
        formatSignedIntTemp(dayBuf, sizeof(dayBuf), (dailyTempMaxC ? dailyTempMaxC[srcIdx] : NAN));
        display.print(dayBuf);
        display.print("/");
        char nightBuf[8];
        formatSignedIntTemp(nightBuf, sizeof(nightBuf), (dailyTempMinC ? dailyTempMinC[srcIdx] : NAN));
        display.print(nightBuf);
        // Без символа градуса в 5-м экране: так строка гарантированно помещается в колонку.

        display.setCursor(x + 3, 41);
        if (isnan(dailyWindDayMs ? dailyWindDayMs[srcIdx] : NAN)) {
          display.print("--");
        } else {
          char wbuf[8];
          snprintf(wbuf, sizeof(wbuf), "%.1f", dailyWindDayMs[srcIdx]);
          display.print(wbuf);
        }

        display.setCursor(x + 3, 52);
        if (isnan(dailyPrecipDayPct ? dailyPrecipDayPct[srcIdx] : NAN)) {
          display.print("--%");
        } else {
          display.print((int)lroundf(dailyPrecipDayPct[srcIdx]));
          display.print("%");
        }
      }
      break;
    }
  }

  display.display();
}

#endif
