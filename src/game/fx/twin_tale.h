// A Kanturu Relics Twin Tale's shows: the marks its hair leaves as it walks, its Hair Attack's
// beams and the water they burst into on what they strike, and the green it dies in.
//
// MuMain, GM_Kanturu_2nd.cpp:
//
//   Render_Kanturu2nd_MonsterVisual, MODEL_TWIN_TAIL (:877-982): walking, every 500 ms a
//   MODEL_TWINTAIL_EFFECT sub 0 under Twintail_Hair24 and then Twintail_Hair32 in turn (bones 16
//   and 24, Bone24 and Bone32, :82-83), in (0.6, 1.0, 0.4): a BITMAP_SPARK + 1 (Effect/Spark03) laid
//   on the ground, added, that fades 0.01 a frame, its light falling by 1/1.02 a frame, and every
//   second turns between shrinking 0.015 a frame and growing 0.02 up to 1.2 (ZzzEffect.cpp:
//   4441-4452, 9065-9072; MoveHandlers.cpp:6821-6860). Dying, over its first three keys each
//   frame five BITMAP_SMOKE sub 39 in (0.1, 1.0, 0.2) and a BITMAP_TWINTAIL_WATER in (0.4, 1.0,
//   0.6) at its feet; at its first, two MODEL_TWINTAIL_EFFECT sub 1/2: a BITMAP_CLOUD on the ground
//   at 3.5, fading 0.01 a frame.
//
//   Move_Kanturu2nd_BlurEffect (:1272-1300): through Attack 2, its Hair Attack (ATMON_SKILL), one
//   frame in two a BITMAP_JOINT_ENERGY sub 0 from each hair's tip at what it attacks, 30 wide.
//   Move_Kanturu2nd_MonsterVisual (:288-318): Attack 2's keys 5.5 to 6.5, each frame five
//   BITMAP_SMOKE sub 1 in (0.4, 0.9, 0.6) and two BITMAP_TWINTAIL_WATER (Effect/water) in (0.4,
//   1.0, 0.6) on what it attacks. The water's sub 0 lives 50-59 frames at 1.40-1.72, up to 60
//   units across, 50 along and 100 up off its point, losing 0.013 of scale and 0.05 of alpha a
//   frame, its light by 1/1.02 (ZzzEffectParticle.cpp:1114-1121, 5253-5266).
//
//   The joint's sub 0 (ZzzEffectJoint.cpp:210-226, 2991-2999, 3237-3364; ZzzAI.cpp:134-145):
//   LifeTime 120, eight tails, Velocity 3, moving Velocity units a frame along its heading; for
//   its first twenty frames its heading turns 10 degrees a frame and it rises 6, then Velocity
//   climbs 5 a frame to 30 and MoveHumming turns it up to Velocity degrees a frame at what it
//   attacks + 120 up, slowing 10 a frame when inside 70 units it turns more than 20, and it is
//   gone inside 35 units of it.
//
// **Ours**: the hair's beams as MU's flying joint, a ribbon of JointEnergy01 through its eight
// tails at a third of MU's width (the user, 2026-10-25: "you decide", over the still streak
// it was), its heading starting from the tip toward what it attacks. MU's bolts fly some 34
// frames and its attack is 30, so they struck after the blow (the user: "looks out of sync?"):
// ours are thrown only through keys 0 to 1.5 and rise their 120 units in 6 frames, not 20,
// turning 30 degrees a frame, and land with the blow; smoke and water at a third of
// MU's size, the user's measure for monster sprites; the marks and clouds at MU's own size, as they
// lie on the ground. Not carried: the MODEL_SKILL_INFERNO sub 9 it dies in, and the energy ring
// MU lays under it only in the Maya battle (map 39). MU's numbers are per reference frame, 25 a
// second, in centimetres. Fed by Play::breedShows; nothing here reaches the sim.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <bgfx/bgfx.h>

#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class Figure;

class TwinTale {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    void shutdown();

    // One Twin Tale this frame: `walking`, `hair` while it plays its Hair Attack (Attack 2) and
    // `key` that clip's key, `target` the feet of what it attacks or null, `dying` once it has
    // fallen and `dyingKey` its death clip's key, `feet` its own feet on the ground.
    void watch(uint32_t id, const Figure& figure, bool walking, bool hair, float key,
               const float* target, bool dying, float dyingKey, const float feet[3], float fade,
               float seconds);
    void update(float seconds);
    void gather(gfx::Effects& effects) const;

private:
    struct One {
        bool found = false;
        bool seen = false;
        int hair[2] = {-1, -1};  // Bone24, Bone32
        float owed = 0.0f;
        float sinceMark = 0.0f;  // seconds
        int nextHair = 0;
        bool clouded = false;
    };
    struct Mark {
        float at[3] = {0, 0, 0};
        float scale = 1.0f, alpha = 1.0f, light = 1.0f, age = 0.0f, fade = 1.0f;
        float spin = 0.0f;
        bool cloud = false;  // the death's BITMAP_CLOUD at 3.5; else the walk's Spark03
    };
    struct Puff {
        float at[3] = {0, 0, 0};
        float colour[3] = {1, 1, 1};
        float scale = 1.0f, alpha = 1.0f, light = 1.0f, rise = 0.0f, spin = 0.0f, age = 0.0f,
              life = 20.0f, fade = 1.0f;
        bool water = false;
    };
    struct Beam {
        uint32_t id = 0;  // whose hair: its mark follows what that one attacks
        float at[3] = {0, 0, 0};
        float to[3] = {0, 0, 0};
        float yaw = 0.0f, pitch = 0.0f;  // degrees: its heading
        float velocity = 3.0f;           // MU units a reference frame, and degrees it may turn
        float tail[8][3] = {};
        int tails = 0;
        float age = 0.0f, fade = 1.0f;
    };

    float unit();
    void puff(const float at[3], const float colour[3], bool water, float fade);

    std::unordered_map<uint32_t, One> ones_;
    std::vector<Mark> marks_;
    std::vector<Puff> puffs_;
    std::vector<Beam> beams_;
    float beamStep_ = 0.0f;  // what of a reference frame the tails are owed
    uint32_t seed_ = 0x7a1e2b93u;
    bgfx::TextureHandle spark_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle cloud_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle water_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle energy_ = BGFX_INVALID_HANDLE;
};

}  // namespace mu::game
