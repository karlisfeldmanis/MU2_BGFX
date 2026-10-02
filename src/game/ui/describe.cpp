#include "game/ui/describe.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <string>

#include "sim/rules.h"
#include "sim/skills.h"
#include "sim/wear.h"

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
    // Before the pets: the rings and pendants share their group 13 (docs/jewellery.md).
    if (sim::ring(row)) return "Ring";
    if (sim::pendant(row)) return "Pendant";
    // Before the jewel: the pets ride in the jewel drop group and are not jewels.
    if (row.group == sim::kGroupPets) return "Pet";
    if (row.jewel()) return "Jewel";
    // In the potions' group as MU files it (14, 22), and not drunk: set in a socket.
    if (sim::creation(row)) return "Epic jewel";
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

// What the fight reckons a piece at, for the comparison: `Realm::redress`'s sums, unworn. A
// weapon's band, as its middle, is the plus's rise, the additional option on both ends (not a
// staff's: that is wizardry damage, which nothing reckons yet) and the excellent bonus. A piece
// of armour's defence is the plus, the option (not a shield's: that goes to its block rate) and
// the excellent bonus.
float swingOf(const content::ItemRow& row, const sim::Held& held) {
    if (!row.weapon() || sim::ammunition(row) || row.maximumDamage <= 0) return 0.0f;
    int bonus = sim::damageBonus(held.refinement);
    if (row.magicPower == 0) bonus += sim::optionValue(row, held.option);
    if (held.excellent != 0) bonus += sim::excellentDamage(row);
    return float(row.minimumDamage + row.maximumDamage + 2 * bonus) / 2.0f;
}

int armourOf(const content::ItemRow& row, const sim::Held& held) {
    if (!row.armour() && !row.shield()) return 0;
    return row.defense + sim::defenseBonus(row.shield(), held.refinement) +
           (row.shield() ? 0 : sim::optionValue(row, held.option)) +
           (held.excellent != 0 ? sim::excellentDefense(row) : 0);
}

Row stat(const char* name, const std::string& text, Tone tone) {
    Row row;
    row.label = name;
    row.values.push_back({text, tone, false, "", 0});
    return row;
}

}  // namespace

void spellLines(const sim::SkillRow& row, const sim::Wearer& who, bool dim,
                std::vector<Row>& out) {
    const auto tone = [&](Tone lit) { return dim ? Tone::Gray : lit; };
    // A blink strikes nothing: where it goes is the whole of it.
    if (row.blinks) {
        out.push_back(stat("Range", std::to_string(int(row.reach)) + " tiles", tone(Tone::White)));
        Row where;
        where.free = "to the ground under the pointer";
        where.freeTone = Tone::Gray;
        out.push_back(where);
        return;
    }
    const auto note = [&](const std::string& text) {
        Row one;
        one.free = text;
        one.freeTone = Tone::Gray;
        out.push_back(one);
    };
    // The band it rolls in, `sim::cast`'s own two lines: energy over nine and over four, the
    // spell's damage on the bottom and half again on the top, times the staff and the spell's own
    // multiplier (one but on Lightning, Ice and Poison's half again and Meteorite's twice).
    const double times = double(sim::force(row, who.points));
    const int low = int((who.wizardMinimum + double(row.damage)) * who.wizardryRate * times);
    const int high = int((who.wizardMaximum + double(row.damage + row.damage / 2)) *
                         who.wizardryRate * times);
    // A channel's damage is each strike's and a rain's each rock's: the band is not the cast's.
    const char* label = row.channelled() || row.burns > 0         ? "Each strike"
                        : row.splash > 0.0f && row.fallTicks > 0 ? "Each rock"
                        : row.splash > 0.0f                      ? "Each body"
                                                                 : "Damage";
    out.push_back(stat(label, std::to_string(low) + " - " + std::to_string(high),
                       tone(Tone::Yellow)));
    char sum[64];
    if (who.staffRise > 0.0f) {
        std::snprintf(sum, sizeof(sum), "%d ene, staff +%d%%", who.points.energy,
                      int(who.staffRise + 0.5f));
    } else {
        std::snprintf(sum, sizeof(sum), "%d ene, no staff", who.points.energy);
    }
    note(sum);
    // **Whom it strikes** (the user, 2026-09-28: "update tooltip for this spell, because it's
    // multiple monsters and is channeling").
    const std::string reach = std::to_string(int(row.reach)) + " tiles";
    if (row.channelled()) {
        char lasts[32];
        std::snprintf(lasts, sizeof(lasts), "%.1f s", double(row.channelTicks) * 0.05);
        out.push_back(stat("Channel", lasts, tone(Tone::White)));
        const int strikes =
            row.pulseTicks > 0 ? (row.strikeUntil - row.strikeFrom) / row.pulseTicks + 1 : 1;
        out.push_back(stat("Strikes", "up to " + std::to_string(strikes), tone(Tone::White)));
        out.push_back(stat("Area", reach + " round him", tone(Tone::White)));
        note(row.strikesEach == 1   ? "going round, once each"
             : row.strikesEach > 1 ? "one body at a time, going round, " +
                                         std::to_string(row.strikesEach) + " times each at most"
                                   : "one body at a time, going round");
    } else if (row.spread == sim::Spread::Line) {
        out.push_back(stat("Range", reach, tone(Tone::White)));
        out.push_back(stat("Area", "a line of " + std::to_string(int(sim::kLineTiles)) + " tiles",
                           tone(Tone::White)));
        note("strikes everything it passes through");
    } else if (row.burns > 0) {
        // Flame: a fire on the ground, `Realm::burn`.
        char round[32], strikes[48];
        std::snprintf(round, sizeof(round), "%.1f tiles round its target", double(row.burnTiles));
        std::snprintf(strikes, sizeof(strikes), "%d, %.1f s apart", row.burns,
                      double(sim::kBurnEvery) * 0.05);
        out.push_back(stat("Range", reach, tone(Tone::White)));
        out.push_back(stat("Area", round, tone(Tone::White)));
        out.push_back(stat("Strikes", strikes, tone(Tone::White)));
        note("the fire burns on the ground; whoever stands in it is struck");
    } else if (row.splash > 0.0f) {
        out.push_back(stat("Range", reach, tone(Tone::White)));
        out.push_back(stat("Area", std::to_string(int(row.splash)) + " tiles round its target",
                           tone(Tone::White)));
        note(row.fallTicks > 0 ? "a rock falls on each body in it" : "it bursts on each body in it");
    } else {
        out.push_back(stat("Range", reach, tone(Tone::White)));
    }
    if (row.pushes) out.push_back(stat("Pushes", "a tile away", tone(Tone::Green)));
    if (row.chillTicks > 0) {
        char slow[48];
        std::snprintf(slow, sizeof(slow), "to half for %d s", row.chillTicks / 20);
        out.push_back(stat("Slows", slow, tone(Tone::Green)));
    }
    if (row.poisonTicks > 0) {
        char lasts[48];
        std::snprintf(lasts, sizeof(lasts), "for %d s", row.poisonTicks / 20);
        out.push_back(stat("Poisons", lasts, tone(Tone::Green)));
        char pulse[64];
        std::snprintf(pulse, sizeof(pulse), "a quarter of the blow every %d s", sim::kPoisonEvery / 20);
        Row how;
        how.free = pulse;
        how.freeTone = Tone::Gray;
        out.push_back(how);
    }
}

