// Kanturu's crystal sparks: MU's RenderKanturu1stObjectVisual arm for type 95, Object96, the
// Ruins' 128 crystals, after the characters (GM_Kanturu_1st.cpp:361-381): at each of the
// crystal's first ten bones a BITMAP_SPARK+1 sprite (Effect/Spark03, 32 texels), Scale 7.5,
// light (0.1, 0.1, 0.3), drawn added (SubType 0, EnableAlphaBlend; zzzeffectsprite.cpp:161-164)
// and 32 * 7.5 = 240 units wide: a dim blue halo 2.4 m across round every point of the crystal.
//
// The crystal holds still in MU (one key), so its bones never move: their points are the
// recipe's `emitters` of kind "spark" (content::EmitterKind::CrystalSpark), the rig's rest
// positions in the model's own frame, and each placement's are laid here once at open, turned
// AND scaled by the placement (MU's BoneTransform carries BodyScale; a fire's offset does not).
//
// Not carried: the arm's second pass, mesh 0 again RENDER_CHROME7 | RENDER_DARK with
// BITMAP_CHROME. Ours: only points within kReach of the camera's point are drawn.
// Nothing here reaches the sim.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <bgfx/bgfx.h>

#include "content/cooked.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class CrystalSparks {
public:
    // Opens where the world's show has crystal_sparks, and takes the showing's `spark_flash`
    // (Effect/Spark03).
    void open(const std::string& assetDir, const std::string& world, const content::CookedTown& town,
              content::Textures& textures);
    void shutdown();
    bool isOpen() const { return open_; }

    // Where the camera looks, in metres: what is near enough to draw.
    void update(const float near[3]);
    void gather(gfx::Effects& effects) const;

    uint32_t pointCount() const { return uint32_t(points_.size()); }

private:
    struct Point {
        float at[3] = {0, 0, 0};
        float halfWidth = 1.2f;  // metres
        float colour[3] = {0.1f, 0.1f, 0.3f};
    };
    std::vector<Point> points_;
    float near_[3] = {0, 0, 0};
    bool open_ = false;
    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
};

}  // namespace mu::game
