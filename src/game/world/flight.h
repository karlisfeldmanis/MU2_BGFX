// How the birds fly: the whole of GOBoid.cpp's rule, with nothing in it that needs a window.
//
// This is the half of `game/world/boids.h` that can be checked without drawing anything, and
// it is split out for the reason foundation 9 gives: **what can be checked without a window is
// checked without one.** Everything here is arithmetic on five points -- the flocking, the
// dive, the landing, the retirement and the odds of a call -- and every one of those rules has
// a way of being wrong that a screenshot cannot show. Two of them were wrong in MU2 for a
// while and neither was visible in a still frame:
//
//   * **the rates were scaled by sixty and not by MU's twenty-five**, so every per-frame number
//     ran at two and a half times the client's, and a wrong sum then slowed the cruise to
//     compensate;
//   * **a bird was deleted where it stood**, so one you were watching blinked out about every
//     nine seconds of its life.
//
// So the rules take the world through two function pointers -- how high the ground is, and
// whether a point is in the frame -- and give their calls back as data. `Boids` supplies the
// real ground and the real camera; tests/boids_test.cpp supplies a flat field and a rectangle,
// and can then run a thousand seconds of sky in a millisecond and assert things no picture
// answers: that a flock arrives all at once, that nothing is ever retired in shot, that the
// sky stays empty between flocks, and that flying at 25 frames a second and at 250 covers the
// same ground in the same time.
//
// Every number is MU's own, and every length is MU's divided by a hundred: MU works in units
// where a tile is a hundred and this works in metres where a tile is one. The per-frame ones
// are against MU's twenty-five a second -- `REFERENCE_FPS` in ZzzAI.h -- and never against the
// rate actually drawn. Why each one is what it is, and which four of them are MU2's judgement
// rather than the client's, is in boids.h; this file is the arithmetic.
#pragma once

#include <cstdint>

namespace mu::game {

// What the rules need of the world. Function pointers and a context rather than an interface,
// which is the shape `Sound::Where` already uses here for the same reason: there is no state to
// own and a vtable would be a type to keep in step.
struct Sky {
    // The ground under a point, in world metres.
    float (*ground)(void* context, float x, float z) = nullptr;
    // Whether a point is on screen, with the margin already allowed for. **False when nobody is
    // looking**, which is not the same as "no". A pool with no camera retires its birds exactly
    // as the client does, and a test that answers true to everything would never see one go.
    bool (*inFrame)(void* context, const float at[3]) = nullptr;
    // Whether a point is over a safe tile, which in Atlans is the dry basin: no fish swims
    // there. Null where nothing asks it.
    bool (*dry)(void* context, float x, float z) = nullptr;
    void* context = nullptr;
};

// One call a bird made this frame, for the caller to play where it says. The rules decide THAT
// a bird calls and where it is; what that sounds like is not theirs.
struct BirdCall {
    float at[3];
    // 0 for SOUND_BIRD01, 1 for SOUND_BIRD02. Two, because GOBoid rolls them independently and
    // both can sound in the same frame -- see boids.h.
    int which;
};

class Flight {
public:
    // Birds in the air at once. The client breaks its loop at five.
    static constexpr int kMaxBirds = 5;
    // And the pool's slots: MAX_BOIDS, forty, every one of which Atlans's fish fill
    // (GOBoid.cpp:1233-1235, no `i >= 5` cut there). Birds use the first five.
    static constexpr int kMaxSlots = 40;
    // Two calls a bird, so a frame can never produce more than this.
    static constexpr int kMostCalls = kMaxBirds * 2;

    enum class State : uint8_t { Fly, Down, Ground, Up };

    struct Bird {
        float position[3] = {0.0f, 0.0f, 0.0f};
        // Where it will be three of MU's frames from now, which is the point the others steer
        // by: the client's `Direction = Position + 3 * p`, said in time rather than in frames so
        // the flock reads the same at any frame rate.
        float heading[2] = {0.0f, 0.0f};
        float facing = 0.0f;
        float speed = 0.0f;
        float climb = 0.0f;
        State state = State::Fly;
        bool live = false;
        // Told to go, and flying out rather than gone. Everything that used to end a bird now
        // only sets this; the frame takes it off. See boids.h, deviation 3.
        bool leaving = false;
        // Whether it has ever been in the frame. "Taken off the first frame it is OUT of the
        // frame" presupposes it was once in it -- and a flock arrives off screen BY DESIGN, so
        // a new bird is off screen from birth. Without this, one whose flat one-in-512 came up
        // on its first step was retired before it had flown a metre, and the flock it arrived
        // with was four. Caught by tests/boids_test.cpp's "a flock arrives with every slot
        // filled". The far backstop still applies, so a flock the camera never looks at is not
        // immortal.
        bool wasSeen = false;
        // MoveBat's `o->Timer`: the bob's phase, drawn at birth as the client's `rand() % 314 *
        // 0.01` and stepped 0.2 a reference frame.
        float timer = 0.0f;
        // The bat's jink: a turn rate in radians a second, redrawn on a roll. See Flight::move.
        float swerve = 0.0f;
        // A fish's height over the floor, drawn at birth (CreateAtlanseFish's 150 to 350 units),
        // and its Velocity, 0.3 or 0.25. See Flight::school.
        float lift = 0.0f;
        float velocity = 0.0f;
        // Seconds left of a fish's turn at the dry basin's edge, its steering held so it swims
        // the turn out rather than back into the edge. See Flight::swim.
        float turned = 0.0f;
        // Ours, a fish's own (Flight::swim): its drawn scale, its cruise in metres a second,
        // seconds left of a dart, and the depth over the floor it is drifting to.
        float size = 0.8f;
        float cruise = 0.0f;
        float dart = 0.0f;
        float depth = 0.0f;
        // The bearing from him of its school's point, shared with the fish it was born beside.
        float anchor = 0.0f;
    };

