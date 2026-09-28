#include "game/world/maps.h"

#include "core/files.h"

namespace mu::game {
namespace {

// mu.db's gates table, the spawn rows:
//   17  map 0  133,118 to 151,135   Lorencia
//   27  map 3  171,108 to 177,117   Noria
// Lorencia's 142,126 is the tile the world has always opened on, and it is inside gate 17; kept
// so a character made before this table comes in where he always did.
constexpr MapRow kMaps[] = {
    {"lorencia", 0, {142, 126}},
    {"noria", 3, {174, 112}},
};

}  // namespace

const MapRow* mapOf(const std::string& world) {
    for (const MapRow& row : kMaps) {
        if (world == row.world) return &row;
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

std::string mapSheet(const std::string& sheetsDir, const std::string& world) {
    const std::string path = core::join(sheetsDir, "worlds/" + world + ".json");
    return core::fileExists(path) ? path : std::string();
}

}  // namespace mu::game
