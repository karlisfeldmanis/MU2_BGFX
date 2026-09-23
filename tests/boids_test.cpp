// The birds, with no window: `build/boids_test`.
//
// Foundation 9 -- what can be checked without a window is checked without one. A flock is the
// hardest thing in this engine to review from a picture: it arrives from off the frame on
// purpose, crosses in about two seconds, and then the sky is deliberately empty for fifteen to
// forty-five. A review run of a few hundred frames sees one pass if it is lucky, and seventeen
// frames of it showed no bird at all (2026-09-23). Meanwhile every rule that matters is
// arithmetic, and two of them were silently wrong in MU2 for a while:
//
//   * the per-frame rates were scaled by sixty rather than MU's twenty-five, so everything ran
//     at two and a half times the client's speed;
//   * a bird was deleted where it stood, so one you were watching blinked out roughly every
//     nine seconds of its life.
//
// Neither shows in a still frame. Both are one assertion here.
//
// `game/world/flight.h` takes the world through two function pointers, so this runs thousands
// of seconds of sky against a flat field and a rectangle, in a millisecond, with no assets, no
// bgfx and no sound device.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "game/world/flight.h"

using mu::game::BirdCall;
using mu::game::Flight;
using mu::game::Sky;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("  FAIL %s\n", what);
    ++failures;
}

// A flat field at a known height, and a frame that is a circle of a given radius around the
// hero on the ground. A rectangle in clip space and a circle in metres are not the same shape,
// but nothing here depends on which: what the rules ask is "is this point being looked at",
// and a circle answers that with no camera to build.
struct Field {
    float height = 1.0f;
    float watchX = 0.0f, watchZ = 0.0f;
    float watchRadius = 8.0f;
    bool blind = false;  // nobody looking, which is not the same as "no"
    // Every point the rules ever asked about, so a test can say what was true of all of them.
    int asked = 0;
};

float groundAt(void* context, float, float) { return static_cast<Field*>(context)->height; }

bool inFrame(void* context, const float at[3]) {
    Field& field = *static_cast<Field*>(context);
    ++field.asked;
    if (field.blind) return false;
    const float dx = at[0] - field.watchX;
    const float dz = at[2] - field.watchZ;
    return dx * dx + dz * dz <= field.watchRadius * field.watchRadius;
}

Sky skyOf(Field& field) {
    Sky sky;
    sky.ground = &groundAt;
    sky.inFrame = &inFrame;
    sky.context = &field;
    return sky;
}

// MU's own clock, which is what a "per reference frame" number means.
constexpr float kReference = 25.0f;

// ---------------------------------------------------------------------------------------

// A flock arrives all at once or not at all: five birds born side by side on one bearing is the
// only reason the flocking rule holds them together, since a bird with no neighbour within four
// tiles flies dead straight. Deviation 1.
void arrivesTogether() {
    Field field;
    Sky sky = skyOf(field);
    const float hero[3] = {0.0f, 1.0f, 0.0f};
    Flight flight;
    flight.reset(12345);
    flight.hurry();

    bool sawPartial = false;
    bool sawFull = false;
    float bearings[Flight::kMaxBirds];
    for (int step = 0; step < 4000; ++step) {
        const uint32_t before = flight.flying();
        flight.update(1.0f / 120.0f, hero, false, false, sky);
        const uint32_t now = flight.flying();
        if (before == 0 && now > 0) {
            // The frame a flock lands: every slot at once, and one bearing between them.
            check(now == Flight::kMaxBirds, "a flock arrives with every slot filled");
            // One bearing between them -- but this is read AFTER the step that landed them,
            // and that step has already flocked each one, so they have each turned a little by
            // the time anyone can look. What is observable is the SPREAD, and its bound is two
            // steps and not one: the turn is clamped to 13 degrees a reference frame in either
            // direction, so the widest any two can be apart after one step is one of them
            // turning fully left while another turns fully right. Measured spread was 0.0945
            // rad against a step of 0.0473, which is exactly that and not a fault.
            for (int i = 0; i < Flight::kMaxBirds; ++i) bearings[i] = flight.bird(i).facing;
            const float oneStep = 13.0f * 3.14159265f / 180.0f / 120.0f * kReference;
            for (int i = 1; i < Flight::kMaxBirds; ++i) {
                const float spread = std::fabs(bearings[i] - bearings[0]);
                check(spread <= 2.0f * oneStep * 1.01f,
                      "every bird in a new flock sets off on the same bearing");
                if (spread > 2.0f * oneStep * 1.01f) {
                    std::printf("    bird %d is %.4f rad off bird 0; a step turns %.4f\n", i,
                                double(spread), double(oneStep));
                }
            }
            sawFull = true;
        }
        if (now > 0 && now < Flight::kMaxBirds) sawPartial = true;
        check(now <= Flight::kMaxBirds, "the pool never exceeds five");
    }
    check(sawFull, "a flock arrived at all");
    // Thinning out is fine and expected; REFILLING one slot at a time is not, and the arrival
    // check above is what proves it, since a trickle would land with `now` already non-zero.
    (void)sawPartial;
}

