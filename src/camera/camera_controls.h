#pragma once
#include <DirectXMath.h>
#include <cmath>

namespace CameraControls {
// The camera looks along local +X, with local +Z up. Positive UI roll
// uses the same sign as the existing Z key (rotation about local -X).
inline float rollDegrees(const float *quaternion) {
    using namespace DirectX;
    XMVECTOR q = XMLoadFloat4(reinterpret_cast<const XMFLOAT4 *>(quaternion));
    float leftZ = XMVectorGetZ(XMVector3Rotate(XMVectorSet(0, 1, 0, 0), q));
    float upZ = XMVectorGetZ(XMVector3Rotate(XMVectorSet(0, 0, 1, 0), q));
    // A world horizon is undefined when looking exactly vertically.
    if (leftZ * leftZ + upZ * upZ < 1e-8f)
        return 0;
    return XMConvertToDegrees(std::atan2(-leftZ, upZ));
}
inline float nextSpeed(float current, bool increase) {
    constexpr float speeds[] = {0.1f, 0.25f, 0.5f, 1, 2, 4, 8, 16};
    if (increase) {
        for (float s : speeds)
            if (s > current + 0.0001f)
                return s;
        return 16;
    }
    for (int i = 7; i >= 0; --i)
        if (speeds[i] < current - 0.0001f)
            return speeds[i];
    return 0.1f;
}
} // namespace CameraControls
