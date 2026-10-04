#include "sim/realm.h"

#include "sim/swings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "core/log.h"
#include "sim/event.h"
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

bool Realm::nearPost(int column, int row) const {
    if (tables_->map != kClearedMap) return false;
    for (const content::Townsperson& person : tables_->folk) {
        if (wardenRow(person.number) == nullptr) continue;
        if (std::max(std::abs(column - person.x), std::abs(row - person.y)) <= kPostClearing) {
            return true;
        }
    }
    return false;
}

bool Realm::raise(const content::Tables* tables, uint64_t seed, int playerColumn,
                  int playerRow, Kin kin, int level) {
    tables_ = tables;
    if (!tables_ || tables_->grid.empty()) return false;
    own_.reset();
    if (tables_->map == kBloodCastleMap) {
        own_ = std::make_unique<content::Tables>(*tables_);
        tables_ = own_.get();
    }
    dice_.seed(seed);
    // A stream of its own, off the same seed: see `wearDice_`.
    wearDice_.seed(seed ^ 0x9e3779b97f4a7c15ull);
    wardenDice_.seed(seed ^ 0xc2b2ae3d27d4eb4full);
    runeDice_.seed(seed ^ 0x165667b19e3779f9ull);
    trapDice_.seed(seed ^ 0x27d4eb2f165667c5ull);
    bossDice_.seed(seed ^ 0x165667b19e3779f9ull);
    mixDice_.seed(seed ^ 0x85ebca6b2c1b3c6dull);
    crackerDice_.seed(seed ^ 0xd6e8feb86659fd93ull);
    featherDice_.seed(seed ^ 0x8c3b1e4f5a7d2961ull);
    gearDice_.seed(seed ^ 0x2f6a9c1d7e4b5803ull);
    ticketDice_.seed(seed ^ 0x9fb21c651e98df25ull);
    treasureDice_.seed(seed ^ 0x3c6ef372fe94f82bull);
    orbDice_.seed(seed ^ 0x4cf5ad432745937full);
    for (int slot = 0; slot < kWorn; ++slot) {
        wearCarry_[slot] = 0.0;
        wearItem_[slot] = -1;
    }
    router_.open(&tables_->grid);
    bodies_.clear();
    happenings_.clear();
    happenings_.reserve(4096);
    castleOwed_ = 0;
    staffOwed_ = false;
    // Raised on a castle, the run's wait starts and the entrance is shut until it ends
    // (WebZen BloodCastle.cpp:1128-1171: the court's 60 s, the barrier lifted at :887-917).
    run_ = CastleRun{};
    if (tables_->map == kBloodCastleMap) {
        run_.phase = CastlePhase::Waiting;
        run_.startsAt = int64_t(kCastleWait) * kCastleTicksPerSecond;
        changeGrid(kCastleEntrance.x1, kCastleEntrance.y1, kCastleEntrance.x2, kCastleEntrance.y2,
                   kCastleEntrance.bits, true);
    }
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
        hero.learned |= uint64_t(1) << skillIndexOf(skill::kEnergyBall);
    }
    bodies_.push_back(std::move(hero));
    reswing(bodies_[0]);
    settleFound(0);

    // Then every nest, in the table's own order. Placement rejects a tile the threshold
    // refuses and draws again: between 6% and 12% of every Lorencia nest rectangle is
    // un-standable, so blind placement puts monsters inside walls. Each attempt costs two
    // draws, which is itself part of the seeded stream and is why the attempt count is
    // bounded rather than "until it works".
    size_t placed = 0, short_ = 0;
    // And kept clear of Noria's guard posts (kPostClearing).
    const auto byPost = [&](int column, int row) { return nearPost(column, row); };
    for (const content::MonsterNest& nest : tables_->nests) {
        const content::MonsterKind& kind = tables_->kinds[nest.kind];
        // A spot of many -- one tile with a count, Devias's two camps of ten Elite Yetis -- is a
        // MonsterSetBase point row whose scatter distance OpenMU's parser drops
        // (BaseMapInitializer.cs:188-192), so as written all ten stand on one tile. Scattered
        // here over free tiles within kPointScatter of it, each on a tile of its own. A nest
        // that is a box draws as it always did, so no other map's dice move.
        const bool point = nest.x1 == nest.x2 && nest.y1 == nest.y2 && nest.count > 1;
        // Blood Castle's courtyard and statue hall are shut behind its door until the run opens
        // it, and WebZen raises their garrison all the same, boxed in (SetMonster,
        // BloodCastle.cpp:887-917): a tile shut only by the entrance's or the door's boxes stands
        // one. Without this 23 of the castle's monsters were never raised, and seven of its eight
        // Spirit Sorcerers. The Giant Ogre MonsterSetBase puts on the drawbridge's gap (14,70)
        // still stands nowhere.
        const auto castleShut = [&](int column, int row) {
            if (tables_->map != kBloodCastleMap) return false;
            if ((tables_->grid.at(column, row) & (content::kNoGround | content::kCharacter)) != 0) {
                return false;
            }
            for (const GridBox& box : {kCastleEntrance, kCastleDoor[0], kCastleDoor[1], kCastleDoor[2]}) {
                if (column >= box.x1 && column <= box.x2 && row >= box.y1 && row <= box.y2) return true;
            }
            return false;
        };
        std::vector<std::pair<int, int>> taken;
        for (uint32_t n = 0; n < nest.count; ++n) {
            int tileColumn = 0, tileRow = 0;
            bool found = false;
            for (int attempt = 0; attempt < 20 && !found; ++attempt) {
                const int reach = point ? kPointScatter : 0;
                tileColumn = dice_.nextInt(nest.x1 - reach, nest.x2 + reach + 1);
                tileRow = dice_.nextInt(nest.y1 - reach, nest.y2 + reach + 1);
                // Not in the town, either: two of Lorencia's nests clip the safe zone by a
                // fraction of a percent, which is enough to put a Hound inside the ring where
                // nothing may be attacked.
                found = (tables_->grid.open(tileColumn, tileRow, content::kWallCharacter) ||
                         castleShut(tileColumn, tileRow)) &&
                        !tables_->grid.safe(tileColumn, tileRow) && !byPost(tileColumn, tileRow);
                if (found && point) {
                    for (const auto& one : taken) {
                        if (one.first == tileColumn && one.second == tileRow) found = false;
                    }
                }
            }
            if (found && point) taken.emplace_back(tileColumn, tileRow);
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
            beast.nest = int32_t(&nest - tables_->nests.data());
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
    // The traps take no id: they are not bodies (realm_traps.cpp).
    raiseTraps();
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

    // Blood Castle's garrison is not there in the court's wait: WebZen raises it as the run
    // starts (SetMonster, BloodCastle.cpp:887-917; the user, 2026-10-03: 'i remember that
    // monsters was not rendered before timer'). Each is raised down, to rise -- on a tile drawn
    // from its nest, as any respawn -- the tick the gate opens; the Statue of Saint not until
    // the run calls it up. A body raised down is never drawn until it rises.
    if (run_.phase == CastlePhase::Waiting) {
        for (Body& one : bodies_) {
            if (!one.monster()) continue;
            // The statue lies along the slope of the wedge of stone it is drawn on (play_show.cpp;
            // Object13 at angle 0), its back on the stone and its head at the top -- MU's .obj
            // and Object13's fitted together at MU's angle 0. Facing up the rows; the other way
            // sank its body into the stone, photographed. It never turns (Realm::fixed).
            if (fixed(one)) one.facing = one.aim = -3.14159265359f / 2.0f;
            one.health = 0;
            const int32_t number = tables_->kinds[size_t(one.kind)].number;
            // The statue and the Spirit Sorcerers are called up by the run (Realm::castleTick):
            // the sorcerers as the bridge lands, the statue when they are dead.
            one.risesAt = castleStatue(number) || castleSorcerer(number)
                              ? std::numeric_limits<int64_t>::max()
                              : run_.startsAt;
        }
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
    if (hero.mightUntil > tick_) {
        out.might = hero.might;
        out.mightTicksLeft = hero.mightUntil - tick_;
    }
    for (int i = 0; i < kSkills; ++i) out.coolsLeft[i] = std::max<int64_t>(0, hero.cools[i] - tick_);
    for (int slot = 0; slot < kSlots; ++slot) out.slots[slot] = bag_[slot];
    for (int i = 0; i < kQuests; ++i) out.quests[i] = quests_[i];
    out.found = found_;
    if (const Body* summon = summoned(); summon != nullptr && summon->alive()) {
        out.summonSkill = summon->summonedBy;
        out.summonHealth = summon->health;
    }
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
    hero.second = sim::promoted(quests_, int(hero.kin));
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
    // And her Greater Damage, no longer than the skill lasts and no more than her energy gives
    // now, also before rearm, which is what carries it into her blows.
    if (const SkillRow* row = skillNumbered(skill::kGreaterDamage);
        row != nullptr && saved.might > 0 && saved.mightTicksLeft > 0) {
        hero.might = std::min(saved.might, int32_t(mightOf(hero.points)));
        hero.mightUntil = tick_ + std::min<int64_t>(saved.mightTicksLeft, row->mightTicks);
    }
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
    settleFound(saved.found);
    // Her summon, raised on the next tick at the health it was saved with: a summon skill she
    // knows, or none.
    const SkillRow* summons = skillNumbered(saved.summonSkill);
    const bool owed = summons != nullptr && summons->summons > 0 && knows(summons->number);
    summonOwed_ = owed ? summons->number : 0;
    summonOwedHealth_ = owed ? saved.summonHealth : 0;
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

    // **Every skill is walked out of**, Teleport and the auras excepted. The user, 2026-10-02: "any
    // spell ahs to be cancelable but without sliding bug when animation is played and char
    // moves" -- which ends the rule of 2026-09-23 that a skill, once thrown, plays to the end of
    // its clip. The hold itself stays: it is what keeps him from turning and re-pathing on his
    // own under the animation. A click that would take a step -- the ground, a thing on the
    // floor, a townsperson, a seat -- breaks it instead: the blow not yet landed is dropped
    // below as a swing's is, a channel stops pulsing, and the drawing cuts the clip the tick he
    // walks (play_show.cpp), so no cast is ever drawn over a moving body.
    //
    // Teleport's hold is its fade and settle, and a self-cast's -- a buff, Heal, a summon -- is
    // its whole clip (the user, the same day: "dont allow to cancel any aura casts"): a click
    // there is dropped where it stands as every one was before, so he does not set off the
    // moment it ends.
    if (casting() &&
        (pending_.kind == Request::Kind::WalkTo || pending_.kind == Request::Kind::Pick ||
         pending_.kind == Request::Kind::Talk || pending_.kind == Request::Kind::Perch)) {
        if (!hero.castBreaks) {
            pending_ = Request{};
        } else {
            hero.castUntil = tick_;
            if (hero.channelSkill != skill::kNone) {
                core::logf("cast: tick %lld, a click ends the channel", (long long)tick_);
                hero.channelSkill = skill::kNone;
                hero.channelEcho = false;
                hero.channelUntil = tick_;
                // The channel's own length stood on the swing clock; walked out of, he owes
                // no more than one swing of his own.
                hero.swingsAt = std::min(hero.swingsAt, tick_ + hero.swingTicks);
            }
        }
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
        closeMachine();
        gating_ = -1;
        angeling_ = -1;
        // **And a skill still waiting to be thrown is dropped by an order that moves him**: a
        // wish outlives a channel (kWishTicks), so a key pressed during Lightning threw it again
        // round him after he had walked on (the user, 2026-09-30: "when i am done with casting
        // spell, and move on it still is casted").
        if (order_.kind == Request::Kind::WalkTo || order_.kind == Request::Kind::Pick ||
            order_.kind == Request::Kind::Talk || order_.kind == Request::Kind::Perch) {
            wants_ = skill::kNone;
        }
        if (order_.kind == Request::Kind::WalkTo) {
            send(hero, order_.column, order_.row);
        } else if (order_.kind == Request::Kind::Attack && arrowless(hero, order_)) {
            // A bow with nothing to loose is not drawn at all: no walk in, no fight clock, no
            // stance -- only MuMain's "no more arrows" (the user, 2026-10-03: "dont even go to
            // combat stance ... but play error sound"). The swing's own nock stays the check
            // for a quiver that runs dry mid-fight.
            say(What::Arrowless, hero, hero.archer);
            halt(hero);
            order_ = Request{};
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
            // Said, because the window says nothing: the key's sweep answers a cooldown and
            // nothing answers the rest. The user pressed a Meteorite that never fell and neither
            // of us could say why.
            const uint32_t at = wantsAt_ != 0 ? wantsAt_ : order_.target;
            const Body* target = find(at);
            const SkillRow* row = skillNumbered(wants_);
            core::logf("skill: %s pressed and never thrown -- target #%u %s, %.1f tiles (reach "
                       "%.0f), cooling %lld, mana %d of %d, he is %s the safe zone",
                       row ? row->name : "?", at,
                       !target ? "none" : !target->alive() ? "dead"
                       : tables_->grid.safe(target->column(), target->row()) ? "sheltered"
                                                                             : "standing",
                       target ? double(reach(hero, *target)) : -1.0, row ? double(row->reach) : 0.0,
                       (long long)cooling(wants_), hero.mana, row ? row->mana : 0,
                       tables_->grid.safe(hero.column(), hero.row()) ? "in" : "out of");
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
    // And a Frenzy's speed, the same way.
    if (hero.frenzyUntil != 0 && tick_ >= hero.frenzyUntil) {
        hero.frenzyUntil = 0;
        hero.frenzyStacks = 0;
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
            // A quest giver spoken to opens his town on the travel list (sim/travel.h), whatever
            // he has to say, even that he is not ready for him yet.
            if (questOf(one.number) >= 0) discover(int32_t(tables_->map));
            if (sells(one.number)) {
                trading_ = int(order_.target);
                say(What::Served, hero, trading_, one.number);
            } else if (one.number == kVaultKeeper) {
                banking_ = int(order_.target);
                say(What::Served, hero, banking_, one.number);
            } else if (one.number == kChaosGoblin) {
                mixing_ = int(order_.target);
                say(What::Served, hero, mixing_, one.number);
            } else if (const int quest = questHere(one.number);
                       quest >= 0 && (!questLocked(quest) || questListed(one.number))) {
                // A quest giver: his dialog opens, whatever it has to say -- the offer, the
                // quest under way, the hand-in, or that it is not his to give again yet; or his
                // list, even of one quest waiting on the hero's level (questListed).
                questing_ = int(order_.target);
                say(What::Offered, hero, quest, questing_, int(quests_[quest].state));
            } else if (one.number == kArchangel) {
                // His page of the Event window: the staff he asks for, and Give.
                angeling_ = int(order_.target);
                say(What::Served, hero, angeling_, one.number);
            } else if (one.number == kMessenger) {
                // His page of the quest window: the ticket he asks for and the door
                // (QuestDialog::kGate).
                gating_ = int(order_.target);
                say(What::Served, hero, gating_, one.number);
            } else if (one.number == kGuildMaster || one.number == kCharon ||
                       one.number == kThompson ||
                       questOf(one.number) >= 0) {
                // Ours (the user, 2026-09-29): MU opens the guild window here, which a single
                // player game has no use for, so he answers with a line instead of nothing.
                // Charon likewise, whose Devil Square window has nothing behind it yet.
                // And a giver whose quest waits on another (Devin, until Lorencia or Noria is
                // cleared) or on a level (Sevina, until 200): not ready.
                // And Thompson, who has only his memory of the Lost Tower to tell.
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
    const int bulk = bulkOf(numberOf(*target));
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
            } else if (within(hero, *target, row->reach + float(bulk)) && !sheltered &&
                       seen(hero, *target)) {
                if (row->thrown() || cooled) {
                    if (tick_ >= hero.castUntil) engage(hero, *target);
                    if (cooled && tick_ >= hero.swingsAt) throwSkill(hero, *row, order_.target);
                    return;
                }
            } else if (row->thrown()) {
                // Out of reach, or in it with a wall between: to where it can be thrown from.
                approach(hero, *target, int(row->reach) + bulk, true);
                return;
            }
        }
    }

    const int reachOf = (hero.archer != 0 ? kArcherReach : kHeroAttackRange) + bulk;
    if (within(hero, *target, float(reachOf)) &&
        !tables_->grid.safe(target->column(), target->row()) && seen(hero, *target)) {
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
    approach(hero, *target, reachOf, true);
}

void Realm::approach(Body& hero, const Body& target, int radius, bool sight) {
    if (tick_ < hero.swingsAt) return;
    if (tick_ >= hero.repathsAt) {
        hero.repathsAt = tick_ + kRepath;
        if (drifted(hero, target)) {
            hero.chaseX = target.x;
            hero.chaseY = target.y;
            int column = 0, row = 0;
            // A tile in sight first; with none in reach, up to it, round the wall.
            if (beside(target, radius, hero, &column, &row, sight) ||
                (sight && beside(target, 1, hero, &column, &row))) {
                send(hero, column, row);
            }
        }
    }
}

// ---- the tick --------------------------------------------------------------------------

void Realm::step() {
    ++tick_;
    happenings_.clear();
    castleTick();
    if (castleOwed_ != 0) {
        const int castle = castleOwed_;
        castleOwed_ = 0;
        passCastle(castle);
    }

    // The order is fixed and is written down because it is the behaviour: the player walks and
    // swings, then every monster is roused, thinks and moves in index order, then the dead are
    // considered for respawn. Nothing here walks a hash container, and every id came from one
    // monotonic counter.
    Body& hero = bodies_[0];
    if (summonOwed_ != 0) {
        const SkillRow* row = skillNumbered(summonOwed_);
        if (hero.alive() && row != nullptr && conjure(hero, *row)) {
            Body& summon = bodies_[size_t(summonSlot_)];
            if (summonOwedHealth_ > 0) {
                summon.health = std::min(summonOwedHealth_, summon.maxHealth);
            }
        }
        summonOwed_ = 0;
    }
    sip();
    recover(hero);
    // The floor he stands on, opened in the travel list when this map opens floor by floor.
    reachFloor();
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
        // Evil Spirit's held blows, each on its own tick.
        for (SpiritBlow& one : spiritBlows_) {
            if (one.at == 0 || tick_ < one.at || !hero.alive()) continue;
            const SpiritBlow blow = one;
            one = SpiritBlow{};
            spiritStrike(hero, blow);
        }
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
                    if (tables_->grid.safe(one.column(), one.row()) || !seen(hero, one)) continue;
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
        // And whatever he let go earlier and has now arrived, and the fires on the ground.
        arrive();
        burn();
        channel(hero);
        if (hero.blinkAt != 0 && tick_ >= hero.blinkAt) blink(hero);
        poisonPulse(hero);
        accept();
        // Pushed by a beast's Lightning: he slides as a pushed monster does, and walks on when
        // he lands. The push goes when the bolt lands; one whose beast is far off by then --
        // he teleported or left the map -- is dropped.
        if (hero.pushAt != 0 && tick_ >= hero.pushAt) {
            hero.pushAt = 0;
            const float dx = hero.x - hero.pushFromX, dy = hero.y - hero.pushFromY;
            if (dx * dx + dy * dy < 12.0f * 12.0f) push(hero, hero.pushFromX, hero.pushFromY);
        }
        if (hero.pushTicks > 0) {
            hero.x += hero.pushX;
            hero.y += hero.pushY;
            if (--hero.pushTicks == 0) {
                hero.x = float(hero.column());
                hero.y = float(hero.row());
            }
        } else {
            advance(hero);
        }
        press();
        fireTraps();
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
        beamOn(beast);
        if (beast.alive() && beast.pushTicks > 0) {
            // Pushed: it slides and does nothing else until it lands on its tile.
            beast.x += beast.pushX;
            beast.y += beast.pushY;
            if (--beast.pushTicks == 0) {
                beast.x = float(beast.column());
                beast.y = float(beast.row());
                // Landed from a Whirlwind's pull: the slash's blow now, if it is beside him.
                if (beast.whirledBy != 0) {
                    Body* by = body(beast.whirledBy);
                    const SkillRow* slash = skillNumbered(skill::kTwistingSlash);
                    if (by && by->alive() && slash && within(*by, beast, slash->reach)) {
                        strikeAt(*by, beast, beast.whirlForce, nullptr, false);
                    }
                    beast.whirledBy = 0;
                }
            }
        } else if (beast.alive() && fixed(beast)) {
            // The statue: struck where it stands, and nothing else.
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
        case What::Gated:
            std::snprintf(line, sizeof(line), "%6u %s goes through gate %d, out at %d,%d",
                          happening.tick, who, happening.a, happening.b, happening.c);
            break;
        case What::Climbed:
            std::snprintf(line, sizeof(line), "%6u %s takes the stair of gate %d to %d,%d",
                          happening.tick, who, happening.a, happening.b, happening.c);
            break;
        case What::Trapped:
            std::snprintf(line, sizeof(line), "%6u %s is caught by trap %d for %d, %d left",
                          happening.tick, who, happening.b, happening.a, happening.c);
            break;
        case What::Spirits:
            if (happening.whom == 0) {
                std::snprintf(line, sizeof(line), "%6u %s casts evil spirit", happening.tick, who);
            } else {
                std::snprintf(line, sizeof(line), "%6u %s lets evil spirits go off %s's miss",
                              happening.tick, who, name(happening.whom).c_str());
            }
            break;
        case What::Barred:
            if (happening.b == 0) {
                std::snprintf(line, sizeof(line), "%6u %s finds gate %d sealed", happening.tick,
                              who, happening.a);
            } else {
                std::snprintf(line, sizeof(line),
                              "%6u %s is too low for gate %d, which asks level %d",
                              happening.tick, who, happening.a, happening.b);
            }
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
        case What::Enlivened:
            std::snprintf(line, sizeof(line), "%6u %s put a Life on slot %d: option %d to %d",
                          happening.tick, who, happening.a, happening.b, happening.c);
            break;
        case What::Mixed:
            std::snprintf(line, sizeof(line), "%6u %s mixed recipe %d at %d%%: %s",
                          happening.tick, who, happening.a, happening.c,
                          happening.b ? "made" : "failed");
            break;
        case What::Cracked:
            if (happening.a < 0) {
                std::snprintf(line, sizeof(line), "%6u %s cracked a firecracker: %d Zen",
                              happening.tick, who, happening.c);
            } else {
                std::snprintf(line, sizeof(line), "%6u %s cracked a firecracker: item %d +%d as #%d",
                              happening.tick, who, happening.b, happening.c, happening.a);
            }
            break;
    }
    return std::string(line);
}

bool Realm::changeGrid(int x1, int y1, int x2, int y2, uint16_t bits, bool set) {
    if (!own_) return false;
    own_->grid.change(x1, y1, x2, y2, bits, set);
    return true;
}

}  // namespace mu::sim
