// What a ridden mount kicks up as it runs. GOBoid.cpp's MoveMount, for the Horn of Uniria's
// horse while its rider's clip is a walk or a run (:542-556):
//
//   if (rand_fps_check(2) && WorldActive != WD_10HEAVEN)
//       Position = o->Position + (rand()%64 - 32, rand()%64 - 32, rand()%32 - 16);
//       CreateParticle(WorldActive == 2 ? BITMAP_SMOKE : BITMAP_SMOKE + 1, Position, o->Angle, (1,1,1));
//
// Devias's white puff is BITMAP_SMOKE, the Bull Fighter's (fx/snort.h), and the caller hands it
// there; this is the other one, BITMAP_SMOKE + 1, Effect/smoke02.tga, brown dust on every other
// map (docs/mount.md). Subtype 0 (ZzzEffectParticle.cpp:1663-1690, 7054-7072): LifeTime 32,
// born +-8 more on the ground's two axes, Scale (rand()%32 + 32) * 0.01, a Velocity of 3 along
// the mount's local +Y -- behind it, as MU's models look down -Y -- and then every reference
// frame Light = LifeTime / 32, Scale += 0.08, Position += Velocity, Velocity *= 0.9, and the
// height held on the terrain at half the sprite's own. Drawn by EnableAlphaBlend3, which is the
// soft `Dust` blend the Budge Dragon's smoke02 already goes through.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/ground.h"
#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class Dust {
public:
    // Takes smoke02 off the cooked showing. Not fatal: without it a mount raises no dust.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table, const content::Ground* ground);
    void shutdown();

    // One puff at `at` (world metres, on the ground), drifting back from a mount facing `yaw`.
    void puff(const float at[3], float yaw);

    void update(float seconds);
    void gather(gfx::Effects& effects) const;

private:
    struct Puff {
        float position[3] = {0, 0, 0};
        float velocity[2] = {0, 0};  // metres a reference frame, on the ground's two axes
        float scale = 0.5f;
        float life = 32.0f;
        float spin = 0.0f;
    };

    const content::Ground* ground_ = nullptr;
    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;
    std::vector<Puff> puffs_;
    uint32_t dice_ = 0x6a09e667u;
    bool open_ = false;

    uint32_t roll();
};

}  // namespace mu::game
