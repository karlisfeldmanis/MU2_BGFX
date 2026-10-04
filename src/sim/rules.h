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
// What the excellent options he wears come to (ExcellentOptions.cs), summed off his worn slots
// by Realm::rearm: the multiplying ones multiply per piece and the adding ones add, as their
// AggregateType says. Neutral for a monster and for anybody wearing nothing excellent.
// What a worn pet does while it has life left: OpenMU's Version075/Items/Pets.cs:32-35, the
// base power-ups CreatePet hangs on the row, and ItemPowerUpFactory.cs:38-41 drops them at 0 --
// corrected where WebZen's own GameServer says otherwise (1.00.93, ObjAttack.cpp:1041-1087 and
// user.cpp gObjSpriteDamage; docs/pets.md, 2026-09-30):
//   Guardian Angel  damage taken x0.7 (the x0.8 is NEW_FORSKYLAND3's later cut), max HP +50,
//                   damage dealt x0.8 (ours, the user's, no server has it),
//                   wears damage x 3/10 / 10 a hit taken
//   Imp             damage dealt x1.3 for 3 of his own life a blow, wears damage x 2/10 / 10
//   Horn of Uniria  nothing but the ride (kRideFactor), wears damage x 1/10 / 10 (docs/mount.md)
//   Horn of Dinorant the ride, damage dealt x1.15 for 1 of his own life a blow and taken x0.9,
//                   wears damage x 1/200 (0.95d's, not 0.75's)
struct PetPower {
    double taken = 1.0;  // on every blow he takes, after the floor and only above 1
    double dealt = 1.0;  // on every blow he lands, after the floor
    int health = 0;      // on his maximum, before the excellent armour's x1.04
    int lifeCost = 0;    // his own life, paid on every blow he lands that `dealt` lifts
    double wear = 0.0;   // the pet's life lost per point of damage he takes
    bool mount = false;  // ridden: he rides off a safe tile (Body::riding)
};

