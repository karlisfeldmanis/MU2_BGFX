// Sprint 5's tests: the rules against OpenMU's own numbers, the router against Lorencia's own
// grid, and two realms raised on one seed that must live the same life.
//
// This is what replaces Instrument, Audit and the bot. It needs no window, no device and no
// .glb -- only the cooked tables -- which is foundation 9 of PLAN.md and is the whole reason
// the sim was built before the thing that draws it.
//
//     cmake --build build --target sim_test && build/sim_test

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "content/tables.h"
#include "sim/audit.h"
#include "sim/items.h"
#include "sim/random.h"
#include "sim/realm.h"
#include "sim/route.h"
#include "sim/rules.h"
#include "sim/swings.h"

using namespace mu;

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool held, const char* what) {
    ++g_checks;
    if (held) return;
    ++g_failures;
    std::printf("  FAILED: %s\n", what);
}

void checkEqual(long long got, long long wanted, const char* what) {
    ++g_checks;
    if (got == wanted) return;
    ++g_failures;
    std::printf("  FAILED: %s -- got %lld, wanted %lld\n", what, got, wanted);
}

void checkNear(double got, double wanted, double slack, const char* what) {
    ++g_checks;
    if (std::fabs(got - wanted) <= slack) return;
    ++g_failures;
    std::printf("  FAILED: %s -- got %.9f, wanted %.9f\n", what, got, wanted);
}

// ---- the arithmetic ---------------------------------------------------------------------

void testRules() {
    std::printf("rules\n");

    // AttackableExtensions.cs:694-716.
    checkNear(sim::hitChance(100, 25), 0.75, 1e-9, "hit chance is 1 - defenseRate/attackRate");
    checkNear(sim::hitChance(25, 100), 0.03, 1e-9, "an out-rated attacker gets 0.03");
    checkNear(sim::hitChance(25, 25), 0.03, 1e-9, "equal rates are 0.03, not zero");

    // The cumulative curve, GameConfigurationInitializerBase.cs:87-100. Level 1 costs nothing;
    // these are the totals to BE each level.
    checkEqual((long long)sim::neededExperience(1), 0, "level 1 is free");
    checkEqual((long long)sim::neededExperience(2), 100, "level 2 costs 100 in all");
    checkEqual((long long)sim::neededExperience(3), 440, "level 3 costs 440 in all");
    checkEqual((long long)sim::neededExperience(10), 10 * 18 * 81, "level 10");
    // And the second branch past 255, which nothing will reach and which is kept so the curve
    // is not quietly wrong where nobody is looking.
    checkEqual((long long)sim::neededExperience(256),
               10ll * 264 * 255 * 255 + 1000ll * 9 * 0 * 0, "level 256 takes the second branch");

    // AttackableExtensions.cs:601-623. A spider is level 2: (2+25)*2/3 = 18, x1.25 = 22.5, and
    // the award site truncates (PlayerExperience.cs:105).
    checkNear(sim::killExperience(2, 1), 22.5, 1e-9, "a spider is worth 22.5");
    checkEqual((long long)int(sim::killExperience(2, 1)), 22, "and 22 once truncated");
    // Eleven levels above it and the penalty bites, in FLOAT division: (2+10)/13.
    checkNear(sim::killExperience(2, 13), 18.0 * (12.0 / 13.0) * 1.25, 1e-9,
              "the over-level penalty divides as a float");
    check(sim::killExperience(2, 13) > 1.0, "and does not collapse to zero");

    // The three classes at level 1, from their own initialisers.
    sim::Fighter fighter;
    int health = 0;
    sim::reckon(sim::Kin::DarkKnight, 1, sim::startingPoints(sim::Kin::DarkKnight), sim::Arms{}, &fighter,
                &health);
    checkEqual(fighter.attackRate, int(5 + 20 * 1.5 + 28 * 0.25), "the knight's attack rate");
    checkEqual(fighter.defenseRate, 6, "the knight's defense rate is agility/3");
    checkEqual(fighter.defense, 3, "the knight's defence is halved");
    checkEqual(fighter.minimumDamage, 4, "the knight's fists, low");
    checkEqual(fighter.maximumDamage, 7, "the knight's fists, high");
    checkEqual(health, 35 + 2 + 75, "the knight's health");

    sim::reckon(sim::Kin::DarkWizard, 1, sim::startingPoints(sim::Kin::DarkWizard), sim::Arms{}, &fighter,
                &health);
    checkEqual(health, 30 + 1 + 30, "the wizard's health");
    checkEqual(fighter.minimumDamage, 18 / 8, "the wizard's fists, low");

    sim::reckon(sim::Kin::FairyElf, 1, sim::startingPoints(sim::Kin::FairyElf), sim::Arms{}, &fighter,
                &health);
    checkEqual(health, 39 + 1 + 40, "the elf's health");
    // Hers is over strength AND agility together -- ClassFairyElf.cs:79-80 -- which is the one
    // thing the census's table of MU2's numbers did not say.
    checkEqual(fighter.minimumDamage, int((22 + 25) / 7.0), "the elf's melee, low");
    checkEqual(fighter.maximumDamage, int((22 + 25) / 4.0), "the elf's melee, high");

    // The order of a blow is the behaviour. A defender who out-rates the attacker takes three
    // tenths, and the level floor comes after that, not before.
    sim::Fighter attacker;
    attacker.level = 30;
    attacker.attackRate = 10;
    attacker.minimumDamage = 100;
    attacker.maximumDamage = 100;  // max == min draws nothing and gives exactly min
    sim::Fighter defender;
    defender.defenseRate = 1000;  // out-rates: the x0.3 arm
    defender.defense = 50;
    sim::Random dice(7);
    // Hit chance is 0.03 here, so the swing is rolled until one lands; what is checked is the
    // damage, not the frequency.
    sim::Blow blow;
    for (int i = 0; i < 10000 && !blow.hit; ++i) blow = sim::strike(attacker, defender, dice);
    check(blow.hit, "one of ten thousand swings landed");
    checkEqual(blow.rolled, 100, "max == min draws nothing and rolls the minimum");
    checkEqual(blow.damage, int((100 - 50) * 0.3), "defence, then overrates, then the floor");
    check(blow.damage > 30 / 10, "and the floor did not bite above it");

    // And the floor itself, on a blow the defence would otherwise erase.
    defender.defense = 500;
    blow = sim::Blow{};
    for (int i = 0; i < 10000 && !blow.hit; ++i) blow = sim::strike(attacker, defender, dice);
    checkEqual(blow.damage, 3, "a level 30 attacker floors at level/10");
}

