#pragma once

// Public portal/web-menu API used by main loop and dashboard flow.
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
