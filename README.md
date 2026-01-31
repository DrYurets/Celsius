# DIY Stellar Clock - Celsius Version

A minimalist ESP32-C3 clock that shows time, date, temperature, humidity, and battery status on a 128×32 OLED. The device keeps time via WiFi/NTP, runs in deep sleep between updates, and switches off the screen automatically during night hours to save power.

![DIY Stellar Clock](outer_view.jpg)

## Inspiration

This project is inspired by the [DIY Stellar Clock](https://sites.google.com/view/huy-materials-used/diy-stellar-clock) by Huy Vector DIY.

## Hardware Components

- **ESP32-C3** - Main microcontroller
- **OLED Display** - SSD1306, 128x32 pixels
- **SHT31-D Sensor** - Temperature and humidity sensor
- **LED** - Status indicator (blinks at the start of each hour)
- **Type-C Charging Module** - Power management
- **Battery** - 18350B or similar 3.7V lithium battery
- **Resistors** - Two 100 kΩ resistors for voltage divider (battery monitoring on GPIO 3)
- **Switch** - Power control
- **Copper Wire** - Connections

## Software Requirements

### Required Libraries

- **ESP32 Arduino Core** - Core support for ESP32-C3
- **Adafruit GFX Library** - Graphics library for displays
- **Adafruit SSD1306** - Driver for SSD1306 OLED displays
- **Adafruit SHT31** - Driver for SHT31 temperature/humidity sensor
- **NTPClient** - Network Time Protocol client for time synchronization
- **ArduinoJson** - JSON parsing for weather API responses

### Built-in Libraries Used

- `WiFi.h` - WiFi connectivity
- `Wire.h` - I2C communication
- `WiFiUdp.h` - UDP communication for NTP
- `time.h` - Time functions
- `WebServer.h` - HTTP server for WiFi and device configuration
- `EEPROM.h` - Non-volatile storage for WiFi credentials and device settings

## Features

- **Time & Date** – Hours/minutes with customizable display options (12/24-hour format, date, weekday)
- **Environment** – Temperature / humidity from SHT31 (single-shot mode for lower consumption); optional outdoor temperature from weather API (e.g. [Narodmon](https://narodmon.com))
- **Battery Indicator** – 5-bar level based on ADC reading (updated every 15 min by default)
- **WiFi Time Sync** – Sequential fallback across multiple NTP servers; retries until time becomes valid; configurable sync period (1-30 days via web interface)
- **Drift Correction** – Automatic time drift compensation between NTP syncs (calculated after second sync)
- **Manual Time Correction** – User-configurable time correction to compensate for quartz crystal inaccuracy (adjustable in seconds per day via web interface)
- **Web Configuration Interface** – Complete web-based setup for WiFi and all device settings via access point mode; no code modification needed
- **Customizable Display** – Toggle date, weekday, time format (12/24h), debug codes, and weekday language (English/Russian)
- **Configurable Night Mode** – Customizable start and end times for night mode (display and LED off)
- **Hourly LED Blink** – Optional status LED blink at the start of each hour (can be disabled)
- **Outdoor Weather (optional)** – Fetch outdoor temperature from a configurable API URL (default: Narodmon); update interval 1–24 hours; WiFi is connected only for the request, then disconnected; weather updates only when display is on (not in night mode)
- **Deep Sleep Strategy** – ESP32-C3 sleeps between updates while OLED keeps the previous frame (restores instantly on wake)
- **Status Codes (optional)** – Two-character diagnostic codes (A1, B2, …) can be enabled via web interface

## Configuration

### Web Configuration Interface

The device automatically enters setup mode on first boot or when WiFi credentials are missing/invalid. **All settings can be configured via web interface - no code modification needed!**

**Setup Process:**

1. **First Boot**: Device creates WiFi access point `CelsiusClock` (password: `12345678`)
2. **Connect**: Join the `CelsiusClock` network from your phone/computer
3. **Configure**: Open browser to `http://192.168.4.1` (or IP shown on display)
4. **Configure Settings**: Fill in all settings in the web form (see available settings below)
5. **Save**: Click "Save and Reset" to apply all settings
6. **Done**: Device reboots and connects to your WiFi network automatically

**Available Settings:**

**WiFi Settings:**
- WiFi SSID
- WiFi Password

**Display Settings:**
- Show debug codes (enable/disable diagnostic codes on OLED)
- Show date (toggle date display)
- Show weekday (toggle weekday display)
- 24-hour format (12/24-hour time format)
- Hourly LED blink (enable/disable LED blink at start of each hour)
- Weekday in Russian (English/Russian weekday language)

**Night Mode:**
- Night start time (hours and minutes)
- Night end time (hours and minutes)

**NTP Sync Settings:**
- Days between NTP syncs (1-30 days)
  - Controls how often the device synchronizes time with NTP servers
  - Default: 1 day (can be increased to reduce WiFi usage)
  - Longer intervals reduce power consumption but may result in less accurate time

**Time Correction:**
- Time correction (seconds per day) - Manual correction for quartz crystal inaccuracy
  - Positive value: speeds up the clock (use if clock is running slow)
  - Negative value: slows down the clock (use if clock is running fast)
  - Default: 0 (no correction)
  - Example: If clock is 4 minutes slow per day, enter `240` (4 minutes × 60 seconds)

**Weather Settings:**
- Enable weather data (checkbox) – show outdoor temperature from an API on the display
- Weather API URL – full URL for the weather API (default: Narodmon sensors; supports JSON with a `sensors` array and `value` field per sensor; values are averaged and rounded)
- Update interval (hours) – how often to fetch weather (1–24 hours, default: 1). Updates run only when the display is on (not in night mode). After each fetch, WiFi is disconnected; it is reconnected only before the next scheduled update.

**Setup Mode Behavior:**

- Device displays "Setup mode" on the OLED with SSID and IP address
- Web server runs at maximum CPU frequency (160 MHz) for responsiveness
- Device does not enter deep sleep in setup mode
- If WiFi connection fails after configuration, device automatically returns to setup mode
- All settings (WiFi credentials and device preferences) are stored in EEPROM and persist across reboots

**Resetting Settings:**

There are two ways to reset settings and return to setup mode:

1. **Via Web Interface** (when already in setup mode):
   - Open the setup page at `http://192.168.4.1`
   - Click the "Reset Settings" button
   - Device will clear settings and reboot into setup mode

2. **Via GPIO Reset** (hardware method):
   - Short GPIO 0 (LED pin) to GND (ground) while powering on the device
   - Hold for at least 50ms during startup
   - Device will detect the reset condition, clear settings, and enter setup mode
   - Diagnostic code I1 will be displayed on screen

### Code Configuration (Optional)

Most settings can be configured via web interface. The following settings can only be changed in code:

1. **Timezone** (line 85):
   ```cpp
   NTPClient timeClient(ntpUDP, "pool.ntp.org", 3 * 3600, 60000);
   ```
   Time is set to GMT+3. Change `3 * 3600` to your timezone offset if needed.

2. **Setup Mode AP** (lines 14-15):
   ```cpp
   #define AP_SSID "CelsiusClock"
   #define AP_PASSWORD "12345678"
   ```

**Note:** All other settings (display options, night mode times, debug codes, NTP sync period, etc.) can be configured via web interface and are stored in EEPROM.

## Pin Configuration

- **I2C SDA**: GPIO 8
- **I2C SCL**: GPIO 9
- **LED**: GPIO 0
- **Battery Monitor**: GPIO 3 (ADC with 11dB attenuation, 0-2.5V range) - requires voltage divider with two 100 kΩ resistors (1:1 ratio)
- **OLED Address**: 0x3C
- **SHT31 Address**: 0x44

## Installation

1. Install the ESP32 Arduino Core in Arduino IDE
2. Install required libraries via Library Manager:
   - Adafruit GFX Library
   - Adafruit SSD1306
   - Adafruit SHT31
   - NTPClient
   - ArduinoJson (for weather API)
3. (Optional) Adjust timezone in the code if needed (all other settings can be configured via web interface)
4. Upload the sketch to your ESP32-C3
5. Connect the hardware according to the pin configuration
6. On first boot, the device will enter setup mode automatically - configure WiFi and all device settings via web interface (see Web Configuration Interface section above)

## Display Layout

The display layout is customizable via web interface:

- **Top**: 5-bar battery indicator (always shown)
- **Date section** (optional, can be disabled): Date in DD.MM format
- **Weekday section** (optional, can be disabled): Weekday abbreviation (Russian: ПН/ВТ/СР/ЧТ/ПТ/СБ/ВС or English: MO/TU/WE/TH/FR/SA/SU)
- **Time section**: Hours and minutes in large font (12 or 24-hour format, configurable)
- **Bottom**: Indoor temperature (°C) and humidity (%) from SHT31 (always shown); if weather is enabled, outdoor temperature is also shown (from the configured API)

All display elements (except battery indicator and temperature/humidity) can be toggled on/off via web interface.

## Notes

- **Web Configuration**: All device settings (WiFi credentials, display options, night mode times, etc.) can be configured via web interface. Settings are stored in EEPROM and persist across reboots. No code modification is required for normal operation.
- **WiFi Configuration**: The device automatically enters setup mode if WiFi credentials are missing or if WiFi network becomes unavailable. In setup mode, the device creates an access point (`CelsiusClock`) and runs a web server for configuration. The device does not attempt NTP synchronization until WiFi is properly configured and available.
- **Time Storage**: The clock stores the last valid epoch in RTC memory, so it keeps ticking even without WiFi between syncs. The NTP sync period is configurable via web interface (1-30 days, default: 1 day).
- **Time Drift Correction**: The device automatically measures and compensates for RTC drift between NTP synchronizations. After the second sync, the drift rate is calculated and applied continuously. This compensates for typical ESP32-C3 RTC drift (e.g., ~3 minutes per day) without requiring more frequent WiFi syncs.
- **Manual Time Correction**: In addition to automatic drift correction, you can manually adjust for quartz crystal inaccuracy using the "Time correction" setting in the web interface. This correction is applied continuously and proportionally to elapsed time. For example, if your clock is consistently 4 minutes slow per day, set the correction to `240` (positive value to speed up). The correction accumulates between NTP syncs, significantly reducing time deviation. This is especially useful when the automatic drift correction alone is insufficient or when you need precise calibration for your specific hardware.
- **Setup Mode**: When in setup mode, the device runs at maximum CPU frequency (160 MHz) and does not enter deep sleep. After settings are configured and saved, the device reboots and returns to normal operation with deep sleep.
- **Night Mode**: Night mode times are fully configurable via web interface (start and end hours/minutes). During night mode, the display and LED are turned off to save power.
- **Display Customization**: All display elements (date, weekday, time format, debug codes) can be toggled on/off via web interface. Weekday language can be switched between English and Russian.
- **Weather & WiFi**: When weather is enabled, the device connects to WiFi only when it is time to update weather (and when the display is on). After the HTTP request, WiFi is disconnected and switched off (`WiFi.disconnect(true)` and `WiFi.mode(WIFI_OFF)`), so the radio is not left on between updates. On the next scheduled update, WiFi is connected again. This keeps power usage low.
- Battery sampling is throttled (`BATTERY_RECHECK_SEC`, default 15 min) to reduce divider losses; adjust if you need more frequent updates.
- Status codes are disabled by default; can be enabled via web interface for quick troubleshooting directly on the OLED.

## License

This project is open source and available for personal and educational use.

