#include "sim/realm.h"

#include "sim/swings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "sim/realm_tuning.h"

namespace mu::sim {


// What a body has in its hands, as the arithmetic wants it. A monster always has empty hands
// here: its damage band is its own row, whatever it is drawn holding.
const Body* Realm::find(uint32_t id) const {
    if (id == 0 || id >= indexOfId_.size()) return nullptr;
    const uint32_t at = indexOfId_[id];
    if (at >= bodies_.size()) return nullptr;
    return &bodies_[at];
}

Body* Realm::body(uint32_t id) {
    return const_cast<Body*>(static_cast<const Realm*>(this)->find(id));
}

RealmCounts Realm::counts() const {
    RealmCounts out;
    for (size_t i = 1; i < bodies_.size(); ++i) {
        if (!bodies_[i].monster()) continue;
        ++out.monsters;
        if (bodies_[i].alive()) ++out.alive;
        if (bodies_[i].temper != Temper::Asleep && bodies_[i].temper != Temper::Dead) ++out.roused;
        if (bodies_[i].walking) ++out.walking;
    }
    return out;
}

void Realm::say(What what, const Body& who, int32_t a, int32_t b, int32_t c, uint32_t whom) {
    Happening happening;
    // Every byte, padding included: a seeded run is compared by hashing these whole, and the
    // three bytes after `what` were whatever the caller's stack held -- the same run from two
    // call depths hashed differently. Every field's default is nought, so this changes no value.
    std::memset(static_cast<void*>(&happening), 0, sizeof(happening));
    happening.tick = uint32_t(tick_);
    happening.what = what;
    happening.who = who.id;
    happening.whom = whom;
    happening.a = a;
    happening.b = b;
    happening.c = c;
    happening.x = who.x;
    happening.y = who.y;
    happenings_.push_back(happening);
}

bool Realm::raise(const content::Tables* tables, uint64_t seed, int playerColumn,
                  int playerRow, Kin kin, int level) {
    tables_ = tables;
    if (!tables_ || tables_->grid.empty()) return false;
    dice_.seed(seed);
    // A stream of its own, off the same seed: see `wearDice_`.
    wearDice_.seed(seed ^ 0x9e3779b97f4a7c15ull);
    wardenDice_.seed(seed ^ 0xc2b2ae3d27d4eb4full);
    runeDice_.seed(seed ^ 0x165667b19e3779f9ull);
    for (int slot = 0; slot < kWorn; ++slot) {
        wearCarry_[slot] = 0.0;
        wearItem_[slot] = -1;
    }
    router_.open(&tables_->grid);
    bodies_.clear();
    happenings_.clear();
    happenings_.reserve(4096);
    // The ground: a minute of drops from a fast hunt is a few dozen; 512 is never reached.
    lying_.reserve(512);
    scratch_.reserve(512);
    tick_ = 0;
    nextId_ = 1;
    pending_ = Request{};
    order_ = Request{};

    // The player first, and at index 0 for good: every loop below walks an index, and "the
    // player is bodies_[0]" is cheaper and steadier than a search.
    Body hero;
    hero.id = nextId_++;
    hero.player = true;
    hero.kin = kin;
    hero.points = startingPoints(kin);
    hero.level = std::max(1, std::min(level, kMaximumLevel));
    // A character asked for above level 1 gets his levels and the points that came with them,
    // and the points are spent nowhere: where they go is the player's choice and there is no
    // player down here. It is the honest shape -- a level-20 knight with 95 unspent points is
    // exactly what a level-20 knight who has never opened the window is.
    hero.experience = neededExperience(hero.level);
    hero.pointsInHand = (hero.level - 1) * kPointsPerLevel;
    reckon(hero.kin, hero.level, hero.points, armsOf(hero), &hero.stats, &hero.maxHealth);
    keepBoon(hero);
    restoreMana(hero);
    hero.health = hero.maxHealth;
    hero.speed = 1.0f / float(kHeroMoveTicks);
    int column = playerColumn, row = playerRow;
    if (!router_.nearestOpen(column, row, content::kWallCharacter, 16, &column, &row)) {
        core::logError("the player has nowhere to stand near (%d, %d)", playerColumn, playerRow);
        return false;
    }
    hero.x = float(column);
    hero.y = float(row);
    hero.homeColumn = column;
    hero.homeRow = row;
    hero.temper = Temper::Wandering;
    // **And no skills.** A character is raised knowing nothing, which is the design whole at
    // last: the orbs are cooked and Hanzo sells all nine, so a key on the bar is one that was
    // bought or found and read (docs/skills-dk.md §3.3). Two stand-ins stood here and both are
    // gone -- the flat hand-over of every built skill, and the ladder that handed them over by
    // level. What replaced them is `Realm::useItem`'s own branch, and the level is asked there,
    // off the item, where a requirement belongs.
    // **Except the wizard's Energy Ball**, which he is born with: OpenMU's
    // `AddEnergyBallForDarkWizard` writes it into a new wizard's list at creation, and 0.75 has
    // no scroll for it. `restore` ORs a save's mask over this, so a wizard made before the spell
    // existed stands up knowing it too.
    if (hero.kin == Kin::DarkWizard) {
        hero.learned |= uint32_t(1) << skillIndexOf(skill::kEnergyBall);
    }
    bodies_.push_back(std::move(hero));
    reswing(bodies_[0]);

    // Then every nest, in the table's own order. Placement rejects a tile the threshold
    // refuses and draws again: between 6% and 12% of every Lorencia nest rectangle is
    // un-standable, so blind placement puts monsters inside walls. Each attempt costs two
    // draws, which is itself part of the seeded stream and is why the attempt count is
    // bounded rather than "until it works".
    size_t placed = 0, short_ = 0;
    // And kept clear of Noria's guard posts (kPostClearing).
    const auto byPost = [&](int column, int row) {
        if (tables_->map != kClearedMap) return false;
        for (const content::Townsperson& person : tables_->folk) {
            if (wardenRow(person.number) == nullptr) continue;
            if (std::max(std::abs(column - person.x), std::abs(row - person.y)) <= kPostClearing) {
                return true;
            }
        }
        return false;
    };
    for (const content::MonsterNest& nest : tables_->nests) {
        const content::MonsterKind& kind = tables_->kinds[nest.kind];
        for (uint32_t n = 0; n < nest.count; ++n) {
            int tileColumn = 0, tileRow = 0;
            bool found = false;
            for (int attempt = 0; attempt < 20 && !found; ++attempt) {
                tileColumn = dice_.nextInt(nest.x1, nest.x2 + 1);
                tileRow = dice_.nextInt(nest.y1, nest.y2 + 1);
                // Not in the town, either: two of Lorencia's nests clip the safe zone by a
                // fraction of a percent, which is enough to put a Hound inside the ring where
                // nothing may be attacked.
                found = tables_->grid.open(tileColumn, tileRow, content::kWallCharacter) &&
                        !tables_->grid.safe(tileColumn, tileRow) && !byPost(tileColumn, tileRow);
            }
            if (!found) {
                ++short_;
                continue;
            }
            Body beast;
            beast.id = nextId_++;
            beast.kind = int32_t(nest.kind);
            beast.level = kind.level;
            beast.maxHealth = kind.health;
            beast.health = kind.health;
            beast.stats.level = kind.level;
            beast.stats.attackRate = kind.attackRate;
            beast.stats.defenseRate = kind.defenseRate;
            beast.stats.defense = kind.defense;
            beast.stats.minimumDamage = kind.minimumDamage;
            beast.stats.maximumDamage = kind.maximumDamage;
            beast.swingTicks = kind.attackTicks;
            beast.speed = 1.0f / float(std::max(1, kind.moveTicks));
            beast.x = float(tileColumn);
            beast.y = float(tileRow);
            beast.homeColumn = tileColumn;
            beast.homeRow = tileRow;
            beast.temper = Temper::Asleep;
            // OpenMU's start delay, so a whole nest does not think on one tick forever after.
            beast.thinksAt = dice_.nextInt(0, 100);
            // A route's worth of tiles, taken now rather than on the tick the animal first
            // walks. A wander is at most a few tiles and a chase across a nest is tens; 64 is
            // over the worst either has produced, and a route past it grows once.
            beast.route.reserve(64);
            bodies_.push_back(std::move(beast));
            ++placed;
        }
    }
    if (short_ > 0) {
        core::logError("%zu monsters had no standable tile in their nest after 20 attempts",
                       short_);
    }
    // And the guards, last, so every monster keeps the id it had before there were any.
    raiseWardens();
    // And after them the one summon body, dormant until she casts (realm_summon.cpp): raised
    // here so a cast never grows `bodies_` under a reference to it, and last so no guard's id
    // moves. Kind 0 only so a reader of `kind` never indexes past the table; it is not drawn.
    {
        Body slot;
        slot.id = nextId_++;
        slot.summoner = bodies_[0].id;
        slot.kind = 0;
        slot.health = 0;
        summonSlot_ = int(bodies_.size());
        bodies_.push_back(std::move(slot));
    }

    players_.clear();
    indexOfId_.assign(bodies_.size() + 1, uint32_t(bodies_.size()));
    for (uint32_t i = 0; i < bodies_.size(); ++i) {
        indexOfId_[bodies_[i].id] = i;
        if (bodies_[i].player) players_.push_back(i);
    }

    for (const Body& one : bodies_) {
        if (one.summoner == 0) say(What::Spawned, one, one.level, one.health);
    }
    core::logf("realm: map %u raised, %zu monsters of %zu breeds in %zu nests, player at "
               "(%d, %d), seed %llu", tables_->map, placed, tables_->kinds.size(),
               tables_->nests.size(), column, row, (unsigned long long)seed);
    return true;
}

HeroRecord Realm::record() const {
    HeroRecord out;
    const Body& hero = bodies_[0];
    out.kin = hero.kin;
    out.column = hero.column();
    out.row = hero.row();
    out.facing = hero.facing;
    out.level = hero.level;
    out.experience = hero.experience;
    out.pointsInHand = hero.pointsInHand;
    out.points = hero.points;
    out.health = hero.health;
    out.mana = hero.mana;
    out.money = money_;
    out.learned = hero.learned;
    if (hero.boonSkill != 0 && hero.boonUntil > tick_) {
        out.boonSkill = hero.boonSkill;
        out.boonDamageTaken = hero.boonDamageTaken;
        out.boonTicksLeft = hero.boonUntil - tick_;
    }
    out.aleTicksLeft = aleLeft();
    for (int i = 0; i < kSkills; ++i) out.coolsLeft[i] = std::max<int64_t>(0, hero.cools[i] - tick_);
    for (int slot = 0; slot < kSlots; ++slot) out.slots[slot] = bag_[slot];
    for (int i = 0; i < kQuests; ++i) out.quests[i] = quests_[i];
    return out;
}

void Realm::restore(const HeroRecord& saved) {
    Body& hero = bodies_[0];
    hero.level = std::max(1, std::min(saved.level, kMaximumLevel));
    hero.experience = saved.experience;
    hero.pointsInHand = std::max(0, saved.pointsInHand);
    hero.points = saved.points;
    // ORed rather than assigned, and only while `raise` still hands a knight his first skill: a
    // save written before the skills existed carries a nought mask, and assigning it would take
    // back the skill the grant above just gave him. The day the orb is the only way in, this
    // becomes an assignment.
    hero.learned |= saved.learned;
    hero.facing = hero.aim = saved.facing;
    money_ = std::max<int64_t>(0, saved.money);
    // The quests as saved, each count held to its step's goal so an edited file cannot hand in
    // a clear it never made.
    for (int i = 0; i < kQuests; ++i) {
        quests_[i] = saved.quests[i];
        if (int(quests_[i].state) > int(QuestState::Resting)) quests_[i] = QuestProgress{};
        for (int step = 0; step < kQuestSteps; ++step) {
            quests_[i].counts[step] = uint16_t(std::min<int>(quests_[i].counts[step],
                                                             questGoal(i, step)));
        }
    }
    bag_.clear();
    for (int slot = 0; slot < kSlots; ++slot) {
        const Held& one = saved.slots[slot];
        if (one.empty() || size_t(one.item) >= tables_->items.size()) continue;
        bag_.put(slot, one);
    }
    // The buff he was saved with, for the ticks it had left and at the factor it was cast at --
    // a skill he knows, lasting no longer than the skill's own length, so an edited file cannot
    // stand him behind a permanent guard.
    if (const SkillRow* row = skillNumbered(saved.boonSkill);
        row != nullptr && row->boonTicks > 0 && saved.boonTicksLeft > 0) {
        hero.boonSkill = row->number;
        hero.boonDamageTaken = std::clamp(saved.boonDamageTaken, 1.0f - kGuardCap, 1.0f);
        hero.boonUntil = tick_ + std::min<int64_t>(saved.boonTicksLeft, row->boonTicks);
    }
    // And an Ale, no longer than one lasts, before rearm so the swing is reckoned with it.
    if (saved.aleTicksLeft > 0) hero.aleUntil = tick_ + std::min(saved.aleTicksLeft, kAleTicks);
    rearm(hero);
    // The waits he was saved with, each no longer than the skill's own cooldown at his agility
    // now, so an edited file cannot lock a key for an hour. After rearm, which is what the clip's
    // length -- a cooldown's floor -- is reckoned off.
    for (int i = 0; i < kSkills; ++i) {
        if (saved.coolsLeft[i] <= 0) continue;
        hero.cools[i] = tick_ + std::min<int64_t>(saved.coolsLeft[i], coolsFor(skillAt(i).number));
    }
    hero.health = saved.health > 0 ? std::min(saved.health, hero.maxHealth) : hero.maxHealth;
    hero.mana = std::max(0, std::min(saved.mana, hero.maxMana));
}

bool Realm::spend(int strength, int agility, int vitality, int energy) {
    if (strength < 0 || agility < 0 || vitality < 0 || energy < 0) return false;
    Body& hero = bodies_[0];
    const int asked = strength + agility + vitality + energy;
    if (asked == 0 || asked > hero.pointsInHand) return false;
    hero.points.strength += strength;
    hero.points.agility += agility;
    hero.points.vitality += vitality;
    hero.points.energy += energy;
    hero.pointsInHand -= asked;
    const int was = hero.maxHealth;
    reckon(hero.kin, hero.level, hero.points, armsOf(hero), &hero.stats, &hero.maxHealth);
    keepBoon(hero);
    restoreMana(hero);
    // Agility buys attack speed, so spending a point can change how often he swings.
    reswing(hero);
    // Vitality's health arrives full rather than as a bigger empty bar, which is what MU does
    // when a point is spent and is the only part of this that is not pure arithmetic.
    hero.health = std::min(hero.maxHealth, hero.health + std::max(0, hero.maxHealth - was));
    return true;
}

// ---- walking ---------------------------------------------------------------------------

void Realm::accept() {
    Body& hero = bodies_[0];
    if (!hero.alive()) return;

    // **A skill cannot be walked out of.** The user's rule, 2026-09-23: only the auto-attack is
    // cancelled by a click to move; a skill, once thrown, plays to the end of its clip. Every
    // order that would take a step -- the ground click, a thing on the floor, a townsperson --
    // is dropped where it stands rather than held, so the click is spent and he does not set off
    // the moment the clip ends. Attack and Stop are let through: neither moves him while
    // `castUntil` is running (`press` returns on `tick_ < swingsAt`, which outlasts the clip),
    // so retargeting mid-cast still works and the blow is thrown the tick he is free.
    //
    // This is also where a click stopped costing the skill its damage: the `dropBlow` below
    // threw away the cast's own unlanded blow, so a click during the clip cancelled the skill
    // and kept nothing -- the same complaint as the swing's, one rule further on.
    if (casting() &&
        (pending_.kind == Request::Kind::WalkTo || pending_.kind == Request::Kind::Pick ||
         pending_.kind == Request::Kind::Talk || pending_.kind == Request::Kind::Perch)) {
        pending_ = Request{};
    }

    if (pending_.kind != Request::Kind::None) {
        // **A new order drops the blow he had not landed yet.** Walking away from a swing is how
        // an attack is cancelled -- press() has said so since sprint 5 -- and until 2026-09-23 the
        // cancel cost him the animation and not the damage, because the damage had been settled at
        // the top of the swing. Reported by the player: "cancel the attack with a click to move
        // and the damage is still done". An Attack order on the SAME body is not a cancel.
        const bool same = pending_.kind == Request::Kind::Attack && order_.kind == pending_.kind &&
                          pending_.target == order_.target;
        if (!same) dropBlow(hero);
        order_ = pending_;
        pending_ = Request{};
        // Any order is walking away from a counter, including another Talk -- and from the vault,
        // and from a quest giver's dialog.
        trading_ = -1;
        banking_ = -1;
        questing_ = -1;
        if (order_.kind == Request::Kind::WalkTo) {
            send(hero, order_.column, order_.row);
        } else if (order_.kind == Request::Kind::Stop) {
            halt(hero);
            order_ = Request{};
        } else if (order_.kind == Request::Kind::Pick) {
            for (const Lying& one : lying_) {
                if (one.id == order_.target) send(hero, one.column, one.row);
            }
        } else if (order_.kind == Request::Kind::Talk) {
            if (order_.target >= tables_->folk.size()) {
                order_ = Request{};
            } else if (!serving(int(order_.target))) {
                // Where he stands now: a townsperson on his rounds is not at his table's tile.
                int column = 0, row = 0;
                folkTile(int(order_.target), &column, &row);
                send(hero, column, row);
            }
        } else if (order_.kind == Request::Kind::Perch) {
            if (order_.target >= tables_->perches.size()) {
                order_ = Request{};
                return;
            }
            const content::Perch& one = tables_->perches[order_.target];
            // **Some of them are furniture nobody can use, and that is MU's own answer.** The
            // click is gated on the placement's TILE before any route is planned:
            // `wall == TW_HEIGHT || wall < TW_CHARACTER`, so the word must be nothing, SafeZone
            // alone, or exactly Height. Eight of Lorencia's 110 stand on NoMove -- one lean box,
            // one tavern bench and six logs, counted off this attribute grid on 2026-09-24 (MU2's
            // Crowd.Pose says 31, and reads the same attributes.png) -- and are not usable.
            if (!content::usable(tables_->grid, one)) {
                order_ = Request{};
                return;
            }
            // Walked to the placement's own tile, and the pose taken once the walk is over (see
            // press). MU's `if (PathFinding2(...)) SendMove(c, o); else Action(c, o, true)`: no
            // walk to make -- already there, or no way there -- and the arm's own one-tile test
            // decides at once. A refused plan stops the old walk, so that test is asked now and
            // not wherever the last click was taking him.
            const bool there = hero.column() == one.column && hero.row() == one.row &&
                               std::fabs(hero.x - float(one.column)) <= 1e-3f &&
                               std::fabs(hero.y - float(one.row)) <= 1e-3f;
            if (!there && !send(hero, one.column, one.row)) halt(hero);
        }
    }
}

// The Perch order's end: MU's operate arm, asked ONCE, standing still, at the end of the walk --
// `Action()` is reached from `if (MovePath(c))`, true on the tick the route runs out. Asked every
// tick of the walk instead, it passes a tile early and he sits down in the road beside the bench
// he was walking to (MU2 did that, and says so in Crowd.Perch). The one-tile slack is for the
// bench whose own tile he cannot stand on: he sits from beside it rather than not at all.
void Realm::perch(Body& hero) {
    const int32_t index = int32_t(order_.target);
    order_ = Request{};
    const content::Perch& one = tables_->perches[size_t(index)];
    if (std::max(std::abs(hero.column() - one.column), std::abs(hero.row() - one.row)) > 1) {
        return;  // stopped short -- held against a fence, or no route; the arm does not run
    }
    halt(hero);
    // The angle is the placement's where MU copies it (`Hero->Object.Angle[2] = TargetAngle`),
    // and otherwise the way he walked up. A lean box needs it: leaning without it is lying back
    // through the wall. Exact, not snapped to MU's eight -- MU2's Realm.Pose says why.
    if (one.turns) {
        hero.facing = hero.aim = one.aim;
        hero.turning = false;
    }
    hero.pose = Pose(one.pose);
    hero.perch = index;
    say(What::Posed, hero, int32_t(hero.pose), index);
}

void Realm::rise(Body& one) {
    if (!one.player || one.pose == Pose::Standing) return;
    one.pose = Pose::Standing;
    one.perch = -1;
    say(What::Posed, one, int32_t(Pose::Standing), -1);
}

void Realm::press() {
    Body& hero = bodies_[0];
    if (!hero.alive()) return;

    // The key, before the order, and it does not replace it: a press spends the next swing on a
    // skill and leaves the knight fighting what he was fighting (docs/skills-dk.md §3.1a). A wish
    // that cannot be thrown -- cooling, no mana, out of reach, nothing learned -- falls through
    // and the ordinary blow lands, which is what "auto-attack is the floor" means.
    //
    // Thrown only when the weapon is out of its own recovery, and `throwSkill` puts the clock
    // forward itself, so the order below sees a swing already spent and does not swing twice.
    if (wants_ != skill::kNone) {
        if (tick_ > wantsUntil_) {
            wants_ = skill::kNone;
        } else if (tick_ >= hero.swingsAt) {
            if (const SkillRow* row = skillNumbered(wants_)) {
                // A self-cast reads its target off the caster; an attack takes the id the key
                // named, or the one he is already fighting when the key named nobody.
                const uint32_t at = row->onSelf()  ? hero.id
                                    : wantsAt_ != 0 ? wantsAt_
                                                    : order_.target;
                if (throwSkill(hero, *row, at)) wants_ = skill::kNone;
            }
        }
    }

    // And a boon lapsing, which is the other half of a buff: replace rather than stack, off on
    // the tick it expires, and `Fighter.damageTaken` back to 1 -- the field `sim/rules.h` has
    // carried since sprint 5 for exactly this.
    if (hero.boonUntil != 0 && tick_ >= hero.boonUntil) {
        hero.boonUntil = 0;
        hero.boonSkill = skill::kNone;
        hero.boonDamageTaken = 1.0f;
        hero.stats.damageTaken = hero.pet.taken;
    }
    // And the Ale, off on its tick, with the swing re-reckoned without its twenty. MuMain's
    // HeroAttributeCalc clears ABILITY_FAST_ATTACK_SPEED the frame AbilityTime[0] runs out.
    if (hero.aleUntil != 0 && tick_ >= hero.aleUntil) {
        hero.aleUntil = 0;
        reswing(hero);
    }
    // And Greater Damage, off on its tick and the band re-reckoned without it.
    if (hero.mightUntil != 0 && tick_ >= hero.mightUntil) {
        hero.mightUntil = 0;
        hero.might = 0;
        rearm(hero);
    }

    if (order_.kind == Request::Kind::Perch) {
        if (!hero.walking) perch(hero);
        return;
    }

    if (order_.kind == Request::Kind::Pick) {
        // Taken on arrival: within a tile of it, which is standing on it or beside it -- the
        // grid may refuse the tile itself when something died against a wall. The reach is
        // this project's; MU picks up when the walk ends on the item.
        size_t at = lying_.size();
        for (size_t i = 0; i < lying_.size(); ++i) {
            if (lying_[i].id == order_.target) at = i;
        }
        if (at == lying_.size()) {
            order_ = Request{};
            return;
        }
        const Lying& one = lying_[at];
        if (std::fabs(hero.x - float(one.column)) <= 1.0f &&
            std::fabs(hero.y - float(one.row)) <= 1.0f) {
            halt(hero);
            take(at);
            order_ = Request{};
        }
        return;
    }

    if (order_.kind == Request::Kind::Talk) {
        // Served the tick he is within reach, whether he walked there or was already there.
        // A townsperson who sells nothing and keeps nothing -- a guard -- is walked to and
        // then nothing happens, which is MU's own answer to talking to a guard.
        if (serving(int(order_.target))) {
            const content::Townsperson& one = tables_->folk[order_.target];
            halt(hero);
            if (sells(one.number)) {
                trading_ = int(order_.target);
                say(What::Served, hero, trading_, one.number);
            } else if (one.number == kVaultKeeper) {
                banking_ = int(order_.target);
                say(What::Served, hero, banking_, one.number);
            } else if (const int quest = questOf(one.number); quest >= 0) {
                // A quest giver: his dialog opens, whatever it has to say -- the offer, the
                // quest under way, the hand-in, or that it is not his to give again yet.
                questing_ = int(order_.target);
                say(What::Offered, hero, quest, questing_, int(quests_[quest].state));
            } else if (one.number == kGuildMaster) {
                // Ours (the user, 2026-09-29): MU opens the guild window here, which a single
                // player game has no use for, so he answers with a line instead of nothing.
                say(What::Shouted, hero, int32_t(Shout::Greet), 0, int(order_.target));
            }
            order_ = Request{};
        }
        return;
    }

    if (order_.kind != Request::Kind::Attack) return;
    const Body* target = find(order_.target);
    // Only a monster: a guard is a body, and not one he may raise a hand to.
    if (!target || !target->alive() || !target->monster()) {
        order_ = Request{};
        return;
    }

    // **The quick slot, which is the right mouse button.** The user's rule of 2026-09-28, for
    // every class alike: the skill on it is thrown whenever it can be, and when it cannot -- the
    // mana is gone, the hand is wrong, it has not been learned -- he closes to arm's length and
    // swings the weapon, which is the left button's attack. So a wizard out of mana walks in and
    // hits with his staff, and a hit gives back the mana that lets him step off and cast again.
    //
    // A skill that is only COOLING is not a reason to close in: a thrown one waits at its own
    // range for the cooldown, and a knight's, whose reach is his arm's, swings through it --
    // which is the auto-attack floor docs/skills-dk.md §3.1a already gave him.
    const bool sheltered = tables_->grid.safe(target->column(), target->row());
    if (order_.skill != skill::kNone) {
        const SkillRow* row = skillNumbered(order_.skill);
        if (row && armed(hero, *row)) {
            const int index = skillIndexOf(row->number);
            const bool cooled = tick_ >= hero.cools[size_t(index)];
            if (row->onSelf()) {
                // A guard on the slot is raised when it can be and the fight goes on under it.
                // A summon only when none stands: a recast is a dismissal (realm_summon.cpp),
                // and the slot re-throwing it on every cooldown would send it away each time.
                const bool standing = row->summons > 0 && summonSlot_ >= 0 &&
                                      bodies_[size_t(summonSlot_)].alive();
                if (!standing && cooled && tick_ >= hero.swingsAt) throwSkill(hero, *row, hero.id);
            } else if (within(hero, *target, row->reach) && !sheltered) {
                if (row->thrown() || cooled) {
                    if (tick_ >= hero.castUntil) engage(hero, *target);
                    if (cooled && tick_ >= hero.swingsAt) throwSkill(hero, *row, order_.target);
                    return;
                }
            } else if (row->thrown()) {
                approach(hero, *target, int(row->reach));
                return;
            }
        }
    }

    const int reachOf = hero.archer != 0 ? kArcherReach : kHeroAttackRange;
    if (within(hero, *target, float(reachOf)) &&
        !tables_->grid.safe(target->column(), target->row())) {
        // Not while a skill's clip is running: the blow was thrown at where he was facing, and a
        // body that turns under its own animation is the sudden movement the user objected to.
        if (tick_ >= hero.castUntil) engage(hero, *target);
        if (tick_ >= hero.swingsAt) {
            // An archer pays for the shot as she draws. None in hand or bag and the attack
            // stops where she stands -- ours: OpenMU lets an empty quiver shoot for nothing.
            if (!nock(hero)) {
                say(What::Arrowless, hero, hero.archer);
                order_ = Request{};
                halt(hero);
                return;
            }
            hero.swingsAt = tick_ + hero.swingTicks;
            begin(hero, order_.target, 1.0f, skill::kNone, hero.swingTicks);
        }
        return;
    }
    // Out of reach: close, on the same re-plan clock a monster's chase uses -- but not until
    // the blow he is in the middle of has finished.
    //
    // A swing and a step are never both, and the way out of a swing is to cancel it. For the
    // player that interval IS the swing animation: sim/swings.cpp works `swingTicks` out of the
    // attack clip's own keys and play speed, so `tick_ < swingsAt` is exactly "the axe is still
    // coming down". Without this line a quarry that shuffled one tile pulled him out of his own
    // blow and he walked with the axe still swinging -- which he then did on three quarters of
    // every swing frame drawn.
    //
    // What cancels it is an order, and only an order: a click on the ground, a click on
    // something else, or a stop, all of which arrive above as `pending_` and replace this one
    // before this line is reached. So the player is never held still by his own attack -- he
    // gives it up, which is what an attack cancel is -- and the chase, which is the engine's
    // decision rather than his, waits its turn.
    approach(hero, *target, reachOf);
}

void Realm::approach(Body& hero, const Body& target, int radius) {
    if (tick_ < hero.swingsAt) return;
    if (tick_ >= hero.repathsAt) {
        hero.repathsAt = tick_ + kRepath;
        if (drifted(hero, target)) {
            hero.chaseX = target.x;
            hero.chaseY = target.y;
            int column = 0, row = 0;
            if (beside(target, radius, hero, &column, &row)) send(hero, column, row);
        }
    }
}

// ---- the tick --------------------------------------------------------------------------

void Realm::step() {
    ++tick_;
    happenings_.clear();

    // The order is fixed and is written down because it is the behaviour: the player walks and
    // swings, then every monster is roused, thinks and moves in index order, then the dead are
    // considered for respawn. Nothing here walks a hash container, and every id came from one
    // monotonic counter.
    Body& hero = bodies_[0];
    sip();
    recover(hero);
    // What has lain its minute goes, in the order it lies -- a fixed order, since the list is
    // only ever appended to and swapped out of by the tick's own events.
    for (size_t i = 0; i < lying_.size();) {
        if (lying_[i].vanishesAt <= tick_) {
            say(What::Vanished, hero, int32_t(lying_[i].id));
            lying_[i] = lying_.back();
            lying_.pop_back();
        } else {
            ++i;
        }
    }
    if (hero.alive()) {
        // What was begun and not cancelled lands first, before this tick's orders: the arm comes
        // down at the moment the drawing shows it coming down, and a click that arrives on this
        // same tick is too late to stop it -- which is the honest boundary and is where the
        // player's own hand is.
        if (hero.blowAt != 0 && tick_ >= hero.blowAt) land(hero);
        // An Arcane Echo's second throw, let go as the first was, paying nothing.
        if (echo_.at != 0 && tick_ >= echo_.at) {
            Echo echo = echo_;
            echo_ = Echo{};
            // The first throw may have killed what it was aimed at: the echo goes on to the
            // nearest living monster within the spell's reach, and is spent if there is none.
            const Body* aimed = body(echo.target);
            const SkillRow* row = skillNumbered(echo.skill);
            if ((aimed == nullptr || !aimed->alive()) && row != nullptr) {
                echo.target = 0;
                float best = 0.0f;
                for (const Body& one : bodies_) {
                    if (!one.monster() || !one.alive() || !within(hero, one, row->reach)) continue;
                    if (tables_->grid.safe(one.column(), one.row())) continue;
                    const float gap = reach(hero, one);
                    if (echo.target == 0 || gap < best) {
                        echo.target = one.id;
                        best = gap;
                    }
                }
            }
            core::logf("arcane echo: tick %lld, skill %d at #%u", (long long)tick_, echo.skill,
                       echo.target);
            release(hero, echo.target, echo.force, echo.skill);
        }
        // And whatever he let go earlier and has now arrived.
        arrive();
        channel(hero);
        if (hero.blinkAt != 0 && tick_ >= hero.blinkAt) blink(hero);
        poisonPulse(hero);
        accept();
        advance(hero);
        press();
    } else if (tick_ >= hero.risesAt) {
        reviveHero();
    }

    for (size_t i = 1; i < bodies_.size(); ++i) {
        Body& beast = bodies_[i];
        if (beast.warden >= 0) {
            watch(beast);
            continue;
        }
        if (beast.summoner != 0) {
            tend(beast);
            continue;
        }
        poisonPulse(beast);
        if (beast.alive() && beast.pushTicks > 0) {
            // Pushed: it slides and does nothing else until it lands on its tile.
            beast.x += beast.pushX;
            beast.y += beast.pushY;
            if (--beast.pushTicks == 0) {
                beast.x = float(beast.column());
                beast.y = float(beast.row());
            }
        } else if (beast.alive()) {
            rouse(beast);
            if (beast.temper != Temper::Asleep) {
                advance(beast);
                think(beast);
            } else if (beast.walking) {
                // Asleep, but not until it has come to a stand. A monster whose walk ended on
                // the tick nobody was left near it would otherwise keep that tick's pace for as
                // long as it slept, and the drawing decides walk or idle from exactly that.
                advance(beast);
            }
        } else if (tick_ >= beast.risesAt) {
            raiseBeast(beast);
        }
    }
}

// ---- the log ---------------------------------------------------------------------------

std::string describe(const Happening& happening, const Realm& realm) {
    const auto name = [&realm](uint32_t id) -> std::string {
        const Body* one = realm.find(id);
        if (!one) return "nobody";
        if (one->player) return "hero";
        if (one->warden >= 0) {
            return realm.tables()->folk[size_t(one->warden)].name + "#" + std::to_string(id);
        }
        if (one->summoner != 0) {
            return realm.tables()->kinds[size_t(one->kind)].label + " of hero#" + std::to_string(id);
        }
        return realm.tables()->kinds[size_t(one->kind)].label + "#" + std::to_string(id);
    };
    char line[512];
    const char* who = nullptr;
    std::string whoName = name(happening.who);
    who = whoName.c_str();
    // Fixed precision everywhere, and never %g: the log's own formatting is part of the
    // contract two runs are compared under.
    switch (happening.what) {
        case What::Spawned:
            std::snprintf(line, sizeof(line), "%6u spawned %s at %.3f,%.3f level %d hp %d",
                          happening.tick, who, happening.x, happening.y, happening.a,
                          happening.b);
            break;
        case What::Rose:
            std::snprintf(line, sizeof(line), "%6u rose %s at %.3f,%.3f hp %d", happening.tick,
                          who, happening.x, happening.y, happening.b);
            break;
        case What::Roused:
            std::snprintf(line, sizeof(line), "%6u %s %s at %.3f,%.3f nearest %d",
                          happening.tick, happening.a ? "woke" : "slept", who, happening.x,
                          happening.y, happening.b);
            break;
        case What::Refused:
            std::snprintf(line, sizeof(line), "%6u %s cannot reach %d,%d from %.3f,%.3f",
                          happening.tick, who, happening.a, happening.b, happening.x,
                          happening.y);
            break;
        case What::Walked:
            std::snprintf(line, sizeof(line), "%6u %s walks to %d,%d in %d steps",
                          happening.tick, who, happening.a, happening.b, happening.c);
            break;
        case What::Stepped:
            std::snprintf(line, sizeof(line), "%6u %s steps to %d,%d", happening.tick, who,
                          happening.a, happening.b);
            break;
        case What::Halted:
            std::snprintf(line, sizeof(line), "%6u %s halts at %.3f,%.3f", happening.tick, who,
                          happening.x, happening.y);
            break;
        case What::Missed:
            std::snprintf(line, sizeof(line), "%6u %s misses %s", happening.tick, who,
                          name(happening.whom).c_str());
            break;
        case What::Hit:
            std::snprintf(line, sizeof(line), "%6u %s hits %s for %d (roll %d) leaving %d",
                          happening.tick, who, name(happening.whom).c_str(), happening.a,
                          happening.b, happening.c);
            break;
        case What::Died:
            std::snprintf(line, sizeof(line), "%6u %s dies at %.3f,%.3f to %s", happening.tick,
                          who, happening.x, happening.y, name(happening.whom).c_str());
            break;
        case What::Gained:
            std::snprintf(line, sizeof(line), "%6u %s gains %d experience, %d in all",
                          happening.tick, who, happening.a, happening.b);
            break;
        case What::Drank:
            std::snprintf(line, sizeof(line), "%6u %s drinks for %d %s", happening.tick, who,
                          happening.a, happening.b ? "mana" : "health");
            break;
        case What::Offered:
            std::snprintf(line, sizeof(line), "%6u %s hears quest %d from %s (%d)", happening.tick,
                          who, happening.a, realm.tables()->folk[size_t(happening.b)].name.c_str(),
                          happening.c);
            break;
        case What::QuestTaken:
            std::snprintf(line, sizeof(line), "%6u %s takes quest %d", happening.tick, who,
                          happening.a);
            break;
        case What::QuestStep:
            std::snprintf(line, sizeof(line), "%6u %s counts %d on quest %d step %d", happening.tick,
                          who, happening.b, happening.a, happening.c);
            break;
        case What::QuestReady:
            std::snprintf(line, sizeof(line), "%6u %s has done quest %d; back to its giver",
                          happening.tick, who, happening.a);
            break;
        case What::QuestDone:
            std::snprintf(line, sizeof(line), "%6u %s hands in quest %d, choosing item %d into %d",
                          happening.tick, who, happening.a, happening.b, happening.c);
            break;
        case What::Set:
            std::snprintf(line, sizeof(line),
                          "%6u %s sets a Rune of Creation (power %d) in socket %d of slot %d",
                          happening.tick, who, happening.b, happening.c, happening.a);
            break;
        case What::Soused:
            std::snprintf(line, sizeof(line), "%6u %s drinks an ale for %d ticks, swinging every %d",
                          happening.tick, who, happening.a, happening.b);
            break;
        case What::Cured:
            std::snprintf(line, sizeof(line), "%6u %s drinks an antidote", happening.tick, who);
            break;
        case What::Dismissed:
            std::snprintf(line, sizeof(line), "%6u %s is dismissed", happening.tick, who);
            break;
        case What::Arrowless:
            std::snprintf(line, sizeof(line), "%6u %s has no more %s", happening.tick, who,
                          happening.a == 2 ? "bolts" : "arrows");
            break;
        case What::Blinked:
            std::snprintf(line, sizeof(line), "%6u %s teleports to %d,%d", happening.tick, who,
                          happening.a, happening.b);
            break;
        case What::Warped:
            std::snprintf(line, sizeof(line), "%6u %s reads a town portal to %d,%d",
                          happening.tick, who, happening.a, happening.b);
            break;
        case What::Shouted:
            std::snprintf(line, sizeof(line), "%6u %s %s %s, pointing to %d,%d", happening.tick,
                          who,
                          happening.a == int32_t(Shout::Pointing) ? "points the hero on from"
                          : happening.a == int32_t(Shout::Salute) ? "salutes"
                          : happening.a == int32_t(Shout::Chat)   ? "talks at the bar (b the line, c who says it) --"
                          : happening.a == int32_t(Shout::Greet)  ? "is greeted (c the folk row) --"
                                                                  : "challenges",
                          name(happening.whom).c_str(), happening.b, happening.c);
            break;
        case What::Served:
            std::snprintf(line, sizeof(line), "%6u %s is served by %s", happening.tick, who,
                          realm.tables()->folk[size_t(happening.a)].name.c_str());
            break;
        case What::Bought:
            std::snprintf(line, sizeof(line), "%6u %s buys %s for %d into slot %d", happening.tick,
                          who, realm.tables()->items[size_t(happening.a)].label.c_str(),
                          happening.b, happening.c);
            break;
        case What::Sold:
            std::snprintf(line, sizeof(line), "%6u %s sells %s for %d from slot %d", happening.tick,
                          who, realm.tables()->items[size_t(happening.a)].label.c_str(),
                          happening.b, happening.c);
            break;
        case What::Dropped:
            if (happening.b < 0) {
                std::snprintf(line, sizeof(line), "%6u %s leaves %d Zen (#%d) at %.3f,%.3f",
                              happening.tick, who, happening.c, happening.a, double(happening.x),
                              double(happening.y));
            } else {
                std::snprintf(line, sizeof(line), "%6u %s leaves %s +%d (#%d) at %.3f,%.3f",
                              happening.tick, who,
                              realm.tables()->items[size_t(happening.b)].label.c_str(), happening.c,
                              happening.a, double(happening.x), double(happening.y));
            }
            break;
        case What::Picked:
            if (happening.b < 0) {
                std::snprintf(line, sizeof(line), "%6u %s takes %d Zen off #%d", happening.tick,
                              who, happening.c, happening.a);
                break;
            }
            std::snprintf(line, sizeof(line), "%6u %s picks up #%d into slot %d (%d Zen)",
                          happening.tick, who, happening.a, happening.b, happening.c);
            break;
        case What::Vanished:
            std::snprintf(line, sizeof(line), "%6u #%d vanishes", happening.tick, happening.a);
            break;
        case What::Levelled:
            std::snprintf(line, sizeof(line), "%6u %s reaches level %d with %d points",
                          happening.tick, who, happening.a, happening.b);
            break;
        case What::Swung:
            std::snprintf(line, sizeof(line), "%6u %s swings at %s", happening.tick, who,
                          name(happening.whom).c_str());
            break;
        case What::Cast: {
            const SkillRow* row = skillNumbered(happening.a);
            std::snprintf(line, sizeof(line), "%6u %s casts %s at %s, cooling %d ticks",
                          happening.tick, who, row ? row->name : "?",
                          name(happening.whom).c_str(), happening.b);
            break;
        }
        case What::Loosed: {
            const SkillRow* row = skillNumbered(happening.a);
            std::snprintf(line, sizeof(line), "%6u %s lets go %s at %s, %d ticks in the air",
                          happening.tick, who,
                          row                ? row->name
                          : happening.c == 2 ? "a bolt"
                          : happening.c == 1 ? "an arrow"
                                             : "?",
                          name(happening.whom).c_str(), happening.b);
            break;
        }
        case What::Shoved:
            std::snprintf(line, sizeof(line), "%6u %s is shoved to %d,%d", happening.tick, who,
                          happening.a, happening.b);
            break;
        case What::Learned: {
            const SkillRow* row = skillNumbered(happening.a);
            std::snprintf(line, sizeof(line), "%6u %s learns %s", happening.tick, who,
                          row ? row->name : "?");
            break;
        }
        case What::Posed: {
            static const char* const kPoses[] = {"stands up", "?", "sits", "leans", "hangs"};
            const int pose = (happening.a >= 0 && happening.a <= 4) ? happening.a : 1;
            std::snprintf(line, sizeof(line), "%6u %s %s at %.3f,%.3f (perch %d)", happening.tick,
                          who, kPoses[pose], double(happening.x), double(happening.y),
                          happening.b);
            break;
        }
        case What::PetLost:
            std::snprintf(line, sizeof(line), "%6u %s lost his pet, row %d", happening.tick, who,
                          happening.a);
            break;
        case What::Worn:
            std::snprintf(line, sizeof(line), "%6u %s wore slot %d to %d/%d", happening.tick, who,
                          happening.a, happening.b, happening.c);
            break;
        case What::Repaired:
            std::snprintf(line, sizeof(line), "%6u %s repaired %s%d piece(s) for %d Zen",
                          happening.tick, who, happening.a < 0 ? "all: " : "", happening.c,
                          happening.b);
            break;
        case What::Refined:
            std::snprintf(line, sizeof(line), "%6u %s refined slot %d from +%d to +%d",
                          happening.tick, who, happening.a, happening.b, happening.c);
            break;
    }
    return std::string(line);
}

}  // namespace mu::sim
