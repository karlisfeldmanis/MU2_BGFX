#pragma once

// Blood Castle's run: when the Messenger lets him in, how long it runs, and the castle's grid
// boxes that the run opens. docs/blood-castle-port.md,
// Part A section 4.3. Castle 1 only for now; castles 2-7 share the terrain (MapManager.cpp:
// 1104-1106) and differ only in their monsters.

#include <cstdint>

#include "content/grid.h"

namespace mu::sim {

// MU's map number for castle 1 (WD_11BLOODCASTLE1). The realm keeps its own copy of this map's
// grid, which the run changes as it goes.
constexpr uint32_t kBloodCastleMap = 11;

// **When it opens** (the user, 2026-10-02: 'every 1 hour BC is opened'): WebZen's hourly default.
// The Messenger of Archangel in Devias lets a ticket holder in from hh:25 for five minutes
// (BloodCastle.cpp:778-802, entry closed at hh:30, :1128-1171), on the local wall clock.
constexpr int kCastlePeriod = 3600;      // seconds between openings
constexpr int kCastleOpensAt = 25 * 60;  // into the hour
constexpr int kCastleEntry = 5 * 60;     // how long the Messenger lets him in
// **The run, on his own clock from the moment he is in** (the user: 'when you are in there is
// timer and BC starts'). Ours that it starts on entry; WebZen starts every castle together at
// hh:31. Its numbers are WebZen's: 60 s in the safe court ("the quest starts in 60 s", lMsg
// 1163), then 15 minutes of play (BloodCastle.cpp:887-917; the client's SetMatchInfo(15 * 60),
// MuMain NewBloodCastleSystem.cpp:46, 73). OpenMU's is 20.
constexpr int kCastleWait = 60;
constexpr int kCastleRun = 15 * 60;

// Seconds the Messenger has left to let him in, at `daySeconds` into the local day, or 0 when
// the door is shut.
constexpr int castleEntryLeft(int daySeconds) {
    const int phase = ((daySeconds - kCastleOpensAt) % kCastlePeriod + kCastlePeriod) % kCastlePeriod;
    return phase < kCastleEntry ? kCastleEntry - phase : 0;
}

// **The Messenger's door** (WebZen NpcTalk.cpp:1655-1753, protocol.cpp:19629-~20030): spoken to
// with an Invisibility Cloak while the entry is open, he takes the cloak and sends him to exit gate
// 66, the castle's safe court (gObjMoveGate(GATE_BLOODCASTLE_1)). MU has no enter gate for it;
// kCastleEnterGate is ours, a row with no box, so the map change takes the gates' road
// (sim/gates.cpp, Realm::passGate). The cloak's level is the castle; castle 1 asks levels 15 to
// 80 (BloodCastle.h:105-120, CheckEnterLevel :1214-1267).
constexpr int32_t kCastleGate = 66;
constexpr int32_t kCastleEnterGate = 1066;
// The castles a cloak can be made for at 0.97d (+1 to +6, sim/machine.h kCloakMostLevel), each
// with its band (BloodCastle.h:105-120; the sixth "281-MAX", 0 here for no ceiling), and how many
// are built: the first. The Messenger's page steps through all six (the user, 2026-10-03: 'allow to
// choose BC levels with arrows sismilair like we show active quests').
constexpr int kCastles = 6;
constexpr int kCastlesBuilt = 1;
constexpr int kCastleBands[kCastles][2] = {{15, 80},   {81, 130},  {131, 180},
                                           {181, 230}, {231, 280}, {281, 0}};
// The castle his level's band holds, 1 to kCastles: the page the Messenger opens on.
constexpr int castleFor(int level) {
    for (int c = 0; c < kCastles; ++c) {
        if (level <= kCastleBands[c][1] || kCastleBands[c][1] == 0) return c + 1;
    }
    return kCastles;
}
// Why he did not let him in, carried in Shout::Greet's b (0 is the old "not ready").
enum class CastleRefusal : int32_t {
    None = 0,
    NoCloak = 1,    // ServerCmd 1,21
    NotYet = 2,     // ServerCmd 1,20: a cloak, outside the entry window
    TooLow = 3,     // result 4
    TooHigh = 4,    // result 3
    NotBuilt = 5,   // ours: a castle past kCastlesBuilt
};

// **The run** (docs/blood-castle-port.md §5; the user, 2026-10-03: 'lets test scnearou that
// character is already in there is timer to start BC, gates are locked'). Raised on the castle,
// he waits kCastleWait in the safe court with the entrance shut, then the run's kCastleRun
// starts and the entrance opens. WebZen's quotas for one player (SetMonsterKillCount,
// gObjMonster.cpp:1238-1341): 40 kills of anything but the Spirit Sorcerer, then 2 of those.
enum class CastlePhase : uint8_t { None, Waiting, Running, Ended };
constexpr int64_t kCastleTicksPerSecond = 20;  // the realm's tick, MU2's Realm.Hz
constexpr int kCastleKills = 40;
constexpr int kCastleSorcerers = 2;
constexpr int32_t kCastleSorcerer = 89;  // the Magic Skeleton, WebZen's Spirit Sorcerer
constexpr int32_t kCastleStatue = 132;   // the Statue of Saint, raised by the run
struct CastleRun {
    CastlePhase phase = CastlePhase::None;
    int castle = 1;
    int64_t startsAt = 0;  // the tick the wait ends and the run starts
    int64_t endsAt = 0;    // the tick the run's time is up
    int kills = 0;         // quota 1
    int sorcerers = 0;     // quota 2
};

// A rectangle of tiles, inclusive, and the attribute bits the run clears from it.
struct GridBox {
    int x1, y1, x2, y2;
    uint16_t bits;
};

// WebZen BloodCastle.h:128-173 (the courtyard and altar from row 80; OpenMU's 78 is castle wall
// in every grid), cleared at BloodCastle.cpp:2506-2625. The entrance at the start, the
// drawbridge's gap at the first quota, the door with the courtyard and altar behind it when the
// Castle Gate dies.
constexpr GridBox kCastleEntrance = {13, 15, 15, 23, content::kNoMove};
constexpr GridBox kCastleBridge = {13, 70, 15, 75, content::kNoGround};
constexpr GridBox kCastleDoor[3] = {
    {13, 76, 15, 79, content::kNoMove},
    {11, 80, 25, 89, content::kNoMove},
    {8, 80, 10, 83, content::kNoMove},
};

}  // namespace mu::sim