// Nothing is ever taken off in shot. This is deviation 3, and the fault it was written for: the
// client drops a boid where it stands, which at MU's camera was mostly off screen and mostly
// got away with.
void nothingVanishesInFrame() {
    Field field;
    field.watchRadius = 12.0f;  // a generous frame, so there is plenty of chance to be caught
    Sky sky = skyOf(field);
    const float hero[3] = {0.0f, 1.0f, 0.0f};
    Flight flight;
    flight.reset(777);
    flight.hurry();

    bool wasLive[Flight::kMaxBirds] = {};
    int retired = 0;
    for (int step = 0; step < 400000; ++step) {
        for (int i = 0; i < Flight::kMaxBirds; ++i) wasLive[i] = flight.bird(i).live;
        flight.update(1.0f / 120.0f, hero, false, false, sky);
        for (int i = 0; i < Flight::kMaxBirds; ++i) {
            if (!wasLive[i] || flight.bird(i).live) continue;
            ++retired;
            // It went this step, and a retired bird keeps the position it went at -- which is
            // where it ended the step, NOT where it began one. Asked of the start instead, this
            // failed 47 times on birds that were in frame when the step opened and out of it by
            // the time they were tested: the rule is about where it is when it goes.
            const float* at = flight.bird(i).position;
            const float dx = at[0] - hero[0];
            const float dy = at[1] - hero[1];
            const float dz = at[2] - hero[2];
            const bool farGone = std::sqrt(dx * dx + dy * dy + dz * dz) >= 29.0f;
            check(!inFrame(&field, at) || farGone, "no bird is retired while in frame");
        }
    }
    check(retired > 20, "birds retired at all (so the check above had something to check)");
}

// The sky empties between flocks: a flock is a pass, not a resident, and one arriving the
// instant the last left reads as a carousel. Deviation 2 -- and the exact thing that broke when
// `--birds-now` was wired into the frame loop and zeroed the wait every frame.
void skyEmptiesBetweenFlocks() {
    Field field;
    Sky sky = skyOf(field);
    const float hero[3] = {0.0f, 1.0f, 0.0f};
    Flight flight;
    flight.reset(99);
    flight.hurry();

    float emptyFor = 0.0f;
    float shortestGap = 1e9f;
    int gaps = 0;
    bool wasFlying = false;
    const float dt = 1.0f / 60.0f;
    for (int step = 0; step < 600000; ++step) {
        flight.update(dt, hero, false, false, sky);
        const bool flying = flight.flying() > 0;
        if (!flying) {
            emptyFor += dt;
        } else {
            if (!wasFlying && gaps > 0) shortestGap = std::fmin(shortestGap, emptyFor);
            if (!wasFlying) ++gaps;
            emptyFor = 0.0f;
        }
        wasFlying = flying;
    }
    check(gaps > 8, "several flocks came and went");
    // kRespawnMin is fifteen seconds. The gap can be longer -- the arrival also refuses a spot
    // the player is looking at and waits another half second -- but never shorter.
    check(shortestGap >= 15.0f, "the sky stays empty at least fifteen seconds between flocks");
    if (shortestGap < 15.0f) std::printf("    shortest gap was %.2f s\n", double(shortestGap));
}

