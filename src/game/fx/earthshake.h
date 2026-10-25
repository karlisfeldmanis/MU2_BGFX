// Earthshake, the Dark Horse's stomp: what MU draws for it (docs/plans/dark-lord-spells/earthshake.md).
//
// MuMain's RenderDarkHorseSkill (GOBoid.cpp:727-769; 1.03.35 GOBoid.cpp:856-895), run on every drawn
// frame of the horse's clip 3 while its rider plays PLAYER_ATTACK_DARKHORSE:
//
//   * **The shock wave**: a BITMAP_SHOCK_WAVE sub-type 0 at the horse every 400 ms -- Scale 20, less
//     one a frame, laid on the terrain by RenderTerrainAlphaBitmap at the horse's light: a ring
//     twenty tiles across closing to nothing in twenty frames (ZzzEffect.cpp:3538-3543, 10030-10040,
//     MoveHandlers.cpp:5766-5769). Begun with the clip (`stomp`), the cooked `shockwave` sheet.
//   * **The rock spikes**: six MODEL_GROUND_STONE or _STONE2 at random, 60 degrees apart from a
//     random start, on a ring round the horse; 1.03.35's odd frames only, so two rings, at 150 and
//     300 units, two frames apart. Each: LifeTime 40, Scale 1.2-1.49 (STONE2 1.0-1.29), a random yaw,
//     its 5-key clip at 0.3 once -- risen out of the ground on its first key, settling on the rest --
//     smoke and a MODEL_STONE1/2 chip off it at LifeTime 33-36, fading Alpha / 1.3 and BlendMeshLight
//     / 2 a frame in its last 8 (ZzzEffect.cpp:3480-3520, MoveHandlers.cpp:5687-5714). Its DhorSS
//     mesh lit, DhorSS_R bright (TextureScript's `_R`, additive). Refused on no-move and no-ground.
//   * **The camera**: `EarthQuake = (rand() % 3 - 3) * 0.7` degrees every frame the spikes are made.
//   * **The crater** (MODEL_SKILL_FURY_STRIKE, Kind 2) is Rageful Blow's without its weapon --
//     game/fx/fury.h `cast(..., groundOnly)`, laid by Play.
//
// **Ours, and marked**: the spikes are drawn off the realm's `Loosed` 62, on the tick its numbers
// come (sim::kQuakeStrikes, MU's 25th frame), so the ground breaks with the blows; the chip is one a
// stone where MU throws one a frame for four; the smoke is the fury's; the ground ring at half MU's
// light (kWaveLight), and MU's warm terrain light under the stones (0.79, 0.72, 0.49, two tiles) is
// one light round him while they stand.
//
// Presentation only: who is struck and when is the realm's (Realm::earthshake).
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
#include "gfx/renderer.h"

namespace mu::game {

class Earthshake {
public:
    bool open(const std::string& assetDir, content::Textures& textures, const content::Showing& table,
              const content::Ground* ground);
    // The stomp begun at `feet`: the ground rings every 400 ms for `seconds`, his clip's length.
    void stomp(const float feet[3], float seconds);
    // The spikes for `owner` at `feet`, `wait` seconds on: the realm's strike.
    void strike(uint32_t owner, const float feet[3], float wait);
    // `chip(at)` where a stone throws its chip (the meteor's stones).
    template <typename Chip>
    void update(float seconds, Chip chip);
    void gatherEffects(gfx::Effects& effects, float daylight) const;
    uint32_t lights(gfx::PointLight* out, uint32_t max) const;
    float quakeDegrees() const { return quake_; }
    // How long a spike takes from fully sunk to standing: its clip's first key at 0.3 a reference
    // frame (`sunk`). Play raises the rings this much before the realm's strike, so the rock is up
    // as the numbers come rather than still underground (the user, 2026-10-25: "check
    // synhronisation" -- the numbers led the spikes by 0.13 s, and the jolt with them).
    static constexpr float kRiseSeconds = 1.0f / (0.3f * 25.0f);
    QuakeFor quakeFor;
    void clear();

private:
    static constexpr float kReferenceFps = 25.0f;
    static constexpr float kUnit = 0.01f;
    static constexpr float kWaveEvery = 0.4f;    // HorseEffectInterval, 10 frames
    static constexpr float kWaveScale = 20.0f;   // tiles across, less one a frame
    static constexpr float kWaveLight = 0.5f;    // ours, of MU's horse light 1
    static constexpr float kWaveOver = 0.06f;    // metres over the ground
    static constexpr float kRings[2] = {150.0f, 300.0f};
    static constexpr float kRingGap = 2.0f / kReferenceFps;  // the second ring two frames on
    static constexpr int kPerRing = 6;
    static constexpr float kStoneLife = 40.0f, kStoneSpeed = 0.3f;
    static constexpr float kJoltSeconds = 4.0f / kReferenceFps;  // frames 8 to 9.5 at 0.34
    static constexpr float kLightTiles = 3.5f;
    static constexpr float kLightColour[3] = {0.79f, 0.72f, 0.49f};

