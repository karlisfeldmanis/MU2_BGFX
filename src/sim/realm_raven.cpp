// **The Dark Lord's Dark Raven** in the realm (sim/lord_pets.h; docs/plans/dark-lord-pets/README.md
// decisions 5-7, server.md section 1): WebZen 1.00.93's CDarkSpirit (DarkSpirit.cpp). It is no body
// of its own: a strike let go from him on WebZen's 500 ms timer by its mode, landed 600 ms on, its
// kill, its exp and its drop his. Ours: monsters only -- the PvP of With owner and Target waits for
// the duel rules.
#include "sim/realm.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "sim/wear.h"

namespace mu::sim {
namespace {

// gObjCalDistance: the whole tiles between two bodies.
int tilesApart(const Body& a, const Body& b) {
    const float dx = float(a.column() - b.column()), dy = float(a.row() - b.row());
    return int(std::sqrt(dx * dx + dy * dy));
}

}  // namespace

int Realm::setRavenMode(int mode, uint32_t target) {
    Body& hero = mine();
    if (hero.kin != Kin::DarkLord || mode < 0 || mode >= kRavenModes) return -1;
    if (mode == int(RavenMode::Target)) {
        const Body* aimed = body(target);
        if (aimed == nullptr || !aimed->alive() || !aimed->monster()) return -1;
        hero.ravenTarget = target;
    } else {
        hero.ravenTarget = 0;
    }
    hero.ravenMode = uint8_t(mode);
    say(What::RavenSet, hero, mode, 0, 0, hero.ravenTarget);
    return mode;
}

void Realm::raven(Body& hero) {
    if (hero.ravenLands != 0 && tick_ >= hero.ravenLands) ravenLand(hero);
    // Back to Normal on his death (user.cpp:22006), and when Target's pick is gone (ReSetTarget).
    if (!hero.alive()) {
        hero.ravenMode = 0;
        hero.ravenTarget = 0;
        hero.ravenLands = 0;
        return;
    }
    const RavenMode mode = RavenMode(hero.ravenMode);
    if (mode == RavenMode::Target) {
        const Body* aimed = body(hero.ravenTarget);
        if (aimed == nullptr || !aimed->alive()) {
            hero.ravenMode = 0;
            hero.ravenTarget = 0;
            say(What::RavenSet, hero, 0, 0, 0, 0);
            return;
        }
    }
    // MonsterAndMsgProc's 500 ms, and the next strike's time.
    if (tick_ % kRavenTimerTicks != 0 || tick_ < hero.ravenNext || hero.ravenLands != 0) return;
    if (mode == RavenMode::Normal || hero.kin != Kin::DarkLord || !tables_) return;
    const Held& worn = me().bag[kWeaponLeft];
    if (worn.empty() || worn.durability <= 0 || !darkRaven(tables_->items[size_t(worn.item)])) return;
    // Run()'s gates (DarkSpirit.cpp:94-128): never off a safe tile, never short of its Command.
    if (tables_->grid.safe(hero.column(), hero.row())) return;
    const int level = petLevel(worn), command = hero.totalPoints().command;
    if (command < ravenCommand(level)) return;

    uint32_t aimed = 0;
    bool dash = false, top = false;
    if (mode == RavenMode::Random) {
        // A monster within 5 of him, drawn (:186-262); a dash at the top of its band one time in
        // ten in three, else the ball.
        int count = 0;
        for (const Body& one : bodies_) {
            if (one.monster() && one.alive() && one.summoner == 0 && tilesApart(hero, one) < kRavenRandomReach) ++count;
        }
        if (count == 0) return;
        int pick = ravenDice_.nextInt(0, count);
        for (const Body& one : bodies_) {
            if (!one.monster() || !one.alive() || one.summoner != 0 || tilesApart(hero, one) >= kRavenRandomReach) continue;
            if (pick-- == 0) {
                aimed = one.id;
                break;
            }
        }
        dash = ravenDice_.nextInt(0, 10) < kRavenDashIn10;
        top = dash;
    } else {
        // With owner: what he last struck; Target: his pick. Within 7 of him (:279-398).
        const Body* one = body(mode == RavenMode::WithOwner ? hero.ravenLast : hero.ravenTarget);
        if (one == nullptr || !one->alive() || !one->monster() || tilesApart(hero, *one) >= kRavenReach) return;
        aimed = one->id;
        // With owner always the ball (WebZen's bug, kept); Target always the dash, 30% at the top
        // of its band (SendAttackMsg, :528-538).
        dash = mode == RavenMode::Target;
        top = dash && ravenDice_.nextInt(0, 10) < kRavenDashIn10;
    }
    hero.ravenAimed = aimed;
    hero.ravenDash = dash;
    hero.ravenTop = top;
    hero.ravenLands = tick_ + kRavenLandTicks;
    const int ms = std::max(0, 1500 - 10 * ravenSpeed(level, command));
    hero.ravenNext = tick_ + ms / 50;
    say(What::Ravened, hero, dash ? 0 : 1, 0, 0, aimed);
}

void Realm::ravenLand(Body& hero) {
    const uint32_t aimed = hero.ravenAimed;
    const bool dash = hero.ravenDash, top = hero.ravenTop;
    hero.ravenLands = 0;
    hero.ravenAimed = 0;
    if (!tables_ || !hero.alive()) return;
    const Held& worn = me().bag[kWeaponLeft];
    if (worn.empty() || worn.durability <= 0 || !darkRaven(tables_->items[size_t(worn.item)])) return;
    Body* first = body(aimed);
    if (first == nullptr || !first->alive()) return;
    const int level = petLevel(worn), command = hero.totalPoints().command;
    const int least = ravenLeast(level, command), most = ravenMost(level, command);
    // A worn scepter's magic power, halved, as a percent, less as it wears (GetAttackDamage,
    // :1720-1744; m_Magic is the row's and 3 a plus, zzzitem.cpp:752-778).
    int rise = 0;
    if (const Held& hand = me().bag[kWeaponRight]; !hand.empty()) {
        const content::ItemRow& row = tables_->items[size_t(hand.item)];
        if (scepter(row)) {
            rise = (row.magicPower + 3 * hand.refinement) / 2;
            rise -= int(float(rise) * wearCut(hand.durability, maximumDurability(row, hand)));
        }
    }
    const int attackRate = ravenAttackRate(level);
    // One blow on one body: rolled in its band (the top for a dash at the top), the scepter's rise,
    // less its defence, WebZen's floor of his level/10; then the miss (MissCheck, :1756-1835).
    const auto strike = [&](Body& target, bool ball) {
        int damage = top ? most : ravenDice_.nextInt(least, most + 1);
        damage += damage * rise / 100;
        damage -= target.stats.defense;
        damage = std::max(damage, hero.level / 10);
        const float blocking = target.stats.defenseRate;
        if (float(attackRate) < blocking) {
            // The all-miss: one in twenty lands, at 30% (MODIFY_SUCCESSBLOCKING_ALLMISS).
            if (ravenDice_.nextInt(0, 100) >= 5) {
                say(What::Missed, hero, 0, 0, target.health, target.id);
                return;
            }
            damage = damage * 30 / 100;
        } else if (float(ravenDice_.nextInt(0, attackRate)) < blocking) {
            say(What::Missed, hero, 0, 0, target.health, target.id);
            return;
        }
        // The ball's every body at damage / 1.5 (criticaldamage 0, :927-929).
        if (ball) damage = damage * 2 / 3;
        damage = std::max(1, damage);
        // Its life, worn by its own landed hits of 50 or more (:1630-1650).
        if (damage >= kRavenWearFrom) wearDown(kWeaponLeft, double(damage * 2 / 100) / double(kRavenWearTally));
        const int bite = std::min(damage, target.health);
        target.health -= bite;
        noteHurt(target, hero, bite);
        say(What::Hit, hero, bite, damage, target.health, target.id);
        happenings_.back().thrown = true;
        if (target.health <= 0) kill(target, hero);
    };
    if (dash) {
        strike(*first, false);
        return;
    }
    // The ball (RangeAttack, :401-492): the target, then the monsters within 4 of it -- the first
    // always, each after it one time in three, four at most.
    const Body around = *first;
    strike(*first, true);
    int more = 0;
    for (Body& one : bodies_) {
        if (more >= kRavenSplashMost) break;
        if (one.id == aimed || !one.monster() || !one.alive() || one.summoner != 0) continue;
        if (tilesApart(around, one) >= kRavenSplashReach) continue;
        if (more > 0 && ravenDice_.nextInt(0, 3) != 0) continue;
        ++more;
        strike(one, true);
    }
}

}  // namespace mu::sim
