#ifndef WEB_CONFIG_SERVER_H
#define WEB_CONFIG_SERVER_H

#include "sensors/SensorTypes.h"
#include "sensors/sht31/SHT31Image.h"
#include "sensors/aht20bmp280/AHT20BMP280Image.h"
#include "sensors/aht21/AHT21Image.h"
#include "sensors/htu21/HTU21Image.h"
#include "sensors/bmi160/BMI160Image.h"

static bool otaPrecheck(String &reason) {
  if (!configMode) {
    reason = "OTA is allowed only in setup mode.";
    return false;
  }
  const esp_partition_t *nextPart = esp_ota_get_next_update_partition(nullptr);
  if (!nextPart) {
    reason = "No OTA partition found. Rebuild with OTA partition scheme.";
    return false;
  }
  uint32_t mv = analogReadMilliVolts(BAT_PIN);
  float vBat = (mv * 2.0f) / 1000.0f;
  if (vBat < OTA_MIN_BATTERY_V) {
    reason = "Battery too low for OTA: " + String(vBat, 2) + "V (min " + String(OTA_MIN_BATTERY_V, 2) + "V)";
    return false;
  }
  return true;
}

static String otaStatusHtml() {
  if (!otaUpdateStarted) {
    return "";
  }
  if (otaUpdateSuccess) {
    return "<p style='color:#4CAF50; margin-top:10px;'>Last OTA: success. Device restarted.</p>";
  }
  return "<p style='color:#f44336; margin-top:10px;'>Last OTA error: " + otaUpdateError + "</p>";
}

void handleOtaUpload();
void handleOtaDone();
void handleSettingsExport();
void handleSettingsImport();
void handleSensorImage();

