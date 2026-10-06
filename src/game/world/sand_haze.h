// Tarkan's sandstorm: MU's RenderOutSides (ZzzInterface.cpp:3677-3689), called first in
// RenderInterface, so over the 3D scene and under the HUD. Two quads cover the screen, added
// (GL_ONE, GL_ONE) at colour (0.3, 0.3, 0.25):
//   * sand01 (BITMAP_CHROME + 2), a soft grey cloud, 0.3 of the sheet across the screen, its U
//     slid (WorldTime % 100000) * 0.0002 -- 0.2 sheet a second;
//   * sand02 (BITMAP_CHROME + 3), sparse white specks, 3 by 2 repeats, slid 1 sheet a second.
// RenderBitmapUV skews each a quarter sheet from top to bottom (ZzzOpenglUtil.cpp:1729-1733),
// so the streaks lean. Nothing else in Tarkan makes its wind.
//
// Here the two are placed quads half a metre in front of the eye, wider than any screen, drawn
// in the effects pass; ours, a share of MU's colour, because the user likes these faint.
// Icarus wears the same two sheets as drifting cloud, cooler, slower and fainter (ours).
#pragma once

#include <string>

#include <bgfx/bgfx.h>

#include "content/texture.h"
#include "gfx/effects.h"
#include "gfx/renderer.h"

namespace mu::game {

struct HazeLook;

class SandHaze {
public:
    // Opens in Tarkan and Icarus: the showing's `sand` and `sand_fine`.
    void open(const std::string& assetDir, const std::string& world, content::Textures& textures);
    void shutdown();
    bool isOpen() const { return look_ && (bgfx::isValid(cloud_) || bgfx::isValid(specks_)); }

    void update(float seconds) { clock_ += seconds; }
    void gather(gfx::Effects& effects, const gfx::Camera& camera) const;

private:
    float clock_ = 0.0f;
    const HazeLook* look_ = nullptr;  // the world's look (sand_haze.cpp), set when open
    bgfx::TextureHandle cloud_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle specks_ = BGFX_INVALID_HANDLE;
};

}  // namespace mu::game
