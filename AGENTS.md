# IMPORTANT (неукоснительно)
Перед началом выполнения любой задачи АГЕНТ ДОЛЖЕН:
1) Неукоснительно соблюдать требования, описанные в этом файле.
2) Полностью перечитать `AGENTS.md` перед каждым началом выполнения задачи, так как он может постоянно дополняться/модифицироваться.

# Celsius Clock (ESP32-C3) — AGENTS knowledge base

## Назначение проекта
Проект `Celsius` — часы на ESP32-C3 с OLED (SSD1306) и питанием от аккумулятора. Устройство:
- получает точное время через WiFi/NTP;
- компенсирует дрейф RTC между NTP-синхронизациями и поддерживает ручную коррекцию;
- обновляет экран и затем уходит в deep sleep до следующего нужного момента;
- может (опционально) запрашивать уличную температуру с погодного API по HTTP (включая подробный debug в Serial и на экране).

## Не ломать инварианты (самое важное)
1. **Deep sleep и расписание пробуждения**: логика `runCycle()` возвращает число секунд до следующего пробуждения, затем вызывается `esp_deep_sleep_start()`. Любые новые сетевые/тяжёлые действия должны быть встроены так, чтобы не ломать сон и не увеличивать время активной фазы без необходимости.
2. **WiFi**: часы не держат WiFi включенным постоянно. WiFi подключается при необходимости (NTP и/или погода), затем должен отключаться (особенно для погоды).
3. **Weather fetch**: погодный запрос должен выполняться только когда это разрешено логикой (включена опция, не ночь, есть валидное время, интервал не истёк).
4. **EEPROM layout**: настройки хранятся в EEPROM по фиксированным адресам; `EEPROM_SIZE` должен быть достаточным для всей структуры `DeviceSettings`.
5. **Debug codes**: функции логирования на OLED (`logToDisplay`) завязаны на флаги показа дебага. Не нужно безусловно “засорять” OLED — используйте `settings.showDebugCodes` и существующие коды.

## Важные файлы
1. `Celsius.ino`
   - основная логика: WiFi/NTP, вычисление epoch, дрейф-коррекция, обработка погоды в цикле, отображение на OLED, web-админка, EEPROM I/O, deep sleep; на ветке **main** — пробуждение по **GPIO4** и показ подробной погоды.
2. `WeatherAPI.h`
   - HTTP GET к погодному API, парсинг JSON (ArduinoJson), усреднение Narodmon; в RTC хранятся температура и (для OpenWeather) давление, влажность, ветер, ощущается как.
3. `WeatherDisplay.h`
   - `drawWeatherInfoScreen`: подробный экран из **кэша RTC** (без HTTP). На **main** (`setRotation(1)`, логически узкий портрет) при `display.width() < 64` — одна колонка коротких строк; при ширине ≥ 64 — компактные многострочные блоки. На **128x64** — своя двухколоночная вёрстка под 64 px высоты (см. ветку).

## Поддерживаемые варианты экрана (ветки)
- `main`, `correction` — панель **SSD1306 128×32**, в коде часто `setRotation(1)` (логическая область рисования «высокая»). Подробная погода: **GPIO4** → GND, настройка `weatherScreenSeconds`.
- `128x64` — дисплей **128×64** в **альбомной** ориентации; разметка даты/дня недели и экран погоды под эту высоту.

При разработке нового функционала важно учитывать, что в коде могут быть координаты/ориентация/размеры, рассчитанные под конкретный вариант экрана. Сначала выбери правильную ветку.

## Архитектура времени
### NTP sync
- В проекте используется `NTPClient` + список NTP серверов (`ntpServers[]`) и индекс (`ntpServerIndex`).
- Функция `ntpSync()` отвечает за:
  - включение/подключение WiFi,
  - попытку получить корректный epoch (через `timeClient.forceUpdate()`),
  - сохранение epoch как опорных значений (`storedEpoch`, `lastSyncEpoch`, `lastSyncLocalEpoch`),
  - обновление дрейф-коррекции при наличии второй синхронизации.

### RTC drift compensation
- Дрейф хранится в `RTC_DATA_ATTR int32_t driftCorrectionMs`.
- Опорная точка для вычисления/применения дрейфа — `lastSyncLocalEpoch`.
- Для вычисления “локального” времени используются:
  - `applyDriftCorrection(baseEpoch, referenceEpoch)`
  - `applyTimeCorrection(baseEpoch, referenceEpoch)` (ручная коррекция, сек/сутки).
- В `runCycle()` перед отрисовкой времени используется `storedEpoch` + поправки.

### Хранение epoch
- `storedEpoch` и связанные поля хранятся в `RTC_DATA_ATTR`, чтобы после deep sleep часы “не начинали с нуля”.

## Web-админка (конфигурация)
### Setup mode
- Если нет валидных WiFi-учётных данных или соединение не удаётся — устройство переходит в режим настройки.
- В setup mode поднимается AP:
  - `WiFi.mode(WIFI_AP)`
  - SSID: `AP_SSID` (CelsiusClock)
  - Password: `AP_PASSWORD`
  - web server на порту `80` (`WebServer server(80)`).
- Настройки выставляются через web-form.

