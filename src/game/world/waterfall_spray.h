// Aida's waterfall spray: MU's RenderAidaObjectVisual arms for types 56, 57 and 58
// (GMAida.cpp:166-183), three kinds of box MU never draws, standing with the Object26 falls (81 of
// the 84 within 2 m of one), cooked as world anchors (tools/cook.py ANCHOR_KINDS_BY_WORLD):
//
//   Drops  Object57, 22: one frame in two a BITMAP_WATERFALL_1 SubType 2 (Effect/waterFall1, cyan
//          droplets): 30 frames, falling 7-11 units a frame and slowing 0.1 a frame, Scale
//          1.6 + the box's, shrinking 0.005 a frame; light 0.2 brightening 1.1 times a frame for
//          its first ten, dimming 1.2 times a frame over its last five
//          (ZzzEffectParticle.cpp:3383-3403, 8357-8380).
//   Burst  Object58, 23: every frame a BITMAP_WATERFALL_3 SubType 4 (waterFall3, white specks):
//          20 frames, thrown up 2-6 units a frame and pulled down 2.5 a frame, so it rises a
//          moment and drops about 4 m; Scale 0.2-0.38 + the box's, growing 0.005 a frame; light 1
//          dimming 1.1 times a frame (:3578-3640, 8544-8690's fall-through).
//   Mist   Object59, 39: one frame in three a BITMAP_WATERFALL_2 SubType 2 (waterFall2, a soft
//          grey cloud): 40 frames, put down within 10 units, falling 3-5 units a frame and
//          slowing 0.1 a frame, jostled +-1 unit sideways each frame, Scale 0.6-1.1 + the box's,
//          growing 0.03 a frame; light 0.4, dimming 1.1 times a frame over its last ten, when
//          MU also holds it at the height it was born (Gravity 0; :3499-3530, 8476-8500).
//
// MU's light is the added sheet's colour; the sheets are JPEGs on black, drawn added. Every box
// stands upright, so a velocity turned by the box's angle is the velocity. The box's Scale rides
// in the anchor's `high` (an anchor has no scale of its own, and its reach stays 0 so Lamps
// lights nothing). MU's rates are per reference frame, 25 a second. Ours: only boxes within
// kReach of the camera's point throw. Nothing here reaches the sim.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <bgfx/bgfx.h>

#include "content/cooked.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class WaterfallSpray {
public:
    // Opens in Aida: the showing's `waterfall_drops`, `waterfall_spray` and `waterfall_mist`, and
    // the cooked town's world anchors of the three kinds.
    void open(const std::string& assetDir, const std::string& world, const content::CookedTown& town,
              content::Textures& textures);
    void shutdown();
    bool isOpen() const { return open_; }

    // One frame round `near`, the point the camera follows, in metres.
    void update(float seconds, const float near[3]);
    void gather(gfx::Effects& effects) const;

    size_t dropCount() const { return drops_.size(); }

private:
    enum class Kind : uint8_t { Drops, Burst, Mist };
    struct Drop {
        Kind kind = Kind::Drops;
        float at[3] = {0, 0, 0};
        float bornY = 0.0f;   // the mist's held height
        float speed = 0.0f;   // metres a second, up
        float age = 0.0f;     // reference frames
        float life = 1.0f;    // reference frames
        float scale = 1.0f;   // MU's Scale
        float spin = 0.0f;
    };
    struct Box {
        Kind kind = Kind::Drops;
        float at[3] = {0, 0, 0};
        float scale = 1.0f;
        float owed = 0.0f;  // seconds towards its next reference frame
    };
    void throwDrop(const Box& box);
    float unit();

    bool open_ = false;
    std::vector<Box> boxes_;
    std::vector<Drop> drops_;
    uint32_t seed_ = 0x51A7E2u;
    bgfx::TextureHandle sheets_[3] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
};

}  // namespace mu::game
