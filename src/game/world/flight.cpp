#include "game/world/flight.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mu::game {
namespace {

// Twenty-five a second, which is the rate MU's per-frame numbers are written for:
// `REFERENCE_FPS` in ZzzAI.h, and `FPS_ANIMATION_FACTOR` is that over the frame rate drawn.
constexpr float kReference = 25.0f;

// How fast a bird crosses the town at `Velocity = 1`, in metres a second. `MoveBoidGroup` steps
// a boid `o->Velocity * 25.f` units along its facing per reference frame, and there are
// twenty-five of those a second: 625 units, a quarter of a tile a frame, 6.25 m/s.
constexpr float kCruise = 6.25f;

// The ring the flock arrives on, and the client's own box within it: `rand() % 1024 - 512` on
// both horizontals is 5.12 m either way.
constexpr float kSpawnRingMin = 8.0f;
constexpr float kSpawnRingMax = 12.0f;
constexpr float kFlockBox = 5.12f;
// How far off a bearing straight at the player the flock may set out, in degrees.
constexpr float kAimSpread = 55.0f;

// How long the sky stays empty between flocks, and how long before the first one.
constexpr float kRespawnMin = 15.0f;
constexpr float kRespawnMax = 45.0f;
constexpr float kFirstMin = 20.0f;
constexpr float kFirstMax = 90.0f;

// How far from the player a bird gets before it is asked to go, and the backstop under the
// on-screen test: twice that is far past anything the town draws.
constexpr float kFlyDistance = 15.0f;
constexpr float kFarGone = 30.0f;

// `Direction[2] = ±10` a reference frame, `-20` in a dive, `+20` off the ground.
constexpr float kLevelClimb = 2.5f;
constexpr float kDiveClimb = -5.0f;
constexpr float kRiseClimb = 5.0f;
// `rand() % 16 - 8` units a frame: two metres a second at the draw's edge, keeping the client's
// slight downward bias -- the draw is one notch shorter above than below.
constexpr float kFlutter = 2.0f;

// The client's "three steps ahead", said in time. Three of twenty-five.
constexpr float kLookAhead = 3.0f / kReference;

// The most a bird turns in a reference frame, and how far it looks for company.
constexpr float kTurnRate = 13.0f;
constexpr float kFlockRange = 4.0f;
constexpr float kPersonalSpace = 0.8f;

// MU's dive window: `GetTickCount() % 8192 < 2048`, the first quarter of an 8.192 s cycle. Kept
// as a cycle of this pool's own rather than off the wall clock, so a run of a fixed number of
// steps sees the same quarter every time.
constexpr float kCycle = 8.192f;
constexpr float kDiveWindow = 2.048f;

// How near the hero a bird has to be before it is heard: GOBoid's own `if (Range < 600)`, six
// tiles. Beyond it the client does not roll at all, which matters more than it sounds -- the
// birds circle a long way out and rolling for every one would have the sky chirping from
// horizon to horizon.
constexpr float kHeard = 6.0f;
// `rand_fps_check(512)`, twice, rolled independently.
constexpr float kCallEvery = 512.0f;

constexpr float kPi = 3.14159265358979f;
constexpr float kTau = 6.28318530717959f;

float wrapPi(float angle) {
    while (angle > kPi) angle -= kTau;
    while (angle < -kPi) angle += kTau;
    return angle;
}

}  // namespace

void Flight::reset(uint32_t seed) {
    for (Bird& bird : birds_) bird = Bird();
    seed_ = seed == 0 ? 1u : seed;
    wait_ = 0.0f;
    cycle_ = 0.0f;
    flying_ = 0;
    hurried_ = false;
    seeded_ = false;
}

float Flight::random01() {
    // xorshift32: a pool of five birds rolling a handful of numbers a step has no business
    // reaching for a distribution object, and nothing here feeds the seeded sim log.
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return float(seed_ & 0xFFFFFFu) / float(0x1000000u);
}

void Flight::hurry() {
    if (hurried_) return;
    hurried_ = true;
    wait_ = 0.0f;
}

