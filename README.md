# DIY Stellar Clock - Celsius Version

A minimalist DIY clock project based on ESP32-C3 that displays time, date, temperature, and humidity on an OLED display. Features automatic display and LED shutdown during night hours (00:00-07:00), WiFi-based time synchronization, battery level indicator, and power-efficient light sleep mode for extended battery life.

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

### Built-in Libraries Used

- `WiFi.h` - WiFi connectivity
- `Wire.h` - I2C communication
- `WiFiUdp.h` - UDP communication for NTP
- `time.h` - Time functions

## Features

- **Time Display** - Shows hours and minutes in large format
- **Date Display** - Shows day and month (DD.MM format)
- **Day of Week** - Displays day of week in Cyrillic characters (ПН, ВТ, СР, ЧТ, ПТ, СБ, ВС)
- **Temperature & Humidity** - Displays readings from SHT31 sensor
- **Battery Level Indicator** - 5-bar battery indicator at the top of the display
- **WiFi Time Sync** - Automatically synchronizes time via NTP every 4 days
- **Compact Status Codes** - Two-character codes (A1, B2, …) briefly appear to show WiFi/NTP/sensor states without crowding the OLED
- **Power Efficiency** - Light sleep mode (950ms) for extended battery life (estimated 35-40 days, actual runtime may be significantly less)
- **Night Mode** - During night hours (00:00 to 07:00), the display is completely turned off and the LED hour indicator is disabled to save power and avoid disturbance
- **Hour Indicator** - LED blinks twice at the start of each hour (disabled during night mode)

## Configuration

Before uploading the code, configure the following:

1. **WiFi Credentials** (lines 16-17):
   ```cpp
   #define WIFI_SSID       "WiFi_SSID"
   #define WIFI_PASSWORD   "WiFi_Password"
   ```

2. **Timezone** (line 41):
   ```cpp
   NTPClient timeClient(ntpUDP, "pool.ntp.org", 3 * 3600, 60000);
   ```
   Time is already set to GMT+3. Change `3 * 3600` to your timezone offset if needed.

3. **Night Mode Times** (lines 28-29):
   ```cpp
   #define NIGHT_START_H   0   // 00:00
   #define NIGHT_END_H     7   // 07:00
   ```

4. **Sync Period** (line 30):
   ```cpp
   #define SYNC_DAYS       4   // Sync every 4 days
   ```

5. **Sleep Duration** (line 26):
   ```cpp
   #define SLEEP_US        950000UL     // 0.95 seconds
   ```
6. **Debug Codes** (line 33):
   ```cpp
   #define SHOW_DEBUG_CODES 0   // set to 1 to show per-minute ADC/battery info
   ```

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
3. Configure WiFi credentials and timezone in the code
4. Upload the sketch to your ESP32-C3
5. Connect the hardware according to the pin configuration

## Display Layout

The display shows:
- **Top**: Battery level indicator (5 bars, 0-5 based on voltage 3.0V-4.2V)
- **Date**: Day.Month format (DD.MM)
- **Day of Week**: Cyrillic day abbreviation (ПН, ВТ, СР, ЧТ, ПТ, СБ, ВС) displayed below the date
- **Time**: Hours and Minutes displayed separately in large text (2x size)
- **Bottom**: Temperature in °C and Humidity in %

## Power Management

- **Light Sleep Mode**: Device enters light sleep for 950ms between updates to conserve power
- **Battery Life**: Estimated 35-40 days of continuous operation (calculated value; actual runtime may be significantly less depending on battery capacity, usage patterns, and environmental conditions)
- **Battery Monitoring**: Voltage divider (1:1 ratio) on GPIO 3 using two 100 kΩ resistors, reads 0-2.5V range
- **Night Mode**: During night hours (00:00-07:00), both the display and LED hour indicator are completely turned off to save power and avoid light disturbance during sleep

## Status / Error Codes

Short two-character codes flash on the display to report internal events:

| Code | Meaning |
| ---- | ------- |
| `A1` | Attempting WiFi connection for NTP |
| `A2` | WiFi connection failed |
| `B1` | NTP synchronization in progress |
| `B2` | Time synchronized successfully |
| `B3` | Waiting for NTP (time not yet valid) |
| `C1` | SHT31 sensor detected |
| `C2` | SHT31 sensor missing |
| `D1` | First sync attempt after boot |
| `D2` | Setup routine finished |
| `E1` | Minute-level ADC/battery log (shown only when `SHOW_DEBUG_CODES` = 1) |

If the initial WiFi sync fails, the clock retries every minute until time is obtained; afterwards it re-syncs every 4 days at the next top-of-hour.

## Notes

- The device will continue to function even if WiFi connection fails (time won't sync, but will continue using last synced time)
- The sensor is optional - the device will work without it (no temperature/humidity display)
- **Night Mode Behavior**: During night hours (00:00-07:00), the display is completely turned off and the LED hour indicator (which normally blinks twice at the start of each hour) is disabled. This saves power and prevents light disturbance during sleep. During day hours, the display operates normally and the LED indicator functions as expected.
- Time synchronization occurs automatically every 4 days at midnight (if WiFi is available)
- Battery voltage is calculated from ADC reading with 2x multiplier (due to 1:1 voltage divider)

## License

This project is open source and available for personal and educational use.

