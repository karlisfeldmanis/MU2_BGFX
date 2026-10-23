// A Kanturu Blade Hunter's strike: a blue tornado where its blow lands and six blasts there.
//
// MuMain, AttackEffectKanturu1stMonster, MODEL_BLADE_HUNTER (GM_Kanturu_1st.cpp:716-740): at
// AttackTime 14 of either attack, 140 units out and 30 down from BLADE_L_HAND (bone 12, Bip01 L
// Hand), a MODEL_BLADE_SKILL in (0.2, 0.2, 1.0) -- Effect/bladetonedo.bmd, a tornado of
// shockwave.jpg 1.8 m tall (its bones turn the exported mesh upright), drawn RENDER_TEXTURE |
// RENDER_BRIGHT three times over (ZzzObject.cpp:1669-1674), its scale 1.5 falling 0.1 a frame to 0.8, ten frames
// (ZzzEffect.cpp:521-530, MoveHandlers.cpp:331-341) -- and six BITMAP_EXPLOTION + 1 at 1.3, 2.3
// and 1.8 twice: Effect/DinoE, four 64 cells played over twelve frames, each ringing
// SOUND_EXPLOTION01 (ZzzEffectParticle.cpp:2497-2500, 4276-4278, 8968-8970).
//
// **Ours**: the blasts on what it hunts at the blow's height (0.3 m below the hand), and 1.4 m
// out along its facing when nothing -- MU's offset is in the hand's own axes, which the Blender
// rebuild rolls -- and the tornado stood on the ground under them. The blasts at a third of MU's size, the user's
// measure for monster sparks; one explosion sound, not six at once. Fed by Play::breedShows;
// nothing here reaches the sim.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <bgfx/bgfx.h>

#include "content/showing.h"
#include "content/texture.h"
#include "game/fx/effect_mesh.h"
#include "gfx/effects.h"

namespace mu::game {

class Figure;

class BladeHunter {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    void shutdown();

    // One hunter this frame: `attack` 1 or 2 while it swings, `key` its clip's key, `yaw` its
    // facing, `target` the feet of what it hunts or null. True the frame its strike lands, for
    // the caller's explosion sound.
    bool watch(uint32_t id, const Figure& figure, int attack, float key, float yaw, float scale,
               const float* target, float fade, float seconds, float at[3]);
    void update(float seconds);
    void gather(gfx::Effects& effects) const;

private:
    struct One {
        bool found = false;
        bool seen = false;
        int hand = -1;
        float owed = 0.0f;
        float frames = 0.0f;
        int lastAttack = 0;
        float lastKey = 0.0f;
        bool struck = false;
    };
    struct Funnel {
        float at[3] = {0, 0, 0};
        float yaw = 0.0f, scale = 1.5f, size = 1.0f, age = 0.0f, fade = 1.0f;
    };
    struct Blast {
        float at[3] = {0, 0, 0};
        float scale = 1.0f, age = 0.0f, fade = 1.0f;
    };

    std::unordered_map<uint32_t, One> ones_;
    std::vector<Funnel> funnels_;
    std::vector<Blast> blasts_;
    std::vector<EffectCorner> funnel_;  // bladetonedo.bmd
    bgfx::TextureHandle shockwave_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle blast_ = BGFX_INVALID_HANDLE;
};

}  // namespace mu::game
