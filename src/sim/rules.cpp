#include "sim/rules.h"

#include <algorithm>
#include <cmath>

namespace mu::sim {

double hitChance(float attackRate, float defenseRate) {
    if (defenseRate < attackRate && attackRate > 0.0f) {
        return 1.0 - (double(defenseRate) / double(attackRate));
    }
    return 0.03;
}

Blow strike(const Fighter& attacker, const Fighter& defender, Random& dice) {
    Blow blow;
    // 1. Did it land. One draw, always.
    if (!dice.nextBool(hitChance(attacker.attackRate, defender.defenseRate))) return blow;
    blow.hit = true;

    // 2. A critical is the maximum exactly -- every bonus term that widens it belongs to a
    // later season. The draw happens only when there is a chance to draw against; hoisting it
    // out of that condition consumes a number and shifts every later draw in the run.
    if (attacker.criticalChance > 0.0 && dice.nextBool(attacker.criticalChance)) {
        blow.critical = true;
        blow.rolled = attacker.maximumDamage;
    } else {
        // 3. rand(min, max), upper bound exclusive, and no draw at all when max <= min.
        blow.rolled = dice.nextInt(attacker.minimumDamage, attacker.maximumDamage);
    }

    // 4. Minus the defence, which cannot help the attacker.
    int damage = blow.rolled - std::max(0, defender.defense);
    blow.afterDefense = damage;

    // 5. Overrates: a defender who out-rates the attacker takes three tenths.
    // AttackableExtensions.cs:728-731.
    if (defender.defenseRate > attacker.attackRate) {
        blow.overrated = true;
        damage = int(double(damage) * 0.3);
    }

    // 6. The level floor, AFTER the subtraction. This is what stops a high-level monster being
    // harmless to a tank, and it would be invisible if it came first.
    const int floorDamage = std::max(1, attacker.level / 10);
    if (damage < floorDamage) {
        blow.floored = true;
        damage = floorDamage;
    }

    // 7. And what the defender is shrugging off, after the floor and only above 1.
    if (damage > 1) damage = int(double(damage) * defender.damageTaken);

    blow.damage = damage;
    return blow;
}

// The three rows, each from its own initialiser under
// Persistence/Initialization/CharacterClasses -- the files
// Version075/CharacterClassInitialization.cs:31-33 constructs. The Dark Knight's was traced
// line by line in the sprint's census; the other two were MU2's word there and are traced here,
// which corrected one thing MU2's table did not say: the Fairy Elf's physical damage is not
// zero, it is conditional on her attack mode, and her melee rate runs over strength AND agility
// together.
//
//               lvl  agi   str  defRate  def    min/str max/str  min/s+a max/s+a  base lvl vit
// DarkWizard      5  1.5  0.25    1/3    0.25     1/8     1/4       0      0        30   1   2
// FairyElf        5  1.5  0.25    0.25   1/10      0       0       1/7    1/4       39   1   2
// DarkKnight      5  1.5  0.25    1/3    1/3      1/6     1/4       0      0        35   2   3
//
// ClassDarkWizard.cs:51-56, :66-71, :111; ClassFairyElf.cs:56-61, :73-80, :119;
// ClassDarkKnight.cs:51-56, :68-71, :107. The halving of the defence is shared by all three:
// CharacterClasses/CharacterClassInitialization.cs:103, `Stats.DefenseFinal, 0.5f,
// Stats.DefenseBase`.
const ClassRow kRows[3] = {
    // Dark Wizard
    {5.0f, 1.5f, 0.25f, 1.0f / 3.0f, 0.25f, 1.0f / 8.0f, 0.25f, 0.0f, 0.0f, 30.0f, 1.0f, 2.0f},
    // Fairy Elf
    {5.0f, 1.5f, 0.25f, 0.25f, 0.1f, 0.0f, 0.0f, 1.0f / 7.0f, 0.25f, 39.0f, 1.0f, 2.0f},
    // Dark Knight
    {5.0f, 1.5f, 0.25f, 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 6.0f, 0.25f, 0.0f, 0.0f, 35.0f, 2.0f,
     3.0f},
};

// ClassDarkWizard.cs:38-41, ClassFairyElf.cs:41-44, ClassDarkKnight.cs:38-41.
const HeroPoints kStarting[3] = {
    {18, 18, 15, 30},  // Dark Wizard
    {22, 25, 20, 15},  // Fairy Elf
    {28, 20, 25, 10},  // Dark Knight
};

const ClassRow& rowOf(Kin kin) { return kRows[int(kin) % 3]; }

HeroPoints startingPoints(Kin kin) { return kStarting[int(kin) % 3]; }

void reckon(Kin kin, int level, const HeroPoints& points, Fighter* out, int* maxHealth) {
    const ClassRow& row = rowOf(kin);
    const double strength = double(points.strength);
    const double agility = double(points.agility);

    out->level = level;
    // Not truncated: see the note on Fighter. These two are the only stats OpenMU reads as
    // floats, and they are read as floats in the two places that matter -- the hit chance and
    // the overrates test.
    out->attackRate = float(double(level) * double(row.ratePerLevel) +
                            agility * double(row.ratePerAgility) +
                            strength * double(row.ratePerStrength));
    out->defenseRate = float(agility * double(row.defenseRatePerAgility));
    // Armour adds nothing until sprint 7 gives items their rows; the halving is the class
    // initialiser's and applies whatever the armour is.
    // And this one IS truncated, because AttackableExtensions.cs:92 truncates it:
    // `defense = (int)((attributes[defenseAttribute] + GreaterDefenseBonus) * DefenseDecrement)`.
    // A knight's 3.333 is 3 in the original as well.
    out->defense = int((agility * double(row.defensePerAgility) + 0.0) * 0.5);
    // And no weapon, so the damage is the arms alone. A naked level-1 knight doing one point to
    // a Bull Fighter is not a bug: it is what 0.75 says about hitting an armoured animal six
    // levels up with your fists, and the damage floor is what he has instead of nothing.
    out->minimumDamage = int(strength * double(row.minimumDamagePerStrength) +
                             (strength + agility) *
                                 double(row.minimumDamagePerStrengthAndAgility));
    out->maximumDamage = int(strength * double(row.maximumDamagePerStrength) +
                             (strength + agility) *
                                 double(row.maximumDamagePerStrengthAndAgility));
    out->criticalChance = 0.0;  // the luck option is 0.75's only source, and it is sprint 7's
    out->damageTaken = 1.0;

    // Truncated, and it is a departure of the same kind as the two above: OpenMU keeps
    // MaximumHealth as a float attribute and compares health against it as one. Nothing in
    // 0.75 grants a fractional health, so every class's total here is a whole number anyway --
    // 35 + 2 + 75 for a knight -- and this is marked rather than argued.
    *maxHealth = int(double(row.baseHealth) + double(level) * double(row.healthPerLevel) +
                     double(points.vitality) * double(row.healthPerVitality));
}

uint64_t neededExperience(int level) {
    if (level <= 0) return 0;
    const uint64_t at = uint64_t(level);
    uint64_t needed = 10ull * (at + 8) * (at - 1) * (at - 1);
    if (level >= 256) {
        needed += 1000ull * (at - 247) * (at - 256) * (at - 256);
    }
    return needed;
}

double killExperience(int killedLevel, int killerLevel) {
    const double killed = double(killedLevel);
    double worth = (killed + 25.0) * killed / 3.0;
    // Float division, deliberately: an integer one gives 0 for every kill more than ten levels
    // down and quietly ends low-level hunting.
    if (double(killerLevel) > killed + 10.0) {
        worth *= (killed + 10.0) / double(killerLevel);
    }
    if (killedLevel >= 65) {
        // Not floored. `targetLevel` is a float attribute at AttackableExtensions.cs:621 and
        // the quarter is a float division: at level 65 the original adds 16.25 and a floored
        // port adds 16. Nothing in this content reaches 65, which is exactly why it would
        // never have been noticed.
        worth += (killed - 64.0) * (killed / 4.0);
    }
    return std::max(worth, 0.0) * 1.25;
}

}  // namespace mu::sim
