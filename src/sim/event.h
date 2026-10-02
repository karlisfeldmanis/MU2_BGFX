#pragma once

// Blood Castle's run: the castle's grid boxes that the run opens. docs/blood-castle-port.md,
// Part A section 4.3. Castle 1 only for now; castles 2-7 share the terrain (MapManager.cpp:
// 1104-1106) and differ only in their monsters.

#include <cstdint>

#include "content/grid.h"

namespace mu::sim {

// MU's map number for castle 1 (WD_11BLOODCASTLE1). The realm keeps its own copy of this map's
// grid, which the run changes as it goes.
constexpr uint32_t kBloodCastleMap = 11;

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
