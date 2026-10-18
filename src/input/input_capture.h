#pragma once
#include <windows.h>
#include <string>

namespace InputCapture {
// Call after MH_Initialize and before enabling hooks. Never alters device setup.
bool install(std::string &error);
void begin();
void end();
void frame(bool foreground);
bool down(int virtualKey);
bool pressed(int virtualKey);
bool takeExitRequest();
void takeMouse(long long &x, long long &y, long long &wheel);
std::string telemetry();
} // namespace InputCapture
