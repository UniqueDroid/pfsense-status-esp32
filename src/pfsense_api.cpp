// pfSense REST API client for gateway/system metrics and traffic samples.
#include "pfsense_api.h"
#include "utils.h"
#include "globals.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

namespace {
int httpsGet(const String &url, String &payload) {
  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  secureClient.setHandshakeTimeout(20);

  HTTPClient http;
  if (!http.begin(secureClient, url)) {
    return -1;
  }

  http.addHeader("X-API-Key", apiKey);
  http.addHeader("Accept", "application/json");
  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    payload = http.getString();
  }
  http.end();
  return code;
}

int apiGet(const char *path, String &payload) {
  const String host = String(pfSenseHost);
  const String httpsUrl = String("https://") + host + path;
  const String httpUrl = String("http://") + host + path;

  // First try HTTPS (preferred for security).
  int httpsCode = httpsGet(httpsUrl, payload);
  if (httpsCode > 0) {
    return httpsCode;
  }

  // Fallback for devices/networks where TLS handshake fails.
  WiFiClient plainClient;
  HTTPClient http;
  if (!http.begin(plainClient, httpUrl)) {
    return -1;
  }

  const char *headerKeys[] = {"Location"};
  http.collectHeaders(headerKeys, 1);
  http.addHeader("X-API-Key", apiKey);
  http.addHeader("Accept", "application/json");
  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    payload = http.getString();
  }

  String location = http.header("Location");
  http.end();

  // If HTTP redirects to HTTPS host, try that exact target once.
  if ((code == HTTP_CODE_MOVED_PERMANENTLY || code == HTTP_CODE_FOUND || code == HTTP_CODE_TEMPORARY_REDIRECT || code == HTTP_CODE_PERMANENT_REDIRECT) &&
      location.startsWith("https://")) {
    int redirectedHttpsCode = httpsGet(location, payload);
    if (redirectedHttpsCode > 0) {
      return redirectedHttpsCode;
    }
  }

  return code;
}
}  // namespace

bool parseGateway(const String &json) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    wanStatus = "JSON ERR";
    return false;
  }

  JsonVariant dataVar = doc["data"];
  JsonObject gw;

  if (dataVar.is<JsonArray>()) {
    JsonArray data = dataVar.as<JsonArray>();
    if (!data.isNull() && data.size() > 0) {
      gw = data[0].as<JsonObject>();
    }
  } else if (dataVar.is<JsonObject>()) {
    JsonObject dataObj = dataVar.as<JsonObject>();
    if (!dataObj["name"].isNull() || !dataObj["status"].isNull()) {
      gw = dataObj;
    } else {
      for (JsonPair kv : dataObj) {
        if (kv.value().is<JsonObject>()) {
          gw = kv.value().as<JsonObject>();
          break;
        }
      }
    }
  }

  if (gw.isNull()) {
    wanStatus = "NO DATA";
    wanDelay = "-";
    wanRttSd = "-";
    wanLoss = "-";
    return false;
  }

  wanName = pickString(gw["name"], "WAN");
  wanStatus = pickString(gw["status"], "unknown");

  wanDelay = pickString(gw["delay"]);
  if (wanDelay == "-") wanDelay = pickString(gw["rtt"]);
  if (wanDelay == "-") wanDelay = pickString(gw["latency"]);
  if (wanDelay == "-") wanDelay = pickString(gw["avg_delay"]);
  if (wanDelay != "-" && wanDelay.indexOf("ms") < 0 && wanDelay.indexOf(" ") < 0) {
    wanDelay += "ms";
  }
  if (!looksLikeRttValue(wanDelay)) {
    wanDelay = "-";
  } else {
    wanDelay = normalizeRttOneDecimal(wanDelay);
  }

  wanRttSd = pickString(gw["rttsd"]);
  if (wanRttSd == "-") wanRttSd = pickString(gw["stddev"]);
  if (wanRttSd == "-") wanRttSd = pickString(gw["delay_stddev"]);
  if (wanRttSd == "-") wanRttSd = pickString(gw["latency_stddev"]);
  if (wanRttSd == "-") wanRttSd = pickString(gw["std_dev"]);
  if (wanRttSd != "-" && wanRttSd.indexOf("ms") < 0 && wanRttSd.indexOf(" ") < 0) {
    wanRttSd += "ms";
  }
  if (!looksLikeRttValue(wanRttSd)) {
    wanRttSd = "-";
  } else {
    wanRttSd = normalizeRttOneDecimal(wanRttSd);
  }

  wanLoss = pickString(gw["loss"]);
  if (wanLoss == "-") wanLoss = pickString(gw["packetloss"]);
  if (wanLoss == "-") wanLoss = pickString(gw["loss_percent"]);
  if (wanLoss != "-" && wanLoss.indexOf("%") < 0 && wanLoss.indexOf(" ") < 0) {
    wanLoss += "%";
  }

  return true;
}

