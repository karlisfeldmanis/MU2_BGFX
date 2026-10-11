#include "game/world/maps.h"

#include <algorithm>
#include <cstdio>

#include "content/tables.h"
#include "core/files.h"
#include "game/world/world_show.h"

namespace mu::game {

std::string zoneLevels(const content::Tables& tables) {
    int low = 0, high = 0;
    for (const content::MonsterNest& nest : tables.nests) {
        if (nest.kind >= tables.kinds.size()) continue;
        const int level = tables.kinds[nest.kind].level;
        low = low == 0 ? level : std::min(low, level);
        high = std::max(high, level);
    }
    if (high == 0) return {};
    char line[48];
    if (low == high) std::snprintf(line, sizeof(line), "Level %d", low);
    else std::snprintf(line, sizeof(line), "Level %d-%d", low, high);
    return line;
}

std::string placeName(const std::string& world, int column, int row) {
    const WorldShow& show = worldShow(world);
    return show.places.empty() ? world : show.placeAt(column, row);
}

std::string mapSheet(const std::string& sheetsDir, const std::string& world) {
    const std::string path = core::join(sheetsDir, "worlds/" + world + ".json");
    return core::fileExists(path) ? path : std::string();
}

}  // namespace mu::game
