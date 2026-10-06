// The raid's tells on the ground (docs/golden-dragon-raid.md §1, "a clear telegraph before every
// big hit"): where the Golden Dragon's next blow lands, marked before it lands.
//
// **Ours, all of it.** MU marks nothing on the ground before a monster's blow -- its bosses roar
// and swing. The raid's design asks for a tell before each Hazard (sim/raid.h), and this is it:
//
//   * a **disc** for what falls on a tile or rings it -- a strafe's fire, a meteor, the roar's
//     shock, the Inferno's whole field -- a faint fill under a soft darker rim, a red stain on the
//     land that deepens from barely there over the tell, darkest as it lands, and fades;
//   * a **cone** for the Breath, the same fill laid out along its sixty degrees from the mouth;
//   * a **pool**'s char while it burns, under its embers;
//   * a **shadow**, the Inferno's shelter: the land darkened (Blend::Minus) under each wing.
//
// Subtle and blurred rather than loud (the user's taste): a dark red stain, alpha-blended, no
// glow, no lines, no symbols. Laid on the land as the click marker is (fx/marker.h): flat parts cut into
// a grid whose corners each take the ground's height.
#pragma once

#include <string>
#include <vector>

#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::content {
class Ground;
}

namespace mu::game {

class Omen {
public:
    // Field: the Inferno's whole ground, dimmed rather than stained, so its shadows read.
    enum class Shape : uint8_t { Disc, Cone, Pool, Shadow, Field };

    // omen_disc.png and omen_blot.png from assets/effects/raid. False when neither is there.
    bool open(const std::string& assetDir, content::Textures& textures);
    void shutdown();

    // A mark at (x, z) world metres, `radius` metres out, told `tell` seconds before it lands
    // and held `hold` seconds after (a breath's burning, a pool's twelve). A cone looks along
    // `yaw` (the drawing's, as a body's) `half` radians either side.
    void tell(Shape shape, float x, float z, float radius, float tell, float hold, float yaw = 0.0f,
              float half = 0.0f);
    void clear() { marks_.clear(); }
    void update(float seconds);
    void gather(gfx::Effects& effects, const content::Ground& ground) const;

private:
    struct Mark {
        Shape shape = Shape::Disc;
        float x = 0.0f, z = 0.0f, radius = 1.0f, yaw = 0.0f, half = 0.0f;
        float tell = 0.0f, hold = 0.0f, age = 0.0f;
    };
    void disc(gfx::Effects& effects, const content::Ground& ground, const Mark& mark,
              bgfx::TextureHandle sheet, float alpha, bool dark) const;
    void cone(gfx::Effects& effects, const content::Ground& ground, const Mark& mark,
              float alpha) const;

    bgfx::TextureHandle disc_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle blot_ = BGFX_INVALID_HANDLE;
    std::vector<Mark> marks_;
};

}  // namespace mu::game