String getConfigPage() {
  uint8_t weekdaysMask = settings.activeWeekdaysMask & WEEKDAY_MASK_ALL;
  if (weekdaysMask == 0) {
    weekdaysMask = WEEKDAY_MASK_ALL;
  }
  const char *selectedTempSensor = tempSensorTypeToFormValue(settings.tempSensorType);

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Celsius Clock Setup</title>";
  html += "<style>";
  html += "body { font-family: Arial; max-width: 500px; margin: 0 auto; padding: 20px; background: #1a1a1a; color: #fff; }";
  html += "h1, h2 { color: #4CAF50; }";
  html += "input, select { width: 100%; padding: 10px; margin: 10px 0; border: 1px solid #555; border-radius: 5px; background: #333; color: #fff; box-sizing: border-box; }";
  html += "input[type='checkbox'] { width: auto; margin-right: 10px; }";
  html += "input[type='radio'] { width: auto; margin-right: 10px; }";
  html += ".checkbox-label { display: flex; align-items: center; margin: 10px 0; }";
  html += ".radio-label { display: flex; align-items: center; margin: 10px 0; }";
  html += ".time-group { display: flex; gap: 10px; }";
  html += ".time-group input { width: 50%; }";
  html += ".sensor-gallery { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 10px; margin: 10px 0 14px 0; }";
  html += ".sensor-card { border: 1px solid #555; border-radius: 8px; padding: 8px; background: #242424; }";
  html += ".sensor-card img { width: 100%; height: 92px; object-fit: cover; border-radius: 6px; display: block; margin-bottom: 6px; }";
  html += ".sensor-card .name { font-size: 12px; color: #ddd; text-align: center; }";
  html += "button { background: #4CAF50; color: white; padding: 15px; border: none; border-radius: 5px; cursor: pointer; width: 100%; font-size: 16px; }";
  html += "button:hover { background: #45a049; }";
  html += ".container { background: #2a2a2a; padding: 20px; border-radius: 10px; }";
  html += "</style></head><body>";
  html += "<div class='container'>";
  html += "<h1>Celsius Clock Setup</h1>";
  html += "<p>Firmware version: <b>" + String(ROM_VERSION) + "</b></p>";
  html += "<form method='POST' action='/save'>";

  html += "<h2>WiFi Settings</h2>";
  html += "<label>WiFi SSID:</label>";
  html += "<input type='text' name='ssid' value='" + String(wifiSSID) + "' required>";
  html += "<label>WiFi Password:</label>";
  html += "<input type='password' name='password' value='" + String(wifiPassword) + "'>";

  html += "<h2>Display Settings</h2>";
  html += "<div class='checkbox-label'><input type='checkbox' name='showDebugCodes' " + String(settings.showDebugCodes ? "checked" : "") + "><label>Show debug codes</label></div>";
  html += "<div class='checkbox-label'><input type='checkbox' name='showDate' " + String(settings.showDate ? "checked" : "") + "><label>Show date</label></div>";
  html += "<div class='checkbox-label'><input type='checkbox' name='showWeekday' " + String(settings.showWeekday ? "checked" : "") + "><label>Show weekday</label></div>";
  html += "<div class='checkbox-label'><input type='checkbox' name='timeFormat24h' " + String(settings.timeFormat24h ? "checked" : "") + "><label>24-hour format</label></div>";
  html += "<div class='checkbox-label'><input type='checkbox' name='hourlyBlink' " + String(settings.hourlyBlink ? "checked" : "") + "><label>Hourly LED blink</label></div>";
  html += "<div class='checkbox-label'><input type='checkbox' name='weekdayLanguageRu' " + String(settings.weekdayLanguageRu ? "checked" : "") + "><label>Weekday in Russian</label></div>";
  html += "<label>Language:</label>";
  html += "<select name='uiLanguage' style='width: 100%; padding: 10px; margin: 10px 0; border: 1px solid #555; border-radius: 5px; background: #333; color: #fff; box-sizing: border-box;'>";
  html += "<option value='ru' " + String(settings.uiLanguage == UI_LANG_RU ? "selected" : "") + ">Russian</option>";
  html += "<option value='en' " + String(settings.uiLanguage == UI_LANG_EN ? "selected" : "") + ">English</option>";
  html += "</select>";

  html += "<h2>Device Configuration</h2>";
  html += "<p style='font-size: 12px; color: #aaa; margin-top: 0; margin-bottom: 10px;'>Select the installed sensors. Temperature sensor can be only one.</p>";
  html += "<div class='sensor-gallery'>";
  html += "<div class='sensor-card'><img src='/sensors/sht31/sht31.jpg' alt='SHT31'><div class='name'>SHT31</div></div>";
  html += "<div class='sensor-card'><img src='/sensors/aht20bmp280/aht20bmp280.jpg' alt='AHT20+BMP280'><div class='name'>AHT20+BMP280</div></div>";
  html += "<div class='sensor-card'><img src='/sensors/aht21/aht21.jpg' alt='AHT21'><div class='name'>AHT21</div></div>";
  html += "<div class='sensor-card'><img src='/sensors/htu21/htu21.jpg' alt='HTU21'><div class='name'>HTU21</div></div>";
  html += "<div class='sensor-card'><img src='/sensors/bmi160/bmi160.jpg' alt='BMI160'><div class='name'>BMI160 (Gyro/Motion)</div></div>";
  html += "</div>";
  html += "<label>Temperature/Humidity sensor:</label>";
  html += "<div class='radio-label'><input type='radio' name='tempSensorType' value='sht31' " + String(strcmp(selectedTempSensor, "sht31") == 0 ? "checked" : "") + "><label>SHT31</label></div>";
  html += "<div class='radio-label'><input type='radio' name='tempSensorType' value='aht20bmp280' " + String(strcmp(selectedTempSensor, "aht20bmp280") == 0 ? "checked" : "") + "><label>AHT20 + BMP280</label></div>";
  html += "<div class='radio-label'><input type='radio' name='tempSensorType' value='aht21' " + String(strcmp(selectedTempSensor, "aht21") == 0 ? "checked" : "") + "><label>AHT21</label></div>";
  html += "<div class='radio-label'><input type='radio' name='tempSensorType' value='htu21' " + String(strcmp(selectedTempSensor, "htu21") == 0 ? "checked" : "") + "><label>HTU21</label></div>";
  html += "<div class='checkbox-label'><input type='checkbox' name='enableBmi160' " + String(settings.bmi160Enabled ? "checked" : "") + "><label>Enable BMI160 (auto screen flip when upside down)</label></div>";
  html += "<p style='font-size: 12px; color: #aaa; margin-top: -5px; margin-bottom: 10px;'>Sensor selection is active in runtime and saved to EEPROM.</p>";

  html += "<h2>Night Mode</h2>";
  html += "<label>Night start time:</label>";
  html += "<div class='time-group'>";
  html += "<input type='number' name='nightStartH' min='0' max='23' value='" + String(settings.nightStartH) + "' required>";
  html += "<input type='number' name='nightStartM' min='0' max='59' value='" + String(settings.nightStartM) + "' required>";
  html += "</div>";
  html += "<label>Night end time:</label>";
  html += "<div class='time-group'>";
  html += "<input type='number' name='nightEndH' min='0' max='23' value='" + String(settings.nightEndH) + "' required>";
  html += "<input type='number' name='nightEndM' min='0' max='59' value='" + String(settings.nightEndM) + "' required>";
  html += "</div>";

  html += "<h2>Work Days</h2>";
  html += "<div class='checkbox-label'><input type='checkbox' name='wdMon' " + String((weekdaysMask & (1U << 0)) ? "checked" : "") + "><label>Mon</label></div>";
  html += "<div class='checkbox-label'><input type='checkbox' name='wdTue' " + String((weekdaysMask & (1U << 1)) ? "checked" : "") + "><label>Tue</label></div>";
  html += "<div class='checkbox-label'><input type='checkbox' name='wdWed' " + String((weekdaysMask & (1U << 2)) ? "checked" : "") + "><label>Wed</label></div>";
  html += "<div class='checkbox-label'><input type='checkbox' name='wdThu' " + String((weekdaysMask & (1U << 3)) ? "checked" : "") + "><label>Th</label></div>";
  html += "<div class='checkbox-label'><input type='checkbox' name='wdFri' " + String((weekdaysMask & (1U << 4)) ? "checked" : "") + "><label>Fr</label></div>";
  html += "<div class='checkbox-label'><input type='checkbox' name='wdSat' " + String((weekdaysMask & (1U << 5)) ? "checked" : "") + "><label>Sat</label></div>";
  html += "<div class='checkbox-label'><input type='checkbox' name='wdSun' " + String((weekdaysMask & (1U << 6)) ? "checked" : "") + "><label>Su</label></div>";

  html += "<h2>Time and Network Sync</h2>";
  html += "<label>Update interval (hours):</label>";
  html += "<input type='number' name='weatherUpdateHours' min='1' max='24' value='" + String(settings.weatherUpdateHours) + "' required style='margin-bottom: 10px;'>";
  html += "<p style='font-size: 12px; color: #aaa; margin-top: -5px; margin-bottom: 10px;'>NTP time sync runs on this interval in active daytime mode (work days, not night). Weather is fetched in the same WiFi session (1-24 h).</p>";

  html += "<h2>Time Correction</h2>";
  html += "<label>Timezone:</label>";
  html += "<select name='timezoneMinutes' style='width: 100%; padding: 10px; margin: 10px 0; border: 1px solid #555; border-radius: 5px; background: #333; color: #fff; box-sizing: border-box;'>";
  html += "<option value='-720' " + String(settings.timezoneMinutes == -720 ? "selected" : "") + ">UTC-12:00</option>";
  html += "<option value='-660' " + String(settings.timezoneMinutes == -660 ? "selected" : "") + ">UTC-11:00</option>";
  html += "<option value='-600' " + String(settings.timezoneMinutes == -600 ? "selected" : "") + ">UTC-10:00</option>";
  html += "<option value='-540' " + String(settings.timezoneMinutes == -540 ? "selected" : "") + ">UTC-09:00</option>";
  html += "<option value='-480' " + String(settings.timezoneMinutes == -480 ? "selected" : "") + ">UTC-08:00</option>";
  html += "<option value='-420' " + String(settings.timezoneMinutes == -420 ? "selected" : "") + ">UTC-07:00</option>";
  html += "<option value='-360' " + String(settings.timezoneMinutes == -360 ? "selected" : "") + ">UTC-06:00</option>";
  html += "<option value='-300' " + String(settings.timezoneMinutes == -300 ? "selected" : "") + ">UTC-05:00</option>";
  html += "<option value='-240' " + String(settings.timezoneMinutes == -240 ? "selected" : "") + ">UTC-04:00</option>";
  html += "<option value='-180' " + String(settings.timezoneMinutes == -180 ? "selected" : "") + ">UTC-03:00</option>";
  html += "<option value='-120' " + String(settings.timezoneMinutes == -120 ? "selected" : "") + ">UTC-02:00</option>";
  html += "<option value='-60' " + String(settings.timezoneMinutes == -60 ? "selected" : "") + ">UTC-01:00</option>";
  html += "<option value='0' " + String(settings.timezoneMinutes == 0 ? "selected" : "") + ">UTC+00:00</option>";
  html += "<option value='60' " + String(settings.timezoneMinutes == 60 ? "selected" : "") + ">UTC+01:00</option>";
  html += "<option value='120' " + String(settings.timezoneMinutes == 120 ? "selected" : "") + ">UTC+02:00</option>";
  html += "<option value='180' " + String(settings.timezoneMinutes == 180 ? "selected" : "") + ">UTC+03:00 (Moscow)</option>";
  html += "<option value='240' " + String(settings.timezoneMinutes == 240 ? "selected" : "") + ">UTC+04:00</option>";
  html += "<option value='300' " + String(settings.timezoneMinutes == 300 ? "selected" : "") + ">UTC+05:00</option>";
  html += "<option value='330' " + String(settings.timezoneMinutes == 330 ? "selected" : "") + ">UTC+05:30</option>";
  html += "<option value='360' " + String(settings.timezoneMinutes == 360 ? "selected" : "") + ">UTC+06:00</option>";
  html += "<option value='420' " + String(settings.timezoneMinutes == 420 ? "selected" : "") + ">UTC+07:00</option>";
  html += "<option value='480' " + String(settings.timezoneMinutes == 480 ? "selected" : "") + ">UTC+08:00</option>";
  html += "<option value='540' " + String(settings.timezoneMinutes == 540 ? "selected" : "") + ">UTC+09:00</option>";
  html += "<option value='570' " + String(settings.timezoneMinutes == 570 ? "selected" : "") + ">UTC+09:30</option>";
  html += "<option value='600' " + String(settings.timezoneMinutes == 600 ? "selected" : "") + ">UTC+10:00</option>";
  html += "<option value='660' " + String(settings.timezoneMinutes == 660 ? "selected" : "") + ">UTC+11:00</option>";
  html += "<option value='720' " + String(settings.timezoneMinutes == 720 ? "selected" : "") + ">UTC+12:00</option>";
  html += "<option value='780' " + String(settings.timezoneMinutes == 780 ? "selected" : "") + ">UTC+13:00</option>";
  html += "<option value='840' " + String(settings.timezoneMinutes == 840 ? "selected" : "") + ">UTC+14:00</option>";
  html += "</select>";
  html += "<p style='font-size: 12px; color: #aaa; margin-top: -5px; margin-bottom: 10px;'>Default is UTC+03:00 (Moscow).</p>";
  html += "<label>Time correction (seconds per day):</label>";
  html += "<input type='number' name='timeCorrectionPerDay' value='" + String(settings.timeCorrectionPerDay) + "' step='1' style='margin-bottom: 10px;'>";
  html += "<p style='font-size: 12px; color: #aaa; margin-top: -5px; margin-bottom: 10px;'>Positive = speed up, negative = slow down. Example: +240 if clock is 4 min slow per day</p>";

  html += "<h2>Weather Settings</h2>";
  html += "<p style='font-size: 12px; color: #aaa; margin-top: 0; margin-bottom: 10px;'>Weather fetch is always enabled.</p>";
  html += "<div id='weatherSettings' style='display: block;'>";
  html += "<label>Weather source:</label>";
  html += "<input type='text' value='Open-Meteo' disabled style='margin-bottom: 10px; color: #aaa;'>";
  html += "<label>Latitude:</label>";
  html += "<input type='number' name='weatherLatitude' min='-90' max='90' step='0.0001' value='" + String(settings.weatherLatitude, 4) + "' required style='margin-bottom: 10px;'>";
  html += "<label>Longitude:</label>";
  html += "<input type='number' name='weatherLongitude' min='-180' max='180' step='0.0001' value='" + String(settings.weatherLongitude, 4) + "' required style='margin-bottom: 10px;'>";
  html += "<p style='font-size: 12px; color: #aaa; margin-top: -5px; margin-bottom: 10px;'>Open-Meteo URL is generated automatically from coordinates.</p>";
  html += "<label>Weather screen timeout (sec):</label>";
  html += "<input type='number' name='weatherScreenSeconds' min='1' max='60' value='" + String(settings.weatherScreenSeconds) + "' required style='margin-bottom: 10px;'>";
  html += "<p style='font-size: 12px; color: #aaa; margin-top: -5px; margin-bottom: 10px;'>GPIO4 short to GND: wake + show cached weather details (no extra HTTP).</p>";
  html += "</div>";
  html += "<h2>Auto OTA</h2>";
  html += "<div class='checkbox-label'><input type='checkbox' name='autoOtaEnabled' " + String(settings.autoOtaEnabled ? "checked" : "") + "><label>Enable automatic firmware updates</label></div>";
  html += "<label>Check interval (hours):</label>";
  html += "<input type='number' name='autoOtaCheckHours' min='1' max='168' value='" + String(settings.autoOtaCheckHours) + "' required style='margin-bottom: 10px;'>";
  html += "<p style='font-size: 12px; color: #aaa; margin-top: -5px; margin-bottom: 10px;'>Checks run only in normal daytime cycle with valid time and sufficient battery.</p>";

  html += "<button type='submit' style='margin-top: 20px;'>Save and Reset</button>";
  html += "<button type='submit' formaction='/settings/export' formmethod='POST' style='background: #607d8b; margin-top: 10px;'>Export current form as JSON</button>";
  html += "</form>";

  html += "<hr style='margin: 20px 0; border-color: #555;'>";
  html += "<h2>Settings Backup</h2>";
  html += "<p style='font-size: 12px; color: #aaa; margin-top: 0;'>Export current settings to JSON and import them back after flashing.</p>";
  html += "<p style='font-size: 12px; color: #aaa; margin-top: 0;'>Use the export button above to export exactly what is currently entered in the form.</p>";
  html += "<form method='POST' action='/settings/import' style='margin-top: 12px;'>";
  html += "<label>Import JSON:</label>";
  html += "<textarea name='settingsJson' rows='10' style='width: 100%; box-sizing: border-box; padding: 10px; background: #333; color: #fff; border: 1px solid #555; border-radius: 5px;' placeholder='{...settings json...}'></textarea>";
  html += "<button type='submit' style='background: #3f51b5; margin-top: 10px;'>Import settings and reboot</button>";
  html += "</form>";

  html += "<hr style='margin: 20px 0; border-color: #555;'>";
  html += "<form method='POST' action='/reset' style='margin-top: 20px;'>";
  html += "<button type='submit' style='background: #f44336;'>Reset Settings</button>";
  html += "</form>";

  html += "<hr style='margin: 20px 0; border-color: #555;'>";
  html += "<h2>Firmware OTA</h2>";
  html += "<p style='font-size: 12px; color: #ffb74d; margin-top: 0;'>Do not power off during update. The firmware filename must be exactly <b>Celsius.ino.bin</b>.</p>";
  html += "<form method='POST' action='/ota' enctype='multipart/form-data'>";
  html += "<label>Firmware file (.bin):</label>";
  html += "<input type='file' id='firmwareFile' name='firmware' accept='.bin,application/octet-stream' required>";
  html += "<button type='submit' style='background: #ff9800; margin-top: 10px;'>Update Firmware (OTA)</button>";
  html += "</form>";
  html += otaStatusHtml();

  html += "<script>";
  html += "const fw=document.getElementById('firmwareFile');";
  html += "if(fw){fw.addEventListener('change',()=>{";
  html += "if(!fw.files||!fw.files.length)return;";
  html += "const n=fw.files[0].name;";
  html += "if(n!=='Celsius.ino.bin'){alert('Firmware file must be named Celsius.ino.bin');fw.value='';}";
  html += "});}";
  html += "</script>";

  html += "</div></body></html>";
  return html;
}

