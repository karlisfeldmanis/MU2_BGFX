// A blow landing, a body dying, what the killer earns by it, and both of them getting back up.
//
// The roll and the damage themselves are sim/rules.cpp, traced to OpenMU's Version075; this is
// what the realm does with the answer.
#include "sim/realm.h"

#include "sim/swings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/log.h"
#include "sim/realm_tuning.h"

namespace mu::sim {

void Realm::strikeAt(Body& attacker, Body& target, float force) {
    if (!target.alive()) return;  // no blow lands on the dead: the invariant, kept here
    Blow blow = strike(attacker.stats, target.stats, dice_);
    if (!blow.hit) {
        say(What::Missed, attacker, 0, 0, 0, target.id);
        return;
    }
    // A skill's multiplier, and it goes exactly here: after the roll, the defence and the level
    // floor, which is where OpenMU spends `Stats.SkillMultiplier`
    // (AttackableExtensions.cs:226-247). One for an ordinary swing, so nothing changes for one.
    // No draw is taken, so a seeded log's dice are untouched by the arithmetic.
    if (force != 1.0f) blow.damage = std::max(1, int(float(blow.damage) * force));
    // The shield takes nine tenths, and what it cannot cover falls through to health: a pool
    // with three points left protects by three and no more. MU2's Realm.Wound, off OpenMU's
    // GetHitInfo shieldRatio and Player.HitAsync's overflow. Monsters have none.
    int wound = blow.damage;
    if (target.sd > 0) {
        const int onto = int(float(blow.damage) * kShieldShare);
        const int over = onto - target.sd;
        target.sd = std::max(0, target.sd - onto);
        wound = blow.damage - onto + std::max(0, over);
    }
    target.health = std::max(0, target.health - wound);
    // What the blow gives back. A landed SWING pays the knight a twentieth of his mana; a skill's
    // own blow pays nothing, which is what makes the basic attack the generator and the skill the
    // spender (kAttackManaShare, and the argument is there). Before the happening, so the log's
    // line and the frame's gauge agree about the tick.
    if (attacker.player && force == 1.0f && attacker.mana < attacker.maxMana) {
        const int back = std::max(1, int(float(attacker.maxMana) * kAttackManaShare));
        attacker.mana = std::min(attacker.maxMana, attacker.mana + back);
    }
    say(What::Hit, attacker, blow.damage, blow.rolled, target.health, target.id);
    happenings_.back().critical = blow.critical;
    if (!target.player) {
        // Hit, so it knows who did it however far off he is standing, and it is awake whether
        // or not it can see him. Without this half a caster outside its sight kills it without
        // it ever running a thought.
        target.provoked = true;
        target.quarry = attacker.id;
        if (target.temper == Temper::Asleep) target.temper = Temper::Wandering;
    }
    if (target.health <= 0) kill(target, attacker);
}

// A blow begun. The clip starts now and the damage is settled when the arm comes down -- half the
// swing later, which is exactly where the drawing has always put the number, the blood and the
// fall (`Showing::kLandingPoint`). Only the player swings this way: a monster's blow still lands
// on the tick it is decided, because nothing can cancel a monster's swing and giving it a wind-up
// would change every seeded log for no gain.
void Realm::begin(Body& hero, uint32_t at, float force, int32_t skill, int32_t overTicks) {
    // Half the clip that is being played -- the weapon's swing, or the skill's own, which is
    // longer. `Showing::kLandingPoint` is the same 0.5 on the drawing's side and the two must
    // not drift apart: this is the number that decides when the damage is real.
    hero.blowAt = tick_ + std::max<int64_t>(1, overTicks / 2);
    hero.blowTarget = at;
    hero.blowForce = force;
    hero.blowSkill = skill;
    say(What::Swung, hero, skill, 0, 0, at);
}

void Realm::land(Body& hero) {
    const uint32_t at = hero.blowTarget;
    const float force = hero.blowForce;
    const int32_t skill = hero.blowSkill;
    hero.blowAt = 0;
    hero.blowTarget = 0;
    hero.blowSkill = 0;
    hero.blowForce = 1.0f;
    // An area skill has no one victim and is resolved where he stands rather than against the
    // body the key named: the shape is measured NOW, at the bottom of the swing, so a monster
    // that walked into the spin while the clip ran is caught by it and one that walked out is
    // not. He cannot have moved or turned himself in between -- `castUntil` holds him -- so the
    // centre and the facing are the ones he threw it with.
    if (const SkillRow* row = skillNumbered(skill)) {
        if (row->spread != Spread::One) {
            strikeAround(hero, *row, force);
            return;
        }
    }
    Body* target = body(at);
    // Gone, or dead before the arm came down: the swing is spent and nothing lands. That is the
    // same answer `strikeAt` gives for a corpse, moved a few ticks earlier.
    if (!target || !target->alive()) return;
    strikeAt(hero, *target, force);
}

void Realm::kill(Body& dead, Body& killer) {
    dead.temper = Temper::Dead;
    dead.walking = false;
    dead.route.clear();
    dead.onStep = 0;
    dead.quarry = 0;
    dead.provoked = false;
    say(What::Died, dead, dead.level, 0, 0, killer.id);

    if (dead.player) {
        // He stands up in town three seconds later, at the map's own spawn box with his health
        // restored -- MU's answer to where is a property of the map and not of the death, and
        // reviving him where he fell puts him back inside whatever killed him. Realm.cs:2013.
        dead.risesAt = tick_ + kRiseTicks;
        order_ = Request{};
        pending_ = Request{};
        return;
    }

    const content::MonsterKind& kind = tables_->kinds[size_t(dead.kind)];
    dead.risesAt = tick_ + kind.respawnTicks;
    // Every monster the killer is still holding as a quarry forgets it, or a chase carries on
    // toward a corpse.
    for (Body& one : bodies_) {
        if (one.quarry == dead.id) {
            one.quarry = 0;
            one.provoked = false;
        }
    }
    // What it leaves, before the experience is paid, so the Zen reads the killer's level as
    // it was when the blow landed.
    if (killer.player) leave(dead, killer);
    if (killer.player) {
        // (int) of the formula, as OpenMU's CalculateAfterKillAsync truncates it
        // (PlayerExperience.cs:105), then the server's rate -- which this note used to say
        // there was none of, and the replica's answer is still the formula above: the rate is
        // one number at one place, stated, the way a live server states one. See kExperienceRate.
        gain(killer, int32_t(killExperience(dead.level, killer.level) * kExperienceRate));
    }
}

void Realm::gain(Body& hero, int32_t award) {
    if (award <= 0) return;
    // PlayerExperience.cs:197-240: a while loop, because one kill can carry more than one
    // level, and the experience is spent up to each threshold rather than poured past it.
    // Experience past the cap is discarded rather than banked.
    int32_t remaining = award;
    while (remaining > 0) {
        if (hero.level >= kMaximumLevel) return;
        const uint64_t needed = neededExperience(hero.level + 1);
        uint64_t gained = uint64_t(remaining);
        bool levels = false;
        if (needed - hero.experience < gained) {
            gained = needed - hero.experience;
            levels = true;
        }
        hero.experience += gained;
        say(What::Gained, hero, int32_t(gained), int32_t(hero.experience));
        if (!levels) return;
        ++hero.level;
        hero.pointsInHand += kPointsPerLevel;
        // Re-reckoned and then refilled, in that order: the health a level gives is part of
        // the maximum it is refilled to.
        reckon(hero.kin, hero.level, hero.points, armsOf(hero), &hero.stats, &hero.maxHealth);
        keepBoon(hero);
        restoreMana(hero);
        reswing(hero);
        hero.health = hero.maxHealth;
        hero.mana = hero.maxMana;
        hero.sd = hero.maxSd;
        say(What::Levelled, hero, hero.level, hero.pointsInHand);
        remaining -= int32_t(gained);
    }
}

void Realm::reviveHero() {
    Body& hero = bodies_[0];
    const int32_t* gate = tables_->safeGate;
    int column = hero.column(), row = hero.row();
    if (gate[2] > gate[0] && gate[3] > gate[1]) {
        // A tile drawn from inside the box, then the nearest standable one to it. The box is a
        // rectangle and the town inside it is not all floor, so a draw lands on something about
        // one time in three; MU2 watched a character stand up on his own corpse beside the thing
        // that killed him before it added the fallback.
        column = dice_.nextInt(gate[0], gate[2] + 1);
        row = dice_.nextInt(gate[1], gate[3] + 1);
        int open = column, openRow = row;
        if (router_.nearestOpen(column, row, content::kWallCharacter, 8, &open, &openRow)) {
            column = open;
            row = openRow;
        }
    }
    hero.x = float(column);
    hero.y = float(row);
    hero.health = hero.maxHealth;
    hero.mana = hero.maxMana;
    hero.sd = hero.maxSd;
    hero.sdCarry = 0.0f;
    hero.temper = Temper::Wandering;
    hero.walking = false;
    hero.route.clear();
    hero.onStep = 0;
    hero.quarry = 0;
    hero.swingsAt = tick_;
    hero.repathsAt = 0;
    say(What::Rose, hero, hero.level, hero.health);
}

void Realm::raiseBeast(Body& beast) {
    const content::MonsterKind& kind = tables_->kinds[size_t(beast.kind)];
    beast.health = beast.maxHealth;
    beast.x = float(beast.homeColumn);
    beast.y = float(beast.homeRow);
    beast.temper = Temper::Asleep;
    beast.quarry = 0;
    beast.provoked = false;
    beast.walking = false;
    beast.route.clear();
    beast.onStep = 0;
    beast.swingsAt = tick_ + kind.attackTicks;
    beast.thinksAt = tick_;
    say(What::Rose, beast, beast.level, beast.health);
}

// ---- the player ------------------------------------------------------------------------

// The order the window raised since the last tick becomes the one he is following. Taken
// BEFORE he moves this tick, not after: after, a click waited a whole tick in `pending_`, was
// planned at the end of the next one, and was first walked on the tick after that -- 100 ms
// of the character ignoring the mouse before the drawing's own interpolation added its 50.

}  // namespace mu::sim
