#pragma once
#include <windows.h>
#include <string>
namespace PhotoCapture {
bool install(HMODULE module, std::string &error);
bool request();
void cancel();
void tick(bool active, bool foreground);
bool hideUi();
int feedback(); // 0 idle, 1 capturing/saving, 2 saved, 3 failed/cancelled
std::string telemetry();
} // namespace PhotoCapture
