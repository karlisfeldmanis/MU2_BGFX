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
    // The Lost Tower's, Gates.cs:138-145, and Devias's 44 (:130); docs/lost-tower-port.md §2.1.
    // 42 is the spawn gate, the safe hall, with no direction (0); 29-41 are each floor's
    // arrival. 1 West (-1,-1), 3 South (+1,-1), 5 East (+1,+1), 2 SouthWest (0,-1).
    {42, 4, {203, 70, 213, 81}, 0, 0},      // the Lost Tower's hall: warp, death, Town Portal
    {29, 4, {162, 2, 166, 3}, 1, 1},        // Lost Tower 1, from Devias
    {31, 4, {241, 237, 244, 238}, -1, -1},  // Lost Tower 2, down from 1
    {33, 4, {86, 166, 87, 168}, 1, -1},     // Lost Tower 3, down from 2
    {35, 4, {87, 86, 88, 89}, 1, -1},       // Lost Tower 4, down from 3
    {37, 4, {128, 53, 131, 54}, -1, -1},    // Lost Tower 5, down from 4
    {39, 4, {52, 53, 55, 54}, -1, -1},      // Lost Tower 6, down from 5
    {41, 4, {8, 85, 9, 87}, -1, -1},        // Lost Tower 7, down from 6
    {44, 2, {2, 246, 3, 247}, 0, -1},       // Devias, out of the Lost Tower, under the beacon
    // Blood Castle 1's, Gates.cs:216 (WZD Gate.txt:115): its safe court beside the Archangel,
    // with no direction. Reached only through the Messenger (sim/event.h).
    {66, 11, {12, 5, 14, 10}, 0, 0},
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
    // Gates.cs:197, Devias's far corner under the Lost Tower's beacon, level 40, to exit gate 29.
    {28, 2, {2, 248, 3, 249}, 40, 29},      // Devias to the Lost Tower
    // The Lost Tower's, Gates.cs:198-204 (docs/lost-tower-port.md §2.2): the door out asks 15,
    // the first two stairs 40 and the last four 50 (OM's, not the repack's 80). Every stair goes
    // down; no source has one back up. Each box sits in a wall niche with 2-3 open tiles, which
    // throughGate's test of the walker's own tile handles. Floors are regions of one map, so
    // 30-40 lead to the map they stand on, as the Dungeon's stairs do.
    {43, 4, {162, 0, 166, 1}, 15, 44},      // Lost Tower 1 out to Devias
    {30, 4, {190, 6, 191, 8}, 40, 31},      // Lost Tower 1 down to 2
    {32, 4, {166, 163, 167, 166}, 40, 33},  // Lost Tower 2 down to 3
    {34, 4, {132, 245, 135, 246}, 50, 35},  // Lost Tower 3 down to 4
    {36, 4, {132, 135, 135, 136}, 50, 37},  // Lost Tower 4 down to 5
    {38, 4, {131, 15, 132, 18}, 50, 39},    // Lost Tower 5 down to 6
    {40, 4, {6, 5, 7, 8}, 50, 41},          // Lost Tower 6 down to 7
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
    // Ours: the Messenger's door into Blood Castle 1 (sim/event.h kCastleEnterGate), a row with
    // a box off the map so no step ever stands in it. His talk sends him through.
    {1066, 2, {-1, -1, -1, -1}, 0, 66},
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