void Flight::update(float seconds, const float hero[3], bool walking, bool indoors,
                    const Sky& sky, BirdCall* calls, int* callCount) {
    if (callCount) *callCount = 0;
    // The first wait is rolled here rather than in reset, so that a pool which has never been
    // stepped has not yet spent a number: a test that resets, asserts and then runs gets the
    // same sky as one that only runs.
    if (!seeded_) {
        seeded_ = true;
        if (!hurried_) wait_ = kFirstMin + random01() * (kFirstMax - kFirstMin);
    }
    // MU's own scaling: a per-frame number means what it meant at twenty-five frames a second,
    // whatever this one is running at. `FPS_ANIMATION_FACTOR`.
    const float factor = seconds * kReference;
    cycle_ = std::fmod(cycle_ + seconds, kCycle);

    // The next flock, once the last one has gone. Empty slots do not refill one by one: a bird
    // that sets off alone has no neighbours for the flocking rule to hold it to, and a flock
    // that trickles in is not one.
    if (flying_ == 0) {
        wait_ -= seconds;
        if (wait_ <= 0.0f && !indoors) arrive(hero, sky);
    }

    uint32_t flying = 0;
    for (Bird& bird : birds_) {
        if (!bird.live) continue;
        // Stepping inside sends them away rather than deleting them: the slot goes back on the
        // clock and the sky refills when you come out.
        if (indoors) bird.leaving = true;

        move(bird, hero, walking, seconds, factor, sky);
        if (!bird.live) continue;
        ++flying;

        // Two INDEPENDENT rolls rather than one that picks between the files, because that is
        // what GOBoid.cpp does and the difference is audible: folding them into one would halve
        // how often anything is heard and make the two calls mutually exclusive, which they are
        // not. Rolled even when nobody is listening, so the sky is the same sky either way.
        const float dx = bird.position[0] - hero[0];
        const float dy = bird.position[1] - hero[1];
        const float dz = bird.position[2] - hero[2];
        const bool near = dx * dx + dy * dy + dz * dz < kHeard * kHeard;
        const float odds = factor / kCallEvery;
        for (int which = 0; which < 2; ++which) {
            const bool sounded = random01() < odds;
            if (!near || !sounded || calls == nullptr || callCount == nullptr) continue;
            if (*callCount >= kMostCalls) continue;
            BirdCall& call = calls[*callCount];
            std::memcpy(call.at, bird.position, sizeof(call.at));
            call.which = which;
            ++*callCount;
        }
    }
    flying_ = flying;
}

void Flight::arrive(const float hero[3], const Sky& sky) {
    // The box's centre is on a ring rather than on the hero, so the flock arrives from off the
    // edge of the frame instead of appearing over the square. The ring is a distance and the
    // frame is a shape on screen, and the two do not agree -- on a wide window most of the ring
    // is in view -- so the bearing is redrawn until the whole box lands off screen, and the pool
    // waits another half second rather than settling for a spot the player is looking at. A bird
    // appearing out of nothing in the middle of the frame is the same fault as one vanishing
    // there.
    float spots[kMaxBirds][3];
    bool found = false;
    for (int attempt = 0; attempt < 12 && !found; ++attempt) {
        const float bearing = random01() * kTau;
        const float range = kSpawnRingMin + random01() * (kSpawnRingMax - kSpawnRingMin);
        const float centreX = hero[0] + std::cos(bearing) * range;
        const float centreZ = hero[2] + std::sin(bearing) * range;
        found = true;
        for (int i = 0; i < kMaxBirds; ++i) {
            // The client's box, and its `rand() % 200 + 150` of height -- a metre and a half to
            // three and a half over the ground, so a new bird is already in the air rather than
            // climbing out of it.
            const float x = centreX + (random01() * 2.0f - 1.0f) * kFlockBox;
            const float z = centreZ + (random01() * 2.0f - 1.0f) * kFlockBox;
            spots[i][0] = x;
            spots[i][1] = sky.ground(sky.context, x, z) + 1.5f + random01() * 2.0f;
            spots[i][2] = z;
            if (sky.inFrame(sky.context, spots[i])) found = false;
        }
    }
    if (!found) {
        wait_ = 0.5f;
        return;
    }

    // One bearing for the whole flock -- the client zeroes every bird's angle -- pointed loosely
    // back across the player, so it flies over rather than away.
    const float inward = std::atan2(hero[0] - spots[0][0], hero[2] - spots[0][2]);
    const float facing = wrapPi(inward + (random01() * 2.0f - 1.0f) * kAimSpread * kPi / 180.0f);

    for (int i = 0; i < kMaxBirds; ++i) {
        Bird& bird = birds_[i];
        std::memcpy(bird.position, spots[i], sizeof(bird.position));
        bird.facing = facing;
        bird.speed = 1.0f;
        bird.climb = 0.0f;
        bird.state = State::Fly;
        bird.live = true;
        bird.leaving = false;
        bird.wasSeen = false;
        // Where it is heading is set before anyone steers by it, or the first step's flocking
        // reads five stale points from the last flock.
        bird.heading[0] = bird.position[0] + std::sin(facing) * kCruise * kLookAhead;
        bird.heading[1] = bird.position[2] + std::cos(facing) * kCruise * kLookAhead;
    }
    flying_ = kMaxBirds;
}

