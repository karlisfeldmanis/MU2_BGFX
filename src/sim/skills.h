// The skills a character can throw: what one costs, what it does to the blow, and how long it
// is before it can be thrown again.
//
// `docs/skills-dk.md` is the argument; this is the table and the two formulas. Three layers, and
// which layer a number comes from is written beside it:
//
//   * **0.75, traced.** The mana, the clip and the knock are OpenMU's
//     `Version075/SkillsInitializer.cs:58-63`, and the multiplier's own base is
//     `ClassDarkKnight.cs:74`/`:112` -- `SkillMultiplier = 2 + energy/1000`, applied to every hit
//     that has a skill (`AttackableExtensions.cs:226-247`). A knight's skill is about twice his
//     swing, which MU2's own docs had wrong until 2026-09-22.
//   * **Ours, and marked `invention`.** The cooldown, because 0.75 has none; strength on the
//     multiplier, because the user asked for the wizard's shape with the knight's stat; and the
//     reach, because nothing here closes a gap.
//   * **Not MU's range.** Every skill is thrown at the knight's own reach. `movesToTarget` is
//     not implemented at all -- see `docs/skills-dk.md` §3.1a.
#pragma once

#include <cstdint>

#include "content/tables.h"
#include "sim/rules.h"

namespace mu::sim {

// MU's own skill numbers, so nothing here carries a magic 19.
namespace skill {
constexpr int32_t kNone = 0;
constexpr int32_t kDefense = 18;
constexpr int32_t kFallingSlash = 19;
constexpr int32_t kLunge = 20;
constexpr int32_t kUppercut = 21;
constexpr int32_t kCyclone = 22;
constexpr int32_t kSlash = 23;
// The three past 0.75, and their numbers and names are MU's own: `skill_eng.bmd`, the client's
// string table, reads 41 Twisting Slash, 42 Rageful Blow, 43 Death Stab (the decode is
// `docs/mu-scrolls-and-orbs.md` §7). 41 is 0.95d's and the other two are Season 6's -- they are
// here because §3.1b gates a skill on the weapon family and 0.75's five leave three families
// with one key each. What each one DOES here is ours; only the name, the number and the icon
// are MU's.
constexpr int32_t kTwistingSlash = 41;
constexpr int32_t kRagefulBlow = 42;
constexpr int32_t kDeathStab = 43;
}  // namespace skill

// ---- the weapon families (docs/skills-dk.md §3.1b) ------------------------------------------
//
// **A skill belongs to a kind of weapon, and is thrown with that kind or not at all.** The
// user's rule, 2026-09-23. It is not an invention so much as 0.75's own arrangement made
// explicit: in the original a knight had a skill only while he held the weapon that carried it
// (§1.2), so Falling Slash WAS an axe's blow and Slash WAS the two-hander's. Learning made a
// skill permanent and quietly threw that away; this column puts it back, and MuMain gates its
// own later skills exactly this way -- `SkillWarrior` in `GameLogic/Combat/SkillCast.cpp:145`
// refuses Impale without a spear and Spiral Slash without a sword before it spends any mana.
//
// A bit for each hand a weapon can be, and MU's own item groups are what decides which: 0
// swords, 1 axes, 2 maces, 3 the polearms. One-handed and two-handed are told apart by
// `Arm::twoHanded()`, which OpenMU keeps as the footprint's width -- a Battle Axe is two cells
// across and a Double Axe is one, and they are different weapons to swing.
namespace arms {
constexpr uint32_t kNone = 0;
constexpr uint32_t kSword1 = 1u << 0;
constexpr uint32_t kSword2 = 1u << 1;
constexpr uint32_t kAxe1 = 1u << 2;
constexpr uint32_t kAxe2 = 1u << 3;
constexpr uint32_t kMace1 = 1u << 4;
constexpr uint32_t kMace2 = 1u << 5;
// Every polearm in MU is two-handed, so the spear has one bit rather than two: Spear, Dragon
// Lance, Berdysh and Great Scythe are all two cells across.
constexpr uint32_t kSpear = 1u << 6;
// Defense's own hand, and the reason a shield is in this enumeration at all: it is the one
// "weapon family" a buff asks for, so one column answers both questions instead of two.
constexpr uint32_t kShield = 1u << 7;

constexpr uint32_t kSwords = kSword1 | kSword2;
constexpr uint32_t kAxes = kAxe1 | kAxe2;
constexpr uint32_t kMaces = kMace1 | kMace2;
constexpr uint32_t kOneHand = kSword1 | kAxe1 | kMace1;
constexpr uint32_t kTwoHand = kSword2 | kAxe2 | kMace2 | kSpear;
// Everything a knight can swing a skill with. Bows, crossbows, staves and empty hands are not
// in it, which is the rule the first pass wrote as "a blade in his hand".
constexpr uint32_t kEvery = kSwords | kAxes | kMaces | kSpear;
}  // namespace arms

// Which family a weapon is, or `arms::kNone` for a hand that throws no skill at all -- empty,
// a bow, a crossbow, a staff, a shield in the right hand. A shield is `kShield` and is found
// by the same call, because the left hand asks the same question.
uint32_t familyOf(const content::Arm* weapon);

// What that family is called, for the card and for the refusal: "a one-handed sword", "an axe".
const char* familyName(uint32_t family);

// Every family a skill may be thrown with, as words -- "Axes", "Maces"; "Two-handed swords",
// "Two-handed axes"; "Any weapon". Filled into `out` in the table's own order and the count comes
// back, because the card stacks them one under another rather than running them into a sentence:
// a value is right-aligned against its label and a long one walks over it.
int familiesNamed(uint32_t families, const char** out, int room);

// Whom a cast lands on. One target is all that is built; the two area shapes are written down
// because the cooldown state and the request are the same for them and choosing the shape later
// should not mean changing either. Ring is Cyclone -- everything within a tile of the caster --
// and Arc is Slash's three tiles off his facing.
enum class Spread : uint8_t { One, Ring, Arc };

struct SkillRow {
    int32_t number = 0;
    const char* name = "";
    // What it costs. 0.75's own column, and the client's `Skills.txt` agrees on all six.
    int32_t mana = 0;
    // How far it reaches, in tiles. OURS: 0.75 gave each skill its own range and then added two
    // on top, because the skills were gap-closers thrown from a step or two out. Nothing closes
    // a gap here, so the reach is the knight's own and the column exists to be read rather than
    // to vary.
    float reach = 1.0f;
    // The multiplier on the blow: `force + strength * forcePerStrength + energy * 0.001`.
    // `force` is 0.75's own 2 on Falling Slash and is spread around it on the other four;
    // `forcePerStrength` is 1/K_dmg and is ours.
    float force = 1.0f;
    float forcePerStrength = 0.0f;
    // The cooldown before haste, in ticks. INVENTION, all of it: 0.75 passes `cooldownMinutes`
    // on nothing, and the client's `SkillAttribute[].Delay` is a Season 6 field that is zero for
    // all six of these.
    int32_t coolTicks = 0;
    // Whether the blow shoves what it hits one tile. 0.75's `movesTarget`, kept on the three
    // single-target skills and dropped on the two area ones -- scattering a crowd is the
    // opposite of what a crowd skill is for.
    bool knock = false;
    Spread spread = Spread::One;
    // A buff's own duration in ticks and what it multiplies incoming damage by, flattened onto
    // the skill as MU2's `Rows.Skill` flattens them: 0.75 has no effect two skills share.
    int32_t boonTicks = 0;
    float damageTaken = 1.0f;
    // One line of what it does, for the tooltip. Written from the row itself -- what it hits,
    // what it is for -- in the voice `describe.cpp` uses for an item's own line.
    const char* tells = "";
    // The player library's action, which is both what the figure plays and -- because in MU a
    // swing rate IS the length of the clip it swings with -- where the cooldown's floor comes
    // from. `WSclient.cpp:4280`: SKILL_SWORD1 + (skill - FallingSlash).
    int32_t clip = 0;
    // The wave, by the key `sounds.json` carries. Cyclone and Slash share SWORD4, which is MU's
    // own reuse and not a slip here.
    const char* sound = "";
    // Whether this project has built it yet. The other five are in the table so that the bar,
    // the save's learned mask and the log all have their final shape from the first one, and so
    // that the next session adds a row's behaviour rather than a row.
    bool built = false;
    // **Which hands may throw it**, as a mask of `arms::` bits -- the column §3.1b is about.
    // For the five attacks 0.75 carried it is traced rather than chosen: the families are the
    // weapons that granted that skill in the original (`Version075/Items/Weapons.cs:93-132`,
    // and §1.2's table), so Falling Slash is the axes' and the maces' because the Morning Star,
    // the Double Axe, the Tomahawk, the Battle Axe and the Nikkea Axe were what carried it.
    // Defense is `kShield`, which is `Armors.cs:40`. The three past 0.75 are ours.
    uint32_t families = arms::kEvery;
    // **What the orb asks of him before he may read it**, in levels, and it is here to be READ
    // rather than enforced: the requirement that stops a young knight learning Slash is the orb's
    // own (`ItemRow::needLevel` and `teachesLevel`, checked in `Realm::useItem`), because a
    // requirement belongs to the thing you pick up. This column carries the same number so the
    // card can print "Learned at level 52" without going looking for an item, and so the two can
    // be checked against each other. §3.3's ladder is where both come from.
    int32_t needLevel = 0;
    // Whether it is cast on the caster and takes no target.
    bool onSelf() const { return boonTicks > 0; }
    // Whether this hand may throw it. One test, asked by the realm before it spends anything
    // and by the plate before it draws the key lit -- they must not be able to disagree.
    bool suits(uint32_t family) const { return family != arms::kNone && (families & family) != 0; }
};

// How many skills the sim has room for: the knight's six of 0.75 and the three that fill out the
// families past it. Also the width of the save's learned mask and of a body's cooldown array --
// and the learned mask is by INDEX, so a new row goes on the END of the table or an old save
// gives a knight somebody else's skill.
constexpr int kSkills = 9;

// How many bodies one area skill may catch. Nine tiles are within a spin's reach and nothing
// stands two deep on one, so this is roomy on purpose -- it is a bound so that a cast allocates
// nothing, not a rule about crowds.
constexpr int kVictims = 16;

// Half the Arc's spread, in radians: 67.5 degrees either side of where he is facing, which is
// exactly the three compass eighths of "ahead and the two diagonals beside it". Written as an
// angle rather than as three tiles because a body stands at a fractional position and a tile
// test would drop a monster straddling the line between two of them.
constexpr float kArcHalfAngle = 1.17809725f;

int skillCount();
const SkillRow& skillAt(int index);
// The row for MU's number, or null. And its index in the table, or -1, which is what a learned
// bit and a cooldown are keyed on -- not the skill number, so the arrays stay six wide.
const SkillRow* skillNumbered(int32_t number);
int skillIndexOf(int32_t number);

// ---- the two formulas (docs/skills-dk.md §3.2) --------------------------------------------

// 250 weighted points would double the casts; 300 is the tuned number and is the one knob that
// decides WHEN a build runs out of cooldown. See the anchor table in the doc.
constexpr float kAgilityPerDoubling = 300.0f;

// The multiplier on a skill's blow. 0.75's `2 + energy/1000` with strength added at the row's
// own rate, which is the whole of the user's rule: strength is force, as energy is for a wizard.
float force(const SkillRow& row, const HeroPoints& points);

// How long the clip itself takes, in ticks, at this character's attack speed -- `keys /
// ((authored + attackSpeed * 0.004) * 25)`, the same chain `sim/swings.cpp` walks for a swing.
// Zero when the cooked tables do not carry the action, in which case the caller keeps the base.
int32_t castTicks(const content::Tables& tables, Kin kin, int agility, const content::Arm* right,
                  const content::Arm* left, const SkillRow& row);

// The cooldown, in ticks: `base / (1 + agility/300)`, never shorter than `floorTicks`.
//
// A FLOOR AND NOT A ZERO, which is WoW's answer and the reason nothing here divides by zero: the
// wall is the animation, and a cooldown under it means the key is ready before the knight has
// finished swinging -- which is what "no cooldown" means in play. For a buff the floor is its own
// duration plus two seconds instead, so Defense can never be permanent half damage.
int32_t cooldownTicks(const SkillRow& row, int agility, int32_t floorTicks);

// What the floor is for this row: the clip's length for an attack, the boon's duration and two
// seconds for a buff.
int32_t floorTicksFor(const SkillRow& row, int32_t clipTicks);

}  // namespace mu::sim
