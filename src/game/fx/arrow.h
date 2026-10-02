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
//
// **Ours:** the wooden arrow also burns at its tail -- small licks of the same `fire` strip,
// laid every few centimetres where the sprite is, rising and fading in a third of a second, at
// the shot's Luminosity roll. MU's embers are 0.7 m apart and drift behind; at 17.5 tiles a
// second they read as a trail and not as an arrow on fire (the user: 'minimal fire emitter').
// And a thin smoke behind the flame (the user: 'and minimal smoke', then 'actual smoke, not fire
// color smoke, but subtle and has to disappear fast'): smoke02 wisps every third of a metre,
// ash grey as the lamps' cooled smoke (world/lamps.cpp), born clear of the flame and gone in
// half a second. The licks rise slowly, so the flame stays at the tail and does not stand in
// for an orange plume. Single shot and the fan alike: every wooden arrow.
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
        float licked;    // metres since the last lick
        float smoked;    // metres since the last wisp
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
    struct Lick {  // ours: the tail's own flame
        bool alive = false;
        float at[3];
        float velocity[3];  // metres a second
        float size;         // metres
        float spin;
        float glow;
        float left;         // reference frames
    };
    struct Wisp {  // ours: the smoke behind the flame
        bool alive = false;
        float at[3];
        float spin;
        float age;          // reference frames
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

    // ---- Ours: the tail's licks ----------------------------------------------------------------
    static constexpr float kLickSpacing = 0.12f;   // metres of flight between licks
    static constexpr float kLickBehind = 0.22f;    // metres behind the arrow's origin: the
                                                   // sprite's middle (loaded z 0 to -0.41 m)
    static constexpr float kLickFrames = 8.0f;
    static constexpr float kSmallestLick = 0.16f, kLargestLick = 0.28f;  // metres
    static constexpr float kLickRise = 0.3f;       // metres a second
    static constexpr float kLickJitter = 0.15f;    // metres a second, either way across

    // ---- Ours: the smoke behind it ---------------------------------------------------------------
    static constexpr float kWispSpacing = 0.35f;   // metres of flight between wisps
    static constexpr float kWispBehind = 0.55f;    // metres behind the origin, past the flame
    static constexpr float kWispFrames = 12.0f;
    static constexpr float kWispBorn = 0.15f, kWispGrown = 0.45f;  // metres across
    static constexpr float kWispRise = 0.25f;      // metres a second
    static constexpr float kWispGrey[3] = {0.30f, 0.30f, 0.32f};
    static constexpr float kWispAlpha = 0.30f;

    static constexpr int kShots = 16;
    static constexpr int kEmbers = 256;
    static constexpr int kLicks = 256;
    static constexpr int kWisps = 384;

    Shape shapes_[kModels];
    bgfx::TextureHandle emberSheet_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle smokeSheet_ = BGFX_INVALID_HANDLE;
    float metresPerTile_ = 1.0f;
    Shot shots_[kShots];
    Ember embers_[kEmbers];
    Lick licks_[kLicks];
    Wisp wisps_[kWisps];
    uint32_t dice_ = 0x41525257u;

    float roll();  // 0..1
    void shed(const Shot& shot);
    void lick(const Shot& shot);
    void smoke(const Shot& shot);
};

}  // namespace mu::game