// ---- the swing ----------------------------------------------------------------------------

// The chain a swing rate is made of, pinned at both ends: the client's ladder picks the clips,
// and the clips' own keys and authored speed plus the character's attack speed give the
// interval. If any link is quietly rewritten, these numbers move.
void testSwings(const content::Tables& tables) {
    std::printf("swings\n");

    const auto arm = [&tables](const char* name) -> const content::Arm* {
        const int32_t index = tables.armNamed(name);
        return index < 0 ? nullptr : &tables.arms[size_t(index)];
    };
    const content::Arm* kris = arm("Sword01");        // group 0, one-handed, speed 50
    const content::Arm* giant = arm("Sword16");       // group 0, two-handed
    const content::Arm* smallAxe = arm("Axe01");      // group 1 -- and it swings a SWORD
    const content::Arm* spear = arm("Spear02");       // group 3, number 1: named individually
    const content::Arm* scythe = arm("Spear09");      // group 3, and not one of the two named
    const content::Arm* shield = arm("Shield10");
    check(kris && giant && smallAxe && spear && scythe && shield, "the arms are all in the table");
    if (!kris || !giant || !smallAxe || !spear || !scythe || !shield) return;

    int32_t actions[4] = {};
    checkEqual(sim::attackActions(nullptr, nullptr, actions), 1, "empty hands are one action");
    checkEqual(actions[0], 38, "and it is the fist");
    checkEqual(sim::attackActions(kris, nullptr, actions), 2, "a one-handed sword has two");
    checkEqual(actions[0], 39, "right 1");
    checkEqual(actions[1], 40, "right 2");
    checkEqual(sim::attackActions(giant, nullptr, actions), 3, "a two-handed sword has three");
    checkEqual(actions[0], 43, "two hand sword 1");
    // The ladder's whole point: an axe is caught by the sword test three rungs above the
    // axe-shaped one anybody would write, because there is no axe action in the rig.
    sim::attackActions(smallAxe, nullptr, actions);
    checkEqual(actions[0], 39, "a Small Axe swings a sword");
    sim::attackActions(spear, nullptr, actions);
    checkEqual(actions[0], 46, "the Spear is named individually");
    checkEqual(sim::attackActions(scythe, nullptr, actions), 3, "the Great Scythe is not");
    checkEqual(actions[0], 47, "and falls to the scythe rung");
    // A shield alone is the fist again, which is the client's final else.
    sim::attackActions(nullptr, shield, actions);
    checkEqual(actions[0], 38, "a shield in the off hand swings nothing");

    // The stat, and then the interval. A Dark Knight buys attack speed at 1/15 an agility.
    checkNear(sim::attackSpeedStat(sim::Kin::DarkKnight, 30, nullptr, nullptr), 2.0, 1e-5,
              "agility alone, at the knight's rate");
    checkNear(sim::attackSpeedStat(sim::Kin::FairyElf, 50, nullptr, nullptr), 1.0, 1e-5,
              "and the elf's is slower");
    checkNear(sim::attackSpeedStat(sim::Kin::DarkKnight, 30, kris, nullptr), 52.0, 1e-5,
              "a weapon's own speed is added whole");

    // The arithmetic, worked by hand from MU's own numbers: the sword clips are 7 keys at an
    // authored 0.25, the bonus is 52 x 0.004 = 0.208, so the rate is (0.25 + 0.208) x 25 =
    // 11.45 frames a second and the clip is 7 / 11.45 = 0.611 s.
    const int withKris = sim::swingMilliseconds(tables, sim::Kin::DarkKnight, 30, kris, nullptr);
    checkNear(withKris, 611, 3, "a Kris swings about every 611 ms");
    // Bare hands are the fist clip, 7 keys at 0.6, with almost no bonus: much faster and much
    // weaker, which is 0.75's own shape.
    const int bare = sim::swingMilliseconds(tables, sim::Kin::DarkKnight, 30, nullptr, nullptr);
    check(bare < withKris, "and fists are faster than any weapon");
    checkNear(bare, 462, 3, "the fist is about 462 ms");
    // A slow weapon is slower: the Short Sword's 20 against the Kris's 50.
    const int slow = sim::swingMilliseconds(tables, sim::Kin::DarkKnight, 30, arm("Sword02"),
                                            nullptr);
    check(slow > withKris, "a Short Sword is slower than a Kris");
    // Ticks round UP, because a swing the body has not finished is a swing cut short.
    checkEqual(sim::swingTicks(611), 13, "611 ms is 13 ticks");
    checkEqual(sim::swingTicks(600), 12, "600 ms is 12");
    checkEqual(sim::swingTicks(601), 13, "and 601 is 13");
    checkEqual(sim::swingTicks(0), 0, "nothing is nothing, so a caller can keep its fallback");
}

