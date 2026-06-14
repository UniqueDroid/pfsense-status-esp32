#include "firmware_update.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>

#include "firmware_version.h"

namespace {
String normalizeVersion(String value) {
  value.trim();
  value.toLowerCase();
  if (value.startsWith("v")) {
    value.remove(0, 1);
  }
  return value;
}

bool downloadJsonDocument(JsonDocument &doc, const String &url, String &errorMessage) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(20);

  HTTPClient http;
  if (!http.begin(client, url)) {
    errorMessage = "Unable to open GitHub release API";
    return false;
  }

  http.setTimeout(8000);
  http.addHeader("User-Agent", "pfSense-status-esp32");
  http.addHeader("Accept", "application/vnd.github+json");

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    if (code == HTTP_CODE_NOT_FOUND) {
      errorMessage = "No GitHub release found yet. Create a tagged release first.";
    } else {
      errorMessage = "GitHub API returned HTTP " + String(code);
    }
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    errorMessage = "Failed to parse GitHub release JSON";
    return false;
  }

  return true;
}

String pickAssetUrl(const JsonArray &assets, String &assetName, size_t &assetSize) {
  String fallbackUrl;
  String fallbackName;
  size_t fallbackSize = 0;

  for (JsonObjectConst asset : assets) {
    String name = asset["name"] | "";
    String url = asset["browser_download_url"] | "";
    size_t size = asset["size"] | 0;
    if (url.length() == 0) {
      continue;
    }

    if (fallbackUrl.length() == 0) {
      fallbackUrl = url;
      fallbackName = name;
      fallbackSize = size;
    }

    String lowerName = name;
    lowerName.toLowerCase();
    if (lowerName.endsWith(".bin")) {
      assetName = name;
      assetSize = size;
      return url;
    }
  }

  assetName = fallbackName;
  assetSize = fallbackSize;
  return fallbackUrl;
}
}  // namespace

bool fetchLatestFirmwareRelease(FirmwareReleaseInfo &info, String &errorMessage) {
  info.currentVersion = kFirmwareVersion;

  JsonDocument doc;
  if (!downloadJsonDocument(doc, kFirmwareGitHubReleaseApi, errorMessage)) {
    info.latestVersion = "unavailable";
    info.releaseName = "GitHub release unavailable";
    info.releaseUrl = kFirmwareGitHubReleasesUrl;
    return false;
  }

  info.latestVersion = doc["tag_name"] | "";
  info.releaseName = doc["name"] | info.latestVersion;
  info.releaseBody = doc["body"] | "";
  info.releaseUrl = doc["html_url"] | kFirmwareGitHubReleasesUrl;
  info.publishedAt = doc["published_at"] | "";

  JsonArray assets = doc["assets"].as<JsonArray>();
  info.assetUrl = pickAssetUrl(assets, info.assetName, info.assetSize);
  info.updateAvailable = normalizeVersion(info.currentVersion) != normalizeVersion(info.latestVersion);

  if (info.assetUrl.length() == 0) {
    errorMessage = "No .bin release asset found";
    return false;
  }

  return true;
}

bool flashFirmwareAsset(const FirmwareReleaseInfo &info, String &errorMessage) {
  if (info.assetUrl.length() == 0) {
    errorMessage = "No firmware download URL available";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(30);

  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  if (!http.begin(client, info.assetUrl)) {
    errorMessage = "Unable to open firmware download URL";
    return false;
  }

  http.setTimeout(15000);
  http.addHeader("User-Agent", "pfSense-status-esp32");
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    errorMessage = "Firmware download failed with HTTP " + String(code);
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  if (!Update.begin(contentLength > 0 ? contentLength : UPDATE_SIZE_UNKNOWN)) {
    errorMessage = "Unable to start OTA update";
    http.end();
    return false;
  }

  size_t written = Update.writeStream(*http.getStreamPtr());
  bool finished = Update.end(true);
  http.end();

  if (!finished) {
    errorMessage = "OTA update failed: " + String(Update.errorString());
    return false;
  }

  if (written == 0) {
    errorMessage = "OTA update wrote no data";
    return false;
  }

  return true;
}