void handleRoot() {
  server.send(200, "text/html", getConfigPage());
}

void handleReset() {
  clearWiFiConfig();
  logToDisplay(CODE_CONFIG_RESET);

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Settings reset</title>";
  html += "<style>body { font-family: Arial; text-align: center; margin-top: 50px; background: #1a1a1a; color: #fff; }";
  html += ".message { background: #2a2a2a; padding: 20px; border-radius: 10px; max-width: 400px; margin: 0 auto; }";
  html += "h1 { color: #f44336; }</style></head><body>";
  html += "<div class='message'><h1>Settings Reset!</h1>";
  html += "<p>Resetting...</p></div></body></html>";
  server.send(200, "text/html", html);

  delay(2000);
  ESP.restart();
}

void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("password")) {
    String ssid = server.arg("ssid");
    String password = server.arg("password");

    ssid.trim();
    password.trim();

    if (ssid.length() == 0 || ssid.length() >= 64 || password.length() >= 64) {
      server.send(400, "text/plain", "Error: Invalid SSID or password length");
      return;
    }

    ssid.toCharArray(wifiSSID, 64);
    password.toCharArray(wifiPassword, 64);
    wifiSSID[63] = '\0';
    wifiPassword[63] = '\0';

    settings.showDebugCodes = server.hasArg("showDebugCodes");
    settings.showDate = server.hasArg("showDate");
    settings.showWeekday = server.hasArg("showWeekday");
    settings.timeFormat24h = server.hasArg("timeFormat24h");
    settings.hourlyBlink = server.hasArg("hourlyBlink");
    settings.weekdayLanguageRu = server.hasArg("weekdayLanguageRu");
    if (server.hasArg("uiLanguage")) {
      String lang = server.arg("uiLanguage");
      lang.trim();
      settings.uiLanguage = (lang == "en") ? UI_LANG_EN : UI_LANG_RU;
    }

    if (server.hasArg("nightStartH")) {
      int h = server.arg("nightStartH").toInt();
      int m = server.arg("nightStartM").toInt();
      if (h >= 0 && h <= 23 && m >= 0 && m <= 59) {
        settings.nightStartH = h;
        settings.nightStartM = m;
      }
    }

    if (server.hasArg("nightEndH")) {
      int h = server.arg("nightEndH").toInt();
      int m = server.arg("nightEndM").toInt();
      if (h >= 0 && h <= 23 && m >= 0 && m <= 59) {
        settings.nightEndH = h;
        settings.nightEndM = m;
      }
    }

    if (server.hasArg("timeCorrectionPerDay")) {
      int32_t correction = server.arg("timeCorrectionPerDay").toInt();
      if (correction >= -3600 && correction <= 3600) {
        settings.timeCorrectionPerDay = correction;
      }
    }
    if (server.hasArg("timezoneMinutes")) {
      int tz = server.arg("timezoneMinutes").toInt();
      if (tz >= -720 && tz <= 840) {
        settings.timezoneMinutes = (int16_t)tz;
      }
    }

    settings.syncDays = 1;

    uint8_t weekdaysMask = 0;
    if (server.hasArg("wdMon")) weekdaysMask |= (1U << 0);
    if (server.hasArg("wdTue")) weekdaysMask |= (1U << 1);
    if (server.hasArg("wdWed")) weekdaysMask |= (1U << 2);
    if (server.hasArg("wdThu")) weekdaysMask |= (1U << 3);
    if (server.hasArg("wdFri")) weekdaysMask |= (1U << 4);
    if (server.hasArg("wdSat")) weekdaysMask |= (1U << 5);
    if (server.hasArg("wdSun")) weekdaysMask |= (1U << 6);
    settings.activeWeekdaysMask = weekdaysMask & WEEKDAY_MASK_ALL;
    if (settings.activeWeekdaysMask == 0) {
      settings.activeWeekdaysMask = WEEKDAY_MASK_ALL;
    }

    settings.weatherEnabled = true;
    settings.weatherSource = WEATHER_SOURCE_OPEN_METEO;

    if (server.hasArg("weatherLatitude")) {
      float lat = server.arg("weatherLatitude").toFloat();
      if (isValidLatitude(lat)) {
        settings.weatherLatitude = lat;
      }
    }

    if (server.hasArg("weatherLongitude")) {
      float lon = server.arg("weatherLongitude").toFloat();
      if (isValidLongitude(lon)) {
        settings.weatherLongitude = lon;
      }
    }
    rebuildOpenMeteoUrlFromCoordinates();

    if (server.hasArg("weatherUpdateHours")) {
      int hours = server.arg("weatherUpdateHours").toInt();
      if (hours >= 1 && hours <= 24) {
        settings.weatherUpdateHours = hours;
      }
    }
    if (server.hasArg("weatherScreenSeconds")) {
      int sec = server.arg("weatherScreenSeconds").toInt();
      if (sec >= 1 && sec <= 60) {
        settings.weatherScreenSeconds = sec;
      }
    }
    if (server.hasArg("tempSensorType")) {
      String sensorType = server.arg("tempSensorType");
      sensorType.trim();
      settings.tempSensorType = parseTempSensorType(sensorType);
    }
    settings.bmi160Enabled = server.hasArg("enableBmi160");
    settings.autoOtaEnabled = server.hasArg("autoOtaEnabled");
    if (server.hasArg("autoOtaCheckHours")) {
      int hours = server.arg("autoOtaCheckHours").toInt();
      if (hours >= AUTOOTA_CHECK_INTERVAL_HOURS_MIN && hours <= AUTOOTA_CHECK_INTERVAL_HOURS_MAX) {
        settings.autoOtaCheckHours = (uint16_t)hours;
      }
    }

    saveWiFiConfig(wifiSSID, wifiPassword);
    saveSettings();
    loadWiFiConfig();

    if (strlen(wifiSSID) == 0 || strcmp(wifiSSID, ssid.c_str()) != 0) {
      char detail[32];
      snprintf(detail, sizeof(detail), "len=%d", strlen(wifiSSID));
      logToDisplay(CODE_WIFI_CONFIG_ERR, detail);
      server.send(500, "text/plain", "Error: Failed to save settings");
      delay(2000);
      return;
    }

    logToDisplay(CODE_CONFIG_SAVED);

    String html = "<!DOCTYPE html><html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>Settings saved</title>";
    html += "<style>body { font-family: Arial; text-align: center; margin-top: 50px; background: #1a1a1a; color: #fff; }";
    html += ".message { background: #2a2a2a; padding: 20px; border-radius: 10px; max-width: 400px; margin: 0 auto; }";
    html += "h1 { color: #4CAF50; }</style></head><body>";
    html += "<div class='message'><h1>Saved!</h1>";
    html += "<p>Resetting...</p></div></body></html>";
    server.send(200, "text/html", html);

    delay(2000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Error: SSID or password missed");
  }
}

