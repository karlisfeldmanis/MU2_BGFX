// A mesh an effect throws -- the meteor's rock and flame, Power Wave's curtain -- read out of
// MU's own exported .obj and drawn into the transparent pass as triangles, each a placed quad
// whose last two corners coincide. Shared so each effect does not carry its own parser.
#pragma once

#include <string>
#include <vector>

#include "gfx/effects.h"

namespace mu::game {

// A corner of an .obj triangle, in metres, with its UV.
struct EffectCorner {
    float x, y, z, u, v;
};

// Loads one .obj, scaled to metres, optionally keeping one named group only, as corners in
// threes. MU's Y runs south and this engine's Z north, so Z is negated here, at the edge.
bool loadEffectObj(const std::string& path, float scale, const std::string& groupFilter,
                   std::vector<EffectCorner>& out);

// Draws `tris` turned by a whole basis -- the model's X, Y and Z land on `x`, `y` and `z` --
// and scaled, at `at`. `uShift` slides the sheet along U, for a sheet that streams along its
// model (the sampler must wrap).
void submitEffectAlong(gfx::Effects& effects, const std::vector<EffectCorner>& tris,
                       bgfx::TextureHandle sheet, gfx::Blend blend, const float at[3],
                       const float x[3], const float y[3], const float z[3], float scale,
                       const float colour[3], float alpha, float uShift = 0.0f);

}  // namespace mu::game
