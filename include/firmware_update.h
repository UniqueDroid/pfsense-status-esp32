#pragma once

#include <Arduino.h>

struct FirmwareReleaseInfo {
  String currentVersion;
  String latestVersion;
  String releaseName;
  String releaseBody;
  String releaseUrl;
  String assetUrl;
  String assetName;
  String publishedAt;
  size_t assetSize = 0;
  bool updateAvailable = false;
};

bool fetchLatestFirmwareRelease(FirmwareReleaseInfo &info, String &errorMessage);
bool flashFirmwareAsset(const FirmwareReleaseInfo &info, String &errorMessage);