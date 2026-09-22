// 0.75's arithmetic: whether a blow lands, what it takes off, what a kill is worth and what a
// level costs. Transcribed from OpenMU's Version075 with the line beside each one, because a
// review has to be able to read the two side by side.
//
// Nothing here knows what a monster is. A fighter is the six numbers a blow reads, and that is
// deliberate: the same function decides a player hitting a spider and a spider hitting a
// player, and there is no second damage path to get out of step with the first.
#pragma once

#include <cstdint>

#include "sim/random.h"

namespace mu::sim {

// MU's maximum level, and what a level pays for.
// GameConfigurationInitializerBase.cs:42 and ClassDarkKnight.cs:37.
constexpr int kMaximumLevel = 400;
constexpr int kPointsPerLevel = 5;

// Everything a blow reads off either side of it.
struct Fighter {
    int level = 1;
    // The two rates are FLOATS and the three below them are integers, and that is not
    // carelessness -- it is where OpenMU's own casts fall. `GetHitChanceTo`
    // (AttackableExtensions.cs:694-716) declares both rates `float` and divides them as
    // floats, and `Overrates` (:728-731) compares them as floats; a knight's 6.667 of defense
    // rate truncated to 6 gives a hit chance of 0.900 where the original gives 0.890.
    // Meanwhile the defence is `(int)((attribute + bonus) * decrement)` at :92 and the damage
    // band comes out of `GetBaseDmg` already cast (`out int`, :837-849) -- so those three are
    // truncated in the original too, at exactly these points, and carrying them as floats here
    // would be as wrong in the other direction.
    float attackRate = 0.0f;
    float defenseRate = 0.0f;
    int defense = 0;
    int minimumDamage = 0;
    int maximumDamage = 0;
    // 0.75 grants this from exactly one thing, the luck item option at 0.05 each, so it is 0
    // for every monster and for an unlucky character. It is a field rather than a constant
    // because the draw against it is conditional and the condition is load-bearing.
    double criticalChance = 0.0;
    // Stats.DamageReceiveDecrement. 0.75 grants it from exactly one thing, the knight's
    // Defense skill at 0.50 for four seconds. 1 is "nothing is reducing this".
    double damageTaken = 1.0;
};

// The workings, not just the number. A log line that says `14` cannot be checked against
// OpenMU and one that says `roll 12 in [9,17] - def 3 = 9, floored to 14` can.
struct Blow {
    bool hit = false;
    bool critical = false;
    int rolled = 0;         // before defence
    int afterDefense = 0;
    bool overrated = false; // the x0.3 arm
    bool floored = false;   // the level floor bit
    int damage = 0;
};

// AttackableExtensions.cs:694-716. 0.03 unless the attacker out-rates the defender, and then
// one minus the ratio. There is no glancing blow: a swing is a miss or a hit.
//
// The `attackRate > 0` guard is MU2's and not OpenMU's, and it is marked here rather than
// left to be found: OpenMU divides without it, which is a division by zero for an attacker
// with no attack rate at all. Nothing in 0.75's data has one. The guard changes no outcome
// this content can produce and removes an undefined one it cannot.
double hitChance(float attackRate, float defenseRate);

// One swing, in OpenMU's own order, and the order is the behaviour rather than the
// presentation. AttackableExtensions.cs:69-225, physical arm, with every branch 0.75 cannot
// reach left out (the excellent arm, double wield, two-handed increase, berserker, the master
// tree, ArmorDamageDecrease -- all present in the file, all identity in this version, and the
// excellent one would add a draw and desynchronise every seeded log).
//
// Draws, in order and only when the condition holds:
//   1. the hit roll, always -- Rand.NextRandomBool(hitChance)
//   2. the critical roll, ONLY when criticalChance > 0
//   3. the damage roll, ONLY when maximumDamage > minimumDamage
Blow strike(const Fighter& attacker, const Fighter& defender, Random& dice);

// The three classes, in mu.db's own enumeration (`characters.class`) rather than MU's packed
// class byte -- MU writes a Dark Knight as 0x10 and a Blade Knight as 0x11 (Beast.cs:2460-2466),
// and a save file that stored the wire value and a loader that read this one would make a Dark
// Knight a Dark Wizard. Sprint 9's save writes this enumeration and says so.
enum class Kin : uint8_t {
    DarkWizard = 0,
    FairyElf = 1,
    DarkKnight = 2,
};

// A character's points. Each class starts with its own four, and they are the starting values
// OpenMU's class initialisers write -- and the same triples mu.db's saved characters carry,
// which is a weak independent check on all twelve numbers.
struct HeroPoints {
    int strength = 0;
    int agility = 0;
    int vitality = 0;
    int energy = 0;
};

HeroPoints startingPoints(Kin kin);

// One class's whole arithmetic, transcribed from OpenMU's own initialisers -- the files
// Version075/CharacterClassInitialization.cs:31-33 calls into. Every field below has a line
// number beside it in rules.cpp; none of them is MU2's word any more.
struct ClassRow {
    float ratePerLevel;        // AttackRatePvm from TotalLevel
    float ratePerAgility;      // AttackRatePvm from TotalAgility
    float ratePerStrength;     // AttackRatePvm from TotalStrength
    float defenseRatePerAgility;
    float defensePerAgility;   // DefenseBase; DefenseFinal halves it for every class
    float minimumDamagePerStrength;
    float maximumDamagePerStrength;
    // The Fairy Elf's melee damage is a rate over strength AND agility together rather than
    // over strength alone (ClassFairyElf.cs:79-80), which is why this is a second pair rather
    // than a bigger number in the first.
    float minimumDamagePerStrengthAndAgility;
    float maximumDamagePerStrengthAndAgility;
    float baseHealth;
    float healthPerLevel;
    float healthPerVitality;
};

const ClassRow& rowOf(Kin kin);

// A character's stats from his points and his level. No weapon and no armour: sprint 7 owns
// items, and until then a fighter's damage is his arms and his defence is his agility.
//
// What is deliberately not here: mana, ability, shield, and the Fairy Elf's archery mode --
// which is a real branch of her damage (ClassFairyElf.cs:75-78, agility and strength at
// different rates when a bow is drawn) and is unreachable until there is a bow to draw. Melee
// is what a class with no items has. A stat with nothing reading it is a field to get wrong
// twice.
// What a character has in his hands, as the arithmetic reads it. Zeroes are bare hands and no
// shield, which is a real state and not a missing one: 0.75 says a man with no weapon swings
// his arms, and the damage floor is what he has instead of nothing.
struct Arms {
    int weaponMinimumDamage = 0;
    int weaponMaximumDamage = 0;
    int armourDefense = 0;  // a shield's, and later a suit's
    int shieldDefenseRate = 0;  // a worn shield's rate with its plus; never halved
};

void reckon(Kin kin, int level, const HeroPoints& points, const Arms& arms, Fighter* out,
            int* maxHealth);

// The mana pool: a base, a share of the level and a share of energy, per class. Sprint 7 draws
// it in the HUD's right-hand gem; nothing spends it yet, because nothing a 0.75 character
// casts is built here (PLAN.md: skills are their own sprint), so it stands full and a potion
// that restores it has nothing to restore.
//
// The numbers are OpenMU's class files as MU2 transcribed them (Beast.cs `Rates.For`, the
// mana, /lvl and /ene columns) and are NOT independently traced to a line of OpenMU here --
// the same standing the other two classes' health rows had until sprint 5 traced them.
//   Dark Knight  10 + 0.5 x level + 1 x energy
//   Dark Wizard   0 + 2   x level + 2 x energy
//   Fairy Elf     6 + 1.5 x level + 1.5 x energy
int maximumMana(Kin kin, int level, const HeroPoints& points);
// The shield's maximum, the same for all three classes: 1.2 of every stat, the final defence
// and level squared over thirty. MU2's Beast.cs, off OpenMU's Season 3 class definitions.
int maximumShield(int level, const HeroPoints& points, int defense);

// GameConfigurationInitializerBase.cs:87-100. The CUMULATIVE experience to BE this level, not
// the cost of the level itself: read as a per-level cost it makes levelling roughly
// quadratically too fast and the curve still looks like a curve.
//
// The past-255 branch is kept although nothing will reach it this year, because omitting it
// makes the curve quietly wrong exactly where nobody is still checking.
uint64_t neededExperience(int level);

// AttackableExtensions.cs:601-623, what a kill is worth. The 1.25 at the end is inside the
// formula and is not a server's rate.
//
// `killerLevel` is a float in the original and the division is float division: an integer port
// gives 0 for every kill more than ten levels down and silently stops all low-level farming.
double killExperience(int killedLevel, int killerLevel);

}  // namespace mu::sim
