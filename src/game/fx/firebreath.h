// The Dinorant's Fire Breath: what the dragon under a knight breathes out at what he strikes.
// MuMain's `AT_SKILL_RIDER` (skill 49) makes a BITMAP_SHOTGUN at his feet as the blow is let go,
// with SOUND_SKILL_SWORD3 (ZzzCharacter.cpp:4406-4409). Born at ZzzEffect.cpp:3003-3036, moved by
// Move_BITMAP_SHOTGUN (MoveHandlers.cpp:5078-5110):
//
//   * **The birth, two sprays.** The carrier starts 20 units ahead of him and 50 up. At (-20, -20,
//     60) and (30, -20, 60) off it -- the dragon's two nostrils -- twenty BITMAP_JOINT_SPARKs each at
//     subtype 1 (Scale 2, Velocity rand()%20 + 16, LifeTime rand()%4 + 4): Angle[0] tipped 5 to 24
//     degrees and Angle[1] stepped 18 a spark, so each ring is a cone round his facing, fast and
//     gone in a fifth of a second.
//   * **The breath, ten frames.** LifeTime 10, `Direction (0, -30, 0)`: thirty units a frame along
//     his facing. Every frame two BITMAP_FIRE + 2 at subtype 10, 20 units either side of it, at
//     Scale `(15 - LifeTime) / 20 * (rand()%3 + 2)` -- so they swell as it goes, a quarter to near
//     three -- and the ground under it lit (0.5, 0.5, 0.8) over two tiles.
//   * **The flame** (ZzzEffectParticle.cpp:430-435, :4613-4620, :9079-9097): LifeTime 24, thrown
//     `(0, -(rand()%16 + 32) * 0.1, 0)` along his facing, Velocity *= 0.98; every frame Scale *=
//     0.95, Gravity += 0.004 and it rises Gravity * 10; Frame `(23 - LifeTime) / 6` of Fire03's
//     four 64-pixel cells, which is **not fire**: a blue-violet haze with stars in it, fading out
//     over the strip. Light stays white; the strip ends it.
//   * **The burst, on its last frame**: CreateBomb2 (ZzzEffect.cpp:6349-6371), thirty units up --
//     twenty BITMAP_SPARKs at subtype 2 and a BITMAP_EXPLOTION + 1 at Scale 4: DinoE.jpg, the
//     Dinorant's own blast, a pink-white bloom going lavender over four 64-pixel cells, a cell every
//     three frames of twelve (:2497, :4276, :8968), SOUND_EXPLOTION01 at its birth.
//
// **Ours, and marked.** The breath flies at the body the realm struck and not ten frames along
// his facing whatever stands there: MU's thirty units a frame (sim's `flies`, 7.5 tiles a second),
// for as long as the realm holds the blow in the air (`Loosed`'s `b`), at least kLeastFrames; so
// the bloom is where the number comes up. The sparks are calmed as the forge's and the wheel's
// are -- kSparksPerRing of MU's twenty. Both sheets are added in their own colour, raised by a
// gain (kFlameGain, kBlastGain): at MU's 1 the haze was lost in this HDR frame. The ground light
// is a point light following the breath, MU's cool blue.
//
// Presentation only, `game` and not `sim`: the realm struck the body on the tick. Pools are sized
// once and a frame allocates nothing.
#pragma once

#include <cmath>
#include <cstdint>
#include <string>

#include "content/ground.h"
#include "content/showing.h"
#include "content/texture.h"
#include "game/fx/forge.h"
#include "gfx/effects.h"
#include "gfx/renderer.h"

namespace mu::game {

class FireBreath {
public:
    // Fire03 and DinoE off the cooked showing, and the forge's sparks. False when neither sheet
    // could be had.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table, const content::Ground* ground);

    // A breath from a dragon standing at `feet` (the ground under him), facing `yaw` as he is
    // drawn, at `to` -- the struck body's middle -- arriving in `air` seconds.
    void cast(const float feet[3], float yaw, const float to[3], float air);

    // `blast(at)` once a breath, at the burst: SOUND_EXPLOTION01's place.
    template <typename Blast>
    void update(float seconds, Blast blast);

    void gatherEffects(gfx::Effects& effects, const float eye[3], const float near[3],
                       float daylight) const;
    uint32_t lights(gfx::PointLight* out, uint32_t max) const;
    void clear();

private:
    static constexpr float kReferenceFps = 25.0f;
    static constexpr float kUnit = 0.01f;
    static constexpr float kDegrees = 3.14159265f / 180.0f;

    // ---- the carrier (BITMAP_SHOTGUN) -------------------------------------------------------
    static constexpr int kLife = 10;
    static constexpr int kLeastFrames = 4;       // ours: see the header
    static constexpr float kStep = 30.0f;         // units a frame, Direction (0, -30, 0)
    static constexpr float kStartAhead = 20.0f, kStartUp = 50.0f;
    static constexpr float kSide = 20.0f;         // the flames, either side of it
    // ---- the sprays, at the nostrils --------------------------------------------------------
    static constexpr float kNostrilAhead = 20.0f, kNostrilUp = 60.0f;
    static constexpr float kNostrilLeft = 20.0f, kNostrilRight = 30.0f;
    static constexpr int kSparksPerRing = 6;      // ours, of MU's twenty
    // ---- the flame (BITMAP_FIRE + 2, subtype 10) --------------------------------------------
    static constexpr float kFlameLife = 24.0f;
    static constexpr int kFlameCells = 4;
    static constexpr float kFlamePixels = 64.0f;  // a cell of Fire03
    static constexpr float kFlameGain = 2.2f;     // ours
    // ---- the burst (CreateBomb2) ------------------------------------------------------------
    static constexpr float kBlastLife = 12.0f;
    static constexpr int kBlastCells = 4;
    static constexpr float kBlastScale = 4.0f;
    static constexpr float kBlastUp = 30.0f;
    static constexpr float kBlastGain = 1.0f;     // MU's; 1.4 blew out white
    static constexpr int kMotes = 6;              // ours, of MU's twenty
    // ---- the light, AddTerrainLight (0.5, 0.5, 0.8) over two tiles --------------------------
    static constexpr float kLightTiles = 2.0f;
    static constexpr float kLightColour[3] = {0.5f, 0.5f, 0.8f};
    static constexpr float kLightGain = 1.6f;     // ours: MU's terrain light, as a point light

