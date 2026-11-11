#pragma once
struct WiFiClientSecure { void setInsecure() {} bool connected() { return false; } };
