#pragma once
#define WL_CONNECTED 3
struct WiFiMock { int status() { return WL_CONNECTED; } };
static WiFiMock WiFi;
