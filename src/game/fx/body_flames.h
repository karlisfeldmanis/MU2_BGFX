// MU's BITMAP_FLAME particles thrown off a monster's own bones every frame: Flame01, added, a
// random turn each frame (ZzzEffectParticle.cpp:577-660 creates, :4777-4830 moves, :9221 draws).
//
// Subtype 1, the plain Beam Knight's hands (ZzzCharacter.cpp:5927-5944): every reference frame a
// flame at bone 62 and at bone 77 (its claws), at Scale 0.2 and Light (L, L/2, L/2) with
// L = sin(WorldTime * 0.002) * 0.3 + 0.7, its Angle MoveHumming's from bone 55 (and 70) to that
// point, so it drifts out past the claw. On creation LifeTime 15, Scale += 0.32-0.63, Velocity
// (0, 0.6-1.05, 0) along the Angle; at LifeTime 10 the Velocity gains 6.4 and the Scale loses
// 0.15; every frame each light channel loses 0.05.
//
// The caller finds the bones by name and feeds the points; this owns the particles.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class BodyFlames {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    void shutdown();

    // Ages and moves every flame by the frame's seconds; MoveCharacterVisual's clock too.
    void update(float seconds);
    // One subtype 1 flame at `at`, drifting along `along` (unit), at MU's `scale`.
    void handFlame(const float at[3], const float along[3], float scale);
    // MoveCharacterVisual's `sinf(WorldTime * 0.002f) * 0.3f + 0.7f`.
    float luminosity() const;
    void gather(gfx::Effects& effects) const;

private:
    struct Flame {
        bool alive = false;
        float at[3] = {};
        float along[3] = {};
        float speed = 0.0f;  // MU's units a reference frame
        float scale = 1.0f;
        float light[3] = {};
        float left = 0.0f;  // reference frames
        float spin = 0.0f;
    };
    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
    std::vector<Flame> flames_;
    float clock_ = 0.0f;  // seconds, wrapped at the sine's period
    uint32_t dice_ = 0x6b2f1d93u;
    bool open_ = false;
    uint32_t roll();
};

}  // namespace mu::game
