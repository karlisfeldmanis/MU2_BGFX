// The cards of the debuffs that stack on him -- the Golden Dragon's Dragonfire and a bleeder's
// bleed -- each saying its stacks and what they do, from the realm's own constants.
// Dragonfire's out of hud.cpp's Hud::boonSheet on 2026-10-12, unchanged, to pay the size ratchet
// for the bleed's.
#include "game/ui/hud.h"

#include <cmath>
#include <string>

#include "sim/raid.h"
#include "sim/realm_tuning.h"

namespace mu::game {
namespace {

tip::Row said(const std::string& label, const std::string& value, tip::Tone tone) {
    tip::Row one;
    one.label = label;
    one.values.push_back({value, tone, false, "", 0});
    return one;
}

tip::Row prose(const std::string& text, tip::Tone tone = tip::Tone::Gray) {
    tip::Row one;
    one.free = text;
    one.freeTone = tone;
    return one;
}

}  // namespace

bool debuffCard(const Hud::Boon& one, tip::Sheet& sheet) {
    if (one.cut) {
        // A Magic Gladiator's Fire Slash (sim/skills.cpp): MU's BUFFTYPE_DEFENSE_POWER_DEC.
        const sim::SkillRow* row = sim::skillNumbered(sim::skill::kFireSlash);
        sheet.name = "Fire Slash";
        sheet.nameTone = tip::Tone::Red;
        sheet.base = "DEBUFF";
        tip::Section what;
        what.rows.push_back(said("Defence", "-" + std::to_string(row ? row->cutShare : 10) + "%",
                                 tip::Tone::Red));
        what.rows.push_back(prose("Every blow on it meets less defence. A second cut starts its "
                                  "seconds again and goes no deeper."));
        sheet.sections.push_back(what);
        return true;
    }
    if (one.dragonfire) {
        // The Golden Dragon's (sim/raid.h): each stack its fire bites deeper, and fire resistance
        // takes the edge off both (the user, 2026-10-08: "players need some fire resistance items").
        sheet.name = "Dragonfire";
        sheet.nameTone = tip::Tone::Red;
        sheet.base = "DEBUFF";
        tip::Section what;
        what.rows.push_back(said("Stacks",
                                 std::to_string(one.stacks) + " of " +
                                     std::to_string(sim::kDragonfireMost),
                                 tip::Tone::Red));
        what.rows.push_back(said("Fire damage taken", "+" + std::to_string(int(std::lround(
                                     double(one.stacks) * sim::kDragonfireTaken * 100.0))) + "%",
                                 tip::Tone::Red));
        what.rows.push_back(prose("Every fire blow of the dragon's adds a stack. Fire resistance "
                                  "lessens its fire and the stacks alike."));
        sheet.sections.push_back(what);
        return true;
    }
    if (one.bleed) {
        // A bleeder's claws (sim kBleeders, the Forest and Bloody Orcs'): a stack each blow that
        // lands, each a share of that blow a second.
        sheet.name = "Bleeding";
        sheet.nameTone = tip::Tone::Red;
        sheet.base = "DEBUFF";
        tip::Section what;
        what.rows.push_back(said("Stacks",
                                 std::to_string(one.stacks) + " of " +
                                     std::to_string(sim::kBleedMost),
                                 tip::Tone::Red));
        // A monster a bleed rune cut (sim::BleedRune): what its stacks take, as a figure.
        if (one.bite > 0) {
            what.rows.push_back(said("Each second", std::to_string(one.bite) + " damage",
                                     tip::Tone::Red));
            what.rows.push_back(prose("Every cut that lands adds a stack and starts its "
                                      "seconds again."));
            sheet.sections.push_back(what);
            return true;
        }
        what.rows.push_back(said("Each second", std::to_string(int(std::lround(
                                     double(one.stacks) * sim::kBleedShare * 100.0))) +
                                     "% of the last cut",
                                 tip::Tone::Red));
        what.rows.push_back(prose("Every claw that lands adds a stack and starts its "
                                  "seconds again. It never takes your last point of life."));
        sheet.sections.push_back(what);
        return true;
    }
    return false;
}

}  // namespace mu::game
