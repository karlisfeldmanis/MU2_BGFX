// What a level looks like: fifteen flares climbing out of the ground around whoever earned it.
// Ported from MU2's `client/core/Aura.cs`, the level-up recipe only -- its second recipe, the
// knight's Defense barrier, was a bench invention on a skill this engine does not have yet.
//
// MuMain's `ReceiveLevelUp` is the whole of it:
//
//     for (int i = 0; i < 15; ++i)
//         CreateJoint(BITMAP_FLARE, o->Position, o->Position, o->Angle, 0, o, 40, 2);
//     CreateEffect(BITMAP_MAGIC + 1, o->Position, o->Angle, o->Light, 0, o);
//     PlayBuffer(SOUND_LEVEL_UP);
//
// Fifteen joints on a ring of 40 units, each at its own phase and its own climb of 2.00 to
// 4.49 units a tick, living fifty ticks and dimming by 1/1.3 a tick over the last ten. Each
// trail is MU's CROSS -- two flat strips 40 units wide at right angles, laid in the hero's
// facing at the moment it is thrown -- and not a camera-facing ribbon. The fade down a trail
// is Flare's own painting, walked across by U, and the colour is the character's white.
//
// Carried over from MU2, each a departure it made and argued in Aura.cs:
//   * the ring's centre is the character, which is MU's call with its one missing line put
//     back (`TargetPosition` is never assigned for this joint);
//   * the trail is nineteen reference ticks long at any frame rate, not nineteen frames;
//   * the blue ground circle is written and switched OFF in play: MU2's crowd silenced it at
//     the user's asking, a three-tile wash of blue under a character being bitten.
//
// And this engine's own, three inventions made for a clean picture (user, 2026-09-22):
//   * **the trail is not stored, it is sampled.** MU lays one tail point a frame and MU2 one
//     a tick; the ring turns half a radian a tick, so either way the trail is a polygon of
//     29-degree chords and its U steps each time a point is laid. The path is closed-form --
//     a circle and a straight climb -- so the trail is drawn from the formula at kSamples a
//     tick, the same curve MU walks, with its corners taken off and its U continuous;
//   * **a strip fades as it turns edge-on.** MU's fixed cross goes to a hard bright line
//     wherever the path runs along one of its strips; those slivers are faded by how squarely
//     the strip faces the eye;
//   * **the flares are dimmer.** At full light, in this engine's HDR, the core of the burst
//     burned white; see kStrength in aura.cpp, judged on fixed-step shots.
//
// The sound is not here: nothing in this engine plays one yet.
//
// It stays where it was thrown and does not follow a character who walks away -- MU's, since
// `TargetPosition` is a point that nothing updates. It is `game`: it knows a ground.
#pragma once

#include <cstdint>
#include <string>

#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::content {
class Ground;
}

namespace mu::game {

class Aura {
public:
    // Takes the flare and the circle. False only when the flare is missing, which is the part
    // that is the effect; a missing circle is logged and the flares are drawn alone.
    bool open(const std::string& assetDir, content::Textures& textures);
    void shutdown();

    // Throws one at `feet` (world metres), in the facing `yaw` (the figure's own, forward along
    // (sin yaw, cos yaw)). Lengths are MU's units at `metresPerTile / 100`, a world length and
    // not the character's: a ring on the ground is a ring on the ground.
    void rise(const float feet[3], float yaw, float metresPerTile);

    // Ages every burst on MU's clock. `seconds` is the frame's own.
    void update(float seconds);
    // `eye` is the camera's position in world metres, which the edge-on fade is taken from.
    void gather(gfx::Effects& effects, const content::Ground& ground, const float eye[3]) const;

    // MU's circle under the flares. Off in play, as MU2 had it; see the header note.
    void setCircle(bool on) { circle_ = on; }
    int live() const;

    static constexpr int kJoints = 15;
    // MaxTails - 1 for BITMAP_FLARE: how many ticks long a trail grows.
    static constexpr int kTrailTicks = 19;
    // Points sampled a tick: the ring turns 0.5 rad a tick, so four is a 7-degree chord.
    static constexpr int kSamples = 4;
    // Two at once is a level earned while the last is still in the air; a third is refused.
    static constexpr int kBursts = 2;

private:
    struct Joint {
        float phase = 0.0f;  // rand() % 500 - 250
        float rise = 0.0f;   // metres a reference tick
    };
    struct Burst {
        bool living = false;
        float feet[3] = {0.0f, 0.0f, 0.0f};
        float across[3] = {1.0f, 0.0f, 0.0f};  // the joint's local X, turned by the hero's yaw
        float yaw = 0.0f;
        float per = 0.01f;    // metres in one of MU's units
        float age = 0.0f;     // reference ticks since it was thrown
        Joint joints[kJoints];
    };

    // Where joint `j` of `b` was `back` ticks ago, as an offset from the feet.
    static void at(const Burst& b, const Joint& j, float back, float out[3]);
    void gatherCircle(gfx::Effects& effects, const content::Ground& ground, const Burst& b) const;

    bgfx::TextureHandle flare_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle ground_ = BGFX_INVALID_HANDLE;
    bool circle_ = false;
    Burst bursts_[kBursts];

    // Drawn only by the drawing and never seeded: where a flare starts on its ring is not a
    // fact, and must not reach the sim's log. The same xorshift the showing uses.
    uint32_t seed_ = 0x2545F491u;
    float unit();
};

}  // namespace mu::game
