// Inferno's ring: what MU makes at the wizard's feet as he lets the spell go. MuMain's
// `CreateInferno(o->Position)` and MODEL_SKILL_INFERNO sub-type 0 (ZzzCharacter.cpp:4574-4583).
//
// **What MU does, and all of it is kept but the ground's dark.**
//
//   * **CreateInferno** (ZzzEffect.cpp:6435-6475), sub-type 0: eight points 220 units out at every
//     45 degrees -- not turned by his facing -- and at each a `CreateBomb(p, true, 0)` and two
//     MODEL_STONEs. A bomb (ZzzEffect.cpp:6275-6347) is twenty BITMAP_SPARKs at sub-type 2 and a
//     BITMAP_EXPLOTION, both eighty units up; the explosion's birth is what plays
//     SOUND_EXPLOTION01 (eExplosion, one voice: the eight are heard as one). The stones are the
//     meteor's (fx/meteor.h `stones`), lent through `cast`.
//   * **A spark at sub-type 2** (ZzzEffectParticle.cpp:2014-2030, mover 6556-6600): sub-type 0's
//     at twice the scale and three times the speed -- Scale (rand()%4 + 4) * 0.2, LifeTime
//     rand()%16 + 24, Gravity rand()%16 + 6, thrown (0, (rand()%20 + 20) * 0.3, 0) turned by the
//     bomb's pitch of 150-209 degrees and a random yaw, so out flat and a little up or down. Every
//     frame Light = LifeTime / 16, height += Gravity, Gravity -= 2, and on the ground it bounces
//     at 0.6 and loses four frames. Drawn as forge's motes are (fx/forge.cpp): Spark02, Flame.
//   * **MODEL_SKILL_INFERNO** (Inferno01, ZzzEffect.cpp:1378-1388, Move_MODEL_SKILL_INFERNO
//     MoveHandlers.cpp:2627-2690): scale 0.9, LifeTime 15, BlendMesh -2 -- every mesh added --
//     at `BlendMeshLight = LifeTime / 20` under its Light of 0.8, its one action stepped at 0.5
//     keys a frame: ring3, a wall of fire 0.6 to 3.9 metres out, and ring4, an eight-metre square
//     of it flat under him, each part spinning (+30 and -45 degrees a key). The keys are blended
//     as a turn, radius and height and the angle the short way round, which is what MU's bone
//     blend does to a spin; a straight blend of the corners would shrink it between keys.
//
// **Ours, and marked: the blasts read as one ring** (the user, 2026-10-02: "fire exploisons has
// to be better blender so we dont see so much invidivudal blasts but they are like one"). MU's
// eight BITMAP_EXPLOTIONs, white and 256 units across at 220 units out, drawn at this bloom were
// eight white blooms with gaps between. Here the ring is sixteen of the same flipbook
// (Explotion01, MU's 4 x 4 at a cell every two frames), smaller than MU's but close enough that
// each overlaps its neighbours, each turned its own way and a little in or out so no two repeat,
// tinted warm and dim so the overlaps sum to one ring of fire rather than to a white blot. One
// warm ground light at the middle stands for their eight. Each blast sits on a soft flare twice
// its size (flare01, the meteor's glow), and there are twenty-four of them at two-thirds the
// light, so the flipbook's own detail melts into one blurred band (the user, 2026-10-02: "fire
// explosion has to be more blurry well blended").
//
// **Ours too: a little smoke after it** (the user, the same day: "we need some minimal smoke
// after the inferno cast"). Seven faint puffs of smoke01 born round the ring as the fire goes,
// rising slowly and opening, gone in two seconds -- at a quarter, after 'smoke to much
// vissible' at a half. MU leaves nothing behind.
//
// **Not ported**: `AddTerrainLight` at (-0.5, -0.5, -0.5) over five tiles, the ground darkened
// under the ring -- this renderer only adds, as fx/ice.h and fx/storm.h say.
//
// Presentation only, `game` and not `sim`: the realm struck everything within four tiles on the
// tick (`Realm::strikeAround`). Pools are sized once and a frame allocates nothing.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "content/ground.h"
#include "content/showing.h"
#include "content/texture.h"
#include "game/fx/effect_mesh.h"
#include "gfx/effects.h"
#include "gfx/renderer.h"

namespace mu::game {

class Inferno {
public:
    // The nine poses and the two ring sheets out of effects/inferno, the spark off the table.
    // False when the poses or the sheets are missing.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table, const content::Ground* ground);

