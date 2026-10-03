// Blood Castle's drawbridge: the door over the gap that falls when the run's first quota is met.
//
// MuMain's ActionObject (ZzzObject.cpp:86-165), set going by SetActionObject(world, 36, 20, 1)
// when the server says the monsters are cleared (NewBloodCastleSystem.cpp:68-70). Type 36,
// Object37, stands raised at its stored 45 degrees. On the first of 21 frames, counted 20 down
// to 0, it is set to 35 and eDownGate plays; every frame it pitches by a speed that starts at 1
// and grows by 1.5, and passing 90 it is knocked back by the frames still to run and the speed
// starts again at 2 -- a fall and a few bounces as it lands; ours lands flat and stays (update). On the last frame it goes (hidden)
// and types 9 and 10, Object10's deck and Object11's chains, show, with the terrain's planks on
// the gap: the lowered bridge. Ours: the door stays, lying at 90 over the planks (lower()). MU's smoke at exactly 80 degrees is a float equality the swing never hits (it reads
// 36, 38.5 ... 85, 86, 88, 81.5 ...), so it never shows in MU and is not drawn here.
//
// A castle opened with the bridge already down -- MU's late joiner, who gets the end state at
// once (WSclient.cpp:8724-8727) -- shows the deck without the fall. The realm decides; this only
// draws it (Realm::castleTick, kCastleBridgeTicks).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mu::game {

class Town;

class Drawbridge {
public:
    // Finds the door and the deck among the town's placements. Only Blood Castle has them.
    void open(const std::string& world, const Town& town, float metresPerTile);
    void shutdown();
    bool isOpen() const { return door_ >= 0; }

    // One frame. `falling`: the run's drawbridge has started down; `down`: its gap is ground.
    // Writes the door's pitch and what shows into the town.
    void update(float seconds, bool falling, bool down, Town& town);

    // Whether the last update started the fall, eDownGate's cue, and the door's place in metres.
    bool started() const { return started_; }
    // Whether it is down: the door gone and the deck shown, and the gap's planks with it.
    bool lowered() const { return state_ == State::Lowered; }
    // Whether the last update landed it, the dust's cue (CastleSparks::dust), and where its far
    // end touches, in metres: the bridge's end, six tiles from the hinge.
    bool landed() const { return landed_; }
    void tip(float out[3]) const;
    const float* at() const { return rest_; }

private:
    void lower(Town& town);

    enum class State : uint8_t { Raised, Falling, Lowered };
    State state_ = State::Raised;
    int door_ = -1;
    std::vector<uint32_t> deck_;
    float rest_[3] = {0.0f, 0.0f, 0.0f};
    float yaw_ = 0.0f, roll_ = 0.0f;
    // MU's own: g_iActionTime, Angle[0] in degrees and g_fActionObjectVelocity, stepped at 25 Hz;
    // `was` the angle a frame before, drawn between the two by `frame`.
    int time_ = 20;
    float degrees_ = 45.0f, was_ = 45.0f, speed_ = 1.0f;
    float frame_ = 0.0f;
    bool started_ = false;
    bool landed_ = false;
    float metresPerTile_ = 1.0f;
};

}  // namespace mu::game