    struct Carrier {
        bool alive = false;
        float at[3] = {};
        float step[3] = {};  // metres a reference frame
        float ahead[2] = {};  // where it flies, flat, unit length
        float right[2] = {};  // the flat side of that
        int life = kLife;    // MU's LifeTime, whole frames
        int frames = kLife;  // what it was born with
        float owed = 0.0f;
    };
    struct Flame {
        bool alive = false;
        float at[3] = {};
        float velocity[2] = {};  // metres a reference frame, flat
        float scale = 1.0f;
        float gravity = 0.0f;    // MU's Gravity, units a frame
        float life = kFlameLife;
        float spin = 0.0f;
    };
    struct Blast {
        bool alive = false;
        float at[3] = {};
        float life = kBlastLife;
    };

    static constexpr int kBreaths = 6;
    static constexpr int kFlamePool = kBreaths * kLife * 2 + 16;

    const content::Ground* ground_ = nullptr;
    Forge sparks_;
    bgfx::TextureHandle flameSheet_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle blastSheet_ = BGFX_INVALID_HANDLE;
    Carrier carriers_[kBreaths] = {};
    Flame flames_[kFlamePool] = {};
    Blast blasts_[kBreaths] = {};
    uint32_t dice_ = 0x2c1b3c6du;

    uint32_t roll();
    void spray(const float at[3], const float ahead[2], const float right[2]);
    void flame(const float at[3], const Carrier& carrier, float scale);
    template <typename Blast_>
    void step(Carrier& carrier, Blast_& blast);
};

template <typename Blast_>
void FireBreath::update(float seconds, Blast_ blast) {
    seconds = seconds < 0.1f ? seconds : 0.1f;  // a long hitch is capped, as the wheel's is
    const float frames = seconds * kReferenceFps;
    sparks_.update(seconds);
    for (Carrier& carrier : carriers_) {
        if (!carrier.alive) continue;
        carrier.owed += frames;
        while (carrier.alive && carrier.owed >= 1.0f) {
            carrier.owed -= 1.0f;
            step(carrier, blast);
        }
    }
    for (Flame& one : flames_) {
        if (!one.alive) continue;
        one.life -= frames;
        if (one.life <= 0.0f) {
            one.alive = false;
            continue;
        }
        one.at[0] += one.velocity[0] * frames;
        one.at[2] += one.velocity[1] * frames;
        const float drag = std::pow(0.98f, frames);
        one.velocity[0] *= drag;
        one.velocity[1] *= drag;
        one.scale *= std::pow(0.95f, frames);
        one.gravity += 0.004f * frames;
        one.at[1] += one.gravity * 10.0f * kUnit * frames;
    }
    for (Blast& one : blasts_) {
        if (!one.alive) continue;
        one.life -= frames;
        if (one.life <= 0.0f) one.alive = false;
    }
}

template <typename Blast_>
void FireBreath::step(Carrier& carrier, Blast_& blast) {
    // Move_BITMAP_SHOTGUN runs before MoveEffect moves it: the flames are laid where it is, then
    // it steps on. Their Scale is `(15 - LifeTime) / 20`, LifeTime read on MU's ten however
    // many frames this breath was given, so a short one swells as far as a long one.
    const float muLife = float(kLife) * float(carrier.life) / float(carrier.frames);
    const float scale = (15.0f - muLife) / 20.0f;
    for (float side : {kSide, -kSide}) {
        const float at[3] = {carrier.at[0] + carrier.right[0] * side * kUnit, carrier.at[1],
                             carrier.at[2] + carrier.right[1] * side * kUnit};
        flame(at, carrier, scale * float(roll() % 3u + 2u));
    }
    if (carrier.life == 1) {
        for (Blast& one : blasts_) {
            if (one.alive) continue;
            one = Blast{};
            one.alive = true;
            for (int k = 0; k < 3; ++k) one.at[k] = carrier.at[k];
            one.at[1] += kBlastUp * kUnit;
            break;
        }
        const float at[3] = {carrier.at[0], carrier.at[1] + kBlastUp * kUnit, carrier.at[2]};
        // Twenty BITMAP_SPARKs at subtype 2, pitched out flat (150 to 209 degrees): the forge's
        // motes, calmed.
        for (int i = 0; i < kMotes; ++i) sparks_.fling(at, 150.0f, 0.0f, false, true);
        blast(at);
        carrier.alive = false;
        return;
    }
    for (int k = 0; k < 3; ++k) carrier.at[k] += carrier.step[k];
    --carrier.life;
}

}  // namespace mu::game
