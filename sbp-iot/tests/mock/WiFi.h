#pragma once
#include "Arduino.h"
#define WL_CONNECTED 3
#define WIFI_STA 1
struct WiFiMock { void mode(int) {} void begin(const char*, const char*, int) {} int status() { return sim::wifiOk ? WL_CONNECTED : 6; } };
inline WiFiMock WiFi;
struct WiFiClient {};
