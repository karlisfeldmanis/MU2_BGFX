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

void Realm::strikeAt(Body& attacker, Body& target, float force, const SkillRow* row,
                     bool thrown, bool pays) {
    if (!target.alive()) return;  // no blow lands on the dead: the invariant, kept here
    // A spell rolls the wizardry sum, off energy and the staff; everything else is a swing's.
    // A guard's fight, either way round, rolls off his own stream (`wardenDice_`).
    Random& dice = attacker.warden >= 0 || target.warden >= 0     ? wardenDice_
                   : attacker.summoner != 0 || target.summoner != 0 ? summonDice_
                                                                    : dice_;
    // The Imp's price: 3 of his own life on every blow that lands, and the x1.3 only while he
    // can pay it. WebZen's gObjAttack (1.00.93 ObjAttack.cpp:1045-1060) takes the 3 and, when
    // that goes below nought, lays his life at 0 and gives that blow no x1.3; here a hero with
    // 3 or less neither pays nor gets it, so the Imp never lays him at 0 -- ours, since nought
    // is dead in this realm. OpenMU leaves the price out.
    const int impCost = attacker.player ? attacker.pet.lifeCost : 0;
    const bool impPaid = impCost > 0 && attacker.health > impCost;
    const double dealt = attacker.stats.damageDealt;
    if (impCost > 0 && !impPaid) attacker.stats.damageDealt = 1.0;
    Blow blow = row && row->wizardry ? cast(attacker.stats, target.stats, row->damage, dice)
                                     : strike(attacker.stats, target.stats, dice);
    attacker.stats.damageDealt = dealt;
    if (blow.hit && impPaid) attacker.health -= impCost;
    // Ice's and Poison's elements, and a poisoner's bite on him. WebZen lays them before the
    // miss is asked (ResistanceCheck at ObjAttack.cpp:578, MissCheck at :636, 1.00.93), so a
    // miss ices and poisons as a hit does, and neither takes again while it is on
    // (ObjBaseAttack.cpp:670-680, 770-777). Iced, a monster also swings kChillSwingTicks later
    // (DelayActionTime 800). The wizard's pulse is a quarter of his blow (ours); a miss has no
    // blow, so it takes WebZen's own 3% of what is left (user.cpp:25699-25710).
    const auto elements = [&](int damage) {
        if (row != nullptr && row->chillTicks > 0 && target.alive() && target.monster() &&
            target.chilledUntil <= tick_ && !resists(target, true, dice)) {
            target.chilledUntil = tick_ + row->chillTicks;
        }
        if (row != nullptr && row->poisonTicks > 0 && target.alive() && target.monster() &&
            !poisoned(target) && !resists(target, false, dice)) {
            target.poisonUntil = tick_ + row->poisonTicks;
            target.poisonNext = tick_ + kPoisonFirst;
            target.poisonDamage = damage > 0
                                      ? std::max(1, damage / 4)
                                      : std::max(1, int(float(target.health) * kHeroPoisonShare));
            target.poisonBy = attacker.id;
        }
        // 0.75's poison, whose pulse is a share of what he has left (`poisonDamage` 0).
        if (target.player && target.alive() && !attacker.player && poisons(attacker) &&
            !poisoned(target)) {
            target.poisonUntil = tick_ + kHeroPoisonTicks;
            target.poisonNext = tick_ + kPoisonFirst;
            target.poisonDamage = 0;
            target.poisonBy = attacker.id;
        }
    };
    if (!blow.hit) {
        say(What::Missed, attacker, 0, 0, 0, target.id);
        happenings_.back().thrown = thrown;
        elements(0);
        chillHero(attacker, target);
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
    // A guard is not killed: ten thousand health against Lorencia's blows is never reached, and
    // this is the floor that says so rather than a guard lying dead at the gate. invention.
    if (target.warden >= 0) target.health = std::max(1, target.health);
    // The hero's hand on a monster, which is what a guard asks after when it dies.
    if (attacker.player && target.monster() && wound > 0) target.heroStruck = true;
    // And what it cost the gear, on the health it took and nothing else: a blow the shield
    // soaked whole wears nothing, as a miss wears nothing (OpenMU reads HitInfo.HealthDamage).
    if (wound > 0) {
        if (target.player) wearOnTaken(wound);
        if (attacker.player) wearOnLanded(target.stats.defense);
    }
    // What the blow gives back. A landed SWING pays the knight a twentieth of his mana; a skill's
    // own blow pays nothing, which is what makes the basic attack the generator and the skill the
    // spender (kAttackManaShare, and the argument is there). Before the happening, so the log's
    // line and the frame's gauge agree about the tick.
    //
    // **A spell pays nothing back**, primary or not. It did for a day: every landed Energy Ball,
    // Fire Ball, Power Wave and Lightning refunded a twentieth of the pool, which on a wizard of
    // 120 mana is six a hit -- more than three of the four cost -- so casting FILLED his pool
    // (the user, 2026-09-28: "something wrong with mana spending for DW, it gaining mana a lot").
    // Now the wizard casts until he is dry and then walks in with his staff, whose landed swing
    // pays as the knight's does: the rule he was first given ("when it's oom it goes to melee range
    // and attacks with the weapon").
    const bool generates = pays && row == nullptr && force == 1.0f;
    if (attacker.player && generates && attacker.mana < attacker.maxMana) {
        const int back = std::max(1, int(float(attacker.maxMana) * kAttackManaShare));
        attacker.mana = std::min(attacker.maxMana, attacker.mana + back);
    }
    say(What::Hit, attacker, blow.damage, blow.rolled, target.health, target.id);
    happenings_.back().critical = blow.critical;
    happenings_.back().excellent = blow.excellent;
    happenings_.back().thrown = thrown;
    // The element, after the blow and only on what it left standing: 0.75's order.
    if (row != nullptr && row->pushes && target.alive() && !target.player) push(target, attacker);
    elements(blow.damage);
    // An Ice Monster's: iced, whatever the blow did.
    chillHero(attacker, target);
    // An excellent armour's reflect: what reached him, health and shield, times the share, sent
    // back at whoever struck (Player.HitAsync's ReflectDamage). It takes no draw.
    if (target.player && target.alive() && !attacker.player && attacker.alive() &&
        target.excel.reflect > 0.0) {
        const int back = int(double(blow.damage) * target.excel.reflect);
        if (back > 0) {
            attacker.health = std::max(0, attacker.health - back);
            say(What::Hit, target, back, back, attacker.health, attacker.id);
            happenings_.back().reflected = true;
            if (attacker.health <= 0) kill(attacker, target);
        }
    }
    if (!target.player) {
        // Hit, so it knows who did it however far off he is standing, and it is awake whether
        // or not it can see him. Without this half a caster outside its sight kills it without
        // it ever running a thought.
        target.provoked = true;
        // **Her summon holds what it has struck** (the user's, 2026-09-28: "can hold aggro"):
        // her own shot does not turn a monster off her living summon onto her. Anybody else's
        // blow -- the summon's own included -- turns it as it always did.
        const Body* holder = find(target.quarry);
        const bool held = attacker.player && holder != nullptr && holder->alive() &&
                          holder->summoner == attacker.id;
        if (!held) target.quarry = attacker.id;
        if (target.temper == Temper::Asleep) target.temper = Temper::Wandering;
    }
    if (target.health <= 0) kill(target, attacker);
    // His swing, landed: the Rune's power may answer it. A plain swing, a plain arrow, or any
    // arrow of a Multi-Shot's fan (the user, 2026-09-29: "ice arrow also has to work on multi
    // shot") -- every other skill's blow calls nothing, and the lightning and the rock a power
    // throws are unpaid with no row (`pays` false), so it cannot call itself.
    const bool fanned = row != nullptr && row->arrows > 0;
    if (attacker.player && ((row == nullptr && pays) || fanned)) {
        stormcall(attacker, target, blow.damage);
    }
}

void Realm::stormcall(Body& hero, Body& struck, int wound) {
    if (!tables_) return;
    // Either hand: a sword is in the right, and MU puts a bow in the LEFT with the arrows in the
    // right (a crossbow the other way round). Only a weapon takes a weapon's power, so a shield
    // in the left carries none.
    for (const int slot : {int(kWeaponRight), int(kWeaponLeft)}) {
        const Held& hand = bag_[slot];
        if (hand.empty()) continue;
        // Each socket's power rolls on its own, in socket order.
        for (int socket = 0; socket < std::min<int>(hand.sockets, kMostSockets); ++socket) {
            const PowerRow* power = powerOf(hand.powers[socket]);
            if (power == nullptr || !power->weapon || power->kin != hero.kin) continue;
            callDown(hero, struck, *power, wound);
            if (!hero.alive()) return;
        }
    }
}

void Realm::callDown(Body& hero, Body& struck, const PowerRow& power, int wound) {
    // Frost Arrow takes the monster her arrow struck: frozen where it stands, and wounded again
    // for half the arrow, said as the wizard's Ice let go at it so the drawing freezes it there.
    if (power.power == Power::Frost) {
        if (!runeDice_.nextBool(kFrostChance)) return;
        if (!struck.alive() || !struck.monster()) return;
        struck.frozenUntil = tick_ + kFrostTicks;
        say(What::Loosed, hero, skill::kIce, 0, 0, struck.id);
        const int bite = std::max(1, int(float(wound) * kFrostWound));
        struck.health = std::max(0, struck.health - bite);
        say(What::Hit, hero, bite, bite, struck.health, struck.id);
        core::logf("frost rune: tick %lld, on #%u, %d more", (long long)tick_, struck.id, bite);
        if (struck.health <= 0) kill(struck, hero);
        return;
    }
    // Ice and Poison take the monster he struck, and only while it stands: the spell's element
    // without its blow, said as the spell let go at it so the drawing puts its ice or its cloud
    // there. A second of either restarts it, as the spell's own does.
    if (power.power == Power::Ice || power.power == Power::Poison) {
        const bool ice = power.power == Power::Ice;
        if (!runeDice_.nextBool(ice ? kIceRuneChance : kPoisonRuneChance)) return;
        if (!struck.alive() || !struck.monster()) return;
        const SkillRow* spell = skillNumbered(ice ? skill::kIce : skill::kPoison);
        if (spell == nullptr) return;
        if (ice) {
            struck.chilledUntil = tick_ + spell->chillTicks;
        } else {
            struck.poisonUntil = tick_ + spell->poisonTicks;
            struck.poisonNext = tick_ + kPoisonEvery;
            struck.poisonDamage = std::max(1, wound / 4);
            struck.poisonBy = hero.id;
        }
        say(What::Loosed, hero, ice ? skill::kIce : skill::kPoison, 0, 0, struck.id);
        core::logf("%s rune: tick %lld, on #%u", ice ? "ice" : "poison", (long long)tick_,
                   struck.id);
        return;
    }
    // Only Stormcall and Meteor call anything down past here. Arcane Echo is a spell's power,
    // asked where he casts; left to fall through, a wizard's plain staff swing called a knight's
    // lightning.
    if (power.power != Power::Stormcall && power.power != Power::Meteor) return;
    const bool meteor = power.power == Power::Meteor;
    // Off the sockets' own stream, so a run is not moved by a power being worn.
    if (!runeDice_.nextBool(meteor ? kMeteorChance : kStormcallChance)) return;
    // Every other living monster within reach of him, and of those one at random.
    const auto near = [&](const Body& b) {
        if (!b.monster() || !b.alive() || b.id == struck.id) return false;
        const float dx = b.x - hero.x, dy = b.y - hero.y;
        return dx * dx + dy * dy <= kStormcallReach * kStormcallReach;
    };
    int count = 0;
    for (const Body& b : bodies_) count += near(b) ? 1 : 0;
    if (count == 0) return;
    int pick = runeDice_.nextInt(0, count);
    Body* struckBy = nullptr;
    for (Body& b : bodies_) {
        if (!near(b)) continue;
        if (pick-- == 0) {
            struckBy = &b;
            break;
        }
    }
    if (struckBy == nullptr) return;
    if (meteor) {
        // Said as Meteorite let go at it, which is what the drawing drops the rock on; the blow is
        // a flight with no skill, so it lands his swing's roll when the rock does, and pays no mana.
        const SkillRow* rock = skillNumbered(skill::kMeteorite);
        const int32_t fall = rock != nullptr && rock->fallTicks > 0 ? rock->fallTicks : 1;
        say(What::Loosed, hero, skill::kMeteorite, fall, 0, struckBy->id);
        core::logf("meteor rune: tick %lld, a rock on #%u beside #%u", (long long)tick_,
                   struckBy->id, struck.id);
        for (Flight& one : flights_) {
            if (one.at != 0) continue;
            one = Flight{tick_ + fall, struckBy->id, 0, kMeteorForce, false};
            return;
        }
        strikeAt(hero, *struckBy, kMeteorForce, nullptr, true, false);
        return;
    }
    // Said as Lightning let go at it, which is what the drawing throws a thunder on; the blow
    // is his swing's roll, on the same tick -- lightning does not fly -- and pays no mana.
    say(What::Loosed, hero, skill::kLightning, 0, 0, struckBy->id);
    core::logf("stormcall: tick %lld, lightning on #%u beside #%u", (long long)tick_,
               struckBy->id, struck.id);
    strikeAt(hero, *struckBy, kStormcallForce, nullptr, true, false);
    if (struckBy->alive()) push(*struckBy, hero);
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
    rise(hero);  // nobody swings sitting down
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
    // His spell, let go: an Arcane Echo may throw it again a beat later (sim/items.h). Rolled
    // before the release, whatever the release finds, as a swing's rune rolls on the landing.
    const SkillRow* spell = skillNumbered(skill);
    if (spell && spell->wizardry && hero.player && echo_.at == 0 && echoes(hero)) {
        echo_ = Echo{tick_ + kEchoTicks, at, skill, force};
    }
    release(hero, at, force, skill);
}

bool Realm::echoes(Body& hero) {
    if (!tables_) return false;
    for (const int slot : {int(kWeaponRight), int(kWeaponLeft)}) {
        const Held& hand = bag_[slot];
        if (hand.empty()) continue;
        for (int socket = 0; socket < std::min<int>(hand.sockets, kMostSockets); ++socket) {
            const PowerRow* power = powerOf(hand.powers[socket]);
            if (power == nullptr || power->power != Power::Echo || power->kin != hero.kin) continue;
            if (runeDice_.nextBool(kEchoChance)) return true;
        }
    }
    return false;
}

void Realm::release(Body& hero, uint32_t at, float force, int32_t skill) {
    // An area skill has no one victim and is resolved where he stands rather than against the
    // body the key named: the shape is measured NOW, at the bottom of the swing, so a monster
    // that walked into the spin while the clip ran is caught by it and one that walked out is
    // not. He cannot have moved or turned himself in between -- `castUntil` holds him -- so the
    // centre and the facing are the ones he threw it with.
    const SkillRow* row = skillNumbered(skill);
    // A line is let go along where he aimed, whether or not what he aimed at still stands, and
    // flies to every body in it.
    if (row && row->spread == Spread::Line) {
        looseLine(hero, *row, at, force);
        return;
    }
    // Skillshot's fan, aimed at the body and flying on whether or not it still stands.
    if (row && row->spread == Spread::Fan) {
        looseFan(hero, *row, at, force);
        return;
    }
    // Flame: a fire on the ground under what he threw it at, striking on its own clock.
    if (row && row->burns > 0) {
        light(hero, *row, at, force);
        return;
    }
    // Meteorite: a rock on everything round what he called it on.
    if (row && row->splash > 0.0f) {
        rain(hero, *row, at, force);
        return;
    }
    if (row && row->spread != Spread::One) {
        strikeAround(hero, *row, force);
        return;
    }
    Body* target = body(at);
    // Gone, or dead before the arm came down: the swing is spent and nothing lands. That is the
    // same answer `strikeAt` gives for a corpse, moved a few ticks earlier.
    if (!target || !target->alive()) return;
    // A spell is not landed at the bottom of the clip, it is LET GO there: the damage waits for
    // the bolt to cross the gap. MU2's rule and its reason -- "the clip's beat is the release,
    // not the landing"; a monster flinching before the thing that hits it has left the caster
    // is what resolving both on the clip draws.
    if (row && row->thrown()) {
        loose(hero, *row, at, force);
        return;
    }
    // An archer's plain shot is let go here in the same way, the arrow already paid for.
    if (!row && hero.player && hero.archer != 0) {
        looseArrow(hero, at, force);
        return;
    }
    strikeAt(hero, *target, force, row);
}

// How fast a spell crosses the ground is its row's (`SkillRow::flies`): Energy Ball's
// `Direction = (0, -60, 0)` a reference frame of MU's 25 is fifteen tiles a second, Fire Ball's
// fifty is twelve and a half. Either ends a tile short of the body it was thrown at
// (`CheckTargetRange`'s hundred units, one function for both). The drawing flies at the same
// column, so the number and the picture cannot disagree. A throw from beside the target is in
// the air for no ticks and lands on the let-go.
constexpr float kBoltStopsShort = 1.0f;
constexpr float kTicksPerSecond = 20.0f;  // the realm's own clock

void Realm::loose(Body& hero, const SkillRow& row, uint32_t at, float force, bool announce,
                  bool pays) {
    const Body* target = body(at);
    // The straight line, not MU's larger-axis reach: a bolt flies the diagonal.
    const float dx = target ? target->x - hero.x : 0.0f;
    const float dy = target ? target->y - hero.y : 0.0f;
    const float gap = std::max(0.0f, std::sqrt(dx * dx + dy * dy) - kBoltStopsShort);
    // A rock out of the sky takes its fall, however far off the body stands.
    const int32_t air =
        row.fallTicks > 0
            ? row.fallTicks
            : int32_t(std::lround(gap / std::max(1.0f, row.flies) * kTicksPerSecond));
    if (announce) say(What::Loosed, hero, row.number, air, 0, at);
    if (air > 0) {
        for (Flight& one : flights_) {
            if (one.at != 0) continue;
            one = Flight{tick_ + air, at, row.number, force, pays};
            return;
        }
    }
    // Point blank, or no room in the air: it lands now.
    if (Body* struck = body(at); struck && struck->alive()) {
        strikeAt(hero, *struck, force, &row, true, pays);
    }
}

// A plain shot: the spell's flight with the arrow's speed and no row, so `arrive` lands it as a
// swing -- the archery band, not the wizardry sum. Said as a `Loosed` with no skill number.
void Realm::looseArrow(Body& hero, uint32_t at, float force) {
    const Body* target = body(at);
    const float dx = target ? target->x - hero.x : 0.0f;
    const float dy = target ? target->y - hero.y : 0.0f;
    const float gap = std::max(0.0f, std::sqrt(dx * dx + dy * dy) - kBoltStopsShort);
    const int32_t air = int32_t(std::lround(gap / kArrowTilesPerSecond * kTicksPerSecond));
    say(What::Loosed, hero, skill::kNone, air, hero.archer, at);
    if (air > 0) {
        for (Flight& one : flights_) {
            if (one.at != 0) continue;
            one = Flight{tick_ + air, at, skill::kNone, force, true};
            return;
        }
    }
    if (Body* struck = body(at); struck && struck->alive()) {
        strikeAt(hero, *struck, force, nullptr, true, true);
    }
}

void Realm::looseFan(Body& hero, const SkillRow& row, uint32_t aimedAt, float force) {
    const Body* aimed = body(aimedAt);
    const float centre =
        aimed ? std::atan2(aimed->y - hero.y, aimed->x - hero.x) : hero.aim;
    // One `Loosed` for the cast: the drawing fans its own arrows off it.
    say(What::Loosed, hero, row.number, 0, hero.archer, aimedAt);
    constexpr float kRadians = 3.14159265358979f / 180.0f;
    for (int a = 0; a < row.arrows; ++a) {
        // Straight, then one either side, then the next pair out.
        const int step = (a + 1) / 2;
        const float turn = float(a % 2 == 1 ? step : -step) * kFanDegrees * kRadians;
        const float cx = std::cos(centre + turn), cy = std::sin(centre + turn);
        // Every body in the lane, nearest first and then by id, which is fixed for the log.
        struct Struck {
            float along;
            uint32_t id;
        };
        Struck lane[kVictims];
        int found = 0;
        for (const Body& one : bodies_) {
            if (!one.alive() || !one.monster()) continue;
            if (tables_->grid.safe(one.column(), one.row())) continue;
            const float dx = one.x - hero.x, dy = one.y - hero.y;
            const float along = dx * cx + dy * cy;
            const float across = std::fabs(dx * cy - dy * cx);
            if (along < kFanNearest || along > row.reach || across > kLineHalfWidth) continue;
            if (found == kVictims) break;
            int at = found++;
            while (at > 0 && (lane[at - 1].along > along ||
                              (lane[at - 1].along == along && lane[at - 1].id > one.id))) {
                lane[at] = lane[at - 1];
                --at;
            }
            lane[at] = Struck{along, one.id};
        }
        for (int i = 0; i < found; ++i) {
            // An arrow a body struck; the quiver running dry mid-fan ends the fan.
            if (!nock(hero)) {
                say(What::Arrowless, hero, hero.archer);
                return;
            }
            loose(hero, row, lane[i].id, force, false, lane[i].id == aimedAt);
        }
    }
}

void Realm::looseLine(Body& hero, const SkillRow& row, uint32_t aimedAt, float force) {
    // The wave is drawn once, toward what he aimed at, and said once.
    say(What::Loosed, hero, row.number, 0, 0, aimedAt);
    uint32_t victims[kVictims];
    const int found = gather(hero, row, victims, kVictims);
    // Nearest first, as `gather` sorts them, so the landings come in the order the wave meets
    // them; the body he aimed at is the one that pays back, as a single throw would.
    for (int i = 0; i < found; ++i) {
        loose(hero, row, victims[i], force, false, victims[i] == aimedAt);
    }
}

void Realm::arrive() {
    Body& hero = bodies_[0];
    // In the order they were let go when two arrive together, which is the order of the array
    // only while nothing has been freed out of the middle -- so the earliest `at` goes first,
    // and a tie goes to the lower place. Fixed either way, which is what the seeded log needs.
    for (;;) {
        int next = -1;
        for (int i = 0; i < kFlights; ++i) {
            const Flight& one = flights_[i];
            if (one.at == 0 || one.at > tick_) continue;
            if (next < 0 || one.at < flights_[next].at) next = i;
        }
        if (next < 0) return;
        const Flight flight = flights_[next];
        flights_[next] = Flight{};
        const SkillRow* row = skillNumbered(flight.skill);
        // A bolt whose target died in the air flies on past the corpse and lands on nothing:
        // `CheckTargetRange`'s own first line is `to->Live`.
        Body* target = body(flight.target);
        if (!target || !target->alive()) continue;
        strikeAt(hero, *target, flight.force, row, true, flight.pays);
    }
}

bool Realm::poisons(const Body& monster) const {
    if (monster.kind < 0 || size_t(monster.kind) >= tables_->kinds.size()) return false;
    const int32_t number = tables_->kinds[size_t(monster.kind)].number;
    for (const int32_t one : kPoisoners) {
        if (one == number) return true;
    }
    return false;
}

bool Realm::chills(const Body& monster) const {
    if (monster.kind < 0 || size_t(monster.kind) >= tables_->kinds.size()) return false;
    const int32_t number = tables_->kinds[size_t(monster.kind)].number;
    for (const int32_t one : kChillers) {
        if (one == number) return true;
    }
    return false;
}

void Realm::chillHero(const Body& attacker, Body& target) {
    if (!target.player || !target.alive() || attacker.player || !chills(attacker)) return;
    // Not again while it is on: OpenMU adds an effect only when it is not already active
    // (AttackableExtensions.cs:473), so ten seconds from the first, not from the last.
    if (target.chilledUntil > tick_) return;
    target.chilledUntil = tick_ + kHeroChillTicks;
}

bool Realm::resists(const Body& target, bool ice, Random& dice) const {
    if (target.kind < 0 || size_t(target.kind) >= tables_->kinds.size()) return false;
    const int32_t number = tables_->kinds[size_t(target.kind)].number;
    for (const Resistance& one : kResistances) {
        if (one.number != number) continue;
        const int32_t x = ice ? one.ice : one.poison;
        if (x <= 0) return false;
        // Takes with 1/(x + 1), as rand()%(r+1) == 0.
        return !dice.nextBool(1.0 / (1.0 + double(x)));
    }
    return false;
}

void Realm::poisonPulse(Body& beast) {
    if (beast.poisonUntil == 0 || tick_ < beast.poisonNext) return;
    if (beast.poisonNext > beast.poisonUntil || !beast.alive()) {
        beast.poisonUntil = 0;
        return;
    }
    beast.poisonNext += kPoisonEvery;
    Body* by = body(beast.poisonBy);
    if (by == nullptr) by = &beast;
    // A quarter of the wizard's blow on a monster; on him, 0.75's share of what is left.
    const int due = beast.poisonDamage > 0
                        ? beast.poisonDamage
                        : std::max(1, int(float(beast.health) * kHeroPoisonShare));
    // Never the last point: a poison leaves one health, and the kill is a blow's.
    const int bite = std::min(due, beast.health - 1);
    if (bite <= 0) return;
    beast.health -= bite;
    say(What::Hit, *by, bite, bite, beast.health, beast.id);
    happenings_.back().thrown = true;
    happenings_.back().poisoned = true;
}

void Realm::rain(Body& hero, const SkillRow& row, uint32_t aimedAt, float force) {
    // A rock on every body standing within the splash of the one he called it on -- that one
    // included, if it still stands -- each said as its own `Loosed`, so the drawing drops a rock
    // on each, and each landing its own blow after the fall. Nearest the aimed body first and
    // then by id, so the dice are drawn in a fixed order; only the aimed body pays back.
    const Body* aimed = body(aimedAt);
    if (aimed == nullptr) return;
    const float cx = aimed->x, cy = aimed->y;
    uint32_t victims[kVictims];
    float off[kVictims] = {};
    int found = 0;
    for (const Body& one : bodies_) {
        if (!one.monster() || !one.alive()) continue;
        if (tables_->grid.safe(one.column(), one.row())) continue;
        const float gap = std::hypot(one.x - cx, one.y - cy);
        if (gap > row.splash || found >= kVictims) continue;
        int at = found;
        while (at > 0 && (off[at - 1] > gap || (off[at - 1] == gap && victims[at - 1] > one.id))) {
            off[at] = off[at - 1];
            victims[at] = victims[at - 1];
            --at;
        }
        off[at] = gap;
        victims[at] = one.id;
        ++found;
    }
    for (int i = 0; i < found; ++i) {
        loose(hero, row, victims[i], force, true, victims[i] == aimedAt);
    }
}

void Realm::light(Body& hero, const SkillRow& row, uint32_t aimedAt, float force) {
    // On the TILE the body stands on, at its centre, as MU lights it at `SkillX + 0.5`: a fire
    // is a place, and the body is free to walk out of it before the second strike.
    const Body* aimed = body(aimedAt);
    if (aimed == nullptr || !aimed->alive()) return;
    for (Fire& one : fires_) {
        if (one.next != 0) continue;
        one = Fire{tick_, float(aimed->column()), float(aimed->row()), row.number, row.burns,
                   force, aimedAt};
        // Said once, at the body, with no flight: the drawing lights its fire on that body's
        // tile, and the wave with it. Its first strike is this tick's, when `step` burns.
        say(What::Loosed, hero, row.number, 0, 0, aimedAt);
        return;
    }
}

void Realm::burn() {
    Body& hero = bodies_[0];
    for (Fire& fire : fires_) {
        if (fire.next == 0 || fire.next > tick_) continue;
        const SkillRow* row = skillNumbered(fire.skill);
        const float radius = row ? row->burnTiles : 1.5f;
        const bool first = fire.left == (row ? row->burns : 0);
        // Everybody in it now, nearest the centre first and then by id, so the dice are drawn
        // in a fixed order -- `rain`'s rule. Only the aimed body's first strike pays back.
        uint32_t victims[kVictims];
        float off[kVictims] = {};
        int found = 0;
        for (const Body& one : bodies_) {
            if (!one.monster() || !one.alive()) continue;
            if (tables_->grid.safe(one.column(), one.row())) continue;
            const float gap = std::hypot(one.x - fire.x, one.y - fire.y);
            if (gap > radius || found >= kVictims) continue;
            int at = found;
            while (at > 0 &&
                   (off[at - 1] > gap || (off[at - 1] == gap && victims[at - 1] > one.id))) {
                off[at] = off[at - 1];
                victims[at] = victims[at - 1];
                --at;
            }
            off[at] = gap;
            victims[at] = one.id;
            ++found;
        }
        for (int i = 0; i < found; ++i) {
            if (Body* struck = body(victims[i]); struck && struck->alive()) {
                strikeAt(hero, *struck, fire.force, row, true, first && victims[i] == fire.aimed);
            }
        }
        if (--fire.left > 0) {
            fire.next = tick_ + kBurnEvery;
        } else {
            fire = Fire{};
        }
    }
}

void Realm::kill(Body& dead, Body& killer) {
    if (dead.player && undying_) {
        dead.health = dead.maxHealth;
        core::logf("undying: tick %lld, the hero is filled again", (long long)tick_);
        return;
    }
    // Out of the pose before the death, so a body that was sitting falls rather than going on
    // sitting through it.
    rise(dead);
    dead.temper = Temper::Dead;
    dead.walking = false;
    // A push in hand ends with the body: it lies where the blow found it, on its tile.
    if (dead.pushTicks > 0) {
        dead.pushTicks = 0;
        dead.x = float(dead.column());
        dead.y = float(dead.row());
    }
    dead.route.clear();
    dead.onStep = 0;
    dead.quarry = 0;
    dead.provoked = false;
    say(What::Died, dead, dead.level, 0, 0, killer.id);

    // Her summon: it falls and stays down -- nothing drops, nothing is earned, and it does not
    // rise; she casts another. What fought it forgets it.
    if (dead.summoner != 0) {
        dropBlow(dead);
        for (Body& one : bodies_) {
            if (one.quarry == dead.id) {
                one.quarry = 0;
                one.provoked = false;
            }
        }
        return;
    }

    if (dead.player) {
        // He stands up in town three seconds later, at the map's own spawn box with his health
        // restored -- MU's answer to where is a property of the map and not of the death, and
        // reviving him where he fell puts him back inside whatever killed him. Realm.cs:2013.
        dead.risesAt = tick_ + kRiseTicks;
        // And he rises with nothing standing on him: every buff ends with the death, as MU's
        // own do, rather than walking back out of town under a guard he raised in the field.
        // The cooldown is left running, so a death is not a way to raise it again sooner.
        dead.boonUntil = 0;
        dead.mightUntil = 0;
        dead.might = 0;
        dead.channelSkill = 0;
        dead.channelUntil = 0;
        dead.blinkAt = 0;
        // And every debuff, the Ice Monster's chill with the poison: he rises with nothing done
        // to him still counting down (the user, 2026-09-30: "when char is killed remove all
        // debuffs").
        dead.poisonUntil = 0;
        dead.poisonNext = 0;
        dead.poisonDamage = 0;
        dead.chilledUntil = 0;
        dead.frozenUntil = 0;
        dead.boonSkill = skill::kNone;
        dead.boonDamageTaken = 1.0f;
        dead.stats.damageTaken = dead.pet.taken;
        // The Ale too, which is this project's rule and not OpenMU's: AlcoholEffectInitializer
        // sets StopByDeath false, so there he would rise still drunk. One rule for everything in
        // the buff strip was the user's (2026-09-25), and a knight who stands up in town with
        // the red still on him reads as a bug.
        if (dead.aleUntil != 0) {
            dead.aleUntil = 0;
            reswing(dead);
        }
        order_ = Request{};
        pending_ = Request{};
        // And the blow he had in the air goes with him. Left, it waited out the three seconds
        // and landed the tick he stood up -- on the monster that killed him, thirty tiles away,
        // from the middle of town.
        dropBlow(dead);
        // And his spells in the air, for the same reason.
        for (Flight& one : flights_) one = Flight{};
        echo_ = Echo{};
        dead.channelEcho = false;
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
    // **A guard's kill the hero had a hand in is the hero's kill**: its drop and its experience
    // go to him, as if his own blow had been the last. One the guard took alone gives nothing,
    // which is what keeps a hero from standing at the gate while the guards farm for him.
    // invention, with the guards themselves.
    Body& hero = bodies_[0];
    const bool helped = dead.heroStruck && hero.alive();
    if (killer.warden >= 0 && helped) {
        leave(dead, hero);
        gain(hero, int32_t(killExperience(dead.level, hero.level) * kExperienceRate));
    }
    // And the guard who fought it points him on to the rest, whoever landed the last blow.
    if (helped && dead.guardedBy != 0) {
        if (Body* guard = body(dead.guardedBy); guard != nullptr && guard->warden >= 0) {
            pointOn(*guard, dead);
        }
    }
    // **Her summon's kill is hers**: its drop and its experience, as if her own blow had been
    // the last (ours; OpenMU pays the owner of a summon's kill the same way). Paid here and not
    // below, which asks `killer.player`.
    if (killer.summoner != 0) {
        if (Body* owner = body(killer.summoner); owner != nullptr && owner->alive()) {
            leave(dead, *owner);
            gain(*owner, int32_t(killExperience(dead.level, owner->level) * kExperienceRate));
        }
    }
    // What it leaves, before the experience is paid, so the Zen reads the killer's level as
    // it was when the blow landed.
    if (killer.player) leave(dead, killer);
    // An excellent weapon's first two: an eighth of each pool back after a kill (OpenMU's
    // Regeneration after monster kill, off the pool's maximum).
    if (killer.player && killer.alive()) {
        if (killer.excel.killLife > 0.0) {
            killer.health = std::min(killer.maxHealth,
                                     killer.health + int(double(killer.maxHealth) * killer.excel.killLife));
        }
        if (killer.excel.killMana > 0.0) {
            killer.mana = std::min(killer.maxMana,
                                   killer.mana + int(double(killer.maxMana) * killer.excel.killMana));
        }
    }
    if (killer.player) {
        // (int) of the formula, as OpenMU's CalculateAfterKillAsync truncates it
        // (PlayerExperience.cs:105), then the server's rate -- which this note used to say
        // there was none of, and the replica's answer is still the formula above: the rate is
        // one number at one place, stated, the way a live server states one. See kExperienceRate.
        gain(killer, int32_t(killExperience(dead.level, killer.level) * kExperienceRate));
    }
    // And his quests count it, when the kill is his by the rules above: his own blow, her
    // summon's, or a guard's he had a hand in. A guard's own kill does not count, as it pays him
    // nothing.
    if (dead.monster() &&
        (killer.player || killer.summoner != 0 ||
         (killer.warden >= 0 && dead.heroStruck && bodies_[0].alive()))) {
        countKill(dead);
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

std::pair<int, int> Realm::haven() {
    const Body& hero = bodies_[0];
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
    return {column, row};
}

void Realm::setDown(Body& hero, int column, int row) {
    hero.x = float(column);
    hero.y = float(row);
    hero.temper = Temper::Wandering;
    hero.walking = false;
    hero.route.clear();
    hero.onStep = 0;
    hero.quarry = 0;
    hero.swingsAt = tick_;
    hero.repathsAt = 0;
}

void Realm::reviveHero() {
    Body& hero = bodies_[0];
    const auto [column, row] = haven();
    hero.health = hero.maxHealth;
    hero.mana = hero.maxMana;
    hero.sd = hero.maxSd;
    hero.sdCarry = 0.0f;
    hero.healthCarry = 0.0f;
    setDown(hero, column, row);
    // Nothing the window raised while he lay dead carries over: `accept` does not run for a
    // corpse, so a click on the ground or on his killer waited in `pending_` through the three
    // seconds and walked him straight back out of town the tick he stood up.
    order_ = Request{};
    pending_ = Request{};
    wants_ = skill::kNone;
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
    beast.guardedBy = 0;
    beast.heroStruck = false;
    beast.chilledUntil = 0;
    beast.frozenUntil = 0;
    beast.poisonUntil = 0;
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
