#pragma once

// Devil Square's run: its clock, its waves and each player's points (docs/devil-square-port.md,
// "The goal and how it is shown"). Square 1 only for now; squares 2-4 are rooms of the same map 9
// and differ in their breeds and their pay. The monsters themselves are not built yet (the doc's
// step 3): until they are, the run keeps its clock and its waves, and scores nothing.

#include <cstdint>

namespace mu::sim {

// MU's map number for squares 1-4 (WD_9DEVILSQUARE).
constexpr uint32_t kDevilSquareMap = 9;

// **The clock.** WebZen's minutes are in its lost DevilSquare.dat; OpenMU's are 20 of play and a
// minute after (Version095d/Events/DevilSquareInitializer.cs:135-137), and both tables we have
// share that frame. Ours, for now: a minute's wait from arrival, as Blood Castle's court, until
// Charon's door and the shared start are built (the doc's decisions 4-6).
constexpr int kSquareWait = 60;
constexpr int kSquarePlay = 20 * 60;
// After the end, the result shows for this long before everyone goes to Noria (WebZen moves them
// to gate 27 a minute into CLOSE, DevilSquare.cpp:960-979).
constexpr int kSquareRest = 60;
constexpr int64_t kSquareTicksPerSecond = 20;  // the realm's tick

// **The waves**, OpenMU's frame (DevilSquareInitializer.cs:175-207), in minutes of play: each
// overlaps the next, and five bosses rise once at minute 15.
constexpr int kSquareWaves = 3;
constexpr int kSquareWaveMinutes[kSquareWaves][2] = {{0, 7}, {5, 14}, {12, 20}};
constexpr int kSquareBossMinute = 15;
constexpr int kSquareBosses = 5;

enum class SquarePhase : uint8_t { None, Waiting, Running, Ended };

struct SquareRun {
    SquarePhase phase = SquarePhase::None;
    int square = 1;
    int64_t startsAt = 0;  // the tick the wait ends and the fight starts
    int64_t endsAt = 0;    // the tick the time is up
    int bossesKilled = 0;
    // The run over: the tick everyone is sent to Noria (kSquareRest after the end), or -1; and
    // `sentOut` once it has passed, for the mode to take him home (as CastleRun's).
    int64_t leavesAt = -1;
    bool sentOut = false;
};

// The wave being fought at `seconds` into the fight, 1 to kSquareWaves: the latest that has
// begun. 0 before the fight.
constexpr int squareWaveAt(int seconds) {
    int wave = 0;
    for (int w = 0; w < kSquareWaves; ++w) {
        if (seconds >= kSquareWaveMinutes[w][0] * 60) wave = w + 1;
    }
    return wave;
}

}  // namespace mu::sim