void handleOtaUpload() {
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    otaUpdateStarted = true;
    otaUpdateSuccess = false;
    otaUpdateError = "";

    if (upload.filename != "Celsius.ino.bin") {
      otaUpdateError = "Invalid filename. Use Celsius.ino.bin";
      return;
    }

    String precheckReason;
    if (!otaPrecheck(precheckReason)) {
      otaUpdateError = precheckReason;
      return;
    }

    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      otaUpdateError = String("Update.begin failed: ") + Update.errorString();
      return;
    }
    Serial.printf("[OTA] Start: %s\n", upload.filename.c_str());
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!otaUpdateError.isEmpty()) {
      return;
    }
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      otaUpdateError = String("Write failed: ") + Update.errorString();
      Update.abort();
      return;
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!otaUpdateError.isEmpty()) {
      return;
    }
    if (!Update.end(true)) {
      otaUpdateError = String("Finalize failed: ") + Update.errorString();
      return;
    }
    otaUpdateSuccess = true;
    Serial.printf("[OTA] Success, size=%u bytes\n", upload.totalSize);
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    otaUpdateError = "Upload aborted";
    Update.abort();
    Serial.println("[OTA] Upload aborted");
  }
}

void handleOtaDone() {
  if (!otaUpdateStarted) {
    server.send(400, "text/plain", "OTA session not started");
    return;
  }
  if (!otaUpdateSuccess) {
    String msg = otaUpdateError.isEmpty() ? String("OTA failed") : ("OTA failed: " + otaUpdateError);
    server.send(500, "text/plain", msg);
    return;
  }

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>OTA success</title>";
  html += "<style>body { font-family: Arial; text-align: center; margin-top: 50px; background: #1a1a1a; color: #fff; }";
  html += ".message { background: #2a2a2a; padding: 20px; border-radius: 10px; max-width: 460px; margin: 0 auto; }";
  html += "h1 { color: #4CAF50; }</style></head><body>";
  html += "<div class='message'><h1>Firmware updated</h1>";
  html += "<p>Rebooting into new firmware...</p></div></body></html>";
  server.send(200, "text/html", html);
  delay(1500);
  ESP.restart();
}

