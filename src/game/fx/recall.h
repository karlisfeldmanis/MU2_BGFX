// Summon, the Dark Lord's: what MU draws for it (docs/plans/dark-lord-spells/summon.md).
//
// **The cast.** PLAYER_ATTACK_TELEPORT (raw 81, 8 keys at 0.28), and 5 ticks after the echo the
// AttackStage jump lets it go (ZzzCharacter.cpp:2904-2910, 4385-4390): two circles at his feet and
// SOUND_PART_TELEPORT, eSummon.wav, whole. They do not follow him.
//
//   * **MODEL_CIRCLE subtype 2**: Hellfire's disc (Circle01) in BITMAP_MAGIC_EMBLEM, Effect/Magic_b
//     -- the blue eight-bladed star -- lit (0.5, 0.5, 1) (ZzzObject.cpp:1540-1544), LifeTime 250,
//     BlendMeshLight `(250 - LifeTime) * 0.1` over its first ten frames and `LifeTime * 0.1` after,
//     which glColor clamps at one (ZzzEffect.cpp:2137-2158, MoveHandlers.cpp:3787-3797).
//   * **MODEL_CIRCLE_LIGHT subtype 3**: Hellfire's wall (Circle02, magic_a02) lit (0.1, 0.1, 10)
//     (ZzzObject.cpp:1534-1538), LifeTime 250, the same rise and fall **capped at 0.5**, its sheet
//     scrolled `-LifeTime * 0.01` along U, the ground lit white by it four tiles round; one frame in
//     two while LifeTime > 30, at a point within 200 units: a BITMAP_FLARE_BLUE spark that rises
//     (life 30-39, z-speed 0-2 growing 0.4 a frame to 8, scale 0.2, x0.98 its last five;
//     ZzzEffectParticle.cpp:3949-3962), and while LifeTime > 40 a BITMAP_FLARE_BLUE joint 19 600
//     units over it, scale 40, twenty tails, life 25-74, that hangs until its life is 25 and then
//     falls at 35-54 a frame growing 5 (ZzzEffectJoint.cpp:1670-1673, 1847-1853, 5514-5523) -- the
//     pillars of light raining onto the star in every clip (MoveHandlers.cpp:3865-3897).
//
// **The hold.** Past key 5.5 the clip runs at a tenth (ZzzCharacter.cpp:2502-2507) -- the drawing's
// (Drawn::recallHold) -- and each frame of it a BITMAP_LIGHT (flare01) rises off bone 42, the left
// hand's, blue (0.3, 0.5, 1), life 10-19, scale 0.5-1 (ZzzCharacter.cpp:4122-4131,
// ZzzEffectParticle.cpp:3159-3164, 7941-7960).
//
// **Ours, and marked**: MU's terrain light is a point light, as Hellfire's is (fx/hellfire.h).
//
// Presentation only: who comes, and when, is the realm's (Realm::recall).
#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "content/ground.h"
#include "content/texture.h"
#include "game/fx/effect_mesh.h"
#include "gfx/effects.h"
#include "gfx/renderer.h"

namespace mu::game {

class Recall {
public:
    // The two circles out of effects/hellfire, Magic_b out of effects/recall, the flares. False
    // when one is missing.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Ground* ground);

    // His cast: the circles and the sound kLetGo seconds on.
    void begin(uint32_t who);
    // A body holding the raised arm this frame: the glitter off its left hand.
    void holding(uint32_t who);

    // Every frame. `feet(id, out, &yaw)` where he stands on the ground and his facing; `point(id, bone, local, out)`
    // where `local`, metres in that bone's frame, stands in the world; `cue(id)` eSummon.
    template <typename Feet, typename Point, typename Cue>
    void update(float seconds, Feet feet, Point point, Cue cue);

    void gatherEffects(gfx::Effects& effects) const;
    uint32_t lights(gfx::PointLight* out, uint32_t max) const;
    void clear();

