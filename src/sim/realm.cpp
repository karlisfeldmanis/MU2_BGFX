#include "sim/realm.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/log.h"

namespace mu::sim {
namespace {

// How far past its view range something can be and still keep a monster awake. Realm.cs:63.
constexpr int kMargin = 8;
// How far a monster will be led from where it was put down before it gives up and walks back,
// and twice that for one that has been hit. Both are MU2's bench inventions and are marked as
// such where they are defined: OpenMU has no leash at all, and without one nothing that has
// seen you can ever be outrun, because a monster walks exactly as fast as a character.
constexpr int kLeash = 10;       // invention (MU2's Realm.cs Leash, and it says so itself)
constexpr int kGrudge = 20;      // invention (MU2's Grudge = Leash * 2)
// How often a chase re-plans, in ticks. Realm.cs:1918.
constexpr int kRepath = 3;
// A player's swing, in ticks. Things.cs:249 is 1000 ms, which MU2 re-reckons from the attack
// clip's authored length once a character has one; there are no clips down here and no weapons
// until sprint 7, so the default stands and is marked as what it is.
constexpr int kHeroSwingTicks = 20;
// A player walks a tile in 400 ms, as every Lorencia monster does. The monster's 400 is traced
// (Lorencia.cs:87); the player's is MU2's derivation from MU's walk clip (Things.cs:583) and is
// borrowed from the monster row rather than invented separately.
constexpr int kHeroMoveTicks = 8;
// How fast a body comes round to where it is going, in degrees a second. A right angle in a
// tenth of a second, which is where MU2 put it and about where WoW and League both sit: quick
// enough that the turn is never the thing being waited for, slow enough to be seen happening
// rather than the facing teleporting. Walker.cs:85 (a bench number, and MU2 marks it as one).
constexpr float kTurnDegrees = 900.0f;
// How far off its heading a body may be and still walk, in degrees. Under it, it sets off and
// finishes coming round as it goes, which is what walking round a corner is. Over it -- a click
// behind the character, a monster turning onto somebody who hit it from behind -- it turns on
// the spot first, because setting off at once means travelling backwards for the length of the
// turn. Walker.cs:98-107.
constexpr float kPivotDegrees = 120.0f;

// A player reaches one tile. Sprint 7's weapons have their own reach.
constexpr int kHeroAttackRange = 1;
// How long a dead character lies there before he stands up in town. MU2's Player.cs:1687, three
// seconds, which is also when a summon is taken off him.
constexpr int kRiseTicks = 60;

// An angle folded into a half turn either side of nothing, so that a body facing just west of
// north turns a few degrees to face just east of it rather than most of the way round the other
// way. Walker.cs:288-309.
float wrapped(float angle) {
    constexpr float kTurnabout = 6.28318530718f;
    angle = std::fmod(angle, kTurnabout);
    if (angle > 3.14159265359f) angle -= kTurnabout;
    if (angle < -3.14159265359f) angle += kTurnabout;
    return angle;
}

// MU's own reckoning of distance: the larger of the two axis distances, not a Euclidean
// length. Things.cs:607.
float reach(const Body& one, const Body& other) {
    return std::max(std::fabs(one.x - other.x), std::fabs(one.y - other.y));
}

bool within(const Body& one, const Body& other, float range) {
    return reach(one, other) <= range;
}

int strayed(const Body& beast) {
    return std::max(std::abs(beast.column() - beast.homeColumn),
                    std::abs(beast.row() - beast.homeRow));
}

}  // namespace

// What a body has in its hands, as the arithmetic wants it. A monster always has empty hands
// here: its damage band is its own row, whatever it is drawn holding.
Arms Realm::armsOf(const Body& one) const {
    Arms arms;
    if (!tables_) return arms;
    if (one.weapon >= 0 && size_t(one.weapon) < tables_->arms.size()) {
        const content::Arm& held = tables_->arms[size_t(one.weapon)];
        arms.weaponMinimumDamage = held.minimumDamage;
        arms.weaponMaximumDamage = held.maximumDamage;
        arms.armourDefense += held.defense;
    }
    if (one.shield >= 0 && size_t(one.shield) < tables_->arms.size()) {
        arms.armourDefense += tables_->arms[size_t(one.shield)].defense;
    }
    return arms;
}

bool Realm::equip(int32_t weapon, int32_t shield) {
    refusal_.clear();
    if (!tables_) return false;
    Body& hero = bodies_[0];
    const auto allowed = [&](int32_t index, bool wantShield) {
        if (index < 0) return true;
        if (size_t(index) >= tables_->arms.size()) {
            refusal_ = "there is no such arm";
            return false;
        }
        const content::Arm& arm = tables_->arms[size_t(index)];
        if (arm.isShield() != wantShield) {
            refusal_ = arm.label + " is " + (arm.isShield() ? "a shield" : "a weapon") +
                       " and was asked for as the other";
            return false;
        }
        // mu.db's enumeration: bit 0 Dark Wizard, bit 1 Fairy Elf, bit 2 Dark Knight.
        if ((arm.classes & (1 << int(hero.kin))) == 0) {
            refusal_ = arm.label + " is not for this class";
            return false;
        }
        // The requirement is the item's own, at its base level. An item's level raises what it
        // asks -- "a row's raw strength is not what the game asks" -- and levelled items are
        // sprint 7's along with the rest of what an item is.
        if (hero.points.strength < arm.wantsStrength) {
            refusal_ = arm.label + " wants " + std::to_string(arm.wantsStrength) +
                       " strength and he has " + std::to_string(hero.points.strength);
            return false;
        }
        if (hero.points.agility < arm.wantsAgility) {
            refusal_ = arm.label + " wants " + std::to_string(arm.wantsAgility) +
                       " agility and he has " + std::to_string(hero.points.agility);
            return false;
        }
        return true;
    };
    if (!allowed(weapon, false) || !allowed(shield, true)) return false;

    hero.weapon = weapon;
    hero.shield = shield;
    const int was = hero.maxHealth;
    reckon(hero.kin, hero.level, hero.points, armsOf(hero), &hero.stats, &hero.maxHealth);
    hero.health = std::min(hero.maxHealth, hero.health + std::max(0, hero.maxHealth - was));
    return true;
}

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
    hero.health = hero.maxHealth;
    hero.swingTicks = kHeroSwingTicks;
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
    bodies_.push_back(std::move(hero));

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
    // Vitality's health arrives full rather than as a bigger empty bar, which is what MU does
    // when a point is spent and is the only part of this that is not pure arithmetic.
    hero.health = std::min(hero.maxHealth, hero.health + std::max(0, hero.maxHealth - was));
    return true;
}

