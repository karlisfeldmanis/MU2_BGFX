// The elf's arrow in the air: MU's missile models out of Data/Skill, drawn from the bow to what
// she shot at. Sprint 15, step 2.
//
// Which model a weapon throws is MuMain's `CreateArrow` switch (ZzzEffectMagicSkill.cpp:220-251),
// which reads the WEAPON and not the ammunition:
//   * every 0.75 bow -> MODEL_ARROW, Arrow01: a wooden shaft and an additive fire sprite on
//     its tail (`BlendMesh = 1`), and the only one that sheds embers (`Move_MODEL_ARROW` lays
//     a BITMAP_FIRE a frame; the other seven share `Move_MODEL_ARROW_STEEL`, which lays none);
//   * the Crossbow and the Golden Crossbow -> MODEL_ARROW_STEEL, ArrowSteel01, solid;
//   * the Arquebus -> MODEL_ARROW_SAW, ArrowSaw01, solid;
//   * the Light Crossbow -> MODEL_ARROW_LASER, ArrowLaser01, additive.
// **Ours:** the Elven Bow's MODEL_ARROW_V, the Chaos Nature Bow's MODEL_ARROW_NATURE and the
// Serpent, Bluewing and Aquagold crossbows' bolts are not built; they throw the wooden arrow or
// the steel bolt until they are.
//
// Where and how it flies. MU puts it at the shooter's feet plus (-10, -60, 135) turned by her
// facing -- 1.35 m up, 0.6 m ahead -- at `Direction[1] = -70` units a reference frame, 17.5
// tiles a second, and ends it on `CheckClientArrow`'s one tile from a body. **Ours:** MU's
// arrow flies level and is not steered; this one is aimed at the target's middle and follows
// it, because a level arrow at 1.35 m passes over a spider, and the realm has already decided
// it hits. The speed and the tile short are the realm's own (`kArrowTilesPerSecond`), so the
// arrow vanishes on the tick the blow lands.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "content/showing.h"
#include "content/texture.h"
#include "game/fx/effect_mesh.h"
#include "gfx/effects.h"

namespace mu::game {

class Arrows {
public:
    enum Model : uint8_t { Wood, Steel, Saw, Laser, kModels };

    // The four models and their sheets out of effects/missiles, and the ember strip (`fire`)
    // off the cooked showing. False only when no model could be had at all.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table, float metresPerTile);
    void shutdown();

    // Which model a weapon throws, by its bow-group number (MuMain's CreateArrow).
    static Model modelFor(int32_t group, int32_t number);

    // One let go from `from` at the body `whom`, aimed first at `to`.
    // `shooter`, when not 0, is reported in landed() the frame the arrow reaches `whom`: a
    // monster's shot, whose blow is shown where it lands (Play::hunterShot).
    void loose(const float from[3], const float to[3], uint32_t whom, Model model,
               uint32_t shooter = 0);
    const std::vector<uint32_t>& landed() const { return landed_; }

    // `middle` answers where a body's middle is drawn now, false once it is not drawn.
    void update(float seconds, const std::function<bool(uint32_t, float*)>& middle);
    void gather(gfx::Effects& effects) const;

    uint32_t flying() const;

private:
    std::vector<uint32_t> landed_;  // this update's shooters whose arrow reached its body
    struct Part {
        std::vector<EffectCorner> triangles;
        bgfx::TextureHandle sheet = BGFX_INVALID_HANDLE;
        gfx::Blend blend = gfx::Blend::Alpha;
    };
    struct Shape {
        std::vector<Part> parts;
    };
    struct Shot {
        bool alive = false;
        Model model = Wood;
        uint32_t whom = 0;
        uint32_t shooter = 0;
        float at[3];
        float along[3];  // unit, the way it is going
        float to[3];     // where the body's middle was last seen
        float left;      // reference frames
        float flown;     // metres since the last ember
        float glow;      // this frame's Luminosity roll
    };
    struct Ember {
        bool alive = false;
        float at[3];
        float velocity[3];  // metres a second
        float size;         // metres
        float spin;
        float rise;
        float left;         // reference frames
    };

    // ---- MU's numbers (Arrow01.json and the three beside it) ---------------------------------
    static constexpr float kReference = 25.0f;
    static constexpr float kUnit = 0.01f;
    static constexpr float kScale = 0.8f;             // `o->Scale = 0.8f`
    static constexpr float kFrames = 30.0f;           // `o->LifeTime = 30`
    static constexpr float kTilesASecond = 17.5f;     // the realm's kArrowTilesPerSecond
    static constexpr float kStopsShort = 1.0f;        // tiles: CheckCharacterRange(o, 100)
    // The embers: MU's BITMAP_FIRE sub-type 0, the numbers `fx/meteor.h` already carries.
    static constexpr float kEmberSpacingUnits = 70.0f;  // one a reference frame at 70 a frame
    static constexpr float kEmberFrames = 24.0f;
    static constexpr int kEmberCells = 4;
    static constexpr int kEmberHeld = 6;
    static constexpr float kSmallestEmber = 1.28f, kLargestEmber = 1.92f;  // x the sheet's 64
    static constexpr float kEmberSheetUnits = 64.0f;
    static constexpr float kEmberShrink = 0.04f;
    static constexpr float kEmberRise = 0.004f;
    static constexpr float kEmberRiseScale = 10.0f;
    static constexpr float kSlowestDrift = 3.2f, kFastestDrift = 4.8f;  // units a frame
    // `(0.8, 0.5, 0.2) x Luminosity`, Luminosity `(rand() % 4 + 7) * 0.1` (ZzzEffect.cpp:6645).
    static constexpr float kEmberLight[3] = {0.8f, 0.5f, 0.2f};

    static constexpr int kShots = 16;
    static constexpr int kEmbers = 256;

    Shape shapes_[kModels];
    bgfx::TextureHandle emberSheet_ = BGFX_INVALID_HANDLE;
    float metresPerTile_ = 1.0f;
    Shot shots_[kShots];
    Ember embers_[kEmbers];
    uint32_t dice_ = 0x41525257u;

    float roll();  // 0..1
    void shed(const Shot& shot);
};

}  // namespace mu::game
