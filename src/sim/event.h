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
