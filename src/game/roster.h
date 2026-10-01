// The account's characters: who stands on the character screen's five pedestals.
//
// One save file a character (game/save.h), in `characters/` beside the account's vault, each
// carrying its own name and slot. MU keeps this on the server as the account's character list
// (OpenMU's `CharacterList075`); here there is no server and no account but the one on this
// machine, so the list is whatever that folder holds.
//
// The rules are OpenMU's and MuMain's, as MU2's `Screens` carried them: five characters an
// account, and a name of letters and digits only, at most ten (`^[a-zA-Z0-9]{3,10}$`, the
// server's), refused under four by the client before it asks ("Type more than 4 letters").
// Names are compared without case -- the folder is on a filesystem that does not tell `Knight`
// from `knight`, and neither should the screen.
//
// **A deletion is kept, not erased** -- ours, not MU's. MU's server deletes the rows; here the
// file is moved into `characters/deleted/` with the time on it, because a character somebody
// levelled for a week is one misread prompt away from gone and there is no server-side backup
// to ask for him back.
#pragma once

#include <string>
#include <vector>

#include "game/save.h"
#include "sim/rules.h"

namespace mu::game {

constexpr int kRosterSlots = 5;
constexpr int kNameLetters = 10;

// One character as the screen sees him: enough to stand him on his pedestal and name him.
struct Seat {
    std::string name;
    std::string path;   // his save file
    std::string world;
    int slot = 0;
    sim::Kin kin = sim::Kin::DarkKnight;
    int level = 1;
    bool fresh = false;
    std::vector<Saved::Item> items;  // as read; slots 0 to 6 are what he wears
};

// saves/characters in the client folder, beside the vault and the old hero.json.
std::string rosterFolder();

// Every character in `folder`, by slot. The first time, before the folder exists, an old
// `hero.json` beside it is copied in as slot 0 -- named for his class, since he was made before
// names existed -- so the character somebody has been playing is on the screen the first time
// it opens. The old file is left where it is. Only that first time: a roster emptied by
// deleting every character stays empty (the user, 2026-10-01, "allow me to delete all chars").
std::vector<Seat> readRoster(const std::string& folder);

// Why a name cannot be made, or None. MU's own words for each are the screen's.
enum class Refusal { None, TooShort, Symbols, Taken, NoRoom };
Refusal refusalOf(const std::string& folder, const std::vector<Seat>& roster,
                  const std::string& name);

// A new character of `kin` in the first free slot: a file that says only his name, slot and
// class, marked fresh. False and nothing written when refusalOf says no.
bool makeCharacter(const std::string& folder, const std::vector<Seat>& roster,
                   const std::string& name, sim::Kin kin);

// Moves his file into `folder/deleted/`. False when it could not be moved.
bool dropCharacter(const std::string& folder, const Seat& who);

// What a new character is given to hold: OpenMU's Version075 by way of MU2's Cradle.cs -- a
// Small Axe for the knight and a Short Bow for the elf -- and, OURS, the Skull Staff for the
// wizard, whom 0.75 makes empty-handed. The user's choice, 2026-09-28: a bare-handed swing reads
// as nothing, whether MU's flailing fist or the sword's clip with no sword in it, and with a
// staff his left button is a weapon swing as the knight's is. The staff's rise also lifts his
// Energy Ball its 3%. He is given it short of its strength, as the knight is his axe.
const char* cradleWeapon(sim::Kin kin);

// The class as the name plate says it: MU's texts 20-22.
const char* className(sim::Kin kin);

}  // namespace mu::game
