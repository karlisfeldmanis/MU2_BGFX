// **Talents** (docs/plans/talents.md; the design is the Talents canvas,
// https://claude.ai/artifact/RTLpPhd7Prn4G6eX86FCaL); the table is generated from it by
// pipeline/talents_table.js.
//
// One tree a class: six columns (the Magic Gladiator's seven), each three tiers of two small talents
// leading to one major. A small takes up to kTalentSmallMost points, each a plain stat; a major takes
// one. A major opens when either small above it holds kTalentMajorOpensAt; the next tier opens when
// that major is taken; a point comes back only if nothing below it closes. A talent point is earned
// for every kStatPointsPerTalent stat points, so 199 by level 400 (279 for the Magic Gladiator).
// No keystones, no drawbacks, no movement speed and no cooldown reduction: the user's standing rules.
// An invention, all of it -- MU's own master tree (Season 4+) is not followed.
#pragma once

#include <array>
#include <cstdint>
#include <iterator>

#include "sim/items.h"
#include "sim/rules.h"

namespace mu::sim {

constexpr int kTalentTiers = 3;
constexpr int kTalentNodes = kTalentTiers * 3;  // small, small, major, three times
constexpr int kTalentColumnsMost = 7;           // the Magic Gladiator's
constexpr int kTalentSmallMost = 10;
constexpr int kTalentMajorOpensAt = 10;  // 5 until the audit of 2026-10-20: every major by level 217
constexpr int kStatPointsPerTalent = 10;

// What a talent adds to. A small's `amount` is per point, a major's is whole. Percent unless the
// stat is a count (AttackSpeed, CastSpeed, IcePoisonResistance, AidReach).
enum class TalentStat : uint8_t {
    Special,  // a major whose behaviour is its own (Special below)
    Damage, SkillDamage, ElementDamage, Wizardry, DamageAndWizardry,
    AttackSpeed, CastSpeed, LightningChannel, AttackRate,
    CriticalChance, ExcellentChance, IgnoreDefense,
    Defense, DefenseRate, DamageDecrease, Reflect, BlockChance,
    GuardEffect, BarrierEffect, GuardAndBarrierEffect,  // multiply the share past kGuardCap
    MaxLife, MaxMana, MaxShield, ManaRegen, LifeOnKill, LifeSteal, IcePoisonResistance, Threat,
    SkillManaCost, ChillLonger, PoisonLonger, ChilledSlower,
    HealingDone, AidManaCost, BuffEffect, AidReach,
    SummonDamage, SummonLife, SummonDefense, SummonAttackRate, SummonManaCost,
    SummonDamageTaken, SummonDamageAndLife,
    MoreZen, MoreJewels, MoreItems, MoreExcellentItems, PotionHealing,  // relative: x(1 + n/100)
};

// What he must hold for the talent to count.
enum class Hold : uint8_t { Any, TwoHanded, DualWield, OneHanded, Shield, Bow };

// The majors that are behaviour rather than a number. Every one is new sim work.
enum class Special : uint8_t {
    None,
    ChillOnHit,          // `chance`% a landed blow chills for `seconds` (Frost Edge, Icy Shot)
    Executioner,         // +amount% to a monster under 30% life
    OffHand,             // +amount% on the left hand's band
    Riposte,             // a monster's miss arms +amount% on his next landed blow
    LastStand,           // -amount% taken under 30% life
    EyeOfTheStorm,       // +amount% a monster in reach, to five
    Attunement,          // +amount% a rune of `elements` worn, to five runes
    DamageProc,          // `chance`% a swing or cast: +amount% for `seconds` (and wizardry)
    KillSpeed,           // a kill: +amount attack speed for `seconds`
    Crowd,               // +amount% while three or more monsters are near
    Reach,               // `skills` reach amount tiles further
    ChainMore,           // Lightning leaps to amount more
    VsChilled,           // +amount% on chilled monsters, `skills` or every spell
    ManaBack,            // `chance`% a cast refunds its mana
    FarShot,             // +amount% at five tiles or more
    SharedLight,         // amount% of a heal on another comes back to her
    Patron,              // +amount% heals and buffs on party members
    Mercy,               // `chance`% a heal: the healed take -amount% for `seconds`
    IronWill,            // a blow of 10% of max life or more: -amount% taken for `seconds`
    WizardryIntoSwings,  // amount% of his wizardry band on weapon blows
};

constexpr uint8_t bit(Element element) { return uint8_t(1u << unsigned(element)); }

struct TalentEffect {
    TalentStat stat = TalentStat::Special;
    float amount = 0.0f;
    Hold hold = Hold::Any;
    uint8_t elements = 0;               // bit(Element), for ElementDamage and Attunement
    int32_t skills[5] = {};             // MU skill numbers, for the skill-scoped ones; 0 ends
    Special special = Special::None;
    int chance = 0;                     // percent, for the procs
    int seconds = 0;
    bool wizardry = false;              // Battle Mage: the proc lifts wizardry too
};

struct TalentNode {
    const char* name;
    const char* text;   // a small's per-point label, a major's whole line
    int16_t icon;       // MU's master-tree cell (new_Master_Icon.OZJ, number - 300)
    TalentEffect effect;
};

struct TalentColumn {
    const char* name;
    const char* sub;
    TalentNode nodes[kTalentNodes];
};

constexpr bool isMajor(int node) { return node % 3 == 2; }
constexpr int mostOf(int node) { return isMajor(node) ? 1 : kTalentSmallMost; }

#include "sim/talents_table.inc"

// A class's columns, Fortune last.
struct TalentTree {
    const TalentColumn* columns = nullptr;
    int count = 0;
};
TalentTree talentTree(Kin kin);

// What he has spent, a rank a node; columns past the class's count stay nought.
using TalentRanks = std::array<std::array<uint8_t, kTalentNodes>, kTalentColumnsMost>;

// The points earned: a tenth of the stat points his levels paid, and each reset's 400 levels again.
// `resets` waits on the reset design (memory: resets are planned, not built).
int talentPoints(Kin kin, int level, int resets);
int talentsSpent(const TalentRanks& ranks);
// Whether node `i` of a column may take a point, and give one back.
bool talentOpen(const std::array<uint8_t, kTalentNodes>& column, int i);
bool talentTakeable(const std::array<uint8_t, kTalentNodes>& column, int i);
// The whole of a save's or a request's ranks, as the realm must check them: every rank within its
// node's most, every node with points open, no column past the class's, no more spent than earned.
bool talentsValid(Kin kin, const TalentRanks& ranks, int points);

// What his ranks give to `stat`, for what he holds and the skill and element of the blow: smalls'
// amount x rank and taken majors' amount, summed. Every talent adds into one sum and the sum
// multiplies once (the plan's rule 1), as the element runes' shares do (realm_tuning.h elementForce).
struct TalentAsk {
    Hold holding[3] = {Hold::Any, Hold::Any, Hold::Any};  // what he holds that counts, Any first
    int32_t skill = 0;                                      // the blow's skill, 0 a plain swing
    Element element = Element::None;                        // the blow's (skillElement)
};
float talentSum(Kin kin, const TalentRanks& ranks, TalentStat stat, const TalentAsk& ask);
// The taken Special majors of one kind that count for `ask`, their amounts summed (0 for none).
// Several majors may share a kind -- Fire and Storm Attunement, the three Reach majors -- so this
// sums them, filtered by hold, skill and element as talentSum filters a stat. `found`, if given,
// is set to the first one, for its chance and seconds.
float talentSpecial(Kin kin, const TalentRanks& ranks, Special special, const TalentAsk& ask,
                    const TalentEffect** found = nullptr);

}  // namespace mu::sim