uint32_t moneyColour(long long zen) {
    if (zen >= 10000000) return gfx::rgba(0.0f, 0.0f, 1.0f);
    if (zen >= 1000000) return gfx::rgba(0.0f, 150.0f / 255.0f, 1.0f);
    if (zen >= 100000) return gfx::rgba(24.0f / 255.0f, 201.0f / 255.0f, 0.0f);
    return gfx::rgba(150.0f / 255.0f, 220.0f / 255.0f, 1.0f);
}

tip::Tone qualityOf(const content::ItemRow& row, const sim::Held& what) {
    // MU's rungs kept in its order (excellent over a socket over +7 over an option, the socket
    // ours from 2026-09-28: "item drop with +socket is rare"), each in WoW's colour for its tier.
    if (sim::creation(row)) return Tone::Legendary;
    if (what.excellent != 0) return Tone::Epic;
    if (socketsOf(what) > 0) return Tone::Rare;
    if ((row.jewel() && row.group != sim::kGroupPets) || what.refinement >= kRefinedFrom) {
        return Tone::Artifact;
    }
    if (what.skill || what.luck || what.option > 0) return Tone::Uncommon;
    return Tone::White;
}

int32_t runeRow(const content::Tables& tables) {
    for (size_t i = 0; i < tables.items.size(); ++i) {
        if (sim::creation(tables.items[i])) return int32_t(i);
    }
    return -1;
}

