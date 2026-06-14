#include <Arduino.h>
#include <lvgl.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "config_portal.h"
#include "config_manager.h"
#include "firmware_update.h"
#include "firmware_version.h"
#include "globals.h"

#ifndef FW_BOOT_DEBUG
#define FW_BOOT_DEBUG 0
#endif

#if FW_BOOT_DEBUG
#define BOOTLOG(...) Serial.printf(__VA_ARGS__)
#else
#define BOOTLOG(...) do { } while (0)
#endif

namespace {
lv_obj_t *bootOverlay = nullptr;
lv_obj_t *bootStepWifi = nullptr;
lv_obj_t *bootStepApi = nullptr;
lv_obj_t *bootStepDash = nullptr;
bool bootCanDismiss = false;
uint32_t bootReadySinceMs = 0;
uint32_t bootShownSinceMs = 0;
bool bootSequenceEnabled = false;
bool bootHasApiKey = false;
bool bootApiChecked = false;
bool bootApiReachableState = false;
uint32_t bootLastApiCheckMs = 0;
constexpr uint32_t kBootMinVisibleMs = 3600;
constexpr uint32_t kBootApiRetryMs = 4000;
String gCustomMenuHtml;
String gCustomStatusHtml;

enum class BootStepState : uint8_t {
  Pending,
  Ok,
  Fail,
  Skip,
};
}

bool isConfigured() {
  return ConfigManager::getInstance().isConfigured();
}

namespace {

void setStepText(lv_obj_t *label, const char *name, BootStepState state, const char *extra = nullptr) {
  if (!label) {
    return;
  }

  const char *symbol = LV_SYMBOL_MINUS;
  lv_color_t color = lv_color_hex(0x8E9BAC);
  switch (state) {
    case BootStepState::Pending:
      symbol = LV_SYMBOL_REFRESH;
      color = lv_color_hex(0x8E9BAC);
      break;
    case BootStepState::Ok:
      symbol = LV_SYMBOL_OK;
      color = lv_color_hex(0x83F7AF);
      break;
    case BootStepState::Fail:
      symbol = LV_SYMBOL_CLOSE;
      color = lv_color_hex(0xFF8B8B);
      break;
    case BootStepState::Skip:
      symbol = LV_SYMBOL_WARNING;
      color = lv_color_hex(0xF5B942);
      break;
  }

  if (extra && strlen(extra) > 0) {
    lv_label_set_text_fmt(label, "%s  %s: %s", symbol, name, extra);
  } else {
    lv_label_set_text_fmt(label, "%s  %s", symbol, name);
  }
  lv_obj_set_style_text_color(label, color, 0);
}

bool tryApiCheckHttp(const String &url) {
  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, url)) {
    return false;
  }
  http.setTimeout(1500);
  http.addHeader("X-API-Key", apiKey);
  http.addHeader("Accept", "application/json");
  int code = http.GET();
  http.end();
  return code == HTTP_CODE_OK;
}

bool tryApiCheckHttps(const String &url) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(6);
  HTTPClient http;
  if (!http.begin(client, url)) {
    return false;
  }
  http.setTimeout(1800);
  http.addHeader("X-API-Key", apiKey);
  http.addHeader("Accept", "application/json");
  int code = http.GET();
  http.end();
  return code == HTTP_CODE_OK;
}

bool isApiReachable() {
  if (WiFi.status() != WL_CONNECTED || strlen(apiKey) == 0) {
    return false;
  }

  String host = String(pfSenseHost);
  String path = "/api/v2/status/gateways";
  if (tryApiCheckHttps(String("https://") + host + path)) {
    return true;
  }
  if (tryApiCheckHttp(String("http://") + host + path)) {
    return true;
  }
  return false;
}

String sanitizeHost(const char *raw) {
  String host = raw ? String(raw) : String();
  host.trim();

  if (host.startsWith("http://")) {
    host.remove(0, 7);
  } else if (host.startsWith("https://")) {
    host.remove(0, 8);
  }

  int slash = host.indexOf('/');
  if (slash >= 0) {
    host = host.substring(0, slash);
  }

  host.trim();
  return host;
}

String sanitizeApiKey(const char *raw) {
  String key = raw ? String(raw) : String();
  key.trim();
  return key;
}

