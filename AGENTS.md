# IMPORTANT (неукоснительно)
Перед началом выполнения любой задачи АГЕНТ ДОЛЖЕН:
1) Неукоснительно соблюдать требования, описанные в этом файле.
2) Полностью перечитать `AGENTS.md` перед каждым началом выполнения задачи, так как он может постоянно дополняться/модифицироваться.
3) **Версия прошивки `ROM_VERSION`**: не увеличивать номер версии в `Celsius.ino` без явного указания пользователя в задаче. Если изменения скетча по смыслу требуют новой версии (или пользователь просит bump), **перед правкой `ROM_VERSION` уточнить у пользователя**, следует ли поднять версию и как (например, patch / minor / major или точная строка). Вместе с `ROM_VERSION` обычно обновляют `project.json` (`version`, `releaseDate`, `whatsNew`/`notes`) для AutoOTA.

# Celsius Clock (ESP32-C3) — AGENTS knowledge base

## Назначение проекта
Проект `Celsius` — часы на ESP32-C3 с OLED и питанием от аккумулятора. Устройство:
- получает точное время через WiFi/NTP;
- компенсирует дрейф RTC между NTP-синхронизациями и поддерживает ручную коррекцию;
- обновляет экран и затем уходит в deep sleep до следующего нужного момента;
- может (опционально) запрашивать уличную температуру с погодного API по HTTP (включая подробный debug в Serial и на экране);
- поддерживает ручной/веб OTA и проверку обновлений AutoOTA;
- (ветки `128x64` / `128x128`) может переворачивать экран по BMI160 и показывать 7 экранов детальной погоды (+ экран статуса на `128x128`).

## Не ломать инварианты (самое важное)
1. **Deep sleep и расписание пробуждения**: логика `runCycle()` возвращает число секунд до следующего пробуждения, затем вызывается `esp_deep_sleep_start()`. Ночью (`night && workdayEnabled`) — сон до `nightEnd`, не каждую минуту; GPIO4 по-прежнему будит. Любые новые сетевые/тяжёлые действия должны быть встроены так, чтобы не ломать сон и не увеличивать время активной фазы без необходимости.
2. **WiFi**: часы не держат WiFi включенным постоянно. WiFi подключается при необходимости (NTP и/или погода и/или AutoOTA), затем должен отключаться.
3. **Сетевой цикл (NTP + погода)**: выполняется только в активном дневном режиме (`timeValid`, `workdayEnabled`, `!night`) и когда истёк интервал `settings.weatherUpdateHours` (`shouldUpdateNetwork`). В одном WiFi-сеансе: NTP → (опционально) AutoOTA → (если `weatherEnabled`) погода.
4. **EEPROM layout**: настройки хранятся в EEPROM по фиксированным адресам; `EEPROM_SIZE` должен быть достаточным для всей структуры `DeviceSettings`.
5. **Debug codes**: функции логирования на OLED (`logToDisplay`) завязаны на флаги показа дебага. Не нужно безусловно “засорять” OLED — используйте `settings.showDebugCodes` и существующие коды.

## Версия прошивки (`ROM_VERSION`)
- В начале `Celsius.ino` задана строковая константа **`#define ROM_VERSION "..."`** — человекочитаемый идентификатор сборки (префикс железа + семантика, напр. `A1.4.10`).
- Значение выводится на OLED в **режиме настройки** (SoftAP): строка вида `v: <ROM_VERSION>` в `updateConfigModeDisplay()`.
- Для AutoOTA версия также задаётся в `project.json` (`version`); после OTA/сборки бинарник обычно лежит в `build/esp32.esp32.esp32c3/Celsius.ino.bin`.
- Агент **не меняет** `ROM_VERSION` про себя; см. пункт 3 в блоке **IMPORTANT** выше.

## Важные файлы
1. `Celsius.ino`
   - основная логика: WiFi/NTP, epoch/дрейф, погода в цикле, OLED, EEPROM, deep sleep; кнопки погоды (GPIO4) и OTA-info (GPIO0 / LED); `ROM_VERSION`; `OledDisplayCompat` / `drawClock`.
2. `WeatherAPI.h`
   - HTTP GET к Open-Meteo (`current+hourly+daily`), парсинг JSON, RTC-данные погоды; `shouldUpdateNetwork` / `lastNetworkUpdate`; кеш `weatherHourly*` для главного экрана.
3. `WeatherDetailScreens.h`
   - отрисовка **7** экранов подробной погоды (GPIO4) + экран статуса; данные только из RTC; `drawWeatherIcon`.
4. `Meteocons.h`
   - глифы погоды по WMO `weather_code` (`meteoconByWmo` / `meteoconByWmoAndWind`).
