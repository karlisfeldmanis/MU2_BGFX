#pragma once

// A new character's outfitting once his realm is raised: the points his starting weapon asks
// spent for it, and the weapon and shield put in his hands. Here, in the rules, because the
// client's mirror and the server must do exactly the same to the same realm before the first
// tick (docs/sprints/18-the-wire.md); it was Play::open's until 2026-10-07.

#include <cstdint>
#include <string>

namespace mu::sim {

class Realm;

// The weapon and shield by their arm names (Tables::armNamed), either empty for none. A
// character made above level 1 arrives with his points in hand, and unspent he cannot lift what
// he is given: the courtesy the headless hand does too, paying only for what he holds. Refused on
// strength or agility, it is given anyway -- what he starts holding is the cradle's gift (see
// Realm::equip). Logged; false only for a name no arm has.
bool outfit(Realm& realm, const std::string& weapon, const std::string& shield);
// The same by arm index (Tables::arms), -1 for none: what a Join command carries.
bool outfitArms(Realm& realm, int32_t weapon, int32_t shield);

}  // namespace mu::sim
