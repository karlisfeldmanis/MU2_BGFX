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
}  // namespace skill

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
    // Whether it is cast on the caster and takes no target.
    bool onSelf() const { return boonTicks > 0; }
};

// How many skills the sim has room for: the knight's six, which is also the width of the save's
// learned mask and of a body's cooldown array.
constexpr int kSkills = 6;

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