String sanitizeMenuPassword(const char *raw) {
  String pass = raw ? String(raw) : String();
  pass.trim();
  return pass;
}

String buildCustomStatusHtml(bool firstRun) {
  if (firstRun) {
    String html = "<div class='msg'><strong>Firewall Host:</strong> not configured</div>";
    html += "<div class='msg'><strong>Firmware:</strong> ";
    html += String(kFirmwareVersion);
    html += "</div>";
    return html;
  }

  String fwColor = bootApiReachableState ? "#5cb85c" : "#FF8B8B";
  String html = "<div class='msg' style='border-left-color:";
  html += fwColor;
  html += "'><strong>Firewall Host:</strong> ";
  html += String(pfSenseHost);
  html += "</div>";
  html += "<div class='msg'><strong>Firmware:</strong> ";
  html += String(kFirmwareVersion);
  html += "</div>";
  return html;
}

String buildCustomMenuHtml(bool firstRun) {
  if (firstRun) {
    return String(
      "<div class='msg'><strong>First Run</strong><br/>Open configuration.</div>"
      "<form action='/wifi' method='get'><button>Configure FW Settings</button></form>");
  }

  String html;
  html += "<form style='margin:0 0 10px 0' action='/factory-erase' method='post' onsubmit=\"return confirm('Erase all saved config and reboot?');\">";
  html += "<button style='background:#b00020;color:#fff'>Config Erase</button></form>";
  html += "<form style='margin:0 0 10px 0' action='/firmware-update' method='get'><button>Firmware Update</button></form>";
  html += "<form style='margin:0 0 12px 0' action='/logout' method='get'><button>Logout</button></form>";
  return html;
}

String escapeHtml(String text) {
  text.replace("&", "&amp;");
  text.replace("<", "&lt;");
  text.replace(">", "&gt;");
  text.replace("\"", "&quot;");
  text.replace("'", "&#39;");
  return text;
}

String formatReleaseBody(String body) {
  body.replace("\r", "");
  body.replace("\n", "<br/>");
  if (body.length() > 900) {
    body = body.substring(0, 900) + "<br/>...";
  }
  return body;
}

String buildFirmwareUpdatePage(const FirmwareReleaseInfo &info, const String &message = String(), bool success = false) {
  String html;
  html.reserve(7000);
  html += "<!DOCTYPE html><html lang='en'><head>";
  html += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1,user-scalable=no'/>";
  html += "<title>Firmware Update</title>";
  html += "<style>";
  html += ".c,body{text-align:center;font-family:verdana}div,input,select{padding:5px;font-size:1em;margin:5px 0;box-sizing:border-box}";
  html += "input,button,select,.msg{border-radius:.3rem;width:100%}button{cursor:pointer;border:0;background-color:#1fa3ec;color:#fff;line-height:2.4rem;font-size:1.2rem;width:100%}";
  html += "button.D{background-color:#dc3630}.wrap{text-align:left;display:inline-block;min-width:260px;max-width:500px}";
  html += "a{color:#000;font-weight:700;text-decoration:none}a:hover{color:#1fa3ec;text-decoration:underline}";
  html += ".msg{padding:20px;margin:20px 0;border:1px solid #eee;border-left-width:5px;border-left-color:#777}";
  html += ".msg.S{border-left-color:#5cb85c}.msg.D{border-left-color:#dc3630}.msg.P{border-left-color:#1fa3ec}";
  html += "dt{font-weight:bold}dd{margin:0;padding:0 0 .5em 0;min-height:12px}";
  html += "</style></head><body><div class='wrap'>";
  html += "<h1>pfSense Firewall Status</h1><h3>Firmware Update</h3>";

  if (message.length() > 0) {
    html += "<div class='msg ";
    html += success ? "S'" : "D'";
    html += "><strong>";
    html += success ? "Status" : "Notice";
    html += "</strong><br/>";
    html += escapeHtml(message);
    html += "</div>";
  }

  html += "<div class='msg P'><strong>Current firmware:</strong> ";
  html += escapeHtml(kFirmwareVersion);
  html += "</div>";

  html += "<h3>Release Information</h3><hr><dl>";
  html += "<dt>Current</dt><dd>";
  html += escapeHtml(info.currentVersion);
  html += "</dd>";
  html += "<dt>Latest</dt><dd>";
  html += escapeHtml(info.latestVersion.length() ? info.latestVersion : String("unknown"));
  html += "</dd>";
  html += "<dt>Release</dt><dd>";
  html += escapeHtml(info.releaseName.length() ? info.releaseName : String("-"));
  html += "</dd>";
  html += "<dt>Asset</dt><dd>";
  html += escapeHtml(info.assetName.length() ? info.assetName : String("-"));
  html += "</dd>";
  html += "<dt>Published</dt><dd>";
  html += escapeHtml(info.publishedAt.length() ? info.publishedAt : String("-"));
  html += "</dd></dl>";

  html += "<div class='msg ";
  html += info.updateAvailable ? "P'" : "S'";
  html += "><strong>";
  html += info.updateAvailable ? "Update available" : "Already on the latest GitHub release";
  html += "</strong></div>";

  if (info.releaseBody.length() > 0) {
    html += "<div class='msg'><strong>Release Notes</strong><br/>";
    html += formatReleaseBody(escapeHtml(info.releaseBody));
    html += "</div>";
  }

  html += "<form action='";
  html += kFirmwareGitHubReleasesUrl;
  html += "' method='get' target='_blank'><button type='submit'>Open GitHub Releases</button></form>";

  if (WiFi.status() == WL_CONNECTED && info.assetUrl.length() > 0) {
    html += "<form action='/firmware-update/install' method='post' onsubmit=\"return confirm('Download and install this release now?');\"><button class='D' type='submit'>Download & Flash Latest Release</button></form>";
  } else if (WiFi.status() != WL_CONNECTED) {
    html += "<div class='msg D'><strong>WiFi is not connected.</strong><br/>Firmware updates need network access.</div>";
  }

  html += "<hr><br/><form action='/' method='get'><button type='submit'>Back to Menu</button></form>";
  html += "</div></body></html>";
  return html;
}

