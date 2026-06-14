#pragma once

// Public portal/web-menu API used by main loop and dashboard flow.
#include "globals.h"

// Returns true when mandatory firewall host and API key are available.
bool isConfigured();

// Configure station mode and captive portal/web menu.
void configureWiFi();

// Persist current in-memory host/key values to Preferences.
void persistFirewallConfig();

// Trigger visual transition after settings were stored.
void handleConfigSavedTransition();

// WiFiManager callback: marks configuration dirty for deferred save.
void saveConfigCallback();

// Draw initial splash/boot information before dashboard is ready.
void drawBootScreen();

// Remove boot screen when connectivity and dashboard state are ready.
void dismissBootScreenIfConnected();

// Register custom HTTP routes for local web menu and update endpoint.
void setupPortalRoutes();

// Mark dashboard initialization complete for boot-screen flow control.
void markBootDashboardReady();
