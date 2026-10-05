// Big slow clouds drifting through a world's void, under the floors that stand over it. Ours:
// the user, 2026-10-02, of Blood Castle's chasm: 'lets ass some nice big subtle clouds on void
// zones'. MU has a kin of it there -- nine Object38 emitters rolling BITMAP_CLOUD and smoke round
// the bridge and the court (ZzzObject.cpp:3226-3240) -- but this is a layer, not those emitters.
//
// Three decks since 2026-10-05, each gathered into banks with open sky between them: big cloud
// masses from our own sheet (pipeline/cloud_sheet.py), 36-56 m across under the floor, wider and
// darker on the two decks below it, out to 165 m, over NoGround tiles round the camera, drifting
// and turning slowly, forming and thinning away over a minute. Cold grey, as the castle's light. Lava smoke's shape
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
    // Opens on the worlds that ask for it (Blood Castle, the Dungeon) and takes the cloud sheet; elsewhere it stays
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
        float fade = 0.0f;     // 0 to 1, moving only gradually: in at birth, out when leaving
        bool leaving = false;  // out of reach or over ground: fading out, never cut off
        bool alive = false;
        int layer = 0;    // into kLayers: 0 under the floor, deeper and wider after
        int variant = 0;  // which of the sheet's four clouds
    };
    bool spawn(Wisp& wisp, const float near[3], bool anyAge);
    bool voidAt(float x, float z) const;
    float bank(float x, float z, int layer) const;
    bool clearUnder(float x, float z, float half, int layer) const;
    float unit();

    const content::Ground* ground_ = nullptr;
    int size_ = 0;
    float metresPerTile_ = 1.0f;
    float floor_ = 0.0f;  // the walkable ground's median height, metres
    float colour_[3] = {1.0f, 1.0f, 1.0f};
    std::vector<Wisp> wisps_;
    uint32_t seed_ = 0xC10D5u;
    float clock_ = 0.0f;  // seconds since open, carrying each layer's banks along
    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
};

}  // namespace mu::game
