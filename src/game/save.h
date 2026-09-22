// The character, kept between runs: where he stands, what he has earned, what he carries and
// wears, and the four potion keys. PLAN.md's "one versioned file written whole".
//
// Items are written by MU's own (group, number) and never by the cooked table's row: a recook
// that adds or orders a row differently would otherwise turn a saved sword into whatever now
// sits at its index. A pair the tables no longer know is dropped on load and said so.
//
// Written to a temporary beside the file and renamed over it, so a crash mid-write leaves the
// last good save rather than half of a new one.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/tables.h"
#include "sim/realm.h"

namespace mu::game {

struct Saved {
    std::string world;
    sim::HeroRecord hero;             // hero.slots filled by resolveSave
    int32_t quick[5] = {-1, -1, -1, -1, -1};  // item rows, -1 for none; filled by resolveSave

    // As read, before the tables are there to turn them into rows. The file is read before
    // the world is -- it decides the class, the level and the tile the world is raised at --
    // and the item tables arrive with the realm, so the two steps are apart.
    struct Item {
        int slot = -1, group = -1, number = -1, plus = 0, durability = 0;
        bool skill = false;
    };
    std::vector<Item> items;
    int quickGroup[5] = {-1, -1, -1, -1, -1}, quickNumber[5] = {-1, -1, -1, -1, -1};
};

// Where the save lives when --save does not say: ~/Library/Application Support/MU2/hero.json,
// outside the build and the repo, so a clean rebuild or a checkout never touches it.
std::string defaultSavePath();

// False when there is no file or it cannot be read as a save; the reason is in the log.
bool loadSave(const std::string& path, Saved& out);
// Turns what loadSave read into item rows, into hero.slots and quick.
void resolveSave(const content::Tables& tables, Saved& saved);
bool writeSave(const std::string& path, const content::Tables& tables, const Saved& saved);

}  // namespace mu::game
