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

// A Teleport's fade out, in ticks: MU takes a tenth off the body's alpha a frame
// (ZzzInterface.cpp:2603), ten frames, 0.4 s. He is put down when it has run.
constexpr int64_t kBlinkFadeTicks = 8;
// And how long after he is put down before he may act again: the first of the fade back in.
// Ours -- MU plays the clip's last eight keys, a second, and a blink held that long read slow.
constexpr int64_t kBlinkSettleTicks = 4;


}  // namespace

void Realm::invoke(int32_t skill, uint32_t at) {
    const Body& hero = bodies_[0];
    if (!hero.alive()) return;
    if (skillIndexOf(skill) < 0) return;
    wants_ = skill;
    wantsAt_ = at;
    wantsColumn_ = wantsRow_ = -1;
    wantsUntil_ = tick_ + kWishTicks;
}

void Realm::invokeAt(int32_t skill, int column, int row) {
    invoke(skill, 0);
    wantsColumn_ = column;
    wantsRow_ = row;
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
    return castTicks(*tables_, hero.kin, hero.points.agility, right, left, row,
                     hero.frenzyUntil > tick_ ? kFrenzySpeed : 0);
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
    // And his class's. Learning already asks it -- an orb or a scroll refuses the wrong class --
    // so this is the same answer asked again where the skill is spent.
    if (row.kin != hero.kin) return false;

    // The cooldown, which is ours and has no counterpart in 0.75. A wait rather than a refusal,
    // exactly as the swing timer is: the key does nothing and says nothing.
    if (tick_ < hero.cools[size_t(index)]) return false;

    // **The hand, and now it is the RIGHT hand rather than any hand.** The user's rule of
    // 2026-09-22 was "nothing is thrown bare-handed, nothing off a bow"; the rule of 2026-09-23
    // is narrower and is the reason this project has a `families` column at all -- a skill
    // belongs to a kind of weapon and is thrown with that kind or not at all (§3.1b). For the
    // five 0.75 carried this is the original restored rather than a new gate: in 0.75 the weapon
    // WAS the skill, so an axe's Falling Slash could never be thrown off a sword; learning made
    // it possible and this line takes it back.
    //
    // Empty hands, a bow, a crossbow, a staff and a shield in the right hand are all
    // `arms::kNone`, so the old rule falls out of the new one rather than sitting beside it.
    if (!row.onSelf()) {
        const content::Arm* weapon = hero.weapon >= 0 &&
                                             size_t(hero.weapon) < tables_->arms.size()
                                         ? &tables_->arms[size_t(hero.weapon)]
                                         : nullptr;
        if (!row.suits(familyOf(weapon))) return false;
    }

    // `player.IsAtSafezone()` refuses everything, buffs included -- so a knight cannot even
    // raise his guard in Lorencia's square. The same rule `press` already applies to a swing.
    if (tables_->grid.safe(hero.column(), hero.row())) return false;

    // **A blink** (Teleport): no body, no blow -- the ground the key named, a fade, and him put
    // down there. The fight he was in is dropped, as a Town Portal drops it: left standing, the
    // order walked him straight back to what he had just left.
    if (row.blinks) {
        int column = 0, where = 0;
        if (wantsColumn_ < 0 ||
            !blinkTo(hero, row, wantsColumn_, wantsRow_, &column, &where)) {
            return false;
        }
        if (hero.mana < row.mana) return false;
        hero.mana -= row.mana;
        const int32_t cool = cooldownTicks(row, hero.points.agility,
                                           floorTicksFor(row, clipTicksOf(hero, row)));
        hero.cools[size_t(index)] = tick_ + cool;
        hero.blinkAt = tick_ + kBlinkFadeTicks;
        hero.blinkColumn = column;
        hero.blinkRow = where;
        hero.swingsAt = hero.castUntil = hero.blinkAt + kBlinkSettleTicks;
        hero.aim = std::atan2(float(where) - hero.y, float(column) - hero.x);
        hero.walking = false;
        hero.route.clear();
        hero.onStep = 0;
        dropBlow(hero);
        order_ = Request{};
        pending_ = Request{};
        rise(hero);
        say(What::Cast, hero, row.number, cool, 0, hero.id);
        return true;
    }

    if (row.onSelf()) {
        // **And the guard needs a shield on the arm** -- the wizard's Soul Barrier as much as
        // the knight's Defense, the user's of 2026-09-28. The user's rule, 2026-09-22, and it is
        // 0.75's own arrangement put back: skill 18 is carried by the Buckler and the nine
        // shields after it (`Version075/Items/Armors.cs:40`), so in the original a knight without
        // one simply did not have Defense. Learning it permanently took that away, and this is
        // the line that gives it back -- the same shape as the weapon test above, one hand each.
        const content::Arm* shield = hero.shield >= 0 &&
                                             size_t(hero.shield) < tables_->arms.size()
                                         ? &tables_->arms[size_t(hero.shield)]
                                         : nullptr;
        // Asked through the same column as an attack's, because `arms::kShield` is a family:
        // one question -- does this hand suit this row -- rather than two rules that can drift.
        if (!row.suits(familyOf(shield))) return false;
        // A self-cast has no target to be far from and reads its victim off the caster.
        if (hero.mana < row.mana) return false;
        hero.mana -= row.mana;
        if (row.summons > 0) {
            // **A second cast dismisses the one standing**, and costs nothing: OpenMU's
            // TargetedSkillDefaultPlugin.cs:121-125, which removes the summon and returns before
            // the mana is taken. The mana was taken above, so it is handed back.
            Body& summon = bodies_[size_t(summonSlot_)];
            if (summon.alive()) {
                hero.mana += row.mana;
                dismiss(summon);
                return true;
            }
            if (!conjure(hero, row)) {
                hero.mana += row.mana;
                return false;
            }
        } else if (row.mends) {
            // Heal: health back at once, never past the most he has.
            hero.health = std::min(hero.maxHealth, hero.health + healOf(hero.points));
        } else if (row.mightTicks > 0) {
            // Greater Damage: reckoned off her energy now and held for the minute; a second cast
            // replaces the first rather than stacking, as MU's magic effects do.
            hero.might = mightOf(hero.points);
            hero.mightUntil = tick_ + row.mightTicks;
            rearm(hero);
        } else {
            hero.boonSkill = row.number;
            // The guard's share off his shield and stats as they stand at the cast, held for its
            // whole length: `guardShare` in sim/skills.h, where the numbers are argued -- or the
            // wizard's `barrierShare`, off energy where the knight's is off his body, or the
            // elf's `wardShare`, off agility with no shield at all.
            hero.boonDamageTaken = 1.0f - boonShare(row, hero.points, hero.shieldDefense);
            hero.boonUntil = tick_ + row.boonTicks;
            hero.stats.damageTaken = double(hero.boonDamageTaken) * hero.pet.taken;
        }
    } else {
        Body* target = body(at);
        // The reach, and it is the knight's own. 0.75 gave each skill a range and then added two
        // tiles of slack because three of the five were thrown from a step or two out and closed
        // the gap themselves; nothing closes a gap here (docs/skills-dk.md §3.1a), so the test
        // is the swing's.
        const bool aimed = target && target->alive() && target->monster() &&
                           within(hero, *target, row.reach);
        // Aimed before the shape is measured, because Arc is measured off where he is looking.
        // Only the aim is set and not the facing: he turns to it at the body's own rate, as he
        // does for a swing (`engage`), and the blow lands half a clip later by which time he has
        // come round. Set even on a refusal below -- a knight turns toward what he tried to hit.
        if (aimed) hero.aim = std::atan2(target->y - hero.y, target->x - hero.x);
        // **A spell turns him at once.** A wizard's clip is cast from the tick it starts, and
        // left to the turn a body coming round from behind began it facing away -- 150 degrees
        // off, the user's "goes to the opposite direction", on two casts in 160 of a hunt. MU
        // snaps the angle onto the target as it attacks (ZzzInterface.cpp:1303, `o->Angle[2] =
        // CreateAngle2D`); the knight's swing keeps its turn, as above.
        if (aimed && row.wizardry) hero.facing = hero.aim;
        if (row.spread == Spread::One || row.spread == Spread::Line ||
            row.spread == Spread::Fan) {
            // A line is thrown AT a body as a single blow is, and goes on through: it needs the
            // body to aim by, and the rest of its way is found when it is let go.
            if (!aimed) return false;
            // Nothing may be thrown at something sheltered either, which is the check the far end
            // of `ApplySkillAsync` makes and `press` already makes for a swing.
            if (tables_->grid.safe(target->column(), target->row())) return false;
        } else {
            // An area skill is not thrown AT a body, it is thrown AROUND him, so what it needs is
            // somebody inside the shape rather than a named target: a knight whose quarry has
            // just died still spins into the three others standing on him. What it will not do is
            // spend mana and a cooldown on empty air, which is what 0.75's "no target, no skill"
            // is really refusing.
            uint32_t victims[kVictims];
            if (gather(hero, row, victims, kVictims) == 0) return false;
        }
        // A fan is arrows: none in hand or in the bag and there is nothing to loose.
        if (row.arrows > 0 && !quivered(hero)) return false;
        if (hero.mana < row.mana) return false;
        hero.mana -= row.mana;
    }

    // A primary has no cooldown at all -- its clip is its pace, and a key's wipe running down
    // over every Energy Ball would say there was a wait where there is none.
    const int32_t cool =
        row.primary() ? 0
                      : cooldownTicks(row, hero.points.agility,
                                      floorTicksFor(row, clipTicksOf(hero, row)));
    hero.cools[size_t(index)] = tick_ + cool;
    // And the swing clock, the longer of his own rhythm and the clip this skill plays. Without
    // the second half a skill whose animation outlasts the weapon's swing is cut off by the next
    // blow -- MU2 measured exactly that on Defense, whose clip is the longest of the six.
    //
    // A spell pays its own clip and not the weapon's: its rhythm is MagicSpeed's and has nothing
    // to do with how fast the staff in his hand would swing.
    const int32_t clip = clipTicksOf(hero, row);
    hero.swingsAt = tick_ + (row.channelled() ? row.channelTicks
                             : row.wizardry  ? std::max<int32_t>(1, clip)
                                             : std::max(hero.swingTicks, clip));
    // And he is locked where he stands for the length of the animation: no turn, no re-path, no
    // step. `press` reads this before it engages, so a quarry that shuffles round him does not
    // spin the body mid-swing.
    //
    // **Not a primary.** The wizard's Energy Ball is thrown over and over, and a lock on every
    // clip would be a wizard who can never be walked away from a fight. Like a swing it is
    // cancelled by a click until it leaves his hand; after that it is in the air and lands.
    // A channel first, primary or not: Lightning has had no cooldown since 2026-09-30 and still
    // roots him for the whole ring.
    //
    // **A knight's skill holds him for the clip the drawing plays**, which is the authored pace:
    // timed off `clip`, quickened by his attack speed, the lock ran out while Twisting Slash was
    // still turning and a click walked him out of the spin (the user, 2026-10-01: "dont allow to
    // use click to move if twisting slash animation is not played to the end"). And no blow of
    // his own begins inside it either. A knight's primary is held as well -- Twisting Slash lost
    // its cooldown the same day, and the walk-out is the wizard's alone.
    const int32_t held =
        row.wizardry ? clip : std::max(clip, authoredCastTicks(*tables_, row));
    hero.castUntil = row.channelled()                ? tick_ + row.channelTicks
                     : row.primary() && row.wizardry ? tick_
                                                     : tick_ + held;
    hero.swingsAt = std::max(hero.swingsAt, hero.castUntil);
    if (row.channelled()) {
        hero.channelSkill = row.number;
        hero.channelFrom = tick_;
        hero.channelUntil = tick_ + row.channelTicks;
        // The first strike when his arm is up in the clip.
        hero.channelNext = tick_ + row.strikeFrom;
        // The sweep starts where he is facing, and nobody has been struck yet.
        hero.channelTurn = hero.aim;
        hero.channelStruckCount = 0;
        hero.channelEcho = false;
    }
    hero.walking = false;
    hero.route.clear();
    hero.onStep = 0;
    // **And the auto-attack goes on.** The user's rule, 2026-09-23, which puts back what the
    // first pass did and takes out the `order_ = Request{}` that stood here for a day: a skill is
    // a beat inside the exchange and not the end of it, so the knight spends this swing on the
    // skill and keeps hitting what he was hitting without a second click. That is the fight
    // §3.1a describes -- auto-attack is the floor, the key is the punctuation -- and it is one
    // line either way. MU2's `Realm.Cast` clears its wizard's `Fighting` and is not followed here.

    // Said BEFORE the blow, so the drawing has the skill in hand when the hit arrives and can
    // play the skill's clip instead of the weapon's. MU2 moved its own herald above the blow for
    // this reason and marked the departure; the sparks could not otherwise know a cast happened.
    // On his feet first: nothing is thrown sitting down, a self-cast included.
    rise(hero);
    say(What::Cast, hero, row.number, cool, 0, row.onSelf() ? hero.id : at);

    if (!row.onSelf() && !row.channelled()) {
        // Begun rather than landed: the blow settles halfway through the clip, as a swing's does
        // (Realm::begin), so what is drawn and what is dealt are the same moment. The multiplier
        // rides with it -- 0.75's own `SkillMultiplier` with strength on it, §3.2 -- and the
        // knock, when a skill has one, belongs to the landing too and is why `shove` is reached
        // from there rather than here.
        begin(hero, at, force(row, hero.points), row.number, clip);
    }
    return true;
}