void handleSettingsExport() {
  if (server.method() == HTTP_POST) {
    DeviceSettings preview = settings;
    if (server.hasArg("showDebugCodes")) preview.showDebugCodes = true; else preview.showDebugCodes = false;
    if (server.hasArg("showDate")) preview.showDate = true; else preview.showDate = false;
    if (server.hasArg("showWeekday")) preview.showWeekday = true; else preview.showWeekday = false;
    if (server.hasArg("timeFormat24h")) preview.timeFormat24h = true; else preview.timeFormat24h = false;
    if (server.hasArg("hourlyBlink")) preview.hourlyBlink = true; else preview.hourlyBlink = false;
    if (server.hasArg("weekdayLanguageRu")) preview.weekdayLanguageRu = true; else preview.weekdayLanguageRu = false;
    if (server.hasArg("uiLanguage")) {
      String lang = server.arg("uiLanguage");
      lang.trim();
      preview.uiLanguage = (lang == "en") ? UI_LANG_EN : UI_LANG_RU;
    }
    if (server.hasArg("nightStartH")) preview.nightStartH = (uint8_t)server.arg("nightStartH").toInt();
    if (server.hasArg("nightStartM")) preview.nightStartM = (uint8_t)server.arg("nightStartM").toInt();
    if (server.hasArg("nightEndH")) preview.nightEndH = (uint8_t)server.arg("nightEndH").toInt();
    if (server.hasArg("nightEndM")) preview.nightEndM = (uint8_t)server.arg("nightEndM").toInt();
    if (server.hasArg("timeCorrectionPerDay")) preview.timeCorrectionPerDay = server.arg("timeCorrectionPerDay").toInt();
    if (server.hasArg("timezoneMinutes")) preview.timezoneMinutes = (int16_t)server.arg("timezoneMinutes").toInt();
    if (server.hasArg("wdMon") || server.hasArg("wdTue") || server.hasArg("wdWed") || server.hasArg("wdThu") || server.hasArg("wdFri") || server.hasArg("wdSat") || server.hasArg("wdSun")) {
      uint8_t mask = 0;
      if (server.hasArg("wdMon")) mask |= (1U << 0);
      if (server.hasArg("wdTue")) mask |= (1U << 1);
      if (server.hasArg("wdWed")) mask |= (1U << 2);
      if (server.hasArg("wdThu")) mask |= (1U << 3);
      if (server.hasArg("wdFri")) mask |= (1U << 4);
      if (server.hasArg("wdSat")) mask |= (1U << 5);
      if (server.hasArg("wdSun")) mask |= (1U << 6);
      preview.activeWeekdaysMask = (mask == 0) ? WEEKDAY_MASK_ALL : mask;
    }
    if (server.hasArg("weatherLatitude")) preview.weatherLatitude = server.arg("weatherLatitude").toFloat();
    if (server.hasArg("weatherLongitude")) preview.weatherLongitude = server.arg("weatherLongitude").toFloat();
    if (server.hasArg("weatherUpdateHours")) preview.weatherUpdateHours = (uint8_t)server.arg("weatherUpdateHours").toInt();
    if (server.hasArg("weatherScreenSeconds")) preview.weatherScreenSeconds = (uint8_t)server.arg("weatherScreenSeconds").toInt();
    if (server.hasArg("tempSensorType")) {
      String sensorType = server.arg("tempSensorType");
      sensorType.trim();
      preview.tempSensorType = parseTempSensorType(sensorType);
    }
    preview.bmi160Enabled = server.hasArg("enableBmi160");
    preview.autoOtaEnabled = server.hasArg("autoOtaEnabled");
    if (server.hasArg("autoOtaCheckHours")) preview.autoOtaCheckHours = (uint16_t)server.arg("autoOtaCheckHours").toInt();

    DeviceSettings saved = settings;
    settings = preview;
    rebuildOpenMeteoUrlFromCoordinates();
    String json;
    bool ok = exportSettingsToJson(json);
    settings = saved;
    if (!ok) {
      server.send(500, "text/plain", "Failed to build settings JSON");
      return;
    }
    server.sendHeader("Content-Disposition", "attachment; filename=\"settings.json\"");
    server.send(200, "application/json; charset=utf-8", json);
    return;
  }

  String json;
  if (!exportSettingsToJson(json)) {
    server.send(500, "text/plain", "Failed to build settings JSON");
    return;
  }
  server.sendHeader("Content-Disposition", "attachment; filename=\"settings.json\"");
  server.send(200, "application/json; charset=utf-8", json);
}

