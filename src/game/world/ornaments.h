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
// 3. **The mill's fall.** House05's water sheet (ston02, BlendMesh 2) runs off its flume and
//    drops into the river. MU throws nothing there -- only the sheet slides -- so the fountain's
//    landing puff is thrown where this fall lands too, on the same numbers. Ours, asked for.
//
// 4. **Noria's glows.** RenderObjectVisual's `case WD_3NORIA`: BITMAP_LIGHT sprites at the
//    origins of named bones -- Object02's three flower heads, the flower-lamp's bud, the big
//    tree's four, the bluebell's, the Chaos Machine's five white ones -- in (0.4, 0.7, 1.0)
//    times the frame's Luminosity, and the machine's star: two BITMAP_LIGHTNING+1 sprites at
//    bone 57 spinning opposite ways at `(int)(WorldTime * 0.1) % 360`, a hundred degrees a
//    second. The table is kNoriaGlows in ornaments.cpp; docs/noria-effects.md, items 2 and 6.
//
// The first two ride the pose Sway just computed, so each is thrown only where Sway posed the
// object this frame -- in sight, as MU only runs RenderObjectVisual for objects it draws. The
// mill does not sway; its fall is fixed in the world at open and always thrown. Nothing here
// reaches the sim or its seeded log.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <bgfx/bgfx.h>

#include "content/ground.h"
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
    // ornament is not drawn and the log says so. The ground keeps the mill's fall's landing
    // from being put under the land it lands on. `world` gates the tables that are one map's:
    // Noria's glows are named Object02..Object40, names every numbered world uses.
    // RenderObject draws Devias's type 100 only while the hero is level 50, or 33 for a Dark
    // Knight (ZzzObject.cpp:3446-3460); World says which each frame.
    void setBeaconSeen(bool seen) { beaconSeen_ = seen; }
    bool open(const std::string& assetDir, const std::string& world, const Town& town,
              const content::Ground& ground, content::Textures& textures);
    void shutdown();

    // Steps the puffs, throws new ones where a fountain was posed, and re-rolls the lanterns.
    // After Sway::update, so the bones are this frame's.
    void update(float seconds, const Sway& sway);
    // Into the transparent pass.
    void gather(gfx::Effects& effects, const Sway& sway) const;

    uint32_t puffCount() const { return uint32_t(puffs_.size()); }
    // Where the fountain nearest `from` stands, measured flat, into `at`; false when the town
    // has none. What its water is heard from (Play::hearWorld).
    bool nearestFountain(const float from[3], float at[3]) const;
    // The Chaos Machine's spark bursts due this frame, at bone 58, in world metres: MU's
    // CreateJoint(BITMAP_JOINT_SPARK) and CreateParticle(BITMAP_SPARK), eight pairs a burst,
    // which the caller throws through the forge (game/fx/forge.h), the smith's own recipe.
    size_t strikeCount() const { return strikeCount_; }
    const float* strikeAt(size_t i) const { return strikes_[i]; }

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
    // A BITMAP_LIGHT (or, sheet 1, a BITMAP_LIGHTNING+1) on a bone: its size is CreateSprite's
    // Scale over the 64-texel sheet, its colour times the frame's Luminosity unless `steady`,
    // and `spin` degrees a second (0 for none).
    struct Lantern {
        Anchor anchor;
        float scale = 1.0f;
        float colour[3] = {1.0f, 1.0f, 1.0f};
        bool steady = false;
        bool swells = false;  // its size breathes with Luminosity too: the merchant animal's
        int sheet = 0;
        float spin = 0.0f;
    };
    // A fall on a placement that never moves: its landing and scatter already in the world.
    struct Fall {
        float point[3] = {0, 0, 0};
        float across[2][3] = {{0, 0, 0}, {0, 0, 0}};
        float reach[2] = {0, 0};  // half the scatter along each direction, metres
        float clock = 0.0f;
    };
    // Each Waterspout01's placement, in world metres: where its water is heard from.
    struct Place {
        float at[3] = {0, 0, 0};
    };
    std::vector<Place> fountains_;
    struct Puff {
        float position[3] = {0, 0, 0};
        float age = 0.0f;
        float scale = 0.0f;  // MU's o->Scale at birth; the sheet is 64 units at 1
        float spin = 0.0f;
    };

    // A BITMAP_SHINY glint: subtype 0 twinkles, 1 twinkles smaller and turns (ZzzEffectParticle).
    struct Glint {
        float position[3] = {0, 0, 0};
        float age = 0.0f;  // reference frames
        float spin = 0.0f;
        bool small = false;
    };
    // A bone that throws on a roll: `every` is rand_fps_check's N, `clock` the frames owed.
    struct Thrower {
        Anchor anchor;
        int every = 1;
        float clock = 0.0f;
        bool sparks = false;  // bone 58's bursts, else a glint pair
    };
    uint32_t next();
    float unit();

    std::vector<Spout> spouts_;
    std::vector<Lantern> lanterns_;
    // The Lost Tower's slab flares (Object10): a BITMAP_LIGHT at each slab's own origin, which
    // stands still, so it is the placement's point and needs no pose. See ornaments.cpp.
    struct Flare {
        float at[3] = {0, 0, 0};
    };
    std::vector<Flare> flares_;
    // Devias's Lost Tower beacon: type 100 is a hidden placement (HiddenMesh -2), so there is
    // nothing posed to hang it on, and it stands at the placement's own point. Shown only to a
    // hero MuMain would show it to -- see setBeaconSeen.
    bool beacon_ = false;
    bool beaconSeen_ = false;
    std::vector<Fall> falls_;
    std::vector<Puff> puffs_;
    // The Lost Tower beacon's mist (ours): pale wisps rising through its light.
    struct Mist {
        float position[3] = {0, 0, 0};
        float drift[2] = {0, 0};  // metres a second, x and z
        float age = 0.0f;         // seconds
        float spin = 0.0f, turn = 0.0f;
    };
    std::vector<Mist> mist_;
    float mistClock_ = 0.0f;
    std::vector<Glint> glints_;
    std::vector<Thrower> throwers_;
    float strikes_[4][3] = {};
    size_t strikeCount_ = 0;
    float luminosity_ = 1.0f;  // this frame's roll, shared by every lantern as MU's is
    float lanternWait_ = 0.0f;
    float spun_ = 0.0f;  // seconds, for the machine's star: WorldTime's own clock, wrapped
    uint32_t seed_ = 0x51AB1Eu;
    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle light_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle lightning_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle magic_ = BGFX_INVALID_HANDLE;  // Effect/Magic_Ground2, MU's BITMAP_MAGIC+1
    bgfx::TextureHandle shiny_ = BGFX_INVALID_HANDLE;  // Effect/Shiny01, MU's BITMAP_SHINY  // lightning2, MU's BITMAP_LIGHTNING+1
};

}  // namespace mu::game
