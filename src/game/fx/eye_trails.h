// The purple ribbons Tarkan's monsters trail from their eyes. Every Tarkan breed's CreateMonster
// case ends in the same two lines (ZzzCharacter.cpp:13703-13768):
//
//   CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 2, o, 30.f);
//   CreateJoint(BITMAP_JOINT_ENERGY, o->Position, o->Position, o->Angle, 3, o, 30.f);
//
// and MoveCharacterVisual's case for the model calls MoveEye(o, b, Right, Left) every frame,
// which puts the two bones' points in o->EyeRight and o->EyeLeft (:5535-5550). The joint lives
// as long as its monster (LifeTime 999999999) and MoveJoint pins its head to an eye each frame
// (ZzzEffectJoint.cpp:262-292, :3035-3091):
//
//   subtype 2: MaxTails 20, following EyeLeft;
//   subtype 3: MaxTails 8, following EyeRight;
//   both: Scale 30 (the ribbon 30 units across), Light (0.5, 0.1, 1.0) (:443-444), added.
//
// So one long purple streak off one eye and a short one off the other, the asymmetry MU's.
// The caller finds the eye bones by name (kEyeTrailRows) and feeds the two points every frame
// the body is drawn; a trail no longer fed is let go.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class EyeTrails {
public:
    static constexpr int kTails = 20;

    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    void shutdown();

    // Moves every trail on by the frame's seconds: a tail laid a reference frame, and a trail
    // that went unfed since the last update let go.
    void update(float seconds);
    // The eye a trail follows this frame: `left` is subtype 2's (20 tails), else subtype 3's (8).
    void feed(uint32_t id, bool left, const float at[3]);
    // `eye` is the camera, which the ribbon is turned to face.
    void gather(gfx::Effects& effects, const float* eye) const;

private:
    struct Trail {
        uint32_t id = 0;
        bool left = false;
        bool fed = false;
        int most = kTails;
        int count = 0;
        float clock = 0.0f;
        float head[3] = {0.0f, 0.0f, 0.0f};
        float tails[kTails][3] = {};
    };
    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
    std::vector<Trail> trails_;
    bool open_ = false;
};

}  // namespace mu::game
