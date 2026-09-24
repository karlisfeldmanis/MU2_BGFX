#include "game/fx/gleam.h"

#include <algorithm>
#include <cmath>

namespace mu::game {

namespace {

// The light (ours). Two and a half metres of reach from the gear's own line; brighter at +9
// than at +7, and breathing on g_Luminosity's own period so it moves with the +3/+5 pulse
// rather than against it. Halved from 0.35 and 0.6 on the user's "too aggressive".
constexpr float kLightReach = 2.5f;
constexpr float kLightStrength7 = 0.18f;
constexpr float kLightStrength9 = 0.3f;

}  // namespace

void Gleam::update(float seconds) {
    clock_ += seconds;
    litCount_ = 0;
}

void Gleam::feedLight(const float from[3], const float to[3], const ShineLook& look) {
    if (look.level < 7 || litCount_ >= kMaxLights) return;
    Lit& lit = lit_[litCount_++];
    std::copy(from, from + 3, lit.from);
    std::copy(to, to + 3, lit.to);
    lit.look = look;
}

float Gleam::nightOf(float daylight) {
    // From full at midnight's 0.08 to nothing by 0.6, which the sheet's dusk passes.
    return 1.0f - std::clamp((daylight - 0.08f) / 0.52f, 0.0f, 1.0f);
}

uint32_t Gleam::lights(gfx::PointLight* out, uint32_t max, float daylight) const {
    if (litCount_ == 0 || out == nullptr || max == 0) return 0;
    const float dark = nightOf(daylight);
    if (dark <= 0.0f) return 0;
    const float breath = 0.85f + 0.15f * std::sin(clock_ * 4.0f);
    uint32_t count = 0;
    for (uint32_t i = 0; i < litCount_ && count < max; ++i) {
        const Lit& lit = lit_[i];
        const float strength =
            (lit.look.level >= 9 ? kLightStrength9 : kLightStrength7) * dark * breath;
        gfx::PointLight& light = out[count++];
        std::copy(lit.from, lit.from + 3, light.position);
        std::copy(lit.to, lit.to + 3, light.to);
        light.line = true;
        light.reach = kLightReach;
        // No column of flat distance under it: the line is the shape. See the header.
        light.height = 0.0f;
        for (int c = 0; c < 3; ++c) light.colour[c] = lit.look.colour[c] * strength;
    }
    return count;
}

}  // namespace mu::game
