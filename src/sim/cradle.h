#pragma once

// A new character's outfitting once his realm is raised: the points his starting weapon asks
// spent for it, and the weapon and shield put in his hands. Here, in the rules, because the
// client's mirror and the server must do exactly the same to the same realm before the first
// tick (docs/sprints/18-the-wire.md); it was Play::open's until 2026-10-07.

#include <cstdint>
#include <string>

#include "sim/rules.h"

namespace mu::sim {

class Realm;

// What a new character is: level 1, his class's weapon in his hand, at the spawn gate of his
// class's town. The rules' and the server's, never the client's to say (a Hello names only his
// class). The weapon by arm name: the Skull Staff, the Short Bow, the Small Axe, the Short Sword.
constexpr int kNewLevel = 1;
const char* cradleWeapon(Kin kin);
// The elf is born in Noria, every other class in Lorencia (the user's, game/roster.cpp).
const char* homeWorld(Kin kin);

// The account's rules for making one, the server's and the character screen's alike (server-plan
// phase 6): five characters an account, and a name of letters and digits, four to ten of them --
// OpenMU's `^[a-zA-Z0-9]{3,10}$` with MuMain's "Type more than 4 letters" -- unique on the server
// without regard to case. The Magic Gladiator only once a character of the account has reached
// kGladiatorLevel: OpenMU's LevelRequirementByCreation (ClassMagicGladiator.cs:36).
constexpr int kRosterSlots = 5;
constexpr int kNameFewest = 4, kNameMost = 10;
constexpr int kGladiatorLevel = 220;
// Letters and digits only, kNameFewest to kNameMost of them.
bool goodName(const std::string& name);

// The weapon and shield by their arm names (Tables::armNamed), either empty for none. A
// character made above level 1 arrives with his points in hand, and unspent he cannot lift what
// he is given: the courtesy the headless hand does too, paying only for what he holds. Refused on
// strength or agility, it is given anyway -- what he starts holding is the cradle's gift (see
// Realm::equip). Logged; false only for a name no arm has.
bool outfit(Realm& realm, const std::string& weapon, const std::string& shield);
// The same by arm index (Tables::arms), -1 for none: what a Join command carries.
bool outfitArms(Realm& realm, int32_t weapon, int32_t shield);

}  // namespace mu::sim