void applyPortalCustomHtml(bool firstRun) {
  gCustomMenuHtml = buildCustomMenuHtml(firstRun);
  gCustomStatusHtml = buildCustomStatusHtml(firstRun);
  wm.setCustomMenuHTML(gCustomMenuHtml.c_str());
  wm.setCustomStatusHTML(gCustomStatusHtml.c_str());
}

void startProtectedConfigPortal() {
  WiFi.softAPdisconnect(true);
  wm.startConfigPortal(kApName, kApPassword);
}

void clearBootOverlay() {
  if (bootOverlay) {
    lv_obj_del(bootOverlay);
    bootOverlay = nullptr;
  }
  bootStepWifi = nullptr;
  bootStepApi = nullptr;
  bootStepDash = nullptr;
  bootCanDismiss = false;
  bootReadySinceMs = 0;
  bootShownSinceMs = 0;
  bootApiChecked = false;
  bootApiReachableState = false;
  bootLastApiCheckMs = 0;
}
}  // namespace

void saveConfigCallback() {
  shouldSaveConfig = true;
}

void saveParamsCallback() {
  // Persist immediately and force apply path in loop() so new API keys take effect.
  persistFirewallConfig();
  shouldSaveConfig = true;
}

void persistFirewallConfig() {
  if (!pfsenseIpParam || !apiKeyParam) {
    return;
  }

  String host = sanitizeHost(pfsenseIpParam->getValue());
  String key = sanitizeApiKey(apiKeyParam->getValue());

  if (host.length() == 0) {
    host = String(pfSenseHost);
  }

  strlcpy(pfSenseHost, host.c_str(), sizeof(pfSenseHost));
  strlcpy(apiKey, key.c_str(), sizeof(apiKey));
  
  // Save to ConfigManager
  ConfigManager& cfg = ConfigManager::getInstance();
  cfg.setPfsenseHost(pfSenseHost);
  cfg.setApiKey(apiKey);
  if (menuPasswordParam) {
    cfg.setWebMenuPassword(sanitizeMenuPassword(menuPasswordParam->getValue()).c_str());
  }
  cfg.saveConfig();
  
  // Recalculate boot sequence after saving config
  bootSequenceEnabled = cfg.isConfigured();
  bootHasApiKey = bootSequenceEnabled;
  if (bootOverlay) {
    drawBootScreen();
  }
}

