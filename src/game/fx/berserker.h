// A Kanturu Berserker's breath: the green smoke it lets out of its mouth while it is not swinging.
//
// MuMain, GM_Kanturu_1st.cpp:
//
//   RenderKanturu1stMonsterVisual, MODEL_BERSERK (:1090-1129): every frame it is neither swinging
//   nor dying, a BITMAP_SMOKE (Effect/smoke01) sub 42 at BERSERK_MOUTH (bone 9, Bone08, :417).
//   Sub 42 lives 16 frames at 0.29-0.47, turned to WorldTime as it is made; each frame its light is
//   (127, 255, 200)/255 times its frames left over 8 -- twice bright as it leaves the mouth, gone at
//   its end -- it rises by a pace that gains 0.2 a frame and grows 0.05 (ZzzEffectParticle.cpp:
//   1161-1166, 5576-5582).
//
// **Ours**: at a third of MU's size, the user's measure for monster sprites. MU's numbers are per
// reference frame, 25 a second, in centimetres. Fed by Play::breedShows; nothing here reaches the
// sim.
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

class Berserker {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    void shutdown();

    // One Berserker this frame: `breathes` while it neither swings nor has fallen.
    void watch(uint32_t id, const Figure& figure, bool breathes, float fade, float seconds);
    void update(float seconds);
    void gather(gfx::Effects& effects) const;

private:
    struct One {
        bool found = false;
        bool seen = false;
        int mouth = -1;
        float owed = 0.0f;
    };
    struct Puff {
        float at[3] = {0, 0, 0};
        float scale = 0.3f, rise = 0.0f, spin = 0.0f, age = 0.0f, fade = 1.0f;
    };

    float unit();

    std::unordered_map<uint32_t, One> ones_;
    std::vector<Puff> puffs_;
    uint32_t seed_ = 0x5b1e42a7u;
    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;
};

}  // namespace mu::game