void Flight::move(Bird& bird, const float hero[3], bool walking, float seconds, float factor,
                  const Sky& sky) {
    const float land = sky.ground(sky.context, bird.position[0], bird.position[2]);
    if (!bird.wasSeen && sky.inFrame(sky.context, bird.position)) bird.wasSeen = true;

    switch (bird.state) {
        case State::Fly: {
            // Only during the first quarter of each cycle, and only from the middle distance --
            // a bird already overhead does not dive. One on its way out does not turn back for
            // it either.
            if (!bird.leaving && cycle_ < kDiveWindow) {
                const float dx = bird.position[0] - hero[0];
                const float dz = bird.position[2] - hero[2];
                const float flat = std::sqrt(dx * dx + dz * dz);
                if (flat >= 2.0f && flat <= 4.0f) bird.state = State::Down;
            }
            bird.speed = 1.0f;
            bird.position[1] += wander(seconds);
            // Absolute heights, not heights above the ground: Lorencia is flat enough that the
            // client never had to tell the difference.
            if (bird.position[1] < 2.0f) {
                bird.climb = kLevelClimb;
            } else if (bird.position[1] > 6.0f) {
                bird.climb = -kLevelClimb;
            }
            break;
        }
        case State::Down:
            bird.climb = kDiveClimb;
            if (bird.position[1] < land) {
                // Landed. The client as it stands bounces straight back to climbing here, which
                // leaves its own BOID_GROUND branch unreachable -- but that branch is written
                // for exactly this bird and this moment, down to being startled by the player
                // walking, and the game it came from had birds that settled. Restored, as MU2
                // restored it.
                bird.state = State::Ground;
                bird.position[1] = land;
                bird.speed = 0.0f;
                bird.climb = 0.0f;
            }
            break;
        case State::Ground:
            bird.position[1] = land;
            // Off again the moment the player moves nearby, and otherwise on a one-in-256 whim a
            // frame -- a few seconds of pecking about.
            if (walking || bird.leaving || chance(1.0f / 256.0f, factor)) {
                bird.state = State::Up;
                bird.speed = 1.1f;
                bird.climb = kRiseClimb;
            }
            break;
        case State::Up:
            bird.position[1] += wander(seconds);
            bird.speed -= 0.005f * factor;
            if (bird.speed <= 1.0f) bird.state = State::Fly;
            break;
    }

    // A perched bird neither flocks nor moves; it only waits.
    if (bird.state != State::Ground) {
        if (bird.leaving) {
            away(bird, hero, factor);
        } else {
            flock(bird, factor);
        }
        step(bird, bird.speed * kCruise * pace_, seconds);
    }

    const float dx = bird.position[0] - hero[0];
    const float dz = bird.position[2] - hero[2];
    const float flat = std::sqrt(dx * dx + dz * dz);
    if (flat >= kFlyDistance || chance(1.0f / 512.0f, factor)) bird.leaving = true;

    // And the only place a bird is actually taken off: out of the frame, or so far out of town
    // that it is under a pixel. Everything above only asks it to go.
    if (bird.leaving) {
        const float dy = bird.position[1] - hero[1];
        const float away3 = std::sqrt(dx * dx + dy * dy + dz * dz);
        const bool gone = bird.wasSeen && !sky.inFrame(sky.context, bird.position);
        if (gone || away3 >= kFarGone) {
            bird.live = false;
            bird.leaving = false;
            // The sky is briefly empty, which is the point: a flock is a pass rather than a
            // resident, and one arriving the instant the last left reads as a carousel. Clocked
            // from the last bird out, not the first: the slots refill together.
            wait_ = kRespawnMin + random01() * (kRespawnMax - kRespawnMin);
        }
    }
}