5. `WebConfigServer.h`
   - web-админка, сохранение настроек, OTA upload, экспорт/импорт JSON.
6. `project.json`
   - манифест AutoOTA (`version`, `whatsNew`/`notes`, URL к `.bin`).

## Поддерживаемые варианты экрана (ветки)
- `main` — дисплей **128×32** (портретная ориентация), SSD1306 / GyverOLED (legacy).
- `128x64` — дисплей **128×64** (SSD1306), GyverOLED, альбомная ориентация.
- `128x128` — дисплей **GME128128-01-IIC ver2.0 / SH1107 128×128**, драйвер **Adafruit_SH110X** (`Adafruit_SH1107`) + **U8g2_for_Adafruit_GFX** (кириллица), обёртка `OledDisplayCompat` в `Celsius.ino`. I2C: SDA=8, SCL=9, адрес `0x3C` (при пустом экране попробовать `0x3D`).

При разработке нового функционала важно учитывать координаты/ориентацию/размеры под конкретную ветку. Сначала выбери правильную ветку.

### Сборка / partition scheme (важно)
Ветки `128x64` / `128x128` с OTA и веб-админкой **не влезают** в схему **Default 4MB with spiffs (1.2MB APP)** (`Maximum is 1310720`). Для ESP32-C3 4MB выбирайте:
- **Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)** — лимит ~1 966 080 байт, OTA сохраняется.
После смены схемы нужна **полная прошивка по USB** (желательно с Erase Flash), не только OTA.

### Checklist прошивки ветки `128x128` (SH1107 / Adafruit_SH110X)
1. В Arduino IDE: **Adafruit SH110X**, **Adafruit GFX**, **Adafruit BusIO**, **U8g2_for_Adafruit_GFX** (и шрифты из неё; полный U8g2 для текста не нужен).
2. Board: ESP32C3; Partition Scheme: **Minimal SPIFFS (1.9MB APP with OTA)**.
3. Драйвер: `Adafruit_SH1107(128, 128, &Wire, -1)`. Если экран пустой/шум — проверить адрес `0x3C` vs `0x3D`.
4. Кириллица: `U8g2_for_Adafruit_GFX` в `OledDisplayCompat` (`6x13`/`10x20` cyrillic, крупные цифры `logisoso*_tn`).
5. Проверить: init, главный экран, RU-подписи погоды, flip BMI160, deep sleep/wake, OTA.

## Архитектура времени
### NTP sync
- `NTPClient` + список `ntpServers[]` / индекс `ntpServerIndex`.
- `ntpSync()` — полноценный WiFi STA + NTP (первичная синхронизация при невалидном времени).
- `ntpSyncOverConnectedWiFi()` — NTP, когда WiFi уже поднят (дневной сетевой цикл).
- offset берётся из `settings.timezoneMinutes` через `getTimezoneOffsetSeconds()`.
- Сохраняются `storedEpoch`, `lastSyncEpoch`, `lastSyncLocalEpoch`; при повторном sync обновляется `driftCorrectionMs`.

### Периодический сетевой цикл (NTP + погода)
- Интервал задаётся **`settings.weatherUpdateHours`** (1..24, по умолчанию 1) — одно поле админки **Time and Network Sync → Update interval (hours)**.
- Проверка: `shouldUpdateNetwork(local, weatherUpdateHours, weatherEnabled)` в `WeatherAPI.h`.
- Условия запуска в `runCycle()`: `timeValid && workdayEnabled && !night && shouldUpdateNetwork(...)`.
- При успехе/попытке пишется `lastNetworkUpdate = local`; при ошибке WiFi/NTP/погоды — `lastNetworkNtpOk = false` (повтор через ~5 мин).
- Поле `syncDays` в `DeviceSettings` остаётся в EEPROM (совместимость layout), но **не задаёт** расписание NTP.

### RTC drift compensation
- Дрейф: `RTC_DATA_ATTR int32_t driftCorrectionMs`, опора — `lastSyncLocalEpoch`.
- `applyDriftCorrection` / `applyTimeCorrection` (сек/сутки из настроек) — **только при чтении** перед отрисовкой/сетью.
- `storedEpoch` при уходе в сон увеличивается на elapsed **без** `timeCorrectionPerDay` (иначе коррекция учитывалась дважды).
- В `runCycle()` перед отрисовкой: `storedEpoch` + поправки.

### Хранение epoch
- `storedEpoch` и связанные поля — `RTC_DATA_ATTR`, чтобы после deep sleep не терять время.

## Web-админка (конфигурация)
### Setup mode
- Нет WiFi-учёток или первичная NTP/WiFi не удалась → SoftAP:
  - SSID `AP_SSID` (CelsiusClock), password `AP_PASSWORD`, порт 80.
