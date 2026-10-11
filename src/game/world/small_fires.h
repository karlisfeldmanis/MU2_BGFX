// Aida's small fires: MU's RenderAidaObjectVisual arm for type 67 (GMAida.cpp:239-242), Object68,
// 11 hidden boxes on rocks and ruins (x 81-204, y 18-205), cooked as world anchors of kind 21
// (tools/cook_anchors.py) with the box's Scale in the emitter's `high`.
//
// Every reference frame each throws a BITMAP_FLAME SubType 7 (Effect/Flame01, 64 texels, added)
// at light (0.3, 0.3, 0.3) (ZzzEffectParticle.cpp:589-604, 4852-4868):
//
//   created  LifeTime 33; Scale the box's less 0-0.19; Rotation -180..179 degrees; nudged 0 or
//            Scale / 2 units along x and the same along y; Gravity (1.8 + 0-0.99) * Scale units
//            a frame; and seven times in ten a drift of (-1..0.9) * Scale units a frame along
//            the box's y, which MovePosition adds each frame.
//   a frame  Rotation 2 degrees towards the side it was nudged to; up by Gravity; Scale down by
//            Gravity / 95, dead at 0; light down by 1.007.
//
// So a flame about 0.6-1.7 m wide at its foot, licking up some 0.8 m and narrowing as it goes.
// Ours, marked in small_fires.cpp: each tongue drawn through the fire's heat ramp (gfx::Blend::
// Flame) rather than added flat at MU's light, 0.7 of MU's width, and eased in and out, since
// MU's flat orange tongues read as blobs (the user, 2026-10-11). Our particle fire, anchor kind
// 1, was tried in their place and the user went back to these.
// MU's rates are per reference frame, 25 a second. Only boxes within kReach of the camera's
// point throw. Nothing here reaches the sim.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <bgfx/bgfx.h>

#include "content/cooked.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class SmallFires {
public:
    // Opens where the world's manifest names `small_fires`: the showing's `flame` (MU's
    // BITMAP_FLAME) and the cooked town's world anchors of kind SmallFire.
    void open(const std::string& assetDir, const std::string& world, const content::CookedTown& town,
              content::Textures& textures);
    void shutdown();
    bool isOpen() const { return open_; }

    // One frame round `near`, the point the camera follows, in metres.
    void update(float seconds, const float near[3]);
    void gather(gfx::Effects& effects) const;

private:
    struct Tongue {
        float at[3] = {0, 0, 0};
        float drift = 0.0f;    // metres a second along the box's y, which is our -z
        float rise = 0.0f;     // metres a second, MU's Gravity
        float age = 0.0f;      // reference frames
        float scale = 1.0f;    // MU's Scale
        float spin = 0.0f;     // radians
        float turn = 0.0f;     // radians a second, towards the side it was nudged to
    };
    struct Box {
        float at[3] = {0, 0, 0};
        float scale = 1.0f;
        float owed = 0.0f;  // seconds towards its next reference frame
    };
    void throwTongue(const Box& box);
    float unit();

    bool open_ = false;
    std::vector<Box> boxes_;
    std::vector<Tongue> tongues_;
    uint32_t seed_ = 0xF1A3E7u;
    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
};

}  // namespace mu::game
