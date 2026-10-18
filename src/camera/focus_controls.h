#pragma once
#include <cstdint>
namespace FocusControls {
// Return addresses of individual environment reads in the native DOF builder.
// Override their results before the game's own CoC computation, leaving authored
// bokeh, fringing, distortion, sharpening and other lens settings alone.
inline float parameter(uintptr_t caller, float original, float distance, float width) {
    switch (caller) {
    case 0x3140bb:
        return distance;
    case 0x3140d2:
    case 0x314134:
        return width;
    case 0x3140e9:
    case 0x31411d:
        return width * 0.5f;
    case 0x314100:
        return 0; // Focus bias.
    case 0x31429b:
        return 0; // Disable autofocus only for this settings read.
    default:
        return original;
    }
}
} // namespace FocusControls
