#include "config_portal.h"
#include "globals.h"

namespace {
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
  prefs.putString("pfsense_host", pfSenseHost);
  prefs.putString("api_key", apiKey);
}

void drawBootScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.drawString("pfSense Firewall Status", tft.width() / 2, 16);
  tft.setTextSize(2);
  tft.drawString("Verbinde WLAN...", tft.width() / 2, 42);
  tft.drawString("Falls noetig:", tft.width() / 2, 64);
  tft.drawString("AP: FW-Status-AP", tft.width() / 2, 86);
  tft.drawString("Portal: 192.168.4.1", tft.width() / 2, 108);
  tft.setTextDatum(TL_DATUM);
}

void configureWiFi() {
  const char *kApName = "FW-Status-AP";
  const char *kApPassword = "FWStatus2026";

  wm.setSaveConfigCallback(saveConfigCallback);
  wm.setSaveParamsCallback(saveParamsCallback);
  wm.setConfigPortalBlocking(false);

  wm.setParamsPage(true);
  wm.setCustomMenuHTML("<form action='/param' method='get'><button>Configure Firewall Status</button></form><br/>\n");
  const char *menu[] = {"wifi", "custom", "info", "sep", "update"};
  wm.setMenu(menu, 5);

  if (!pfsenseIpParam) {
    pfsenseIpParam = new WiFiManagerParameter("pfsense_ip", "pfSense Host/IP", pfSenseHost, sizeof(pfSenseHost));
    wm.addParameter(pfsenseIpParam);
  }

  if (!apiKeyParam) {
    apiKeyParam = new WiFiManagerParameter("api_key", "pfSense API Key", apiKey, sizeof(apiKey));
    wm.addParameter(apiKeyParam);
  }

  drawBootScreen();

  bool hasApiKey = strlen(apiKey) > 0;

  if (!hasApiKey) {
    wm.startConfigPortal(kApName, kApPassword);
    return;
  } else {
    bool ok = wm.autoConnect(kApName, kApPassword);
    if (!ok) {
      wm.startConfigPortal(kApName, kApPassword);
      return;
    }
  }

  if (shouldSaveConfig) {
    persistFirewallConfig();
  }
}