### EEPROM и сохранение настроек
- Структура настроек: `DeviceSettings`
  - флаги отображения (debug codes, date, weekday, 12/24, hourly blink),
  - night mode start/end,
  - `syncDays`,
  - `timeCorrectionPerDay`,
  - погода: `weatherEnabled`, `weatherSource`, `weatherApiUrl[200]`, `weatherUpdateHours`, `weatherScreenSeconds`.
- Адреса EEPROM:
  - SSID: `EEPROM_SSID_ADDR = 0` (64 байта)
  - Password: `EEPROM_PASS_ADDR = 64` (64 байта)
  - Settings: `EEPROM_SETTINGS_ADDR = 128`
  - `EEPROM_SIZE` должен быть >= `EEPROM_SETTINGS_ADDR + sizeof(DeviceSettings)` (в текущем коде используется `512`).

## Погодный модуль (WeatherAPI.h)
### Ожидаемый формат JSON
- Для `weatherSource = 0` (narodmon):
  - ожидается корневой ключ `sensors`
  - `sensors` должен быть массивом объектов
  - внутри каждого объекта ожидается поле `value` (число)
  - значения усредняются, затем округляются до целого
- Для `weatherSource = 1` (accuweather/openweathermap current weather):
  - ожидается объект `main`; обязательно `main.temp` (округление для главного экрана);
  - дополнительно в RTC: `main.pressure`, `main.humidity`, `main.feels_like`, при наличии `wind.speed`.

### Интервалы обновления
- `lastWeatherUpdate` хранится в RTC; после **успешного** fetch задаётся в `Celsius.ino` как **`local`** (та же шкала, что `shouldUpdateWeather`), не `time(nullptr)`.
- `shouldUpdateWeather(currentTime, updateHours)`:
  - если `lastWeatherUpdate == 0` — первое обновление происходит сразу;
  - если `updateHours == 0` или слишком большое — принудительно минимум `1` час (чтобы не обновлялось каждую минуту);
  - если последняя погода неуспешна (outdoorTemperature = NaN) — повторная попытка через 5 минут;
  - успешное обновление соблюдает полный интервал `updateHours * 3600`;
  - если `currentTime - lastWeatherUpdate < 0` — возвращает `true`.

### Экран подробной погоды (GPIO4)
- `WEATHER_BUTTON_PIN` = **GPIO4** (замыкание на GND): `esp_deep_sleep_enable_gpio_wakeup` + в `runCycle()` при `wokeByWeatherButton || isWeatherButtonPressed()` вызывается `drawWeatherInfoScreen`, пауза `weatherScreenSeconds`. Отдельного HTTP нет.

### Ограничение по времени суток
- В `runCycle()` погодные обновления выполняются только если:
  - `settings.weatherEnabled == true`
  - `timeValid == true`
  - `!night` (вне night mode)
  - интервал обновления не истёк: `shouldUpdateWeather(local, settings.weatherUpdateHours)`

### WiFi в процессе погоды
- Перед HTTP запросом WiFi должен быть подключён.
- После `fetchOutdoorTemperature()` выполняется:
  - `WiFi.disconnect(true)`
  - `WiFi.mode(WIFI_OFF)`
  - переход в low-power.

### Debug
- `WeatherAPI.h` печатает подробности в Serial:
  - URL/длину,
  - HTTP code,
  - тело ответа,
  - ключи JSON,
  - значения sensor/value, усреднение.
- На OLED эти же этапы частично отображаются через `logToDisplay()` (только если включён `settings.showDebugCodes`).

## Отображение на OLED
### Рисование
- Дисплей: `Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1)`.
- В `drawClock(...)` (зависит от ветки): батарея, дата/день недели, время, outdoor/indoor, `display.display()`, сохранение буфера в RTC.
- Подробная погода: `WeatherDisplay.h` / `drawWeatherInfoScreen` — координаты под фактические `display.width()`/`height()` после `setRotation`.

### Батарея / дата (зависят от ветки)
- **main / correction**: см. текущий `drawBattery` и разметку `drawClock` в `Celsius.ino` ветки.
- **128x64**: иконка батареи, дата и день недели в одной строке — см. ветку `128x64`.

## Последовательность работы на устройстве
### setup()
1. Инициализация Serial; определение причины пробуждения (в т.ч. GPIO4 для погоды).
2. Подготовка LED_PIN / проверка аппаратного сброса (GPIO0 замкнут).
3. I2C / OLED init.
4. Инициализация SHT31.
5. `loadSettings()` (EEPROM).
6. Если WiFi настроек нет — `startConfigMode()` и ожидание в режиме веб-сервера.
7. Иначе выполняется `runCycle()` и затем deep sleep.

### loop()
- В обычном режиме почти ничего не делает (устройство выходит из deep sleep в setup()).
- В setup mode обрабатывает HTTP клиентов.

## Практика разработки (рекомендации агенту)
1. Перед любым изменением:
   - прочитай `AGENTS.md`,
   - проверь, под какую ветку/экран ты пишешь код.
2. Если добавляешь сетевые функции:
   - включай WiFi только на время операции,
   - не забывай отключать `WiFi.mode(WIFI_OFF)` после выполнения.
3. Если добавляешь новую отладку:
   - предпочти `Serial.printf` (для точной диагностики),
   - OLED дебаг включай только при `settings.showDebugCodes` через `logToDisplay`.
4. После правок скетча:
   - проверь компиляцию в Arduino IDE,
   - проверь, что deep sleep и отрисовка не “ломаются” координатами/ориентацией.

