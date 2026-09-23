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

// What a burst is made of. **Two recipes, as MU2's `Aura.cs` had two**: MU's level-up flares,
// and the knight's Defense barrier -- which this engine left out in so many words, "a bench
// invention on a skill this engine does not have yet". It has the skill now.
//
// The barrier's numbers are MU2's Defense recipe (`Shape.Barrier, 5, 20, 100, Circle, Follows,
// Tails 30, Light (0.4, 0.8, 0.2)`), and what they are taken FROM is worth repeating here
// because it is the one thing about this effect that is not traced: MuMain gives skill 18 a
// clip and a sound and nothing else -- no buff, no joints, no icon. The vocabulary is borrowed
// from the one defensive skill MU does draw, the elf's Greater Defense
// (`ZzzCharacter.cpp:4925`), which throws `CreateEffect(BITMAP_MAGIC + 1)` and five
// `CreateJoint(MODEL_SPEARSKILL, ..., 4, to, 20.0f)` -- subtype 4 being MU's barrier.
struct Recipe {
    int joints = 15;
    float orbit = 40.0f;      // the ring's radius, in MU units
    float wide = 40.0f;       // the cross's own width
    float ticks = 50.0f;      // how long one lives, in reference ticks
    float slowRise = 2.00f;   // MU units a reference tick
    float fastRise = 4.49f;
    float trail = 19.0f;      // how many ticks of tail it grows to
    float band = 0.0f;        // MU units of body the ribbons are spread up, 0 to climb instead
    bool spread = false;      // phases laid evenly round the ring rather than drawn
    bool follows = false;     // whether it walks with the body
    bool circle = false;      // MU's ground circle thrown with it
    float light[3] = {1.0f, 1.0f, 1.0f};
};

// MU's level-up: fifteen flares on a ring of forty, climbing away and dimming. The circle is
// switched on from outside (`setCircle`), which is where it has always been.
inline constexpr Recipe kRising{};
// And the guard: five ribbons evenly round a ring of twenty, climbing a hundred units over the
// boon's own length, following the body, in the elf's green, under one flash of the circle.
// The trail is one turn of the ring and no more (the ring turns half a radian a tick, so
// twelve ticks is a shade under a full circle), and the five ribbons are spread up a hundred
// units of him rather than climbing away: a guard is a cage that stands, and MU's climb is what
// makes the level-up's flares LEAVE. That much is this engine's reading of `Shape.Barrier`.
// The band is 150 and not MU2's 100 because a hundred units is a tile, and a tile is his waist:
// the cage has to close over his head to read as one. And the green is pushed well past MU2's
// (0.4, 0.8, 0.2), which over this sheet's own gold came out olive -- the sheet is the colour,
// and what is passed here is what is left of it.
inline constexpr Recipe kGuarding{5,    20.0f, 20.0f, 100.0f, 0.0f, 0.0f, 12.0f, 150.0f,
                                  true, true,  true,  {0.18f, 1.0f, 0.35f}};

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

    // The knight's guard: thrown like a burst, but it lives for `seconds` -- the boon's own
    // duration, so the picture and the sim lapse together -- and follows the body until then.
    // Thrown again while one stands replaces it, which is what the realm does with the boon.
    void guard(const float feet[3], float yaw, float metresPerTile, float seconds);
    // Where the body is now, every frame it is drawn: the one thing a following burst needs.
    void follow(const float feet[3]);
    // And the guard put down early -- a boon that lapsed, or a character who died.
    void release();

    // Ages every burst on MU's clock. `seconds` is the frame's own.
    void update(float seconds);
    // `eye` is the camera's position in world metres, which the edge-on fade is taken from.
    void gather(gfx::Effects& effects, const content::Ground& ground, const float eye[3]) const;

    // MU's circle under the flares. Off in play, as MU2 had it; see the header note.
    void setCircle(bool on) { circle_ = on; }
    int live() const;

    static constexpr int kJoints = 15;  // the most any recipe asks for
    // MaxTails - 1 for BITMAP_FLARE: how many ticks long the level-up's trail grows.
    static constexpr int kTrailTicks = 19;
    // Points sampled a tick: the ring turns 0.5 rad a tick, so four is a 7-degree chord.
    static constexpr int kSamples = 4;
    // Two at once is a level earned while the last is still in the air, and the third slot is
    // the guard's, which stands for seconds at a time and must not take a level-up's place.
    static constexpr int kBursts = 3;

private:
    struct Joint {
        float phase = 0.0f;   // rand() % 500 - 250
        float rise = 0.0f;    // metres a reference tick
        float height = 0.0f;  // metres it stands at, for a band that does not climb
    };
    struct Burst {
        bool living = false;
        Recipe r;
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
    int guarding_ = -1;  // which burst is the standing guard, or -1
    Burst bursts_[kBursts];
    Burst* throwOne(const Recipe& recipe, const float feet[3], float yaw, float metresPerTile);

    // Drawn only by the drawing and never seeded: where a flare starts on its ring is not a
    // fact, and must not reach the sim's log. The same xorshift the showing uses.
    uint32_t seed_ = 0x2545F491u;
    float unit();
};

}  // namespace mu::game
