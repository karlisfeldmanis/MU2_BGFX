// The worlds a character can stand in, one row each: what the rest of the game used to assume
// was Lorencia by writing 142,126 and "lorencia" wherever it needed a map.
//
// A row is only what no cooked file says. The land, the town and the rules are each world's
// own cook (assets/world/<name>, cooked/<name>/<name>.mut and .mur) and are found by the name;
// the lighting laid over the base sheet is sheets/worlds/<name>.json when there is one. What is
// left is where a character comes in, which is MU's spawn gate for the map.
#pragma once

#include <string>

namespace mu::content {
struct Tables;
}

namespace mu::game {

struct MapRow {
    const char* world;  // the folder name, which is also --world's
    int number;         // MU's map number: 0 Lorencia, 3 Noria (mu.db gates.map)
    // The tile a character comes in on: the middle of mu.db's spawn gate for the map (gates
    // 17 and 27, both spawn = 1). The realm moves him to the nearest tile he may stand on.
    int arrive[2];
    // Under a roof on every tile: no sky's leaves, no wind, the room's air. The Dungeon and the
    // Lost Tower, which MU draws on its black clear with no weather (SceneManager.cpp:402).
    bool underground = false;
    // MU's map number of the town a death or a Town Portal sends him to from a map with no
    // safe zone of its own: Lorencia (OpenMU's SafezoneMap fallback, BaseMapInitializer.cs:91),
    // or Devias from Blood Castle (WebZen user.cpp:22084-22089, gate 22).
    int home = 0;
    // An event map: a save never resumes inside it. He is written standing in `home`'s town, as
    // WebZen logs a player on a Blood Castle map in at Devias (user.cpp:3147-3150).
    bool event = false;
};

// The row for `world`, or nullptr for one not in the table.
const MapRow* mapOf(const std::string& world);

// The row for MU's map number, or nullptr: where a gate's target map is found.
const MapRow* mapNumbered(int number);

// The next world in the table after `world`, wrapping: the stand-in for a Move window until
// there is one (app/modes/play_mode.cpp, the M key).
const MapRow* mapAfter(const std::string& world);

// The line under the map's name as he comes in: "Level 2-38", the lowest and the highest level
// of the monsters the map's nests raise. Empty on a map with no nests. Invention: MU's
// ShowMapName is the map's picture alone.
std::string zoneLevels(const content::Tables& tables);

// The name to show for a tile of `world`: "Dungeon 1", "Dungeon 2" or "Dungeon 3" by the floor
// the tile is on -- three disconnected regions of one map, the Move list's Dungeon, Dungeon2 and
// Dungeon3 (OpenMU Version075 Gates.cs:48-50; docs/dungeon-port.md §1.2) -- and `world` itself
// on every other map. The user's, 2026-09-30.
std::string placeName(const std::string& world, int column, int row);

// The lighting overlay for a world, laid over sheets/lighting.json by TimeOfDay::setScene:
// `sheetsDir`/worlds/<world>.json when the file exists, and empty -- the base sheet alone --
// when it does not.
std::string mapSheet(const std::string& sheetsDir, const std::string& world);

}  // namespace mu::game
