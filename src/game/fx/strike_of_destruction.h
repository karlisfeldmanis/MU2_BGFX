// Strike of Destruction: the Blade Knight's Season 4 orb skill, MU's AT_SKILL_STRIKE_OF_DESTRUCTION
// (232) -- the ground breaking where he drove his sword, MuMain's MODEL_BLOW_OF_DESTRUCTION and the
// blades, cracks, splash and stones it lays. Here the Ancient rune Strike of Destruction's
// (sim/items.h kSoD...), said as Loosed with skill 232 (sim::skill::kStrikeOfDestruction).
// docs/plans/ancient-runes/strike-of-destruction.md.
//
// **What is MU's, and kept** (MuMain; 1.03.35 the same). An invisible controller in two parts,
// each LifeTime 40 and Light (1.2, 1.2, 1.2) decaying /1.05 a frame once it shows (ZzzEffect.cpp:
// 4771-4808; MoveHandlers.cpp:7530-7640). Frames are MU's 25 a second.
//   * **The impact**, 100 units before him and 20 to his side, turned by his heading: the white
//     BITMAP_SWORD_EFFECT_MONO flare 65 up at scale 3.0-3.1 in Light x (0.5, 0.5, 1.0), and the
//     blue BITMAP_FLARE_BLUE laid on the ground four cells across, turned by his heading
//     (ZzzEffect.cpp:9216-9225, :10058-10065); EarthQuake (rand() % 8 - 4) x 0.1 for ten frames.
//     A frame on, once: two MODEL_NIGHTWATER_01 (scale 1), MODEL_KNIGHT_PLANCRACK_A at 1.2, and
//     the line of MODEL_KNIGHT_PLANCRACK_B toward the target, `length / 100 + 1` of them 55 units
//     apart, each turned his way 10-29 degrees off, alternately one way and the other.
//   * **The burst at the point**: the big blue BITMAP_LIGHT (flare01) 100 up at scale 5 in Light x
//     (0.5, 0.5, 1.0), the blue floor flare six cells across (ZzzEffect.cpp:9227-9237,
//     :10066-10072). For ten frames, 15 a frame within 150 units of it: a BITMAP_WATERFALL_5 or _3
//     (subtype 8) in (0.5, 0.5, 1.0), and on a coin flip a white BITMAP_SMOKE (subtype 55)
//     (ZzzEffectParticle.cpp:3474-3479, :3663-3668, :1538-1544; moved :8435-8444, :8595-8604,
//     :5734-5743). A frame on, once: two MODEL_NIGHTWATER_01 at 2 and 1, MODEL_RAKLION_BOSS_
//     CRACKEFFECT (knight_plancrack_grand) at 1.2, and 5-7 MODEL_STONE1/2 up to 150 units out --
//     the meteor's stones (fx/meteor.h), handed out by `update`'s callback.
//   * **The blades** (nightwater01): LifeTime 25, a random turn, Alpha -0.04 a frame, added in
//     (0.3, 0.3, 1.0) x Alpha (ZzzEffect.cpp:4809-4815; MoveHandlers.cpp:7671-7677).
//   * **The cracks**: the crack marks and the light columns standing in them, their five keys
//     played at 0.3 a frame so the columns sink into the ground, added in (0.3, 0.3, 1.0) x Alpha;
//     A and B's columns' V scrolled by -WorldTime % 2000 x 0.0001 (ZzzObject.cpp:599-628). A and B
//     LifeTime 25 at -0.04 a frame, the grand crack LifeTime 40 at -0.03.
//   * BLOW_OF_DESTRUCTION.wav, played by the caller at the cast.
//
// **Ours, and marked.** MU's first sixteen frames are skipped: they waited on his leap, which a
// rune does not play, so the burst shows on the tick the realm strikes (the user: the hit lands
// with the visual). One drawn at a time: a roll while one plays strikes unseen. The camera shakes
// at half MU's, and only for the hero's own. Every crack lies three centimetres over the ground
// where MU lifts them 15 and 30 units over its own (this ground is not MU's). The splash is born
// from the ground up to 300 units over it, where MU's is ±150 round a point 150 up, some of it
// underground. Its tuning against the footage is kLook below. And calmer (the user: "its to
// haotics"): only the burst at the point is drawn, two blade crowns, no stones or smoke and a
// sparse splash -- kImpact and kSplashCalm in the .cpp. And fire, not MU's ice blue (the user:
// "more like fire"; the realm strikes in fire and lays a burn): orange tints, flare01 on the
// floor, flame01 tongues and embers rising -- kCrackLight, kFlareTint, kFlameTint in the .cpp.
//
// Presentation only: the realm chose the bodies at the roll and struck them on the same tick.
// Pools are sized once and a frame allocates nothing.
#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "content/ground.h"
#include "content/showing.h"
#include "content/texture.h"
#include "game/fx/effect_mesh.h"
#include "game/fx/quake.h"
#include "gfx/effects.h"

namespace mu::game {

class StrikeOfDestruction {
public:
    // The blades', the cracks' meshes and sheets out of effects/destruction, the flares, the
    // splash and the smoke off the cooked showing. False when the grand crack is missing.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table, const content::Ground* ground);

