#include "sim/skills.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "sim/swings.h"

namespace mu::sim {
namespace {

// The knight's six, in MU's own order, and then the three that fill out the weapon families.
// The mana column is 0.75's on the first six and is corroborated by the client's own
// `Skills.txt` -- 30, 9, 9, 8, 9, 10, two independent readings agreeing.
//
// **The order of this table is the save's**, because `learned` is a bit per INDEX: the six come
// first and forever, and anything new is appended. A row inserted in the middle would hand a
// saved knight a skill he never learned.
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
     "A guard raised behind the shield: half of every blow that lands, for four seconds.", 187,
     "player_skill_defense", true, arms::kShield, 6},
    // The knock is OFF on all six, and the column is kept rather than removed. 0.75 sets
    // `movesTarget` on the knight's five and it puts the monster on a neighbouring tile at once --
    // which is a one-tile teleport, and the user's rule of 2026-09-22 ("no sudden position changes
    // or teleports") is against exactly that. It comes back the day something can SHOVE a body
    // over a few ticks instead of moving it; until then `knock` stays false and `Realm::shove` is
    // written and unreached.
    //
    // **The families are 0.75's own carriers** (§1.2, `Version075/Items/Weapons.cs:93-132`):
    // whichever weapons granted a skill in the original are the weapons that may throw it here.
    // Not one of the five is a choice made at this desk.
    //
    // Falling Slash: the Morning Star (a mace), the Double Axe and the Tomahawk (one-handed),
    // the Battle Axe and the Nikkea Axe (two-handed). Axes and maces, in either hand -- and the
    // icon MU painted for it is an axe, which is the same fact from the artist's side.
    {skill::kFallingSlash, "Falling Slash", 9, 1.0f, 2.0f, 1.0f / 1000.0f, 80, false, Spread::One,
     0, 1.0f, "An overhead blow brought down on one body -- the heaviest single strike he has.",
     60, "player_skill_sword1", true, arms::kAxes | arms::kMaces, 13},
    // Lunge: the Gladius, and nothing else in 0.75 carried it. A one-handed sword, which is what
    // MU's own description says in as many words -- "used with weapons like Gladius and Katana
    // to execute quick stabs".
    {skill::kLunge, "Lunge", 9, 1.0f, 1.4f, 1.0f / 1400.0f, 60, false, Spread::One, 0, 1.0f,
     "A thrust straight ahead: the cheapest key and the one that comes back soonest.", 61,
     "player_skill_sword2", true, arms::kSword1, 20},
    // Uppercut: the Sword of Assassin, the Falchion and the Serpent Sword. All one-handed.
    {skill::kUppercut, "Uppercut", 8, 1.0f, 1.7f, 1.0f / 1200.0f, 60, false, Spread::One, 0, 1.0f,
     "A rising blow under the guard, between the jab and the overhead in force and in wait.",
     62, "player_skill_sword3", true, arms::kSword1, 12},
    // Cyclone: the Blade (a one-handed sword), the Berdysh and the Great Scythe (polearms). The
    // odd pair is MU's own, and it reads as a spin with something long or something quick.
    {skill::kCyclone, "Cyclone", 9, 1.0f, 1.3f, 1.0f / 1400.0f, 100, false, Spread::Ring, 0, 1.0f,
     "A spin that catches everything within a tile: weakest against one, strongest in a crowd.",
     63, "player_skill_sword4", true, arms::kSword1 | arms::kSpear, 36},
    // Every row is built now. `built` used to mean "and the bar has a key for it", which is why
    // Slash sat here false with its arc written and tested; the list of 2026-09-23 holds every
    // learned skill and the four keys are the player's to fill from it, so the two questions
    // came apart and this one is the simple one again.
    // Slash: the Giant Sword, the Crystal Sword and the Chaos Dragon Axe -- two two-handed
    // swords and a two-handed axe, which is exactly what MU's own description says ("only works
    // with the Giant Sword, Chaos Dragon Axe, or Crystal Sword"). Of those three Lorencia cooks
    // the Giant Sword; the other two are elsewhere and the family is written for them anyway.
    {skill::kSlash, "Slash", 10, 1.0f, 1.8f, 1.0f / 1000.0f, 120, false, Spread::Arc, 0, 1.0f,
     "A wide sweep across the three tiles he faces, thrown with both hands on the haft.", 64,
     "player_skill_sword4", true, arms::kSword2 | arms::kAxe2, 52},

