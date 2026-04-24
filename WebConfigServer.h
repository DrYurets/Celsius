#ifndef WEB_CONFIG_SERVER_H
#define WEB_CONFIG_SERVER_H

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

String getConfigPage() {
  uint8_t weekdaysMask = settings.activeWeekdaysMask & WEEKDAY_MASK_ALL;
  if (weekdaysMask == 0) {
    weekdaysMask = WEEKDAY_MASK_ALL;
  }

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Celsius Clock Setup</title>";
  html += "<style>";
  html += "body { font-family: Arial; max-width: 500px; margin: 0 auto; padding: 20px; background: #1a1a1a; color: #fff; }";
  html += "h1, h2 { color: #4CAF50; }";
  html += "input, select { width: 100%; padding: 10px; margin: 10px 0; border: 1px solid #555; border-radius: 5px; background: #333; color: #fff; box-sizing: border-box; }";
  html += "input[type='checkbox'] { width: auto; margin-right: 10px; }";
  html += ".checkbox-label { display: flex; align-items: center; margin: 10px 0; }";
  html += ".time-group { display: flex; gap: 10px; }";
  html += ".time-group input { width: 50%; }";
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

  html += "<h2>NTP Sync</h2>";
  html += "<p style='font-size: 12px; color: #aaa; margin-top: 0; margin-bottom: 10px;'>Time is synced automatically with every weather update.</p>";

  html += "<h2>Time Correction</h2>";
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
  html += "<label>Update interval (hours):</label>";
  html += "<input type='number' name='weatherUpdateHours' min='1' max='24' value='" + String(settings.weatherUpdateHours) + "' required style='margin-bottom: 10px;'>";
  html += "<label>Weather screen timeout (sec):</label>";
  html += "<input type='number' name='weatherScreenSeconds' min='1' max='60' value='" + String(settings.weatherScreenSeconds) + "' required style='margin-bottom: 10px;'>";
  html += "<p style='font-size: 12px; color: #aaa; margin-top: -5px; margin-bottom: 10px;'>How often to fetch weather data (1-24 hours). Updates only when display is on.</p>";
  html += "</div>";
  html += "<h2>Auto OTA</h2>";
  html += "<div class='checkbox-label'><input type='checkbox' name='autoOtaEnabled' " + String(settings.autoOtaEnabled ? "checked" : "") + "><label>Enable automatic firmware updates</label></div>";
  html += "<label>Check interval (hours):</label>";
  html += "<input type='number' name='autoOtaCheckHours' min='1' max='168' value='" + String(settings.autoOtaCheckHours) + "' required style='margin-bottom: 10px;'>";
  html += "<p style='font-size: 12px; color: #aaa; margin-top: -5px; margin-bottom: 10px;'>Checks run only in normal daytime cycle with valid time and sufficient battery.</p>";

  html += "<button type='submit' style='margin-top: 20px;'>Save and Reset</button>";
  html += "</form>";
  html += "<hr style='margin: 20px 0; border-color: #555;'>";
  html += "<form method='POST' action='/reset' style='margin-top: 20px;'>";
  html += "<button type='submit' style='background: #f44336;'>Reset Settings</button>";
  html += "</form>";

  html += "<hr style='margin: 20px 0; border-color: #555;'>";
  html += "<h2>Firmware OTA</h2>";
  html += "<p style='font-size: 12px; color: #ffb74d; margin-top: 0;'>Do not power off during update. Use only the .bin firmware file built for this board.</p>";
  html += "<form method='POST' action='/ota' enctype='multipart/form-data'>";
  html += "<label>Confirm AP password:</label>";
  html += "<input type='password' name='otaPassword' placeholder='AP password' required>";
  html += "<label>Firmware file (.bin):</label>";
  html += "<input type='file' name='firmware' accept='.bin,application/octet-stream' required>";
  html += "<button type='submit' style='background: #ff9800; margin-top: 10px;'>Update Firmware (OTA)</button>";
  html += "</form>";
  html += otaStatusHtml();

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

    if (!server.hasArg("otaPassword") || server.arg("otaPassword") != AP_PASSWORD) {
      otaUpdateError = "Authentication failed";
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

void updateConfigModeDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(12, 0);
  display.println("--- НАСТРОЙКИ ---");
  display.setCursor(0, 16);
  display.print("WiFi: ");
  display.print(AP_SSID);
  display.setCursor(0, 28);
  display.print("Password: ");
  display.print(AP_PASSWORD);
  display.setCursor(0, 40);
  display.print("IP: ");
  display.print(WiFi.softAPIP());
  display.setCursor(0, 56);
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

  bool apStarted = false;
  for (uint8_t attempt = 0; attempt < 3 && !apStarted; ++attempt) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(120);
    WiFi.mode(WIFI_AP);
    WiFi.setTxPower(WIFI_POWER_15dBm);
    delay(80);
    apStarted = WiFi.softAP(AP_SSID, AP_PASSWORD);
    if (!apStarted) {
      delay(200);
    }
  }

  IPAddress apIp = WiFi.softAPIP();
  if (apStarted && apIp == IPAddress((uint32_t)0)) {
    uint32_t waitStart = millis();
    while ((millis() - waitStart) < 1500UL) {
      delay(50);
      apIp = WiFi.softAPIP();
      if (apIp != IPAddress((uint32_t)0)) {
        break;
      }
    }
  }
  snprintf(detail, sizeof(detail), "%s %s", apStarted ? "OK" : "FAIL", apIp.toString().c_str());
  logToDisplay(CODE_CONFIG_AP_START, detail);
  Serial.printf("AP start: %s, SSID=%s, IP=%s\n",
                apStarted ? "OK" : "FAIL",
                AP_SSID,
                apIp.toString().c_str());

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/reset", HTTP_POST, handleReset);
  server.on("/ota", HTTP_POST, handleOtaDone, handleOtaUpload);
  server.begin();

  updateConfigModeDisplay();
}

#endif