void drawBootScreen() {
  clearBootOverlay();

  bootOverlay = lv_obj_create(lv_scr_act());
  bootShownSinceMs = millis();
  lv_obj_remove_style_all(bootOverlay);
  lv_obj_set_size(bootOverlay, DASHBOARD_WIDTH, DASHBOARD_HEIGHT);
  lv_obj_align(bootOverlay, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(bootOverlay, lv_color_hex(0x101318), 0);
  lv_obj_set_style_bg_grad_color(bootOverlay, lv_color_hex(0x1B2330), 0);
  lv_obj_set_style_bg_grad_dir(bootOverlay, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(bootOverlay, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(bootOverlay, 0, 0);

  lv_obj_t *icon = lv_label_create(bootOverlay);
  lv_label_set_text(icon, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(icon, lv_color_hex(0x83F7AF), 0);
  lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 14, 10);

  lv_obj_t *title = lv_label_create(bootOverlay);
  lv_label_set_text(title, "pfSense Firewall Status");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xDDE7F2), 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 48, 12);

  if (bootSequenceEnabled) {
    lv_obj_t *status = lv_label_create(bootOverlay);
    lv_label_set_text(status, "Boot sequence");
    lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(0x8E9BAC), 0);
    lv_obj_align(status, LV_ALIGN_TOP_LEFT, 14, 36);

    bootStepWifi = lv_label_create(bootOverlay);
    lv_obj_set_style_text_font(bootStepWifi, &lv_font_montserrat_14, 0);
    lv_obj_align(bootStepWifi, LV_ALIGN_TOP_LEFT, 14, 60);
    setStepText(bootStepWifi, "WiFi", BootStepState::Pending, "connecting");

    bootStepApi = lv_label_create(bootOverlay);
    lv_obj_set_style_text_font(bootStepApi, &lv_font_montserrat_14, 0);
    lv_obj_align(bootStepApi, LV_ALIGN_TOP_LEFT, 14, 84);
    setStepText(bootStepApi, "API", BootStepState::Pending, "waiting");

    bootStepDash = lv_label_create(bootOverlay);
    lv_obj_set_style_text_font(bootStepDash, &lv_font_montserrat_14, 0);
    lv_obj_align(bootStepDash, LV_ALIGN_TOP_LEFT, 14, 110);
    setStepText(bootStepDash, "Dashboard", BootStepState::Pending, "initializing");
  } else {
    lv_obj_t *status = lv_label_create(bootOverlay);
    lv_label_set_text(status, "First start / portal mode");
    lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(0xF5B942), 0);
    lv_obj_align(status, LV_ALIGN_TOP_LEFT, 14, 36);

    lv_obj_t *portalState = lv_label_create(bootOverlay);
    lv_label_set_text(portalState, "No saved host/API config yet");
    lv_obj_set_style_text_font(portalState, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(portalState, lv_color_hex(0x8E9BAC), 0);
    lv_obj_align(portalState, LV_ALIGN_TOP_LEFT, 14, 60);

    lv_obj_t *hint = lv_label_create(bootOverlay);
    lv_label_set_text(hint, "Open portal for setup:");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xAAB6C4), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 14, 126);

    lv_obj_t *ap = lv_label_create(bootOverlay);
    lv_label_set_text_fmt(ap, "AP: %s", kApName);
    lv_obj_set_style_text_font(ap, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ap, lv_color_hex(0x83F7AF), 0);
    lv_obj_align(ap, LV_ALIGN_BOTTOM_LEFT, 14, -20);

    lv_obj_t *portal = lv_label_create(bootOverlay);
    lv_label_set_text(portal, "Portal: 192.168.4.1");
    lv_obj_set_style_text_font(portal, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(portal, lv_color_hex(0xF5B942), 0);
    lv_obj_align(portal, LV_ALIGN_BOTTOM_RIGHT, -14, -20);
  }

  lv_timer_handler();
}