void fetchGatewayStatus() {
  if (WiFi.status() != WL_CONNECTED) {
    wanStatus = "NO WIFI";
    return;
  }

  if (strlen(apiKey) == 0) {
    wanStatus = "NO KEY";
    wanDelay = "-";
    wanRttSd = "-";
    wanLoss = "-";
    return;
  }

  String payload;
  int code = apiGet("/api/v2/status/gateways", payload);
  if (code == HTTP_CODE_OK) {
    parseGateway(payload);
  } else {
    wanStatus = String("HTTP ") + code;
  }
}

void fetchWanTraffic() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (strlen(apiKey) == 0) {
    return;
  }

  String payload;
  int code = apiGet("/api/v2/status/interfaces", payload);
  if (code != HTTP_CODE_OK) {
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    return;
  }

  JsonVariantConst root = doc["data"].isNull() ? doc.as<JsonVariantConst>() : doc["data"].as<JsonVariantConst>();
  JsonObjectConst wanObj;

  if (root.is<JsonObjectConst>()) {
    JsonObjectConst rootObj = root.as<JsonObjectConst>();

    if (!rootObj["wan"].isNull() && rootObj["wan"].is<JsonObjectConst>()) {
      wanObj = rootObj["wan"].as<JsonObjectConst>();
    }

    if (wanObj.isNull() && !rootObj[wanName].isNull() && rootObj[wanName].is<JsonObjectConst>()) {
      wanObj = rootObj[wanName].as<JsonObjectConst>();
    }

    if (wanObj.isNull()) {
      for (JsonPairConst kv : rootObj) {
        if (!kv.value().is<JsonObjectConst>()) {
          continue;
        }

        JsonObjectConst candidate = kv.value().as<JsonObjectConst>();
        String key = kv.key().c_str();
        String descr = pickString(candidate["description"], "");
        if (descr.length() == 0) descr = pickString(candidate["descr"], "");
        String iface = pickString(candidate["if"], "");
        String name = pickString(candidate["name"], "");
        String combined = key + " " + descr + " " + iface + " " + name;

        if ((wanName.length() > 0 && containsIgnoreCase(combined, wanName)) || containsIgnoreCase(combined, "wan")) {
          wanObj = candidate;
          break;
        }
      }
    }
  } else if (root.is<JsonArrayConst>()) {
    JsonArrayConst arr = root.as<JsonArrayConst>();
    for (JsonVariantConst v : arr) {
      if (!v.is<JsonObjectConst>()) {
        continue;
      }
      JsonObjectConst candidate = v.as<JsonObjectConst>();
      String name = pickString(candidate["name"], "");
      String descr = pickString(candidate["descr"], "");
      String iface = pickString(candidate["if"], "");
      String hwif = pickString(candidate["hwif"], "");
      String combined = name + " " + descr + " " + iface + " " + hwif;

      if (containsIgnoreCase(name, "wan") || containsIgnoreCase(combined, wanName) || containsIgnoreCase(combined, "wan")) {
        wanObj = candidate;
        break;
      }
    }
  }

  if (wanObj.isNull()) {
    return;
  }

  const char *inKeys[] = {"inbytes", "in_bytes", "ibytes", "bytes_in", "received-bytes", "inbytes_frmt", "inbytes_formatted"};
  const char *outKeys[] = {"outbytes", "out_bytes", "obytes", "bytes_out", "sent-bytes", "outbytes_frmt", "outbytes_formatted"};

  bool hasIn = false;
  bool hasOut = false;
  uint64_t inBytes = 0;
  uint64_t outBytes = 0;

  for (size_t i = 0; i < sizeof(inKeys) / sizeof(inKeys[0]); ++i) {
    JsonVariantConst v = wanObj[inKeys[i]];
    if (!v.isNull()) {
      inBytes = uint64FromVariant(v);
      hasIn = true;
      break;
    }
  }

  for (size_t i = 0; i < sizeof(outKeys) / sizeof(outKeys[0]); ++i) {
    JsonVariantConst v = wanObj[outKeys[i]];
    if (!v.isNull()) {
      outBytes = uint64FromVariant(v);
      hasOut = true;
      break;
    }
  }

  if (hasIn && hasOut) {
    uint32_t nowMs = millis();
    if (wanTrafficPrimed && nowMs > wanPrevSampleMs && inBytes >= wanPrevInBytes && outBytes >= wanPrevOutBytes) {
      float dt = (nowMs - wanPrevSampleMs) / 1000.0f;
      float rxKbps = ((inBytes - wanPrevInBytes) * 8.0f) / (dt * 1000.0f);
      float txKbps = ((outBytes - wanPrevOutBytes) * 8.0f) / (dt * 1000.0f);
      pushTrafficSample(rxKbps, txKbps);
    }
    wanPrevInBytes = inBytes;
    wanPrevOutBytes = outBytes;
    wanPrevSampleMs = nowMs;
    wanTrafficPrimed = true;
  }

}

