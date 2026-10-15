// What a death leaves on the ground: MU's CreateBlood, two dark stains by the head of a body
// whose death clip has just played out (ZzzEffectBlurSpark.cpp:445-468, called from
// EtcStopAnimationSetting and PlayerStopAnimationSetting, ZzzCharacter.cpp:3467-3527).
//
// What is MU's: two BITMAP_BLOOD pointers (blood01.tga), each up to 50 units either way of the
// head bone, turned at random, 0.8-1.2 tiles across, laid on the land by
// RenderTerrainAlphaBitmap; each lives 50-80 frames, grows 0.004 of a tile a frame and is lit
// (0.1, 0, 0), so the sheet's red reads nearly black, and fades over its last 50
// (CreatePointer and MovePointers, ZzzEffectPointer.cpp:20-95). Alpha-blended, by the sheet's
// own alpha.
//
// Not MU's: the 0.25 alpha test MU draws the sheet under, which eats the stain's rim as it
// fades; here it thins evenly. The offset is taken on the ground's plane, where MU turns it by
// the head bone (a lying head's plane is the ground's, near enough).
//
// Who bleeds is the caller's (Play::sandOnDeath): a player, and the monsters whose recipe carries
// the "bleeds" show tag.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::content {
class Ground;
}

namespace mu::game {

class Stains {
public:
    // blood_pool (effects/drop/blood01.png) from the showing table. False when it is not there.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    // Two stains about `head`, world metres; only its x and z are read.
    void spill(const float head[3]);
    void clear() { stains_.clear(); }
    void update(float seconds);
    void gather(gfx::Effects& effects, const content::Ground& ground) const;

private:
    struct Stain {
        float x = 0.0f, z = 0.0f;
        float turn = 0.0f;    // radians
        float across = 1.0f;  // tiles, growing
        float left = 0.0f;    // reference frames
    };
    float roll(float low, float high);

    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
    std::vector<Stain> stains_;
    uint32_t dice_ = 0x2545F491u;
};

}  // namespace mu::game