// ---- walking ---------------------------------------------------------------------------

bool Realm::send(Body& one, int column, int row) {
    if (!router_.plan(one.column(), one.row(), column, row, content::kWallCharacter, scratch_)) {
        // A refusal is an event and not a silence. It was a silence for one evening, and the
        // scripted hand -- which asks again whenever it is not walking -- asked for the same
        // impossible tile every tick for the rest of the run: nine thousand ticks in which
        // nothing whatever was written to the log, because nothing whatever happened.
        say(What::Refused, one, column, row);
        return false;
    }
    one.route.assign(scratch_.begin(), scratch_.end());
    one.onStep = 0;
    one.walking = true;
    say(What::Walked, one, column, row, int32_t(one.route.size()));
    return true;
}

void Realm::halt(Body& one) {
    if (!one.walking) return;
    one.walking = false;
    one.route.clear();
    one.onStep = 0;
    say(What::Halted, one);
}

// One tick of movement along the line the route describes. The diagonal is NOT penalised here
// and that is the whole of the diagonal question: MU's A* costs a diagonal 7 against a
// straight 5 because that is a SEARCH cost, and OpenMU's GetStepDelay scales the step delay by
// 1.414 because that is a SPEED. Taking both makes a diagonal walk 41% slow and taking neither
// makes it fast. Here a walk is a line and a line has a length, which is MU2's answer
// (Walker.cs:63-67) and is the one that needs no penalty of either kind.
// Turns a body toward its aim and says whether it is still swinging round on the spot -- in
// which case it may not cover ground this tick. Walker.cs:275-286.
bool Realm::turn(Body& one) {
    constexpr float kToRadians = 3.14159265359f / 180.0f;
    const float apart = wrapped(one.aim - one.facing);
    // One tick's worth, and the tick is the only clock: 900 degrees a second at 20 Hz is 45
    // degrees a tick, so a full reversal takes four ticks.
    const float most = kTurnDegrees * kToRadians / 20.0f;
    one.facing = std::fabs(apart) <= most ? one.aim
                                          : one.facing + (apart < 0.0f ? -most : most);
    one.turning = std::fabs(wrapped(one.aim - one.facing)) > kPivotDegrees * kToRadians;
    return one.turning;
}

