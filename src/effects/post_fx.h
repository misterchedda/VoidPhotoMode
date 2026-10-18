#pragma once
#include <windows.h>
#include <string>
namespace PostFx {
bool install(uintptr_t base, std::string &error);
void begin();
void end();
void reset();
bool enabled(int effect); // 0 bloom, 1 grading, 2 DOF; permitted, not forced.
void setEnabled(int effect, bool enabled);
float exposure();
void setExposure(float stops); // Relative to entry; positive is brighter.
bool manual();
bool manualAvailable();
bool setManual(bool enabled);
float distance();
float width();
void setDistance(float value);
void setWidth(float value);
float bloomStrength();
bool bloomAvailable();
void setBloomStrength(float value);
float bokeh();
void setBokeh(float value);
unsigned flags();
std::string telemetry();
} // namespace PostFx
