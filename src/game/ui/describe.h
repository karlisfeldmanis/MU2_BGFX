// What MU tells you about a thing when the pointer rests on it.
//
// MU2's `Panel.Describe`, which is MuMain's `RenderItemInfo` read against the source: the
// name in the colour of its quality, the damage or the defence at its plus, the attack speed,
// the requirements -- white where met, red where not, and a red "(Lacking n)" under each one
// short -- and "Can be equipped by" per class where not every class may. One function with
// three callers in MU (the bag, the ground, the shop), and so one here.
//
// What comes back is a tip::Sheet rather than a list of lines: the user chose the card layout
// on 2026-09-22, so the rows carry a label and their values apart, and the drawing puts one
// on the left and the other on the right. MU's colours are unchanged.
//
// Not yet said, because the rules for them do not exist: luck, the additional option, the
// excellent options, what a skill does (the flag is carried and named, nothing more), wear as
// a thing that falls, set and socket blocks. Every one of them has a section waiting for it --
// see docs/reference/mu-tooltip-lines.md for the whole catalogue and where each line goes.
#pragma once

#include "content/tables.h"
#include "game/ui/tip.h"
#include "sim/items.h"

namespace mu::game {

tip::Sheet describe(const content::Tables& tables, const sim::Held& what, const sim::Wearer& who,
                    const sim::Satchel& bag);

// The colour a Zen figure is drawn in: getGoldColor, a pale blue that steps through green and
// blue as the amount grows. Bag.MoneyColour.
uint32_t moneyColour(long long zen);

}  // namespace mu::game
