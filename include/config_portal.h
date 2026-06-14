#pragma once

#include "globals.h"

bool isConfigured();
void configureWiFi();
void persistFirewallConfig();
void handleConfigSavedTransition();
void saveConfigCallback();
void drawBootScreen();
void dismissBootScreenIfConnected();
void setupPortalRoutes();
void markBootDashboardReady();