    // The ground broken at `target` by a knight standing at `caster` (world metres, the ground
    // under each). `shake`: whether it is the hero's own, whose camera it shakes. False, and
    // nothing drawn, while one is still playing.
    bool cast(const float caster[3], const float target[3], uint32_t caster_id, bool shake);
    // `stone(at)` is called for each of MU's 5-7 stones, at the ground where it is thrown from.
    template <typename Stone>
    void update(float seconds, Stone stone);
    void gather(gfx::Effects& effects) const;
    // MU's EarthQuake, in degrees of camera pitch, as fx/fury.h's.
    float quakeDegrees() const { return quake_; }

    static constexpr float kFps = 25.0f;

private:
    static constexpr float kUnit = 0.01f;
    static constexpr int kKeys = 5;
    static constexpr int kLife = 25;        // the controller's frames once shown (MU's 40 - 16 + 1)
    static constexpr int kSplashFrames = 10;
    static constexpr int kSplashEach = 15;
    static constexpr int kBlades = 4;
    static constexpr int kLineMost = 8;
    static constexpr int kDrops = kSplashFrames * kSplashEach;
    static constexpr int kPuffs = kDrops;

    enum Crack : uint8_t { kCrackA, kCrackB, kGrand, kCracks };
    struct Placed {
        float at[3] = {};
        float x[3] = {1, 0, 0}, y[3] = {0, 1, 0}, z[3] = {0, 0, 1};
    };
    struct Blade {
        Placed where;
        float scale = 1.0f;
        float alpha = 1.0f;
    };
    struct Mesh {
        std::vector<EffectCorner> poses[kKeys];  // one group's corners at each key
        bgfx::TextureHandle sheet = BGFX_INVALID_HANDLE;
        bool streams = false;                    // lines2: V scrolled up the columns
    };
    struct Drop {
        bool alive = false;
        bool speckle = false;   // WATERFALL_5, else WATERFALL_3
        bool smoke = false;     // BITMAP_SMOKE subtype 55
        float at[3] = {};
        float rise = 0.0f;      // MU units a frame
        float scale = 1.0f;
        float light = 1.0f;
        float spin = 0.0f;
        int life = 30;
    };

    float unit();
    void step();
    void lay(Placed& out, const float at[3], float yaw) const;
    void floorFlare(gfx::Effects& effects, const float at[3], float yaw, float cells,
                    float light) const;
    void drawCrack(gfx::Effects& effects, int which, const Placed& where, float scale,
                   float key, float alpha) const;

    const content::Ground* ground_ = nullptr;
    std::vector<Mesh> cracks_[kCracks];
    mutable std::vector<EffectCorner> blend_;
    std::vector<EffectCorner> blade_;
    bgfx::TextureHandle bladeSheet_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle white_ = BGFX_INVALID_HANDLE;    // Swordeff_mono
    bgfx::TextureHandle blue_ = BGFX_INVALID_HANDLE;     // flareBlue
    bgfx::TextureHandle light_ = BGFX_INVALID_HANDLE;    // flare01
    bgfx::TextureHandle speckle_ = BGFX_INVALID_HANDLE;  // waterFall5
    bgfx::TextureHandle spray_ = BGFX_INVALID_HANDLE;    // waterFall3
    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;    // smoke01

    // The one playing.
    bool playing_ = false;
    bool shake_ = false;
    float owed_ = 0.0f;
    int frame_ = 0;          // our frames since the cast: MU's frame 16 is our 0
    float clock_ = 0.0f;     // seconds since the cast, for the columns' scrolling V
    float glow_ = 1.2f;      // the controllers' Light
    float yaw_ = 0.0f;       // his heading to the point, radians in world x/z
    float impact_[3] = {};
    float target_[3] = {};
    bool laid_ = false;      // MU's frame 17 done
    int stonesOwed_ = 0;
    Blade blades_[kBlades] = {};
    int bladeAge_ = 0;
    Placed crackA_, grand_;
    Placed line_[kLineMost];
    int lineCount_ = 0;
    int crackAge_ = 0;
    Drop drops_[kDrops + kPuffs] = {};
    float quake_ = 0.0f;
    uint32_t dice_ = 0x50D232u;
};

template <typename Stone>
void StrikeOfDestruction::update(float seconds, Stone stone) {
    seconds = std::fmin(seconds, 0.1f);
    owed_ += seconds * kFps;
    clock_ += seconds;
    while (owed_ >= 1.0f) {
        owed_ -= 1.0f;
        step();
    }
    while (stonesOwed_ > 0) {
        --stonesOwed_;
        // `(0, rand() % 150, 0)` turned rand() % 360 about the point.
        const float out = float(int(unit() * 150.0f)) * kUnit;
        const float turn = unit() * 6.2831853f;
        const float at[3] = {target_[0] + std::cos(turn) * out, target_[1],
                             target_[2] + std::sin(turn) * out};
        float ground = at[1];
        if (ground_ != nullptr) ground = ground_->heightAt(at[0], at[2]);
        const float there[3] = {at[0], ground, at[2]};
        stone(there);
    }
}

}  // namespace mu::game
