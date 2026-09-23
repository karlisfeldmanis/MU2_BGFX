#include "game/ui/describe.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

#include "sim/skills.h"

namespace mu::game {
namespace {

using tip::Row;
using tip::Section;
using tip::Sheet;
using tip::Tone;
using tip::Value;

// Refined to +7 or beyond turns a name yellow: MU's only rung these rows can reach.
constexpr int kRefinedFrom = 7;

// Refine.StaffRise: half the magic power, and a table chosen by its parity (Weapons.cs:316).
constexpr int kRiseEven[] = {0, 3, 7, 10, 14, 17, 21, 24, 28, 31, 35, 40, 45, 50, 56, 63};
constexpr int kRiseOdd[] = {0, 4, 7, 11, 14, 18, 21, 25, 28, 32, 36, 40, 45, 51, 57, 63};

float staffRise(int magicPower, int refinement) {
    const int at = refinement < 0 ? 0 : (refinement > 15 ? 15 : refinement);
    return float(magicPower) / 2.0f + float(magicPower % 2 == 0 ? kRiseEven[at] : kRiseOdd[at]);
}

std::string label(const content::ItemRow& row, int refinement) {
    return refinement > 0 ? row.label + " +" + std::to_string(refinement) : row.label;
}

std::string decimal(float v) {
    char text[32];
    std::snprintf(text, sizeof text, "%.1f", double(v));
    std::string s = text;
    if (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0) s.resize(s.size() - 2);
    return s;
}

// The line under the name: what the thing is, and whom it is for. MU has no such line -- it
// says the same in its "Can be equipped by" list and in the damage line's own wording -- and
// it is here because the head of a card wants a second, quieter line.
std::string kindOf(const content::ItemRow& row) {
    if (sim::ammunition(row)) return "Ammunition";
    if (row.weapon()) return row.twoHanded() ? "Two-handed weapon" : "One-handed weapon";
    if (row.shield()) return "Shield";
    if (row.jewel()) return "Jewel";
    if (row.group == 15) return "Scroll";
    if (row.group == 12) return "Orb";
    switch (row.group) {
        case sim::kGroupHelms: return "Helm";
        case sim::kGroupArmours: return "Armour";
        case sim::kGroupPants: return "Pants";
        case sim::kGroupGloves: return "Gloves";
        case sim::kGroupBoots: return "Boots";
        case sim::kGroupPotions: return "Consumable";
        default: break;
    }
    return "Item";
}

std::string shouted(std::string s) {
    for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

Row stat(const char* name, const std::string& text, Tone tone) {
    Row row;
    row.label = name;
    row.values.push_back({text, tone, false, "", 0});
    return row;
}

}  // namespace

uint32_t moneyColour(long long zen) {
    if (zen >= 10000000) return gfx::rgba(0.0f, 0.0f, 1.0f);
    if (zen >= 1000000) return gfx::rgba(0.0f, 150.0f / 255.0f, 1.0f);
    if (zen >= 100000) return gfx::rgba(24.0f / 255.0f, 201.0f / 255.0f, 0.0f);
    return gfx::rgba(150.0f / 255.0f, 220.0f / 255.0f, 1.0f);
}

Sheet describe(const content::Tables& tables, const sim::Held& what, const sim::Wearer& who,
               const sim::Satchel& bag) {
    Sheet sheet;
    if (what.empty() || size_t(what.item) >= tables.items.size()) return sheet;
    const content::ItemRow& row = tables.items[size_t(what.item)];
    const int plus = what.refinement;
    sheet.name = label(row, plus);
    // MU's name ladder, as far as these rows reach it: a jewel is yellow, +7 and above is
    // yellow, anything carrying an option is blue, everything else white. Excellent, ancient
    // and socket colours wait for the items that have them.
    sheet.nameTone = row.jewel() ? Tone::Yellow
                     : plus >= kRefinedFrom ? Tone::Yellow
                     : what.skill ? Tone::Blue
                                  : Tone::White;

    static const char* const kNames[3] = {"Dark Wizard", "Fairy Elf", "Dark Knight"};
    int named = 0;
    for (int i = 0; i < 3; ++i) named += (row.classes >> i) & 1;
    std::string base = kindOf(row);
    if (named == 1) {
        for (int i = 0; i < 3; ++i) {
            if ((row.classes >> i) & 1) base += std::string(" \xB7 ") + kNames[i];
        }
    }
    sheet.base = base;

    // ---- what it is worth in a fight --------------------------------------------------------
    // The heading is the kind's own word rather than one heading for everything: a sword is
    // read for its Combat block and a breastplate for its Defense, which is how the game talks
    // about them and how MU's own item tables are grouped.
    Section does;
    const bool defensive = row.armour() || row.shield();
    const bool drinkable = sim::heals(row) || sim::restores(row);
    // A jewel is asked before the potion group because it IS in the potion group: the Bless
    // and the Soul are group 14 numbers 13 and 14, and they are spent, not swallowed.
    const bool consumable = row.group == sim::kGroupPotions && !row.jewel();
    does.kicker = row.weapon() && !sim::ammunition(row) ? "Combat"
                  : defensive                          ? "Defense"
                  : row.jewel()                        ? "Use"
                  : drinkable || consumable            ? "Effect"
                                                       : "Base stats";
    does.mark = defensive ? tip::Mark::Shield
                : drinkable || consumable || row.jewel() ? tip::Mark::Note
                                                         : tip::Mark::Blade;

    const bool weapon = row.weapon() && !sim::ammunition(row);
    const int bonus = weapon ? sim::damageBonus(plus) : 0;
    const Tone lifted = plus > 0 ? Tone::Yellow : Tone::White;
    // MU quotes the damage under the hand it takes -- `Lookup(40 + TwoHand)` -- so the label
    // says which and there is no separate two-handed line.
    const float mine = weapon ? float(row.minimumDamage + row.maximumDamage + 2 * bonus) / 2.0f
                              : 0.0f;
    if (weapon && row.maximumDamage > 0) {
        // The head's second line already says which hand it takes, so the row is just Damage.
        does.rows.push_back(stat("Damage",
                                 std::to_string(row.minimumDamage + bonus) + " ~ " +
                                     std::to_string(row.maximumDamage + bonus),
                                 lifted));
    }
    const bool worn = row.armour() || row.shield();
    const int defense = worn ? row.defense + sim::defenseBonus(row.shield(), plus) : 0;
    if (worn) does.rows.push_back(stat("Armor", std::to_string(defense), lifted));
    if (row.defenseRate > 0) {
        does.rows.push_back(stat("Block rate", std::to_string(row.defenseRate), Tone::White));
    }
    // A staff's magic power, the one line MU prints for it, and the percentage it comes to --
    // MU2's addition, marked there as the project's, because it is what a wizard chooses on.
    float rise = 0.0f;
    if (row.magicPower > 0) {
        rise = staffRise(row.magicPower, plus);
        does.rows.push_back(stat("Magic power", std::to_string(row.magicPower), Tone::White));
        does.rows.push_back(stat("Wizardry damage", "+" + decimal(rise) + "%", lifted));
    }
    if (weapon && row.attackSpeed > 0) {
        does.rows.push_back(stat("Attack speed", std::to_string(row.attackSpeed), Tone::White));
    }
    // The row's own line: what the thing does, for the six rows no column speaks for -- the
    // Ale, the Antidote, the Town Portal Scroll, and the three jewels, whose sentences are
    // MU's own (GT 572, 573, 574, 157). It leads the section, because on every one of those
    // rows it is the section. See ItemRow::tells and docs/mu-tooltip-lines.md section 2.9.
    if (!row.tells.empty()) {
        Row line;
        line.free = row.tells;
        line.freeTone = Tone::White;
        does.rows.push_back(line);
    }
    if (sim::heals(row) || sim::restores(row)) {
        Row line;
        line.free = sim::heals(row) ? "Restores life when it goes down."
                                    : "Restores mana when it goes down.";
        line.freeTone = Tone::White;
        does.rows.push_back(line);
    }
    // A stack says how many, as MU's `Number of items` does; a quiver's shots are its wear and
    // go in the foot with everything else that is spent.
    if (!sim::ammunition(row) && what.durability > 1 && !worn && !weapon) {
        does.rows.push_back(stat("Quantity", std::to_string(what.durability), Tone::Blue));
    }
    if (!does.rows.empty()) sheet.sections.push_back(does);

    // ---- what it teaches ----------------------------------------------------------------------
    // A scroll or an orb is read once and gone, and what it leaves behind is a skill. MU says
    // none of this -- its scroll tooltip is the name, the requirements and the class, and the
    // player is expected to know what a Twister does -- so the name of the skill and a line of
    // what it does are ours, carried in the asset beside the numbers. See content/tables.h.
    if (row.teaches > 0) {
        Section teaches;
        teaches.kicker = "Teaches";
        teaches.mark = tip::Mark::Star;
        // Whether he has read one already, which is the one thing about an orb the card could
        // not say before (the user, 2026-09-23). `Realm::useItem` calls `learn`, `learn` refuses
        // a skill already in the mask, and the refusal is silent like every other down there --
        // so a second orb of Falling Slash was a right-click that did nothing and said nothing.
        // It is asked by INDEX, as `Body::learned` is keyed: `skillIndexOf` turns MU's number
        // into the bit, and -1 (a row that teaches something this build has no skill for) reads
        // as not known, which is the safe way round -- it lets him try.
        const int index = sim::skillIndexOf(row.teaches);
        const bool known = index >= 0 && (who.learned & (uint32_t(1) << index)) != 0;
        if (!row.teachesName.empty()) {
            teaches.rows.push_back(stat("Skill", row.teachesName, known ? Tone::Gray : Tone::Blue));
        }
        if (known) {
            // The words go in the FOOT, opposite the Zen -- the user, 2026-09-23, moving them
            // out of this block where they first landed as a "Known" row. The foot is where
            // they belong: on Hanzo's shelf the price is the other half of the same question,
            // and "Already learned" across from "3,000 Zen" is one glance instead of two.
            sheet.note = "Already learned";
            sheet.noteTone = Tone::Red;
        }
        if (!row.teachesTells.empty()) {
            Row line;
            line.free = row.teachesTells;
            // Dimmed once he knows it: the sentence is still worth having -- it is what the
            // skill DOES, and he may be checking -- but it is no longer an offer.
            line.freeTone = known ? Tone::Gray : Tone::White;
            teaches.rows.push_back(line);
        }
        if (!teaches.rows.empty()) sheet.sections.push_back(teaches);
    }

    // ---- what it carries --------------------------------------------------------------------
    // The options section. The only one of MU's options these rows can carry today is the
    // skill flag, and the fight has no skills to fire; every other option (luck, the additional
    // option, the excellent set, harmony, ancient bonuses) lands here as it arrives.
    Section options;
    options.kicker = "Item options";
    options.mark = tip::Mark::Star;
    if (what.skill) {
        options.rows.push_back(stat("Skill", "carried, unused for now", Tone::Blue));
    }
    if (!options.rows.empty()) sheet.sections.push_back(options);

    // ---- what it asks -----------------------------------------------------------------------
    Section asks;
    asks.kicker = "Requirements";
    asks.mark = tip::Mark::Triangle;
    const sim::Needs asked = sim::asks(row, plus);
    const sim::Needs owed = sim::shortOf(asked, who.level, who.points);
    const auto require = [&](const char* name, int wants, int lacking) {
        if (wants <= 0) return;
        Row line;
        line.label = name;
        // Green when you meet it, red when you do not, the same pair the class chips use: a
        // requirement is a yes or a no, and a column of white numbers makes you read each one to
        // find out which. MU prints them white and turns only the failures red.
        Value value{std::to_string(wants), lacking > 0 ? Tone::Red : Tone::Green, false, "", 0};
        if (lacking > 0) value.text += " (lacking " + std::to_string(lacking) + ")";
        line.values.push_back(value);
        asks.rows.push_back(line);
    };
    // **One level line, whichever of the two says it.** A row can ask for a level twice over: the
    // item's own column, and -- on a scroll or an orb -- the skill it teaches. On the knight's
    // orbs both are set, deliberately and to the same number (the recipes say so, and
    // `Realm::useItem` takes the larger), so printing each in turn read as "Level 13, Level 13"
    // on every orb in the shop. The card prints the larger, once. The user, 2026-09-23.
    const int wantsLevel = std::max(asked.level, row.teaches > 0 ? row.teachesLevel : 0);
    require("Level", wantsLevel, std::max(0, wantsLevel - who.level));
    require("Strength", asked.strength, owed.strength);
    require("Agility", asked.agility, owed.agility);
    require("Vitality", asked.vitality, owed.vitality);
    require("Energy", asked.energy, owed.energy);
    // A scroll's energy is asked by the SKILL, not by the row, and it is not scaled the way a
    // worn thing's requirement is: ItemExtensions.GetRequirement hands back the minimum
    // unchanged for anything without a slot.
    if (row.teaches > 0 && row.teachesEnergy > 0) {
        require("Energy", row.teachesEnergy, std::max(0, row.teachesEnergy - who.points.energy));
    }
    // One chip per class allowed, and none when they all are: mu.db's order, the wizard, the
    // elf, the knight. Shouted, because a chip is a label and not a sentence. Your own class is
    // GREEN -- this one is yours to use -- and any other is red, which is MU's dark-red band
    // made smaller. MU has only the red half of that: it prints every line white and bands the
    // ones you are not.
    if (named > 0 && named < 3) {
        Row line;
        line.label = named > 1 ? "Classes" : "Class";
        for (int i = 0; i < 3; ++i) {
            if (!((row.classes >> i) & 1)) continue;
            line.values.push_back(
                {shouted(kNames[i]), i == int(who.kin) ? Tone::Green : Tone::Red, true, "", 0});
        }
        asks.rows.push_back(line);
    }
    if (!asks.rows.empty()) sheet.sections.push_back(asks);

    // ---- against what he has on ---------------------------------------------------------------
    // MU2's comparison, marked there as the project's: it hangs off the row it belongs to
    // rather than being a sentence of its own, which is the design page's own change.
    const int slot = sim::placeOf(row);
    if (slot >= 0 && !bag[slot].empty() && &bag[slot] != &what && !sheet.sections.empty()) {
        const sim::Held& on = bag[slot];
        const content::ItemRow& theirs = tables.items[size_t(on.item)];
        const bool hisWeapon = theirs.weapon() && !sim::ammunition(theirs);
        const int hisBonus = hisWeapon ? sim::damageBonus(on.refinement) : 0;
        const float hisDamage =
            hisWeapon ? float(theirs.minimumDamage + theirs.maximumDamage + 2 * hisBonus) / 2.0f
                      : 0.0f;
        const float hisDefense =
            (theirs.armour() || theirs.shield())
                ? float(theirs.defense + sim::defenseBonus(theirs.shield(), on.refinement))
                : 0.0f;
        const float hisRise = theirs.magicPower > 0 ? staffRise(theirs.magicPower, on.refinement)
                                                    : 0.0f;
        const auto against = [&](const char* name, float ours, float his, const char* unit) {
            if (ours <= 0.0f && his <= 0.0f) return;
            for (Row& line : sheet.sections[0].rows) {
                if (line.label != name || line.values.empty()) continue;
                const float by = ours - his;
                line.values[0].delta = (by > 0.0f ? "+" : "") + decimal(by) + unit + " vs worn";
                line.values[0].deltaWay = by > 0.0f ? 1 : by < 0.0f ? -1 : 0;
            }
        };
        against("Damage", mine, hisDamage, "");
        against("Armor", float(defense), hisDefense, "");
        against("Wizardry damage", rise, hisRise, "%");
    }

    // ---- the foot -----------------------------------------------------------------------------
    // Ammunition's durability is its shots, and MU draws it the same way.
    if (sim::ammunition(row) && row.durability > 0) {
        sheet.wear = std::to_string(what.durability) + " / " + std::to_string(row.durability);
        sheet.worn = float(what.durability) / float(row.durability);
    }
    return sheet;
}

}  // namespace mu::game
