#include "sim/realm.h"

#include "sim/swings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

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
        ++out.monsters;
        if (bodies_[i].alive()) ++out.alive;
        if (bodies_[i].temper != Temper::Asleep && bodies_[i].temper != Temper::Dead) ++out.roused;
        if (bodies_[i].walking) ++out.walking;
    }
    return out;
}

void Realm::say(What what, const Body& who, int32_t a, int32_t b, int32_t c, uint32_t whom) {
    Happening happening;
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
    bodies_.push_back(std::move(hero));
    reswing(bodies_[0]);

    // Then every nest, in the table's own order. Placement rejects a tile the threshold
    // refuses and draws again: between 6% and 12% of every Lorencia nest rectangle is
    // un-standable, so blind placement puts monsters inside walls. Each attempt costs two
    // draws, which is itself part of the seeded stream and is why the attempt count is
    // bounded rather than "until it works".
    size_t placed = 0, short_ = 0;
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
                        !tables_->grid.safe(tileColumn, tileRow);
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

    players_.clear();
    indexOfId_.assign(bodies_.size() + 1, uint32_t(bodies_.size()));
    for (uint32_t i = 0; i < bodies_.size(); ++i) {
        indexOfId_[bodies_[i].id] = i;
        if (bodies_[i].player) players_.push_back(i);
    }

    for (const Body& one : bodies_) say(What::Spawned, one, one.level, one.health);
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
    for (int slot = 0; slot < kSlots; ++slot) out.slots[slot] = bag_[slot];
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
    bag_.clear();
    for (int slot = 0; slot < kSlots; ++slot) {
        const Held& one = saved.slots[slot];
        if (one.empty() || size_t(one.item) >= tables_->items.size()) continue;
        bag_.put(slot, one);
    }
    rearm(hero);
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
        // Any order is walking away from a counter, including another Talk.
        trading_ = -1;
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
                const content::Townsperson& one = tables_->folk[order_.target];
                send(hero, one.x, one.y);
            }
        }
    }
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
        hero.stats.damageTaken = 1.0;
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
        // A townsperson who sells nothing -- a guard, the vault keeper -- is walked to and
        // then nothing happens, which is MU's own answer to talking to a guard.
        if (serving(int(order_.target))) {
            const content::Townsperson& one = tables_->folk[order_.target];
            halt(hero);
            if (sells(one.number)) {
                trading_ = int(order_.target);
                say(What::Served, hero, trading_, one.number);
            }
            order_ = Request{};
        }
        return;
    }

    if (order_.kind != Request::Kind::Attack) return;
    const Body* target = find(order_.target);
    if (!target || !target->alive()) {
        order_ = Request{};
        return;
    }
    if (within(hero, *target, float(kHeroAttackRange)) &&
        !tables_->grid.safe(target->column(), target->row())) {
        // Not while a skill's clip is running: the blow was thrown at where he was facing, and a
        // body that turns under its own animation is the sudden movement the user objected to.
        if (tick_ >= hero.castUntil) engage(hero, *target);
        if (tick_ >= hero.swingsAt) {
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
    if (tick_ < hero.swingsAt) return;
    if (tick_ >= hero.repathsAt) {
        hero.repathsAt = tick_ + kRepath;
        if (drifted(hero, *target)) {
            hero.chaseX = target->x;
            hero.chaseY = target->y;
            int column = 0, row = 0;
            if (beside(*target, kHeroAttackRange, hero, &column, &row)) send(hero, column, row);
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
        accept();
        advance(hero);
        press();
        // And whatever Zen he is standing on, after the step that put him there.
        sweep();
    } else if (tick_ >= hero.risesAt) {
        reviveHero();
    }

    for (size_t i = 1; i < bodies_.size(); ++i) {
        Body& beast = bodies_[i];
        if (beast.alive()) {
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
    }
    return std::string(line);
}

}  // namespace mu::sim
