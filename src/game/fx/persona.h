// A Kanturu Relics Persona's shows: the light on its brow, the blue smoke round its feet, the
// water its two tails drip and the water it dies in; its storm and spirits are Play's own.
//
// MuMain, GM_Kanturu_2nd.cpp:
//
//   Create_Kanturu2nd_Monster, MONSTER_PERSONA (:57-69): bones PRSona_A1 (73), PRSona_Tail (76)
//   and PRSona_Tail1 (77), ours a1, Bip01 Tail and Bip01 Tail1.
//
//   Render_Kanturu2nd_MonsterVisual, MODEL_PERSONA (:763-860), every frame: a BITMAP_LIGHT
//   (Effect/flare01) sprite at 2.0 on PRSona_A1 in (0.5, 0.6, 1.0) at 0.7-0.99; one frame in five
//   a BITMAP_SMOKE sub 40 at its feet; one frame in three a BITMAP_WATERFALL_3 sub 5 at 0.8 under
//   each tail bone in (0.5, 0.5, 0.5). Attack 1, keys 3 to 4: a MODEL_STORM at its feet each
//   frame, walking the way it faces. Attack 2, keys 3 to 4: each frame two BITMAP_JOINT_SPIRIT
//   pairs (Scale 80 and 20) a metre over it at a random quarter, homing on itself. Dying:
//   BlendMesh -2, and each frame a sub 6 WATERFALL_3 under each tail and five more off random
//   bones.
//
//   The smoke's sub 40 (ZzzEffectParticle.cpp:1406-1417, 5559-5566): LifeTime 50, born 32 aside
//   and 32-96 up, thrown 40-47 units a frame at a pitch of -45 to 45 and a random yaw, its speed
//   0.4 of itself a frame, scale 0.8-1.11 growing 0.05 a frame, its light (0.5, 0.5, 1) at
//   LifeTime / 50. WATERFALL_3 sub 5/6 (:3642-3652, 8566-8585): LifeTime 20, scale 1.0-1.18
//   turned at random, born 20 aside and 50 under its point, a Gravity of 0-2; sub 5 sinks by its
//   Gravity, which falls 0.05 a frame, sub 6 rises by it, growing 0.05; both lose 1/1.1 of their
//   light a frame.
//
// **Ours**: one storm a swing, cast as Attack 1 reaches key 3, and Play's Evil Spirit release
// (eight joints at the four quarters) as Attack 2 does -- MU makes one of each every frame of
// keys 3 to 4, some three of them on the same spot. Toward what it attacks, or the way it faces.
// Sprites at a third of MU's size, the user's measure for monster sprites. Not carried:
// BlendMesh -2 as it dies, and the five drips off random bones. MU's numbers are per reference
// frame, 25 a second, in centimetres. Fed by Play::breedShows; nothing here reaches the sim.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <bgfx/bgfx.h>

#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class Figure;

class Persona {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    void shutdown();

    // What a swing asks of Play this frame.
    struct Asks {
        bool storm = false;    // Attack 1 reached key 3
        bool spirits = false;  // Attack 2 reached key 3
    };
    // One Persona this frame: `attack` 1 or 2 while it swings (0 not) at `key`, `dying` once it
    // has fallen, `feet` its own feet on the ground.
    Asks watch(uint32_t id, const Figure& figure, int attack, float key, bool dying,
               const float feet[3], float fade, float seconds);
    void update(float seconds);
    void gather(gfx::Effects& effects) const;

private:
    struct One {
        bool found = false;
        bool seen = false;
        int brow = -1;               // a1
        int tails[2] = {-1, -1};     // Bip01 Tail, Bip01 Tail1
        float owed = 0.0f;
        int swung = 0;               // the attack this swing already asked for, or 0
        float brightAt[3] = {0, 0, 0};
        float bright = 0.0f;         // this frame's brow light, 0 when not drawn
        float fade = 1.0f;
    };
    struct Puff {
        float at[3] = {0, 0, 0};
        float speed[3] = {0, 0, 0};  // metres a reference frame
        float scale = 1.0f, light = 1.0f, spin = 0.0f, age = 0.0f, life = 50.0f, fade = 1.0f;
        float gravity = 0.0f;        // the drips' MU units a frame
        int kind = 0;                // 0 the smoke, 5 a sinking drip, 6 a rising one
    };

    float unit();
    void drip(const float at[3], int kind, float fade);

    std::unordered_map<uint32_t, One> ones_;
    std::vector<Puff> puffs_;
    uint32_t seed_ = 0x51e7a0c3u;
    bgfx::TextureHandle light_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle water_ = BGFX_INVALID_HANDLE;
};

}  // namespace mu::game