    // The ring at `feet`, turned to `yaw`, as the spell is let go. `stones(at)` is called for
    // each of the eight bombs: the meteor's stones. The spell is both halves; a Tarkan monster's
    // blow asks for one: `bombs`, MU's CreateInferno (the eight bombs, their blasts, sparks,
    // smoke and light), and `mesh`, MODEL_SKILL_INFERNO (the wall and floor of fire). The
    // Tantallos and Zaikan throw the bombs alone, the Beam Knight the mesh alone and the Death
    // Beam Knight both (ZzzCharacter.cpp:1808-1846, :1880-1886).
    template <typename Stones>
    void cast(const float feet[3], float yaw, Stones stones, bool bombs = true, bool mesh = true);

    void update(float seconds);
    void gatherEffects(gfx::Effects& effects) const;
    uint32_t lights(gfx::PointLight* out, uint32_t max) const;
    void clear();

private:
    static constexpr float kReferenceFps = 25.0f;
    static constexpr float kUnit = 0.01f;
    static constexpr float kTwoPi = 6.28318531f;
    static constexpr float kDegrees = 3.14159265f / 180.0f;

    static constexpr int kKeys = 9;                 // Inferno01_00..08, all that 15 frames reach
    static constexpr float kKeysPerFrame = 0.5f;    // Velocity
    static constexpr float kFrames = 15.0f;         // LifeTime
    static constexpr float kScale = 0.9f;
    static constexpr float kLight = 0.8f;           // o->Light, sub-type 0
    static constexpr int kBombs = 8;
    static constexpr float kBombReach = 220.0f;     // units out, every 45 degrees
    static constexpr float kBombLift = 80.0f;       // CreateBomb's Position[2] += 80
    static constexpr int kSparksPerBomb = 20;
    static constexpr float kMotePixels = 4.0f;      // Spark02 is four pixels across
    static constexpr float kMoteBright = 16.0f;     // the LifeTime its Light is 1 at
    // fs_flame's shape and heat for a mote, forge's (fx/forge.cpp kSparkShape, kMoteHeat).
    static constexpr float kSparkShape = 2.0f / 20.0f;
    static constexpr float kMoteHeat = 0.75f;
    // The blasts: Explotion01's flipbook, MU's ten cells (a cell every two of its twenty
    // frames) -- ours, walked over the ring's own fifteen and dimmed over the last third, so the
    // ring, its blasts, its sparks and its light all end together (the user, 2026-10-02: "fire
    // explosioon has to disepear faster all has to be in cync").
    static constexpr float kBlastFrames = kFrames;
    static constexpr int kBlastCells = 10;
    static constexpr float kBlastFadeShare = 1.0f / 3.0f;
    // Ours too: a spark lives at most the ring's fifteen frames, where MU's live 24 to 39.
    static constexpr float kSparkMostFrames = kFrames;
    static constexpr int kBlastGrid = 4;
    static constexpr float kBlastInset = 0.005f;
    static constexpr float kBlastUnits = 256.0f;
    // Ours: sixteen round the ring where MU has eight, three quarters of MU's size, at under a
    // third of white and warm, each a little in or out (see the head of this file). At half
    // again MU's size they swelled into one blot over the screen (the user, 2026-10-02:
    // "explosions are to crazy, need to calm down them or make smaller").
    static constexpr int kBlasts = 24;
    static constexpr float kBlastShare = 0.75f;
    static constexpr float kBlastJitter = 30.0f;  // units in or out
    static constexpr float kBlastTint[3] = {0.19f, 0.13f, 0.075f};
    // The soft flare under each, twice its size.
    static constexpr float kFlareShare = 2.0f;
    static constexpr float kFlareTint[3] = {0.16f, 0.08f, 0.025f};
    // The smoke after it: born as the fire is half gone, rising and opening, two seconds.
    static constexpr int kPuffsPerRing = 7;
    static constexpr float kPuffWait = 7.0f;     // reference frames after the burst
    static constexpr float kPuffFrames = 50.0f;
    static constexpr float kPuffBorn = 1.4f;     // metres across, and at its end
    static constexpr float kPuffGrown = 3.2f;
    static constexpr float kPuffRise = 0.45f;    // metres a second
    static constexpr float kPuffGrey = 0.45f;
    static constexpr float kPuffAlpha = 0.25f;
    // And one warm light at the middle for the eight MU's blasts would each have lit.
    static constexpr float kGlowTiles = 5.0f;
    static constexpr float kGlow[3] = {0.65f, 0.35f, 0.15f};

