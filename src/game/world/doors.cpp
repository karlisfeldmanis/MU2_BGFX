#include "game/world/doors.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "game/world/town.h"

namespace mu::game {
namespace {

constexpr float kPi = 3.14159265f;
// MU's radius, 200 units: two tiles.
constexpr float kReachTiles = 2.0f;
// TurnAngle2's 10 degrees a frame and the gate's fifth of the way home, both at the 25 Hz
// MoveObject is written for.
constexpr float kReference = 25.0f;
constexpr float kTurnPerFrame = 10.0f;
constexpr float kHomePerFrame = 0.2f;

// The shortest way from `from` to `to`, in degrees, -180 to 180.
float towards(float from, float to) {
    float d = std::fmod(to - from, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    return d;
}

// Which of MU's four rest yaws a placement's angle is: 0, 90, 180 or 270, or -1 for a door
// MuMain's arms do not name (it then stands still, as it does in MU).
int quarter(float degrees) {
    for (int q : {0, 90, 180, 270}) {
        if (std::fabs(towards(degrees, float(q))) < 0.5f) return q;
    }
    return -1;
}

}  // namespace

void Doors::open(const std::string& world, const Town& town, float metresPerTile) {
    doors_.clear();
    creaks_.clear();
    metresPerTile_ = metresPerTile;
    if (world != "devias") return;
    const auto& models = town.cooked().models;
    const auto& instances = town.cooked().instances;
    int swings = 0, slides = 0, odd = 0;
    for (uint32_t i = 0; i < instances.size(); ++i) {
        const content::TownInstance& at = instances[i];
        if (at.model >= models.size()) continue;
        const std::string& name = models[at.model].name;
        const bool swinging = name == "Object21" || name == "Object66" || name == "Object89";
        const bool sliding = name == "Object87";
        if (!swinging && !sliding) continue;
        Door door;
        door.instance = i;
        door.slides = sliding;
        door.restDegrees = std::fmod(at.yaw * 180.0f / kPi + 3600.0f, 360.0f);
        if (quarter(door.restDegrees) < 0) {
            ++odd;
            continue;
        }
        for (int a = 0; a < 3; ++a) door.rest[a] = at.position[a];
        door.degrees = door.restDegrees;
        doors_.push_back(door);
        (sliding ? slides : swings)++;
    }
    if (!doors_.empty() || odd > 0) {
        core::logf("doors %s: %d on hinges, %d sliding%s", world.c_str(), swings, slides,
                   odd > 0 ? ", and some at a yaw MU's arms do not name, left standing" : "");
    }
}

void Doors::update(float seconds, float heroX, float heroZ, Town& town) {
    creaks_.clear();
    if (doors_.empty()) return;
    const float frames = seconds * kReference;
    const float reach = kReachTiles * metresPerTile_;
    // Metres per MU unit, for the gate's (200 - d) * 2.
    const float perUnit = metresPerTile_ / 100.0f;
    for (Door& door : doors_) {
        const float dx = heroX - door.rest[0];
        const float dz = heroZ - door.rest[2];
        const float d = std::sqrt(dx * dx + dz * dz);
        const bool near = d < reach;
        if (near != door.near) {
            creaks_.push_back({{door.rest[0], door.rest[1], door.rest[2]}, door.slides});
            door.near = near;
        }
        // In MU's units, which is what the arithmetic is written in.
        const float into = near ? (reach - d) / perUnit : 0.0f;  // 200 - d
        const int q = quarter(door.restDegrees);
        float yaw = door.restDegrees;
        float position[3] = {door.rest[0], door.rest[1], door.rest[2]};
        if (door.slides) {
            const float target = near ? into * 2.0f * perUnit : 0.0f;
            // MU sets the open slide and eases the shut one; eased both ways here.
            door.offset += (target - door.offset) *
                           (1.0f - std::pow(1.0f - kHomePerFrame, frames));
            // MU's +y is our -z; its +x ours.
            if (q == 90) position[2] -= door.offset;
            if (q == 270) position[2] += door.offset;
            if (q == 0) position[0] += door.offset;
            if (q == 180) position[0] -= door.offset;
        } else {
            float target = door.restDegrees;
            if (near) {
                if (q == 90) target = 30.0f - into * 0.5f;
                if (q == 270) target = 330.0f + into * 0.5f;
                if (q == 0) target = 300.0f - into * 0.5f;
                if (q == 180) target = 240.0f + into * 0.5f;
            }
            const float turn = towards(door.degrees, target);
            const float most = kTurnPerFrame * frames;
            door.degrees += std::clamp(turn, -most, most);
            yaw = door.degrees;
        }
        town.movePlacement(door.instance, yaw * kPi / 180.0f, position);
    }
}

}  // namespace mu::game