void dismissBootScreenIfConnected() {
  if (!bootOverlay) {
    return;
  }
  if (!bootSequenceEnabled) {
    return;
  }

  bool wifiOk = (WiFi.status() == WL_CONNECTED);
  if (wifiOk) {
    setStepText(bootStepWifi, "WiFi", BootStepState::Ok, WiFi.localIP().toString().c_str());
  } else if (bootHasApiKey) {
    setStepText(bootStepWifi, "WiFi", BootStepState::Pending, "reconnecting");
  }

  if (bootHasApiKey && wifiOk && (!bootApiChecked || (!bootApiReachableState && (millis() - bootLastApiCheckMs) >= kBootApiRetryMs))) {
    setStepText(bootStepApi, "API", BootStepState::Pending, "checking host");
    bootApiReachableState = isApiReachable();
    bootApiChecked = true;
    bootLastApiCheckMs = millis();
    if (bootApiReachableState) {
      setStepText(bootStepApi, "API", BootStepState::Ok, "connected");
    } else {
      setStepText(bootStepApi, "API", BootStepState::Fail, "unreachable");
    }
    applyPortalCustomHtml(false);
  }

  if (!bootCanDismiss) {
    return;
  }
  if (!wifiOk) {
    return;
  }
  if (bootHasApiKey && !bootApiChecked) {
    return;
  }
  if (millis() - bootShownSinceMs < kBootMinVisibleMs) {
    return;
  }
  if (millis() - bootReadySinceMs < 700) {
    return;
  }
  clearBootOverlay();
}

void markBootDashboardReady() {
  if (!bootOverlay || bootCanDismiss) {
    return;
  }
  if (!bootSequenceEnabled) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    setStepText(bootStepDash, "Dashboard", BootStepState::Pending, "wait wifi");
    return;
  }
  if (bootHasApiKey && !bootApiChecked) {
    setStepText(bootStepDash, "Dashboard", BootStepState::Pending, "wait api");
    return;
  }
  setStepText(bootStepDash, "Dashboard", BootStepState::Ok, "ready");
  bootCanDismiss = true;
  bootReadySinceMs = millis();
}

void setupPortalRoutes() {
  if (!wm.server) {
    return;
  }

  auto eraseAllAndReboot = []() {
    BOOTLOG("[BOOT] Factory erase requested\n");
    wm.server->send(200, "text/html", "<html><head><meta charset='utf-8'></head><body><h3>Config erased. Rebooting...</h3></body></html>");
    wm.server->client().stop();
    delay(600);
    ConfigManager::getInstance().clearConfig();
    // Clear WiFiManager/SDK STA credentials after app config keys are wiped.
    wm.resetSettings();
    delay(500);
    ESP.restart();
  };

  wm.server->on("/factory-erase", HTTP_POST, eraseAllAndReboot);

  wm.server->on("/firmware-update", HTTP_GET, []() {
    FirmwareReleaseInfo info;
    String errorMessage;
    bool ok = fetchLatestFirmwareRelease(info, errorMessage);
    if (!ok) {
      info.currentVersion = kFirmwareVersion;
      info.latestVersion = "unavailable";
        info.releaseName = "GitHub release unavailable";
        info.releaseBody.clear();
        info.releaseUrl = kFirmwareGitHubReleasesUrl;
    }

    wm.server->send(200, "text/html", buildFirmwareUpdatePage(info, ok ? String() : errorMessage, ok));
  });

  wm.server->on("/firmware-update/install", HTTP_POST, []() {
    FirmwareReleaseInfo info;
    String errorMessage;
    if (!fetchLatestFirmwareRelease(info, errorMessage)) {
      info.currentVersion = kFirmwareVersion;
      info.latestVersion = "unavailable";
        info.releaseName = "GitHub release unavailable";
        info.releaseBody.clear();
        info.releaseUrl = kFirmwareGitHubReleasesUrl;
      wm.server->send(200, "text/html", buildFirmwareUpdatePage(info, errorMessage, false));
      return;
    }

    if (!flashFirmwareAsset(info, errorMessage)) {
      wm.server->send(200, "text/html", buildFirmwareUpdatePage(info, errorMessage, false));
      return;
    }

    wm.server->send(200, "text/html", "<html><head><meta charset='utf-8'></head><body><h3>Firmware updated successfully. Rebooting now...</h3></body></html>");
    wm.server->client().stop();
    delay(1000);
    ESP.restart();
  });
}