void Flight::flock(Bird& bird, float factor) {
    // Each neighbour within range contributes its own heading, and either the direction toward
    // this bird or away from it depending on whether it is inside the personal space. The sum is
    // normalised per neighbour and averaged, and the bird turns toward it at a fixed rate -- so a
    // flock loosely agrees on a direction without ever converging on a point.
    float target[2] = {0.0f, 0.0f};
    int neighbours = 0;
    for (const Bird& other : birds_) {
        if (!other.live || &other == &bird) continue;
        const float dx = bird.position[0] - other.position[0];
        const float dy = bird.position[1] - other.position[1];
        const float dz = bird.position[2] - other.position[2];
        const float apart = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (apart >= kFlockRange) continue;

        const bool close = apart < kPersonalSpace;
        const float heading[2] = {other.heading[0] - other.position[0],
                                  other.heading[1] - other.position[2]};
        const float between[2] = {other.heading[0] - bird.position[0],
                                  other.heading[1] - bird.position[2]};
        const float contribution[2] = {
            (close ? heading[0] - between[0] : heading[0] + between[0]) * factor,
            (close ? heading[1] - between[1] : heading[1] + between[1]) * factor};
        const float length =
            std::sqrt(contribution[0] * contribution[0] + contribution[1] * contribution[1]);
        if (length <= 0.01f) continue;
        target[0] += contribution[0] / length;
        target[1] += contribution[1] / length;
        ++neighbours;
    }
    if (neighbours == 0) return;

    const float toward[2] = {target[0] / float(neighbours), target[1] / float(neighbours)};
    if (toward[0] * toward[0] + toward[1] * toward[1] <= 0.0001f) return;
    const float desired = std::atan2(toward[0], toward[1]);
    const float difference = wrapPi(desired - bird.facing);
    const float turn = kTurnRate * kPi / 180.0f * factor;
    bird.facing = wrapPi(bird.facing + std::clamp(difference, -turn, turn));
}

void Flight::away(Bird& bird, const float hero[3], float factor) {
    // Without this a bird told to go could hold its bearing across the square for as long as the
    // camera kept it in view, and one told to go while flying AT the hero would cross the middle
    // of the screen first. Off the player's heading is the shortest way out of a frame he is
    // standing in the middle of.
    const float outward[2] = {bird.position[0] - hero[0], bird.position[2] - hero[2]};
    if (outward[0] * outward[0] + outward[1] * outward[1] <= 0.0001f) return;
    const float desired = std::atan2(outward[0], outward[1]);
    const float difference = wrapPi(desired - bird.facing);
    const float turn = kTurnRate * kPi / 180.0f * factor;
    bird.facing = wrapPi(bird.facing + std::clamp(difference, -turn, turn));
}

void Flight::step(Bird& bird, float speed, float seconds) {
    // Metres a second throughout, rather than the client's per-frame steps: the birds are the one
    // thing here fast enough that the difference between sixty frames and a hundred and forty
    // showed in how quickly they crossed the square.
    const float forward[2] = {std::sin(bird.facing), std::cos(bird.facing)};
    bird.position[0] += forward[0] * speed * seconds;
    bird.position[2] += forward[1] * speed * seconds;
    bird.position[1] += bird.climb * seconds;
    bird.heading[0] = bird.position[0] + forward[0] * speed * kLookAhead;
    bird.heading[1] = bird.position[2] + forward[1] * speed * kLookAhead;
}

float Flight::wander(float seconds) {
    // `rand(16) - 8` over eight, keeping the client's slight downward bias, at a rate said in
    // metres a second.
    const int roll = int(random01() * 16.0f) & 15;
    return float(roll - 8) / 8.0f * kFlutter * seconds;
}

bool Flight::chance(float perFrame, float factor) { return random01() < perFrame * factor; }

}  // namespace mu::game