// **The one MU2 got wrong twice.** Every per-frame number is against MU's twenty-five a second,
// so the same elapsed time must cover the same ground whatever the frame rate. Run the same seed
// at 25, 60, 120 and 240 steps a second and the flock must cross at the same speed.
void sameAtAnyFrameRate() {
    const float rates[] = {25.0f, 60.0f, 120.0f, 240.0f};
    float crossed[4] = {};
    for (int r = 0; r < 4; ++r) {
        Field field;
        field.blind = true;  // nobody looking, so no bird is ever retired for leaving the frame
        Sky sky = skyOf(field);
        const float hero[3] = {0.0f, 1.0f, 0.0f};
        Flight flight;
        flight.reset(2024);
        flight.hurry();
        const float dt = 1.0f / rates[r];
        // One step to land the flock, then add up the ground bird 0 covers over two seconds.
        //
        // **Distance flown, not displacement.** A bird turns as it flies and every turn is
        // driven by a roll, so two frame rates spend their rolls differently and no two paths
        // are the same shape. Straight-line displacement over two seconds is then mostly a
        // measure of how much it happened to turn -- measured that way this read 167% at 240
        // fps and said nothing about speed. The path LENGTH is speed times time with no roll
        // in it, which is exactly the rule under test.
        flight.update(dt, hero, false, false, sky);
        float from[3];
        std::memcpy(from, flight.bird(0).position, sizeof(from));
        const int steps = int(rates[r] * 2.0f);
        for (int s = 0; s < steps; ++s) {
            flight.update(dt, hero, false, false, sky);
            const float dx = flight.bird(0).position[0] - from[0];
            const float dz = flight.bird(0).position[2] - from[2];
            crossed[r] += std::sqrt(dx * dx + dz * dz);
            std::memcpy(from, flight.bird(0).position, sizeof(from));
        }
    }
    // A twentieth, which is loose enough for the flutter and far tighter than the fault worth
    // catching: rates scaled by sixty rather than MU's twenty-five is two and a half times out.
    for (int r = 1; r < 4; ++r) {
        const float ratio = crossed[r] / crossed[0];
        check(ratio > 0.95f && ratio < 1.05f,
              "a flock crosses the same ground at any frame rate");
        std::printf("    %3.0f fps: %.3f m in 2 s (%.0f%% of 25 fps)\n", double(rates[r]),
                    double(crossed[r]), double(ratio * 100.0f));
    }
    // And it is the client's speed and not some other one: 6.25 m/s for two seconds is 12.5 m
    // of path, and the only thing that moves it off that is the 1.1 of a bird climbing.
    check(crossed[0] > 12.0f && crossed[0] < 14.0f, "and at the client's own 6.25 m a second");
    std::printf("    25 fps: %.3f m flown in 2 s (the client's 6.25 m/s is 12.5 m)\n",
                double(crossed[0]));
}

// Nothing flies indoors, and a flock caught inside leaves rather than being deleted.
void noBirdsIndoors() {
    Field field;
    Sky sky = skyOf(field);
    const float hero[3] = {0.0f, 1.0f, 0.0f};
    Flight flight;
    flight.reset(555);
    flight.hurry();

    // Never arrives while he is inside.
    for (int step = 0; step < 200000; ++step) {
        flight.update(1.0f / 120.0f, hero, false, true, sky);
        check(flight.flying() == 0, "no flock arrives while the hero is indoors");
        if (failures) return;
    }

    // And one already up goes when he steps in -- within a few seconds, not instantly, because
    // it flies out rather than being switched off.
    Flight second;
    second.reset(556);
    second.hurry();
    int step = 0;
    while (second.flying() == 0 && step < 200000) {
        second.update(1.0f / 120.0f, hero, false, false, sky);
        ++step;
    }
    check(second.flying() > 0, "a flock was up before the hero went in");
    float inside = 0.0f;
    while (second.flying() > 0 && inside < 30.0f) {
        second.update(1.0f / 120.0f, hero, false, true, sky);
        inside += 1.0f / 120.0f;
    }
    check(second.flying() == 0, "the flock leaves once the hero is indoors");
    check(inside > 0.01f, "and flies out rather than being switched off in place");
    std::printf("    the sky cleared %.2f s after he stepped inside\n", double(inside));
}

