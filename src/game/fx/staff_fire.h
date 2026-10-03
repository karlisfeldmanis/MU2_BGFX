// The Staff of Resurrection's fire, while it is held. MuMain's render switch for a held weapon
// (RenderCharacter, ZzzCharacter.cpp:10302-10330), every frame, in the hand bone's own space:
//
//   case MODEL_STAFF_OF_RESURRECTION:
//       Position = (0, -145, 0)                         -- its head
//       CreateSprite(BITMAP_SPARK, p, 3.f, L*(1, 0.6, 0.4));
//       CreateSprite(BITMAP_SHINY + 2, p, 1.5f, L*(1, 0.6, 0.4));
//       for 4: Position = (rand 10 - 10, rand 10 - 100, rand 10 - 10)
//              CreateParticle(BITMAP_SPARK, p, Angle, L*(1, 0.6, 0.4), 1);
//       for j in 0..9, three times in four: Position = (0, 60 - 20 j, 0)
//              CreateSprite(BITMAP_LIGHT, p, 1.f, L*(1, 0.2, 0.1));
//
// with Luminosity rolled (rand() % 30 + 70) * 0.01 each frame (:9989): a flaring spark at the
// head, sparks thrown off below it, and ten red lights down the shaft flickering on and off.
//
// **Ours, marked: subtle.** The user turned down MU's sprites on held swords before (fx/gleam.h)
// and asked for these kept faint and small (2026-10-04, 'Yes, subtle'), as the monster auras
// and the castle's sparks were: every size and brightness here is a fraction of MU's, the
// sparks a quarter as many. Rolled at MU's 25 frames a second, not the monitor's.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class StaffFire {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    void shutdown();

    // Forgets the last frame's staffs and moves the sparks on. Called before the feeds.
    void update(float seconds);
    // One held staff this frame: its head, the point the sparks leave from, and the ten shaft
    // points, MU's from the hand down, all in world metres.
    void feed(const float head[3], const float sparks[3], const float shaft[10][3]);
    void gather(gfx::Effects& effects) const;

    // The points MU names, in the staff's own space in metres (its glb: along +z, MU's -y).
    static void points(float head[3], float sparks[3], float shaft[10][3]);

private:
    struct Staff {
        float head[3];
        float shaft[10][3];
    };
    struct Spark {
        float at[3];
        float velocity[3];
        float age = 0.0f;
        float life = 0.3f;
    };
    float unit();

    bgfx::TextureHandle spark_ = BGFX_INVALID_HANDLE;  // BITMAP_SPARK
    bgfx::TextureHandle shiny_ = BGFX_INVALID_HANDLE;  // BITMAP_SHINY
    bgfx::TextureHandle light_ = BGFX_INVALID_HANDLE;  // BITMAP_LIGHT, flare01
    std::vector<Staff> staffs_;
    std::vector<Spark> sparks_;
    // MU's per-frame rolls, held between its frames: the luminosity and which shaft lights burn.
    float luminosity_ = 0.85f;
    uint16_t lit_ = 0x3ff;
    float step_ = 0.0f;   // seconds to MU's next frame
    float owed_ = 0.0f;   // sparks owed, at a quarter of MU's four a frame
    uint32_t seed_ = 0x51AFFu;
    bool open_ = false;
};

}  // namespace mu::game
