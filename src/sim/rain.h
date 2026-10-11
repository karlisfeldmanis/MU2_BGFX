#pragma once

// The rain's rhythm: how long a dry spell and a wet one last. The server's World keeps the clock
// and sends `rain` with every tick, and the client's Weather draws what it is told -- or, run
// without a server, keeps the same rhythm itself (game/world/weather.cpp). The numbers were
// written twice, once in each, with a comment owning up to it; here once (docs/order-plan.md
// sprint C). Lengthened 2026-09-29 by the user: "rain is happening longer and daytime also is
// longer", then "we need bigger intervals between daytime and rain".

#include <algorithm>
#include <cstdint>

namespace mu::sim {

constexpr float kDryLow = 600.0f, kDryHigh = 1080.0f;  // seconds of a dry spell
constexpr float kWetLow = 180.0f, kWetHigh = 300.0f;   // and of a wet one

// The server's clock, one a world: starts dry, turns at the end of each spell.
struct RainClock {
    bool wet = false;
    float left = kDryLow;  // seconds left in the current spell
    uint32_t seed = 0x9E3779B9u;

    float random01() {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return float(seed & 0xFFFFu) / 65535.0f;
    }
    // `held`: a Golden Invasion is in its sky or on its ground, and its storm does not pass
    // until the dragon is killed or flies away (sim/invasion.h).
    void tick(float dt, bool held = false) {
        left -= dt;
        if (held && wet) left = std::max(left, 1.0f);
        if (left > 0.0f) return;
        wet = !wet;
        left = wet ? kWetLow + random01() * (kWetHigh - kWetLow)
                   : kDryLow + random01() * (kDryHigh - kDryLow);
    }
};

}  // namespace mu::sim
