// Refined gear as a light source: a +7 or +9 hero's suit and weapon light the dark themselves.
//
// **Ours, and marked so.** MuMain lights nothing with a refined item: no AddTerrainLight is
// called for one. The user wanted the item itself to become a light source (2026-09-24), and
// this engine has moving lights. So at night:
//   * the item's own +7/+9 effect glows -- the chrome bands and the star brighten by the
//     sheet's refine_glow (shine.sh's shineAdded, through Renderer::setShineGlow);
//   * and it lights what is near it as a SHAPE, not a point: the suit as a line from the pelvis
//     to the neck, a weapon as a line from its grip to its tip (PointLight::line, lights.sh
//     lighting from the nearest point of the segment), so the ground and the gear beside it
//     are lit along the whole body and the whole blade, with no hot spot.
// What came first and was turned down, so it is not built again: one light at chest height
// with a chest-high column of flat distance (a floor disc round the feet); a point on each
// piece (lamps hung beside the gear); a flat glow of the chrome's colour over the whole
// surface, which ate the bands and the star ("the light effect is eating the +7,+9 effect");
// lights of 0.35 and 0.6 ("too aggressive"); and MuMain's own flare01 sprite at the tip of ten
// swords (RenderCharacter, ZzzCharacter.cpp:10174-10475), which the user did not want on the
// weapon. Nothing by day.
#pragma once

#include <cstdint>

#include "game/shine.h"
#include "gfx/renderer.h"

namespace mu::game {

class Gleam {
public:
    // Forgets last frame's lights and keeps the breathing clock. Called once a frame before
    // anything is fed.
    void update(float seconds);

    // A piece of refined gear lighting this frame: the line it lights from, end to end, and how
    // it shines. Below +7 it lights nothing; beyond kMaxLights the rest are dropped.
    void feedLight(const float from[3], const float to[3], const ShineLook& look);
    static constexpr uint32_t kMaxLights = 2;

    // Into `out`, at most `max`; how many were written. `daylight` is app::daylightOf's, 0.08
    // at midnight and 1 at noon, and the light is gone by day.
    uint32_t lights(gfx::PointLight* out, uint32_t max, float daylight) const;

    // How far into the night it is: 0 by day, 1 at midnight. The chrome's night boost is the
    // sheet's refine_glow times this.
    static float nightOf(float daylight);

private:
    struct Lit {
        float from[3];
        float to[3];
        ShineLook look;
    };
    float clock_ = 0.0f;
    Lit lit_[kMaxLights];
    uint32_t litCount_ = 0;
};

}  // namespace mu::game