// ---- the two area shapes (docs/skills-dk.md §3.1a) ------------------------------------------
//
// **Only the area differs, never the range.** Both shapes are centred on the knight and measured
// with his own reach, so neither needs a ground target, a cursor mode or a frustum: Ring is
// everything within a tile of him and Arc is the same ring narrowed to the eighth he faces and
// the two beside it. That is the whole of the geometry, and it is why these are cheap.
//
// **The order is the contract.** Nearest first, then clockwise from north, then by id -- one
// `strike()` and one hit roll a target, in that order, so two runs of the same seed draw the same
// dice in the same sequence and the log stays byte-identical. Walking `bodies_` in index order
// would be deterministic too, but it would put the blow on the monster that spawned first rather
// than the one under his feet, and the log is read by people as well as by diff.
int Realm::gather(const Body& hero, const SkillRow& row, uint32_t* victims, int room) const {
    // What the sort is on, kept beside the id so the comparison never touches a body again.
    float off[kVictims] = {};
    float turnOf[kVictims] = {};
    int found = 0;
    for (const Body& one : bodies_) {
        if (!one.monster() || !one.alive()) continue;
        // A line runs as far as the wave sweeps, past the reach it is aimed within.
        if (!within(hero, one, row.spread == Spread::Line ? kLineTiles : row.reach)) continue;
        // Sheltered ground is sheltered from a spin as well: the same test a single blow makes.
        if (tables_->grid.safe(one.column(), one.row())) continue;
        const float dx = one.x - hero.x, dy = one.y - hero.y;
        // Clockwise from north, where north is the row decreasing -- the drawing's own negation
        // of the row, borrowed here only to give the sort a stated zero.
        float turn = std::atan2(dx, -dy);
        if (turn < 0.0f) turn += 6.28318530718f;
        if (row.spread == Spread::Line) {
            // Ahead of him along the aim, and within half the curtain's width of the line.
            const float c = std::cos(hero.aim), s = std::sin(hero.aim);
            const float ahead = dx * c + dy * s;
            const float aside = -dx * s + dy * c;
            if (ahead <= 0.0f || ahead > kLineTiles || std::fabs(aside) > kLineHalfWidth) continue;
        }
        if (row.spread == Spread::Arc) {
            // The facing eighth and the two beside it. `aim` and not `facing`, because the throw
            // aims him at what the key named and the body turns to it over the clip; the blow
            // belongs where he threw it.
            const float toward = std::atan2(dy, dx);
            if (std::fabs(wrapped(toward - hero.aim)) > kArcHalfAngle) continue;
        }
        if (found >= room) continue;
        // Insertion, because the list is at most a handful long and an insertion sort of a
        // handful is both the fastest thing and the one with no allocation in it.
        const float gap = reach(hero, one);
        int at = found;
        while (at > 0 && (off[at - 1] > gap ||
                          (off[at - 1] == gap &&
                           (turnOf[at - 1] > turn ||
                            (turnOf[at - 1] == turn && victims[at - 1] > one.id))))) {
            off[at] = off[at - 1];
            turnOf[at] = turnOf[at - 1];
            victims[at] = victims[at - 1];
            --at;
        }
        off[at] = gap;
        turnOf[at] = turn;
        victims[at] = one.id;
        ++found;
    }
    return found;
}