void handleConfigSavedTransition() {
  // Persist custom params, then restart so WiFiManager reconnects using stored WLAN credentials.
  persistFirewallConfig();
  delay(250);
  ESP.restart();
}

void configureWiFi() {
  const char *firstRunMenu[] = {"custom"};
  const char *fullMenu[] = {"wifi", "param", "info", "custom", "restart", "sep"};

  wm.setSaveConfigCallback(saveConfigCallback);
  wm.setSaveParamsCallback(saveParamsCallback);
  wm.setConfigPortalBlocking(false);
  wm.setTitle("pfSense Firewall Status");
  wm.setShowBack(true);
  // Keep Info page clean: only informational content, no destructive quick actions.
  wm.setShowInfoErase(false);
  wm.setShowInfoUpdate(false);

  // Load current values from ConfigManager
  const DeviceConfig& cfg = ConfigManager::getInstance().getConfig();

  if (!pfsenseIpParam) {
    pfsenseIpParam = new WiFiManagerParameter("pfsense_ip", "Firewall Host / IP", cfg.pfsense_host, sizeof(cfg.pfsense_host));
    wm.addParameter(pfsenseIpParam);
  }

  if (!apiKeyParam) {
    apiKeyParam = new WiFiManagerParameter("api_key", "API Key", cfg.api_key, sizeof(cfg.api_key));
    wm.addParameter(apiKeyParam);
  }

  if (!menuPasswordParam) {
    menuPasswordParam = new WiFiManagerParameter("web_menu_password", "Menu Password (min 8 chars)", cfg.web_menu_password, sizeof(cfg.web_menu_password));
    wm.addParameter(menuPasswordParam);
  }

  bootSequenceEnabled = isConfigured();
  BOOTLOG("[BOOT] isConfigured=%d host='%s' api_len=%u\n", bootSequenceEnabled ? 1 : 0, cfg.pfsense_host, (unsigned)strlen(cfg.api_key));
  wm.setParamsPage(false);
  wm.setMenu(bootSequenceEnabled ? fullMenu : firstRunMenu, bootSequenceEnabled ? 6 : 1);
  applyPortalCustomHtml(!bootSequenceEnabled);
  if (bootSequenceEnabled && strlen(cfg.web_menu_password) >= 8) {
    wm.setHttpAuth("admin", cfg.web_menu_password);
  } else {
    wm.setHttpAuth("", "");
  }
  drawBootScreen();
  bootHasApiKey = bootSequenceEnabled;

  if (!bootSequenceEnabled) {
    BOOTLOG("[BOOT] First-run path, starting AP '%s' (pass len=%u)\n", kApName, (unsigned)strlen(kApPassword));
    setStepText(bootStepWifi, "WiFi", BootStepState::Skip, "AP mode");
    setStepText(bootStepApi, "API", BootStepState::Skip, "no API key");
    bootApiChecked = true;
    bootApiReachableState = false;
    startProtectedConfigPortal();
    return;
  }

  // In configured mode, first try to reconnect to previously stored STA credentials.
  WiFi.begin();
  uint32_t wifiWaitStart = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - wifiWaitStart) < 8000) {
    delay(50);
    lv_timer_handler();
  }
  if (WiFi.status() != WL_CONNECTED) {
    BOOTLOG("[BOOT] WiFi connect failed, fallback to AP '%s'\n", kApName);
    setStepText(bootStepWifi, "WiFi", BootStepState::Fail, "connect failed");
    setStepText(bootStepApi, "API", BootStepState::Skip, "no network");
    startProtectedConfigPortal();
    return;
  }
  setStepText(bootStepWifi, "WiFi", BootStepState::Ok, WiFi.localIP().toString().c_str());

  setStepText(bootStepApi, "API", BootStepState::Pending, "checking host");
  bool apiReachable = isApiReachable();
  bootApiChecked = true;
  bootApiReachableState = apiReachable;
  bootLastApiCheckMs = millis();
  if (apiReachable) {
    setStepText(bootStepApi, "API", BootStepState::Ok, "connected");
  } else {
    setStepText(bootStepApi, "API", BootStepState::Fail, "unreachable");
  }
  applyPortalCustomHtml(false);

  if (shouldSaveConfig) {
    persistFirewallConfig();
  }
}
