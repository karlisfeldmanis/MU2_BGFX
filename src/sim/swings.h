// How often a character may swing, which in MU is **the length of the clip he swings with**.
//
// That sentence is the whole of this file and it is not obvious, so it is worth stating plainly:
// a weapon's `attack_speed` is not a rate and not an interval. The client spends it as
// `PlaySpeed = authored + AttackSpeed * 0.004` on the attack animation, so it makes the clip
// itself run faster, and the swing rate falls out of how long the clip then takes. A port that
// read `attack_speed` as "swings per second" or as "milliseconds between blows" would be
// inventing a number MU does not have -- which is exactly why this sprint carried the field
// unused rather than guessing at it.
//
// The chain, each link traced:
//
//   1. **Which clips he cycles through** -- `SetPlayerAttack` in MuMain's `ZzzCharacter.cpp`,
//      the un-mounted, un-winged branch, which is the whole of it this game can reach. It is a
//      LADDER and not a table, and the order matters: a one-handed axe is caught by the
//      sword-group test three rungs above the axe-shaped one anybody would write, because the
//      first test spans MU's groups 0, 1 and 2 together -- swords, axes AND maces all swing a
//      sword. There is no axe action in the rig.
//   2. **What the character adds to a clip's speed** -- `AttackSpeed1 = AttackSpeed * 0.004f`,
//      `ZzzCharacter.cpp:813`.
//   3. **What his AttackSpeed is** -- `agility x class rate + weapon speed`, the class rate
//      being 1/15 for a Dark Knight, 1/20 for a Dark Wizard and 1/50 for a Fairy Elf
//      (`ClassDarkKnight.cs:58`, `ClassDarkWizard.cs:58`, `ClassFairyElf.cs:63`), and the
//      weapon's own speed halved when there is one in each hand.
//   4. **How long a clip then takes** -- `keys / ((authored speed + bonus) x 25)` seconds, 25
//      being the frame rate MU's play speeds are stated against.
//   5. **And the interval** -- the MEAN over the clips he cycles through, which is this
//      engine's one departure and is marked as such below.
#pragma once

#include <cstdint>

#include "content/tables.h"
#include "sim/rules.h"

namespace mu::sim {

// The actions a pair of hands cycles through, in the client's own order. Returns how many were
// written into `out`, which must hold at least four.
int attackActions(const content::Arm* right, const content::Arm* left, int32_t* out);

// What a character's AttackSpeed stat comes to: his agility at his class's rate, plus what is in
// his hands. Two weapons halve the weapon's contribution before it is added, which is OpenMU's
// `AttackSpeedAny` and the halving MU2 found in the same place.
float attackSpeedStat(Kin kin, int agility, const content::Arm* right, const content::Arm* left);

// Milliseconds between two blows, or 0 when the tables cannot say -- in which case the caller
// keeps whatever placeholder it had rather than being handed an interval invented out of no
// data.
int swingMilliseconds(const content::Tables& tables, Kin kin, int agility,
                      const content::Arm* right, const content::Arm* left);

// And that in ticks, as MU2's realm converts every delay: `max(1, ceil(ms / 50))`. Rounding UP
// rather than to nearest, because a swing the body has not finished is a swing cut short --
// MU2 measured a realm ordering a blow every second while the clip needed 1.097 of one.
int32_t swingTicks(int milliseconds);

}  // namespace mu::sim
