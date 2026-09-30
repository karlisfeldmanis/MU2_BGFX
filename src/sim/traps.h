// The Dungeon's traps: OpenMU Version075's three trap breeds and their 58 spots, carried in code
// as the gates are (sim/gates.h) -- mu.db has neither, and a trap is not a monster: it has no
// body to hit, never moves and never dies (TrapIntelligenceBase.RegisterHit throws).
//
// Each fires on its own clock, every AttackDelay (1000 ms), its first after that and a random
// share of a second more (TrapIntelligenceBase.Start). What it fires at is its intelligence's:
//
// - **Iron Stick Trap (101)**, AttackSingleWhenPressedTrapIntelligence: whoever stands ON its tile.
// - **Fire Trap (102)**, AttackAreaTargetInDirectionTrapIntelligence: whoever is within its
//   AttackRange (2) and whose direction from it is the way it faces, off a safe tile.
// - **Lance Trap (100)**: OpenMU gives it the Iron Stick's "when pressed", but all 27 stand on
//   tiles nobody can enter, so as OpenMU has them they never fire. Fired here as the Fire Trap
//   fires, at its own AttackRange 4 -- ours, the user's (2026-09-30), docs/dungeon-port.md
//   decision 2.
//
// The client has a 59th, a Fire Trap object at 69,73 with no OpenMU row; left out, the user's
// (decision 3). MuMain draws each as the Dungeon object it stands on: 100 as model 39
// (Object40), 101 as 40 (Object41), 102 as 51 (Object52) (ZzzCharacter.cpp:14274-14282).
#pragma once

#include <cstddef>
#include <cstdint>

namespace mu::sim {

struct TrapKind {
    int32_t number = 0;
    const char* label = "";
    const char* model = "";  // the Dungeon object MuMain draws it as
    int32_t attackRange = 0;
    bool pressed = false;    // fires at its own tile rather than along its facing
    int32_t level = 0;
    int32_t minimumDamage = 0, maximumDamage = 0;
    float attackRate = 0.0f;
    float defenseRate = 0.0f;
};

struct TrapSpot {
    uint32_t map = 0;     // MU's own map number
    int32_t number = 0;   // the breed
    int32_t column = 0, row = 0;
    // OpenMU's Direction as the tile it points at (DirectionExtensions.CalculateTargetPoint), the
    // gates' own convention: SouthWest (0, -1), SouthEast (+1, 0), NorthEast (0, +1).
    int32_t dx = 0, dy = 0;
};

// The breed with that number, or nullptr.
const TrapKind* trapKind(int32_t number);

// Every trap spot, all maps.
const TrapSpot* trapSpots(size_t* count);

// OpenMU's GetDirectionTo as one of its eight octants, 0 to 7, or -1 for the same tile: the
// angle from one tile to the other, half-turned and cut into eighths with .NET's rounding of a
// half to the even eighth (DirectionExtensions.cs:44-68). Two points are "the same way" when
// their octants agree, which is the Fire Trap's whole test.
int octantOf(int dx, int dy);

}  // namespace mu::sim