    // ---- past 0.75, and appended so the six keep their indices -------------------------------
    //
    // **Ours, in everything but the name.** Gating the five above on their own carriers leaves a
    // one-handed axe, a mace and a spear with a single key each, which is a dead bar -- so three
    // more rows, each one a knight skill MU itself went on to write (41 in 0.95d, 42 and 43 in
    // Season 6; `skill_eng.bmd` is where the numbers and the names come from). What they DO is
    // this project's, in §3.2's shape: no OpenMU row for any of them exists in 0.75 to be
    // followed, and inventing one and calling it traced would be worse than saying this.
    //
    // Their clips are 0.75's five, reused. MuMain plays Twisting Slash on its own action and
    // this library does not have it, so the nearest blow is played instead and the note is here
    // rather than in the drawing: the sword-spin for the spin, the overhead for the crush, the
    // thrust for the stab.
    //
    // Twisting Slash is the one skill of the three MU puts no weapon requirement on at all --
    // it is the knight's staple, "whirl his weapon violently around him" -- so it is the one row
    // here that every family may throw, and it is what keeps the mace and the spear from having
    // a bar of two. Its mana is MU's: MuMain's own orb tooltip reads `Twisting Slash Skill
    // (Mana:22)` (docs/mu-scrolls-and-orbs.md §5).
    {skill::kTwistingSlash, "Twisting Slash", 22, 1.0f, 1.2f, 1.0f / 1500.0f, 90, false,
     Spread::Ring, 0, 1.0f,
     "A whirl of whatever he is holding, into everything within a tile. Every weapon can throw "
     "it; none throws it hard.",
     63, "player_skill_sword4", true, arms::kEvery, 28},
    // Rageful Blow: **any weapon**, on the user's word of 2026-09-23. It was written for the
    // heavy hands -- MU's description is a "colossal area attack that unleashes shockwaves ... to
    // crush multiple opponents", which reads as something brought DOWN rather than drawn across --
    // and it is opened to all six families for the same reason Twisting Slash is: MU puts no
    // weapon requirement on either, and a knight of any hand should have a heavy answer as well as
    // a wide one. So the two skills past 0.75 that every family shares are the pair, spin and
    // crush, and every gate in the table below them is traced to a carrier.
    {skill::kRagefulBlow, "Rageful Blow", 25, 1.0f, 2.1f, 1.0f / 900.0f, 170, false, Spread::Arc,
     0, 1.0f,
     "The weapon driven down into the ground, and what it breaks is the three tiles ahead of "
     "him.",
     60, "player_skill_sword1", true, arms::kEvery, 44},
    // Death Stab: the spear's, and MU gates it on the hand too -- `SkillWarrior` refuses it with
    // a staff in the right hand (SkillCast.cpp:157) and the skill has been a spear's in every
    // version that hands it out. The hardest single blow in the table, and the point of carrying
    // a polearm: a spear's answer to Falling Slash, which it may not throw.
    {skill::kDeathStab, "Death Stab", 15, 1.0f, 2.3f, 1.0f / 900.0f, 110, false, Spread::One, 0,
     1.0f, "The point driven through one body at speed. Nothing he has hits one thing harder.",
     61, "player_skill_sword2", true, arms::kSpear, 60},
};

// The energy term is 0.75's own and is kept rather than replaced: a knight who spends on energy
// still gets his tenth of a percent a point, as he does in the original.
constexpr float kForcePerEnergy = 0.001f;

// Two seconds over a boon's duration, so half damage always has a gap in it.
constexpr int32_t kBoonGapTicks = 40;

}  // namespace

// ---- the families (docs/skills-dk.md §3.1b) -------------------------------------------------

uint32_t familyOf(const content::Arm* weapon) {
    if (!weapon) return arms::kNone;
    if (weapon->isShield()) return arms::kShield;
    // A bow, a crossbow and a staff throw none of these: the five 0.75 clips are
    // `PLAYER_ATTACK_SKILL_SWORD1..5` and the streak MU lays on them is a blade's. The missile
    // test comes first because a quiver is in group 4 with the bows.
    if (weapon->missile()) return arms::kNone;
    const bool both = weapon->twoHanded();
    switch (weapon->group) {
        case 0: return both ? arms::kSword2 : arms::kSword1;
        case 1: return both ? arms::kAxe2 : arms::kAxe1;
        case 2: return both ? arms::kMace2 : arms::kMace1;
        // Every polearm in MU is two-handed, so there is one bit and no branch. A row that says
        // otherwise is a transcription error and would land here as a spear anyway, which is the
        // safe way round: it is what the weapon IS.
        case 3: return arms::kSpear;
        default: return arms::kNone;  // 4 the bows, 5 the staves, and anything uncooked
    }
}

const char* familyName(uint32_t family) {
    switch (family) {
        case arms::kSword1: return "a one-handed sword";
        case arms::kSword2: return "a two-handed sword";
        case arms::kAxe1: return "a one-handed axe";
        case arms::kAxe2: return "a two-handed axe";
        case arms::kMace1: return "a mace";
        case arms::kMace2: return "a two-handed mace";
        case arms::kSpear: return "a spear";
        case arms::kShield: return "a shield";
        default: return "";
    }
}

int familiesNamed(uint32_t families, const char** out, int room) {
    int found = 0;
    const auto add = [&](const char* word) { if (found < room) out[found++] = word; };
    if (families == arms::kShield) {
        add("A shield");
        return found;
    }
    if ((families & arms::kEvery) == arms::kEvery) {
        add("Any weapon");
        return found;
    }
    // A family whose two hands are both in the mask is said once and without the hand, which is
    // what makes one word "Axes" out of two bits. Each word is a line of its own on the card, so
    // this is a list and not a sentence -- no "and", no commas, nothing to wrap.
    const uint32_t swords = families & arms::kSwords;
    if (swords == arms::kSwords) add("Swords");
    else if (swords == arms::kSword1) add("One-handed swords");
    else if (swords == arms::kSword2) add("Two-handed swords");
    const uint32_t axes = families & arms::kAxes;
    if (axes == arms::kAxes) add("Axes");
    else if (axes == arms::kAxe1) add("One-handed axes");
    else if (axes == arms::kAxe2) add("Two-handed axes");
    const uint32_t maces = families & arms::kMaces;
    if (maces == arms::kMace2 && (families & arms::kMace1) == 0) add("Two-handed maces");
    else if (maces != 0) add("Maces");  // no two-handed mace is cooked; one word for the pair
    if ((families & arms::kSpear) != 0) add("Spears");
    return found;
}

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
