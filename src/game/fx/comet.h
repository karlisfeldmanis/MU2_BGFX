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
//   * **The landing** (MoveHandlers.cpp:2550-2573): MU's stones, BITMAP_EXPLOTION and ring.jpg
//     star are **not** kept (the user, 2026-10-06: the fire read as Meteorite's, the star as
//     jagged; "some kind of crystal explosion with smoke after"). Ours: Ice's shatter, ten
//     crystal shards, laid by the caller, which also sounds SOUND_EXPLOTION01 and shocks what
//     stands there as for a rock; a short soft glow (flare01) in the comet's blue; frost smoke
//     rising after; and the ribbon running on into the ground for a few frames.
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
    // Its ribbon: ten tails, one a reference frame. Ours (the user, 2026-10-06: the teal
    // trail looked wrong): MU's Scale 100 narrowed to 65, and JointLaser01's teal tinted to
    // the comet's own blue and dimmed by this, so the trail is the head's and not a stripe.
    static constexpr int kTails = 10;
    static constexpr float kRibbonUnits = 65.0f;
    static constexpr float kRibbonTint[3] = {0.55f, 0.42f, 0.75f};
    // After it lands, its ribbon a while yet, running into the ground and fading (frames).
    static constexpr float kTrailLinger = 6.0f;
    // The landing, ours (the user, 2026-10-06: "some kind of crystal explosion with smoke
    // after"): Ice's shatter -- ten crystal shards thrown off the ground, laid by the caller --
    // under a short soft glow, and frost smoke rising after. MU's ring.jpg star, spiky at 64
    // pixels and spinning, read as jagged shards of light; the glow is MU's flare01 (the
    // `light` sheet) in the comet's blue, swelling as it goes out.
    static constexpr float kFlashUnits = 170.0f;
    static constexpr float kFlashPeak = 0.7f;
    static constexpr float kFlashLift = 60.0f;
    static constexpr float kFlashFrames = 8.0f;
    static constexpr float kFlashTint[3] = {0.45f, 0.65f, 1.0f};
    // The smoke: smoke01 as Ice's frost wisps are drawn, a few puffs a landing, opening after
    // the glow, rising, spreading and going out.
    static constexpr int kPuffsALanding = 5;
    static constexpr float kPuffWaits = 4.0f;    // reference frames after the landing
    static constexpr float kPuffFrames = 55.0f;  // its life once open
    static constexpr float kPuffRise = 0.012f;   // metres a reference frame
    static constexpr float kPuffSpread = 0.45f;  // metres off the landing
    static constexpr float kPuffSize[2] = {0.7f, 1.8f};  // across, born and gone
    static constexpr float kPuffTint[3] = {0.34f, 0.42f, 0.56f};
    // Its light: MU's blue over two tiles while it falls, and the star's, fading with it.
    static constexpr float kGlow[3] = {0.2f, 0.4f, 1.0f};
    static constexpr float kGlowTiles = 2.0f;
    static constexpr float kFlashGlow = 0.5f;  // the star's light, of the falling comet's

    static constexpr int kMaxComets = 32;  // a cast and an echo's, twelve, and room
    static constexpr int kMaxFlashes = 32;
    static constexpr int kMaxPuffs = 128;

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
        bool landed;         // on the ground: no head, its ribbon lingering
        float linger;        // reference frames of ribbon left
    };
    struct Flash {
        bool alive = false;
        float at[3];
        float size;
        float spin;
        float left;  // reference frames
    };
    struct Puff {
        bool alive = false;
        float at[3];
        float spin;
        float wait;  // reference frames before it opens
        float age;   // reference frames since
        float weight;
    };

    std::vector<EffectCorner> mesh_;
    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle trailSheet_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle flashSheet_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle smokeSheet_ = BGFX_INVALID_HANDLE;
    const content::Ground* ground_ = nullptr;
    Live comets_[kMaxComets] = {};
    Flash flashes_[kMaxFlashes] = {};
    Puff puffs_[kMaxPuffs] = {};
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
