// Cometfall's comets: what MU calls down on the tile a wizard names. MuMain's two
// MODEL_SKILL_BLASTs at the let-go (ZzzCharacter.cpp:4557-4567), drawn here as Meteorite's
// shower is, a comet for each of the realm's rocks (Realm::shower), at the weight it rolled.
//
// **What MU does, and what is kept.**
//
//   * **The comet** (ZzzEffect.cpp:1355-1366): Blast01, one mesh of crossed cards on ring2 --
//     a blue-white head with rays streaming up behind it -- added (BlendMesh 0), scale
//     (rand()%8 + 10) * 0.1, born 300 to 800 units over the tile and 200 to 300 aside, falling at
//     50 to 100 units a frame turned by its Angle (0, 20, 0). Kept: the model, its sheet, its
//     roll of scale, its slant, its heading -- every comet in from the east -- and its frame,
//     drawn at that Angle and not along its velocity. Ours: it lands on the realm's ground in
//     Meteorite's seven ticks (Meteor::fallSeconds) from its own height in MU's band, so the
//     blow and the comet arrive together.
//   * **Its ribbon** (`CreateJoint(BITMAP_JOINT_ENERGY, ..., 5, o, 100.f)`, ZzzEffectJoint.cpp:
//     210-288): ten tails following the comet, a metre across, white light, JointLaser01 added,
//     gone when the comet lands. Turned to the eye, as MU's joints are.
//   * **The ground light** while it falls: (0.2, 0.4, 1.0) over two tiles (AddTerrainLight,
//     MoveHandlers.cpp:2575-2577).
//   * **The landing** (MoveHandlers.cpp:2550-2573): the meteor's stones -- lent by fx/meteor.h
//     through the caller, which also sounds SOUND_EXPLOTION01 and shocks what stands there, as
//     for a rock -- and a BITMAP_SHINY + 4 (Effect/ring.jpg), the blue star drawn here. **Not
//     MU's BITMAP_EXPLOTION**: the user, 2026-10-06, read its fire as Meteorite's; the comet
//     lands blue. Its life is ours: MU's particle fades it in a
//     few frames, here twelve, spinning.
//
// Presentation only, `game` and not `sim`: the realm resolved every blow on the tick it was
// cast. Pools are sized once and a frame allocates nothing.
#pragma once

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

class Comet {
public:
    // Blast01 and ring2 out of effects/comet, the ribbon and the star off the cooked showing.
    // False when the model or its sheet is missing.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table, const content::Ground* ground);
    void shutdown();

    // One comet, landing at `x, z` (world metres) `fallSeconds` from now. `weight` is the
    // shower's rock's own (sim::kLightestRock..kHeaviestRock): it scales the comet, its ribbon,
    // its star and its light by its root, as Meteor::cast does a rock.
    void cast(float x, float z, uint32_t attacker, float weight, float fallSeconds);

    // Where one landed this frame: the caller lays the meteor's stones and blast there, sounds
    // the explosion and shocks the bodies round it.
    struct Landing {
        float x, y, z;      // world metres, the ground under it
        float weight;       // the drawn weight, the root of the shower's
        uint32_t attacker;
    };
    void update(float seconds, std::vector<Landing>& landings);

    void gather(gfx::Effects& effects, const float* eye) const;
    uint32_t lights(gfx::PointLight* out, uint32_t max) const;
    uint32_t refused() const { return refused_; }

private:
    static constexpr float kReferenceFps = 25.0f;
    static constexpr float kUnit = 0.01f;
    // Born 300 to 800 units up (`rand() % 500 + 300`), slanted in from the east at MU's
    // twenty degrees.
    static constexpr float kLowest = 300.0f, kHighest = 800.0f;
    static constexpr float kSlantDegrees = 20.0f;
    static constexpr float kSmallest = 1.0f, kLargest = 1.7f;  // (rand()%8 + 10) * 0.1
    // Its ribbon: ten tails, one a reference frame, Scale 100 across.
    static constexpr int kTails = 10;
    static constexpr float kRibbonUnits = 100.0f;
    // The star where it lands: ring.jpg, ours at this many units across and frames of life.
    static constexpr float kFlashUnits = 220.0f;
    static constexpr float kFlashLift = 80.0f;
    static constexpr float kFlashFrames = 12.0f;
    static constexpr float kFlashSpin = 0.08f;  // radians a reference frame
    // Its light: MU's blue over two tiles while it falls, and the star's, fading with it.
    static constexpr float kGlow[3] = {0.2f, 0.4f, 1.0f};
    static constexpr float kGlowTiles = 2.0f;

    static constexpr int kMaxComets = 32;  // a cast and an echo's, twelve, and room
    static constexpr int kMaxFlashes = 32;

    struct Live {
        bool alive = false;
        float at[3];
        float velocity[3];   // metres a second
        float floorY;
        float left;          // seconds to the ground
        float size;          // its roll times its drawn weight
        float weight;        // drawn: the root of the shower's
        float tails[kTails][3];
        int tailCount;
        float tailDue;       // reference frames to the next tail
        uint32_t attacker;
    };
    struct Flash {
        bool alive = false;
        float at[3];
        float size;
        float spin;
        float left;  // reference frames
    };

    std::vector<EffectCorner> mesh_;
    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle trailSheet_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle flashSheet_ = BGFX_INVALID_HANDLE;
    const content::Ground* ground_ = nullptr;
    Live comets_[kMaxComets] = {};
    Flash flashes_[kMaxFlashes] = {};
    uint32_t refused_ = 0;

    // The drawing's own dice, never the sim's.
    uint32_t dice_ = 0x5EEDC0DEu;
    float unit();
    float between(float a, float b) { return a + unit() * (b - a); }
    float metres() const { return ground_ ? ground_->metresPerTile() : 1.0f; }
    void gatherRibbon(gfx::Effects& effects, const Live& comet, const float* eye,
                      float fade) const;
    // Out at this far from the eye, whole from here. Ours: MU's band, 300 to 800 units up,
    // brought a comet within 5.6 m of our nearer camera (measured 2026-10-06, the arena at
    // level 220, eye ~11 m off the ground), and a head that near filled a quarter of the
    // screen in flat white and teal blocks. An added mesh fades by its colour.
    static constexpr float kNearGone = 6.0f, kNearWhole = 9.0f;
    static float nearness(const float at[3], const float* eye);
};

}  // namespace mu::game
