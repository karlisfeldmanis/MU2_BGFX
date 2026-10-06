// A thin smoke lying on the Lost Tower's lava. Ours, all of it: MU draws the lava as a sliding
// sheet and nothing over it (the user, 2026-10-01: 'can we add minimal smoke layer sitting of
// top of lava?').
//
// A hundred and forty wisps of smoke02, each a flat sheet 5-7 m across hanging half a metre
// over a lava tile near the camera, overlapping into one thin layer over all the lava in view,
// drifting slowly one way and turning slower still, faded in and out over ten seconds or so. Flat rather than facing the eye, because a
// billboard that low cuts into the lava with a hard line (the slab flares did on the floor),
// and a layer is what was asked for. Lit from below: warm grey, not the dark of a fire's smoke.
// The user dislikes busy effects, so it is faint and slow; nothing here flickers.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/ground.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class LavaSmoke {
public:
    // Finds the world's lava tiles (its `TileWater01`) and takes smoke02. Only the Lost Tower
    // has lava; anywhere else this stays closed.
    void open(const std::string& assetDir, const std::string& world, const content::Ground& ground,
              content::Textures& textures);
    void shutdown();
    bool isOpen() const { return !lava_.empty(); }

    // One frame round `near`, the point the camera follows, in metres.
    void update(float seconds, const float near[3]);
    void gather(gfx::Effects& effects) const;

private:
    struct Wisp {
        float at[3] = {0, 0, 0};
        float drift[2] = {0, 0};  // metres a second, x and z
        float age = 0.0f, life = 0.0f;
        float size = 1.0f;        // half width, metres
        float turn = 0.0f, spin = 0.0f;
        bool alive = false;
    };
    bool spawn(Wisp& wisp, const float near[3], bool anyAge);
    bool lavaAt(float x, float z) const;
    // Whether the ground under a sheet `half` across from (x, z) stays under `height`.
    bool clear(float x, float z, float half, float height) const;
    float unit();

    const content::Ground* ground_ = nullptr;
    std::vector<uint8_t> lava_;  // a byte a tile, [row * size + column]
    int size_ = 0;
    float metresPerTile_ = 1.0f;
    std::vector<Wisp> wisps_;
    uint32_t seed_ = 0x5A0E11u;
    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
};

}  // namespace mu::game
