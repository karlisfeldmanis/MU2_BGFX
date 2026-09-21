// A frustum, the same Gribb-Hartmann one game/town.cpp culls chunks with, tested against a
// sphere. For what moves or poses on its own -- a figure, a swaying tree -- rather than a
// chunk, whose bounds are settled at cook time. At a few hundred tests a frame it is still
// under a microsecond. Crowd and Sway both cull with it.
#pragma once

#include <bgfx/bgfx.h>

#include <cmath>

namespace mu::game {

struct Frustum {
    float plane[6][4];

    explicit Frustum(const float* m) {
        auto set = [&](int index, int column, float sign) {
            for (int row = 0; row < 4; ++row) {
                plane[index][row] = m[row * 4 + 3] + sign * m[row * 4 + column];
            }
        };
        set(0, 0, 1.0f);
        set(1, 0, -1.0f);
        set(2, 1, 1.0f);
        set(3, 1, -1.0f);
        set(4, 2, 1.0f);
        set(5, 2, -1.0f);
        if (!bgfx::getCaps()->homogeneousDepth) {
            for (int row = 0; row < 4; ++row) plane[4][row] = m[row * 4 + 2];
        }
    }

    bool holds(const float* centre, float radius) const {
        for (const float* p : plane) {
            const float length = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
            if (length < 1e-8f) continue;
            const float distance =
                (p[0] * centre[0] + p[1] * centre[1] + p[2] * centre[2] + p[3]) / length;
            if (distance < -radius) return false;
        }
        return true;
    }
};

}  // namespace mu::game
