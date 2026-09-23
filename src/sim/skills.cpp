#include "sim/skills.h"

#include <algorithm>
#include <cmath>

#include "sim/swings.h"

namespace mu::sim {
namespace {

// The knight's six, in MU's own order. The mana column is 0.75's and is corroborated by the
// client's own `Skills.txt` -- 30, 9, 9, 8, 9, 10, two independent readings agreeing.
//
// The three invented columns, and the reasoning is `docs/skills-dk.md` §3.2:
//   * `force` spreads around Falling Slash's 2.0, which is the class's own multiplier in 0.75.
//     The two area skills are paid in coverage rather than in force, so Cyclone's 1.3 is the
//     weakest key against one monster and the strongest against four.
//   * `forcePerStrength` is 1/K_dmg: 1/1000 on the anchor, gentler on the jab, steeper on the
//     two-handed sweep.
//   * `coolTicks` is at 20 Hz: 60 is three seconds.
constexpr SkillRow kRows[kSkills] = {
    // Defense 18: a buff, four seconds of half damage for 30 mana
    // (`DefenseEffectInitializer`). Twelve seconds of cooldown, floored at its own duration and
    // two, so it is never permanent.
    {skill::kDefense, "Defense", 30, 0.0f, 1.0f, 0.0f, 240, false, Spread::One, 80, 0.50f,
     "A guard raised where he stands: half of every blow that lands, for four seconds.", 187,
     "player_skill_defense", false},
    // The knock is OFF on all six, and the column is kept rather than removed. 0.75 sets
    // `movesTarget` on the knight's five and it puts the monster on a neighbouring tile at once --
    // which is a one-tile teleport, and the user's rule of 2026-09-22 ("no sudden position changes
    // or teleports") is against exactly that. It comes back the day something can SHOVE a body
    // over a few ticks instead of moving it; until then `knock` stays false and `Realm::shove` is
    // written and unreached.
    {skill::kFallingSlash, "Falling Slash", 9, 1.0f, 2.0f, 1.0f / 1000.0f, 80, false, Spread::One,
     0, 1.0f, "An overhead blow brought down on one body -- the heaviest single strike he has.",
     60, "player_skill_sword1", true},
    {skill::kLunge, "Lunge", 9, 1.0f, 1.4f, 1.0f / 1400.0f, 60, false, Spread::One, 0, 1.0f,
     "A thrust straight ahead: the cheapest key and the one that comes back soonest.", 61,
     "player_skill_sword2", true},
    {skill::kUppercut, "Uppercut", 8, 1.0f, 1.7f, 1.0f / 1200.0f, 60, false, Spread::One, 0, 1.0f,
     "A rising blow under the guard, between the jab and the overhead in force and in wait.",
     62, "player_skill_sword3", true},
    {skill::kCyclone, "Cyclone", 9, 1.0f, 1.3f, 1.0f / 1400.0f, 100, false, Spread::Ring, 0, 1.0f,
     "A spin that catches everything within a tile: weakest against one, strongest in a crowd.",
     63, "player_skill_sword4", true},
    // Slash's shape IS built -- `Spread::Arc` is written, tested and is the same sweep Cyclone's
    // ring is cut from -- and the row is `false` for one reason only: the bar holds four keys and
    // the four above it fill them. It is one word here the day a learned-skills list can drag a
    // fifth onto Q W E R.
    {skill::kSlash, "Slash", 10, 1.0f, 1.8f, 1.0f / 1000.0f, 120, false, Spread::Arc, 0, 1.0f,
     "A wide sweep across the three tiles he faces. The longest wait of the six.", 64,
     "player_skill_sword4", false},
};

// The energy term is 0.75's own and is kept rather than replaced: a knight who spends on energy
// still gets his tenth of a percent a point, as he does in the original.
constexpr float kForcePerEnergy = 0.001f;

// Two seconds over a boon's duration, so half damage always has a gap in it.
constexpr int32_t kBoonGapTicks = 40;

}  // namespace

int skillCount() { return kSkills; }

const SkillRow& skillAt(int index) {
    return kRows[size_t(std::clamp(index, 0, kSkills - 1))];
}

const SkillRow* skillNumbered(int32_t number) {
    for (const SkillRow& row : kRows) {
        if (row.number == number) return &row;
    }
    return nullptr;
}

int skillIndexOf(int32_t number) {
    for (int i = 0; i < kSkills; ++i) {
        if (kRows[i].number == number) return i;
    }
    return -1;
}

float force(const SkillRow& row, const HeroPoints& points) {
    return row.force + float(points.strength) * row.forcePerStrength +
           float(points.energy) * kForcePerEnergy;
}

int32_t castTicks(const content::Tables& tables, Kin kin, int agility, const content::Arm* right,
                  const content::Arm* left, const SkillRow& row) {
    const content::PlayerAction* clip = tables.action(row.clip);
    if (!clip || clip->keys <= 0) return 0;
    // The attack speed's own term, and it is the ATTACK one rather than the magic one: 60 to 64
    // are `PLAYER_ATTACK_SKILL_SWORD*` and fall on `SetAttackSpeed`'s attack branch, where a
    // wizard's four cast clips read MagicSpeed instead. MU2 got this wrong the other way round
    // once and a staff threw spells a fifth too fast.
    const float bonus = attackSpeedStat(kin, agility, right, left) * 0.004f;
    const float rate = (clip->speed + bonus) * 25.0f;
    if (rate <= 0.0f) return 0;
    return swingTicks(int(float(clip->keys) / rate * 1000.0f));
}

int32_t floorTicksFor(const SkillRow& row, int32_t clipTicks) {
    if (row.boonTicks > 0) return row.boonTicks + kBoonGapTicks;
    return std::max<int32_t>(1, clipTicks);
}

int32_t cooldownTicks(const SkillRow& row, int agility, int32_t floorTicks) {
    if (row.coolTicks <= 0) return std::max<int32_t>(0, floorTicks);
    const float haste = float(std::max(0, agility)) / kAgilityPerDoubling;
    const int32_t hasted = int32_t(std::lround(float(row.coolTicks) / (1.0f + haste)));
    return std::max(std::max<int32_t>(1, floorTicks), hasted);
}

}  // namespace mu::sim
