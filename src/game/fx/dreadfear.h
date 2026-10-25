// A Kanturu Relics Dreadfear's shows: its two teal eyes, the cloud its wings shed as it moves
// and swings, and the smoke they break into as it dies. Its sword's blur is the recipe's
// show.claws (play_show.cpp).
//
// MuMain, GM_Kanturu_2nd.cpp:
//
//   Create_Kanturu2nd_Monster, MONSTER_DREADFEAR (:86-104): Scale 1.3; bones Dreadfear_Wing32
//   (71), _Wing34 (68), _Wing51 (50), _Wing53 (47), _Eye52 (9) and _Eye54 (10), ours Bone32,
//   Bone34, Bone51, Bone53, Bone52 and Bone54.
//
//   Render_Kanturu2nd_MonsterVisual, MODEL_DREADFEAR (:985-1080), every frame: a BITMAP_LIGHT + 1
//   (Object9/Impack03) sprite at 0.45-0.54 on each eye in (0.25, 0.7, 0.6). Neither standing
//   (STOP1) nor dying: one frame in three a BITMAP_CLUD64 sub 0 at Wing32 and Wing34, and one
//   frame in three at Wing51 and Wing53. Dying: each frame a BITMAP_SMOKE + 3 sub 3 at each of
//   the four. (Its BITMAP_ENERGY_RING and _FIELD under it are the Tower's Maya battle alone.)
//
//   Clud64 sub 0 (ZzzEffectParticle.cpp:951-963, 5097-5106): LifeTime 40, three in four drawn
//   as BITMAP_SMOKE instead, scale 2.5-2.69 losing 0.03 a frame, turned at random, born 10
//   aside and 20 under its point, a Gravity of 0.5-1.4 falling 0.01 a frame that it sinks by,
//   its light LifeTime / 10 (whole until its last ten frames), its alpha losing 0.05 a frame.
//   Smoke + 3 sub 3 (:1741-1751, 5933-5940): drawn as Clud64, LifeTime 60, scale 1.28-2.54,
//   born 125 aside and up to 45 under, white, its light / 1.012 a frame, growing 0.01 a frame
//   and turning by a Gravity of -5 to 5 halved.
//
// **Ours**: sprites at a third of MU's size, the user's measure for monster sprites. Not
// carried: the after-images MU draws its body with through Attack 2 (RenderObject_AfterImage,
// ZzzCharacter.cpp:8655-8658), its wings left out of its shadow (ZzzObject.cpp:9516-9519), and
// its body fading 0.1 a frame as it dies (GM_Kanturu_2nd.cpp:718-725). MU's numbers are per
// reference frame, 25 a second, in centimetres. Fed by Play::breedShows; nothing here reaches
// the sim.
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

class Dreadfear {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    void shutdown();

    // One Dreadfear this frame: `standing` while it plays its first idle, `dying` once it has
    // fallen.
    void watch(uint32_t id, const Figure& figure, bool standing, bool dying, float fade,
               float seconds);
    void update(float seconds);
    void gather(gfx::Effects& effects) const;

private:
    struct One {
        bool found = false;
        bool seen = false;
        int eyes[2] = {-1, -1};          // Bone52, Bone54
        int wings[4] = {-1, -1, -1, -1};  // Bone32, Bone34, Bone51, Bone53
        float owed = 0.0f;
        float eyeAt[2][3] = {};
        bool eyeSeen[2] = {false, false};
        float eyeScale = 0.5f;
        float fade = 1.0f;
    };
    struct Puff {
        float at[3] = {0, 0, 0};
        float scale = 1.0f, light = 1.0f, alpha = 1.0f, spin = 0.0f, turn = 0.0f, gravity = 0.0f;
        float age = 0.0f, life = 40.0f, fade = 1.0f;
        bool smoke = false;  // drawn with BITMAP_SMOKE, else Clud64
        bool dying = false;  // Smoke + 3 sub 3, else Clud64 sub 0
    };

    float unit();
    void shed(const float at[3], float fade);
    void dissolve(const float at[3], float fade);

    std::unordered_map<uint32_t, One> ones_;
    std::vector<Puff> puffs_;
    uint32_t seed_ = 0x2d7f9e41u;
    bgfx::TextureHandle eye_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle cloud_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;
};

}  // namespace mu::game
