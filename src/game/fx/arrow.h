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
// arrow flies level; this one is aimed at the target's middle when it leaves, because a level
// arrow at 1.35 m passes over a spider, and the realm has already decided it hits. Not steered
// after, as MU's is not. The speed and the tile short are the realm's own (`kArrowTilesPerSecond`), so the
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
//
// **Ours:** the steel and the saw bolt, which MU flies bare, leave a cold trail in the same
// rhythm (the user, 2026-10-03: 'more exciting with similar effect but maybe not fire'): a
// blue-white streak of flare01 glints laid close behind the head and gone in a quarter of a
// second, and smaller glints thrown off sideways as sparks that fall and wink out, where the wooden
// arrow's embers drift, a faint cold light with the head, and the arrow's own thin smoke. The Light
// Crossbow's laser glows already and is left bare.
//
// **Ours:** a very special bow or crossbow keeps the arrow's or the bolt's look and tones its
// colour (the user, 2026-10-03: 'if there is some very special bow/crossbow we can tone the
// effects colors', 'like for chaos bow we can make green fire'), each from MU's own picture of
// it: the Chaos Nature Bow burns green (its BITMAP_SHINY+1 glints, ZzzCharacter.cpp:7036-7041),
// the Silver Bow pale silver (PartObjectColor 5, white), the Bluewing Crossbow's streak cyan
// (its ArrowWing01's bu009) and the Serpent Crossbow's amber (its ArrowThunder01's bow_d). A
// tinted arrow's embers and licks are drawn off fire_grey -- fire01 with its brightest channel
// in all three -- because orange times green is mud; the small fire on its own tail is
// multiplied. The smoke stays grey.
//
// ArrowSteel01 is built head at -Z -- its broadhead, widest at z -18 and pointed at -43, with
// the bare shaft out to +44 -- where Arrow01's head is at +Z; drawn as the others it flew tail
// first (the user: 'bolts looks inverted'). It is turned half round (Shape::reversed).
//
// **Penetration's arrow** (`pierce`) is the weapon's own, wound in MU's MODEL_PIERCING: four
// BITMAP_FLARE+1 joints (Flare02, white at half, added) on the arrow, at 0, 90, 180 and 240
// degrees round it (ZzzEffect.cpp:1506-1543, the joints' `Velocity`). Each lays a tail pair
// every thirty degrees of its turn, three a reference frame, a band 20 units wide 18 units off
// the line (`Direction = Scale * 1.5`, ZzzEffectJoint.cpp:2061-2095), so the four corkscrew
// along the whole flight and stay where they were laid; at fifteen frames of its thirty the
// band dims by a third a frame (ZzzEffectJoint.cpp:6190-6222). Laid level across the flight as
// the joint's own matrix lays a tail.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "content/showing.h"
#include "content/texture.h"
#include "game/fx/effect_mesh.h"
#include "gfx/effects.h"
#include "gfx/renderer.h"

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
    // Half a missile's length at its drawn scale: Arrow01's -42 to +48 units at 0.8.
    static constexpr float kHalfLength = 0.36f;
    // Its fire's or streak's colour when it is one of the few toned (see the top), or null.
    static const float* tintFor(int32_t group, int32_t number);

    // One let go from `from` at the body `whom`, aimed first at `to`.
    // `shooter`, when not 0, is reported in landed() the frame the arrow reaches `whom`: a
    // monster's shot, whose blow is shown where it lands (Play::hunterShot).
    // `tint`, from tintFor, tones the fire or the streak.
    // `seconds`, when above 0, is how long it has to reach `to`: an arrow held for its string
    // flies faster to land when the realm says it lands (Play::nocking_).
    // `pierce` winds MODEL_PIERCING's bands round it: Penetration's arrow (see the top).
    void loose(const float from[3], const float to[3], uint32_t whom, Model model,
               uint32_t shooter = 0, const float* tint = nullptr, float seconds = 0.0f,
               bool pierce = false);
    const std::vector<uint32_t>& landed() const { return landed_; }

    // `middle` answers where a body's middle is drawn now, false once it is not drawn.
    void update(float seconds, const std::function<bool(uint32_t, float*)>& middle);
    void gather(gfx::Effects& effects) const;
    // Ours: a bolt's faint cold light at its head, one a steel or saw bolt in the air.
    uint32_t lights(gfx::PointLight* out, uint32_t max) const;

    uint32_t flying() const;
    // Whether `shooter`'s arrow at `whom` is still in the air: her blow waits for it to land.
    bool flyingAt(uint32_t shooter, uint32_t whom) const;