struct Excellence {
    // On a weapon.
    double killMana = 0.0;        // 1: an eighth of max mana back after a kill
    double killLife = 0.0;        // 2: an eighth of max life
    int speed = 0;                // 3: attack speed +7
    double damageRate = 1.0;      // 4: x1.02 on the damage (PhysicalBaseDmgIncrease)
    int levelPieces = 0;          // 5: + level / 20 on the damage, each
    double excellentChance = 0.0; // 6: 0.1, a blow at 1.2 x the top of the band
    // On armour and the shield.
    double zenRate = 1.0;         // 1: x1.4 on Zen picked up (MoneyAmountRate)
    double defenseRateRate = 1.0; // 2: x1.1 on the defence rate
    double reflect = 0.0;         // 3: 0.05 of what reaches him sent back
    double damageDecrease = 0.0;  // 4: 0.04 off what reaches him (ArmorDamageDecrease)
    double manaRate = 1.0;        // 5: x1.04 on max mana
    // A 2nd wing's extras (sim::kWingMaxMana, kWingIgnoreDefense): raw max mana before the rate,
    // and the chance a blow ignores the defence it meets.
    int moreMana = 0;
    double ignoreDefense = 0.0;
    double healthRate = 1.0;      // 6: x1.04 on max life
    // Not an option: the Rune of the Undying's x1.2 on max life, each one set in anything worn
    // (sim::kUndyingHealth). Beside the excellent rate so the two multiply, kept apart so the
    // character window can say which is which.
    double undyingRate = 1.0;
    // The Dungeon's runes, each set in anything worn (sim/items.h): Keen Eye's critical chance,
    // Bloodwell's share of a wound back as life (its mana after a kill rides `killMana`), and how
    // many Frenzies he wears, each rolling on its own.
    double runeCritical = 0.0;
    double lifeSteal = 0.0;
    int frenzies = 0;
    // Renewal's share of maximum health every three seconds, anywhere (sim::kRenewalShare).
    double renewal = 0.0;
    // How many Evil Spirit runes his shield, rings and pendant carry, each rolling on a miss
    // (sim::kSpiritChance).
    int spirits = 0;
    // How many element runes his hands, rings and pendant carry, by sim::Element (sim/items.h
    // kElementRuneDamage), each adding its share to his blows of that element.
    int elementRunes[6] = {};
    // A Bulwark in his hands, which lets Defense up with no shield on his arm, and a Kinship in a
    // ring or the pendant, which lifts his pet's price (sim/items.h).
    bool bulwark = false;
    bool kinship = false;
    // How many Wraths his hands carry, each adding sim::kWrathDamage to every blow he deals.
    int wraths = 0;
    // Ironskin's share more defence, Steadfast's chance a blow is blocked, and how many
    // Whirlwinds a knight's hands carry (sim/items.h). Second Wind rides killLife and killMana.
    double runeDefense = 0.0;
    double blockChance = 0.0;
    int whirlwinds = 0;
    // And Piercing Volleys in a Muse Elf's hands, which fan her Penetration (sim::lanesOf).
    int volleys = 0;
    // Not options either: the rings' and the pendant's (docs/jewellery.md). The largest
    // resistance worn in each element and the resistance powers on top: Ice and Poison each turn
    // the element aside r times in r + 1, as Lightning does a beast's push; Fire cuts a fire blow
    // (sim::kResistanceCut).
    // And the percent of maximum life the worn pieces' options give back every
    // kJewelleryRegenTicks.
    int iceResistance = 0;
    int poisonResistance = 0;
    int lightningResistance = 0;
    int fireResistance = 0;
    int lifeRegen = 0;
    // The powered rings' and pendant's (sim::Affix), every worn piece's summed, in percent but
    // the Leech's: more experience a kill, more Zen, a likelier item, life on a landed blow, and
    // a critical's top raised.
    int moreExperience = 0;
    int moreZen = 0;
    int itemFind = 0;
    int lifeOnHit = 0;
    int criticalDamage = 0;
};

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
    // A Dark Knight's second band, while he holds a weapon in each hand (Arms::dual), and 0
    // for everybody else: WebZen's m_AttackDamageMinLeft/MaxLeft, strength and the left weapon,
    // rolled apart from the right and added to it (1.00.93 ObjAttack.cpp:3346-3414).
    int offhandMinimumDamage = 0;
    int offhandMaximumDamage = 0;
    // 0.75 grants this from exactly one thing, the luck item option at 0.05 each, so it is 0
    // for every monster and for an unlucky character. It is a field rather than a constant
    // because the draw against it is conditional and the condition is load-bearing.
    double criticalChance = 0.0;
    // What a critical lays on the top of the band, in percent: the Pendant of Fury's and its
    // kind's (sim::Affix::Fury), ours. 0 is MU's critical, the maximum exactly. No draw.
    int criticalDamage = 0;
    // The chance a blow lays the defence it meets at nought, a 2nd wing's extra
    // (GetTargetDefense, ObjBaseAttack.cpp:1322-1339). Drawn only when above nought.
    double ignoreDefense = 0.0;
    // Stats.DamageReceiveDecrement. 0.75 grants it from exactly one thing, the knight's
    // Defense skill at 0.50 for four seconds. 1 is "nothing is reducing this".
    double damageTaken = 1.0;
    // The excellent options' two that live in a blow: the chance of an excellent hit, and the
    // share taken off one received (Excellence).
    double excellentChance = 0.0;
    double damageDecrease = 0.0;
    // Steadfast's chance a blow that would land on him is blocked and misses (sim/items.h). Drawn
    // only when above nought, so nobody without one draws anything new.
    double blockChance = 0.0;
    // `Stats.GreaterDamageBonus`: the elf's Greater Damage, added to every blow after the
    // defence (AttackableExtensions.cs:185). Nought for anybody not under it.
    int greaterDamage = 0;
    // Stats.AttackDamageIncrease: the Imp's x1.3, taken on every blow after the level floor and
    // before damageTaken (AttackableExtensions.cs:220-224). 1 for everybody without one.
    double damageDealt = 1.0;
    // The wizardry band, before a spell's own damage is added: `MinimumWizBaseDmg = energy / 9`
    // and `MaximumWizBaseDmg = energy / 4` (ClassDarkWizard.cs:72-73). Floats, because OpenMU
    // keeps them as float attributes and truncates only after the spell and the staff are in
    // (AttackableExtensions.cs:848-849). Nought for the two classes that have no such row.
    double wizardMinimum = 0.0;
    double wizardMaximum = 0.0;
    // `WizardryAttackDamageIncrease`: one, plus a hundredth of the staff's rise
    // (ClassDarkWizard.cs:81). One for anybody without a staff.
    double wizardryRate = 1.0;
};

