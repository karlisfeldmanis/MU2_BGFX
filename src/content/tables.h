// The cooked rules tables: what a breed is, where its nests are, and the map's attribute
// grid. Written by tools/cook.py's cook_tables, which is the only thing that writes them, and
// read here whole. No sqlite in the game (PLAN.md foundation 11), and no json either: this is
// what a fight reads a hundred times a tick.
//
// What is NOT here: experience and drops. mu.db holds no such table -- MU has an expression
// per level rather than a list -- and both are code, in src/sim/rules.h, with the OpenMU
// lines beside them. A cook that emitted an "experience table" would be inventing one.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/grid.h"

namespace mu::content {

// One breed, with every delay already in ticks. The conversion happened once, in the cook, at
// the tick rate written into the file: a delay re-derived per tick in floating point is how a
// seeded run stops reproducing.
struct MonsterKind {
    std::string figure;  // the cooked figure this breed wears, or empty
    std::string label;   // "Bull Fighter"
    int32_t number = 0;  // MU's own monster number
    int32_t level = 0;
    int32_t health = 0;
    int32_t minimumDamage = 0;
    int32_t maximumDamage = 0;
    int32_t defense = 0;
    int32_t moveRange = 0;    // how far it wanders, in tiles -- NOT how fast
    int32_t attackRange = 0;
    int32_t viewRange = 0;
    int32_t moveTicks = 0;    // per tile
    int32_t attackTicks = 0;  // between swings
    int32_t respawnTicks = 0;
    int32_t attackRate = 0;
    int32_t defenseRate = 0;
    int32_t attackSkill = 0;  // a monster's animation and projectile choice, not a skill system
    float scale = 1.0f;
};

// One nest: a rectangle in MU tile coordinates, a count and a breed. `y` is the attribute
// grid's row and is negated only on the way into the world.
struct MonsterNest {
    uint32_t kind = 0;  // index into Tables::kinds
    int32_t x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    uint32_t count = 0;
};

struct Tables {
    uint32_t hz = 0;   // the tick rate the delays were converted at; checked, never assumed
    uint32_t map = 0;  // MU's own map number
    // The map's own spawn box, where a dead character stands up. Zeroes when the map has none.
    int32_t safeGate[4] = {0, 0, 0, 0};  // x1, y1, x2, y2 in tiles
    std::vector<MonsterKind> kinds;
    std::vector<MonsterNest> nests;
    Grid grid;

    uint32_t population() const {
        uint32_t total = 0;
        for (const MonsterNest& nest : nests) total += nest.count;
        return total;
    }
};

// Both fill `error` with a sentence rather than logging: the caller knows which file it asked
// for. Every count in the file is treated as hostile.
bool parseTables(const std::vector<uint8_t>& bytes, Tables& out, std::string& error);
bool loadTables(const std::string& path, Tables& out, std::string& error);

}  // namespace mu::content