// ---- the dice ----------------------------------------------------------------------------

void testRandom() {
    std::printf("random\n");
    sim::Random a(12345), b(12345);
    for (int i = 0; i < 1000; ++i) checkEqual((long long)a.next(), (long long)b.next(), "");
    g_checks -= 999;  // one check, not a thousand: the loop is one claim

    sim::Random dice(1);
    // Upper-exclusive, as OpenMU's Rand.NextInt is.
    bool sawLow = false, sawHigh = false, sawPast = false;
    for (int i = 0; i < 10000; ++i) {
        const int value = dice.nextInt(3, 6);
        sawLow |= value == 3;
        sawHigh |= value == 5;
        sawPast |= value == 6;
    }
    check(sawLow && sawHigh, "nextInt reaches both ends of [min, max)");
    check(!sawPast, "and never reaches max itself");
    checkEqual(dice.nextInt(4, 4), 4, "max <= min gives min");
    const uint64_t before = dice.draws();
    dice.nextInt(4, 4);
    checkEqual((long long)(dice.draws() - before), 0, "and draws nothing");
}

// ---- the router ----------------------------------------------------------------------------

void testRouter(const content::Tables& tables) {
    std::printf("router\n");
    sim::Router router;
    router.open(&tables.grid);
    std::vector<sim::Step> route;

    // A straight walk across open ground -- and the ground has to BE open, which took looking
    // for: the row through the middle of town is safe-zone-and-NoMove and a walk across it
    // detours. (173..186, 100) is fourteen clear tiles with clear rows either side of it.
    check(router.plan(173, 100, 183, 100, content::kWallCharacter, route), "a short walk plans");
    checkEqual((long long)route.size(), 10, "ten tiles for ten tiles of clear ground");
    int column = 173, row = 100;
    bool contiguous = true, standable = true;
    for (const sim::Step& step : route) {
        contiguous &= std::abs(step.column - column) <= 1 && std::abs(step.row - row) <= 1;
        standable &= tables.grid.open(step.column, step.row, content::kWallCharacter);
        column = step.column;
        row = step.row;
    }
    check(contiguous, "every step is one tile from the last");
    check(standable, "and no step stands where the grid refuses");

    // The same plan twice is the same route: the router keeps no state between searches that
    // could make it answer differently.
    std::vector<sim::Step> again;
    router.plan(173, 100, 183, 100, content::kWallCharacter, again);
    check(again.size() == route.size() &&
              std::memcmp(again.data(), route.data(), route.size() * sizeof(sim::Step)) == 0,
          "the same plan twice gives the same route");

    // A goal inside a wall is resolved to the nearest standable tile BEFORE the search, so the
    // plan does not explore the whole map and then fail.
    int blockedColumn = -1, blockedRow = -1;
    for (int r = 0; r < tables.grid.size() && blockedColumn < 0; ++r) {
        for (int c = 0; c < tables.grid.size(); ++c) {
            if (!tables.grid.open(c, r, content::kWallCharacter) && c > 100 && c < 160 &&
                r > 100 && r < 160) {
                blockedColumn = c;
                blockedRow = r;
                break;
            }
        }
    }
    check(blockedColumn >= 0, "the town has a blocked tile to aim at");
    if (blockedColumn >= 0) {
        const bool planned =
            router.plan(173, 100, blockedColumn, blockedRow, content::kWallCharacter, route);
        check(planned, "a walk toward a wall still plans, to the tile beside it");
        if (planned) {
            check(tables.grid.open(route.back().column, route.back().row,
                                   content::kWallCharacter),
                  "and ends somewhere standable");
        }
    }

    // Off the map is refused rather than clamped.
    check(!router.plan(173, 100, -5, 130, content::kWallCharacter, route),
          "off the map is refused");
    check(!router.plan(173, 100, 173, 100, content::kWallCharacter, route),
          "a walk to where you stand is refused");
}

