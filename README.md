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
- **Switch** - Power control
- **Copper Wire** - Connections

## Software Requirements

### Required Libraries

- **ESP32 Arduino Core** - Core support for ESP32-C3
- **Adafruit GFX Library** - Graphics library for displays
- **Adafruit SSD1306** - Driver for SSD1306 OLED displays
- **Adafruit SHT31** - Driver for SHT31 temperature/humidity sensor
- **NTPClient** - Network Time Protocol client for time synchronization

### Built-in Libraries Used

- `WiFi.h` - WiFi connectivity
- `Wire.h` - I2C communication
- `WiFiUdp.h` - UDP communication for NTP
- `time.h` - Time functions

## Features

- **Time & Date** – Hours/minutes plus DD.MM and weekday (Cyrillic abbreviations)
- **Environment** – Temperature / humidity from SHT31 (single-shot mode for lower consumption)
- **Battery Indicator** – 5-bar level based on ADC reading (updated every 15 min by default)
- **WiFi Time Sync** – Sequential fallback across multiple NTP servers; retries until time becomes valid
- **Deep Sleep Strategy** – ESP32-C3 sleeps between updates while OLED keeps the previous frame (restores instantly on wake)
- **Night Mode** – Display and LED fully off from 23:00 to 07:00; daytime brightness fixed at 1/255
- **Hourly LED Blink** – Status LED toggles once at the top of every hour (disabled at night)
- **Status Codes (optional)** – Two-character codes (A1, B2, …) can be shown for troubleshooting when `SHOW_DEBUG_CODES` is set to 1

## Configuration

Before uploading the code, configure the following:

1. **WiFi Credentials** (lines 10-11):
   ```cpp
   const char* ssid = "WiFi_SSID";
   const char* password = "WiFi_Password";
   ```

2. **Timezone** (line 21):
   ```cpp
   NTPClient timeClient(ntpUDP, "pool.ntp.org", 3 * 3600, 60000); // GMT+3
   ```
   Change `3 * 3600` to your timezone offset (e.g., `7 * 3600` for GMT+7, `-5 * 3600` for GMT-5)

3. **Night Mode Window** (lines ~23-24):
   ```cpp
   #define NIGHT_START_H 23   // start hour (23:00)
   #define NIGHT_END_H   7    // end hour  (07:00)
   ```

4. **Debug Codes** (line 34):
   ```cpp
   #define SHOW_DEBUG_CODES 0   // set to 1 to show diagnostic codes on OLED
   ```

## Pin Configuration

- **I2C SDA**: GPIO 8
- **I2C SCL**: GPIO 9
- **LED**: GPIO 0
- **OLED Address**: 0x3C
- **SHT31 Address**: 0x44

## Installation

1. Install the ESP32 Arduino Core in Arduino IDE
2. Install required libraries via Library Manager:
   - Adafruit GFX Library
   - Adafruit SSD1306
   - Adafruit SHT31
   - NTPClient
3. Configure WiFi credentials and timezone in the code
4. Upload the sketch to your ESP32-C3
5. Connect the hardware according to the pin configuration

## Display Layout

- Top: 5-bar battery indicator
- Below top line: Date (DD.MM) and weekday (ПН / ВТ / …)
- Middle: Hours and minutes rendered in large font
- Bottom: Temperature (°C) and humidity (%)

## Notes

- The clock stores the last valid epoch in RTC memory, so it keeps ticking even without WiFi between syncs (default resync every 4 days).
- If WiFi is unavailable at boot, the firmware retries every minute until time is obtained, then returns to low duty cycle.
- Battery sampling is throttled (`BATTERY_RECHECK_SEC`, default 15 min) to reduce divider losses; adjust if you need more frequent updates.
- Status codes are disabled by default; enable `SHOW_DEBUG_CODES` for quick troubleshooting directly on the OLED.

## License

This project is open source and available for personal and educational use.