    struct Wave {
        bool alive = false;
        float at[3] = {};
        float wait = 0.0f, age = 0.0f;  // seconds to its birth, frames since
    };
    struct Ring {
        bool alive = false;
        uint32_t owner = 0;
        float at[3] = {};
        float wait = 0.0f, radius = 150.0f;
    };
    struct Stone {
        bool alive = false;
        int model = 0;
        float at[3] = {};
        float yaw = 0.0f, scale = 1.0f, age = 0.0f;
        bool chipped = false;
    };
    struct Jolt {
        bool alive = false;
        uint32_t owner = 0;
        float at[3] = {};
        float wait = 0.0f, left = kJoltSeconds;
    };
    static constexpr int kWaves = 16, kRingPool = 16, kStones = 96, kJolts = 8;

    struct Model {
        std::vector<EffectCorner> rock, seam;
        float lift = 0.0f;  // units under its risen key that its first key stands
    };
    Model models_[2];
    bgfx::TextureHandle rockTex_ = BGFX_INVALID_HANDLE, seamTex_ = BGFX_INVALID_HANDLE,
                        waveTex_ = BGFX_INVALID_HANDLE;
    const content::Ground* ground_ = nullptr;
    Wave waves_[kWaves] = {};
    Ring rings_[kRingPool] = {};
    Stone stones_[kStones] = {};
    Jolt jolts_[kJolts] = {};
    float quake_ = 0.0f;
    uint32_t dice_ = 0x6a09e667u;

    uint32_t roll();
    float metre() const { return (ground_ ? ground_->metresPerTile() : 1.0f) * kUnit; }
    float floorAt(float x, float z) const { return ground_ ? ground_->heightAt(x, z) : 0.0f; }
    bool groundAt(float x, float z) const;
    void raise(const Ring& ring);
    // The stone's lift under its risen pose, in units at scale 1, `age` frames into its clip.
    float sunk(const Stone& stone) const;
};

template <typename Chip>
void Earthshake::update(float seconds, Chip chip) {
    seconds = std::fmin(seconds, 0.1f);
    const float frames = seconds * kReferenceFps;
    for (Wave& wave : waves_) {
        if (!wave.alive) continue;
        if (wave.wait > 0.0f) {
            wave.wait -= seconds;
            continue;
        }
        wave.age += frames;
        if (wave.age >= kWaveScale) wave.alive = false;
    }
    for (Ring& ring : rings_) {
        if (!ring.alive) continue;
        ring.wait -= seconds;
        if (ring.wait > 0.0f) continue;
        ring.alive = false;
        raise(ring);
    }
    for (Stone& stone : stones_) {
        if (!stone.alive) continue;
        stone.age += frames;
        if (stone.age >= kStoneLife) {
            stone.alive = false;
            continue;
        }
        // LifeTime 36 to 33: smoke and a chip off it, 60 east, 60 south and 50 up of it.
        if (!stone.chipped && stone.age >= 4.0f) {
            stone.chipped = true;
            const float m = metre();
            const float at[3] = {stone.at[0] + 60.0f * m, stone.at[1] + 50.0f * m, stone.at[2] + 60.0f * m};
            chip(at);
        }
    }
    quake_ = 0.0f;
    for (Jolt& jolt : jolts_) {
        if (!jolt.alive) continue;
        if (jolt.wait > 0.0f) {
            jolt.wait -= seconds;
            continue;
        }
        jolt.left -= seconds;
        if (jolt.left <= 0.0f) {
            jolt.alive = false;
            continue;
        }
        if (quakeFor.shakes(jolt.owner, jolt.at[0], jolt.at[2])) {
            quake_ = float(int(roll() % 3u) - 3) * 0.7f;
        }
    }
}

}  // namespace mu::game