// ---- the realm -----------------------------------------------------------------------------

// The proving sentence's first clause, in miniature: two realms on one seed, stepped side by
// side, must produce the same happenings in the same order with the same numbers. The 10 000
// tick version is the headless run and its two files are compared with `cmp`; this is the one
// that fails on the developer's own machine a minute after the mistake.
// Every happening of a run, folded into one number the way the headless log's fingerprint is.
// A hash and not a comparison so that the "different seed" half can be stated as plainly as
// the "same seed" half.
uint64_t liveFor(sim::Realm& realm, int ticks) {
    uint64_t hash = 0xcbf29ce484222325ull;
    const auto fold = [&hash](const void* data, size_t bytes) {
        const unsigned char* at = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < bytes; ++i) {
            hash ^= at[i];
            hash *= 0x100000001b3ull;
        }
    };
    fold(realm.happenings().data(), realm.happenings().size() * sizeof(sim::Happening));
    for (int tick = 0; tick < ticks; ++tick) {
        realm.step();
        fold(realm.happenings().data(), realm.happenings().size() * sizeof(sim::Happening));
    }
    return hash;
}

void testDeterminism(const content::Tables& tables) {
    std::printf("determinism\n");
    sim::Realm one, two, other;
    check(one.raise(&tables, 20260920, 138, 124), "the first realm raises");
    check(two.raise(&tables, 20260920, 138, 124), "the second realm raises");
    check(other.raise(&tables, 20260921, 138, 124), "and a third on another seed");

    const uint64_t first = liveFor(one, 400);
    const uint64_t second = liveFor(two, 400);
    const uint64_t third = liveFor(other, 400);
    check(first == second, "two realms on one seed live the same 400 ticks");
    checkEqual((long long)one.draws(), (long long)two.draws(), "and draw the same numbers");
    // Or the check above proves only that the sim is deaf.
    check(first != third, "another seed lives another life");
}