    struct Ring {
        bool alive = false;
        float at[3] = {};
        float yaw = 0.0f;
        float left = 0.0f;  // LifeTime, reference frames
        float key = 0.0f;
        bool bombs = true;  // its light is the blasts'
        bool mesh = true;   // the wall and floor are drawn
    };
    struct Spark {
        bool alive = false;
        float at[3] = {};      // metres
        float flat[2] = {};    // metres a reference frame, across the ground
        float rise = 0.0f;     // its Velocity's own up, metres a reference frame
        float gravity = 0.0f;  // MU's Gravity, metres a reference frame
        float scale = 1.0f;
        float left = 0.0f;
    };
    // A corner as a turn about the model's up: radius, angle, height, and the sheet's place.
    struct Polar {
        float r, angle, y, u, v;
    };
    static constexpr int kRings = 6;
    static constexpr int kSparks = kRings * kBombs * kSparksPerBomb;
    struct Blast {
        bool alive = false;
        float at[3] = {};
        float spin = 0.0f;
        float left = 0.0f;  // reference frames
    };
    static constexpr int kBlastPool = kRings * kBlasts;
    struct Puff {
        bool alive = false;
        float at[3] = {};
        float spin = 0.0f;
        float wait = 0.0f;  // reference frames before it shows
        float age = 0.0f;
    };
    static constexpr int kPuffPool = kRings * kPuffsPerRing;

    std::vector<Polar> poses_[kKeys];
    int keyCount_ = 0;
    size_t wallCorners_ = 0;  // ring3 is the first `wallCorners_` corners of a pose, ring4 the rest
    mutable std::vector<EffectCorner> wall_, floor_;  // blended into, sized once at open
    bgfx::TextureHandle wallSheet_ = BGFX_INVALID_HANDLE, floorSheet_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle spark_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle blastSheet_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle flareSheet_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle smokeSheet_ = BGFX_INVALID_HANDLE;
    Blast blasts_[kBlastPool] = {};
    Puff puffs_[kPuffPool] = {};
    const content::Ground* ground_ = nullptr;
    Ring rings_[kRings] = {};
    Spark sparks_[kSparks] = {};
    uint32_t dice_ = 0x1f3e2a07u;

    float unit();
    float between(float a, float b) { return a + (b - a) * unit(); }
    float metres() const { return ground_ ? ground_->metresPerTile() : 1.0f; }
    void sparksAt(const float at[3]);
};

template <typename Stones>
void Inferno::cast(const float feet[3], float yaw, Stones stones, bool bombs, bool mesh) {
    for (Ring& ring : rings_) {
        if (ring.alive) continue;
        ring = Ring{};
        ring.alive = true;
        for (int k = 0; k < 3; ++k) ring.at[k] = feet[k];
        ring.yaw = yaw;
        ring.left = kFrames;
        ring.bombs = bombs;
        ring.mesh = mesh;
        break;
    }
    if (!bombs) return;
    // A tile is a hundred of MU's units.
    const float perUnit = metres() / 100.0f;
    for (int j = 0; j < kBombs; ++j) {
        const float turn = float(j) * 45.0f * kDegrees;
        float at[3] = {feet[0] + std::sin(turn) * kBombReach * perUnit, feet[1],
                       feet[2] + std::cos(turn) * kBombReach * perUnit};
        if (ground_) at[1] = ground_->heightAt(at[0], at[2]);
        stones(at);
        const float up[3] = {at[0], at[1] + kBombLift * perUnit, at[2]};
        sparksAt(up);
    }
    // The ring of blasts, every 22.5 degrees, each a little in or out and turned its own way.
    int made = 0;
    for (Blast& one : blasts_) {
        if (made == kBlasts) break;
        if (one.alive) continue;
        const float turn = float(made) * (360.0f / float(kBlasts)) * kDegrees;
        const float reach = (kBombReach + between(-kBlastJitter, kBlastJitter)) * perUnit;
        one = Blast{};
        one.alive = true;
        one.at[0] = feet[0] + std::sin(turn) * reach;
        one.at[2] = feet[2] + std::cos(turn) * reach;
        one.at[1] = (ground_ ? ground_->heightAt(one.at[0], one.at[2]) : feet[1]) +
                    kBombLift * perUnit;
        one.spin = unit() * kTwoPi;
        one.left = kBlastFrames;
        ++made;
    }
    // The smoke, round the ring and a little in or out, waiting for the fire to go.
    made = 0;
    for (Puff& one : puffs_) {
        if (made == kPuffsPerRing) break;
        if (one.alive) continue;
        const float turn = (float(made) + unit()) * (360.0f / float(kPuffsPerRing)) * kDegrees;
        const float reach = (kBombReach + between(-60.0f, 40.0f)) * perUnit;
        one = Puff{};
        one.alive = true;
        one.at[0] = feet[0] + std::sin(turn) * reach;
        one.at[2] = feet[2] + std::cos(turn) * reach;
        one.at[1] = (ground_ ? ground_->heightAt(one.at[0], one.at[2]) : feet[1]) +
                    between(40.0f, 110.0f) * perUnit;
        one.spin = unit() * kTwoPi;
        one.wait = kPuffWait + between(0.0f, 3.0f);
        ++made;
    }
}

}  // namespace mu::game
