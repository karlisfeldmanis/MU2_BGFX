// Throwing a skill: what a key press asks for, the refusals in OpenMU's own order, the cooldown
// it sets, and the knock at the far end.
//
// `docs/skills-dk.md` is the design and `sim/skills.cpp` is the table. Three things about the
// shape are worth having at the top, because each of them was a decision:
//
//   * **A press is not an order.** `invoke` remembers a wish; it never replaces the standing
//     attack order. So the knight opens on auto-attack, presses a key, spends that swing on the
//     skill and goes on swinging the same monster -- which is the fight the user described and is
//     why nothing here touches `order_`.
//   * **A cast pays the swing timer as well as its own cooldown.** The swing clock is the length
//     of the clip he swings with (`sim/swings.cpp`), so it is the wall that stops any amount of
//     haste firing a skill inside its own animation. MU2's `Realm.Cast` takes the longer of the
//     two for the same reason and its remark is the argument.
//   * **Every refusal is silent.** The interface asks and redraws from the realm; a no is a
//     message that does not come back. `Swing` and `Move` already work that way.
#include "sim/realm.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "sim/realm_tuning.h"
#include "sim/skills.h"
#include "sim/swings.h"

namespace mu::sim {
namespace {

// How long a press is held for before it is forgotten: a little over a swing, so a key pressed
// on any frame of the current blow is thrown by the one after it. **invention**, and the number
// is small on purpose -- a wish held for a second is a knight who casts at something he stopped
// aiming at. MU2 keeps its `Wants` on the same argument and bounds it by the swing instead.
constexpr int64_t kWishTicks = 30;

}  // namespace

void Realm::invoke(int32_t skill, uint32_t at) {
    const Body& hero = bodies_[0];
    if (!hero.alive()) return;
    if (skillIndexOf(skill) < 0) return;
    wants_ = skill;
    wantsAt_ = at;
    wantsUntil_ = tick_ + kWishTicks;
}

bool Realm::learn(int32_t skill) {
    const int index = skillIndexOf(skill);
    if (index < 0) return false;
    Body& hero = bodies_[0];
    const uint32_t bit = uint32_t(1) << index;
    if ((hero.learned & bit) != 0) return false;
    hero.learned |= bit;
    say(What::Learned, hero, skill);
    return true;
}

bool Realm::knows(int32_t skill) const {
    const int index = skillIndexOf(skill);
    if (index < 0) return false;
    return (bodies_[0].learned & (uint32_t(1) << index)) != 0;
}

int64_t Realm::cooling(int32_t skill) const {
    const int index = skillIndexOf(skill);
    if (index < 0) return 0;
    return std::max<int64_t>(0, bodies_[0].cools[index] - tick_);
}

int32_t Realm::coolsFor(int32_t skill) const {
    const SkillRow* row = skillNumbered(skill);
    if (!row) return 0;
    const Body& hero = bodies_[0];
    return cooldownTicks(*row, hero.points.agility, floorTicksFor(*row, clipTicksOf(hero, *row)));
}

int32_t Realm::clipTicksOf(const Body& hero, const SkillRow& row) const {
    if (!tables_) return 0;
    // The same two hands `reswing` reads, for the same reason: the play speed a clip runs at has
    // the weapon's own attack speed in it.
    const content::Arm* right = hero.weapon >= 0 && size_t(hero.weapon) < tables_->arms.size()
                                    ? &tables_->arms[size_t(hero.weapon)]
                                    : nullptr;
    const content::Arm* left = hero.shield >= 0 && size_t(hero.shield) < tables_->arms.size()
                                   ? &tables_->arms[size_t(hero.shield)]
                                   : nullptr;
    return castTicks(*tables_, hero.kin, hero.points.agility, right, left, row);
}

// The refusals, in the order `TargetedSkillDefaultPlugin` refuses them, with the two this
// design changes marked. The order is behaviour and not presentation: mana is taken after the
// reach test and before the blow, so a knight who is too far away has not paid for the press.
bool Realm::throwSkill(Body& hero, const SkillRow& row, uint32_t at) {
    const int index = skillIndexOf(row.number);
    if (index < 0) return false;

    // Learned. In 0.75 this question was asked of his hands; here it is asked of what he has
    // read, which is the one place the design leaves the original on purpose.
    if ((hero.learned & (uint32_t(1) << index)) == 0) return false;

    // The cooldown, which is ours and has no counterpart in 0.75. A wait rather than a refusal,
    // exactly as the swing timer is: the key does nothing and says nothing.
    if (tick_ < hero.cools[size_t(index)]) return false;

    // **Nothing is thrown bare-handed**, and nothing is thrown off a bow. A knight's five are
    // sword swings -- the clips are `PLAYER_ATTACK_SKILL_SWORD1..5` and the streak MU lays on
    // them is a blade's -- so a man with empty hands has no skill to throw, whatever he has
    // learned. The user's rule, 2026-09-22, and it is ours rather than 0.75's only in where it
    // is written down: in the original the weapon WAS the skill, so "no weapon, no skill" was
    // true by construction. Learning made it possible to keep a skill with nothing in hand, and
    // this is the line that says that is not a knight.
    if (!row.onSelf()) {
        const content::Arm* weapon = hero.weapon >= 0 &&
                                             size_t(hero.weapon) < tables_->arms.size()
                                         ? &tables_->arms[size_t(hero.weapon)]
                                         : nullptr;
        if (!weapon || weapon->isShield() || weapon->bow() || weapon->crossbow()) return false;
    }

    // `player.IsAtSafezone()` refuses everything, buffs included -- so a knight cannot even
    // raise his guard in Lorencia's square. The same rule `press` already applies to a swing.
    if (tables_->grid.safe(hero.column(), hero.row())) return false;

    if (row.onSelf()) {
        // **And the guard needs a shield on the arm.** The user's rule, 2026-09-22, and it is
        // 0.75's own arrangement put back: skill 18 is carried by the Buckler and the nine
        // shields after it (`Version075/Items/Armors.cs:40`), so in the original a knight without
        // one simply did not have Defense. Learning it permanently took that away, and this is
        // the line that gives it back -- the same shape as the weapon test above, one hand each.
        const content::Arm* shield = hero.shield >= 0 &&
                                             size_t(hero.shield) < tables_->arms.size()
                                         ? &tables_->arms[size_t(hero.shield)]
                                         : nullptr;
        if (!shield || !shield->isShield()) return false;
        // A self-cast has no target to be far from and reads its victim off the caster.
        if (hero.mana < row.mana) return false;
        hero.mana -= row.mana;
        hero.boonSkill = row.number;
        hero.boonDamageTaken = row.damageTaken;
        hero.boonUntil = tick_ + row.boonTicks;
        hero.stats.damageTaken = double(row.damageTaken);
    } else {
        Body* target = body(at);
        if (!target || !target->alive() || target->player) return false;
        // The reach, and it is the knight's own. 0.75 gave each skill a range and then added two
        // tiles of slack because three of the five were thrown from a step or two out and closed
        // the gap themselves; nothing closes a gap here (docs/skills-dk.md §3.1a), so the test
        // is the swing's.
        if (!within(hero, *target, row.reach)) return false;
        // Nothing may be thrown at something sheltered either, which is the check the far end of
        // `ApplySkillAsync` makes and `press` already makes for a swing.
        if (tables_->grid.safe(target->column(), target->row())) return false;
        if (hero.mana < row.mana) return false;
        hero.mana -= row.mana;
    }

    const int32_t cool =
        cooldownTicks(row, hero.points.agility, floorTicksFor(row, clipTicksOf(hero, row)));
    hero.cools[size_t(index)] = tick_ + cool;
    // And the swing clock, the longer of his own rhythm and the clip this skill plays. Without
    // the second half a skill whose animation outlasts the weapon's swing is cut off by the next
    // blow -- MU2 measured exactly that on Defense, whose clip is the longest of the six.
    const int32_t clip = clipTicksOf(hero, row);
    hero.swingsAt = tick_ + std::max(hero.swingTicks, clip);
    // And he is locked where he stands for the length of the animation: no turn, no re-path, no
    // step. `press` reads this before it engages, so a quarry that shuffles round him does not
    // spin the body mid-swing.
    hero.castUntil = tick_ + clip;
    hero.walking = false;
    hero.route.clear();
    hero.onStep = 0;
    // **And the auto-attack stops.** The user's rule, 2026-09-22, and it reverses what the first
    // pass did: a cast used to leave the standing order alone so the knight went on swinging
    // afterwards. He does not -- a skill ends the exchange, and going back to hitting the monster
    // is another click. MU2's `Realm.Cast` does the same to its wizard (`player.Fighting = 0`) and
    // argued the opposite for the knight; this is the user's call and it is one line either way.
    // `pending_` is left alone: a click the player has already made this tick is his, not ours.
    order_ = Request{};

    // Said BEFORE the blow, so the drawing has the skill in hand when the hit arrives and can
    // play the skill's clip instead of the weapon's. MU2 moved its own herald above the blow for
    // this reason and marked the departure; the sparks could not otherwise know a cast happened.
    say(What::Cast, hero, row.number, cool, 0, row.onSelf() ? hero.id : at);

    if (!row.onSelf()) {
        // Begun rather than landed: the blow settles halfway through the clip, as a swing's does
        // (Realm::begin), so what is drawn and what is dealt are the same moment. The multiplier
        // rides with it -- 0.75's own `SkillMultiplier` with strength on it, §3.2 -- and the
        // knock, when a skill has one, belongs to the landing too and is why `shove` is reached
        // from there rather than here.
        begin(hero, at, force(row, hero.points), row.number, clip);
    }
    return true;
}

void Realm::shove(Body& target) {
    // One tile at random, and only onto something standable: OpenMU's `MoveRandomlyAsync` picks
    // a neighbour and a blocked one is simply not taken, which is what the grid test is. The
    // draw happens whether or not the tile is free, so the seeded log does not depend on the
    // map's walls for its NUMBER of draws -- only for the outcome.
    const int step = dice_.nextInt(0, 8);
    static const int kAround[8][2] = {{0, -1}, {1, -1}, {1, 0},  {1, 1},
                                      {0, 1},  {-1, 1}, {-1, 0}, {-1, -1}};
    const int column = target.column() + kAround[step][0];
    const int row = target.row() + kAround[step][1];
    if (!tables_->grid.open(column, row, content::kWallCharacter)) return;
    target.x = float(column);
    target.y = float(row);
    // The walk it was on is void: it has been put somewhere its route does not start from.
    target.walking = false;
    target.route.clear();
    target.onStep = 0;
    say(What::Shoved, target, column, row);
}

}  // namespace mu::sim