private:
    std::vector<uint32_t> landed_;  // this update's shooters whose arrow reached its body
    struct Part {
        std::vector<EffectCorner> triangles;
        bgfx::TextureHandle sheet = BGFX_INVALID_HANDLE;
        gfx::Blend blend = gfx::Blend::Alpha;
        // Turned half round on its own: Arrow01's shaft, whose point is at loaded -Z (the
        // loader negates Z) where its fire01 is laid behind -- it flew tail first under the
        // fire, and bare on a Penetration arrow it showed (the user, 2026-10-04: 'looks like
        // arrow is inverted'). The fire stays where it trails.
        bool flipped = false;
    };
    struct Shape {
        std::vector<Part> parts;
        // Built head at -Z: turned half round about its up, so it flies head first.
        bool reversed = false;
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
        float glinted;   // metres since the last glint (the bolts')
        float chipped;   // metres since the last spark
        float travelled; // metres from the muzzle
        float speed;     // metres a second
        bool tinted;
        float tint[3];
        // Penetration's: wound in the bands and bare of fire -- no embers, licks or fire tail,
        // only the thin smoke, where MU's wooden arrow keeps them (the user, 2026-10-04: 'better but without
        // fire'). ours.
        bool pierce;
        // Reference frames left of its fade, once its flight is over: Penetration's flies on as
        // it goes (the user, 2026-10-04: 'penetraiton arrow cant just stoped and fadeout, it has
        // to fadeout in movement'). 0 while it flies. ours.
        float fading;
    };
    static constexpr float kPierceFade = 8.0f;  // reference frames, a third of a second
    struct Glint {  // ours: a bolt's streak (still) or spark (thrown, falling)
        bool alive = false;
        bool spark = false;
        float at[3];
        float velocity[3];  // metres a second
        float size;         // metres
        float spin;
        float left;         // reference frames
        float frames;       // what it was born with
        float colour[3];
    };
    struct Ember {
        bool alive = false;
        bool grey = false;  // off fire_grey, in `colour`
        float colour[3];
        float at[3];
        float velocity[3];  // metres a second
        float size;         // metres
        float spin;
        float rise;
        float left;         // reference frames
    };
    struct Lick {  // ours: the tail's own flame
        bool alive = false;
        bool grey = false;
        float colour[3];
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
    // ---- MU's MODEL_PIERCING (see the top) ------------------------------------------------------
    // **Ours: a short spiral that travels with the arrow** (the user, 2026-10-04: 'penetration
    // effect dont have to just fade out on plane place but that had to happened on movement'),
    // where MU's fifty tails lay the whole flight in the air: twenty-four pairs, eight reference
    // frames of its turn, about 5 m ('penetration trail has to be longer with some smoke'),
    // bright at the head and gone at its back end. The wooden arrow's thin smoke rides behind.
    static constexpr int kBandTails = 24;
    static constexpr float kBandFrames = 30.0f;        // `LifeTime = PKKey`, 30
    static constexpr float kBandDimFrom = 15.0f;       // `if (o->LifeTime < 15)`
    static constexpr float kBandDim = 1.5f;            // `powf(1.f / 1.5f, ...)` a frame
    static constexpr float kBandTurn = 30.0f;          // degrees a tail pair
    static constexpr float kBandTurnsAFrame = 3.0f;    // the mover's three a frame
    static constexpr float kBandRadiusUnits = 18.0f;   // 12 * 1.5
    static constexpr float kBandWidthUnits = 20.0f;    // `o->Scale = 20.f`, Skill 0
    static constexpr float kBandLight = 0.5f;          // `Vector(0.5f, 0.5f, 0.5f, o->Light)`
    static constexpr float kBandPhases[4] = {0.0f, 90.0f, 180.0f, 240.0f};
    // **Ours: drilled, not painted** (the user, 2026-10-04: 'a tighter spiral at the head'):
    // the bands wind at this share of MU's 18 units round the arrowhead and open to the second
    // share at the spiral's back end.
    static constexpr float kBandHeadRadius = 0.35f;
    static constexpr float kBandTailRadius = 1.4f;
    // **Ours: a faint cool light on the arrow** ('some minimal light emiter'), off MU's gathering's
    // (0.2, 0.4, 1.0) (MoveHandlers.cpp:1638), dimmer than the bolts' and fading with the arrow.
    // Gold since the bands went gold (below), where MU's gathering is blue.
    static constexpr float kPierceLight[3] = {0.50f, 0.36f, 0.14f};
    // **Ours: gold tones** (the user, 2026-10-04: 'can we add some gold tones for penetration'):
    // two of the four bands, the first and the third, are drawn in this gold over MU's white.
    static constexpr float kBandGold[3] = {1.0f, 0.72f, 0.30f};
    static constexpr float kPierceLightReach = 1.8f;  // metres on the ground
    struct Band {
        bool alive = false;
        int shot = -1;       // its arrow in shots_, -1 once the arrow is gone
        bool gold = false;   // one of the two gold bands
        float turn = 0.0f;   // degrees round the line
        float due = 0.0f;    // reference frames to the next tail pair
        float age = 0.0f;    // reference frames
        int count = 0;       // tails laid, oldest first
        float axis[kBandTails][3];    // the arrow's line where each pair was laid
        float radial[kBandTails][3];  // and the unit out from it to the band
        float across[kBandTails][3];  // level, across the flight: the band's width
    };
    static constexpr int kBands = 64;
    Band bands_[kBands];
    bgfx::TextureHandle bandSheet_ = BGFX_INVALID_HANDLE;  // flare02
    void wind(int shot);
    void lay(Band& band, const Shot& shot, float back);

    // ---- MU's numbers (Arrow01.json and the three beside it) ---------------------------------
    static constexpr float kReference = 25.0f;
    static constexpr float kUnit = 0.01f;
    static constexpr float kScale = 0.8f;             // `o->Scale = 0.8f`
    static constexpr float kFrames = 30.0f;           // `o->LifeTime = 30`
    static constexpr float kTilesASecond = 17.5f;     // the realm's kArrowTilesPerSecond
    static constexpr float kStopsShort = 1.0f;        // tiles: CheckCharacterRange(o, 100)
    static constexpr float kIntoBody = 0.25f;         // metres from its middle: ours
    // The embers: MU's BITMAP_FIRE sub-type 0, the numbers `fx/meteor.h` already carries.
    static constexpr float kEmberSpacingUnits = 70.0f;  // one a reference frame at 70 a frame
    static constexpr float kEmberFrames = 24.0f;
    static constexpr int kEmberCells = 4;
    static constexpr int kEmberHeld = 6;
    static constexpr float kSmallestEmber = 1.28f, kLargestEmber = 1.92f;  // x the sheet's 64
    // **Ours:** drawn at this share of MU's size (the user, 2026-10-03: 'reduce arrows fire
    // effect little bit, its to wide').
    static constexpr float kEmberShare = 0.75f;  // 0.65 was a touch narrow
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
    static constexpr float kSmallestLick = 0.14f, kLargestLick = 0.24f;  // metres
    static constexpr float kLickRise = 0.3f;       // metres a second
    static constexpr float kLickJitter = 0.15f;    // metres a second, either way across

    // ---- Ours: the smoke behind it ---------------------------------------------------------------
    // Cleaner, still soft (the user, 2026-10-03: 'make that smoke more clean but keep the
    // blurriness'): fewer, lighter, fainter wisps that open wider and scatter less.
    static constexpr float kWispSpacing = 0.45f;   // metres of flight between wisps
    static constexpr float kWispBehind = 0.55f;    // metres behind the origin, past the flame
    static constexpr float kWispFrames = 12.0f;
    static constexpr float kWispBorn = 0.18f, kWispGrown = 0.55f;  // metres across
    static constexpr float kWispRise = 0.25f;      // metres a second
    static constexpr float kWispGrey[3] = {0.42f, 0.42f, 0.44f};
    static constexpr float kWispAlpha = 0.22f;
    // Not at the bow: the first wisp waits until the arrow is this far out, so the smoke is
    // left after the shot and not in her hands (the user, 2026-10-03).
    static constexpr float kWispFromMuzzle = 1.0f;  // metres

    // ---- Ours: the bolts' cold trail -----------------------------------------------------------
    static constexpr float kGlintSpacing = 0.04f;  // metres of flight between glints
    static constexpr float kGlintBehind = 0.10f;   // metres behind the head
    static constexpr float kGlintFrames = 6.0f;
    static constexpr float kSmallestGlint = 0.22f, kLargestGlint = 0.32f;  // metres
    static constexpr float kGlintLight[3] = {0.45f, 0.62f, 0.95f};
    static constexpr float kSparkSpacing = 0.30f;  // metres of flight between sparks
    static constexpr float kSparkFrames = 9.0f;
    static constexpr float kSparkSize = 0.05f;     // metres
    static constexpr float kSparkThrow = 1.6f;     // metres a second, across
    static constexpr float kSparkFall = 6.0f;      // metres a second, a second
    static constexpr float kSparkLight[3] = {0.85f, 0.92f, 1.0f};
    static constexpr float kBoltLight[3] = {0.25f, 0.38f, 0.70f};
    static constexpr float kBoltLightReach = 1.6f;   // metres on the ground
    static constexpr float kBoltLightHeight = 1.4f;  // metres, about where it flies

    // ---- Ours: the toned few (see the top) -----------------------------------------------------
    static constexpr float kNatureFire[3] = {0.30f, 0.95f, 0.40f};
    static constexpr float kSilverFire[3] = {0.75f, 0.80f, 0.95f};
    static constexpr float kBluewingStreak[3] = {0.25f, 0.80f, 0.95f};
    static constexpr float kSerpentStreak[3] = {0.95f, 0.70f, 0.25f};
    static constexpr float kTailTone = 1.6f;    // the tail's fire01, multiplied
    // fire_grey carries the flame in all three channels where fire01 is mostly red, and green
    // reads far brighter than red: at the tint alone the green embers were glowing balls.
    static constexpr float kGreyFireShare = 0.45f;
    static constexpr float kToneLight = 0.6f;   // a toned bolt's light, of its streak

    static constexpr int kShots = 16;
    static constexpr int kGlints = 384;
    static constexpr int kEmbers = 256;
    static constexpr int kLicks = 256;
    static constexpr int kWisps = 384;

    Shape shapes_[kModels];
    bgfx::TextureHandle emberSheet_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle smokeSheet_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle greyFireSheet_ = BGFX_INVALID_HANDLE;  // fire_grey
    bgfx::TextureHandle glintSheet_ = BGFX_INVALID_HANDLE;  // flare01, streak and sparks
    float metresPerTile_ = 1.0f;
    Shot shots_[kShots];
    Ember embers_[kEmbers];
    Lick licks_[kLicks];
    Wisp wisps_[kWisps];
    Glint glints_[kGlints];
    uint32_t dice_ = 0x41525257u;

    float roll();  // 0..1
    void shed(const Shot& shot);
    void lick(const Shot& shot);
    void smoke(const Shot& shot);
    void glint(const Shot& shot, bool spark, float back);
};

}  // namespace mu::game
