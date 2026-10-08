// What MU tells you about a thing when the pointer rests on it.
//
// MU2's `Panel.Describe`, which is MuMain's `RenderItemInfo` read against the source: the
// name in the colour of its quality, the damage or the defence at its plus, the attack speed,
// the requirements -- white where met, red where not, and a red "(Lacking n)" under each one
// short -- and "Can be equipped by" per class where not every class may. One function with
// three callers in MU (the bag, the ground, the shop), and so one here.
//
// What comes back is a tip::Sheet rather than a list of lines: the user chose the card layout
// on 2026-09-22, and on 2026-09-27 concept B of the Diablo IV study (`Sheet::item`) -- the fight
// figure as a headline with the sum that made it on a rail, options named first with their value
// last and a mark for their kind, and whom it is for and what it asks in the foot's left column.
// MU's colours are unchanged.
//
// What a row DOES, where no column says it -- the Ale, the Antidote, the Town Portal Scroll
// and the three jewels -- comes off the row as `ItemRow::tells`, and for four of those six the
// sentence is MU's own (GT 572, 573, 574, 157). See section 2.9 of the catalogue below.
//
// Not yet said, because the rules for them do not exist: luck, the additional option, the
// excellent options, what a skill does (the flag is carried and named, nothing more), wear as
// a thing that falls, set and socket blocks. Every one of them has a section waiting for it --
// see docs/mu-tooltip-lines.md for the whole catalogue and where each line goes.
#pragma once

#include "content/tables.h"
#include "game/ui/tip.h"
#include "sim/items.h"
#include "sim/skills.h"

namespace mu::game {

tip::Sheet describe(const content::Tables& tables, const sim::Held& what, const sim::Wearer& who,
                    const sim::Satchel& bag);

// The colour a Zen figure is drawn in: getGoldColor, a pale blue that steps through green and
// blue as the amount grows. Bag.MoneyColour.
uint32_t moneyColour(long long zen);

// **A thing's sockets, as the windows read them**: how many it has, up to three (the user,
// 2026-09-28: "there could be max of 3 socket slots"), and the power set in the one at `at`,
// 0 for empty. The only two places the windows touch the sim's socket fields, so a change in
// how sim::Held keeps them is followed here and nowhere else.
constexpr int kMostSockets = sim::kMostSockets;
// The Rune of Creation's row in these tables, or -1: what a set socket is drawn holding.
int32_t runeRow(const content::Tables& tables);
inline int socketsOf(const sim::Held& held) { return held.sockets; }
inline uint8_t powerAt(const sim::Held& held, int at) {
    return at >= 0 && at < kMostSockets ? held.powers[at] : 0;
}

// **A thing's quality**, the colour its name is drawn in on the card and on the ground: WoW's
// ladder, the user's choice of 2026-09-29, since MU's refinement orange read a +3 as legendary.
// Top down: the Rune of Creation legendary orange, excellent epic purple, socketed rare blue,
// +7 and the jewels artifact gold, luck/skill/option uncommon green, the rest common white.
tip::Tone qualityOf(const content::ItemRow& row, const sim::Held& what);

// **What a spell does, as lines**: the band it rolls in his hands and the sum behind it, then
// whom it strikes -- a channel's length and strikes, a line's sweep, a rain's area -- and a push.
// One function for the spell's own card (`Desk::skillSheet`) and its scroll's (`describe`), so the
// two cannot say different things about one spell. `dim` greys the values, for a scroll he has
// read already.
void spellLines(const sim::SkillRow& row, const sim::Wearer& who, bool dim,
                std::vector<tip::Row>& out);
// **And what a self-cast skill does**: a guard's share and how long, Greater Damage's bonus, Heal's
// health, a summon's level, health and bite as it would stand now (sim::summonFit). The skill's
// card and its orb's, as `spellLines` is. `tables` may be null, which leaves a summon's numbers
// out.
void selfLines(const sim::SkillRow& row, const content::Tables* tables, const sim::Wearer& who,
               bool dim, std::vector<tip::Row>& out);

}  // namespace mu::game
