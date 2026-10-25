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
//     a BITMAP_SMOKE sub 11 and a MODEL_STONE1/2 sub-10 chip off it on each of LifeTime 36 to 33,
//     fading Alpha / 1.3 and BlendMeshLight / 2 a frame in its last 8 (ZzzEffect.cpp:3480-3520,
//     MoveHandlers.cpp:5687-5714). Its DhorSS mesh lit, then its DhorSS_R shell over it, bright
//     (TextureScript's `_R`, additive): the glowing lower half MU's footage shows. Refused on
//     no-move, no-ground and water tiles.
//   * **The smoke** (sub 11, ZzzEffectParticle.cpp:1255-1266, moved :5334-5341): smoke01 added,
//     LifeTime 50, Scale 2 growing 0.05 a frame, scattered 32 round and 32-96 up, a drift of 40-47
//     units a frame at a random pitch of 45 and any yaw, kept 0.4 of each frame, sinking a unit a
//     frame, its light (1, 0.8, 0.6) x LifeTime / 50.
//   * **The camera**: `EarthQuake = (rand() % 3 - 3) * 0.7` degrees every frame the spikes are made.
//   * **The crater** (MODEL_SKILL_FURY_STRIKE, Kind 2) is Rageful Blow's without its weapon --
//     game/fx/fury.h `cast(..., groundOnly)`, laid by Play.
//
// **Ours, and marked**: the spikes are drawn off the realm's `Loosed` 62, on the tick its numbers
// come (sim::kQuakeStrikes, MU's 25th frame), so the ground breaks with the blows; and MU's warm
// terrain light under every stone (0.79, 0.72, 0.49, two tiles, AddTerrainLight each frame it
// stands) is one light round him, reaching the outer ring's two tiles past it: the renderer takes
// four moving lights a frame (gfx::Renderer::kMaxTransientLights, a loop every lit pixel runs), and
// twelve stones would push every other skill's light out.
//
// MU's white CreateBlur off the horse's bone 9 every frame of its clip 3 (GOBoid.cpp:452-462) is not
// drawn: bone 9 (Object24) is a fixed node of DarkHorse.bmd, still in every key of clip 3, and
// TransformPosition without Translate leaves both ends in the model's own space -- a ribbon of one
// unmoving pair by the world's origin, which MU's player never sees.
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
    // **The Dark Horse's hoof in Icarus**: a BITMAP_SHOCK_WAVE sub-type 1 at `at` -- LifeTime 20,
    // Scale 1-1.9 growing 0-0.4 a frame, wandering 4 units a frame either way, laid on the
    // ground at the horse's light (GOBoid.cpp:346-385; ZzzEffect.cpp:3544-3548; MoveHandlers.cpp:
    // 5770-5776).
    void hoof(const float at[3]);
    // The spikes for `owner` at `feet`, `wait` seconds on: the realm's strike.
    void strike(uint32_t owner, const float feet[3], float wait);
    // `chip(at)` where a stone throws its chip (the meteor's stones).
    template <typename Chip>
    void update(float seconds, Chip chip);
    // `eye` is the camera: each stone's bright shell is laid over its own rock, its back half left
    // out as MU's depth test leaves it (the pass sorts and does not write depth).
    void gatherEffects(gfx::Effects& effects, float daylight, const float eye[3]) const;
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
    static constexpr float kWaveLight = 1.0f;    // the horse's Light, (1, 1, 1)
    static constexpr float kWaveOver = 0.06f;    // metres over the ground
    static constexpr float kRings[2] = {150.0f, 300.0f};
    static constexpr float kRingGap = 2.0f / kReferenceFps;  // the second ring two frames on
    static constexpr int kPerRing = 6;
    static constexpr float kStoneLife = 40.0f, kStoneSpeed = 0.3f;
    static constexpr float kJoltSeconds = 4.0f / kReferenceFps;  // frames 8 to 9.5 at 0.34
    static constexpr float kLightTiles = 5.0f;  // the outer ring's 3 and MU's 2 round a stone
    static constexpr int kChips = 4;            // LifeTime 36, 35, 34, 33
    static constexpr float kChipFrom = 4.0f;    // frames into its 40
    static constexpr float kPuffLife = 50.0f, kPuffScale = 2.0f, kPuffGrows = 0.05f;
    static constexpr float kPuffPixels = 64.0f;  // smoke01, Width x Scale units
    static constexpr float kPuffLight[3] = {1.0f, 0.8f, 0.6f};
    static constexpr float kLightColour[3] = {0.79f, 0.72f, 0.49f};

    struct Wave {
        bool alive = false;
        float at[3] = {};
        float wait = 0.0f, age = 0.0f;  // seconds to its birth, frames since
        bool hoof = false;               // sub-type 1, Earthshake::hoof
        float scale = 0.0f;              // a hoof's, in tiles
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
        int chips = 0;  // thrown so far
    };
    struct Puff {
        bool alive = false;
        float at[3] = {};
        float drift[3] = {};  // units a frame, kept 0.4 of each frame
        float age = 0.0f, scale = kPuffScale, spin = 0.0f;
    };
    struct Jolt {
        bool alive = false;
        uint32_t owner = 0;
        float at[3] = {};
        float wait = 0.0f, left = kJoltSeconds;
    };
    static constexpr float kHoofLife = 20.0f;
    static constexpr int kWaves = 32, kRingPool = 16, kStones = 96, kJolts = 8, kPuffs = kStones * kChips;

    struct Model {
        std::vector<EffectCorner> rock, seam;
        float lift = 0.0f;  // units under its risen key that its first key stands
    };
    Model models_[2];
    bgfx::TextureHandle rockTex_ = BGFX_INVALID_HANDLE, seamTex_ = BGFX_INVALID_HANDLE,
                        waveTex_ = BGFX_INVALID_HANDLE, smokeTex_ = BGFX_INVALID_HANDLE;
    const content::Ground* ground_ = nullptr;
    Wave waves_[kWaves] = {};
    Ring rings_[kRingPool] = {};
    Stone stones_[kStones] = {};
    Jolt jolts_[kJolts] = {};
    Puff puffs_[kPuffs] = {};
    float quake_ = 0.0f;
    uint32_t dice_ = 0x6a09e667u;

    uint32_t roll();
    float metre() const { return (ground_ ? ground_->metresPerTile() : 1.0f) * kUnit; }
    float floorAt(float x, float z) const { return ground_ ? ground_->heightAt(x, z) : 0.0f; }
    bool groundAt(float x, float z) const;
    void raise(const Ring& ring);
    void puff(const float at[3]);
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
        if (wave.hoof) {
            wave.scale += float(roll() % 5u) / 10.0f * frames;
            wave.at[0] += (float(roll() % 8u) - 4.0f) * frames * metre();
            wave.at[2] += (float(roll() % 8u) - 4.0f) * frames * metre();
        }
        if (wave.age >= (wave.hoof ? kHoofLife : kWaveScale)) wave.alive = false;
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
        // LifeTime 36 to 33: smoke and a chip off it each frame, 60 east, 60 south and 50 up of it.
        while (stone.chips < kChips && stone.age >= kChipFrom + float(stone.chips)) {
            ++stone.chips;
            const float m = metre();
            const float at[3] = {stone.at[0] + 60.0f * m, stone.at[1] + 50.0f * m, stone.at[2] + 60.0f * m};
            chip(at);
            puff(at);
        }
    }
    {
        const float m = metre(), keep = std::pow(0.4f, frames);
        for (Puff& one : puffs_) {
            if (!one.alive) continue;
            one.age += frames;
            if (one.age >= kPuffLife) {
                one.alive = false;
                continue;
            }
            for (int k = 0; k < 3; ++k) one.at[k] += one.drift[k] * frames * m;
            for (float& d : one.drift) d *= keep;
            one.at[1] -= frames * m;
            one.scale += kPuffGrows * frames;
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
