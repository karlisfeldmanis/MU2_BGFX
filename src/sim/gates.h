// The gates a character walks through from one map to the next: MU's Gate.txt as OpenMU's
// Version075 transcribes it (Persistence/Initialization/Version075/Gates.cs). Two kinds of row,
// as there:
//
// - An ENTER gate is a box on a map. MuMain's CheckGate (ZzzInterface.cpp:2709) fires the
//   moment the hero's tile lies inside one, and if his level is high enough the server's
//   WarpGateAction moves him to the gate's target.
// - An EXIT gate is where he comes out: a box on the other map, a tile in it chosen at random
//   (Player.WarpToAsync), and the way he faces there.
//
// Only Lorencia's and Noria's pair is here, the one walk this game has two maps for. mu.db's
// `gates` table holds the spawn rows alone and no enter gates, so the rows are written out and
// not cooked.
#pragma once

#include <cstdint>

namespace mu::sim {

struct GateBox {
    int32_t x1 = 0, y1 = 0, x2 = 0, y2 = 0;  // in tiles, both corners inside
    bool holds(int column, int row) const {
        return column >= x1 && column <= x2 && row >= y1 && row <= y2;
    }
};

struct ExitGate {
    int32_t number = 0;
    uint32_t map = 0;  // MU's own map number
    GateBox box;
    // OpenMU's Direction as the tile it points at (DirectionExtensions.CalculateTargetPoint),
    // so the facing it gives is atan2(dy, dx), the realm's own sense of an angle.
    int32_t dx = 0, dy = 0;
};

struct EnterGate {
    int32_t number = 0;
    uint32_t map = 0;
    GateBox box;
    int32_t level = 0;   // the level it asks; 0.75 has no class that pays two thirds of it
    int32_t target = 0;  // the exit gate's number; -1 sealed, a gate to a map not built
    const char* sealed = nullptr;  // a sealed gate's place, for the map and the refusal
};

// The enter gate on `map` whose box holds the tile, or nullptr.
const EnterGate* enterGateAt(uint32_t map, int column, int row);

// The enter gate with that number, or nullptr.
const EnterGate* enterGateNumbered(int32_t number);

// The exit gate with that number, or nullptr.
const ExitGate* exitGate(int32_t number);

}  // namespace mu::sim
