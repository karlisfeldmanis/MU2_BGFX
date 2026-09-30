#include "sim/gates.h"

namespace mu::sim {
namespace {

// Gates.cs, CreateTargetGates. Lorencia's 26 is Direction.West (1), which points at (-1,-1),
// up the map and away from gate 23 below it; Noria's 24 is Direction.East (5), (+1,+1), down
// the map and away from gate 25 above it. Either way he comes out facing into the map.
constexpr ExitGate kExits[] = {
    {26, 0, {213, 244, 217, 245}, -1, -1},  // Lorencia, from Noria
    {24, 3, {148, 5, 155, 6}, 1, 1},        // Noria, from Lorencia
    // Lorencia's west edge and Devias's east edge. 21 is Direction.South (3), (+1,-1), into
    // Lorencia and away from gate 18 beside it; 19 is Direction.North (7), (-1,+1), into
    // Devias and away from gate 20.
    {21, 0, {7, 38, 8, 41}, 1, -1},         // Lorencia, from Devias
    {19, 2, {242, 34, 243, 37}, -1, 1},     // Devias, from Lorencia
    // The Dungeon's, Gates.cs:114 and 119-125. 1 West is (-1,-1) and 3 South (+1,-1), each
    // facing away from the enter box beside it. Lorencia's 4 is in front of DoungeonGate01.
    {4, 0, {121, 231, 123, 231}, -1, -1},   // Lorencia, out of the Dungeon
    {2, 1, {107, 247, 110, 247}, -1, -1},   // Dungeon 1, from Lorencia
    {6, 1, {231, 126, 234, 127}, -1, -1},   // Dungeon 2, down from Dungeon 1
    {8, 1, {240, 149, 241, 151}, 1, -1},    // Dungeon 1, back up from Dungeon 2
    {10, 1, {3, 83, 4, 86}, 1, -1},         // Dungeon 3, down from Dungeon 2 by gate 9
    {12, 1, {3, 16, 6, 17}, 1, -1},         // Dungeon 2, back up from Dungeon 3 by gate 11
    {14, 1, {29, 125, 30, 126}, -1, -1},    // Dungeon 3, down from Dungeon 2 by gate 13
    {16, 1, {5, 32, 7, 33}, -1, -1},        // Dungeon 2, back up from Dungeon 3 by gate 15
};

// Gates.cs, CreateEnterGates: 23 on Lorencia's last rows, 25 on Noria's first, each two rows
// past its exit gate so a character who comes out is not sent straight back, and each asks
// level 10.
constexpr EnterGate kEnters[] = {
    {23, 0, {213, 246, 217, 247}, 10, 24},  // Lorencia to Noria
    {25, 3, {148, 3, 155, 4}, 10, 26},      // Noria to Lorencia
    // Gates.cs:193-194. Lorencia's asks level 15; the way back asks nothing.
    {18, 0, {5, 38, 6, 41}, 15, 19},        // Lorencia to Devias
    {20, 2, {244, 34, 245, 37}, 0, 21},     // Devias to Lorencia
    // Gates.cs:197, Devias's far corner under the Lost Tower's beacon, level 40, to exit gate
    // 29. Sealed: the Lost Tower is not built, so its target is -1 and the realm refuses every
    // step into it (Realm::throughGate). Ours, until there is a map behind it.
    {28, 2, {2, 248, 3, 249}, 40, -1, "The Lost Tower"},  // Devias to the Lost Tower, sealed
    // Gates.cs:185, Lorencia's stair down at DoungeonGate01, level 20, to the Dungeon's exit
    // gate 2. And the Dungeon's own, :186-192: the way out asks nothing, every stair between
    // its three floors asks level 20. Floors are regions of one map, so 5 to 15 lead to the
    // map they stand on (Realm::throughGate).
    {1, 0, {121, 232, 123, 233}, 20, 2},    // Lorencia to Dungeon 1
    {3, 1, {108, 248, 109, 248}, 0, 4},     // Dungeon 1 to Lorencia
    {5, 1, {239, 149, 239, 150}, 20, 6},    // Dungeon 1 down to Dungeon 2
    {7, 1, {232, 127, 233, 128}, 20, 8},    // Dungeon 2 up to Dungeon 1
    {9, 1, {2, 17, 2, 18}, 20, 10},         // Dungeon 2 down to Dungeon 3
    {11, 1, {2, 84, 2, 85}, 20, 12},        // Dungeon 3 up to Dungeon 2
    {13, 1, {5, 34, 6, 34}, 20, 14},        // Dungeon 2 down to Dungeon 3, the second way
    {15, 1, {29, 127, 30, 127}, 20, 16},    // Dungeon 3 up to Dungeon 2, the second way
};

}  // namespace

const EnterGate* enterGateAt(uint32_t map, int column, int row) {
    for (const EnterGate& gate : kEnters) {
        if (gate.map == map && gate.box.holds(column, row)) return &gate;
    }
    return nullptr;
}

const EnterGate* enterGateNumbered(int32_t number) {
    for (const EnterGate& gate : kEnters) {
        if (gate.number == number) return &gate;
    }
    return nullptr;
}

const ExitGate* exitGate(int32_t number) {
    for (const ExitGate& gate : kExits) {
        if (gate.number == number) return &gate;
    }
    return nullptr;
}

}  // namespace mu::sim