void testInvariants(const content::Tables& tables) {
    std::printf("invariants\n");
    sim::Realm realm;
    check(realm.raise(&tables, 5, 138, 124), "the realm raises");

    // Every monster was placed somewhere it may stand, and outside the town.
    bool placed = true, outside = true;
    for (const sim::Body& one : realm.bodies()) {
        if (one.player) continue;
        placed &= tables.grid.open(one.column(), one.row(), content::kWallCharacter);
        outside &= !tables.grid.safe(one.column(), one.row());
    }
    check(placed, "every monster stands on a tile the grid allows");
    check(outside, "and none of them inside the safe zone");

    sim::Findings findings;
    for (int tick = 0; tick < 600; ++tick) {
        realm.step();
        sim::audit(realm, findings);
    }
    checkEqual((long long)findings.onBlocked, 0, "nothing walked onto a blocked tile");
    checkEqual((long long)findings.belowZero, 0, "nothing fell below zero health");
    checkEqual((long long)findings.hitTheDead, 0, "no blow landed on the dead");
    checkEqual((long long)findings.pastTheLeash, 0, "nothing wandered past its leash");
    checkEqual((long long)findings.unpaidLevel, 0, "no level was won unpaid");
    for (const std::string& line : findings.first) std::printf("    %s\n", line.c_str());
}

