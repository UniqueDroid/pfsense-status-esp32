#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "boards/board_profile.h"
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiManager.h>

// Imports
#include "config_portal.h"
#include "pfsense_api.h"
#include "dashboard.h"

// Global constants
const char *kPrefsNs = "fwstatus";
const char *kApName = "FW-Status-AP";
const char *kApPassword = "FWStatus2026";
const uint32_t kPollMs = 4500;
const uint32_t kWifiRetryMs = 10000;
const uint32_t kButtonDebounceMs = 180;
const uint32_t kRenderMs = 250;

#ifndef DASHBOARD_WIDTH
#define DASHBOARD_WIDTH 320
#endif

#ifndef DASHBOARD_HEIGHT
#define DASHBOARD_HEIGHT 170
#endif

#ifndef DASHBOARD_ROTATION
#define DASHBOARD_ROTATION 1
#endif

// Global objects
Preferences prefs;
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite *sprite = nullptr;  // Initialized in setup() after tft.init()

// WiFi & Config
char pfSenseHost[64] = "192.168.1.1";
char apiKey[160] = "";
bool shouldSaveConfig = false;
WiFiManager wm;
WiFiManagerParameter *pfsenseIpParam = nullptr;
WiFiManagerParameter *apiKeyParam = nullptr;

// WAN Status
String wanName = "WAN";
String wanStatus = "INIT";
String wanDelay = "-";
String wanRttSd = "-";
String wanLoss = "-";
int kTrafficPoints = 46;
float wanRxHistory[46] = {0};
float wanTxHistory[46] = {0};
bool wanTrafficPrimed = false;
uint64_t wanPrevInBytes = 0;
uint64_t wanPrevOutBytes = 0;
uint32_t wanPrevSampleMs = 0;

// System Metrics
int cpuPercent = 0;
int memPercent = 0;
int tempPercent = 0;
String tempValue = "-";
String lastUpdate = "00:00:00";

// Polling
uint32_t lastPoll = 0;
uint8_t currentDashboardIndex = 0;
bool lastButtonPrevState = HIGH;
bool lastButtonNextState = HIGH;
uint32_t lastPrevButtonEventMs = 0;
uint32_t lastNextButtonEventMs = 0;
const uint8_t kBrightnessLevels[] = {40, 96, 160, 255};
const uint8_t kBrightnessLevelCount = sizeof(kBrightnessLevels) / sizeof(kBrightnessLevels[0]);
uint8_t brightnessLevelIndex = kBrightnessLevelCount - 1;

void applyBacklightLevel() {
#if defined(BOARD_PROFILE_LILYGO_T_DISPLAY_S3) && defined(TFT_BL)
  analogWrite(TFT_BL, kBrightnessLevels[brightnessLevelIndex]);
#elif defined(TFT_BL)
#ifdef TFT_BACKLIGHT_ON
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
#else
  digitalWrite(TFT_BL, HIGH);
#endif
#endif
}

bool adjustBrightness(int8_t delta) {
  int newIndex = (int)brightnessLevelIndex + delta;
  if (newIndex < 0) newIndex = 0;
  if (newIndex >= (int)kBrightnessLevelCount) newIndex = (int)kBrightnessLevelCount - 1;
  if (newIndex == (int)brightnessLevelIndex) {
    return false;
  }
  brightnessLevelIndex = (uint8_t)newIndex;
  applyBacklightLevel();
  return true;
}

bool cycleBrightnessLevel() {
  if (brightnessLevelIndex == 0) {
    brightnessLevelIndex = kBrightnessLevelCount - 1;
  } else {
    brightnessLevelIndex--;
  }
  applyBacklightLevel();
  return true;
}

