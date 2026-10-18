#pragma once
#include <algorithm>
#include <cmath>
// A sampled delta spans two frames. Discard it when the gesture or focus changes
// so a click, release, entry or alt-tab cannot turn UI motion into camera motion.
struct PointerControls {
    bool priorActive = false, priorLook = false;
    struct Motion {
        long long pointerX = 0, pointerY = 0, lookX = 0, lookY = 0;
        bool looking = false;
    };
    Motion frame(bool active, bool rightDown, long long x, long long y) {
        Motion m;
        m.looking = active && rightDown;
        if (active && priorActive && m.looking == priorLook) {
            if (m.looking) {
                m.lookX = x;
                m.lookY = y;
            } else {
                m.pointerX = x;
                m.pointerY = y;
            }
        }
        priorActive = active;
        priorLook = m.looking;
        return m;
    }
    static float wheelSpeed(float speed, long long delta) {
        return std::clamp(speed * std::pow(1.25f, float(std::clamp(delta, -12000LL, 12000LL)) / 120.0f), 0.1f,
                          16.0f);
    }
};
