#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Network & Config
extern char pfSenseHost[64];
extern char apiKey[160];
extern String wanName;

// WAN Status
extern String wanStatus;
extern String wanDelay;
extern String wanRttSd;
extern String wanLoss;

// Traffic History
extern int kTrafficPoints;
extern float wanRxHistory[];
extern float wanTxHistory[];
extern bool wanTrafficPrimed;
extern uint64_t wanPrevInBytes;
extern uint64_t wanPrevOutBytes;
extern uint32_t wanPrevSampleMs;

// System Metrics
extern int cpuPercent;
extern int memPercent;
extern int tempPercent;
extern String tempValue;
extern String lastUpdate;

// Preferences
extern bool shouldSaveConfig;

// Polling
extern uint32_t lastPoll;
extern const uint32_t kPollMs;