void Realm::advance(Body& one) {
    // A body that is standing still still comes round: a fighter between two blows turns onto
    // what it is hitting, and a walk that has just been given spends its first tick or two
    // turning before any ground is covered.
    if (!one.walking) {
        turn(one);
        return;
    }
    // Aimed at the tile it is walking to, and turned toward it BEFORE any ground is covered.
    if (one.onStep < one.route.size()) {
        const Step& target = one.route[one.onStep];
        const float dx = float(target.column) - one.x;
        const float dy = float(target.row) - one.y;
        if (dx * dx + dy * dy > 1e-6f) one.aim = std::atan2(dy, dx);
    }
    if (turn(one)) return;  // still coming round: no ground this tick
    float left = one.speed;
    while (left > 0.0f && one.onStep < one.route.size()) {
        const Step& target = one.route[one.onStep];
        const float dx = float(target.column) - one.x;
        const float dy = float(target.row) - one.y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        if (distance <= 1e-6f) {
            ++one.onStep;
            continue;
        }
        one.aim = std::atan2(dy, dx);
        if (distance <= left) {
            one.x = float(target.column);
            one.y = float(target.row);
            left -= distance;
            ++one.onStep;
            say(What::Stepped, one, target.column, target.row);
        } else {
            one.x += dx / distance * left;
            one.y += dy / distance * left;
            left = 0.0f;
        }
    }
    if (one.onStep >= one.route.size()) {
        one.walking = false;
        one.route.clear();
        one.onStep = 0;
        say(What::Halted, one);
    }
}

// ---- the mind --------------------------------------------------------------------------

bool Realm::worth(const Body& beast, const Body& target, int range) const {
    return target.alive() && within(beast, target, float(range)) &&
           !tables_->grid.safe(target.column(), target.row());
}

void Realm::rouse(Body& beast) {
    const content::MonsterKind& kind = tables_->kinds[size_t(beast.kind)];
    const float far = float(kind.viewRange + kMargin);
    float nearest = 1e30f;
    for (uint32_t who : players_) {
        // Alive or not, and the "or not" is the point: being roused is about whether anybody
        // can SEE the monster, and a dead character is still standing there. Whether there is
        // anything worth attacking is a separate question and worth() does check.
        nearest = std::min(nearest, reach(beast, bodies_[who]));
    }
    const bool roused = nearest <= far || (beast.provoked && beast.quarry != 0);
    const bool was = beast.temper != Temper::Asleep;
    if (roused == was) return;

    if (roused) {
        beast.temper = Temper::Wandering;
        // A monster that has just woken thinks on the NEXT tick and with a jitter, which is
        // OpenMU's start delay: a whole nest that woke on one tick would swing in unison
        // forever after. No jitter for one woken by a blow -- there is one animal, it has
        // already been told what hit it, and the stagger would only be slowness where somebody
        // is watching for a reaction.
        beast.thinksAt = tick_ + (beast.provoked ? 1 : dice_.nextInt(1, std::max(2, 20 / 4)));
        say(What::Roused, beast, 1, nearest > 1e29f ? -1 : int32_t(nearest));
    } else {
        beast.temper = Temper::Asleep;
        beast.quarry = 0;
        beast.provoked = false;
        halt(beast);
        say(What::Roused, beast, 0, -1);
    }
}

void Realm::wander(Body& beast) {
    const content::MonsterKind& kind = tables_->kinds[size_t(beast.kind)];
    if (kind.moveRange <= 0) return;
    // Two draws, always in this order, and the tile is tested rather than corrected: a
    // wander that retried until it found somewhere would consume a variable number of draws.
    const int column = beast.column() + dice_.nextInt(-kind.moveRange, kind.moveRange + 1);
    const int row = beast.row() + dice_.nextInt(-kind.moveRange, kind.moveRange + 1);
    if (tables_->grid.open(column, row, content::kWallCharacter)) send(beast, column, row);
}

void Realm::retreat(Body& beast) {
    if (beast.walking || tick_ < beast.repathsAt) return;
    beast.repathsAt = tick_ + kRepath;
    if (!send(beast, beast.homeColumn, beast.homeRow)) wander(beast);
}

