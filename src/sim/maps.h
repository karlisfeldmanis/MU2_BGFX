// The worlds a character can stand in, one row each (the server's as much as the client's: the
// server lands a player by it, docs/sprints/20-the-world-host.md): what the rest of the game used to assume
// was Lorencia by writing 142,126 and "lorencia" wherever it needed a map.
//
// A row is only what no cooked file says. The land, the town and the rules are each world's
// own cook (assets/world/<name>, cooked/<name>/<name>.mut and .mur) and are found by the name;
// the lighting laid over the base sheet is sheets/worlds/<name>.json when there is one. What is
// left is where a character comes in, which is MU's spawn gate for the map.
#pragma once

#include <string>

namespace mu::sim {

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
    // A dungeon of floors -- the Dungeon's three, the Lost Tower's seven -- and the only maps
    // Go Back! opens from (app/context.h). Left by magic from any other map, the field of a
    // town's map, Atlans or Tarkan among them, there is no way back (the user, 2026-10-07: 'go
    // back! only works for dungeons with floors').
    bool floors = false;
};

// The row for `world`, or nullptr for one not in the table.
const MapRow* mapOf(const std::string& world);

// The row for MU's map number, or nullptr: where a gate's target map is found.
const MapRow* mapNumbered(int number);

// The next world in the table after `world`, wrapping: the stand-in for a Move window until
// there is one (app/modes/play_mode.cpp, the M key).
const MapRow* mapAfter(const std::string& world);

}  // namespace mu::sim