void handleSettingsImport() {
  String json = server.arg("settingsJson");
  if (json.length() == 0 && server.hasArg("plain")) {
    json = server.arg("plain");
  }
  json.trim();
  if (json.length() == 0) {
    server.send(400, "text/plain", "settingsJson payload is empty");
    return;
  }

  String error;
  if (!importSettingsFromJson(json, error)) {
    server.send(400, "text/plain", "Import failed: " + error);
    return;
  }

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Settings imported</title>";
  html += "<style>body { font-family: Arial; text-align: center; margin-top: 50px; background: #1a1a1a; color: #fff; }";
  html += ".message { background: #2a2a2a; padding: 20px; border-radius: 10px; max-width: 460px; margin: 0 auto; }";
  html += "h1 { color: #4CAF50; }</style></head><body>";
  html += "<div class='message'><h1>Settings imported</h1>";
  html += "<p>Device will reboot now.</p></div></body></html>";
  server.send(200, "text/html", html);
  delay(1200);
  ESP.restart();
}

void handleSensorImage() {
  String uri = server.uri();
  const uint8_t *data = nullptr;
  size_t len = 0;

  if (uri == "/sensors/sht31/sht31.jpg") {
    data = sensors_sht31_sht31_jpg;
    len = sensors_sht31_sht31_jpg_len;
  } else if (uri == "/sensors/aht20bmp280/aht20bmp280.jpg") {
    data = sensors_aht20bmp280_aht20bmp280_jpg;
    len = sensors_aht20bmp280_aht20bmp280_jpg_len;
  } else if (uri == "/sensors/aht21/aht21.jpg") {
    data = sensors_aht21_aht21_jpg;
    len = sensors_aht21_aht21_jpg_len;
  } else if (uri == "/sensors/htu21/htu21.jpg") {
    data = sensors_htu21_htu21_jpg;
    len = sensors_htu21_htu21_jpg_len;
  } else if (uri == "/sensors/bmi160/bmi160.jpg") {
    data = sensors_bmi160_bmi160_jpg;
    len = sensors_bmi160_bmi160_jpg_len;
  } else {
    server.send(404, "text/plain", "Image not found");
    return;
  }

  server.send_P(200, "image/jpeg", (PGM_P)data, len);
}

