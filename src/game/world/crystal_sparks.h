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
// The arm's second pass, mesh 0 again RENDER_CHROME7 | RENDER_DARK with BITMAP_CHROME, is
// lendCrystalChrome below. Ours: only points within kReach of the camera's point are drawn.
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

class Town;

// MU's second pass over the crystal (GM_Kanturu_1st.cpp:368-370): mesh 0 again, RENDER_CHROME7
// with BITMAP_CHROME (Effect/Chrome01). RenderMesh tests CHROME7 before DARK, so it is added,
// GL_ONE, GL_ONE, at the same BodyLight as the body (ZzzBMD.cpp:1409-1457), its coordinates
// u = v = (n.z + n.x) * 0.8 + WorldTime * 0.00006 (:1667-1671): a chrome slid over the facets
// by their normals, drifting 0.06 a second. Each glow part of the world show's crystal_chrome
// model takes a second part over the same triangles, a copy of its material with
// content::Material::chromePass 5 and Chrome01 for its sheet (fs_glow, shine.sh's
// chromePassUv). Called once the town's meshes are built.
void lendCrystalChrome(Town& town, const std::string& assetDir, const std::string& world,
                       content::Textures& textures);

class CrystalSparks {
public:
    // Opens where the world's show has crystal_sparks, and takes the showing's `spark_flash`
    // (Effect/Spark03).
    void open(const std::string& assetDir, const std::string& world, const content::CookedTown& town,
              content::Textures& textures);
    void shutdown();
    bool isOpen() const { return open_; }

    // Where the camera looks, in metres: what is near enough to draw.
    void update(float seconds, const float near[3]);
    void gather(gfx::Effects& effects) const;

    uint32_t pointCount() const { return uint32_t(points_.size()); }

private:
    struct Point {
        float at[3] = {0, 0, 0};
        float halfWidth = 1.2f;  // metres
        float colour[3] = {0.1f, 0.1f, 0.3f};
        // A pole's flare (content::EmitterKind::PoleFlare): MU's fLumi between low and high
        // on a sine of hz, scaling both its light and its Scale (fLumi / 3.2).
        bool flare = false;
        float low = 1.0f, high = 1.0f, hz = 0.0f;
    };
    std::vector<Point> points_;
    float near_[3] = {0, 0, 0};
    float clock_ = 0.0f;  // seconds, for the twinkle
    bool open_ = false;
    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle flareSheet_ = BGFX_INVALID_HANDLE;  // Effect/flare01, the showing's `flare`
};

}  // namespace mu::game
