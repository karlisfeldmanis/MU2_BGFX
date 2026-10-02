// Big slow clouds drifting through a world's void, under the floors that stand over it. Ours:
// the user, 2026-10-02, of Blood Castle's chasm: 'lets ass some nice big subtle clouds on void
// zones'. MU has a kin of it there -- nine Object38 emitters rolling BITMAP_CLOUD and smoke round
// the bridge and the court (ZzzObject.cpp:3226-3240) -- but this is a layer, not those emitters.
//
// Flat sheets of smoke02, 14-22 m across, a few metres below the castle's floor over NoGround
// tiles near the camera, overlapping into one faint layer, drifting one way and turning slowly,
// faded in and out over half a minute. Cold grey, as the castle's light. Lava smoke's shape
// (game/world/lava_smoke.h); nothing here flickers.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/ground.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class VoidClouds {
public:
    // Opens on the worlds that ask for it (Blood Castle) and takes smoke02; elsewhere it stays
    // closed. The layer's height is the walkable ground's middle height less its depth.
    void open(const std::string& assetDir, const std::string& world, const content::Ground& ground,
              content::Textures& textures);
    void shutdown();
    bool isOpen() const { return ground_ != nullptr; }

    // One frame round `near`, the point the camera follows, in metres.
    void update(float seconds, const float near[3]);
    void gather(gfx::Effects& effects) const;

private:
    struct Wisp {
        float at[3] = {0, 0, 0};
        float drift[2] = {0, 0};
        float age = 0.0f, life = 0.0f;
        float size = 1.0f;  // half width, metres
        float turn = 0.0f, spin = 0.0f;
        bool alive = false;
    };
    bool spawn(Wisp& wisp, const float near[3], bool anyAge);
    bool voidAt(float x, float z) const;
    float unit();

    const content::Ground* ground_ = nullptr;
    int size_ = 0;
    float metresPerTile_ = 1.0f;
    float floor_ = 0.0f;  // the walkable ground's median height, metres
    std::vector<Wisp> wisps_;
    uint32_t seed_ = 0xC10D5u;
    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
};

}  // namespace mu::game