void updateConfigModeDisplay() {
  applyDisplayOrientation();
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(12, 0);
  display.println("--- НАСТРОЙКИ ---");
  display.setCursor(0, 14);
  display.print("WiFi: ");
  display.print(AP_SSID);
  display.setCursor(0, 27);
  display.print("Password: ");
  display.print(AP_PASSWORD);
  display.setCursor(0, 40);
  display.print("IP: ");
  display.print(AP_IP_STR);
  // 6x13 ≈13 px высоты; y=56 обрезало низ — держим как нижнюю строку часов (~51)
  display.setCursor(0, 51);
  display.print("ROM: ");
  display.println(ROM_VERSION);
  display.display();
}

void startConfigMode() {
  logToDisplay(CODE_CONFIG_MODE);
  configMode = true;
  setCpuMaxPerformance();

  char detail[32];
  snprintf(detail, sizeof(detail), "%d MHz", getCpuFrequencyMhz());
  logToDisplay(CODE_CPU_FREQ, detail);

  const IPAddress apIp = AP_IP_ADDR;
  const IPAddress apGw = AP_IP_ADDR;
  const IPAddress apMask(255, 255, 255, 0);

  bool apStarted = false;
  for (uint8_t attempt = 0; attempt < 3 && !apStarted; ++attempt) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(120);
    WiFi.mode(WIFI_AP);
    WiFi.setTxPower(WIFI_POWER_15dBm);
    delay(80);
    // Force classic SoftAP address so OLED / clients always use 192.168.4.1
    WiFi.softAPConfig(apIp, apGw, apMask);
    apStarted = WiFi.softAP(AP_SSID, AP_PASSWORD);
    if (!apStarted) {
      delay(200);
    }
  }

  IPAddress reportedIp = WiFi.softAPIP();
  if (apStarted && (reportedIp == IPAddress((uint32_t)0) || reportedIp != apIp)) {
    uint32_t waitStart = millis();
    while ((millis() - waitStart) < 1500UL) {
      delay(50);
      reportedIp = WiFi.softAPIP();
      if (reportedIp == apIp) {
        break;
      }
    }
  }
  snprintf(detail, sizeof(detail), "%s %s", apStarted ? "OK" : "FAIL", AP_IP_STR);
  logToDisplay(CODE_CONFIG_AP_START, detail);
  Serial.printf("AP start: %s, SSID=%s, IP=%s (softAPIP=%s)\n",
                apStarted ? "OK" : "FAIL",
                AP_SSID,
                AP_IP_STR,
                reportedIp.toString().c_str());

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/sensors/sht31/sht31.jpg", HTTP_GET, handleSensorImage);
  server.on("/sensors/aht20bmp280/aht20bmp280.jpg", HTTP_GET, handleSensorImage);
  server.on("/sensors/aht21/aht21.jpg", HTTP_GET, handleSensorImage);
  server.on("/sensors/htu21/htu21.jpg", HTTP_GET, handleSensorImage);
  server.on("/sensors/bmi160/bmi160.jpg", HTTP_GET, handleSensorImage);
  server.on("/settings/export", HTTP_GET, handleSettingsExport);
  server.on("/settings/export", HTTP_POST, handleSettingsExport);
  server.on("/settings/import", HTTP_POST, handleSettingsImport);
  server.on("/reset", HTTP_POST, handleReset);
  server.on("/ota", HTTP_POST, handleOtaDone, handleOtaUpload);
  server.begin();

  updateConfigModeDisplay();
}

#endif
