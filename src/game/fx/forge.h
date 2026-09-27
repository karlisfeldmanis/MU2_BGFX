// What comes off Hanzo's forge: the sparks his hammer strikes, and the smoke and embers his
// hearth gives off while he works.
//
// **The sparks are MU's.** MODEL_SMITH's own case in MuMain's per-frame effect switch
// (ZzzCharacter.cpp:6089), and the whole of what the client draws round him:
//
//     if (o->CurrentAction == 0 && o->AnimationFrame >= 5.f && o->AnimationFrame <= 6.f
//             && rand_fps_check(1)) {
//         b->TransformPosition(o->BoneTransform[17], (0,0,0), Position, true);
//         for (int i = 0; i < 4; i++) {
//             Vector(rand() % 60 + 60 + 90, 0, rand() % 30, Angle);
//             CreateJoint(BITMAP_JOINT_SPARK, Position, Position, Angle);
//             CreateParticle(BITMAP_SPARK, Position, Angle, Light);
//         }
//     }
//
// Bone 17 is `Box03`, the head of the hammer in his left hand. So a blow is four pairs, a
// reference frame, for as long as the swing is between keys 5 and 6: a STREAK
// (BITMAP_JOINT_SPARK subtype 0, Effect/Spark01, a straight line thrown off the anvil) and a
// MOTE (BITMAP_SPARK subtype 0, Effect/Spark02, a spark that hops, falls and bounces on the
// ground). Each one's motion is ZzzEffectJoint.cpp's and ZzzEffectParticle.cpp's own, kept
// in MU's units per 25 Hz reference frame and converted at the edge, so the numbers read
// against the source without arithmetic.
//
// Two things about them are MU's and look like mistakes, and both are kept:
//   * the streaks fly the same way in the WORLD whatever way he faces. The angle handed to
//     CreateJoint is built fresh and never turned by o->Angle, so AngleMatrix sends every
//     streak along MU's +y, spread 30 degrees to one side and 30 up or down.
//   * the motes ignore the yaw they were given: subtype 0 re-rolls Angle[2] to anything in
//     360, so they spray round the anvil in every direction.
//
// **One departure, the town's, and marked.** MU draws both sheets in their own orange at
// Light 1, which in this HDR frame is a flat colour that never passes 1.0 and so never
// blooms. They are drawn through fs_flame instead, the lamps' embers' program: the sheet's red
// is the shape, and MU's own fade -- the mote's Light = LifeTime / 16 -- is read as heat, so a
// spark is born white-hot and cools through orange to red as it dies. docs/sprints/08b.
//
// **The smoke and the embers are ours, all of them.** MuMain gives the smith no smoke at all
// (the only smoke in Lorencia's MoveObject is none: its fires are CreateFire and nothing
// else), and his BlendMesh 4 -- the coals -- is a flicker and not an emitter. A forge that is
// working has a thin smoke leaving its hearth and an ember lifting off the coals now and
// then; the user asked for both (2026-09-24). They are drawn as the lamps draw a bonfire's,
// same sheets and same programs, a good deal less of it: "nothing too much" is the standing
// word on ground smoke (tuning 2026-09-22).
//
// Pools sized once, and a frame allocates nothing: a particle past the pool is refused and
// counted, the rule every pool in this engine keeps. It is `game` and not `sim`: nothing it
// rolls reaches the seeded log.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/ground.h"
#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class Forge {
public:
    // Takes Spark01, Spark02, flare01 and smoke02 off the cooked showing. Not fatal: a sheet
    // that is missing draws nothing of its kind and says so.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table, const content::Ground* ground);
    void shutdown();

    // One reference frame of a blow: MU's four streaks and four motes at `at`, the hammer's
    // head, in world metres.
    void strike(const float at[3]);
    // The hearth at `at` working for `seconds`: its smoke and its embers, owed and paid whole.
    // `owed` is the caller's, a pair a forge, so two smiths never share a debt.
    void smoulder(const float at[3], float seconds, float owed[2]);

    void update(float seconds);
    // What is within reach of `near`, into the transparent pass. `eye` turns each streak's
    // width to the camera; `daylight` lights the smoke, which the pass does not.
    void gather(gfx::Effects& effects, const float eye[3], const float near[3],
                float daylight) const;

    uint32_t live() const { return uint32_t(streaks_.size() + motes_.size() + puffs_.size()); }
    uint32_t refused() const { return refused_; }

private:
    struct Streak {
        float position[3] = {0, 0, 0};
        float velocity[3] = {0, 0, 0};  // metres a reference frame, straight: no gravity
        float life = 8.0f;              // reference frames left
    };
    struct Mote {
        float position[3] = {0, 0, 0};
        float velocity[3] = {0, 0, 0};  // metres a reference frame
        float gravity = 0.0f;  // MU's Gravity: units a frame upward, losing 2 a frame
        float scale = 0.5f;
        float life = 24.0f;
    };
    // The hearth's smoke and embers, in metres and seconds as the lamps' particles are.
    static constexpr uint8_t kSmoke = 0, kEmber = 1;
    struct Puff {
        float position[3] = {0, 0, 0};
        float velocity[3] = {0, 0, 0};
        float age = 0.0f, life = 1.0f;
        float size = 0.3f;
        float spin = 0.0f, spinRate = 0.0f;
        float phase = 0.0f;
        uint8_t kind = kSmoke;
    };

    const content::Ground* ground_ = nullptr;
    bgfx::TextureHandle streak_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle mote_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle ember_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;
    std::vector<Streak> streaks_;
    std::vector<Mote> motes_;
    std::vector<Puff> puffs_;
    uint32_t dice_ = 0x68e31da4u;
    uint32_t refused_ = 0;
    bool open_ = false;

    uint32_t roll();
    float unit();
    void puff(const float at[3], uint8_t kind);
};

}  // namespace mu::game