void fetchSystemStatus() {
  if (WiFi.status() != WL_CONNECTED) {
    cpuPercent = 0;
    memPercent = 0;
    tempPercent = 0;
    tempValue = "-";
    return;
  }

  if (strlen(apiKey) == 0) {
    cpuPercent = 0;
    memPercent = 0;
    tempPercent = 0;
    tempValue = "-";
    return;
  }

  String payload;
  int code = apiGet("/api/v2/status/system", payload);
  if (code == HTTP_CODE_OK) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (!err) {
      JsonVariantConst root = doc["data"].isNull() ? doc.as<JsonVariantConst>() : doc["data"].as<JsonVariantConst>();
      const char *memoryKeys[] = {"mem_usage", "memory", "memory_percent", "memory_usage", "mem", "swap", "ram"};
      const char *cpuKeys[] = {"cpu_usage", "cpu", "usage"};
      const char *tempKeys[] = {"temp", "temperature", "cpu_temp", "cpu_temperature", "temp_c", "temperature_c"};

      int cpu = -1;
      float cpuUsageRaw = floatFromVariant(root["cpu_usage"]);
      int cpuCount = (int)floatFromVariant(root["cpu_count"]);
      if (cpuUsageRaw >= 0.0f) {
        if (cpuCount > 1) {
          cpu = constrain((int)(cpuUsageRaw / cpuCount), 0, 100);
        } else {
          cpu = constrain((int)cpuUsageRaw, 0, 100);
        }
      }
      if (cpu < 0) {
        cpu = findPercentRecursive(root, cpuKeys, sizeof(cpuKeys) / sizeof(cpuKeys[0]));
      }
      if (cpu >= 0) cpuPercent = cpu;

      int mem = -1;
      mem = findPercentRecursive(root, memoryKeys, sizeof(memoryKeys) / sizeof(memoryKeys[0]));
      if (mem < 0) {
        JsonVariantConst memoryNode = root["memory"];
        if (memoryNode.isNull()) memoryNode = root["mem"];
        if (memoryNode.isNull()) memoryNode = root["system"]["memory"];

        float used = floatFromVariant(memoryNode["used"]);
        if (used < 0) used = floatFromVariant(memoryNode["used_bytes"]);
        if (used < 0) used = floatFromVariant(memoryNode["usage"]);
        if (used < 0) used = floatFromVariant(root["memory_used"]);
        if (used < 0) used = floatFromVariant(root["memory_usage"]);

        float total = floatFromVariant(memoryNode["total"]);
        if (total < 0) total = floatFromVariant(memoryNode["total_bytes"]);
        if (total <= 0.0f) {
          total = floatFromVariant(memoryNode["physmem"]);
        }
        if (total <= 0.0f) total = floatFromVariant(root["memory_total"]);
        if (total <= 0.0f) total = floatFromVariant(root["system"]["memory_total"]);
        if (total > 0.0f) {
          mem = constrain((int)((used * 100.0f) / total), 0, 100);
        }
      }
      if (mem >= 0) memPercent = mem;

      String temp = findStringRecursive(root, tempKeys, sizeof(tempKeys) / sizeof(tempKeys[0]));
      if (temp != "-") {
        String lower = temp;
        lower.toLowerCase();
        if (lower.indexOf("c") < 0 && lower.indexOf("f") < 0) {
          temp += "°C";
        } else if (lower.indexOf("°") < 0) {
          if (temp.endsWith(" C") || temp.endsWith(" c")) {
            temp.remove(temp.length() - 2);
          } else if (temp.endsWith("C") || temp.endsWith("c")) {
            temp.remove(temp.length() - 1);
          }
          temp += "°C";
        }
        tempValue = temp;
        int parsedTempPercent = temperatureToPercent(tempValue);
        tempPercent = parsedTempPercent >= 0 ? parsedTempPercent : 0;
      } else {
        tempValue = "-";
        tempPercent = 0;
      }
    }
  }
}
