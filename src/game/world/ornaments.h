// What MU hangs off a placed object's bones without drawing it as a mesh: RenderObjectVisual's
// per-type switch for Lorencia in ZzzObject.cpp, the lines after the ones that slide a sheet.
//
// 1. **The fountain's spray.** MODEL_WATERSPOUT: `if (rand_fps_check(2))` a BITMAP_SMOKE puff
//    at bone 1 (the spout's mouth) and one at bone 4 (where the fall lands). MU2's Spray.cs
//    traced every number and threw only the landing -- the mouth's puff read as smoke off the
//    statue's beak -- and this does as MU2 did; the mouth's frame is kept in ornaments.cpp.
// 2. **The merchant animal's two lanterns.** MODEL_MERCHANT_ANIMAL01: a BITMAP_LIGHT sprite at
//    bones 48 and 57, `Luminosity * 5` in scale and `(0.6, 0.3, 0.1) * Luminosity` in colour,
//    Luminosity re-rolled 0.7 to 1.0 every frame MU draws. MU2 never drew these.
//
// Both ride the pose Sway just computed, so each is thrown only where Sway posed the object
// this frame -- in sight, as MU only runs RenderObjectVisual for objects it draws. Nothing here
// reaches the sim or its seeded log.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <bgfx/bgfx.h>

#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class Sway;
class Town;

class Ornaments {
public:
    // Finds the fountains and the merchant animals among `town`'s placements and carries MU's
    // points into their bones' own frames. The sheets are the showing's `smoke01` (MU's
    // BITMAP_SMOKE) and `light` (Effect/flare01, MU's BITMAP_LIGHT); without one, that
    // ornament is not drawn and the log says so.
    bool open(const std::string& assetDir, const Town& town, content::Textures& textures);
    void shutdown();

    // Steps the puffs, throws new ones where a fountain was posed, and re-rolls the lanterns.
    // After Sway::update, so the bones are this frame's.
    void update(float seconds, const Sway& sway);
    // Into the transparent pass.
    void gather(gfx::Effects& effects, const Sway& sway) const;

    uint32_t puffCount() const { return uint32_t(puffs_.size()); }

private:
    // A point on a bone and the two directions MU scatters across, all in the bone's OWN
    // frame -- carried there once from the model's bind pose, because the glb's bones are not
    // oriented as the .bmd's are (bone 4's Y is up the fall in MU and along the model's X in
    // the glb) and MU's offsets are written in MU's bone frames.
    struct Anchor {
        uint32_t townIndex = 0;
        int bone = -1;
        float point[3] = {0, 0, 0};
        float across[2][3] = {{0, 0, 0}, {0, 0, 0}};
    };
    struct Spout {
        Anchor anchor;
        float clock = 0.0f;  // reference frames owed a coin flip
    };
    struct Lantern {
        Anchor anchor;
    };
    struct Puff {
        float position[3] = {0, 0, 0};
        float age = 0.0f;
        float scale = 0.0f;  // MU's o->Scale at birth; the sheet is 64 units at 1
        float spin = 0.0f;
    };

    uint32_t next();
    float unit();

    std::vector<Spout> spouts_;
    std::vector<Lantern> lanterns_;
    std::vector<Puff> puffs_;
    float luminosity_ = 1.0f;  // this frame's roll, shared by every lantern as MU's is
    float lanternWait_ = 0.0f;
    uint32_t seed_ = 0x51AB1Eu;
    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle light_ = BGFX_INVALID_HANDLE;
};

}  // namespace mu::game