// The workings, not just the number. A log line that says `14` cannot be checked against
// OpenMU and one that says `roll 12 in [9,17] - def 3 = 9, floored to 14` can.
struct Blow {
    bool hit = false;
    bool critical = false;
    bool excellent = false;  // 1.2 x the top of the band
    int rolled = 0;         // before defence
    int afterDefense = 0;
    bool overrated = false; // the x0.3 arm
    bool floored = false;   // the level floor bit
    bool blocked = false;   // a Steadfast's block: a miss, though the hit roll landed
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

// One spell, the wizardry arm of the same function (AttackableExtensions.cs:155-182, :842-850),
// and it is shorter than the physical one. `skillDamage` is the spell's own `AttackDamage`,
// which `GetSkillDmg` adds to the bottom of the band whole and to the top half again
// (`skillDamage + skillDamage / 2`, integer halves). The band is then `(base + skill) *
// wizardryRate`, truncated. Defence is taken BEFORE the excellent and critical multipliers here,
// which is the one ordering difference from a swing: an excellent spell is
// `(top - defence) * 1.2` where an excellent swing is `top * 1.2 - defence`. Everything after
// -- the overrate, the armour's decrease, the level floor, damageTaken -- is the swing's own.
// The same draws in the same order as `strike`.
Blow cast(const Fighter& attacker, const Fighter& defender, int skillDamage, Random& dice);

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
// The Fairy Elf's archery mode is `Arms::archery` (sprint 15): with a bow or crossbow drawn her
// band is agility and strength at their own rates instead of her melee pair.
// What a character has in his hands, as the arithmetic reads it. Zeroes are bare hands and no
// shield, which is a real state and not a missing one: 0.75 says a man with no weapon swings
// his arms, and the damage floor is what he has instead of nothing.
struct Arms {
    int weaponMinimumDamage = 0;
    int weaponMaximumDamage = 0;
    // A Dark Knight's second weapon (sim::offHanded), with its plus and wear in.
    bool dual = false;
    // What both bands are taken at: 1 for a matched pair, kMixedPair for two kinds of weapon.
    double dualRate = 1.0;
    int offhandMinimumDamage = 0;
    int offhandMaximumDamage = 0;
    int armourDefense = 0;  // a shield's, and later a suit's
    int shieldDefenseRate = 0;  // a worn shield's rate with its plus; never halved
    double setDefense = 0.0;    // a complete set's share of the final defence (sim::setDefense)
    double criticalChance = 0.0;  // luck, 0.05 a lucky thing worn
    Excellence excel;              // what his excellent pieces come to
    // A staff's rise, in percent: `magicPower / 2` and its plus (Version075/Items/Weapons.cs:315
    // and the two tables at :29-30). Nought for everything that is not a staff.
    double staffRise = 0.0;
    // A bow or a crossbow in hand, which puts the Fairy Elf's damage on her archery band
    // (ClassFairyElf.cs:78-81, ArcheryAttackMode :88-89) in place of her melee one.
    bool archery = false;
    // The plus of the quiver in the hand her bow leaves free, 0 to 3, which raises the archery
    // band (sim::reckon).
    int quiverPlus = 0;
    // Greater Damage while it stands (Fighter::greaterDamage).
    int greaterDamage = 0;
    // A worn wing's option (docs/wings.md): Satan's on both ends of every band he swings, and
    // Heaven's on his wizardry, 4 a level, PlusSpecial's AT_IMPROVE_DAMAGE and AT_IMPROVE_MAGIC
    // (ObjCalCharacter.cpp:473-477, :582-583).
    int wingDamage = 0;
    int wingWizardry = 0;
    // The worn pet's, while its life lasts.
    PetPower pet;
};

// A knight's two weapons of different kinds -- a sword and an axe, a mace and a spear -- each
// hit for 55%: OpenMU's double wield (Season 6, AttackableExtensions.cs:131 and
// FixDamageCalcsPlugInBase's -0.45), kept for a mixed pair only. A matched pair hits for
// WebZen's whole two bands. The split is the user's, 2026-10-01. INVENTION.
constexpr double kMixedPair = 0.55;

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

// How much of that a kill actually pays. **Ours, and the one rate in the sim**: every number
// above is OpenMU's, and this is the server multiplier a live MU server has always had, stated
// once here rather than folded into the formula -- so the replica's arithmetic stays readable
// and the hunt's pace is a single line to turn. 1.0 is the original's own pace; this is what
// the hunt was asked for (2026-09-22), raised from 2 to 5 on 2026-09-28 (*"lets increase exp
// gaining"*) and from 5 to 10 the same afternoon (*"increase gain experience"*), and from 10 to 20 on
// 2026-10-01 (*"lets increase exp gain"*), then to 100 the same hour (*"lets do 100"*). It pays the character alone: the Zen a body leaves is
// the untouched formula, so raising this does not quietly make the town richer too.
constexpr double kExperienceRate = 100.0;

}  // namespace mu::sim