    // The rig's bone the glitter rises off: `hand_bofdgne01`, a child of Bip01 L Hand.
    static constexpr int kHandBone = 42;

private:
    static constexpr float kReferenceFps = 25.0f;
    static constexpr float kUnit = 0.01f;
    static constexpr float kLetGo = 5.0f / kReferenceFps;  // the AttackStage jump, 5 ticks
    static constexpr float kLife = 250.0f;                 // both circles' LifeTime
    static constexpr float kRise = 10.0f;                  // frames to full, and from it
    static constexpr float kWallCap = 0.5f;
    static constexpr float kSigilColour[3] = {0.5f, 0.5f, 1.0f};
    static constexpr float kWallColour[3] = {0.1f, 0.1f, 10.0f};
    static constexpr float kGlowTiles = 4.0f;
    static constexpr float kReach = 200.0f;      // rand() % 200 round him
    static constexpr float kSparkLast = 30.0f;   // sparks while LifeTime > 30
    static constexpr float kPillarLast = 40.0f;  // pillars while LifeTime > 40
    static constexpr float kPillarHigh = 600.0f;
    static constexpr float kPillarWidth = 40.0f;
    static constexpr int kTails = 20;

    struct Charge {
        bool alive = false;
        uint32_t who = 0;
        float left = 0.0f;
    };
    struct Ring {
        bool alive = false;
        float at[3] = {};
        float yaw = 0.0f;   // his, at the let-go: MU makes both off o->Angle
        float life = 0.0f;  // LifeTime left, reference frames
        float due = 0.0f;   // frames owed to the one-in-two spawn
    };
    struct Spark {
        bool alive = false;
        float at[3] = {};
        float rise = 0.0f, life = 0.0f, light = 1.0f, scale = 0.2f;
        float colour[3] = {1.0f, 1.0f, 1.0f};
        bool hand = false;  // a hand's flare01 rather than flareBlue
    };
    struct Pillar {
        bool alive = false;
        float head[3] = {};
        float tails[kTails][3] = {};
        int count = 0;
        float fall = 0.0f, life = 0.0f, due = 0.0f;
    };
    struct Holder {
        uint32_t who = 0;
        bool seen = false;
        float due = 0.0f;
    };
    static constexpr int kCharges = 8, kRings = 4, kSparks = 192, kPillars = 64, kHolders = 8;

    struct Model {
        std::vector<EffectCorner> tris;
        bgfx::TextureHandle sheet = BGFX_INVALID_HANDLE;
    };
    Model sigil_, wall_;
    bgfx::TextureHandle blueSheet_ = BGFX_INVALID_HANDLE, handSheet_ = BGFX_INVALID_HANDLE;
    const content::Ground* ground_ = nullptr;
    Charge charges_[kCharges] = {};
    Ring rings_[kRings] = {};
    Spark sparks_[kSparks] = {};
    Pillar pillars_[kPillars] = {};
    Holder holders_[kHolders] = {};
    uint32_t dice_ = 0x6a09e667u;

    float unit();
    float metre() const { return (ground_ ? ground_->metresPerTile() : 1.0f) * kUnit; }
    // A circle's light at `life` frames left: up over its first ten, then `LifeTime * 0.1`.
    static float lightOf(float life) {
        const float light = life > kLife - kRise ? (kLife - life) * 0.1f : life * 0.1f;
        return light < 0.0f ? 0.0f : light;
    }
    void spawn(const Ring& ring);
    void glitter(const float hand[3]);
    void step(float frames);
};

template <typename Feet, typename Point, typename Cue>
void Recall::update(float seconds, Feet feet, Point point, Cue cue) {
    seconds = std::fmin(seconds, 0.1f);
    const float frames = seconds * kReferenceFps;
    for (Charge& one : charges_) {
        if (!one.alive) continue;
        one.left -= seconds;
        if (one.left > 0.0f) continue;
        one.alive = false;
        float at[3], yaw = 0.0f;
        if (!feet(one.who, at, &yaw)) continue;
        for (Ring& ring : rings_) {
            if (ring.alive) continue;
            ring = Ring{};
            ring.alive = true;
            for (int k = 0; k < 3; ++k) ring.at[k] = at[k];
            ring.yaw = yaw;
            ring.life = kLife;
            break;
        }
        cue(one.who);
    }
    // The glitter: one flare a reference frame off the left hand of each body holding.
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    for (Holder& one : holders_) {
        if (one.who == 0) continue;
        if (!one.seen) {
            one = Holder{};
            continue;
        }
        one.seen = false;
        one.due += frames;
        float hand[3];
        const bool placed = point(one.who, kHandBone, origin, hand);
        while (one.due >= 1.0f) {
            one.due -= 1.0f;
            if (placed) glitter(hand);
        }
    }
    step(frames);
}

}  // namespace mu::game