- На OLED в AP: строка `v: <ROM_VERSION>`.
- Старт AP: `disconnect` → `WIFI_OFF` → `WIFI_AP`, до 3 попыток `softAP`.

### EEPROM и сохранение настроек
- Структура `DeviceSettings` (фрагмент актуальных полей):
  - флаги отображения (debug, date, weekday, 12/24, hourly blink, **`showSyncProgress`**),
  - night mode, timezone, timeCorrectionPerDay,
  - `syncDays` (legacy layout),
  - погода: `weatherEnabled`, координаты, `weatherApiUrl[768]`, **`weatherUpdateHours`** (NTP+погода), `weatherScreenSeconds`,
  - датчики: `tempSensorType`, `bmi160Enabled`,
  - AutoOTA: `autoOtaEnabled`, `autoOtaCheckHours`,
  - `activeWeekdaysMask`.
- Адреса: SSID `0`, PASS `64`, Settings `128`; `EEPROM_SIZE` 2048 + `static_assert`.
- Дефолты: Open-Meteo, `weatherUpdateHours = 1`, `activeWeekdaysMask = WEEKDAY_MASK_ALL`, `forecast_days=4` в URL, **`showSyncProgress = false`**.

### Экран прогресса синхронизации (`showSyncProgress`)
- Опция в админке **Display Settings → Show sync progress on OLED** (по умолчанию выкл.).
- Если включено: на время сетевого цикла главный экран заменяется на шаги WiFi → NTP → OTA → Погода (`...` / `OK` / `ERR` / `-`).
- После завершения — краткий итог «Готово»/«Ошибка», затем обычный `drawClock` (и при необходимости экраны по кнопке).
- Удлиняет активную фазу только на время сетевого цикла, когда опция включена.

## Погодный модуль (WeatherAPI.h)
### Ожидаемый формат JSON
- Источник: **Open-Meteo** Forecast API (единственный; `weatherSource` — legacy):
  - `current.temperature_2m`, `current.weather_code`, PoP из matching `hourly` часа;
  - `hourly` / `daily` для детальных экранов и главного экрана;
  - URL до 767 символов (`weatherApiUrl[768]`), дефолт `latitude=53.92&longitude=30.35`.
  - `timezone` в URL строится из `settings.timezoneMinutes` (`Etc/GMT±N` с инверсией знака; нецелые часы → `auto`); `rebuildOpenMeteoUrlFromCoordinates()` при load/save.

### RTC-кеш hourly для главного экрана
- При успешном fetch: до **24** слотов от текущего часа API (`weatherHourlyHour/TempC/PrecipPct/WmoCode`, `weatherHourlyValidCount`).
- На экране через `weatherHourlyAheadForClock(clockHour, …)` всегда колонки **`clockHour+1..+3`** (сдвигаются при смене часа на часах без нового HTTP).
- Иконки: WMO `weather_code` → `Meteocons.h` / `drawWeatherIcon` (день/ночь по часу колонки / текущему часу).

### Интервалы обновления
- `lastNetworkUpdate` / `lastNetworkNtpOk` в RTC (та же шкала `local` / `storedEpoch`, не `time(nullptr)`).
- `shouldUpdateNetwork(currentTime, updateHours, weatherEnabled)`:
  - `lastNetworkUpdate == 0` → сразу;
  - `updateHours` вне 1..24 → принудительно 1;
  - ошибка прошлого сеанса (`!lastNetworkNtpOk`) или (weatherEnabled и нет успешной погоды / outdoor NaN) → период 5 минут;
  - иначе `updateHours * 3600`;
  - откат времени (`delta < 0`) → `true`.

### Экран подробной погоды (кнопка)
- **`WEATHER_BUTTON_PIN` (GPIO4)** → GND: wakeup / показ деталей.
- **`OTA_BUTTON_PIN` (GPIO0, тот же что LED / сброс настроек)** → GND:
  - короткое нажатие (если есть обновление) — changelog OTA;
  - на экране OTA удержание ~2 с — установка;
  - **удержание ~5 с после главного экрана** — сброс WiFi (`clearWiFiConfig`) и reboot в SoftAP;
  - при **загрузке** (не deep sleep / не `ESP.restart`): удержание ~2 с — тот же сброс.
  В активной фазе после отрисовки часов: idle-окно **~0.4 с** (или ~2.5 с, если есть OTA); опрос GPIO0 **импульсами** (PULLUP → read → OUTPUT LOW), чтобы LED не светился вполнакала. При удержании — continuous INPUT для UI сброса/OTA. Из deep sleep GPIO0 **не** будит.