// Sprint 7: the items. The requirement formula against the worked numbers MU2's Rows.cs gives
// for it, the footprint walk, and equipping as a move through the same gate a window colours by.
void testItems(const content::Tables& tables) {
    std::printf("items\n");
    checkEqual(long(tables.items.size()), 118, "118 item rows cooked");
    const int shield = tables.itemAt(6, 0), axe = tables.itemAt(1, 0), staff = tables.itemAt(5, 0);
    const int small = tables.itemAt(14, 1);
    check(shield >= 0 && axe >= 0 && staff >= 0 && small >= 0, "the rows the tests use exist");
    if (shield < 0 || axe < 0 || staff < 0 || small < 0) return;
    // Rows.cs: "the Small Shield at drop level 3 asks 3 x 3 x 70 / 100 + 20 = 26 strength".
    checkEqual(sim::asks(tables.items[size_t(shield)], 0).strength, 26,
               "a Small Shield asks 26 strength, not its raw 70");
    // And a plus raises the drop level three a step: +2 is drop level 9, 3 x 9 x 70 / 100 + 20.
    checkEqual(sim::asks(tables.items[size_t(shield)], 2).strength, 38, "and a +2 asks 38");
    checkEqual(sim::asks(tables.items[size_t(axe)], 0).strength, 21, "a Small Axe asks 21");
    checkEqual(sim::damageBonus(3), 9, "a +3 weapon adds 9 to both ends");
    checkEqual(sim::defenseBonus(true, 3), 3, "a +3 shield adds 3");
    checkEqual(sim::defenseBonus(false, 3), 9, "a +3 helm adds 9");
    checkEqual(sim::placeOf(tables.items[size_t(shield)]), sim::kWeaponLeft, "a shield is left");
    checkEqual(sim::placeOf(tables.items[size_t(axe)]), sim::kWeaponRight, "an axe is right");
    checkEqual(sim::placeOf(tables.items[size_t(small)]), -1, "a potion is not worn");

    // The footprint: a Small Axe is one across and three down, recorded once at its top left.
    sim::Satchel bag;
    bag.put(sim::kWorn, sim::Held{axe, 0, 0});
    checkEqual(bag.holder(tables, sim::kWorn + 8), sim::kWorn, "the axe covers the cell below");
    checkEqual(bag.holder(tables, sim::kWorn + 16), sim::kWorn, "and the one below that");
    checkEqual(bag.holder(tables, sim::kWorn + 24), -1, "and not a fourth");
    checkEqual(bag.free(tables, 1, 3), sim::kWorn + 1, "the next 1x3 goes beside it");
    check(!bag.room(tables, sim::kWorn + 7, 2, 2), "a 2x2 cannot start in the last column");

    sim::Realm realm;
    check(realm.raise(&tables, 7, 138, 124), "a realm raises for the items");
    check(realm.equip(tables.armNamed("Axe01"), -1, true), "the cradle's axe goes in his hand");
    checkEqual(realm.satchel()[sim::kWeaponRight].item, axe, "and it is in the right hand slot");
    check(realm.hero().weapon >= 0, "which is where his weapon is read from");
    const int minimumArmed = realm.hero().stats.minimumDamage;
    check(realm.moveItem(sim::kWeaponRight, sim::kWorn + 3), "taken off into the bag");
    checkEqual(realm.hero().weapon, -1, "and his hands are empty");
    check(realm.hero().stats.minimumDamage < minimumArmed, "and he hits for less");
    check(!realm.moveItem(sim::kWorn + 3, sim::kWeaponLeft), "an axe does not go in the left hand");
    check(realm.moveItem(sim::kWorn + 3, sim::kWeaponRight), "and back on, since 28 >= 21");
    checkEqual(realm.hero().stats.minimumDamage, minimumArmed, "and he hits for what he did");

    const int staffAt = realm.give(staff);
    check(staffAt >= sim::kWorn, "a Skull Staff goes into the bag");
    check(!sim::movable(tables, realm.wearer(), realm.satchel(), staffAt, sim::kWeaponRight),
          "and a knight may not hold it: the gate the window colours by");
    check(!realm.moveItem(staffAt, sim::kWeaponRight), "and the move is refused by the same gate");

    const int potionAt = realm.give(small, -1, 0, 3);
    check(potionAt >= sim::kWorn, "three small healing potions go into the bag");
    check(realm.useItem(potionAt), "one is drunk");
    checkEqual(realm.satchel()[potionAt].durability, 2, "and two are left");
    check(!realm.useItem(potionAt), "a second inside half a second is not drunk");
    for (int i = 0; i < 10; ++i) realm.step();
    check(realm.useItem(potionAt), "after it, it is");
    for (int i = 0; i < 10; ++i) realm.step();
    check(realm.useItem(potionAt), "and the last one");
    check(realm.satchel()[potionAt].empty(), "leaves the slot empty");
}

