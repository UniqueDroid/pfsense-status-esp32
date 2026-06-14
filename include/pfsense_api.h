#pragma once

// High-level fetch/parse interface for pfSense API endpoints.
#include "globals.h"

bool parseGateway(const String &json);
void fetchGatewayStatus();
void fetchWanTraffic();
void fetchSystemStatus();