- В простое / deep sleep GPIO0 = **OUTPUT LOW** (LED выключен).
- Сброс WiFi по GPIO0 при boot: не после `ESP.restart()` и не после deep sleep (иначе ложный SoftAP из‑за LED на том же пине); при cold/USB/EXT — удержание ~2 с.
- Экран OTA **не** перехватывает GPIO4: погода по-прежнему на кнопке погоды.
- `kWeatherDetailScreenCount = 7`, всего страниц `kDetailScreenCount = 8` (после погоды — статус).
- Экран статуса: `ROM_VERSION`, дата/время последней успешной NTP (`lastSyncLocalEpoch`) и погоды (`lastSuccessfulWeatherLocalEpoch`).
- Повторного HTTP нет — только RTC-кеш.
- Для прогноза на 3 дня вперёд API с `forecast_days=4`; ночной минимум — hourly `21:00..08:59`.

### OTA (web + AutoOTA)
- Web OTA: `/ota` в setup mode; проверки OTA-partition, батареи, пароля AP; заливается application `.bin`.
- AutoOTA: манифест `project.json`; индикатор на главном экране; ручное подтверждение через GPIO0.
- Важно: библиотека AutoOTA считает update любую `version !=` текущей. В прошивке принимаем только **числово более новую** (`isRemoteFirmwareNewer`, напр. A1.4.10 > A1.4.9); иначе иконка/установка сбрасываются.

### Ограничение сетевого цикла
В `runCycle()` WiFi для NTP/погоды/AutoOTA только если:
- `timeValid`, `workdayEnabled`, `!night`,
- `shouldUpdateNetwork(local, settings.weatherUpdateHours, settings.weatherEnabled)`.

Погода внутри сеанса — дополнительно при `settings.weatherEnabled`.

### WiFi в сетевом цикле
- Общий хелпер `connectWifiSta()`: `WIFI_OFF` → STA, `WIFI_POWER_15dBm`, `WiFi.begin(ssid, pass)` **без** фиксации канала (канал `15` в API — не мощность), таймаут **30 с**.
- Подключение STA → NTP → AutoOTA → погода (опционально) → `WiFi.disconnect(true)` + `WIFI_OFF` + low CPU.
- Экран «Статус»: NTP/погода обновляются **только при успехе**. Часы при этом идут от RTC — отсутствие сети на главном экране неочевидно. При фейле WiFi/NTP/погоды — `lastNetworkNtpOk=false`, повтор ~5 мин.

### Debug
- Serial в `WeatherAPI.h` (URL, HTTP, JSON).
- OLED — только при `settings.showDebugCodes` через `logToDisplay`.

## Отображение на OLED (`128x128` / SH1107)
### Рисование (`drawClock`)
- Adafruit_SH1107 + `U8g2_for_Adafruit_GFX` через `OledDisplayCompat` (UTF-8/кириллица; курсор снаружи — top-left).
- Верхняя строка: weekday → дата → indoor T/RH → иконка OTA → батарея (без иконки домика).
- Строка времени: HH:MM на `y≈22`; иконка текущей погоды справа на `y≈16`; под иконкой outdoor T и PoP.
- Ниже: 3 колонки hourly (`час+1..+3`): **час → иконка → T → PoP**.
- Ориентация BMI160 — `setRotation(0/2)`.
- Детальная погода — кадры под 128×128 (`WeatherDetailScreens.h`).

### Батарея
- Иконка «телефонного» типа у правого верхнего края.

## Последовательность работы на устройстве
### setup()
1. Serial, GPIO0 reset check, I2C/OLED.
2. Indoor sensors по `tempSensorType`; опционально BMI160.
3. `loadSettings()` (EEPROM).
4. Нет WiFi → SoftAP config mode.
5. Невалидное время → первичная `ntpSync()` (при фейле — setup mode).
6. `runCycle()` → deep sleep (таймер + GPIO4 wake).

### loop()
- Config mode: `server.handleClient()`.
- Иначе unused (работа через wake → setup).

## Практика разработки (рекомендации агенту)
1. Перед изменением: перечитай `AGENTS.md`, выбери ветку/экран.
2. **`ROM_VERSION` / `project.json`**: не bump самостоятельно; при запросе пользователя — согласовать точную строку/тип bump (или следовать явному «апни»).
3. Сетевые функции: WiFi только на время операции, затем `WIFI_OFF`.
4. OLED-дебаг только через `showDebugCodes` / `logToDisplay`.
5. После правок: компиляция в Arduino IDE с **достаточной** partition scheme; проверить sleep и координаты.
6. Падения: `Guru Meditation` / access fault — через Exception Decoder/addr2line.
