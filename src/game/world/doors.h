// Devias's doors: the only objects in MU's first maps that move for the player.
//
// MoveObject, case WD_2DEVIAS (ZzzObject.cpp:3918-3963), for types 20, 65, 88 (Object21, 66,
// 89: doors on a hinge) and 86 (Object87: an iron gate that slides). CreateObject copies each
// one's placement as its rest -- `HeadAngle` the yaw, taken modulo 360, and `HeadTargetAngle`
// the position (:4703-4711) -- and every frame after that, with the hero `d` units from the
// rest point:
//
//   inside 200 units, a door swings to a yaw by its rest yaw --
//       90 -> 30 - (200 - d) / 2     270 -> 330 + (200 - d) / 2
//        0 -> 300 - (200 - d) / 2    180 -> 240 + (200 - d) / 2
//   and a gate slides along its own rest yaw by (200 - d) * 2 units,
//       90 -> +y, 270 -> -y, 0 -> +x, 180 -> -x;
//   outside it, a door turns back to its rest by 10 degrees a frame (TurnAngle2) and a gate
//   eases a fifth of the way home a frame.
//
// What is not MU's, and marked: MU SETS an opening door's yaw, so a door snaps its first sixty
// degrees on the frame the hero comes inside the radius; here it turns towards that target at
// the same pace it turns back, so the swing is seen. And the sound: MU calls PlayBuffer every
// frame the hero is near, and one voice turns that into a door that creaks again every second
// and a third he stands in the doorway. Here a door creaks once as it starts to open and once
// as it starts to shut, placed at the door. Nothing here reaches the sim, whose grid MU left
// open under every doorway.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mu::game {

class Town;

class Doors {
public:
    // Finds the world's doors among the town's placements. Only Devias has any.
    void open(const std::string& world, const Town& town, float metresPerTile);
    void shutdown() { doors_.clear(); }
    bool isOpen() const { return !doors_.empty(); }

    // One frame, the hero's feet in metres. Writes each door's pose into the town.
    void update(float seconds, float heroX, float heroZ, Town& town);

    // The creaks the last update started: where, and whether a gate's (aCastleDoor) or a
    // door's (aDoor). Read and cleared by whoever plays them.
    struct Creak {
        float at[3];
        bool gate;
    };
    const std::vector<Creak>& creaks() const { return creaks_; }

private:
    struct Door {
        uint32_t instance = 0;
        bool slides = false;
        float restDegrees = 0.0f;  // MU's HeadAngle, modulo 360
        float rest[3] = {0.0f, 0.0f, 0.0f};  // metres, the placement as cooked
        float degrees = 0.0f;      // the yaw now, MU's degrees
        float offset = 0.0f;       // a gate's slide now, metres along its axis
        bool near = false;         // the hero inside the radius last frame
    };
    std::vector<Door> doors_;
    std::vector<Creak> creaks_;
    float metresPerTile_ = 1.0f;
};

}  // namespace mu::game
