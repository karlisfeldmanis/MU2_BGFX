// Power Slash's look: the Magic Gladiator's five waves fanned out ahead of his blade, each a
// moving point of light that sheds a bright blue-white smoke and a long dim haze behind it.
// docs/plans/mg-skills/power-slash.md section 2 is the trace; this is its section 5.7.
//
// **What MU does, and all of it is kept.** AttackStage (ZzzCharacter.cpp:2820-2843) on a Power
// Slash, at MU's 25 frames a second, on the first frame of the swing with AttackTime set:
//
//   * **Five MODEL_MAGIC2 sub 2** at his feet, turned -40, -20, 0, +20 and +40 off his facing
//     (ZzzEffect.cpp:1991-1998), and SOUND_SKILL_SWORD3 with them, once. Each goes Direction
//     (0, -60, 0) a frame in a straight line at the height it was made (MoveParticle). Its mesh
//     is never drawn -- sub 2 hides it (MoveHandlers.cpp:3365) -- so a wave is what it leaves.
//   * **Every frame, from each** (Move_MODEL_MAGIC2, MoveHandlers.cpp:3346-3381): a luminosity
//     L of 0.7-1.0, dropping (5 - LifeTime) * 0.2 in its last four frames (ZzzEffect.cpp:
//     6645-6650); four BITMAP_SMOKE sub 3; one BITMAP_SMOKE sub 11, lit (0.3L, 0.6L, L), scale
//     1.20-1.67; at 50 up two Shiny02 sprites (scale 1.5, 48 x 96 units, white, rolled at
//     random) and one flare01 (BITMAP_LIGHT, scale 3.5, 224 units, (0.3L, 0.6L, L), rolled),
//     each for that one frame; and AddTerrainLight (0.3L, 0.6L, L) three tiles round it.
//   * **Smoke sub 3, the bright puffs** (ZzzEffectParticle.cpp:1248-1254, 5328-5333): life 10,
//     scale 0.80-1.11, thrown off at a random yaw and a pitch of +-45 at 40-47 units a frame,
//     the speed four tenths of itself each frame and the scale a tenth more; lit (0.8, 0.8, 1)
//     times LifeTime / 8, overbright and fading. Not spun.
//   * **Smoke sub 11, the haze** (:1255-1266, 5334-5341): life 50, set off it +-32 aside and
//     32-95 up, thrown the same way, turned at random; its light the wave's (0.3L, 0.6L, L)
//     times LifeTime / 50, the scale a twentieth more a frame, sinking a unit. It outlives the
//     waves by two seconds, the pale mist the footage holds over the pack.
//   * **Nothing on what it strikes**: no hit effect, sound or light (the dossier's 2.4 E).
//
// **Ours, and marked.** The waves fade out past where they strike: MU's live twenty frames to
// 11.4 tiles, twice the server's six; here twelve, to 7.2, MU's last-four-frames dimming kept on
// those (the dossier's 5.4 -- "nothing drawn where nothing is hit"). A wave keeps to the ground
// it crosses rather than to the height it left at. They leave a tick after the cast is said,
// when the realm lets the blow go (sim::SkillRow::release, its least tick), so the numbers come
// as each wave reaches its body (sim::kWaveTilesPerSecond, the same fifteen tiles a second).
// A colour over one is drawn as a second layer of the same sprite (fire_slash.cpp's addLit), so
// the puffs are MU's 1.125 at their making: MuMain's ImmediateRenderer takes the float unclamped,
// and this pass adds in linear light where MU added in the screen's -- which the smoke makes up
// for at twice its light (kSmokeGain), soft so the field's grass does not stipple it. The blade's
// trail is play_show.cpp's (fx/streak.h, motion_blur, motion_blur_r on the Sword Dancer), MU's
// straight fade down it. The ground light
// is MU's colour at twice its strength: AddTerrainLight adds to a terrain light that MU's maps
// keep well under one, and a point light here lands on a sunlit field.
//
// Presentation only, `game` and not `sim`: nothing rolled here reaches the seeded log. Pools are
// sized once and a frame allocates nothing.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "content/ground.h"
#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"
#include "gfx/renderer.h"