void Realm::engage(Body& one, const Body& target) {
    halt(one);
    // Aimed at what it is hitting. A halt keeps whatever aim the walk it ended had, and a blow
    // is the only other thing that aims, so a fighter stopped in reach between two blows faces
    // wherever it was last walking -- MU2 measured a Skeleton Warrior 105 degrees off the
    // knight it was fighting.
    one.aim = std::atan2(target.y - one.y, target.x - one.x);
}

// Whether a chase needs planning again: the chaser has stopped, or its quarry has moved a whole
// tile from where it was when the line was drawn. Realm.cs:1985-1996, and the tile is MU2's
// `Drift`: under it the approach tile would not change anyway and re-planning only jitters the
// line; over it the walker is heading somewhere its quarry has left.
//
// This was `quarry.column() != chaseColumn`, which is a WHOLE-tile comparison and fires the
// moment a monster's rounded position changes -- so a fighter standing next to something that
// shuffled between two tiles re-planned three times a second and took a step each time. It read
// as a character walking while he was hitting something, which is what it was: measured over
// 4 000 ticks, 205 walk orders for 140 blows. With the drift it is 24.
bool Realm::drifted(const Body& chaser, const Body& target) const {
    constexpr float kDrift = 1.0f;
    return !chaser.walking ||
           std::fabs(target.x - chaser.chaseX) + std::fabs(target.y - chaser.chaseY) > kDrift;
}

// The nearest tile beside a target that a walker can both stand on and finish its approach
// from. Nearest rather than OpenMU's random one: the randomness is there to stop a pack
// converging on one tile, and nearest-to-the-walker keeps that spread by construction while
// being STABLE -- asked again a tick later from almost the same place it gives the same
// answer, so a chase that re-plans three times a second does not zig-zag.
bool Realm::beside(const Body& target, int radius, const Body& walker, int* column, int* row) {
    bool found = false;
    float closest = 1e30f;
    for (int down = -radius; down <= radius; ++down) {
        for (int across = -radius; across <= radius; ++across) {
            if (across == 0 && down == 0) continue;
            const int c = target.column() + across, r = target.row() + down;
            if (!tables_->grid.open(c, r, content::kWallCharacter)) continue;
            if (c == walker.column() && r == walker.row()) continue;
            // Standing here has to be close enough on the same measure the arrival will be
            // judged by -- the centre of this tile against where the target actually is. Without
            // this the approach and the arrival are in different spaces and can disagree
            // forever, which is a deadlock rather than a wobble.
            if (std::max(std::fabs(float(c) - target.x), std::fabs(float(r) - target.y)) >
                float(radius)) {
                continue;
            }
            const float toX = float(c) - walker.x, toY = float(r) - walker.y;
            const float far = toX * toX + toY * toY;
            if (!found || far < closest) {
                found = true;
                closest = far;
                *column = c;
                *row = r;
            }
        }
    }
    return found;
}

