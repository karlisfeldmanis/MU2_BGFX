#include "game/ui/describe.h"

#include <cstdio>
#include <string>

namespace mu::game {
namespace {

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

}  // namespace

uint32_t moneyColour(long long zen) {
    if (zen >= 10000000) return gfx::rgba(0.0f, 0.0f, 1.0f);
    if (zen >= 1000000) return gfx::rgba(0.0f, 150.0f / 255.0f, 1.0f);
    if (zen >= 100000) return gfx::rgba(24.0f / 255.0f, 201.0f / 255.0f, 0.0f);
    return gfx::rgba(150.0f / 255.0f, 220.0f / 255.0f, 1.0f);
}

std::vector<panel::Line> describe(const content::Tables& tables, const sim::Held& what,
                                  const sim::Wearer& who, const sim::Satchel& bag) {
    std::vector<panel::Line> lines;
    if (what.empty() || size_t(what.item) >= tables.items.size()) return lines;
    const content::ItemRow& row = tables.items[size_t(what.item)];
    const int plus = what.refinement;
    lines.push_back({label(row, plus), plus >= kRefinedFrom ? panel::kRefined : panel::kOrdinary,
                     true});

    // The damage at its plus, which is how MU quotes it -- `Lookup(40 + TwoHand)`, so the label
    // says which and there is no separate two-handed line. Yellow where the plus adds to it.
    const bool weapon = row.weapon() && !sim::ammunition(row);
    const int bonus = weapon ? sim::damageBonus(plus) : 0;
    if (weapon && row.maximumDamage > 0) {
        lines.push_back({std::string(row.twoHanded() ? "Two-handed" : "One-handed") +
                             " damage : " + std::to_string(row.minimumDamage + bonus) + " ~ " +
                             std::to_string(row.maximumDamage + bonus),
                         plus > 0 ? panel::kRefined : panel::kOrdinary});
    }
    const bool worn = row.armour() || row.shield();
    const int defense = worn ? row.defense + sim::defenseBonus(row.shield(), plus) : 0;
    if (worn) {
        lines.push_back({"Defense : " + std::to_string(defense),
                         plus > 0 ? panel::kRefined : panel::kOrdinary});
    }
    // A staff's magic power, the one line MU prints for it, and the percentage it comes to --
    // MU2's addition, marked there as the project's, because it is what a wizard chooses on.
    float rise = 0.0f;
    if (row.magicPower > 0) {
        rise = staffRise(row.magicPower, plus);
        lines.push_back({"Magic power : " + std::to_string(row.magicPower), panel::kOrdinary});
        lines.push_back({"Wizardry damage : +" + decimal(rise) + "%",
                         plus > 0 ? panel::kRefined : panel::kOrdinary});
    }
    if (weapon && row.attackSpeed > 0) {
        lines.push_back({"Attack speed : " + std::to_string(row.attackSpeed), panel::kOrdinary});
    }
    // Ammunition's durability is its shots, and MU draws it the same way.
    if (sim::ammunition(row) && row.durability > 0) {
        lines.push_back({"Durability : " + std::to_string(what.durability) + "/" +
                             std::to_string(row.durability),
                         panel::kOrdinary});
    }

    // Each requirement, white where met and red where not, and a red "(Lacking n)" beneath:
    // the same shortfall the equip is refused by. The client's order.
    const sim::Needs asked = sim::asks(row, plus);
    const sim::Needs owed = sim::shortOf(asked, who.level, who.points);
    const auto require = [&](const char* name, int asks, int lacking) {
        if (asks <= 0) return;
        lines.push_back({std::string(name) + " : " + std::to_string(asks),
                         lacking > 0 ? panel::kUnmet : panel::kOrdinary});
        if (lacking > 0) {
            lines.push_back({"(Lacking " + std::to_string(lacking) + ")", panel::kUnmet});
        }
    };
    require("Required Level", asked.level, owed.level);
    require("Required Strength", asked.strength, owed.strength);
    require("Required Agility", asked.agility, owed.agility);
    require("Required Vitality", asked.vitality, owed.vitality);
    require("Required Energy", asked.energy, owed.energy);

    // One line per class allowed, and none when they all are: mu.db's order, the wizard, the
    // elf, the knight.
    static const char* const kNames[3] = {"Dark Wizard", "Fairy Elf", "Dark Knight"};
    int named = 0;
    for (int i = 0; i < 3; ++i) named += (row.classes >> i) & 1;
    if (named > 0 && named < 3) {
        for (int i = 0; i < 3; ++i) {
            if (!((row.classes >> i) & 1)) continue;
            lines.push_back({std::string("Can be equipped by ") + kNames[i],
                             i == int(who.kin) ? panel::kOrdinary : panel::kUnmet});
        }
    }

    // And against what he has on in the slot it would go in: MU2's, marked there as the
    // project's. Nothing is said against an empty slot, or against itself.
    const int slot = sim::placeOf(row);
    if (slot >= 0 && !bag[slot].empty() && &bag[slot] != &what) {
        const sim::Held& on = bag[slot];
        const content::ItemRow& theirs = tables.items[size_t(on.item)];
        const auto against = [&](const char* name, float mine, float his, const char* unit) {
            if (mine <= 0.0f && his <= 0.0f) return;
            const float by = mine - his;
            std::string text = by > 0.0f   ? "+" + decimal(by) + unit + " " + name + " over the one worn"
                               : by < 0.0f ? decimal(by) + unit + " " + name + " against the one worn"
                                           : std::string("The same ") + name + " as the one worn";
            lines.push_back({text, by > 0.0f   ? panel::kRefined
                                   : by < 0.0f ? panel::kUnmet
                                               : panel::kOrdinary});
        };
        const bool hisWeapon = theirs.weapon() && !sim::ammunition(theirs);
        const int hisBonus = hisWeapon ? sim::damageBonus(on.refinement) : 0;
        against("damage",
                weapon ? float(row.minimumDamage + row.maximumDamage + 2 * bonus) / 2.0f : 0.0f,
                hisWeapon
                    ? float(theirs.minimumDamage + theirs.maximumDamage + 2 * hisBonus) / 2.0f
                    : 0.0f,
                "");
        const bool hisWorn = theirs.armour() || theirs.shield();
        against("defense", float(defense),
                hisWorn ? float(theirs.defense + sim::defenseBonus(theirs.shield(), on.refinement))
                        : 0.0f,
                "");
        against("wizardry damage", rise,
                theirs.magicPower > 0 ? staffRise(theirs.magicPower, on.refinement) : 0.0f, "%");
    }
    return lines;
}

}  // namespace mu::game
