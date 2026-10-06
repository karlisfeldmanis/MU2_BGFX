// The Golden Invasion's entrance: golden dragons crossing the sky in the rain, trailing fire, and
// the last of them coming down where the realm's Golden Dragon then stands.
//
// MuMain's sky is GOBoid.cpp:1205-1305 and :1389-1405: while `EnableEvent` is set, Lorencia's five
// bird slots fill with MODEL_DRAGON_ instead, each scaled 0.6 to 0.8, flying straight along -y (its
// Angle (0, 0, -90)) at `Scale * 40` units a reference frame, 300 units over the ground with a bob
// of up to 100 more, playing MONSTER01_DIE + 1 (its flight) at half speed, calling
// SOUND_MONSTER_BULLATTACK1 one frame in 128, and breathing -- a red BITMAP_LIGHTNING + 1 and a
// BITMAP_FIRE every frame at bone 11, 50 units out (:1547-1555); SubType 1 for the golden event
// draws it in the metal-and-chrome pair (the recipe's `plus`). What is ours, and why:
//
//   * **he never sees them, only their shadows** (the user, 2026-10-06: 'character never sees
//     dragons but sees just shadows, which will be more dramatic'): the crossings are drawn into
//     the sun's list alone, at 1.0 to 1.4 for MU's 0.6 to 0.8, so a dragon's shape sweeps over
//     the ground and his cry is heard, and the last, the diver, is the first one seen;
//   * **they never appear or vanish in frame.** MU puts each 2 to 6 m in front of the hero and
//     drops it after 128 to 256 frames wherever it is; here each starts 24 to 34 m back along the
//     same line and goes 40 m past him, the boids' rule (game/world/flight.h);
//   * **they come quickly and many**: three at once as the rain is in, then one every 0.6 to 1.4
//     seconds into ten slots, where MU's one in 300 a frame into five averages twelve -- the
//     entrance is thirty seconds (the user, 2026-10-06: 'longer with more dragons');
//   * **no fire falls** (the user, 2026-10-06: 'dont shoot meteors but better work on some special
//     effects for dragons when they fly'), neither MU's event fire (MODEL_FIRE subtype 3, a small
//     fireball drifting along -y with SOUND_METEORITE01) nor a meteor. In its place **each dragon
//     leaves a faint heat trail off both wingtips** (Bip01 L/R Finger02; the user: 'focus more on
//     fire trails', then 'more subtle and has to make sense, more blurry'): MU's soft flare
//     (flare01, the `light` sheet) in a dim ember orange, left where the wingtip passed, swelling
//     and fading over 0.7 s -- the air the burning dragon has heated, not flames. The only fire is
//     MU's own, at the mouth. Ours, every number;
//   * **their cries are heard everywhere**, as MU's PlayBuffer has them, but at MU's own rate for
//     five dragons however many fly;
//   * **the dive**: MU has no landing (OpenMU puts the monster on its tile and AppearMonster roars
//     it in). The last dragon flies in along the landed dragon's own facing, down from 8 m over
//     the last four seconds, slowing as it comes, and turns its flight into the roar it lands on.
//
// Not drawn: MU's ground darkened round the hero (AddTerrainLight -0.3 over 16 tiles), whose
// work the storm's wet light does here, and the faster wind in the grass.
#pragma once

#include <cstdint>
#include <vector>

#include "content/ground.h"
#include "game/crowd.h"
#include "game/figures.h"
#include "gfx/effects.h"
#include "gfx/renderer.h"

namespace mu::game {

class InvasionSky {
public:
    // Ten in the air at most, twice MU's five boid slots (the user, 2026-10-06: 'more dragons').
    static constexpr int kDragons = 10;

    // What the sky asked for this frame, for Play to sound and throw (it owns the sound and the
    // meteor).
    struct Asks {
        int calls = 0;             // cries this frame, heard unplaced
        std::vector<float> sparks; // x, y, z, along x, along z per breath spark
        bool landed = false;       // the dive touched down this frame
    };

    // The dragon's body, from this world's figures (GoldenDragon01), the breath glow's sheet,
    // `lightning_2`, and the heat trail's, `light`.
    void open(const FigureBody* dragon, bgfx::TextureHandle glow, bgfx::TextureHandle haze);
    bool isOpen() const { return body_ != nullptr; }

    // Begun: the rain comes in for `rainSeconds`, then the dragons; the dive lands at (x, z), in
    // world metres, facing `yaw`, `landSeconds` from now. Without `crossings` the dive alone: the
    // raid's one dragon (the user, 2026-10-06: 'we need that only 1 dragon boss spawnes on
    // lorencia').
    void begin(float x, float z, float yaw, float rainSeconds, float landSeconds,
               bool crossings = true);
    // Over: no more come; those in the air fly on out.
    void end();
    bool flying() const;

    void update(float seconds, const float hero[3], const content::Ground& ground, Asks& asks);
    void gather(gfx::Renderer& renderer, float* scratch, std::vector<gfx::Drawable>& out,
                std::vector<gfx::Drawable>* casters);
    // The breath's glow at each mouth, and the heat trails.
    void glow(gfx::Effects& effects) const;

private:
    // Where a dragon trails heat from: its two wingtips.
    static constexpr int kTrails = 2;
    struct Haze {
        float at[3];
        float age, life, spin;
        float scale;
    };
    struct Wyrm {
        bool live = false;
        bool diving = false;
        float at[3] = {0, 0, 0};
        float yaw = 0.0f;
        float scale = 0.7f;
        float timer = 0.0f;     // the bob's phase, MU's o->Timer
        float sparkDue = 0.0f;  // seconds to the next breath spark
        Figure figure;
        int palette = -1;
        float trailDue = 0.0f;  // seconds to the next flame left
        float trailWas[kTrails][3] = {};  // where each trail's last puff was left
        bool trailFed = false;
    };
    void trail(Wyrm& one, float seconds);
    int trailBones_[kTrails] = {-1, -1};
    std::vector<Haze> hazes_;
    bgfx::TextureHandle haze_ = BGFX_INVALID_HANDLE;
    float random01();
    void launch(Wyrm& one, const float hero[3], const content::Ground& ground);
    void breathe(Wyrm& one, float seconds, Asks& asks);

    const FigureBody* body_ = nullptr;
    bgfx::TextureHandle glow_ = BGFX_INVALID_HANDLE;
    int flightClip_ = -1;
    int mouth_ = -1;  // attack01, bone 11
    int roarClip_ = -1;
    Wyrm wyrms_[kDragons];
    Wyrm diver_;
    bool on_ = false;
    bool crossings_ = true;
    float clock_ = 0.0f;  // seconds since begin
    float rainSeconds_ = 0.0f, landSeconds_ = 0.0f;
    float landAt_[2] = {0, 0};
    float landYaw_ = 0.0f;
    float nextLaunch_ = 0.0f;
    float callDue_ = 0.0f;
    uint32_t seed_ = 0x2545F491u;
};

}  // namespace mu::game
