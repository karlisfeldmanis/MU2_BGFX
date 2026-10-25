// MU's angle arithmetic, in degrees, for an effect that steers as MU's do: CreateAngle, TurnAngle2
// and AngleMatrix, shared by fx/fire_burst's homing heads and fx/raven's spirit bolts.
#pragma once

#include <cmath>

namespace mu::game {

constexpr float kMuDegree = 3.14159265358979f / 180.0f;

// MU's own, in degrees (Main 1.03.35 ZzzAI.cpp:31-148; MuMain Engine/AI/ZzzAI.cpp:60-145).
inline float createAngle(float x1, float y1, float x2, float y2) {
    const float nx = x2 - x1, ny = y2 - y1;
    if (std::fabs(nx) < 0.0001f) return ny < 0.0f ? 0.0f : 180.0f;
    if (std::fabs(ny) < 0.0001f) return nx < 0.0f ? 270.0f : 90.0f;
    const float a = std::atan(ny / nx) / kMuDegree + 90.0f;
    return nx < 0.0f ? a + 180.0f : a;
}

inline float turnAngle2(float angle, float a, float d) {
    if (angle < 0.0f) angle += 360.0f;
    if (a < 0.0f) a += 360.0f;
    if (angle < 180.0f) {
        const float aa = angle - d;
        if (a >= angle + d && a < angle + 180.0f) return angle + d;
        if (aa >= 0.0f && (a >= angle + 180.0f || a < aa)) return angle - d;
        if (aa < 0.0f && (a >= angle + 180.0f && a < aa + 360.0f)) return angle - d + 360.0f;
        return a;
    }
    const float aa = angle + d;
    if (a < angle - d && a >= angle - 180.0f) return angle - d;
    if (aa < 360.0f && (a < angle - 180.0f || a >= aa)) return angle + d;
    if (aa >= 360.0f && (a < angle - 180.0f && a >= aa - 360.0f)) return angle + d - 360.0f;
    return a;
}

// MU's AngleMatrix (ZzzMathLib.cpp:194): angles[2] the yaw about Z, [1] about Y, [0] about X.
inline void angleMatrix(const float angles[3], float m[3][3]) {
    const float sy = std::sin(angles[2] * kMuDegree), cy = std::cos(angles[2] * kMuDegree);
    const float sp = std::sin(angles[1] * kMuDegree), cp = std::cos(angles[1] * kMuDegree);
    const float sr = std::sin(angles[0] * kMuDegree), cr = std::cos(angles[0] * kMuDegree);
    m[0][0] = cp * cy;
    m[1][0] = cp * sy;
    m[2][0] = -sp;
    m[0][1] = sr * sp * cy - cr * sy;
    m[1][1] = sr * sp * sy + cr * cy;
    m[2][1] = sr * cp;
    m[0][2] = cr * sp * cy + sr * sy;
    m[1][2] = cr * sp * sy - sr * cy;
    m[2][2] = cr * cp;
}

}  // namespace mu::game
