#include "sim/gates.h"

namespace mu::sim {
namespace {

// The tables, generated from each world's manifest's `gates` by pipeline/realm_tables.py, where
// every row's citation is (OpenMU's Gates.cs, WebZen's gate.txt, and the user's level choices).
#include "sim/gates_table.inc"

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

const EnterGate* enterGates(int* count) {
    *count = int(sizeof(kEnters) / sizeof(kEnters[0]));
    return kEnters;
}

const ExitGate* exitGate(int32_t number) {
    for (const ExitGate& gate : kExits) {
        if (gate.number == number) return &gate;
    }
    return nullptr;
}

const EnterGate* gateToward(uint32_t from, uint32_t to, int column, int row) {
    if (from == to) return nullptr;
    // Where a gate lets out, or none: a sealed one, one off the map (Blood Castle's door), a stair.
    const auto leadsTo = [&](const EnterGate& gate, uint32_t* map) {
        if (gate.target < 0 || gate.box.x1 < 0) return false;
        const ExitGate* out = exitGate(gate.target);
        if (!out || out->map == gate.map) return false;
        *map = out->map;
        return true;
    };
    // Gates still to go from each map to `to`, walked back from it.
    constexpr int kMaps = 128;
    int left[kMaps];
    for (int& one : left) one = -1;
    if (to >= uint32_t(kMaps)) return nullptr;
    left[to] = 0;
    for (bool grew = true; grew;) {
        grew = false;
        for (const EnterGate& gate : kEnters) {
            uint32_t map = 0;
            if (!leadsTo(gate, &map) || map >= uint32_t(kMaps) || gate.map >= uint32_t(kMaps)) continue;
            if (left[map] < 0) continue;
            if (left[gate.map] < 0 || left[gate.map] > left[map] + 1) {
                left[gate.map] = left[map] + 1;
                grew = true;
            }
        }
    }
    const EnterGate* best = nullptr;
    int bestLeft = 0, bestFar = 0;
    for (const EnterGate& gate : kEnters) {
        uint32_t map = 0;
        if (gate.map != from || !leadsTo(gate, &map) || map >= uint32_t(kMaps) || left[map] < 0) continue;
        const int dc = (gate.box.x1 + gate.box.x2) / 2 - column, dr = (gate.box.y1 + gate.box.y2) / 2 - row;
        const int far = dc * dc + dr * dr;
        if (!best || left[map] < bestLeft || (left[map] == bestLeft && far < bestFar)) {
            best = &gate;
            bestLeft = left[map];
            bestFar = far;
        }
    }
    return best;
}

}  // namespace mu::sim
