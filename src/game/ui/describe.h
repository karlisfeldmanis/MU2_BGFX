// What MU tells you about a thing when the pointer rests on it.
//
// MU2's `Panel.Describe`, which is MuMain's `RenderItemInfo` read against the source: the
// name in the colour of its quality, the damage or the defence at its plus, the attack speed,
// the requirements -- white where met, red where not, and a red "(Lacking n)" under each one
// short -- and "Can be equipped by" per class where not every class may. One function with
// three callers in MU (the bag, the ground, the shop), and so one here.
//
// Not yet said, and why: luck and options (nothing carries them yet), the skill a piece grants
// (no skills), what a scroll teaches (no skills to name), durability as wear (nothing wears).
// The staff's two lines and MU2's comparison with the piece already worn are kept.
#pragma once

#include <vector>

#include "content/tables.h"
#include "game/ui/panel.h"
#include "sim/items.h"

namespace mu::game {

std::vector<panel::Line> describe(const content::Tables& tables, const sim::Held& what,
                                  const sim::Wearer& who, const sim::Satchel& bag);

// The colour a Zen figure is drawn in: getGoldColor, a pale blue that steps through green and
// blue as the amount grows. Bag.MoneyColour.
uint32_t moneyColour(long long zen);

}  // namespace mu::game
