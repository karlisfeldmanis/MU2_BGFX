#pragma once

// Devil Square's run: its clock, its waves, each player's points, and the ranks and pay at its end
// (docs/devil-square-port.md, "The goal and how it is shown"). Square 1 only for now; squares 2-4
// are rooms of the same map 9 and differ in their breeds and their pay.

#include <cstdint>

namespace mu::sim {

// MU's map number for squares 1-4 (WD_9DEVILSQUARE).
constexpr uint32_t kDevilSquareMap = 9;

// **Charon's door** (docs/devil-square-port.md A 2.1-2.2; WebZen NpcRusipher, NpcTalk.cpp:
// 1005-1048, and GCReqmoveDevilSquare, protocol.cpp:18587-18923): spoken to with a Devil's
// Invitation while the gate is open, he takes it and sends him to the square's gate -- 58 for
// square 1, the arena's box (OpenMU 0.95d Gates.cs:141). MU has no enter gate for it;
// kSquareEnterGate is ours, a row with no box, as Blood Castle's kCastleEnterGate is.
constexpr int32_t kSquareGate = 58;
constexpr int32_t kSquareEnterGate = 1058;
// The squares at 0.97d and how many are built (square 1 alone: its nests are in mu.db), each with
// WebZen 0.97d's band (protocol.cpp:18789-18832; the fourth "250+", 0 for no ceiling). Both ends
// are asked, as WebZen asks them: too high is "You're underestimating yourself", too low "If you
// wish to stay alive" (texts 686, 687).
constexpr int kSquares = 4;
constexpr int kSquaresBuilt = 1;
constexpr int kSquareBands[kSquares][2] = {{10, 99}, {100, 179}, {180, 249}, {250, 0}};
// The level a band is read against: his own, but the Magic Gladiator's ((L + 1) / 2) * 3
// (protocol.cpp:18714-18723), since his levels count for more.
constexpr int squareLevel(int level, bool gladiator) { return gladiator ? (level + 1) / 2 * 3 : level; }
// The square his level's band holds, 1 to kSquares: the page Charon opens on.
constexpr int squareFor(int level) {
    for (int s = 0; s < kSquares; ++s) {
        if (level <= kSquareBands[s][1] || kSquareBands[s][1] == 0) return s + 1;
    }
    return kSquares;
}
// Why Charon would not let him in.
enum class SquareRefusal : int32_t {
    None = 0,
    NoInvitation = 1,  // ServerCmd 1,2, text 652: go to the Chaos Goblin with the eye and the key
    NotYet = 2,        // ServerCmd 1,3, text 653: "You have come too soon"
    TooLow = 3,        // result 4, text 687
    TooHigh = 4,       // result 3, text 686
    NotBuilt = 5,      // ours: a square past kSquaresBuilt
};

// **The Devil's Eye and Key** (14/17, 14/18) fall from monsters anywhere but in the square, each
// its own roll off its own dice, one that lands the kill's whole drop -- as Blood Castle's scroll
// and bone (sim/items.h). WebZen keeps them in every monster's drop list at one in two
// (EyesOfDevilSquareDropRate, Gamemain.cpp:3635-3636); OpenMU makes them a flat 1% each
// (EventTicketItems.cs:34-73). Ours: OpenMU's 1%, as Blood Castle's ingredients were raised
// (docs/devil-square-port.md decision 13).
constexpr int kDevilsEyeIn10000 = 100;
constexpr int kDevilsKeyIn10000 = 100;
// Their level by the monster's, WebZen 0.97d's table (user.cpp:8346-8366): none at 15 or under,
// +1 to 40, +2 to 54, +3 to 66, +4 from 67. 0 for none: a level-0 one never falls (:8373).
constexpr int devilTicketLevel(int monsterLevel) {
    return monsterLevel <= 15 ? 0 : monsterLevel <= 40 ? 1 : monsterLevel <= 54 ? 2 : monsterLevel <= 66 ? 3 : 4;
}

// **The clock.** WebZen's minutes are in its lost DevilSquare.dat; OpenMU's are 20 of play and a
// minute after (Version095d/Events/DevilSquareInitializer.cs:135-137), and both tables we have
// share that frame. One start for everyone, MU's: through Charon's door the wait is what is left
// of the gate's five minutes (RealmConfig::square; Realm::setSquare), so all who came in fight
// from the odd hour together. A realm raised there without the door (a test, or a save) waits
// kSquareWait.
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

// **Each square's breeds**, OpenMU's 0.95d (Maps/DevilSquare1-4.cs): two a wave, 35 of each over
// the square's box (mu.db monster_spawns, map 9), and its five bosses. A wave's breeds rise as it
// begins and rise again while it lasts (SpawnTrigger.AutomaticDuringWave); the bosses rise once
// (OnceAtWaveStart). WebZen's own table is in its lost DevilSquare.dat.
constexpr int32_t kSquareBreeds[4][kSquareWaves][2] = {
    {{17, 15}, {5, 13}, {8, 36}},    // Cyclops, Skeleton Archer; Hell Hound, Hell Spider; Poison Bull, Shadow
    {{10, 39}, {34, 41}, {40, 35}},  // Dark Knight, Poison Shadow; Cursed Wizard, Death Cow; Death Knight, Death Gorgon
    {{41, 37}, {35, 34}, {64, 65}},  // Death Cow, Devil; Death Gorgon, Cursed Wizard; Orc Archer, Elite Orc
    {{64, 65}, {60, 69}, {57, 70}},  // Orc Archer, Elite Orc; Bloody Wolf, Alquamos; Iron Wheel, Queen Rainer
};
constexpr int32_t kSquareBoss[4] = {18, 49, 67, 66};  // Gorgon, Hydra, Metal Balrog, Cursed King

// Which of `square`'s waves a breed comes in, 1 to kSquareWaves, kSquareWaves + 1 for its bosses,
// or 0 for none of its.
constexpr int squareWaveOf(int square, int32_t number) {
    const int s = square < 1 ? 0 : square > 4 ? 3 : square - 1;
    if (kSquareBoss[s] == number) return kSquareWaves + 1;
    for (int w = 0; w < kSquareWaves; ++w) {
        if (kSquareBreeds[s][w][0] == number || kSquareBreeds[s][w][1] == number) return w + 1;
    }
    return 0;
}

// **The pay**, a row by rank (docs/devil-square-port.md 5.2): OpenMU's 0.95d table
// (Version095d/Events/DevilSquareInitializer.cs:47-83), very likely WebZen's own lost dat (the
// repack's is exactly 100x it), each row {experience, Zen}. The experience goes at the game's rate,
// as every kill's (rules.h kExperienceRate), as Blood Castle's win does.
constexpr int kSquareRows = 4;
constexpr int64_t kSquarePay[4][kSquareRows][2] = {
    {{6000, 30000}, {4000, 25000}, {2000, 20000}, {1000, 15000}},
    {{8000, 40000}, {6000, 35000}, {4000, 30000}, {2000, 25000}},
    {{10000, 50000}, {8000, 45000}, {6000, 40000}, {4000, 35000}},
    {{20000, 60000}, {10000, 55000}, {8000, 50000}, {6000, 45000}},
};

// Which row `rank` of `count` is paid, WebZen 0.97d's rule (DevilSquareGround.cpp:374-411): six
// or fewer, the first row 0, the second row 1 and the rest row 3 (row 2 unused); seven or more,
// the first row 0, then the top 30% row 1, the top 50% row 2, everyone else row 3. Everyone ranked
// is paid: there is no rank without a row.
constexpr int squarePayRow(int rank, int count) {
    if (rank <= 1) return 0;
    if (count <= 6) return rank == 2 ? 1 : 3;
    const int share = rank * 100 / count;
    return share <= 30 ? 1 : share <= 50 ? 2 : 3;
}

enum class SquarePhase : uint8_t { None, Waiting, Running, Ended };

struct SquareRun {
    SquarePhase phase = SquarePhase::None;
    int square = 1;
    int64_t startsAt = 0;  // the tick the wait ends and the fight starts
    int64_t endsAt = 0;    // the tick the time is up
    int bossesKilled = 0;
    // The waves called up so far, a bit each (bit kSquareWaves the bosses): each once, as its
    // minute comes (Realm::squareTick).
    uint8_t called = 0;
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
