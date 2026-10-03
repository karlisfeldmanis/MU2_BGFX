// Blood Castle's air: faint white sparks rising round the hero. MU's, MoveObjectSetting's castle
// arm (ZzzObject.cpp:4365-4381): one frame in four, a BITMAP_FLARE particle, SubType 3 at Scale
// 0.19, born 2.5-3 m over the hero within -3 to +6 m of him in each of x and y, white; it lives
// 60 frames, climbs 1-5 units a frame and turns round its birth point 40 units out at 0.1 rad a
// frame, shrinking 0.002 a frame (ZzzEffectParticle.cpp:763-800, 4995-5013).
//
// And its nine mist emitters, Object38's placements (type 37, no .bmd; cooked as smoke anchors):
// MU throws, about one frame in eight each, BITMAP_ADV_SMOKE puffs that climb faster as they go
// and swell, fading over their second, and with every other a pink-white BITMAP_FLARE rising
// (1, 0.8, 0.8), SubType 4 (ZzzObject.cpp:3227-3242; ZzzEffectParticle.cpp:3290-3335, 8217-8265).
// Here a puff is MU's white smoke01 added, cold and dim -- the user likes these faint.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <bgfx/bgfx.h>

#include "content/cooked.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class CastleSparks {
public:
    // Opens in Blood Castle and takes the showing's `flare` (Effect/Flare, MU's BITMAP_FLARE).
    void open(const std::string& assetDir, const std::string& world, const content::CookedTown& town,
              content::Textures& textures);
    void shutdown();
    bool isOpen() const { return open_; }

    // One frame round `hero`, his feet, in metres.
    void update(float seconds, const float hero[3]);
    // A little dust thrown up along a line: the drawbridge landing (game/world/drawbridge.h).
    // `count` puffs at `at`, spread `spread` metres either way along x, slow and faint.
    void dust(const float at[3], int count, float spread);
    void gather(gfx::Effects& effects) const;

private:
    struct Puff {
        float at[3] = {0, 0, 0};
        float rise = 0.0f;   // metres a second, climbing faster as it goes
        float drift[2] = {0, 0};
        float age = 0.0f, life = 1.0f;
        float size = 1.0f;   // half width, metres
        float spin = 0.0f;
        bool flare = false;  // the pink-white flare rather than a puff
        bool dust = false;   // a landing's dust: rises slowly and does not speed up
    };
    struct Vent {
        float at[3] = {0, 0, 0};
        float owed = 0.0f;
        int count = 0;
    };
    std::vector<Vent> vents_;
    std::vector<Puff> puffs_;
    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;

    struct Spark {
        float start[3] = {0, 0, 0};
        float height = 0.0f;  // metres climbed
        float climb = 0.0f;   // metres a second
        float phase = 0.0f;   // MU's Velocity[0], the turn's start
        float age = 0.0f;     // seconds
        float scale = 0.19f;
    };
    float unit();

    bool open_ = false;
    std::vector<Spark> sparks_;
    float owed_ = 0.0f;  // seconds towards the next birth
    uint32_t seed_ = 0x5BA4Cu;
    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
};

}  // namespace mu::game
