// The Guardian Angel's and the Imp's own light (docs/game/pets.md; the audit of 2026-10-25,
// docs/plans/dark-lord-pets/README.md batch 5):
//
//   * **the Angel**: each frame four BITMAP_SPARK sub 1 within 8 of it, grey (0.4) (MoveMount's
//     MODEL_HELPER, GOBoid.cpp:605-619): Effect/Spark02, Scale 0.4..0.7, the default two frames,
//     its light its LifeTime / 16 (ZzzEffectParticle.cpp:2014-2015, 6556-6568) -- a faint
//     twinkle; and as it is drawn a BITMAP_LIGHT at Scale 1 in L x (0.5, 0.8, 0.6), L 0.7..0.99
//     rolled each frame (RenderMount, :703-708) -- its soft green glow;
//   * **the Imp**: a BITMAP_LIGHT at Scale 1.5 in L x (0.5, 0, 0) at (20, 0, 15) in his left
//     clavicle's frame, the bone it sits on (ZzzCharacter.cpp:15455-15462) -- a red ember.
//
// MU's own: the counts, places, colours and lives; a particle a texel a unit, Spark02 four of them.
// Ours: the lights at a quarter of MU's size and under half its light, as the held weapons'
// (fx/held_lights.cpp). Presentation only. A frame allocates nothing.
#pragma once

#include <cstdint>
#include <string>

#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class PetLights {
public:
    bool open(const std::string& assetDir, content::Textures& textures, const content::Showing& table);

    // This frame's: an Angel flying at `at`, an Imp's light at `at` (world metres). Fed after the
    // pets are posed, before update.
    void angel(const float at[3]);
    void imp(const float at[3]);

    void update(float seconds);
    void gather(gfx::Effects& effects) const;

private:
    struct Mote {
        bool alive = false;
        float at[3] = {};
        float half = 0.0f, life = 2.0f;
    };
    struct Glow {
        float at[3] = {};
        float half = 0.0f, light = 1.0f;
        bool red = false;
    };

    static constexpr int kMotes = 256, kGlows = 32;

    float unit();

    bgfx::TextureHandle spark_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle light_ = BGFX_INVALID_HANDLE;
    Mote motes_[kMotes] = {};
    Glow glows_[kGlows] = {};  // fed this frame
    int glowCount_ = 0;
    Glow shown_[kGlows] = {};  // and drawn, taken at update
    int shownCount_ = 0;
    // The Angels fed this frame, for their motes on each whole frame's step.
    float angels_[kGlows][3] = {};
    int angelCount_ = 0;
    float owed_ = 0.0f;
    uint32_t dice_ = 0x2F6B1C35u;
};

}  // namespace mu::game