// Sprint 7's sentence, headless: kill, pick up, equip, sell. A plain hand hunts the field
// south-east of town (sprint 5's hunting ground), picks up whatever falls before it fights
// again, then puts on anything it can and sells the rest at Lumen's counter.
void testLoot(const content::Tables& tables) {
    std::printf("loot\n");
    sim::Realm realm;
    check(realm.raise(&tables, 1, 200, 160, sim::Kin::DarkKnight, 8), "a hunt raises");
    check(realm.equip(tables.armNamed("Axe01"), -1, true), "with the axe in hand");
    int dropped = 0, picked = 0, kills = 0;
    for (int tick = 0; tick < 12000; ++tick) {
        const sim::Body& hero = realm.hero();
        if (hero.alive() && realm.tick() % 10 == 0) {
            sim::Request request;
            float best = 12.0f * 12.0f;
            for (const sim::Lying& one : realm.lying()) {
                const float dx = float(one.column) - hero.x, dy = float(one.row) - hero.y;
                if (dx * dx + dy * dy < best) {
                    best = dx * dx + dy * dy;
                    request.kind = sim::Request::Kind::Pick;
                    request.target = one.id;
                }
            }
            if (request.kind == sim::Request::Kind::None) {
                for (const sim::Body& body : realm.bodies()) {
                    if (body.player || !body.alive()) continue;
                    const float dx = body.x - hero.x, dy = body.y - hero.y;
                    if (dx * dx + dy * dy < best) {
                        best = dx * dx + dy * dy;
                        request.kind = sim::Request::Kind::Attack;
                        request.target = body.id;
                    }
                }
            }
            // Nothing in reach -- which is also where he stands up after a death, in town -- and
            // he walks back to the field, as the headless hand does.
            if (request.kind == sim::Request::Kind::None && !hero.walking) {
                request.kind = sim::Request::Kind::WalkTo;
                request.column = 200;
                request.row = 160;
            }
            if (request.kind != sim::Request::Kind::None) realm.ask(request);
        }
        realm.step();
        if (std::getenv("LOOT_TRACE") && tick % 1000 == 0) {
            std::printf("    tick %d hero %.1f,%.1f hp %d walking %d\n", tick, hero.x, hero.y,
                        hero.health, int(hero.walking));
        }
        for (const sim::Happening& h : realm.happenings()) {
            if (std::getenv("LOOT_TRACE") && h.what != sim::What::Stepped) {
                std::printf("    %s\n", sim::describe(h, realm).c_str());
            }
            dropped += h.what == sim::What::Dropped;
            picked += h.what == sim::What::Picked && h.b >= 0;
            kills += h.what == sim::What::Died && h.who != realm.hero().id;
        }
    }
    std::printf("  %d kills, %d drops, %d items picked up, %lld Zen\n", kills, dropped, picked,
                (long long)realm.money());
    check(kills > 0 && dropped > 0, "the hunt kills and things fall");
    check(picked > 0, "an item was picked up into the bag");
    check(realm.money() > 0, "and Zen into the purse");

    // His points, as a player spends them at the character window: into strength, which is
    // what most of what a knight finds asks for. Then equip whatever fits, straight from the
    // bag through the same move a drag makes.
    realm.spend(realm.hero().pointsInHand, 0, 0, 0);
    int worn = 0;
    for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
        const sim::Held& one = realm.satchel()[slot];
        if (one.empty()) continue;
        const content::ItemRow& row = tables.items[size_t(one.item)];
        const int place = sim::placeOf(row);
        const bool on = place >= 0 && realm.moveItem(slot, place);
        std::printf("    found %s +%d: %s\n", row.label.c_str(), one.refinement,
                    on ? "put on" : (place < 0 ? "not worn" : "refused"));
        if (on) ++worn;
    }
    std::printf("  %d pieces put on\n", worn);

    // And sell the rest at Lumen's: walk there, be served, sell every bag slot.
    int lumen = -1;
    for (size_t i = 0; i < tables.folk.size(); ++i) {
        if (tables.folk[i].number == 255) lumen = int(i);
    }
    check(lumen >= 0, "Lumen is in the town's table");
    if (lumen < 0) return;
    sim::Request talk;
    talk.kind = sim::Request::Kind::Talk;
    talk.target = uint32_t(lumen);
    realm.ask(talk);
    for (int tick = 0; tick < 3000 && realm.trading() < 0; ++tick) realm.step();
    check(realm.trading() == lumen, "walked back to town and served at the bar");
    const int64_t before = realm.money();
    int sold = 0;
    for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
        if (!realm.satchel()[slot].empty() && realm.sellItem(slot) >= 0) ++sold;
    }
    std::printf("  %d sold for %lld Zen\n", sold, (long long)(realm.money() - before));
    check(worn > 0, "something found was put on");
    check(sold > 0, "and the rest sold");
    check(realm.sellItem(sim::kWeaponRight) < 0, "and what is worn is never sold");
}

}  // namespace

int main() {
    const std::string path =
        std::string(MU2_ASSET_DIR) + "/cooked/lorencia/lorencia.mur";
    content::Tables tables;
    std::string error;
    if (!content::loadTables(path, tables, error)) {
        std::printf("sim_test: %s: %s\n", path.c_str(), error.c_str());
        std::printf("  (run tools/cook.py --world lorencia --only tables first)\n");
        return 1;
    }
    std::printf("tables: %zu breeds, %zu nests, %u monsters, grid %d\n", tables.kinds.size(),
                tables.nests.size(), tables.population(), tables.grid.size());

    testRules();
    testSwings(tables);
    testRandom();
    testRouter(tables);
    testDeterminism(tables);
    testInvariants(tables);
    testItems(tables);
    testLoot(tables);

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