namespace mu::game {

class PowerSlash {
public:
    // The smoke, the flare and Shiny02, off the showing's table. False when the smoke's sheet
    // could not be had.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table, const content::Ground* ground);

    // A Power Slash by `owner`, its waves let go `wait` seconds on from where he stands then,
    // fanned round `yaw` -- the drawing's yaw of the realm's aim, which is what they strike along.
    void cast(uint32_t owner, float yaw, float wait);

    // Every frame. `place(id, feet)` where a body is drawn, false for none; `sound(id, feet)`
    // SOUND_SKILL_SWORD3 as the waves leave.
    template <typename Place, typename Sound>
    void update(float seconds, Place place, Sound sound);

    void gatherEffects(gfx::Effects& effects) const;
    // A blue light under each wave, MU's AddTerrainLight.
    uint32_t lights(gfx::PointLight* out, uint32_t max) const;
    void clear();

private:
    static constexpr float kReferenceFps = 25.0f;
    static constexpr float kDegrees = 3.14159265f / 180.0f;
    // ---- the waves (MODEL_MAGIC2 sub 2) -----------------------------------------------------
    static constexpr int kWaves = 5;
    static constexpr float kWaveTurns[kWaves] = {-40.0f, -20.0f, 0.0f, 20.0f, 40.0f};
    static constexpr float kWaveStep = 60.0f;       // Direction (0, -60, 0), units a frame
    static constexpr int kWaveLife = 12;            // ours: to 7.2 tiles, MU's 20 to 11.4
    static constexpr float kLeast = 0.7f, kMost = 1.0f;
    static constexpr float kSpriteLift = 50.0f;
    static constexpr float kBlue[3] = {0.3f, 0.6f, 1.0f};
    static constexpr float kLightTiles = 3.0f;
    static constexpr float kLightGain = 2.0f;       // ours, see above
    // ---- its sprites, a frame each ----------------------------------------------------------
    // Ours, smaller than MU's 1.5 and 3.5 (a 224-unit flare on each of five heads): the user,
    // 2026-10-24, "sparkles has to be much smaller", then half that again: "keep finetune
    // sparkles still to large".
    static constexpr float kShinyScale = 0.3f, kShinyWide = 32.0f, kShinyTall = 64.0f;
    static constexpr float kFlareScale = 0.6f, kFlarePixels = 64.0f;
    // ---- the puffs (BITMAP_SMOKE sub 3) and the haze (sub 11) -------------------------------
    // Ours, MU's 4: two a frame, each half as large again and a third brighter (kPuffBigger,
    // kPuffBrighter), and the haze larger too -- MU's 64-pixel cloud is mottled, and twenty small
    // ones stacked read as speckle where a few large ones read as a soft cloud (the user,
    // 2026-10-24: "smoke effects nice and smooth").
    static constexpr int kPuffsAFrame = 2;
    static constexpr float kPuffBigger = 1.6f, kHazeBigger = 1.4f, kPuffBrighter = 1.35f;
    static constexpr float kSmokePixels = 64.0f;
    static constexpr float kPuffLife = 10.0f, kPuffGrows = 0.1f;
    static constexpr float kHazeLife = 50.0f, kHazeGrows = 0.05f, kHazeSinks = 1.0f;
    static constexpr float kSlows = 0.4f;
    // MU's (0.8, 0.8, 1) times LifeTime / 8 (ZzzEffectParticle.cpp:5328-5333).
    static constexpr float kPuffTint[3] = {0.8f, 0.8f, 1.0f};
    // Ours: the smoke's light, puffs and haze, times this. MU added smoke01 to the screen's
    // colour, where half of white on a dim field reads near white; added here in linear light
    // the same sheet is a third as strong, and the fan's fog a faint haze.
    static constexpr float kSmokeGain = 2.0f;
    static constexpr float kSmokeSoft = 2.0f;  // metres, gfx::Sprite::soft (see gatherSmoke)
    // Ours: the smoke lifted over the field's grass, and the haze's sinking stopped short of it, so
    // no blade cuts it (the user, 2026-10-24: "check if smoke effects are nice and smooth without
    // cut/crop effect"). Metres.
    static constexpr float kSmokeLift = 0.5f, kHazeSinksMost = 0.3f;

