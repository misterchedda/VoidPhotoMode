#pragma once
#include <windows.h>
#include <string>
namespace NativeUi {
bool install(uintptr_t base, HMODULE module, std::string &error);
bool ready();
void begin();
void end();
bool focused();
bool hud();
void setHud(bool show);
void hidePanel();
void requestReset();
void frame(float fov, float roll, float speed, bool thirds, unsigned effects, float exposure, float distance,
           float width, float bloomStrength, float bokeh, float saturation, float contrast, float warmth,
           float tint, long long mouseX, long long mouseY, bool windowFocused, bool looking, bool togglePanel,
           bool click, bool held);
int takeAction(); // Native panel action; page switches stay inside AS3.
std::string telemetry();
} // namespace NativeUi