// The calls: only within six tiles, and at GOBoid's own odds -- two independent one-in-512 rolls
// per reference frame, which is what stops the sky chirping from horizon to horizon. This is
// also the answer to "are the birds silent?", which a log line cannot give: it counts how often
// a call actually happens over a long run.
void callsAtTheClientsOdds() {
    Field field;
    field.blind = true;  // nobody looking, so the flock is not retired for leaving the frame
    Sky sky = skyOf(field);
    const float hero[3] = {0.0f, 1.0f, 0.0f};
    Flight flight;
    flight.reset(31337);
    flight.hurry();

    const float dt = 1.0f / 120.0f;
    double seconds = 0.0;
    double birdSecondsInRange = 0.0;
    int calls = 0;
    int sounded[2] = {0, 0};
    for (int step = 0; step < 4000000; ++step) {
        BirdCall heard[Flight::kMostCalls];
        int count = 0;
        flight.update(dt, hero, false, false, sky, heard, &count);
        seconds += dt;
        for (int i = 0; i < Flight::kMaxBirds; ++i) {
            const Flight::Bird& bird = flight.bird(i);
            if (!bird.live) continue;
            const float dx = bird.position[0] - hero[0];
            const float dy = bird.position[1] - hero[1];
            const float dz = bird.position[2] - hero[2];
            if (dx * dx + dy * dy + dz * dz < 6.0f * 6.0f) birdSecondsInRange += dt;
        }
        for (int i = 0; i < count; ++i) {
            ++calls;
            ++sounded[heard[i].which];
            const float dx = heard[i].at[0] - hero[0];
            const float dy = heard[i].at[1] - hero[1];
            const float dz = heard[i].at[2] - hero[2];
            check(std::sqrt(dx * dx + dy * dy + dz * dz) < 6.0f,
                  "a call is only heard within six tiles");
        }
    }
    std::printf("    %d call(s) over %.0f s; %.0f bird-seconds within six tiles\n", calls,
                seconds, birdSecondsInRange);
    check(calls > 0, "the birds call at all");
    check(birdSecondsInRange > 1.0, "a bird came within earshot at all");
    if (birdSecondsInRange > 1.0) {
        // Two rolls at one in 512 per reference frame is 2 * 25 / 512 calls a second from a bird
        // in range: about one call every ten seconds each. Wide bounds, because this is an
        // ORDER check -- the faults worth catching are "never" and "a chorus".
        const double rate = double(calls) / birdSecondsInRange;
        const double want = 2.0 * kReference / 512.0;
        std::printf("    %.4f calls a bird-second; the client's own is %.4f\n", rate, want);
        check(rate > want * 0.5 && rate < want * 2.0,
              "and at about the client's two-in-512 a reference frame");
    }
    // Both files sound, and at about the same rate, which is what "two independent rolls"
    // means in practice. NOT "both in one frame": that is (factor/512) squared and comes to
    // about one occurrence in thirty runs of this length, so asserting it would be a test that
    // fails at random.
    std::printf("    bird_1 sounded %d time(s), bird_2 %d\n", sounded[0], sounded[1]);
    check(sounded[0] > 0 && sounded[1] > 0, "both calls sound, on their own rolls");
    if (sounded[1] > 0) {
        const double balance = double(sounded[0]) / double(sounded[1]);
        check(balance > 0.7 && balance < 1.4, "and neither is favoured over the other");
    }
}

// A cruising bird stays in MU's band of absolute height. It may leave it to dive and to sit on
// the ground, which are their own states.
void staysInItsBand() {
    Field field;
    field.blind = true;
    Sky sky = skyOf(field);
    const float hero[3] = {0.0f, 1.0f, 0.0f};
    Flight flight;
    flight.reset(4242);
    flight.hurry();
    float highest = 0.0f;
    for (int step = 0; step < 400000; ++step) {
        flight.update(1.0f / 120.0f, hero, false, false, sky);
        for (int i = 0; i < Flight::kMaxBirds; ++i) {
            const Flight::Bird& bird = flight.bird(i);
            if (!bird.live || bird.state != Flight::State::Fly) continue;
            highest = std::fmax(highest, bird.position[1]);
            check(bird.position[1] > 0.0f, "a cruising bird stays above the ground");
        }
    }
    // The band's ceiling is six metres and the push back down is a rate, not a clamp, so it
    // overshoots a little before it turns round. Well under twice the ceiling is the check.
    check(highest < 9.0f, "a cruising bird does not climb out of MU's band");
    std::printf("    highest a cruising bird reached: %.2f m (the band's top is 6 m)\n",
                double(highest));
}

}  // namespace

int main() {
    std::printf("boids_test: the flight rules, with no window\n");
    arrivesTogether();
    nothingVanishesInFrame();
    skyEmptiesBetweenFlocks();
    sameAtAnyFrameRate();
    noBirdsIndoors();
    callsAtTheClientsOdds();
    staysInItsBand();
    std::printf("boids_test: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