void Realm::think(Body& beast) {
    const content::MonsterKind& kind = tables_->kinds[size_t(beast.kind)];

    // The quarry it has, while it is worth having; else the nearest that is. A target learned
    // by being hit is not measured against eyesight at all -- what bounds that chase is the
    // grudge, which is asked first.
    uint32_t chosen = 0;
    if (strayed(beast) <= (beast.provoked ? kGrudge : kLeash)) {
        if (const Body* held = find(beast.quarry)) {
            const bool keep = beast.provoked
                                  ? held->alive() && !tables_->grid.safe(held->column(), held->row())
                                  : worth(beast, *held, kind.viewRange);
            if (keep) chosen = held->id;
        }
        if (chosen == 0) {
            float closest = 1e30f;
            for (uint32_t who : players_) {
                const Body& one = bodies_[who];
                if (!worth(beast, one, kind.viewRange)) continue;
                const float distance = reach(beast, one);
                if (distance < closest) {
                    closest = distance;
                    chosen = one.id;
                }
            }
        }
    }
    beast.quarry = chosen;

    if (chosen == 0) {
        // Lost, so the grudge goes with it: provoked qualifies a quarry and means nothing
        // without one.
        beast.provoked = false;
        if (strayed(beast) > kLeash) {
            beast.temper = Temper::Homing;
            retreat(beast);
            return;
        }
        beast.temper = Temper::Wandering;
        if (!beast.walking && tick_ >= beast.thinksAt) {
            beast.thinksAt = tick_ + kind.attackTicks;
            wander(beast);
        }
        return;
    }

    const Body& quarry = *find(chosen);
    // In reach: stop, face it, and swing when the swing comes off its own clock. A monster
    // standing in a safe zone cannot attack out of it, which is the same tile bit that stops
    // it being attacked there.
    if (within(beast, quarry, float(kind.attackRange)) &&
        !tables_->grid.safe(beast.column(), beast.row())) {
        beast.temper = Temper::Fighting;
        engage(beast, quarry);
        if (tick_ >= beast.swingsAt) {
            // The swing clock is the beast's own and is NOT the thinking clock. They were one
            // field in MU2 once, so making a monster think oftener made it hit oftener, which
            // is a fight the player loses for reasons nothing on screen explains. It is also
            // where a per-skill cooldown will go.
            beast.swingsAt = tick_ + kind.attackTicks;
            strikeAt(beast, *body(chosen));
        }
        return;
    }

    beast.temper = Temper::Chasing;
    const bool chases = beast.provoked || within(beast, quarry, float(kind.viewRange + 1));
    if (chases && tick_ >= beast.repathsAt) {
        beast.repathsAt = tick_ + kRepath;
        // Only when it has stopped or its quarry has moved a tile. Re-planning on the clock
        // alone is not harmless: the line is pulled tight from wherever the walker is standing,
        // so the same order given three times a second yields a slightly different line each
        // time and the animal picks its way over in a zig-zag.
        if (drifted(beast, quarry)) {
            beast.chaseX = quarry.x;
            beast.chaseY = quarry.y;
            int column = 0, row = 0;
            if (beside(quarry, std::max(1, kind.attackRange), beast, &column, &row) &&
                send(beast, column, row)) {
                return;
            }
        } else {
            return;
        }
    }

    if (!beast.walking && tick_ >= beast.thinksAt) {
        beast.thinksAt = tick_ + kind.attackTicks;
        wander(beast);
    }
}

// ---- the fight -------------------------------------------------------------------------

void Realm::strikeAt(Body& attacker, Body& target) {
    if (!target.alive()) return;  // no blow lands on the dead: the invariant, kept here
    const Blow blow = strike(attacker.stats, target.stats, dice_);
    if (!blow.hit) {
        say(What::Missed, attacker, 0, 0, 0, target.id);
        return;
    }
    target.health = std::max(0, target.health - blow.damage);
    say(What::Hit, attacker, blow.damage, blow.rolled, target.health, target.id);
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
    if (killer.player) {
        // (int) of the formula, as OpenMU's CalculateAfterKillAsync truncates it
        // (PlayerExperience.cs:105). No rate: a replica that quietly pays several times over
        // is not a replica.
        gain(killer, int32_t(killExperience(dead.level, killer.level)));
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
        hero.health = hero.maxHealth;
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

void Realm::press() {
    Body& hero = bodies_[0];
    if (!hero.alive()) return;

    if (pending_.kind != Request::Kind::None) {
        order_ = pending_;
        pending_ = Request{};
        if (order_.kind == Request::Kind::WalkTo) {
            send(hero, order_.column, order_.row);
        } else if (order_.kind == Request::Kind::Stop) {
            halt(hero);
            order_ = Request{};
        }
    }

    if (order_.kind != Request::Kind::Attack) return;
    const Body* target = find(order_.target);
    if (!target || !target->alive()) {
        order_ = Request{};
        return;
    }
    if (within(hero, *target, float(kHeroAttackRange)) &&
        !tables_->grid.safe(target->column(), target->row())) {
        engage(hero, *target);
        if (tick_ >= hero.swingsAt) {
            hero.swingsAt = tick_ + hero.swingTicks;
            strikeAt(hero, *body(order_.target));
        }
        return;
    }
    // Out of reach: close, on the same re-plan clock a monster's chase uses.
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
    if (hero.alive()) {
        advance(hero);
        press();
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
        case What::Levelled:
            std::snprintf(line, sizeof(line), "%6u %s reaches level %d with %d points",
                          happening.tick, who, happening.a, happening.b);
            break;
    }
    return std::string(line);
}

}  // namespace mu::sim