    struct Wave {
        bool alive = false;
        uint32_t owner = 0;
        float wait = 0.0f;   // seconds to its leaving
        bool gone = false;   // left, and moving
        int life = kWaveLife;
        float owed = 0.0f;   // reference frames not yet stepped
        float at[3] = {};
        float shown[3] = {}; // where this frame's smoke and sprites were made, before the move
        float step[2] = {};  // metres a frame, flat
        float light = 0.0f;  // L this frame
        float shinyRoll[2] = {}, flareRoll = 0.0f;
        bool sounds = false; // the first of a cast says SWORD3
    };
    // A puff is a function of its age: MU's speed four tenths of itself a frame is a way of
    // v (1 - 0.4^age) / 0.6 from where it was made, drawn smooth between MU's frames.
    struct Puff {
        bool alive = false;
        float from[3] = {}, way[3] = {};  // metres, and a unit direction
        float speed = 0.0f;               // metres its first frame
        float scale = 1.0f, age = 0.0f, spin = 0.0f;
        float tint[3] = {1.0f, 1.0f, 1.0f};  // a haze's light at its making
    };

    static constexpr int kCasts = 6;
    static constexpr int kPuffs = 1024;
    static constexpr int kHazes = 384;

    const content::Ground* ground_ = nullptr;
    bgfx::TextureHandle smokeSheet_ = BGFX_INVALID_HANDLE, flareSheet_ = BGFX_INVALID_HANDLE,
                        shinySheet_ = BGFX_INVALID_HANDLE;
    Wave waves_[kCasts * kWaves] = {};
    Puff puffs_[kPuffs] = {};
    Puff hazes_[kHazes] = {};
    uint32_t dice_ = 0x5a1a5eu;

    float unit();
    float between(float a, float b) { return a + (b - a) * unit(); }
    float perUnit() const { return (ground_ ? ground_->metresPerTile() : 1.0f) / 100.0f; }
    void stepWave(Wave& one);
    void smoke(Puff* pool, int size, const float at[3], float scale, const float tint[3]);
    void gatherSmoke(gfx::Effects& effects, const Puff* pool, int size, bool haze) const;
};

template <typename Place, typename Sound>
void PowerSlash::update(float seconds, Place place, Sound sound) {
    seconds = std::fmin(seconds, 0.1f);
    const float frames = seconds * kReferenceFps;
    for (Wave& one : waves_) {
        if (!one.alive) continue;
        if (!one.gone) {
            one.wait -= seconds;
            if (one.wait > 0.0f) continue;
            float feet[3];
            if (!place(one.owner, feet)) {
                one.alive = false;
                continue;
            }
            for (int k = 0; k < 3; ++k) one.at[k] = feet[k];
            one.gone = true;
            if (one.sounds) sound(one.owner, feet);
            // MU moves an effect on the frame it is made.
            one.owed = -one.wait * kReferenceFps;
            stepWave(one);
        } else {
            one.owed += frames;
        }
        while (one.alive && one.owed >= 1.0f) {
            one.owed -= 1.0f;
            stepWave(one);
        }
    }
    for (Puff& one : puffs_) {
        if (one.alive && (one.age += frames) >= kPuffLife) one.alive = false;
    }
    for (Puff& one : hazes_) {
        if (one.alive && (one.age += frames) >= kHazeLife) one.alive = false;
    }
}

}  // namespace mu::game
