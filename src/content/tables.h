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

// One weapon or shield, with what a fight reads off it and nothing else. The bag, the drop, the
// durability, an item's level and its options are all sprint 7's; this is the damage band, the
// defence, who may hold it and what it asks of him.
//
// `attackSpeed` is carried and NOT consumed. MU paces a swing by the attack clip's own authored
// length (MU2's Beast.cs:2124-2137 re-reckons SwingDelay from it), and a mapping from this
// number to a swing delay would be an invention in the one sprint that has none.
struct Arm {
    std::string name;    // "Sword01"
    std::string label;   // "Kris"
    std::string stance;  // how it is held: "sword", "two_hand_sword", "spear", "scythe", ...
    int32_t kind = 0;    // 0 a weapon, 1 a shield
    int32_t minimumDamage = 0;
    int32_t maximumDamage = 0;
    int32_t attackSpeed = 0;
    int32_t defense = 0;
    int32_t wantsStrength = 0;
    int32_t wantsAgility = 0;
    // Bit 0 Dark Wizard, bit 1 Fairy Elf, bit 2 Dark Knight -- mu.db's own class enumeration
    // and not MU's packed class byte.
    int32_t classes = 0;
    // MU's own item group and index, which is what the client's attack ladder tests: groups 0,
    // 1 and 2 are swords, axes and maces and all three swing a sword; 3 is the polearms, of
    // which the Spear and the Dragon Lance are named individually; 5 the staves.
    int32_t group = -1;
    int32_t number = -1;
    int32_t flags = 0;  // bit 0 two-handed, bit 1 a bow, bit 2 a crossbow

    bool isShield() const { return kind == 1; }
    bool twoHanded() const { return (flags & 1) != 0; }
    bool bow() const { return (flags & 2) != 0; }
    bool crossbow() const { return (flags & 4) != 0; }
    bool missile() const { return bow() || crossbow(); }
};

// One item, as the bag, the shop, the tooltip and the drop read it: MU2's `Prize` with its
// `Arm` and `Wear` columns folded in, because here they are one row off one asset
// (index.json's objects[].stats) and a second and third table would be the same row cut
// three ways. The fight still reads `Arm` above, which is found from an item by its name.
//
// The requirement is carried RAW. What the game asks is MU's formula over it -- `(3 x drop
// level x raw / 100) + 20`, four for energy -- and that is sim code (sim/items.h), because
// the plus on a particular piece moves it.
struct ItemRow {
    std::string name;   // "Axe01", the asset's
    std::string label;  // "Small Axe"
    std::string glb;    // the model, relative to the assets, for the bag's picture
    int32_t group = 0, number = 0;
    int32_t dropLevel = 0;
    int32_t width = 1, height = 1;
    int32_t minimumDamage = 0, maximumDamage = 0, attackSpeed = 0;
    int32_t defense = 0;
    int32_t defenseRate = 0;  // a shield's block column; 0 on everything else
    int32_t magicPower = 0;
    int32_t durability = 0;  // the shots in a quiver; 0 for everything this sprint wears
    int32_t classes = 0;     // as Arm::classes; 0 is anybody
    int32_t needLevel = 0, needStrength = 0, needAgility = 0, needEnergy = 0, needVitality = 0;
    int32_t flags = 0;
    int32_t maximumDropLevel = 0;
    int32_t skill = 0;
    // What it TEACHES, for a scroll or an orb: the skill's number, what it asks, its name and
    // one line of what it does. Nought and empty on everything else. A weapon's `skill` above
    // is a different thing -- a skill it grants while it is held.
    int32_t teaches = 0;
    int32_t teachesLevel = 0, teachesEnergy = 0;
    std::string teachesName, teachesTells;
    // One line of what the ITEM does, for the rows whose whole point is a thing no column
    // states: the Ale, the Antidote, the Town Portal Scroll and the three jewels. Empty on
    // everything a damage band or a defence already speaks for. Where MU ships its own line
    // for a row this is MU's English letter for letter; where it does not, the asset says so.
    // A scroll's `teachesTells` above is a different sentence -- what the SPELL does -- and a
    // row may honestly carry both.
    std::string tells;

    bool dropsFromMonsters() const { return (flags & 1) != 0; }
    bool jewel() const { return (flags & 2) != 0; }
    bool twoHanded() const { return (flags & 4) != 0; }
    bool armour() const { return (flags & 8) != 0; }
    bool shield() const { return (flags & 16) != 0; }
    bool weapon() const { return (flags & 32) != 0; }
};

// One of the town's people: who, where, which way, and what draws them. See FOLK_VERSION075
// in tools/cook.py for the source. `figure` is empty where the world's own placements already
// stand them.
struct Townsperson {
    std::string name;
    std::string figure;
    int32_t number = 0;  // MU's own NPC number: 253 Amy, 251 Hanzo, ...
    int32_t x = 0, y = 0;
    int32_t look = 0;    // MU2's Look: 1 West, 2 SouthWest, 3 South ... 8 NorthWest
};

// One of the player library's attack clips, as two numbers: how many keys it has and the play
// speed it was authored at. That is all a swing rate is made of --
// `length = keys / ((speed + attackSpeed * 0.004) * 25)` seconds -- and it is here because the
// sim has no clips and must not grow any.
struct PlayerAction {
    int32_t action = 0;  // MU's own number: 38 the fist, 39-45 the swords, 50 the bow
    int32_t keys = 0;
    float speed = 0.0f;
};

struct Tables {
    uint32_t hz = 0;   // the tick rate the delays were converted at; checked, never assumed
    uint32_t map = 0;  // MU's own map number
    // The map's own spawn box, where a dead character stands up. Zeroes when the map has none.
    int32_t safeGate[4] = {0, 0, 0, 0};  // x1, y1, x2, y2 in tiles
    std::vector<MonsterKind> kinds;
    std::vector<MonsterNest> nests;
    std::vector<Arm> arms;
    std::vector<PlayerAction> actions;
    std::vector<ItemRow> items;
    std::vector<Townsperson> folk;
    Grid grid;

    // By MU's own group and number, or -1.
    int32_t itemAt(int32_t group, int32_t number) const {
        for (size_t i = 0; i < items.size(); ++i) {
            if (items[i].group == group && items[i].number == number) return int32_t(i);
        }
        return -1;
    }
    int32_t itemNamed(const std::string& name) const {
        for (size_t i = 0; i < items.size(); ++i) {
            if (items[i].name == name) return int32_t(i);
        }
        return -1;
    }

    const PlayerAction* action(int32_t number) const {
        for (const PlayerAction& one : actions) {
            if (one.action == number) return &one;
        }
        return nullptr;
    }

    // By name, as the command line and the figure tables spell it. -1 for none.
    int32_t armNamed(const std::string& name) const {
        for (size_t i = 0; i < arms.size(); ++i) {
            if (arms[i].name == name) return int32_t(i);
        }
        return -1;
    }

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
