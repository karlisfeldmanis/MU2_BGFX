#include "sim/maps.h"

namespace mu::sim {
namespace {

// The world table, generated from each world's manifest's `rules` by pipeline/realm_tables.py,
// in its `move_order` (the M key's walk, mapAfter), with every row's citation in the manifest.
#include "sim/maps_table.inc"

}  // namespace

const MapRow* mapOf(const std::string& world) {
    for (const MapRow& row : kMaps) {
        if (world == row.world) return &row;
    }
    return nullptr;
}

const MapRow* mapNumbered(int number) {
    for (const MapRow& row : kMaps) {
        if (row.number == number) return &row;
    }
    return nullptr;
}

const MapRow* mapAfter(const std::string& world) {
    constexpr int count = int(sizeof(kMaps) / sizeof(kMaps[0]));
    for (int i = 0; i < count; ++i) {
        if (world == kMaps[i].world) return &kMaps[(i + 1) % count];
    }
    return &kMaps[0];
}

}  // namespace mu::sim
