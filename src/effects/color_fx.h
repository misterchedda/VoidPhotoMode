#pragma once
#include <string>
namespace ColorFx {
bool install(std::string &error);
void begin();
void end();
void reset();
void trackCube(void *wrapper);
float get(int control); // saturation, contrast, warmth, tint
bool set(int control, float value);
std::string telemetry();
} // namespace ColorFx