void setupButtons() {
#if defined(BOARD_PROFILE_LILYGO_T_DISPLAY_S3)
  pinMode(0, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
  // LilyGO T-Display-S3: upper button is GPIO14, lower button is GPIO0.
  lastButtonPrevState = digitalRead(14);
  lastButtonNextState = digitalRead(0);
#endif
}

void drawActiveDashboard() {
  switch (currentDashboardIndex) {
    case 1:
      drawDashboardGraph();
      break;
    case 2:
      drawDashboardMetrics();
      break;
    default:
      drawDashboardMain();
      break;
  }
}

bool handleDashboardButtons() {
#if defined(BOARD_PROFILE_LILYGO_T_DISPLAY_S3)
  const uint32_t now = millis();
  bool prevState = digitalRead(14);
  bool nextState = digitalRead(0);
  bool changed = false;

  // Upper button (GPIO14): cycle dashboards on release.
  if (lastButtonPrevState == LOW && prevState == HIGH) {
    if ((now - lastPrevButtonEventMs) >= kButtonDebounceMs) {
      currentDashboardIndex = (currentDashboardIndex + 1) % 3;
      lastPrevButtonEventMs = now;
      changed = true;
    }
  }

  // Lower button (GPIO0): cycle brightness on press.
  if (lastButtonNextState == HIGH && nextState == LOW) {
    if ((now - lastNextButtonEventMs) >= kButtonDebounceMs) {
      changed |= cycleBrightnessLevel();
      lastNextButtonEventMs = now;
    }
  }

  lastButtonPrevState = prevState;
  lastButtonNextState = nextState;
  return changed;
#endif
  return false;
}

String formatUptime(uint32_t uptimeSeconds) {
  uint32_t hours = uptimeSeconds / 3600;
  uint32_t minutes = (uptimeSeconds % 3600) / 60;
  uint32_t seconds = uptimeSeconds % 60;
  char buffer[20];
  snprintf(buffer, sizeof(buffer), "%02lu:%02lu:%02lu",
           static_cast<unsigned long>(hours),
           static_cast<unsigned long>(minutes),
           static_cast<unsigned long>(seconds));
  return String(buffer);
}



void enableBacklight() {
#ifdef TFT_BL
#if defined(BOARD_PROFILE_LILYGO_T_DISPLAY_S3)
#if defined(LCD_POWER_ON)
  pinMode(LCD_POWER_ON, OUTPUT);
  digitalWrite(LCD_POWER_ON, HIGH);
#endif
  pinMode(TFT_BL, OUTPUT);
  applyBacklightLevel();
#else
  pinMode(TFT_BL, OUTPUT);
#ifdef TFT_BACKLIGHT_ON
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
#else
  digitalWrite(TFT_BL, HIGH);
#endif
#endif
#endif
}



void setup() {
  delay(200);

  prefs.begin(kPrefsNs, false);
  String savedHost = prefs.getString("pfsense_host", "192.168.1.1");
  String savedApiKey = prefs.getString("api_key", "");
  strlcpy(pfSenseHost, savedHost.c_str(), sizeof(pfSenseHost));
  strlcpy(apiKey, savedApiKey.c_str(), sizeof(apiKey));

  enableBacklight();

  tft.init();
  tft.setRotation(DASHBOARD_ROTATION);
  setupButtons();
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.drawString("pfSense Firewall Status", tft.width() / 2, tft.height() / 2 - 34);
  tft.drawString("Display OK", tft.width() / 2, tft.height() / 2 - 12);
  tft.setTextSize(2);
  tft.drawString("Booting...", tft.width() / 2, tft.height() / 2 + 10);
  tft.setTextDatum(TL_DATUM);
  delay(500);

  sprite = new TFT_eSprite(&tft);
  sprite->setColorDepth(16);
  sprite->createSprite(DASHBOARD_WIDTH, DASHBOARD_HEIGHT);

  configureWiFi();

  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  if (WiFi.status() == WL_CONNECTED) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("pfSense Firewall Status", tft.width() / 2, tft.height() / 2 - 26);
    tft.drawString("WLAN verbunden", tft.width() / 2, tft.height() / 2 - 6);
    tft.setTextSize(2);
    tft.drawString(WiFi.localIP().toString(), tft.width() / 2, tft.height() / 2 + 16);
  } else {
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("pfSense Firewall Status", tft.width() / 2, tft.height() / 2 - 30);
    tft.drawString("Config Portal", tft.width() / 2, tft.height() / 2 - 10);
    tft.setTextSize(1);
    tft.drawString("AP: FW-Status-AP", tft.width() / 2, tft.height() / 2 + 8);
    tft.drawString("192.168.4.1", tft.width() / 2, tft.height() / 2 + 20);
  }
  tft.setTextDatum(TL_DATUM);
  delay(1000);

  // Starte WiFiManager Portal im Hintergrund.
  wm.startWebPortal();
}

void loop() {
  static uint32_t lastWifiRetry = 0;
  static uint32_t lastRenderMs = 0;
  static bool wasWifiConnected = false;

  // Verarbeite WiFiManager Portal und Server-Anfragen
  wm.process();
  if (wm.server) {
    wm.server->handleClient();
  }

  if (shouldSaveConfig) {
    shouldSaveConfig = false;
    persistFirewallConfig();
    delay(500);
    ESP.restart();
  }

  // Reconnect WLAN automatisch im Loop, falls Verbindung verloren geht.
  uint32_t now = millis();
  bool needsRender = handleDashboardButtons();
  bool wifiConnected = (WiFi.status() == WL_CONNECTED);

  if (!wifiConnected) {
    if (now - lastWifiRetry >= kWifiRetryMs) {
      lastWifiRetry = now;
      WiFi.reconnect();
    }

    if (wanStatus != "offline") {
      wanStatus = "offline";
      needsRender = true;
    }
    if (wasWifiConnected) {
      needsRender = true;
    }

    if (needsRender || (now - lastRenderMs) >= kRenderMs) {
      drawActiveDashboard();
      lastRenderMs = now;
    }

    wasWifiConnected = false;
    delay(25);
    return;
  }

  if (!wasWifiConnected) {
    needsRender = true;
  }

  if (now - lastPoll >= kPollMs) {
    lastPoll = now;
    fetchGatewayStatus();
    fetchWanTraffic();
    fetchSystemStatus();
    lastUpdate = formatUptime(now / 1000);
    needsRender = true;
  }

  if (needsRender || (now - lastRenderMs) >= kRenderMs) {
    drawActiveDashboard();
    lastRenderMs = now;
  }

  wasWifiConnected = true;
  delay(25);
}
