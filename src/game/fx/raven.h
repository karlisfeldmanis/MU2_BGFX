// The Dark Raven's showing (docs/plans/dark-lord-pets/client.md section 1; MuMain CSPetSystem.cpp):
//
//   * **the dash**: over its first three frames ten BITMAP_LIGHT joints a frame (sub 1: Effect/
//     flare01, (1, 0.8, 0.6), life 10, thrown 30 up at a random heading, 1..5 units a frame and
//     quicker by half a unit each) -- a burst of gold streaks off the bird as it dives (:729-757);
//   * **the ball**: at the charge's end, the bird's mesh-1 triangles let go BITMAP_JOINT_SPIRIT
//     joints (sub 13: JointSpirit01, (1, 0.6, 0.4), 40 units a frame, life 20, ten tails) that
//     home on the target, +-70 about it and 50 up, each headed by a BITMAP_LIGHT and BITMAP_SHINY
//     of 2 x life/10 + 2 (ZzzEffectJoint.cpp:812-822, 3830-3885) -- five here, MuMain's five
//     triangles of 96;
//   * **a level**: five BITMAP_FLARE joints up (fifteen down) spiralling about it 40 units out and
//     rising 2.0..4.5 a frame for 50 (:656-679, ZzzEffectJoint.cpp:1669-1690).
//
// MU's own: the counts, the colours, the speeds and lives. Ours: each joint drawn as a strip from
// its tail to its head (as fx/drain's strands), its tails a fixed length. Presentation only: the
// realm struck on its own tick (What::Ravened, What::Hit). Pools sized once; a frame allocates
// nothing.
#pragma once

#include <cstdint>
#include <string>

#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class RavenShow {
public:
    bool open(const std::string& assetDir, content::Textures& textures, const content::Showing& table);

    // The dash's burst at the bird (world metres), over its first three frames.
    void dash(const float at[3]);
    // The ball from the bird's head to `target`'s middle.
    void ball(const float from[3], const float to[3]);
    // A level up (or down) at the bird.
    void level(const float at[3], bool up);

    void update(float seconds);
    void gather(gfx::Effects& effects) const;

private:
    struct Joint {
        bool alive = false;
        int kind = 0;  // 0 a streak, 1 a spirit, 2 a flare
        float at[3] = {}, was[3] = {};  // its head now and a tail's length back
        float aim[3] = {};              // a spirit's target, a flare's centre
        float dir[3] = {};              // a streak's way
        float speed = 0.0f, spin = 0.0f, rise = 0.0f;
        float age = 0.0f, life = 10.0f;
    };
    struct Burst {
        bool alive = false;
        float at[3] = {};
        float age = 0.0f;
        int thrown = 0;  // frames let go of the three
    };

    static constexpr float kFps = 25.0f;
    static constexpr float kUnit = 0.01f;
    static constexpr int kJoints = 256, kBursts = 8;

    float unit();
    Joint* free();
    void streaks(const float at[3]);

    bgfx::TextureHandle light_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle spirit_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle flare_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle shiny_ = BGFX_INVALID_HANDLE;
    Joint joints_[kJoints] = {};
    Burst bursts_[kBursts] = {};
    uint32_t dice_ = 0x5EB1A7C3u;
};

}  // namespace mu::game