Sheet describe(const content::Tables& tables, const sim::Held& what, const sim::Wearer& who,
               const sim::Satchel& bag) {
    Sheet sheet;
    if (what.empty() || size_t(what.item) >= tables.items.size()) return sheet;
    // As read at its plus: the Orb of Summoning is six orbs by its plus (sim::asRead).
    const content::ItemRow row = sim::asRead(tables.items[size_t(what.item)], what.refinement);
    const int plus = what.refinement;
    sheet.item = true;
    // A third of the square a bag cell of its longer side, so a three-cell sword or a two-by-
    // three armour fills it and a one-cell jewel stands at 40 px, about its own cell's size.
    sheet.artScale = std::clamp(float(std::max(row.width, row.height)) / 3.0f, 0.42f, 1.0f);
    // A summoning orb's plus is already in its name.
    sheet.name = sim::summoningOrb(row) ? row.label : label(row, plus);
    sheet.nameTone = qualityOf(row, what);

    static const char* const kNames[3] = {"Dark Wizard", "Fairy Elf", "Dark Knight"};
    int named = 0;
    for (int i = 0; i < 3; ++i) named += (row.classes >> i) & 1;
    // The type line, in the name's tone. GetItemName puts `Excellent ` before the name
    // (ZZ:2645-2671); the card moves it here, where Diablo's "Legendary Bow" stands, so the name
    // keeps to one line -- the user's choice of 2026-09-27. Whom it is for is in the foot.
    std::string base = kindOf(row);
    if (what.excellent != 0) {
        base[0] = char(std::tolower(static_cast<unsigned char>(base[0])));
        base = "Excellent " + base;
    }
    sheet.base = base;

    // ---- what it is worth in a fight --------------------------------------------------------
    // The one figure a thing is compared by goes to the card's headline (`tip::Hero`); what is
    // left of this block -- a staff's power, a potion's sentence, a stack's count -- stays a
    // section under it.
    Section does;

    const bool weapon = row.weapon() && !sim::ammunition(row);
    // The plus's rise, and being excellent on top (sim::excellentDamage) -- MU prints the band
    // with both in, in blue on an excellent thing.
    const int bonus = weapon ? sim::damageBonus(plus) +
                                   (what.excellent != 0 ? sim::excellentDamage(row) : 0)
                             : 0;
    // The damage and defence lines are blue on an excellent thing (ZI:3972-4064).
    const Tone lifted = plus > 0 ? Tone::Yellow : what.excellent != 0 ? Tone::Blue : Tone::White;
    // A rail row: its figure first, in its tone, and then its words.
    const auto rail = [&](const std::string& figure, Tone tone, const std::string& words) {
        Row line;
        line.label = words;
        line.values.push_back({figure, tone, false, "", 0});
        sheet.hero.rail.push_back(line);
    };
    const auto range = [](int low, int high) {
        return std::to_string(low) + " ~ " + std::to_string(high);
    };
    if (weapon && row.maximumDamage > 0) {
        // The head's second line already says which hand it takes, so the word is just Damage.
        // The band as MU prints it, and under it on the rail the parts it is the sum of -- the
        // base only where something was added to it.
        sheet.hero.value = range(row.minimumDamage + bonus, row.maximumDamage + bonus);
        sheet.hero.tone = lifted;
        sheet.hero.word = "Damage";
        if (bonus > 0) rail(range(row.minimumDamage, row.maximumDamage), Tone::White, "base");
        if (plus > 0) {
            rail("+" + std::to_string(sim::damageBonus(plus)), Tone::Yellow,
                 "refined to +" + std::to_string(plus));
        }
        if (what.excellent != 0) {
            rail("+" + std::to_string(sim::excellentDamage(row)), Tone::Green, "excellent");
        }
        if (row.attackSpeed > 0) rail(std::to_string(row.attackSpeed), Tone::White, "attack speed");
    }
    // A knight's second weapon (sim::offHanded), and what it pairs to: the weapon in his other
    // hand -- the left's partner is the right, anything else's is the right. Two of one kind hit
    // whole, two kinds at sim::kMixedPair each.
    if (weapon && sim::offHanded(row, who.kin)) {
        static const char* const kKinds[4] = {"sword", "axe", "mace", "spear"};
        static const char* const kPairs[4] = {"swords", "axes", "maces", "spears"};
        const sim::Held& partner = &what == &bag[sim::kWeaponRight] ? bag[sim::kWeaponLeft]
                                                                    : bag[sim::kWeaponRight];
        const content::ItemRow* other =
            partner.empty() || size_t(partner.item) >= tables.items.size()
                ? nullptr
                : &tables.items[size_t(partner.item)];
        const std::string share = std::to_string(int(std::lround(sim::kMixedPair * 100.0))) + "%";
        // In the affix grammar: "Dual Wield" named first, the value last and heavier. Green for
        // the pair that hits whole, red for the one cut, white for the rule when he holds none.
        const auto say = [&](const std::string& words, const std::string& value, Tone tone) {
            Row line;
            line.keyword = "Dual Wield";
            line.free = words;
            line.tail = value;
            line.freeTone = tone;
            line.mark = tip::Mark::Diamond;
            line.markTone = tone;
            does.rows.push_back(line);
        };
        const std::string mine = kKinds[row.group];
        if (other && sim::offHanded(*other, who.kin) && other->group == row.group) {
            say(std::string("two ") + kPairs[row.group] + ", each hand deals", "100%",
                Tone::Green);
        } else if (other && sim::offHanded(*other, who.kin)) {
            say(mine + " and " + kKinds[other->group] + ", each hand deals", share, Tone::Red);
        } else {
            say("with another " + mine + ", each hand deals", "100%", Tone::White);
            say("with another weapon type, each hand deals", share, Tone::Gray);
        }
    }
    const bool worn = row.armour() || row.shield();
    const int defense = worn ? row.defense + sim::defenseBonus(row.shield(), plus) +
                                   (what.excellent != 0 ? sim::excellentDefense(row) : 0)
                             : 0;
    if (worn) {
        sheet.hero.value = std::to_string(defense);
        sheet.hero.tone = lifted;
        sheet.hero.word = "Armor";
        const int refined = sim::defenseBonus(row.shield(), plus);
        const int excellent = what.excellent != 0 ? sim::excellentDefense(row) : 0;
        if (refined > 0 || excellent > 0) rail(std::to_string(row.defense), Tone::White, "base");
        if (refined > 0) {
            rail("+" + std::to_string(refined), Tone::Yellow, "refined to +" + std::to_string(plus));
        }
        if (excellent > 0) rail("+" + std::to_string(excellent), Tone::Green, "excellent");
    }
    if (row.defenseRate > 0) {
        const int block =
            row.defenseRate + (what.excellent != 0 ? sim::excellentBlock(row) : 0);
        does.rows.push_back(stat("Block rate", std::to_string(block),
                                 what.excellent != 0 ? Tone::Blue : Tone::White));
    }
    // A staff's magic power, the one line MU prints for it, and the percentage it comes to --
    // MU2's addition, marked there as the project's, because it is what a wizard chooses on.
    float rise = 0.0f;
    if (row.magicPower > 0) {
        rise = staffRise(row.magicPower, plus);
        does.rows.push_back(stat("Magic power", std::to_string(row.magicPower), Tone::White));
        does.rows.push_back(stat("Wizardry damage", "+" + decimal(rise) + "%", lifted));
    }
    // The row's own line: what the thing does, for the six rows no column speaks for -- the
    // Ale, the Antidote, the Town Portal Scroll, and the three jewels, whose sentences are
    // MU's own (GT 572, 573, 574, 157). It leads the section, because on every one of those
    // rows it is the section. See ItemRow::tells and docs/mu-tooltip-lines.md section 2.9.
    if (!row.tells.empty()) {
        Row line;
        line.free = row.tells;
        line.freeTone = Tone::White;
        line.mark = tip::Mark::Diamond;
        does.rows.push_back(line);
    }
    // A pet's powers in MU's own words, which ZzzInventory prints for ITEM_GUARDIAN_ANGEL and
    // ITEM_IMP (ZzzInventory.cpp:4258-4268; Game.en.resx "Absorb %d%% of Damage", "Max HP +%d
    // increased", "Increase 30%% of attacking & Wizardry Dmg"), with the numbers off the rule the
    // realm fights by (sim::petPower), so the card and the blow cannot disagree.
    // A ring's or a pendant's resistance, 1 a plus in its element (docs/jewellery.md); MuMain's
    // card says it as "Ice Resistance +%d" and the rest alike.
    if (sim::jewellery(row)) {
        static const char* const kElement[] = {"", "Ice resistance", "Poison resistance",
                                               "Lightning resistance", "Fire resistance"};
        const int resists = sim::resistanceOf(row, what.refinement);
        does.rows.push_back(stat(kElement[int(sim::elementOf(row))], "+" + std::to_string(resists),
                                 resists > 0 ? Tone::White : Tone::Gray));
    }
    if (row.group == sim::kGroupPets && !sim::jewellery(row)) {
        const sim::PetPower power = sim::petPower(row);
        const auto say = [&](const std::string& words) {
            Row line;
            line.free = words;
            line.freeTone = Tone::White;
            line.mark = tip::Mark::Diamond;
            does.rows.push_back(line);
        };
        if (power.taken != 1.0) {
            say("Absorb " + std::to_string(int(std::lround((1.0 - power.taken) * 100.0))) +
                "% of Damage");
        }
        if (power.health > 0) say("Max HP +" + std::to_string(power.health) + " increased");
        // The Horn of Uniria. MuMain's card has only its Life; "Moving speed" is GT 68, the line
        // later mounts print (ZzzInventory.cpp:4081). Ours, so the card says what the horn is for.
        if (power.mount) say("Moving speed: ride outside town, faster than running");
        if (power.dealt > 1.0) {
            say("Increase " + std::to_string(int(std::lround((power.dealt - 1.0) * 100.0))) +
                "% of attacking & Wizardry Dmg");
        }
        // The Angel's price (ours), in red beside the Imp's.
        if (power.dealt < 1.0) {
            Row line;
            line.free = "Decrease " + std::to_string(int(std::lround((1.0 - power.dealt) * 100.0))) +
                        "% of attacking & Wizardry Dmg";
            line.freeTone = Tone::Red;
            line.mark = tip::Mark::Diamond;
            does.rows.push_back(line);
        }
        // The Imp's price, which MuMain's card never printed and WebZen's server always took
        // (sim::PetPower::lifeCost). Ours, worded as muonlinefanz's item page has it, and red
        // because it is a cost.
        if (power.lifeCost > 0) {
            Row line;
            line.free = "Life -" + std::to_string(power.lifeCost) + " for each successful attack";
            line.freeTone = Tone::Red;
            line.mark = tip::Mark::Diamond;
            does.rows.push_back(line);
        }
    }
    if (sim::heals(row) || sim::restores(row)) {
        Row line;
        line.free = sim::heals(row) ? "Restores life when it goes down."
                                    : "Restores mana when it goes down.";
        line.freeTone = Tone::White;
        line.mark = tip::Mark::Diamond;
        does.rows.push_back(line);
    }
    // A stack says how many, as MU's `Number of items` does; a quiver's shots are its wear and
    // go in the foot with everything else that is spent.
    if (!sim::ammunition(row) && what.durability > 1 && !worn && !weapon &&
        row.group != sim::kGroupPets) {
        does.rows.push_back(stat("Quantity", std::to_string(what.durability), Tone::Blue));
    }
    if (!does.rows.empty()) sheet.sections.push_back(does);

    // ---- its options ----------------------------------------------------------------------------
    // GetSpecialOptionText's lines in SetItemAttributes' order -- luck, then the additional
    // option -- every one TEXT_COLOR_BLUE, and luck's two lines as ZzzInventory.cpp:5162-5172
    // prints them. MU's own English (GT 87, 94, 88-91), docs/mu-tooltip-lines.md section 3(d).
    //
    // Set in the card's affix grammar (2026-09-27): what the option does first, its value last
    // and heavier, and a mark in the gutter that says which kind it is -- a blue diamond for
    // what a drop rolls, a green star for an excellent option. The text stays MU's blue. Luck's
    // two lines become "Luck" as a keyword on each, which is the only change to MU's wording.
    const bool carriesSkill = what.skill && row.skill > 0;
    if ((sim::takesOptions(row) || sim::jewellery(row)) &&
        (what.luck || what.option > 0 || what.excellent != 0 || carriesSkill)) {
        Section options;
        // MU's line split at its last word where that word is the value: "Increase Max HP +4%"
        // is the prose "Increase Max HP" and the value "+4%". A line whose last word is not a
        // value -- "+life/8" is one, "+Mana/8" another -- keeps it as its value all the same.
        const auto option = [&](const std::string& keyword, const std::string& text,
                                tip::Mark mark, Tone markTone) {
            Row line;
            line.keyword = keyword;
            const size_t space = text.rfind(' ');
            if (space != std::string::npos && text[space + 1] == '+') {
                line.free = text.substr(0, space);
                line.tail = text.substr(space + 1);
            } else {
                line.free = text;
            }
            line.freeTone = Tone::Blue;
            line.mark = mark;
            line.markTone = markTone;
            options.rows.push_back(line);
        };
        const auto rolled = [&](const std::string& keyword, const std::string& text) {
            option(keyword, text, tip::Mark::Diamond, Tone::Blue);
        };
        // The weapon's own skill, first, as MU prints it above the options.
        if (carriesSkill) {
            const sim::SkillRow* skill = sim::skillNumbered(row.skill);
            rolled("Skill", skill && skill->name[0] ? skill->name : "carried");
        }
        if (what.luck) {
            rolled("Luck", "Critical damage rate +5%");
            rolled("Luck", "Jewel of Soul success rate +25%");
        }
        if (what.option > 0) {
            const std::string value = std::to_string(sim::optionValue(row, what.option));
            // A ring's and a pendant's is AT_LIFE_REGENERATION, MuMain's "Automatic HP recovery".
            if (sim::jewellery(row)) rolled("", "Automatic HP recovery +" + value + "%");
            else if (row.shield()) rolled("", "Additional defense rate +" + value);
            else if (row.armour()) rolled("", "Additional defense +" + value);
            else if (row.magicPower > 0) rolled("", "Additional Wizardry Dmg +" + value);
            else rolled("", "Additional Dmg +" + value);
        }
        // And the excellent ones last, in their bit order, as SetItemAttributes adds them.
        for (int bit = 0; bit < sim::kExcellentOptions; ++bit) {
            if (what.excellent & (1u << bit)) {
                option("", sim::excellentLine(row, bit), tip::Mark::Star, Tone::Green);
            }
        }
        sheet.sections.push_back(options);
    }

    // ---- its socket --------------------------------------------------------------------------
    // Ours, not MU's: a weapon or a piece of armour may roll up to three sockets as it rolls
    // luck, and a Rune of Creation is set in it (sim/items.h). Drawn as Diablo III
    // draws its sockets, on the user's reference of 2026-09-28: no frame, a row a socket under
    // the options, a round bronze ring in the gutter and "Empty Socket" in the options' blue --
    // or, set, the stone in the ring and its power in MU's orange, the jewel's own name colour.
    if (const int sockets = std::min(socketsOf(what), kMostSockets); sockets > 0) {
        Section socket;
        for (int at = 0; at < sockets; ++at) {
            Row line;
            if (const sim::PowerRow* power = sim::powerOf(powerAt(what, at))) {
                sheet.rune = runeRow(tables);
                line.keyword = power->name;
                line.free = power->tells ? power->tells : "";
                line.freeTone = Tone::Orange;
                line.mark = tip::Mark::RingSet;
                line.markTone = Tone::Orange;
                socket.rows.push_back(line);
                // What the rune strikes for in his hands with THIS weapon (the user, 2026-10-01:
                // "show damage for dps runes based on base dmg and scaling"): the swing's band as
                // `sim::reckon` makes it off his strength and agility and this weapon, its share,
                // and his energy's band on top (sim/items.h, kRuneEnergyLow/High).
                const bool storm = power->power == sim::Power::Stormcall;
                const bool arrow = power->power == sim::Power::Frost;
                // The knight's Ice wounds as Frost Arrow does, off his swing.
                const bool frost = arrow || power->power == sim::Power::Ice;
                if ((storm || frost) && weapon) {
                    sim::Arms arms;
                    arms.weaponMinimumDamage = row.minimumDamage + bonus;
                    arms.weaponMaximumDamage = row.maximumDamage + bonus;
                    const int32_t arm = tables.armNamed(row.name);
                    arms.archery = arm >= 0 && size_t(arm) < tables.arms.size() &&
                                   tables.arms[size_t(arm)].missile();
                    sim::Fighter swing;
                    int health = 0;
                    sim::reckon(who.kin, who.level, who.points, arms, &swing, &health);
                    const float share = storm ? sim::kStormcallForce : sim::kFrostWound;
                    const int energy = who.points.energy;
                    const int eLow = int(energy * sim::kRuneEnergyLow);
                    const int eHigh = int(energy * sim::kRuneEnergyHigh);
                    const int low = std::max(1, int(float(swing.minimumDamage) * share)) + eLow;
                    const int high = std::max(1, int(float(swing.maximumDamage) * share)) + eHigh;
                    socket.rows.push_back(stat(storm ? "Lightning" : "Frost wound",
                                               std::to_string(low) + " ~ " + std::to_string(high),
                                               Tone::Yellow));
                    char sum[96];
                    std::snprintf(sum, sizeof(sum), "%s %d ~ %d, +%d ~ %d from %d ene",
                                  storm ? "his swing" : arrow ? "half the arrow" : "half his swing",
                                  int(float(swing.minimumDamage) * share),
                                  int(float(swing.maximumDamage) * share), eLow, eHigh, energy);
                    Row how;
                    how.free = sum;
                    how.freeTone = Tone::Gray;
                    socket.rows.push_back(how);
                }
                // Evil Spirit's blow as Realm::spiritStrike rolls it: the spell's 45 through
                // `sim::cast`, on his wizardry band, or his energy's where his class has none.
                if (power->power == sim::Power::Spirits) {
                    const sim::SkillRow* spell = sim::skillNumbered(sim::skill::kEvilSpirit);
                    const bool band = who.wizardMinimum > 0.0 || who.wizardMaximum > 0.0;
                    const double bandLow = band ? who.wizardMinimum
                                                : double(who.points.energy) * sim::kRuneEnergyLow;
                    const double bandHigh = band ? who.wizardMaximum
                                                 : double(who.points.energy) * sim::kRuneEnergyHigh;
                    const int damage = spell ? spell->damage : 0;
                    const int low = int((bandLow + double(damage)) * who.wizardryRate);
                    const int high =
                        int((bandHigh + double(damage + damage / 2)) * who.wizardryRate);
                    socket.rows.push_back(stat("Each spirit",
                                               std::to_string(low) + " ~ " + std::to_string(high),
                                               Tone::Yellow));
                    char sum[96];
                    std::snprintf(sum, sizeof(sum), "spell %d ~ %d, +%d ~ %d from %d ene", damage,
                                  damage + damage / 2, int(bandLow), int(bandHigh),
                                  who.points.energy);
                    Row how;
                    how.free = sum;
                    how.freeTone = Tone::Gray;
                    socket.rows.push_back(how);
                }
                continue;
            } else {
                line.free = "Empty Socket";
                line.freeTone = Tone::Blue;
                line.mark = tip::Mark::Ring;
            }
            socket.rows.push_back(line);
        }
        sheet.sections.push_back(socket);
    }

    // ---- its set -------------------------------------------------------------------------------
    // Ours, the user's (2026-09-30), in MU's framed set block: the five pieces of its number,
    // green where he wears them and gray where not, then the two steps of sim::setDefense, lit
    // when they stand. The set's name is the row's label less its last word ("Leather Helm").
    if (const int set = sim::setOf(row); set >= 0) {
        Section pieces;
        pieces.framed = true;
        const size_t space = row.label.rfind(' ');
        pieces.kicker = (space == std::string::npos ? row.label : row.label.substr(0, space)) + " Set";
        bool all = true, excellent = true;
        for (int slot = sim::kHelm; slot <= sim::kBoots; ++slot) {
            const int group = slot - sim::kHelm + sim::kGroupHelms;
            const content::ItemRow* piece = nullptr;
            for (const content::ItemRow& r : tables.items) {
                if (r.group == group && r.number == set) piece = &r;
            }
            const sim::Held& worn = bag[slot];
            const bool on = !worn.empty() && size_t(worn.item) < tables.items.size() &&
                            tables.items[size_t(worn.item)].group == group &&
                            tables.items[size_t(worn.item)].number == set;
            all = all && on;
            excellent = excellent && on && worn.excellent != 0;
            if (!piece) continue;
            Row line;
            line.free = piece->label;
            line.freeTone = on ? Tone::Green : Tone::Gray;
            pieces.rows.push_back(line);
        }
        const auto step = [&](const std::string& when, double rate, bool stands) {
            Row line;
            line.free = when + ": Defense";
            line.tail = "+" + std::to_string(int(std::lround(rate * 100.0))) + "%";
            line.freeTone = stands ? Tone::Green : Tone::Gray;
            pieces.rows.push_back(line);
        };
        step("Complete set", sim::kSetDefense, all && !excellent);
        step("Excellent set", sim::kExcellentSetDefense, excellent);
        sheet.sections.push_back(pieces);
    }
    // A Rune of Creation: the power it carries, what that does, and whose and where it goes.
    if (sim::creation(row)) {
        if (const sim::PowerRow* power = sim::powerOf(powerAt(what, 0))) {
            sheet.rune = runeRow(tables);
            Section carries;
            Row line;
            line.keyword = power->name;
            line.mark = tip::Mark::RingSet;
            line.markTone = Tone::Orange;
            line.free = power->tells ? power->tells : "";
            line.freeTone = Tone::Orange;
            carries.rows.push_back(line);
            // Its group (sim::PowerRow): the classes, and the sockets that take it.
            const bool everyone = power->classes == sim::kEveryClass;
            std::string classes = everyone ? "Every class" : "";
            for (size_t i = 0; !everyone && i < 3; ++i) {
                if (!power->takenBy(sim::Kin(i))) continue;
                classes += (classes.empty() ? "" : " / ") + std::string(kNames[i]);
            }
            std::vector<const char*> kinds;
            if (power->slots & sim::kInWeapon) kinds.push_back("weapon's");
            if (power->slots & sim::kInArmour) kinds.push_back("armour's");
            if (power->slots & sim::kInShield) kinds.push_back("shield's");
            if (power->slots & sim::kInJewellery) {
                kinds.push_back("ring's");
                kinds.push_back("pendant's");
            }
            std::string sockets = kinds.empty() || kinds[0][0] != 'a' ? "a " : "an ";
            for (size_t k = 0; k < kinds.size(); ++k) {
                sockets += k == 0 ? "" : k + 1 == kinds.size() ? " or " : ", ";
                sockets += kinds[k];
            }
            Row where;
            where.free = classes + " \xC2\xB7 " + sockets + " socket";
            where.freeTone = Tone::Gray;
            carries.rows.push_back(where);
            sheet.sections.push_back(carries);
        }
    }

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
        const bool known = index >= 0 && (who.learned & (uint64_t(1) << index)) != 0;
        if (!row.teachesName.empty()) {
            teaches.rows.push_back(stat("Skill", row.teachesName, known ? Tone::Gray : Tone::Blue));
        }
        // **And what it is thrown with** (the user, 2026-09-29: "on orb tooltip for DK skills we
        // need to also show for which weapon type its usable"): the row the skill's own card
        // prints (`Desk::skillSheet`), one family a line under the label, white when what he
        // holds will throw it and red when it will not. A spell asks nothing of the hand and has
        // no such row, as on its card.
        if (const sim::SkillRow* skill = sim::skillNumbered(row.teaches); skill && !skill->wizardry) {
            const std::string families = sim::familiesListed(skill->families);
            if (!families.empty()) {
                const bool holds = skill->suits(skill->onSelf() ? who.offHand : who.hand);
                teaches.rows.push_back(
                    stat("Weapon", families, known ? Tone::Gray : holds ? Tone::White : Tone::Red));
            }
        }
        if (known) {
            // The words go in the FOOT, opposite the Zen -- the user, 2026-09-23, moving them
            // out of this block where they first landed as a "Known" row. The foot is where
            // they belong: on Hanzo's shelf the price is the other half of the same question,
            // and "Already learned" across from "3,000 Zen" is one glance instead of two.
            sheet.note = "Already learned";
            sheet.noteTone = Tone::Red;
        }
        // **And what the skill hits for, off the skill's own row.** The user asked for the
        // damage modifiers here on 2026-09-23, and the reason they belong on a scroll and not
        // only on the skill card is that this card is read BEFORE the decision: on Hanzo's
        // shelf, beside a price, deciding whether to spend three thousand Zen on it. The skill
        // card is read afterwards.
        //
        // The same three lines the card prints, in the same words and the same colours
        // (`Desk::skillSheet`), because two cards describing one skill differently is how a
        // player learns to distrust both. The multiplier is THIS character's -- `sim::force`
        // folds his strength in -- so an orb read at 30 strength and the same orb at 200 do not
        // claim the same blow.
        if (const sim::SkillRow* skill = sim::skillNumbered(row.teaches)) {
            if (skill->onSelf()) {
                teaches.rows.push_back(
                    stat("Absorbs",
                         sim::absorbed(sim::boonShare(*skill, who.points, who.shieldDefense)) +
                             " of every blow",
                         known ? Tone::Gray : Tone::Green));
                teaches.rows.push_back(stat("Lasts",
                                            sim::spoken(float(skill->boonTicks) * 0.05f),
                                            known ? Tone::Gray : Tone::White));
            } else if (skill->wizardry) {
                // A spell: the lines its own card prints, in his hands (`spellLines`). The
                // knight's "x1.00 of a swing" and its strength sum said nothing true of one.
                spellLines(*skill, who, known, teaches.rows);
            } else {
                char sum[64];
                std::snprintf(sum, sizeof sum, "%.2f of a swing",
                              double(sim::force(*skill, who.points)));
                teaches.rows.push_back(
                    stat("Damage", "x" + std::string(sum), known ? Tone::Gray : Tone::Yellow));
                // And the sum that made it, grey and on one line, exactly as the skill card
                // prints it: the row's own base plus his strength over the skill's divisor.
                // It is what argues for spending a point on strength, and on a shelf it is
                // what tells one orb's ceiling from another's.
                std::snprintf(sum, sizeof sum, "%.2f + %d str / %d", double(skill->force),
                              who.points.strength,
                              skill->forcePerStrength > 0.0f
                                  ? int(1.0f / skill->forcePerStrength + 0.5f)
                                  : 0);
                Row how;
                how.free = sum;
                how.freeTone = Tone::Gray;
                teaches.rows.push_back(how);
            }
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

    // ---- what it asks -----------------------------------------------------------------------
    // The foot's left column, under whom it is for (2026-09-27). Quiet when it is met and red,
    // with how far short, when it is not: a card read for a second should have the one line that
    // stops you wearing it be the one that shouts. MU prints them white and only the failures
    // red; the card goes one step quieter on the ones you meet.
    const sim::Needs asked = sim::asks(row, plus, what.excellent != 0);
    const sim::Needs owed = sim::shortOf(asked, who.level, who.points);
    // One class line, and none when every class may: the allowed ones in mu.db's order, the
    // wizard, the elf, the knight -- green when yours is among them and red when it is not.
    if (named > 0 && named < 3) {
        std::string classes;
        for (int i = 0; i < 3; ++i) {
            if (!((row.classes >> i) & 1)) continue;
            classes += (classes.empty() ? "" : " / ") + std::string(kNames[i]);
        }
        const bool mine = (row.classes >> int(who.kin)) & 1;
        sheet.who.push_back({classes, mine ? Tone::Green : Tone::Red, false});
    }
    const auto require = [&](const char* name, int wants, int lacking) {
        if (wants <= 0) return;
        std::string text = std::string(name) + " " + std::to_string(wants);
        if (lacking > 0) text += " \xB7 lacking " + std::to_string(lacking);
        sheet.who.push_back({text, lacking > 0 ? Tone::Red : Tone::White, lacking <= 0});
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
    // ---- against what he has on ---------------------------------------------------------------
    // MU2's comparison, marked there as the project's: it hangs off the figure it belongs to --
    // the headline, or a staff's wizardry row -- rather than being a sentence of its own.
    //
    // **Both sides are what the fight reckons them at**, `Realm::redress`'s own sums: the plus,
    // the additional option and being excellent, on this one and on the one worn alike. It
    // compared the printed band and the bare plus until 2026-09-27, so a +Option or an excellent
    // piece worn read as worse than it is -- the user's report. The headline itself stays MU's
    // printed figure, the option on its own line under it, as MuMain prints them.
    const int slot = sim::placeOf(row);
    if (slot >= 0 && !bag[slot].empty() && &bag[slot] != &what) {
        const sim::Held& on = bag[slot];
        const content::ItemRow& theirs = tables.items[size_t(on.item)];
        const float mine = swingOf(row, what), hisDamage = swingOf(theirs, on);
        const float ours = float(armourOf(row, what)), hisDefense = float(armourOf(theirs, on));
        const float hisRise = theirs.magicPower > 0 ? staffRise(theirs.magicPower, on.refinement)
                                                    : 0.0f;
        const auto by = [](float ours, float his, const char* unit, std::string& delta, int& way) {
            const float d = ours - his;
            delta = (d > 0.0f ? "+" : "") + decimal(d) + unit + " vs worn";
            way = d > 0.0f ? 1 : d < 0.0f ? -1 : 0;
        };
        if (sheet.hero.word == "Damage" && (mine > 0.0f || hisDamage > 0.0f)) {
            by(mine, hisDamage, "", sheet.hero.delta, sheet.hero.deltaWay);
        } else if (sheet.hero.word == "Armor" && (ours > 0.0f || hisDefense > 0.0f)) {
            by(ours, hisDefense, "", sheet.hero.delta, sheet.hero.deltaWay);
        }
        if (rise > 0.0f || hisRise > 0.0f) {
            for (Section& section : sheet.sections) {
                for (Row& line : section.rows) {
                    if (line.label != "Wizardry damage" || line.values.empty()) continue;
                    by(rise, hisRise, "%", line.values[0].delta, line.values[0].deltaWay);
                }
            }
        }
    }

    // Too dear to throw away: MuMain's IsHighValueItem, in its RedPurple, where the concept put
    // "Account Bound". The window refuses the drop either way; this says so before it is tried.
    if (sim::expensive(tables, what)) {
        sheet.keep.push_back({"Cannot be dropped", Tone::RedPurple, false});
    }

    // ---- the foot -----------------------------------------------------------------------------
    // Ammunition's durability is its shots, and MU draws it the same way.
    if (sim::ammunition(row) && row.durability > 0) {
        sheet.wear = std::to_string(what.durability) + " / " + std::to_string(row.durability);
        sheet.worn = float(what.durability) / float(row.durability);
    }
    // And gear's is its wear: MU's `Durability: [51/66]` (GT 71, ZI:4756) against the maximum at
    // its plus, the bar in the band's colour once it is at half or under -- the same four the
    // warning icons and the slot's wash use.
    if (sim::wears(row)) {
        const int maximum = sim::maximumDurability(row, what);
        // A pet's is its Life: MU's `Life: %d` (GT 70) for ITEM_HELPER to +7 (:4656-4661).
        const char* word =
            row.group == sim::kGroupPets && !sim::jewellery(row) ? "Life " : "Durability ";
        sheet.wear = word + std::to_string(what.durability) + " / " + std::to_string(maximum);
        sheet.worn = maximum > 0 ? float(what.durability) / float(maximum) : 0.0f;
        switch (sim::wornBand(what.durability, maximum)) {
            case sim::Worn::Broken:
            case sim::Worn::Fifth: sheet.wearTone = tip::Tone::Red; break;
            case sim::Worn::Third: sheet.wearTone = tip::Tone::Orange; break;
            case sim::Worn::Half: sheet.wearTone = tip::Tone::Yellow; break;
            default: break;
        }
        if (what.durability <= 0) sheet.note = "Broken";
    }
    return sheet;
}

}  // namespace mu::game