void Realm::strikeAround(Body& hero, const SkillRow& row, float force) {
    uint32_t victims[kVictims];
    const int found = gather(hero, row, victims, kVictims);
    for (int i = 0; i < found; ++i) {
        // Looked up again rather than held: a body killed earlier in this same sweep may have
        // been left where it fell, and `strikeAt` refuses the dead itself. The vector cannot
        // grow inside the loop -- nothing here spawns -- but ids are what survive one that does.
        if (Body* victim = body(victims[i])) strikeAt(hero, *victim, force);
    }
}

bool Realm::armed(const Body& hero, const SkillRow& row) const {
    const int index = skillIndexOf(row.number);
    if (index < 0 || (hero.learned & (uint32_t(1) << index)) == 0) return false;
    if (row.kin != hero.kin || hero.mana < row.mana) return false;
    // A fan with nothing to loose falls back to the bow, which then says there are no arrows.
    if (row.arrows > 0 && !quivered(hero)) return false;
    const auto armAt = [&](int32_t at) -> const content::Arm* {
        return at >= 0 && size_t(at) < tables_->arms.size() ? &tables_->arms[size_t(at)] : nullptr;
    };
    return row.suits(familyOf(armAt(row.onSelf() ? hero.shield : hero.weapon)));
}

void Realm::keepBoon(Body& hero) {
    // The guard and the Guardian Angel multiply, as two DamageReceiveDecrement power-ups do.
    hero.stats.damageTaken =
        (hero.boonUntil > tick_ ? double(hero.boonDamageTaken) : 1.0) * hero.pet.taken;
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

void Realm::channel(Body& hero) {
    if (hero.channelSkill == skill::kNone) return;
    if (tick_ >= hero.channelUntil) {
        // An Arcane Echo (sim/items.h): the sweep once more, a beat after, from where he stands
        // and without his arm -- the clip is not played again and nothing holds him -- striking
        // afresh, each body its share again. The echo's own end does not echo.
        const SkillRow* row = skillNumbered(hero.channelSkill);
        if (row != nullptr && !hero.channelEcho && echoes(hero)) {
            hero.channelEcho = true;
            hero.channelNext = tick_ + kEchoTicks;
            hero.channelFrom = hero.channelNext - row->strikeFrom;
            hero.channelUntil = hero.channelFrom + row->strikeUntil + 1;
            hero.channelStruckCount = 0;
            core::logf("arcane echo: tick %lld, %s sweeps again", (long long)tick_, row->name);
            return;
        }
        hero.channelSkill = skill::kNone;
        hero.channelEcho = false;
        return;
    }
    if (tick_ < hero.channelNext) return;
    const SkillRow* row = skillNumbered(hero.channelSkill);
    if (row == nullptr) {
        hero.channelSkill = skill::kNone;
        return;
    }
    // Past the window, his arm is coming down: the channel runs out without striking.
    if (tick_ > hero.channelFrom + row->strikeUntil) return;
    hero.channelNext += std::max<int32_t>(1, row->pulseTicks);
    // Everything in its shape now -- a body that walked in since the last strike can be the next
    // -- and of those ONE: the first clockwise from where the last strike went, so the bolt goes
    // round the ring. Clockwise is the bearing increasing in the realm's own x/y; a lone body is
    // struck every time. One `Loosed`, which is what the drawing throws a bolt on, and the blow
    // on the same tick: lightning does not fly. Ties go to the lower id, so the log is fixed.
    uint32_t victims[kVictims];
    const int found = gather(hero, *row, victims, kVictims);
    // How often this channel has struck a body already.
    const auto times = [&](uint32_t id) -> int {
        for (int k = 0; k < hero.channelStruckCount; ++k) {
            if (hero.channelStruck[k] == id) return hero.channelTimes[k];
        }
        return 0;
    };
    Body* next = nullptr;
    float nextTurn = 0.0f, nextGap = 0.0f;
    for (int i = 0; i < found; ++i) {
        Body* victim = body(victims[i]);
        if (victim == nullptr || !victim->alive()) continue;
        // Struck its share already: the sweep passes it by.
        if (row->strikesEach > 0 && times(victim->id) >= row->strikesEach) continue;
        const float turn = std::atan2(victim->y - hero.y, victim->x - hero.x);
        float gap = turn - hero.channelTurn;
        while (gap <= 1e-4f) gap += 6.28318530718f;
        while (gap > 6.28318530718f + 1e-4f) gap -= 6.28318530718f;
        if (next == nullptr || gap < nextGap || (gap == nextGap && victim->id < next->id)) {
            next = victim;
            nextTurn = turn;
            nextGap = gap;
        }
    }
    if (next == nullptr) return;
    hero.channelTurn = nextTurn;
    // Counted before the blow, so a death under it changes nothing.
    bool counted = false;
    for (int k = 0; k < hero.channelStruckCount; ++k) {
        if (hero.channelStruck[k] == next->id) {
            ++hero.channelTimes[k];
            counted = true;
        }
    }
    if (!counted && hero.channelStruckCount < kVictims) {
        hero.channelStruck[hero.channelStruckCount] = next->id;
        hero.channelTimes[hero.channelStruckCount] = 1;
        ++hero.channelStruckCount;
    }
    say(What::Loosed, hero, row->number, 0, 0, next->id);
    strikeAt(hero, *next, force(*row, hero.points), row, true);
}

bool Realm::blinkTo(const Body& hero, const SkillRow& row, int column, int row_, int* outColumn,
                    int* outRow) const {
    const float dx = float(column) - hero.x, dy = float(row_) - hero.y;
    const float far = std::sqrt(dx * dx + dy * dy);
    if (far < 1.0f) return false;
    // Pulled back to its reach along the line, and then walked back toward him a quarter tile at a
    // time until a tile will take him: open to a character, not sheltered (MU's `Wall == 0`
    // refuses both), and not the one he stands on.
    const float reach = std::min(far, row.reach);
    for (float along = reach; along >= 1.0f; along -= 0.25f) {
        const int c = int(std::lround(hero.x + dx / far * along));
        const int r = int(std::lround(hero.y + dy / far * along));
        if (c == hero.column() && r == hero.row()) continue;
        if (!tables_->grid.open(c, r, content::kWallCharacter)) continue;
        if (tables_->grid.safe(c, r)) continue;
        *outColumn = c;
        *outRow = r;
        return true;
    }
    return false;
}

void Realm::blink(Body& hero) {
    hero.x = float(hero.blinkColumn);
    hero.y = float(hero.blinkRow);
    hero.blinkAt = 0;
    hero.walking = false;
    hero.route.clear();
    hero.onStep = 0;
    hero.repathsAt = 0;
    // A summon, if one stands, goes with any warp of his (the Town Portal's rule): no summoner
    // casts Teleport in 0.75, so this only keeps the rule whole.
    if (summonSlot_ >= 0) dismiss(bodies_[size_t(summonSlot_)]);
    say(What::Blinked, hero, hero.blinkColumn, hero.blinkRow);
}

// How long a push takes, in ticks: three tenths of a second -- quick enough to read as a blow,
// slow enough that the drawing slides it rather than jumping it. A body caught mid-step on a
// diagonal is off its tile's centre and can go up to 2.1 tiles to reach the next, so at five
// ticks the worst tick was 0.42 of a tile; at six it is 0.35.
constexpr int32_t kPushTicks = 6;

void Realm::push(Body& target, const Body& from) {
    // Not again while it is still sliding: a push restarted mid-slide begins off the tile's
    // centre and can go two tiles in five ticks. The next strike finds it landed.
    if (target.pushTicks > 0) return;
    // Straight away from him, snapped to the nearest of the eight compass steps. No draw is taken,
    // so the seeded log's dice are the same with or without it.
    const float dx = target.x - from.x, dy = target.y - from.y;
    const float far = std::sqrt(dx * dx + dy * dy);
    if (far < 1e-3f) return;
    const int stepX = int(std::lround(dx / far)), stepY = int(std::lround(dy / far));
    if (stepX == 0 && stepY == 0) return;
    const int column = target.column() + stepX;
    const int row = target.row() + stepY;
    if (!tables_->grid.open(column, row, content::kWallCharacter)) return;
    if (tables_->grid.safe(column, row)) return;
    target.pushX = (float(column) - target.x) / float(kPushTicks);
    target.pushY = (float(row) - target.y) / float(kPushTicks);
    target.pushTicks = kPushTicks;
    // The walk it was on is void, and it is not reaching anybody while it slides.
    target.walking = false;
    target.route.clear();
    target.onStep = 0;
    say(What::Shoved, target, column, row);
}

}  // namespace mu::sim
