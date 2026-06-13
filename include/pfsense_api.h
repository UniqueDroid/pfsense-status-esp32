#pragma once

#include "globals.h"

bool parseGateway(const String &json);
void fetchGatewayStatus();
void fetchWanTraffic();
void fetchSystemStatus();