    // `seed` makes a run repeatable, which is what lets a test say "this seed, this second, this
    // many birds". The game leaves it at the default.
    void reset(uint32_t seed = 0x9E3779B9u);

    // `speed` is `o->Velocity` for this pool -- a bird's 1.0, a butterfly's 0.3.
    void setPace(float speed) { pace_ = speed; }
    // Noria's butterfly rather than Lorencia's bird: MoveButterFly in place of MoveBird, and
    // the flock asked one frame in four (GOBoid.cpp:932 and :1135). See flutter().
    void setButterfly(bool on) { butterfly_ = on; }
    bool isButterfly() const { return butterfly_; }
    // The Dungeon's bat rather than a bird: MoveBat in place of MoveBird (GOBoid.cpp:925-930),
    // a height held over the floor rather than a dive, and one call at one frame in 256 rather
    // than the bird's two at 512 (:1490-1494). The flock is the bird's MoveBoidGroup.
    void setBat(bool on) { bat_ = on; }
    // A bird's calls other than the bird's two rolls at one frame in 512: Blood Castle's crow
    // rolls once at one in 128 (GOBoid.cpp:1495-1501). 0 keeps the bird's (or the bat's).
    void setCalls(float every, int rolls) { callEvery_ = every; callRolls_ = rolls; }
    bool isBat() const { return bat_; }
    // Atlans's fish rather than birds: CreateAtlanseFish and MoveBoid's Atlans arm (GOBoid.cpp:
    // 886-918, 1141-1160). Forty, each slot born again as it empties while the hero is in the
    // map's north half, swimming at a height held over the floor, cruising and darting.
    void setFish(bool on) { fish_ = on; }
    bool isFish() const { return fish_; }
    // Icarus's dragons rather than birds: CreateDragon and MoveBoidGroup (GOBoid.cpp:824-851,
    // 1140-1196). Three, each born again as it is gone, gliding straight 6 m under him; born out
    // of the frame and aimed back across him, and gone only out of it, as a bird (ours).
    // A dragon's `size` is its Scale and its `cruise` MU's Velocity, its pace and its clip rate.
    void setDragon(bool on) { dragon_ = on; }
    bool isDragon() const { return dragon_; }

    // The first flock arrives on the next step rather than 20 to 90 seconds in. One-shot: a
    // caller with this in its frame loop would otherwise refill the sky the instant it empties,
    // which is the one thing the wait exists to prevent.
    void hurry();

    // One step. `calls` is filled with what sounded, up to kMostCalls, and `callCount` says how
    // many; pass null for a pool that does not call. Nothing here draws, allocates or reads a
    // clock: the same seed and the same steps give the same sky every time.
    void update(float seconds, const float hero[3], bool walking, bool indoors, const Sky& sky,
                BirdCall* calls = nullptr, int* callCount = nullptr);

    const Bird& bird(int i) const { return birds_[i]; }
    uint32_t flying() const { return flying_; }
    // Seconds until the next flock. A test reads it to check the sky is meant to be empty.
    float waiting() const { return wait_; }

private:
    void arrive(const float hero[3], const Sky& sky);
    void school(float seconds, float factor, const float hero[3], const Sky& sky);
    void glide(float factor, const float hero[3], const Sky& sky);
    bool seen(const float at[3], const Sky& sky) const;
    void swim(Bird& fish, int index, const float hero[3], float seconds, float factor,
              const Sky& sky);
    void move(Bird& bird, const float hero[3], bool walking, float seconds, float factor,
              const Sky& sky);
    void flock(Bird& bird, float factor);
    void flutter(Bird& bird, float land, float factor);
    void away(Bird& bird, const float hero[3], float factor);
    void home(Bird& bird, const float hero[3], float factor);
    void step(Bird& bird, float speed, float seconds);
    float wander(float seconds);
    bool chance(float perFrame, float factor);
    float random01();

    Bird birds_[kMaxSlots];
    float pace_ = 1.0f;
    bool butterfly_ = false;
    bool bat_ = false;
    bool fish_ = false;
    bool dragon_ = false;
    float callEvery_ = 0.0f;
    int callRolls_ = 0;
    // Seconds the bats' flock has left with him before it flies off. See Flight::update.
    float stay_ = 0.0f;
    float wait_ = 0.0f;
    float cycle_ = 0.0f;
    uint32_t flying_ = 0;
    uint32_t seed_ = 0x9E3779B9u;
    bool hurried_ = false;
    bool seeded_ = false;
};

}  // namespace mu::game
