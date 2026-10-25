// The Dark Raven's showing (docs/plans/dark-lord-pets/client.md section 1; MuMain CSPetSystem.cpp):
//
//   * **its flight**: a BITMAP_SPARK + 1 a step off one of its bones 0..65 while it flies (sub 5:
//     Effect/Spark03, (0.3, 0.4, 0.7), Scale 0.8 losing a tenth a frame, its light / 1.03 a frame,
//     ten frames, drifting 2..3 units a frame across its heading) -- its blue trail (:425-437,
//     ZzzEffectParticle.cpp:2147-2152, 6646-6651);
//   * **the dash**: its first two steps ten BITMAP_LIGHT joints each, thrown off the bird where it
//     is (sub 1: Effect/flare01, (1, 0.8, 0.6), life 10, 30 up at a random heading, 1..5 units a
//     frame and quicker by half a unit each) -- gold streaks off it as it dives; and the first a
//     MODEL_DARKLORD_SKILL flash, sub 3, at (45, -90..89, 0) in (1, 0.8, 0.6), ten frames, Scale 0.2
//     grown by a velocity of 0.1 that grows 0.02 a frame, its light / 1.8 a frame under seven left
//     -- 1.03.35's move, as fx/fire_burst's flash (:729-745; ZzzEffect.cpp:3449-3470); and past
//     its third step, each odd one while over 100 above him, MODEL_AIR_FORCE (Skill/AirForce.bmd on
//     SpiE_R, added) at its head, bone 6 50 along, turned as it is: fifteen frames, Scale 0.6 grown
//     0.2 a frame, its light / 1.5 a frame (:747-756; ZzzEffect.cpp:3176-3181, MoveHandlers.cpp:
//     5338-5342);
//   * **the charge**: a BITMAP_LIGHT sub 6 on every other bone (from 0 or 1) each step of its 15,
//     Effect/flare01 at Scale 1.3 in (1, 0.6, 0.4), two frames each -- the bird lit up (:759-775,
//     ZzzEffectParticle.cpp:3207-3210);
//   * **the ball**: at the charge's end, the 15 vertices of every 22nd triangle of its mesh 1 each
//     let go a BITMAP_JOINT_SPIRIT (sub 13: JointSpirit01, (1, 0.6, 0.4), life 20, ten tails),
//     pitched up 0..89 and turned +-60 off its heading, 40 units a frame and one more a frame to 50,
//     homing by MoveHumming at speed - 20 degrees a frame on the target +-70 and 50 up -- the aim
//     rolled anew each frame and the pitch kicked 20 one frame in two -- its life down twice as
//     fast within 50 of it, its light / 1.2 a frame under ten, headed by a BITMAP_LIGHT and
//     BITMAP_SHINY of 2 x life/10 + 2 (ZzzBMD.cpp:2184-2199, ZzzEffectJoint.cpp:812-822,
//     3830-3885) -- a fan that rises and curls in;
//   * **a level**: five BITMAP_FLARE joints up (fifteen down) spiralling about it 40 units out and
//     rising 2.0..4.5 a frame for 50 (:656-679, ZzzEffectJoint.cpp:1669-1690).
//
// MU's own: the counts, the colours, the speeds and lives; a particle a texel a unit
// (ZzzEffectParticle.cpp:8922-8935). Ours: each joint drawn as a strip from its tail to its head
// (as fx/drain's strands), its tails a fixed length -- a spirit's a ribbon through its last ten
// steps through a Catmull-Rom curve, its sheet stretched along it, fading to its end, and out over
// four frames once within 50 of its target, its turn tightening as it ages, where MU's looped about
// it or sailed past; the sparks at half MU's size and the charge's light
// and the spirits' heads at a third (the user's "too large"); the ball's 15 spread about the bird rather than
// on mesh 1's vertices. Presentation only: the realm struck on its own tick
// (What::Ravened, What::Hit). Pools sized once; a frame allocates nothing.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/showing.h"
#include "content/texture.h"
#include "game/fx/effect_mesh.h"
#include "gfx/effects.h"

namespace mu::game {

class RavenShow {
public:
    bool open(const std::string& assetDir, content::Textures& textures, const content::Showing& table);

    // Each at the bird or its bone, in world metres. `yaw` its heading, MU's Angle[2] in degrees.
    void spark(const float at[3], float yaw);
    void streaks(const float at[3]);
    void flash(const float at[3]);
    void glow(const float at[3]);
    // MODEL_AIR_FORCE at its head, turned as the bird is: `yaw` MU's Angle[2], `pitch` ours.
    void puff(const float at[3], float yaw, float pitch);
    // The ball from the bird to `target`'s feet.
    void ball(const float from[3], const float to[3], float yaw);
    // A level up (or down) at the bird.
    void level(const float at[3], bool up);

    void update(float seconds);
    void gather(gfx::Effects& effects) const;

private:
    enum Kind : uint8_t { kStreak, kSpirit, kFlare, kSpark, kGlow };
    static constexpr int kTails = 10;
    struct Joint {
        bool alive = false;
        Kind kind = kStreak;
        float at[3] = {}, was[3] = {};  // its head now and a tail's length back
        float aim[3] = {};              // a spirit's target's feet, a flare's centre
        float dir[3] = {};              // a streak's or a spark's way
        float speed = 0.0f, spin = 0.0f, rise = 0.0f;
        float age = 0.0f, life = 10.0f;
        float scale = 1.0f, light = 1.0f;  // a spark's
        // A spirit's flight in MU's space and units, stepped a whole frame at a time and drawn
        // between its last two steps.
        float mu[3] = {}, muWas[3] = {}, angle[3] = {};
        // And its tails, MaxTails 10 (ZzzEffectJoint.cpp:819): where it was at each of its last
        // steps, newest first, in world metres.
        float tails[kTails][3] = {};
        int tailCount = 0;
    };
    struct Flash {
        bool alive = false;
        float at[3] = {};
        float roll = 0.0f;
        float scale = 0.2f, velocity = 0.1f, light = 1.0f, left = 10.0f;
    };
    struct Puff {
        bool alive = false;
        float at[3] = {};
        float yaw = 0.0f, pitch = 0.0f;
        float scale = 0.6f, light = 1.0f, left = 15.0f;
    };

    static constexpr float kFps = 25.0f;
    static constexpr float kUnit = 0.01f;
    static constexpr int kJoints = 512, kFlashes = 8, kPuffs = 16;

    float unit();
    Joint* free();
    void flySpirit(Joint& one);

    bgfx::TextureHandle light_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle spirit_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle flare_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle shiny_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle spark_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle flashSheet_ = BGFX_INVALID_HANDLE;
    std::vector<EffectCorner> flashMesh_;
    bgfx::TextureHandle puffSheet_ = BGFX_INVALID_HANDLE;
    std::vector<EffectCorner> puffMesh_;
    Puff puffs_[kPuffs] = {};
    Joint joints_[kJoints] = {};
    Flash flashes_[kFlashes] = {};
    float owed_ = 0.0f;  // reference frames toward the spirits' next whole step
    uint32_t dice_ = 0x5EB1A7C3u;
};

}  // namespace mu::game
