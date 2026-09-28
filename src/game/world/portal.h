// Noria's decorative warp: five discs of added light turning over a corner of the map.
//
// MapManager.cpp:88-103 loads Data/Npc/warp01..03 for Noria alone and stands one MODEL_WARP at
// tile (223, 30), which no gate in OpenMU Version075 or mu.db leads to -- set dressing. Its
// creation (ZzzObject.cpp:4722) raises five effects 350 units over it, stacked along MU's y:
//
//     WARP at +0, WARP2 at +4, WARP at +8, WARP2 at +12, WARP3 at +20
//
// each one `BlendMesh -2` (every mesh added) and living for ever (ZzzEffect.cpp:553): WARP and
// WARP2 at a Scale of 1.3 to 1.79 and a `Gravity` of 0 to 7.9, WARP3 at 0.6. Every frame
// (Move_MODEL_WARP3, MoveHandlers.cpp:454) each turns about its Angle[1] by `4 + Gravity`
// degrees and takes the colour `sin(WorldTime * 0.0011 / 0.0017 / 0.0013) * 0.2 + 0.01` in
// its three channels -- a faint drifting tint that goes to black and back.
//
// The meshes are cooked with the missiles (source/effects/missiles/Warp01..03). They are drawn
// through the transparent pass as textured triangles rather than as model instances, because
// the glow shader carries one brightness per instance and MU's warp is a COLOUR per disc. 98
// triangles a ring, 2 for the centre: 396 a frame, and none when the hero is far away.
//
// Not drawn: the BITMAP_SPARK+1 shimmer MoveObject throws at it (ZzzObject.cpp:3967), which is
// owed. Nothing here reaches the sim.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <bgfx/bgfx.h>

#include "content/ground.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class Portal {
public:
    // Stands the warp if `world` has one (Noria alone) and its three meshes are cooked.
    bool open(const std::string& assetDir, const std::string& world,
              const content::Ground& ground, content::Textures& textures);
    void shutdown();
    void update(float seconds);
    // Into the transparent pass, while `near` (the hero) is within sight of it.
    void gather(gfx::Effects& effects, const float near[3]) const;
    bool isOpen() const { return !discs_.empty(); }

private:
    struct Shape {
        std::vector<float> positions;  // x, y, z a vertex, metres, the cooked mesh's own
        std::vector<float> uvs;        // u, v a vertex
        std::vector<uint32_t> indices;
        bgfx::TextureHandle sheet = BGFX_INVALID_HANDLE;
    };
    struct Disc {
        int shape = 0;
        float at[3] = {0, 0, 0};
        float scale = 1.0f;
        float turn = 0.0f;     // degrees a reference frame: 4 + Gravity
        float angle = 0.0f;    // radians, about the disc's own horizontal axis
    };
    uint32_t next();

    std::vector<Shape> shapes_;
    std::vector<Disc> discs_;
    float centre_[3] = {0, 0, 0};
    float clock_ = 0.0f;   // seconds, WorldTime's own
    float colour_[3] = {0, 0, 0};
    uint32_t seed_ = 0x3AB1E5u;
};

}  // namespace mu::game
