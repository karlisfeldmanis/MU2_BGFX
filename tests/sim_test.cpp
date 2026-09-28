// Sprint 5's tests: the rules against OpenMU's own numbers, the router against Lorencia's own
// grid, and two realms raised on one seed that must live the same life.
//
// This is what replaces Instrument, Audit and the bot. It needs no window, no device and no
// .glb -- only the cooked tables -- which is foundation 9 of PLAN.md and is the whole reason
// the sim was built before the thing that draws it.
//
//     cmake --build build --target sim_test && build/sim_test

#include <algorithm>
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
#include "sim/realm_tuning.h"
#include "sim/realm.h"
#include "sim/route.h"
#include "sim/rules.h"
#include "sim/skills.h"
#include "sim/swings.h"
#include "sim/wear.h"

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
    // SD: 1.2 x (28 + 20 + 25 + 10), his final defence 3, and 1/30 of level squared.
    checkEqual(sim::maximumShield(1, sim::startingPoints(sim::Kin::DarkKnight), fighter.defense),
               102, "the knight's shield");

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
    // MU's fist (38) is not used: an empty hand swings the sword's pair (ours, sim/swings.cpp).
    checkEqual(sim::attackActions(nullptr, nullptr, actions), 2, "empty hands swing the sword's pair");
    checkEqual(actions[0], 39, "and not the fist");
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
    // A shield alone is the client's final else, the fist, which is the empty hand's swing here.
    sim::attackActions(nullptr, shield, actions);
    checkEqual(actions[0], 39, "a shield in the off hand swings as an empty hand does");

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

    // Pulled tight. Across open ground a straight walk is one leg; and over a sweep of long
    // walks through the town, every leg of every pulled route is clear by Router::sees, the
    // legs are fewer than the tiles, and the route still ends where the plan did.
    {
        std::vector<sim::Step> pulled = route;
        router.pull(173.0f, 100.0f, content::kWallCharacter, pulled);
        checkEqual((long long)pulled.size(), 1, "a straight walk on open ground is one leg");
        int walks = 0, fewer = 0;
        bool clear = true, sameEnd = true;
        for (int i = 0; i < 40; ++i) {
            const int fromColumn = 120 + (i * 7) % 60, fromRow = 110 + (i * 11) % 50;
            const int toColumn = 120 + (i * 13 + 29) % 60, toRow = 110 + (i * 17 + 23) % 50;
            if (!tables.grid.open(fromColumn, fromRow, content::kWallCharacter)) continue;
            if (!router.plan(fromColumn, fromRow, toColumn, toRow, content::kWallCharacter,
                             pulled)) {
                continue;
            }
            const sim::Step end = pulled.back();
            const size_t tiles = pulled.size();
            router.pull(float(fromColumn), float(fromRow), content::kWallCharacter, pulled);
            ++walks;
            fewer += pulled.size() < tiles;
            sameEnd &= pulled.back().column == end.column && pulled.back().row == end.row;
            float atX = float(fromColumn), atY = float(fromRow);
            for (const sim::Step& leg : pulled) {
                clear &= router.sees(atX, atY, float(leg.column), float(leg.row),
                                     content::kWallCharacter);
                atX = float(leg.column);
                atY = float(leg.row);
            }
        }
        check(walks > 10, "the sweep found walks to pull");
        check(clear, "every pulled leg touches only open tiles");
        check(sameEnd, "and a pulled route ends where the plan did");
        check(fewer * 2 > walks, "and most walks come out with fewer points than tiles");
    }

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
        if (!one.monster()) continue;
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
    // 128: the catalogue's 118, the nine knight orbs added on 2026-09-23 and the wizard's Scroll
    // of Soul Barrier on 2026-09-28. A count rather than a list, because what it is guarding is
    // the cook -- a recipe that stops being picked up is a row the shelf silently cannot sell.
    checkEqual(long(tables.items.size()), 128, "128 item rows cooked");
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
    check(realm.hero().maxSd > 0 && realm.hero().sd == realm.hero().maxSd,
          "he is raised with his shield full");
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

    // A shield's block column goes on his defence rate whole, and its defence in halved. The
    // Small Shield is 1 and 3 (CreateShield(0, ... 3, 1, 3, ...)), and a knight's 28 strength
    // wears it at +0 but not at +2, which asks 38.
    checkEqual(tables.items[size_t(shield)].defenseRate, 3, "a Small Shield's rate is cooked");
    const float rateBare = realm.hero().stats.defenseRate;
    const int shieldAt = realm.give(shield);
    check(realm.moveItem(shieldAt, sim::kWeaponLeft), "a Small Shield goes on");
    checkNear(realm.hero().stats.defenseRate, rateBare + 3, 1e-4, "and adds 3 to his defence rate");
    check(realm.moveItem(sim::kWeaponLeft, shieldAt), "and comes off");
    checkNear(realm.hero().stats.defenseRate, rateBare, 1e-4, "and takes it back");

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

    // Stacks, twenty a cell: a count pours onto its kind before it takes a cell.
    const int first = realm.give(small, -1, 0, 3);
    checkEqual(realm.give(small, -1, 0, 25), first, "twenty-five more top the three up first");
    checkEqual(realm.satchel()[first].durability, sim::kStackMost, "to twenty");
    int rest = -1;
    for (int s = sim::kWorn; s < sim::kSlots; ++s) {
        if (s != first && realm.satchel()[s].item == small) rest = s;
    }
    check(rest >= 0 && realm.satchel()[rest].durability == 8, "and the eight left take a cell");
    check(realm.moveItem(rest, first) && realm.satchel()[first].durability == 8 &&
              realm.satchel()[rest].durability == 20,
          "a stack on a full stack swaps with it");
    const int plusOne = realm.give(small, -1, 1, 2);
    check(plusOne != first && plusOne != rest, "a +1 is not poured into a +0");
    check(realm.moveItem(rest, first) && realm.satchel()[first].durability == 20 &&
              realm.satchel()[rest].durability == 8,
          "one let go on another tops it up and leaves the rest behind");
    check(realm.moveItem(rest, plusOne) && realm.satchel()[plusOne].refinement == 0,
          "and a stack on another plus swaps");

    // The drag out of the window: the axe leaves his hand and lies on the ground, where the
    // same Pick order a kill's drop answers takes it back. The hands are re-reckoned both
    // ways, which is what makes a thrown weapon a real loss and a recovered one a real gain.
    const size_t lyingBefore = realm.lying().size();
    const uint32_t thrownId = realm.discard(sim::kWeaponRight);
    check(thrownId != 0, "the axe is thrown out of his hand");
    checkEqual((long long)realm.lying().size(), (long long)lyingBefore + 1, "and lies on the ground");
    check(realm.satchel()[sim::kWeaponRight].empty(), "the hand it came out of is empty");
    checkEqual(realm.hero().weapon, -1, "he is bare-handed again");
    check(realm.hero().stats.minimumDamage < minimumArmed, "and hits for less");
    check(realm.discard(sim::kWeaponRight) == 0, "an empty slot throws nothing");
    check(realm.discard(-1) == 0 && realm.discard(sim::kSlots) == 0,
          "and neither does a slot that is not one");
    const sim::Lying& thrown = realm.lying().back();
    checkEqual((long long)thrown.id, (long long)thrownId, "the id it answered is what lies there");
    checkEqual(thrown.what.item, axe, "what lies there is the axe");
    checkEqual((long long)thrown.column, (long long)realm.hero().column(), "at his own tile");
    checkEqual((long long)thrown.row, (long long)realm.hero().row(), "in both directions");
    // And picked up again: the Pick order, standing on it.
    sim::Request pick;
    pick.kind = sim::Request::Kind::Pick;
    pick.target = thrown.id;
    realm.ask(pick);
    for (int tick = 0; tick < 100 && realm.lying().size() > lyingBefore; ++tick) realm.step();
    checkEqual((long long)realm.lying().size(), (long long)lyingBefore, "and it is picked up again");
}

// Sprint 7's sentence, headless: kill, pick up, equip, sell. A plain hand hunts the field
// south-east of town (sprint 5's hunting ground), picks up whatever falls before it fights
// again, then puts on anything it can and sells the rest at Lumen's counter.
void testLoot(const content::Tables& tables) {
    std::printf("loot\n");
    sim::Realm realm;
    check(realm.raise(&tables, 1, 200, 160, sim::Kin::DarkKnight, 8), "a hunt raises");
    check(realm.equip(tables.armNamed("Axe01"), -1, true), "with the axe in hand");
    // Eight is under the axe skill's own level, so this hunt is also where a skill is met ON A
    // LEVEL rather than on a raise: he learns Falling Slash at 13, somewhere in the middle of it.
    check(!realm.knows(sim::skill::kFallingSlash), "and no skill yet, at level 8");
    int dropped = 0, picked = 0, kills = 0, learned = 0;
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
                    if (!body.monster() || !body.alive()) continue;
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
            learned += h.what == sim::What::Learned;
        }
    }
    std::printf("  %d kills, %d drops, %d items picked up, %lld Zen\n", kills, dropped, picked,
                (long long)realm.money());
    check(kills > 0 && dropped > 0, "the hunt kills and things fall");
    check(picked > 0, "an item was picked up into the bag");
    check(realm.money() > 0, "and Zen into the purse");
    // He wins a level or two off eighty-eight spiders and nothing opens between 8 and 13, so
    // what this hunt shows is the quiet half of the rule: a level that opens nothing says
    // nothing. The ladder's own steps are checked in testSkills, on all three doors into it.
    check(learned == 0 && !realm.knows(sim::skill::kFallingSlash),
          "no skill was opened by a level that opens none");

    // His points, as a player spends them at the character window: into strength, which is
    // what most of what a knight finds asks for. Then equip whatever fits, straight from the
    // bag through the same move a drag makes.
    // Standing up first. A hunt of twelve thousand ticks can end on the tick he is lying
    // down, and a dead man moves nothing -- `Realm::moveItem` refuses every drag while
    // `bodies_[0]` is not alive, so the whole equip loop below would answer no for a reason
    // that has nothing to do with what dropped. It is waited out rather than left to the seed.
    for (int tick = 0; tick < 400 && !realm.hero().alive(); ++tick) realm.step();
    check(realm.hero().alive(), "he is on his feet again before he sorts his bag");
    realm.spend(realm.hero().pointsInHand, 0, 0, 0);
    int worn = 0, allowed = 0;
    for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
        const sim::Held& one = realm.satchel()[slot];
        if (one.empty()) continue;
        const content::ItemRow& row = tables.items[size_t(one.item)];
        const int place = sim::placeOf(row);
        // What the window would let him drag, asked before the drag: a gate that says no and a
        // move that then says yes (or the other way about) is the bug this pairing catches.
        const bool may =
            place >= 0 && sim::movable(tables, realm.wearer(), realm.satchel(), slot, place);
        allowed += may ? 1 : 0;
        const bool on = place >= 0 && realm.moveItem(slot, place);
        std::printf("    found %s +%d: %s\n", row.label.c_str(), one.refinement,
                    on ? "put on" : (place < 0 ? "not worn" : "refused"));
        if (on) ++worn;
    }
    std::printf("  %d pieces put on, %d the gate allowed\n", worn, allowed);

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
    // Everything the gate allowed went on, and nothing else did. **Not `worn > 0`**, which is
    // what this asked until 2026-09-23: what a hunt drops is the dice's business, and a run in
    // which the only wearable thing was a bow (a knight may not hold one) or a second axe (the
    // hand it goes in is full) failed a test about the EQUIP PATH for a reason that had nothing
    // to do with it. Any change to what monsters do reshuffles the drops, so the old form made
    // every AI change look like an item bug. Putting something on from the bag is covered
    // without dice in testItems, piece by piece.
    checkEqual((long long)worn, (long long)allowed,
               "everything the gate allowed was put on, and nothing it refused");
    check(sold > 0, "and the rest sold");
    check(realm.sellItem(sim::kWeaponRight) < 0, "and what is worn is never sold");
}

// A hand spamming clicks: a new walk somewhere near him on most ticks, for a minute, in the
// town where nothing fights. The walk must never go faster than his pace, never travel far
// off where he faces (the moonwalk), never stand on a tile the grid refuses, never hang
// "walking" without moving for longer than a turn takes after the LAST click (a hand that keeps
// reversing is answered by turning to each new order, which is right), and when it stops, end
// exactly on the last tile asked for.
// A beast that kills its quarry stands over the body instead of turning away on the tick.
//
// This exists because the seeded hunt CANNOT cover it: that run fights nobody -- "0 blows
// landed, 0 missed, 0 deaths" -- so its fingerprint is blind to every rule that only fires
// when something dies. The behaviour was added on 2026-09-22 for a reason that lives in the
// drawing (a fall waits for its blow to be seen landing, so a killer that turns away on the
// tick walks off while its victim is still standing), and a rule put there for the screen's
// sake is exactly the kind that rots quietly.
// A skill is not walked out of, and only the swing is (2026-09-23, the user's rule). While a
// cast's clip runs the knight is locked where he stands, so the three orders that would take a
// step -- the ground click, a thing on the floor, a townsperson -- are DROPPED rather than held:
// the click is spent and he does not set off the moment the clip ends. Attack and Stop go
// through, since neither moves him while `castUntil` runs.
//
// Two halves, because the rule has two: the self-cast half is run with nothing to fight, so
// nothing but the click can move him and "he did not move" means exactly that; the fighting half
// checks what the click used to cost him, which was the skill's own unlanded blow.
void testCastLock(const content::Tables& tables) {
    std::printf("a skill is not walked out of\n");
    sim::Realm realm;
    check(realm.raise(&tables, 7, 190, 110, sim::Kin::DarkKnight, 60), "a realm raises for the lock");
    // A blade and a shield: Defense is a shield's skill, and it is thrown at himself, so this
    // half needs no monster at all.
    check(realm.equip(tables.armNamed("Sword03"), tables.armNamed("Shield01"), true),
          "a blade in one hand and a shield on the other");
    check(realm.learn(sim::skill::kDefense), "and the guard in his head");
    check(!tables.grid.safe(realm.hero().column(), realm.hero().row()),
          "he stands outside the safe zone, where a skill may be thrown");

    // The press, and the tick it is thrown on.
    bool thrown = false;
    realm.invoke(sim::skill::kDefense, realm.hero().id);
    for (int tick = 0; tick < 60 && !thrown; ++tick) {
        realm.step();
        for (const sim::Happening& one : realm.happenings()) {
            thrown |= one.what == sim::What::Cast && one.who == realm.hero().id;
        }
    }
    check(thrown, "the guard is thrown");
    check(realm.casting(), "and its clip is still running the tick after");

    // The click, mid-clip, at a tile ten away.
    const int stoodColumn = realm.hero().column(), stoodRow = realm.hero().row();
    sim::Request walk;
    walk.kind = sim::Request::Kind::WalkTo;
    walk.column = stoodColumn + 10;
    walk.row = stoodRow;
    realm.ask(walk);
    bool stillStanding = true;
    int clipTicks = 0;
    for (int tick = 0; tick < 200 && realm.casting(); ++tick) {
        realm.step();
        ++clipTicks;
        stillStanding &= !realm.hero().walking && realm.hero().column() == stoodColumn &&
                         realm.hero().row() == stoodRow;
    }
    check(clipTicks > 1, "the clip is long enough to be walked out of, if it could be");
    check(stillStanding, "and he does not take a step of it");

    // And the click was SPENT, not held: nothing is standing to send him anywhere afterwards.
    bool stayedPut = true;
    for (int tick = 0; tick < 60; ++tick) {
        realm.step();
        stayedPut &= !realm.hero().walking && realm.hero().column() == stoodColumn &&
                     realm.hero().row() == stoodRow;
    }
    check(stayedPut, "and the click does not set him off once the clip ends");

    // The same click, with no clip running, still walks him -- the gate closes on the cast and
    // on nothing else.
    realm.ask(walk);
    bool walked = false;
    for (int tick = 0; tick < 60 && !walked; ++tick) {
        realm.step();
        walked = realm.hero().walking;
    }
    check(walked, "the same click walks him when no skill is running");

    // ---- and the blow the click used to throw away ------------------------------------------
    //
    // `accept` drops the unlanded blow of whatever order it replaces -- that is how an attack is
    // cancelled -- and until this rule the skill's blow went the same way, so a click during the
    // clip cancelled the skill and kept nothing. Cyclone is thrown at a monster and settles half
    // a clip in, which is after the click below.
    sim::Realm fight;
    check(fight.raise(&tables, 7, 190, 110, sim::Kin::DarkKnight, 60), "a realm raises for the fight");
    check(fight.equip(tables.armNamed("Sword03"), -1, true), "with a one-handed sword, Cyclone's");
    check(fight.learn(sim::skill::kCyclone), "and the spin in his head");
    bool cast = false, landed = false;
    uint32_t fighting = 0;
    for (int tick = 0; tick < 6000 && !landed; ++tick) {
        const sim::Body& hero = fight.hero();
        if (hero.alive() && !cast) {
            uint32_t nearest = 0;
            float closest = 1e30f;
            for (const sim::Body& one : fight.bodies()) {
                if (!one.monster() || !one.alive()) continue;
                const float off = std::max(std::fabs(one.x - hero.x), std::fabs(one.y - hero.y));
                if (off <= 12.0f && off < closest) {
                    closest = off;
                    nearest = one.id;
                }
            }
            if (nearest != 0 && nearest != fighting) {
                fighting = nearest;
                sim::Request request;
                request.kind = sim::Request::Kind::Attack;
                request.target = nearest;
                fight.ask(request);
            }
            if (nearest != 0 && fight.cooling(sim::skill::kCyclone) == 0) {
                fight.invoke(sim::skill::kCyclone, nearest);
            }
        }
        fight.step();
        for (const sim::Happening& one : fight.happenings()) {
            if (one.what == sim::What::Cast && one.who == fight.hero().id && !cast) {
                cast = true;
                // The click, on the tick the spin is thrown and before its blow settles.
                sim::Request away;
                away.kind = sim::Request::Kind::WalkTo;
                away.column = fight.hero().column() + 8;
                away.row = fight.hero().row();
                fight.ask(away);
            } else if (cast && one.what == sim::What::Hit && one.who == fight.hero().id) {
                landed = true;
            }
        }
    }
    check(cast, "the spin is thrown at something");
    check(landed, "and its blow lands, though a click to move came in over the top of it");

    // ---- the quick slot: a wizard's right button (the user, 2026-09-28) ---------------------
    //
    // An Attack carrying Energy Ball: he stops at six tiles and throws, the bolt is let go at the
    // bottom of the clip and lands when it has crossed the gap, and a hit pays mana back. Then
    // the same order with his mana gone: he closes to arm's length and swings the staff.
    const auto nearestTo = [](const sim::Realm& realm) {
        const sim::Body& hero = realm.hero();
        uint32_t nearest = 0;
        float closest = 1e30f;
        for (const sim::Body& one : realm.bodies()) {
            if (!one.monster() || !one.alive()) continue;
            const float off = std::max(std::fabs(one.x - hero.x), std::fabs(one.y - hero.y));
            if (off <= 12.0f && off < closest) {
                closest = off;
                nearest = one.id;
            }
        }
        return nearest;
    };
    {
        sim::Realm wiz;
        check(wiz.raise(&tables, 7, 190, 110, sim::Kin::DarkWizard, 30), "a wizard raises to hunt");
        // `far` counts the OPENING bolt of each fight: once a monster has closed on him he goes on
        // casting point-blank, as MU's wizard does, so the rest say nothing about the range.
        int loosed = 0, far = 0, opened = 0, thrownHits = 0, swings = 0;
        float worstFacing = 0.0f;
        uint32_t fighting = 0, openedOn = 0;
        // Whether the fight he was just given was already inside a swing when he took it on: a
        // monster that rises a tile from where he stands cannot be opened on from range, and
        // counting it measures where the nests are rather than what the wizard chooses.
        bool beganClose = false;
        for (int tick = 0; tick < 4000; ++tick) {
            const uint32_t nearest = wiz.hero().alive() ? nearestTo(wiz) : 0;
            if (nearest != 0 && nearest != fighting) {
                fighting = nearest;
                if (const sim::Body* at = wiz.find(nearest)) {
                    beganClose = std::max(std::fabs(at->x - wiz.hero().x),
                                          std::fabs(at->y - wiz.hero().y)) <= 1.5f;
                }
                sim::Request request;
                request.kind = sim::Request::Kind::Attack;
                request.target = nearest;
                request.skill = sim::skill::kEnergyBall;
                wiz.ask(request);
            }
            wiz.step();
            for (const sim::Happening& one : wiz.happenings()) {
                if (one.who != wiz.hero().id) continue;
                if (one.what == sim::What::Loosed) {
                    ++loosed;
                    const sim::Body* at = wiz.find(one.whom);
                    // He faces what he throws at: the angle between where he looks and the
                    // target, at the let-go, half a clip after the cast aimed him.
                    if (at) {
                        const sim::Body& me = wiz.hero();
                        float off = std::atan2(at->y - me.y, at->x - me.x) - me.facing;
                        while (off > 3.14159265f) off -= 6.28318531f;
                        while (off < -3.14159265f) off += 6.28318531f;
                        worstFacing = std::max(worstFacing, std::fabs(off));
                    }
                    if (at && one.whom != openedOn) {
                        openedOn = one.whom;
                        if (one.whom == fighting && beganClose) continue;
                        ++opened;
                        if (std::max(std::fabs(at->x - one.x), std::fabs(at->y - one.y)) > 1.5f) ++far;
                    }
                }
                if (one.what == sim::What::Hit && one.thrown) ++thrownHits;
                if (one.what == sim::What::Swung && one.a == 0) ++swings;
            }
        }
        std::printf("  energy ball: %d loosed over %d fights, %d opened from range, %d hits\n",
                    loosed, opened, far, thrownHits);
        std::printf("  energy ball: worst facing at a let-go %.1f degrees\n",
                    double(worstFacing) * 57.2958);
        check(loosed > 20, "he throws Energy Ball over and over");
        check(worstFacing < 0.35f, "and faces what he throws at, within twenty degrees");
        check(far * 4 >= opened * 3, "and opens most fights from further off than a swing reaches");
        check(thrownHits > 0, "and the bolts land as thrown hits, after their flight");
        checkEqual(swings, 0, "and with mana to spend he never swings the staff");

        // Emptied, through the save's door: the same order now walks in and swings.
        sim::HeroRecord dry = wiz.record();
        dry.mana = 0;
        sim::Realm empty;
        check(empty.raise(&tables, 7, 190, 110, sim::Kin::DarkWizard, 30), "a dry wizard raises");
        empty.restore(dry);
        int staff = 0, spells = 0;
        fighting = 0;
        for (int tick = 0; tick < 400 && staff == 0; ++tick) {
            const uint32_t nearest = empty.hero().alive() ? nearestTo(empty) : 0;
            if (nearest != 0 && nearest != fighting) {
                fighting = nearest;
                sim::Request request;
                request.kind = sim::Request::Kind::Attack;
                request.target = nearest;
                request.skill = sim::skill::kEnergyBall;
                empty.ask(request);
            }
            empty.step();
            for (const sim::Happening& one : empty.happenings()) {
                if (one.who != empty.hero().id) continue;
                if (one.what == sim::What::Swung && one.a == 0) ++staff;
                if (one.what == sim::What::Loosed) ++spells;
            }
        }
        check(staff > 0, "out of mana, the right button's order swings the weapon");
        checkEqual(spells, 0, "and throws nothing it cannot pay for");
    }

    // ---- Fire Ball (docs/skills-dw.md §2b) ----------------------------------------------------
    //
    // 0.75's row, and a primary as Energy Ball is: thrown over and over with no wait, flying
    // slower than the bolt, and learned off its scroll only at forty energy.
    {
        const sim::SkillRow& fire = *sim::skillNumbered(sim::skill::kFireBall);
        check(fire.wizardry && fire.kin == sim::Kin::DarkWizard && fire.thrown() && fire.primary(),
              "Fire Ball is the wizard's thrown spell, a primary with no cooldown");
        check(fire.damage == 8 && fire.mana == 3 && fire.reach == 9.0f,
              "at 0.75's eight damage and three mana, thrown from nine tiles");
        check(fire.flies < sim::skillNumbered(sim::skill::kEnergyBall)->flies,
              "and it flies slower than Energy Ball");

        const int32_t scroll = tables.itemAt(15, 3);
        check(scroll >= 0 && tables.items[size_t(scroll)].teaches == sim::skill::kFireBall &&
                  tables.items[size_t(scroll)].teachesEnergy == 40,
              "the Scroll of Fire Ball is cooked, teaching skill 4 at forty energy");

        // A wizard of thirty energy with the scroll in his bag: refused, then read at forty.
        sim::Realm reader;
        check(reader.raise(&tables, 7, 190, 110, sim::Kin::DarkWizard, 10), "a wizard of ten raises");
        sim::HeroRecord carrying = reader.record();
        const int bagged = sim::kWorn + 40;
        carrying.slots[bagged].item = scroll;
        carrying.slots[bagged].durability = 1;
        reader.restore(carrying);
        check(!reader.useItem(bagged) && !reader.knows(sim::skill::kFireBall),
              "at thirty energy the scroll will not be read");
        carrying = reader.record();
        carrying.points.energy = 40;
        reader.restore(carrying);
        check(reader.useItem(bagged) && reader.knows(sim::skill::kFireBall),
              "at forty it is read, and Fire Ball is his");

        // A hunt on Fire Ball alone, as the right button would carry it: thrown over and over,
        // landing as thrown hits, and never swinging the staff while there is mana.
        sim::Realm wiz;
        check(wiz.raise(&tables, 7, 190, 110, sim::Kin::DarkWizard, 30), "a wizard raises to hunt");
        check(wiz.learn(sim::skill::kFireBall), "who knows Fire Ball");
        int balls = 0, landed = 0, swings = 0, paidBack = 0;
        uint32_t fighting = 0;
        for (int tick = 0; tick < 3000; ++tick) {
            const int manaBefore = wiz.hero().mana;
            const uint32_t nearest = wiz.hero().alive() ? nearestTo(wiz) : 0;
            if (nearest != 0 && nearest != fighting) {
                fighting = nearest;
                sim::Request request;
                request.kind = sim::Request::Kind::Attack;
                request.target = nearest;
                request.skill = sim::skill::kFireBall;
                wiz.ask(request);
            }
            wiz.step();
            for (const sim::Happening& one : wiz.happenings()) {
                if (one.who != wiz.hero().id) continue;
                if (one.what == sim::What::Loosed && one.a == sim::skill::kFireBall) ++balls;
                if (one.what == sim::What::Hit && one.thrown) {
                    ++landed;
                    // Off the three-second regeneration tick, a landed spell must not raise him.
                    if (wiz.tick() % 60 != 0 && wiz.hero().mana > manaBefore) ++paidBack;
                }
                if (one.what == sim::What::Swung && one.a == 0) ++swings;
            }
        }
        checkEqual(paidBack, 0, "a landed spell pays no mana back");
        std::printf("  fire ball: %d thrown, %d landed, %d staff swings\n", balls, landed, swings);
        check(balls > 20, "he throws Fire Ball over and over");
        check(landed > 0, "and it lands after its flight");
        checkEqual(wiz.cooling(sim::skill::kFireBall), 0LL, "and nothing is left cooling");
        std::printf("  fire ball: %d staff swings once the pool ran dry\n", swings);
    }

    // ---- Power Wave: 0.75's row, a primary, off its scroll at fifty-six energy ----------------
    {
        const sim::SkillRow& wave = *sim::skillNumbered(sim::skill::kPowerWave);
        check(wave.wizardry && wave.primary() && wave.thrown() && wave.damage == 14 &&
                  wave.mana == 5 && wave.flies == 15.0f && wave.spread == sim::Spread::Line,
              "Power Wave is a thrown primary at fourteen damage and five mana, down a line");
        const int32_t scroll = tables.itemAt(15, 10);
        check(scroll >= 0 && tables.items[size_t(scroll)].teaches == sim::skill::kPowerWave &&
                  tables.items[size_t(scroll)].teachesEnergy == 56,
              "the Scroll of Power Wave teaches skill 11 at fifty-six energy");
        sim::Realm reader;
        check(reader.raise(&tables, 7, 190, 110, sim::Kin::DarkWizard, 10), "a wizard raises");
        sim::HeroRecord carrying = reader.record();
        const int bagged = sim::kWorn + 40;
        carrying.slots[bagged].item = scroll;
        carrying.slots[bagged].durability = 1;
        carrying.points.energy = 55;
        reader.restore(carrying);
        check(!reader.useItem(bagged), "at fifty-five energy it is refused");
        carrying = reader.record();
        carrying.points.energy = 56;
        reader.restore(carrying);
        check(reader.useItem(bagged) && reader.knows(sim::skill::kPowerWave), "and read at fifty-six");

        sim::Realm wiz;
        check(wiz.raise(&tables, 7, 190, 110, sim::Kin::DarkWizard, 30), "a wizard raises to hunt");
        check(wiz.learn(sim::skill::kPowerWave), "who knows Power Wave");
        int waves = 0, landed = 0, widest = 0, thisWave = 0;
        float farthestAside = 0.0f;
        uint32_t fighting = 0;
        for (int tick = 0; tick < 6000; ++tick) {
            const uint32_t nearest = wiz.hero().alive() ? nearestTo(wiz) : 0;
            if (nearest != 0 && nearest != fighting) {
                fighting = nearest;
                sim::Request request;
                request.kind = sim::Request::Kind::Attack;
                request.target = nearest;
                request.skill = sim::skill::kPowerWave;
                wiz.ask(request);
            }
            wiz.step();
            for (const sim::Happening& one : wiz.happenings()) {
                if (one.who != wiz.hero().id) continue;
                if (one.what == sim::What::Loosed && one.a == sim::skill::kPowerWave) {
                    ++waves;
                    thisWave = 0;
                }
                if ((one.what == sim::What::Hit || one.what == sim::What::Missed) && one.thrown) {
                    if (one.what == sim::What::Hit) ++landed;
                    widest = std::max(widest, ++thisWave);
                    // Where it was, against the line he threw along: never beside it.
                    if (const sim::Body* victim = wiz.find(one.whom)) {
                        const sim::Body& me = wiz.hero();
                        const float dx = victim->x - me.x, dy = victim->y - me.y;
                        farthestAside = std::max(
                            farthestAside,
                            std::fabs(-dx * std::sin(me.aim) + dy * std::cos(me.aim)));
                    }
                }
            }
        }
        std::printf("  power wave: %d thrown, %d landed, %d struck by one wave at the most, "
                    "%.2f tiles off the line at the most\n",
                    waves, landed, widest, double(farthestAside));
        check(waves > 20 && landed > 0, "he throws Power Wave through a hunt and it lands");
        check(widest >= 2, "and one wave strikes more than one body in its line");
    }

    // ---- a spider's poison on him, and the Antidote that clears it ----------------------------
    {
        const int32_t antidote = tables.itemAt(14, 8);
        check(antidote >= 0, "the Antidote is in the tables");
        // Stood at the first Spider's home and left to be bitten.
        sim::Realm probe;
        check(probe.raise(&tables, 7, 190, 110, sim::Kin::DarkKnight, 1), "a realm raises");
        int column = -1, row = -1;
        for (const sim::Body& one : probe.bodies()) {
            if (one.monster() && one.kind >= 0 && tables.kinds[size_t(one.kind)].number == 3) {
                column = one.homeColumn;
                row = one.homeRow;
                break;
            }
        }
        check(column >= 0, "Lorencia has spiders");
        sim::Realm bit;
        check(bit.raise(&tables, 7, column, row, sim::Kin::DarkKnight, 1), "a knight among them");
        int64_t poisonedAt = -1;
        int pulses = 0, pulseWrong = 0, pulseKilled = 0;
        for (int tick = 0; tick < 3000 && bit.hero().alive(); ++tick) {
            const int before = bit.hero().health;
            bit.step();
            if (poisonedAt < 0 && bit.hero().poisonUntil > bit.tick()) poisonedAt = bit.tick();
            for (const sim::Happening& one : bit.happenings()) {
                if (one.what != sim::What::Hit || !one.poisoned || one.whom != bit.hero().id) continue;
                ++pulses;
                // 0.75's share of what he had, at least one, never the last point.
                const int due = std::max(1, int(float(before) * 0.03f));
                if (one.a > due) ++pulseWrong;
                if (one.c <= 0) ++pulseKilled;
            }
            if (pulses >= 2) break;
        }
        std::printf("  spider poison: bitten at tick %lld, %d pulses\n", (long long)poisonedAt, pulses);
        check(poisonedAt >= 0, "a spider's bite poisons him");
        check(pulses >= 1 && pulseWrong == 0, "and it pulses at a share of what he has left");
        checkEqual(pulseKilled, 0, "never killing him");

        sim::HeroRecord carrying = bit.record();
        const int bagged = sim::kWorn + 40;
        carrying.slots[bagged].item = antidote;
        carrying.slots[bagged].durability = 1;
        carrying.slots[bagged + 1].item = antidote;
        carrying.slots[bagged + 1].durability = 1;
        // Restoring a record keeps what is standing on him? Re-bite if not.
        bit.restore(carrying);
        for (int tick = 0; tick < 3000 && bit.hero().poisonUntil <= bit.tick(); ++tick) bit.step();
        check(bit.hero().poisonUntil > bit.tick(), "poisoned again for the Antidote");
        check(bit.useItem(bagged) && bit.hero().poisonUntil == 0, "an Antidote clears it");
        bool pulsedAfter = false;
        for (int tick = 0; tick < 80; ++tick) {
            // Kept out of the spiders' reach would be cleaner; a fresh bite is allowed.
            bit.step();
            for (const sim::Happening& one : bit.happenings()) {
                if (one.what == sim::What::Hit && one.poisoned && one.whom == bit.hero().id &&
                    bit.hero().poisonUntil == 0) {
                    pulsedAfter = true;
                }
            }
            if (bit.hero().poisonUntil != 0) break;
        }
        check(!pulsedAfter, "and no pulse follows it");
        if (bit.hero().poisonUntil == 0) {
            check(!bit.useItem(bagged + 1), "with no poison on him the Antidote is kept");
        }
    }

    // ---- Poison: a cooldown burst that goes on hurting ---------------------------------------
    {
        const sim::SkillRow& poison = *sim::skillNumbered(sim::skill::kPoison);
        check(poison.wizardry && !poison.primary() && poison.damage == 12 && poison.mana == 42 &&
                  poison.poisonTicks == 400 && poison.splash == 4.0f,
              "Poison is a cooldown spell of twelve damage and forty-two mana, poisoning twenty "
              "seconds within four tiles");
        const int32_t scroll = tables.itemAt(15, 0);
        check(scroll >= 0 && tables.items[size_t(scroll)].teaches == sim::skill::kPoison &&
                  tables.items[size_t(scroll)].teachesEnergy == 140,
              "the Scroll of Poison teaches skill 1 at a hundred and forty energy");

        sim::Realm wiz;
        check(wiz.raise(&tables, 7, 190, 110, sim::Kin::DarkWizard, 30), "a wizard raises to hunt");
        check(wiz.learn(sim::skill::kPoison), "who knows Poison");
        int casts = 0, widest = 0, thisCast = 0, pulses = 0, pulseKills = 0, offBeat = 0;
        int64_t lastCast = -1, closest = 1 << 30, castTick = -1;
        std::vector<std::pair<uint32_t, int64_t>> lastPulse;
        uint32_t fighting = 0;
        for (int tick = 0; tick < 6000; ++tick) {
            const uint32_t nearest = wiz.hero().alive() ? nearestTo(wiz) : 0;
            if (nearest != 0 && nearest != fighting) {
                fighting = nearest;
                sim::Request request;
                request.kind = sim::Request::Kind::Attack;
                request.target = nearest;
                wiz.ask(request);
            }
            if (nearest != 0 && wiz.cooling(sim::skill::kPoison) == 0) {
                wiz.invoke(sim::skill::kPoison, nearest);
            }
            wiz.step();
            for (const sim::Happening& one : wiz.happenings()) {
                if (one.what == sim::What::Cast && one.who == wiz.hero().id &&
                    one.a == sim::skill::kPoison) {
                    ++casts;
                    if (lastCast >= 0) closest = std::min<int64_t>(closest, one.tick - lastCast);
                    lastCast = one.tick;
                }
                if (one.what == sim::What::Loosed && one.a == sim::skill::kPoison) {
                    if (int64_t(one.tick) != castTick) {
                        castTick = one.tick;
                        thisCast = 0;
                    }
                    widest = std::max(widest, ++thisCast);
                }
                if (one.what == sim::What::Hit && one.poisoned) {
                    ++pulses;
                    // A pulse leaves one health at least: the kill is a blow's.
                    if (one.c <= 0) ++pulseKills;
                    // And three seconds after the last pulse on the same body, or the blow.
                    for (auto& seen : lastPulse) {
                        if (seen.first == one.whom && int64_t(one.tick) - seen.second != sim::kPoisonEvery) {
                            ++offBeat;
                        }
                    }
                    bool known = false;
                    for (auto& seen : lastPulse) {
                        if (seen.first == one.whom) {
                            seen.second = one.tick;
                            known = true;
                        }
                    }
                    if (!known) lastPulse.push_back({one.whom, int64_t(one.tick)});
                }
            }
        }
        std::printf("  poison: %d cast, %d poisoned by one cast at the most, %d pulses, "
                    "%lld ticks apart at the closest\n",
                    casts, widest, pulses, (long long)closest);
        check(casts > 10 && pulses > 0, "he throws Poison through a hunt and it pulses");
        check(widest >= 2, "and one cast poisons more than one body");
        check(closest >= wiz.coolsFor(sim::skill::kPoison) && closest >= 100,
              "never inside its cooldown");
        checkEqual(pulseKills, 0, "a pulse never kills");
        std::printf("  poison: %d pulses off the three-second beat (a recast restarts it)\n", offBeat);
    }

    // ---- Ice: a cooldown spell that bursts round its target and halves the walk -------------
    {
        const sim::SkillRow& ice = *sim::skillNumbered(sim::skill::kIce);
        check(ice.wizardry && !ice.primary() && ice.damage == 10 && ice.mana == 38 &&
                  ice.chillTicks == 200 && ice.splash == 4.0f,
              "Ice is a cooldown spell of ten damage and thirty-eight mana, chilling ten seconds "
              "within four tiles");
        const int32_t scroll = tables.itemAt(15, 6);
        check(scroll >= 0 && tables.items[size_t(scroll)].teaches == sim::skill::kIce &&
                  tables.items[size_t(scroll)].teachesEnergy == 120,
              "the Scroll of Ice teaches skill 7 at a hundred and twenty energy");

        sim::Realm wiz;
        check(wiz.raise(&tables, 7, 190, 110, sim::Kin::DarkWizard, 30), "a wizard raises to hunt");
        check(wiz.learn(sim::skill::kIce), "who knows Ice");
        int casts = 0, struck = 0, widest = 0, thisCast = 0, fastWhileIced = 0, icedSteps = 0;
        int64_t lastCast = -1, closest = 1 << 30, castTick = -1;
        uint32_t fighting = 0;
        std::vector<std::pair<float, float>> was;
        for (int tick = 0; tick < 6000; ++tick) {
            const uint32_t nearest = wiz.hero().alive() ? nearestTo(wiz) : 0;
            if (nearest != 0 && nearest != fighting) {
                fighting = nearest;
                sim::Request request;
                request.kind = sim::Request::Kind::Attack;
                request.target = nearest;
                wiz.ask(request);
            }
            if (nearest != 0 && wiz.cooling(sim::skill::kIce) == 0) {
                wiz.invoke(sim::skill::kIce, nearest);
            }
            was.clear();
            for (const sim::Body& one : wiz.bodies()) was.push_back({one.x, one.y});
            wiz.step();
            // An iced body never covers more than half its own ground in a tick.
            for (size_t i = 1; i < wiz.bodies().size() && i < was.size(); ++i) {
                const sim::Body& one = wiz.bodies()[i];
                if (!one.monster() || !one.alive() || one.chilledUntil <= wiz.tick()) continue;
                if (one.pushTicks > 0) continue;
                const float moved = std::hypot(one.x - was[i].first, one.y - was[i].second);
                if (moved > 0.0f) ++icedSteps;
                if (moved > one.speed * sim::kChillFactor + 1e-3f && moved < 2.0f) ++fastWhileIced;
            }
            for (const sim::Happening& one : wiz.happenings()) {
                if (one.who != wiz.hero().id) continue;
                if (one.what == sim::What::Cast && one.a == sim::skill::kIce) {
                    ++casts;
                    if (lastCast >= 0) closest = std::min<int64_t>(closest, one.tick - lastCast);
                    lastCast = one.tick;
                }
                if (one.what == sim::What::Loosed && one.a == sim::skill::kIce) {
                    if (int64_t(one.tick) != castTick) {
                        castTick = one.tick;
                        thisCast = 0;
                    }
                    widest = std::max(widest, ++thisCast);
                }
                if (one.what == sim::What::Hit && one.thrown) ++struck;
            }
        }
        std::printf("  ice: %d cast, %d struck, %d iced by one cast at the most, %lld ticks apart "
                    "at the closest, %d iced steps, %d too fast\n",
                    casts, struck, widest, (long long)closest, icedSteps, fastWhileIced);
        check(casts > 10 && struck > 0, "he throws Ice through a hunt and it strikes");
        check(widest >= 2, "and one cast ices more than one body");
        check(closest >= wiz.coolsFor(sim::skill::kIce) && closest >= 80, "never inside its cooldown");
        check(icedSteps > 0, "iced bodies still walk");
        checkEqual(fastWhileIced, 0, "at half their pace");
    }

    // ---- Teleport: a blink to the ground he points at ----------------------------------------
    {
        const sim::SkillRow& blink = *sim::skillNumbered(sim::skill::kTeleport);
        check(blink.blinks && !blink.primary() && blink.mana == 30 && blink.clip == 147 &&
                  blink.reach == 6.0f,
              "Teleport is a blink of six tiles for thirty mana, cast with one hand");
        const int32_t scroll = tables.itemAt(15, 5);
        check(scroll >= 0 && tables.items[size_t(scroll)].teaches == sim::skill::kTeleport &&
                  tables.items[size_t(scroll)].teachesEnergy == 88,
              "the Scroll of Teleport teaches skill 6 at eighty-eight energy");

        sim::Realm wiz;
        check(wiz.raise(&tables, 7, 190, 110, sim::Kin::DarkWizard, 30), "a wizard raises");
        check(wiz.learn(sim::skill::kTeleport), "who knows Teleport");
        // Run until what was asked has happened or a second has gone; say what was seen.
        const auto run = [&](int ticks, int64_t* castAt, int64_t* blinkAt) {
            *castAt = *blinkAt = -1;
            for (int t = 0; t < ticks; ++t) {
                wiz.step();
                for (const sim::Happening& one : wiz.happenings()) {
                    if (one.who != wiz.hero().id) continue;
                    if (one.what == sim::What::Cast && one.a == sim::skill::kTeleport) {
                        *castAt = one.tick;
                    }
                    if (one.what == sim::What::Blinked) *blinkAt = one.tick;
                }
            }
        };
        const float fromX = wiz.hero().x, fromY = wiz.hero().y;
        const int mana = wiz.hero().mana;
        wiz.invokeAt(sim::skill::kTeleport, int(fromX) + 3, int(fromY));
        int64_t castAt = 0, blinkAt = 0;
        run(20, &castAt, &blinkAt);
        const float went = std::hypot(wiz.hero().x - fromX, wiz.hero().y - fromY);
        std::printf("  teleport: cast at %lld, put down at %lld, %.2f tiles, %d mana spent\n",
                    (long long)castAt, (long long)blinkAt, double(went), mana - wiz.hero().mana);
        check(castAt >= 0 && blinkAt - castAt == 8, "he is put down eight ticks after the cast");
        check(went >= 1.0f && went <= 3.6f, "on the tile he pointed at, or beside it");
        check(mana - wiz.hero().mana >= 30, "for thirty mana");
        check(wiz.cooling(sim::skill::kTeleport) > 0, "and it cools");

        const float midX = wiz.hero().x, midY = wiz.hero().y;
        wiz.invokeAt(sim::skill::kTeleport, int(midX) - 3, int(midY));
        run(10, &castAt, &blinkAt);
        check(castAt < 0 && wiz.hero().x == midX, "a press while it cools does nothing");

        run(80, &castAt, &blinkAt);
        wiz.invokeAt(sim::skill::kTeleport, int(midX) + 20, int(midY));
        run(20, &castAt, &blinkAt);
        const float far = std::hypot(wiz.hero().x - midX, wiz.hero().y - midY);
        check(castAt >= 0 && far >= 1.0f && far <= 6.5f,
              "twenty tiles off, he goes six at the most");

        run(80, &castAt, &blinkAt);
        wiz.invoke(sim::skill::kTeleport, 0);
        run(20, &castAt, &blinkAt);
        check(castAt < 0, "and with no ground named it is not thrown");
    }

    // ---- Meteorite: a cooldown spell, off its scroll at a hundred and four energy ----------
    {
        const sim::SkillRow& rock = *sim::skillNumbered(sim::skill::kMeteorite);
        check(rock.wizardry && !rock.primary() && rock.thrown() && rock.damage == 21 &&
                  rock.fallTicks == 7 && rock.clip == 183 && rock.force == 3.0f,
              "Meteorite is a thrown cooldown spell at twenty-one damage, three times the band, "
              "falling seven ticks, cast in the arm-up clip");
        const int32_t scroll = tables.itemAt(15, 1);
        check(scroll >= 0 && tables.items[size_t(scroll)].teaches == sim::skill::kMeteorite &&
                  tables.items[size_t(scroll)].teachesEnergy == 104,
              "the Scroll of Meteorite teaches skill 2 at a hundred and four energy");
        check(tables.action(183) != nullptr, "the tables carry the arm-up clip's length");
        sim::Realm reader;
        check(reader.raise(&tables, 7, 190, 110, sim::Kin::DarkWizard, 10), "a wizard raises");
        sim::HeroRecord carrying = reader.record();
        const int bagged = sim::kWorn + 40;
        carrying.slots[bagged].item = scroll;
        carrying.slots[bagged].durability = 1;
        carrying.points.energy = 103;
        reader.restore(carrying);
        check(!reader.useItem(bagged), "at a hundred and three energy it is refused");
        carrying = reader.record();
        carrying.points.energy = 104;
        reader.restore(carrying);
        check(reader.useItem(bagged) && reader.knows(sim::skill::kMeteorite),
              "and read at a hundred and four");

        sim::Realm wiz;
        check(wiz.raise(&tables, 7, 190, 110, sim::Kin::DarkWizard, 30), "a wizard raises to hunt");
        check(wiz.learn(sim::skill::kMeteorite), "who knows Meteorite");
        const int32_t lock = sim::castTicks(tables, sim::Kin::DarkWizard,
                                             wiz.hero().points.agility, nullptr, nullptr, rock);
        int casts = 0, falls = 0, landed = 0, lateOrEarly = 0, movedWhile = 0, widest = 0, thisFall = 0;
        int64_t fallTick = -1;
        int64_t lastCast = -1, closest = 1 << 30;
        int64_t loosedAt[8] = {};
        uint32_t loosedOn[8] = {};
        uint32_t fighting = 0;
        for (int tick = 0; tick < 6000; ++tick) {
            const uint32_t nearest = wiz.hero().alive() ? nearestTo(wiz) : 0;
            if (nearest != 0 && nearest != fighting) {
                fighting = nearest;
                sim::Request request;
                request.kind = sim::Request::Kind::Attack;
                request.target = nearest;
                wiz.ask(request);
            }
            if (nearest != 0 && wiz.cooling(sim::skill::kMeteorite) == 0) {
                wiz.invoke(sim::skill::kMeteorite, nearest);
            }
            const float wasX = wiz.hero().x, wasY = wiz.hero().y;
            const bool held = wiz.casting();
            wiz.step();
            if (held && wiz.casting() && (wiz.hero().x != wasX || wiz.hero().y != wasY)) ++movedWhile;
            for (const sim::Happening& one : wiz.happenings()) {
                if (one.who != wiz.hero().id) continue;
                if (one.what == sim::What::Cast && one.a == sim::skill::kMeteorite) {
                    ++casts;
                    if (lastCast >= 0) closest = std::min<int64_t>(closest, one.tick - lastCast);
                    lastCast = one.tick;
                }
                if (one.what == sim::What::Loosed && one.a == sim::skill::kMeteorite) {
                    loosedAt[falls % 8] = one.tick;
                    loosedOn[falls % 8] = one.whom;
                    ++falls;
                }
                // The staff's swings do not fly, so every thrown landing is a rock -- and it
                // lands its fall after the let-go, within two tiles of where it was called.
                if ((one.what == sim::What::Hit || one.what == sim::What::Missed) && one.thrown) {
                    if (one.what == sim::What::Hit) ++landed;
                    bool matched = false;
                    for (int k = 0; k < 8; ++k) {
                        if (loosedOn[k] != 0 && int64_t(one.tick) - loosedAt[k] == 7) matched = true;
                    }
                    if (!matched) ++lateOrEarly;
                    if (int64_t(one.tick) != fallTick) {
                        fallTick = one.tick;
                        thisFall = 0;
                    }
                    widest = std::max(widest, ++thisFall);
                }
            }
        }
        std::printf("  meteorite: %d cast, %d fell, %d landed, %d struck by one volley at the most, "
                    "%lld ticks apart at the closest, the clip %d ticks\n",
                    casts, falls, landed, widest, (long long)closest, lock);
        check(lock > 20, "the arm-up clip has its length in the realm");
        check(casts > 10 && falls > 0 && landed > 0, "he calls Meteorite through a hunt and it lands");
        check(closest >= wiz.coolsFor(sim::skill::kMeteorite) && closest >= 100,
              "never inside its cooldown");
        checkEqual(lateOrEarly, 0, "every rock lands its fall after the let-go");
        check(widest >= 2, "and one cast drops a rock on more than one body");
        checkEqual(movedWhile, 0, "and he does not move while he calls it");
    }

    // ---- Lightning: a channel -- three seconds of pulses into everything around him ----------
    {
        const sim::SkillRow& bolt = *sim::skillNumbered(sim::skill::kLightning);
        check(bolt.wizardry && bolt.channelled() && !bolt.primary() && !bolt.thrown() &&
                  bolt.pushes && bolt.spread == sim::Spread::Ring && bolt.damage == 17 &&
                  bolt.mana == 40 && bolt.coolTicks == 200 && bolt.channelTicks == 42,
              "Lightning is a channel round him as long as its clip, ten seconds to cool, and it "
              "pushes");
        check(bolt.force == 2.0f && sim::force(bolt, sim::HeroPoints{}) == 2.0f &&
                  sim::force(*sim::skillNumbered(sim::skill::kFireBall), sim::HeroPoints{}) == 1.0f,
              "and each strike is twice the band, where the other spells are once");
        check(bolt.pulseTicks == 3 && bolt.strikeFrom == 14 && bolt.strikeUntil == 32 &&
                  bolt.strikesEach == 1,
              "and it strikes every three ticks while his arm is up, once at most a body");
        const int32_t scroll = tables.itemAt(15, 2);
        check(scroll >= 0 && tables.items[size_t(scroll)].teaches == sim::skill::kLightning &&
                  tables.items[size_t(scroll)].teachesEnergy == 72,
              "the Scroll of Lighting teaches skill 3 at seventy-two energy");

        sim::Realm wiz;
        check(wiz.raise(&tables, 7, 190, 110, sim::Kin::DarkWizard, 12), "a wizard of twelve raises");
        check(wiz.learn(sim::skill::kLightning), "who knows Lightning");
        int channels = 0, pushes = 0, away = 0, widest = 0, stillWhile = 0;
        int64_t lastCast = -1, closest = 1 << 30, pulseTick = -1, earliest = 1 << 30;
        int pulses = 0, mostPulses = 0, thisPulse = 0, thisChannel = 0, sweptMost = 0;
        uint32_t swept[16] = {};
        int sweptTimes[16] = {};
        int sweptCount = 0, mostOnOne = 0;
        float worstStep = 0.0f;
        uint32_t fighting = 0, sliding = 0;
        float lastX = 0.0f, lastY = 0.0f, before = 0.0f;
        int slidFor = 0;
        for (int tick = 0; tick < 6000; ++tick) {
            const uint32_t nearest = wiz.hero().alive() ? nearestTo(wiz) : 0;
            if (nearest != 0 && nearest != fighting) {
                fighting = nearest;
                sim::Request request;
                request.kind = sim::Request::Kind::Attack;
                request.target = nearest;
                wiz.ask(request);
            }
            // The key, pressed whenever it may be.
            if (nearest != 0 && wiz.cooling(sim::skill::kLightning) == 0) {
                wiz.invoke(sim::skill::kLightning, nearest);
            }
            const float wasX = wiz.hero().x, wasY = wiz.hero().y;
            // Running before this tick: the cast's own tick may finish the step he was on.
            const bool running = wiz.hero().channelSkill != 0;
            wiz.step();
            // He does not move while it runs.
            if (running && wiz.hero().channelSkill != 0 &&
                (wiz.hero().x != wasX || wiz.hero().y != wasY)) {
                ++stillWhile;
            }
            if (sliding != 0) {
                const sim::Body* one = wiz.find(sliding);
                if (one != nullptr && one->alive()) {
                    worstStep = std::max(worstStep, std::hypot(one->x - lastX, one->y - lastY));
                    lastX = one->x;
                    lastY = one->y;
                    if (++slidFor == 6) {
                        const sim::Body& me = wiz.hero();
                        if (std::hypot(one->x - me.x, one->y - me.y) > before + 0.5f) ++away;
                        sliding = 0;
                    }
                } else {
                    sliding = 0;
                }
            }
            for (const sim::Happening& one : wiz.happenings()) {
                if (one.what == sim::What::Cast && one.who == wiz.hero().id &&
                    one.a == sim::skill::kLightning) {
                    ++channels;
                    if (lastCast >= 0) closest = std::min<int64_t>(closest, one.tick - lastCast);
                    lastCast = one.tick;
                    mostPulses = std::max(mostPulses, thisChannel);
                    thisChannel = 0;
                    sweptMost = std::max(sweptMost, sweptCount);
                    sweptCount = 0;
                }
                if (one.what == sim::What::Loosed && one.a == sim::skill::kLightning && lastCast >= 0) {
                    earliest = std::min<int64_t>(earliest, int64_t(one.tick) - lastCast);
                }
                if (one.what == sim::What::Loosed && one.a == sim::skill::kLightning) {
                    if (int64_t(one.tick) != pulseTick) {
                        pulseTick = one.tick;
                        ++pulses;
                        ++thisChannel;
                        thisPulse = 0;
                    }
                    widest = std::max(widest, ++thisPulse);
                    bool seen = false;
                    for (int k = 0; k < sweptCount; ++k) {
                        if (swept[k] == one.whom) {
                            seen = true;
                            ++sweptTimes[k];
                            mostOnOne = std::max(mostOnOne, sweptTimes[k]);
                        }
                    }
                    if (!seen && sweptCount < 16) {
                        sweptTimes[sweptCount] = 1;
                        mostOnOne = std::max(mostOnOne, 1);
                        swept[sweptCount++] = one.whom;
                    }
                }
                if (one.what != sim::What::Shoved) continue;
                ++pushes;
                const sim::Body* pushed = wiz.find(one.who);
                if (pushed != nullptr && sliding == 0) {
                    sliding = one.who;
                    lastX = pushed->x;
                    lastY = pushed->y;
                    slidFor = 0;
                    const sim::Body& me = wiz.hero();
                    before = std::hypot(pushed->x - me.x, pushed->y - me.y);
                }
            }
        }
        mostPulses = std::max(mostPulses, thisChannel);
        sweptMost = std::max(sweptMost, sweptCount);
        std::printf("  lightning: %d channels, %d pulses (%d in one at most), %d bodies in one "
                    "pulse at most, closest casts %lld ticks apart, %d pushes, %d measured "
                    "further off, worst tick %.2f tiles\n",
                    channels, pulses, mostPulses, widest, (long long)closest, pushes, away,
                    double(worstStep));
        check(channels > 3, "he channels Lightning through a hunt");
        // Up to seven, one a body: a strike with nobody left unstruck in reach is not thrown, so a
        // cast strikes as many times as there are bodies round him, to seven.
        check(mostPulses >= 1 && mostPulses <= 7, "and a channel strikes up to seven times");
        check(earliest >= 14, "and never before his arm is up");
        check(closest >= wiz.coolsFor(sim::skill::kLightning) && closest >= 60,
              "and never twice inside its cooldown");
        // One body a strike, and round the ring: a channel with company strikes more than one.
        checkEqual(widest, 1, "and each strike goes to one body");
        check(mostOnOne <= 1, "and no body is struck more than once in a cast");
        check(sweptMost >= 2, "and a channel goes round to more than one");
        checkEqual(stillWhile, 0, "and he stands still while it runs");
        // A strike at twice the band kills most of what it hits here, and the dead are not
        // pushed; what survives is.
        check(pushes >= 2 && away > 0, "and it pushes what it does not kill, away from him");
        check(worstStep <= 0.41f, "and slides it there, no tick moving it more than 0.4 of a tile");
    }

    // ---- a cooldown outlives a save (the user, 2026-09-28) -----------------------------------
    {
        sim::Realm knight;
        check(knight.raise(&tables, 7, 190, 110, sim::Kin::DarkKnight, 30), "a knight raises to guard");
        check(knight.learn(sim::skill::kDefense), "who knows Defense");
        check(knight.equip(tables.armNamed("Sword01"), tables.armNamed("Shield01"), true),
              "a sword and a Small Shield");
        knight.invoke(sim::skill::kDefense, knight.hero().id);
        for (int tick = 0; tick < 60 && knight.cooling(sim::skill::kDefense) == 0; ++tick) knight.step();
        for (int tick = 0; tick < 100; ++tick) knight.step();
        const int64_t left = knight.cooling(sim::skill::kDefense);
        check(left > 0, "he raised it and it is cooling");
        const sim::HeroRecord saved = knight.record();
        sim::Realm again;
        check(again.raise(&tables, 7, 190, 110, sim::Kin::DarkKnight, 30), "and comes back");
        again.restore(saved);
        checkEqual((long long)again.cooling(sim::skill::kDefense), (long long)left,
                   "with the same wait left on Defense");
        sim::HeroRecord edited = saved;
        edited.coolsLeft[sim::skillIndexOf(sim::skill::kDefense)] = 100000000;
        sim::Realm forged;
        check(forged.raise(&tables, 7, 190, 110, sim::Kin::DarkKnight, 30), "a forged save raises");
        forged.restore(edited);
        check(forged.cooling(sim::skill::kDefense) <= forged.coolsFor(sim::skill::kDefense),
              "and an edited file holds a key no longer than its own cooldown");
    }
}

void testStandsOverTheKill(const content::Tables& tables) {
    std::printf("standing over the kill\n");
    sim::Realm realm;
    // A level 1 hero put down in the hunting ground with no orders: he never swings, and what
    // is out there kills him. That is the only way a beast loses a quarry to death.
    check(realm.raise(&tables, 3, 200, 160, sim::Kin::DarkKnight, 1), "a realm raises for the kill");

    int64_t diedAt = -1;
    for (int tick = 0; tick < 6000 && diedAt < 0; ++tick) {
        realm.step();
        if (!realm.hero().alive()) diedAt = realm.tick();
    }
    check(diedAt >= 0, "and what is hunting him kills him");
    if (diedAt < 0) return;

    // Everything standing close enough to have been the one that did it.
    const float heroX = realm.hero().x, heroY = realm.hero().y;
    struct Where {
        uint32_t id = 0;
        float x = 0.0f, y = 0.0f;
    };
    std::vector<Where> over;
    for (const sim::Body& one : realm.bodies()) {
        if (!one.monster() || !one.alive()) continue;
        if (std::max(std::fabs(one.x - heroX), std::fabs(one.y - heroY)) <= 3.0f) {
            over.push_back({one.id, one.x, one.y});
        }
    }
    check(!over.empty(), "with something standing over him");

    // The hold is shorter than the revive (kRiseTicks), so nothing here races the hero getting
    // back up.
    bool held = true;
    for (int tick = 0; tick + 1 < sim::kStandOverTicks; ++tick) {
        realm.step();
        for (const Where& was : over) {
            const sim::Body* now = realm.find(was.id);
            if (now != nullptr && (now->x != was.x || now->y != was.y)) held = false;
        }
    }
    check(held, "and not one of them takes a step while the body is going down");
}

// The two area shapes, and the cooldown's own arithmetic under them.
//
// A spin and a sweep cannot be posed by hand -- there is no way to put four monsters round the
// knight -- so this is a hunt with a hand that presses its keys in turn, and what is checked is
// what the happenings say: who was caught, in what order, and never anything outside the shape.
// The single-target skills are covered by the same run for free.
void testSkills(const content::Tables& tables) {
    std::printf("skills\n");

    // The formulas first, which need no realm at all. Monotone in agility, never under the
    // floor, and the floor is the wall: docs/skills-dk.md §3.2's "a floor and not a zero".
    const sim::SkillRow& cyclone = *sim::skillNumbered(sim::skill::kCyclone);
    const sim::SkillRow& guard = *sim::skillNumbered(sim::skill::kDefense);
    int32_t last = sim::cooldownTicks(cyclone, 0, 20);
    bool falls = true, floored = true;
    for (int agility = 0; agility <= 3000; agility += 10) {
        const int32_t cool = sim::cooldownTicks(cyclone, agility, 20);
        falls &= cool <= last;
        floored &= cool >= 20;
        last = cool;
    }
    check(falls, "more agility is never a longer cooldown");
    check(floored, "and no cooldown falls under the clip that floors it");
    checkEqual(sim::cooldownTicks(cyclone, 300, 1), cyclone.coolTicks / 2,
               "300 agility halves a cooldown");
    check(sim::cooldownTicks(guard, 100000, sim::floorTicksFor(guard, 0)) > guard.boonTicks,
          "and a guard's cooldown always outlasts the guard");
    // Defense's share: the shield, strength and agility raise it, and nothing reaches the cap.
    const sim::HeroPoints fresh{28, 20, 25, 10};
    const float start = sim::guardShare(fresh, 3);
    check(start > 0.10f && start < 0.25f, "a new knight's guard takes a modest share");
    check(sim::guardShare(fresh, 20) > start &&
              sim::guardShare({128, 20, 25, 10}, 3) > start &&
              sim::guardShare({28, 120, 25, 10}, 3) > start,
          "the shield, strength and agility each raise the guard");
    check(sim::guardShare({28, 20, 25, 500}, 3) == start, "and energy does not");
    // Strength is the knight's as energy is the wizard's: each spending on his main stat stays
    // within a point of the other at every stage.
    for (int spent : {0, 25, 110, 300}) {
        const float knight = sim::guardShare({28 + spent, 20, 25, 10}, 3);
        const float wizard = sim::barrierShare({18, 18, 15, 30 + spent}, 3);
        check(std::fabs(knight - wizard) < 0.01f,
              "a knight on strength and a wizard on energy stand level");
    }
    check(sim::guardShare({30000, 30000, 0, 30000}, 5000) < sim::kGuardCap,
          "and no build reaches the guard's cap");
    check(sim::force(cyclone, sim::HeroPoints{2000, 0, 0, 0}) >
              sim::force(cyclone, sim::HeroPoints{28, 0, 0, 0}),
          "strength is force");

    sim::Realm realm;
    check(realm.raise(&tables, 7, 190, 110, sim::Kin::DarkKnight, 60), "a realm raises for the keys");
    // A blade in his hand, because nothing is thrown bare-handed and `raise` dresses nobody:
    // `given`, so a level-60 knight's strength is not what this is testing.
    check(realm.equip(tables.armNamed("Sword03"), -1, true), "and a blade is put in his hand");

    // ---- and nothing in his head (docs/skills-dk.md §3.3) ------------------------------------
    //
    // **A character is raised knowing no skills at all**, which is the whole design as of
    // 2026-09-23: the orbs are the route, and both stand-ins that came before -- the flat grant
    // at raise, and the ladder that granted by level -- are gone. So a knight buys his bar.
    {
        int met = 0;
        for (int i = 0; i < sim::skillCount(); ++i) met += realm.knows(sim::skillAt(i).number);
        checkEqual(met, 0, "a knight is raised knowing nothing");
    }

    // Hanzo's counter, and the nine orbs on it. This is the route end to end: walk to the
    // blacksmith, buy the orb, right-click it, and the skill is his -- the same three calls the
    // windows make (`Talk`, `buy`, `useItem`).
    int hanzo = -1;
    for (size_t i = 0; i < tables.folk.size(); ++i) {
        if (tables.folk[i].number == 251) hanzo = int(i);
    }
    check(hanzo >= 0, "Hanzo is in the town's table");
    realm.earn(3000000);  // a purse for nine orbs; the prices are the curve's, not this test's
    {
        sim::Request talk;
        talk.kind = sim::Request::Kind::Talk;
        talk.target = uint32_t(hanzo);
        realm.ask(talk);
    }
    for (int tick = 0; tick < 4000 && realm.trading() < 0; ++tick) realm.step();
    check(realm.trading() == hanzo, "walked to the blacksmith and was served");

    int orbs = 0, read = 0;
    int count = 0;
    const sim::Offer* stock = sim::stockOf(251, &count);
    for (int i = 0; i < count; ++i) {
        if (stock[i].group != 12) continue;
        ++orbs;
        const int slot = realm.buy(stock[i].slot);
        if (slot < 0) continue;
        // Read where it landed. A second read of the same orb is refused by `learn` and the orb
        // is left in the bag, which is checked below on the one that is still there.
        if (realm.useItem(slot)) ++read;
    }
    checkEqual(orbs, 9, "the blacksmith stocks all nine orbs");
    checkEqual(read, 9, "and every one of them was bought and read");
    bool all = true;
    for (int i = 0; i < sim::skillCount(); ++i) {
        if (sim::skillAt(i).kin != sim::Kin::DarkKnight) continue;
        all &= realm.knows(sim::skillAt(i).number);
    }
    check(all, "so the knight now knows every knight's skill in the table");
    check(!realm.knows(sim::skill::kEnergyBall), "and not the wizard's Energy Ball");

    // A tenth purchase is refused at the reading rather than at the counter: MU sells a man as
    // many orbs as he can pay for, and the second one does nothing when he opens it.
    {
        int at = -1;
        for (int i = 0; i < count && at < 0; ++i) {
            if (stock[i].group == 12) at = realm.buy(stock[i].slot);
        }
        check(at >= 0, "a second orb of something he knows is sold to him");
        check(!realm.useItem(at), "and reading it again is refused");
        check(!realm.satchel()[at].empty(), "so it is still in his bag to be sold");

        // The undo on a sale (the user's, 2026-09-28): taken back at what it fetched, into
        // the slot it left, and only for kBuybackSeconds.
        const int32_t orb = realm.satchel()[at].item;
        const int64_t purse = realm.money();
        const int64_t paid = realm.sellItem(at);
        check(paid > 0 && realm.satchel()[at].empty(), "the spare orb sells");
        check(realm.lastSale() != nullptr, "and the sale can be undone");
        checkEqual(realm.buyBack(), at, "bought back into the slot it left");
        checkEqual((long long)realm.money(), (long long)purse, "for exactly what it fetched");
        checkEqual(realm.satchel()[at].item, orb, "and it is the same orb");
        check(realm.buyBack() < 0, "a second undo has nothing to take back");
        realm.sellItem(at);
        for (int t = 0; t < sim::Realm::kBuybackSeconds * 20; ++t) realm.step();
        check(realm.lastSale() == nullptr && realm.buyBack() < 0,
              "and past its window a sale is final");
    }
    check(!realm.learn(sim::skill::kSlash), "and learning one twice is refused");

    // ---- what the orb asks (docs/skills-dk.md §3.3) -------------------------------------------
    //
    // The level is the ITEM's, checked when it is read: 0.75's own ladder of carriers, so a
    // knight meets his skills in the order MU gave them to him. And the class is the item's too.
    {
        sim::Realm young;
        check(young.raise(&tables, 3, 138, 124, sim::Kin::DarkKnight, 12),
              "a knight of twelve raises in town");
        young.earn(3000000);
        sim::Request talk;
        talk.kind = sim::Request::Kind::Talk;
        talk.target = uint32_t(hanzo);
        young.ask(talk);
        for (int tick = 0; tick < 4000 && young.trading() < 0; ++tick) young.step();
        check(young.trading() == hanzo, "and is served");
        const auto orbOf = [&](int32_t skill) {
            for (int i = 0; i < count; ++i) {
                if (stock[i].group != 12) continue;
                const int32_t at = tables.itemAt(12, stock[i].number);
                if (at >= 0 && tables.items[size_t(at)].teaches == skill) return stock[i].slot;
            }
            return -1;
        };
        const int uppercut = young.buy(orbOf(sim::skill::kUppercut));
        const int slash = young.buy(orbOf(sim::skill::kSlash));
        check(uppercut >= 0 && slash >= 0, "he buys the orb he is ready for and one he is not");
        check(young.useItem(uppercut), "twelve is enough for Uppercut, which asks for twelve");
        check(young.knows(sim::skill::kUppercut), "and he has it");
        check(!young.useItem(slash), "but Slash asks for fifty-two and he is twelve");
        check(!young.knows(sim::skill::kSlash), "so he has not learned it");
        check(!young.satchel()[slash].empty(), "and the orb is unspent, waiting for the level");

        // And a wizard may not read a knight's orb at any level: the row names its class, and
        // `useItem` asks that before it asks anything else.
        sim::Realm wizard;
        check(wizard.raise(&tables, 3, 138, 124, sim::Kin::DarkWizard, 60), "a wizard raises");
        wizard.earn(3000000);
        sim::Request ask;
        ask.kind = sim::Request::Kind::Talk;
        ask.target = uint32_t(hanzo);
        wizard.ask(ask);
        for (int tick = 0; tick < 4000 && wizard.trading() < 0; ++tick) wizard.step();
        const int his = wizard.buy(orbOf(sim::skill::kUppercut));
        check(his >= 0, "and buys the knight's orb, which nothing stops him doing");
        check(!wizard.useItem(his), "but he cannot read it");
        check(!wizard.knows(sim::skill::kUppercut), "and has learned nothing");

        // ---- the wizard's own: Energy Ball (MU2/docs/spells.md) ----------------------------
        check(wizard.knows(sim::skill::kEnergyBall),
              "a wizard is born knowing Energy Ball (AddEnergyBallForDarkWizard)");
        // The band on paper, which fits on a line: energy/9 + 3 to energy/4 + 3 + 1, bare-handed.
        {
            sim::Fighter caster;
            caster.attackRate = 1000.0f;
            caster.wizardMinimum = 30.0 / 9.0;
            caster.wizardMaximum = 30.0 / 4.0;
            sim::Fighter dummy;
            sim::Random dice(11);
            int lowest = 1 << 30, highest = 0;
            for (int n = 0; n < 2000; ++n) {
                const sim::Blow blow = sim::cast(caster, dummy, 3, dice);
                if (!blow.hit) continue;
                lowest = std::min(lowest, blow.rolled);
                highest = std::max(highest, blow.rolled);
            }
            // int(3.33 + 3) = 6, and the top is exclusive: int(7.5 + 4) = 11, so 6 to 10.
            checkEqual(lowest, 6, "a 30-energy Energy Ball rolls from int(30/9 + 3) = 6");
            checkEqual(highest, 10, "to under int(30/4 + 3 + 3/2) = 11");
            caster.wizardryRate = 1.0 + 23.0 / 100.0;  // a staff of magic power 46
            int top = 0;
            for (int n = 0; n < 2000; ++n) top = std::max(top, sim::cast(caster, dummy, 3, dice).rolled);
            checkEqual(top, 13, "and a 23-rise staff lifts the top to under int(11.5 x 1.23) = 14");
        }
    }
    check(!realm.learn(sim::skill::kSlash), "and learning one twice is refused");

    // ---- the wizard's guard: Soul Barrier (docs/skills-dw.md) --------------------------------
    //
    // The user's rule of 2026-09-28: bought as a scroll at Pasi's, read once, thrown on himself
    // behind a shield, and at the start of the game within a point of what the knight's Defense
    // takes -- off energy where the knight's is off his body.
    {
        const sim::HeroPoints knight{28, 20, 25, 10}, wizard{18, 18, 15, 30};
        for (int shield : {1, 3}) {
            const float guard = sim::guardShare(knight, shield);
            const float barrier = sim::barrierShare(wizard, shield);
            check(std::fabs(guard - barrier) < 0.01f,
                  "a new wizard's barrier and a new knight's guard are within a point");
        }
        const sim::HeroPoints fresh = wizard;
        const float start = sim::barrierShare(fresh, 3);
        check(sim::barrierShare(fresh, 5) > start && sim::barrierShare({18, 18, 15, 130}, 3) > start &&
                  sim::barrierShare({18, 118, 15, 30}, 3) > start,
              "the shield, energy and agility each raise the barrier");
        check(sim::barrierShare({500, 18, 15, 30}, 3) == start, "and strength does not");
        check(sim::barrierShare({0, 30000, 0, 30000}, 5000) < sim::kGuardCap,
              "and no build reaches the cap");
        const sim::SkillRow& barrier = *sim::skillNumbered(sim::skill::kSoulBarrier);
        check(barrier.onSelf() && barrier.kin == sim::Kin::DarkWizard &&
                  barrier.families == sim::arms::kShield && !barrier.wizardry,
              "Soul Barrier is the wizard's, thrown on himself, off a shield");
        checkEqual(barrier.boonTicks, sim::skillNumbered(sim::skill::kDefense)->boonTicks,
                   "and it stands as long as Defense");

        int pasi = -1;
        for (size_t i = 0; i < tables.folk.size(); ++i) {
            if (tables.folk[i].number == 254) pasi = int(i);
        }
        check(pasi >= 0, "Pasi is in the town's table");
        int stocked = 0;
        const sim::Offer* scrolls = sim::stockOf(254, &stocked);
        int scrollSlot = -1;
        for (int i = 0; i < stocked; ++i) {
            if (scrolls[i].group == 15 && scrolls[i].number == 15) scrollSlot = scrolls[i].slot;
        }
        check(scrollSlot >= 0, "Pasi sells the Scroll of Soul Barrier");
        const int32_t row = tables.itemAt(15, 15);
        check(row >= 0 && tables.items[size_t(row)].teaches == sim::skill::kSoulBarrier,
              "and it is cooked, teaching skill 16");

        sim::Realm mage;
        check(mage.raise(&tables, 3, 138, 124, sim::Kin::DarkWizard, 6), "a wizard of six raises");
        mage.earn(3000000);
        sim::Request talk;
        talk.kind = sim::Request::Kind::Talk;
        talk.target = uint32_t(pasi);
        mage.ask(talk);
        for (int tick = 0; tick < 4000 && mage.trading() < 0; ++tick) mage.step();
        check(mage.trading() == pasi, "walks to Pasi and is served");
        const int bought = mage.buy(scrollSlot);
        check(bought >= 0, "buys the scroll");
        check(mage.useItem(bought), "and reads it at level six");
        check(mage.knows(sim::skill::kSoulBarrier), "so Soul Barrier is his");

        // Out of town, with nothing on his arm first: refused, as a knight's guard is.
        sim::Realm field;
        check(field.raise(&tables, 7, 190, 110, sim::Kin::DarkWizard, 60), "a wizard in the field");
        check(field.learn(sim::skill::kSoulBarrier), "who knows the barrier");
        const auto thrown = [](sim::Realm& r) {
            bool cast = false;
            r.invoke(sim::skill::kSoulBarrier, r.hero().id);
            for (int tick = 0; tick < 60 && !cast; ++tick) {
                r.step();
                for (const sim::Happening& one : r.happenings()) {
                    cast |= one.what == sim::What::Cast && one.who == r.hero().id;
                }
            }
            return cast;
        };
        check(!thrown(field), "is refused it with no shield on his arm");
        check(field.equip(tables.armNamed("Staff01"), tables.armNamed("Shield01"), true),
              "a staff in one hand and a Small Shield on the other");
        check(thrown(field), "and throws it behind the shield");
        const sim::Body& hero = field.hero();
        const float share = sim::barrierShare(hero.points, hero.shieldDefense);
        check(std::fabs(hero.boonDamageTaken - (1.0f - share)) < 1e-6f,
              "and every blow is taken down by the barrier's share");
        check(hero.boonUntil - field.tick() > 5000, "for five minutes");
        // And the shield taken off ends it, the barrier's share and all.
        check(field.moveItem(sim::kWeaponLeft, sim::kWorn + 30), "the shield goes into the bag");
        check(field.hero().boonUntil <= field.tick() && field.hero().stats.damageTaken == 1.0,
              "and the barrier falls with it");
        check(field.cooling(sim::skill::kSoulBarrier) > 0, "while its wait runs on");

        // And a knight may not read it, whatever his level.
        sim::Realm knightRealm;
        check(knightRealm.raise(&tables, 3, 138, 124, sim::Kin::DarkKnight, 60), "a knight raises");
        knightRealm.earn(3000000);
        knightRealm.ask(talk);
        for (int tick = 0; tick < 4000 && knightRealm.trading() < 0; ++tick) knightRealm.step();
        const int his = knightRealm.buy(scrollSlot);
        check(his >= 0 && !knightRealm.useItem(his), "and cannot read the wizard's scroll");
    }

    // ---- the families, before the hunt (docs/skills-dk.md §3.1b) -----------------------------
    //
    // The gate itself, asked of the table rather than of a fight: every attack names the hands
    // that may throw it, no attack is thrown bare-handed or off a bow, and the guard asks for a
    // shield. This is what keeps a row added later from quietly being throwable with anything.
    {
        bool gated = true, everyFamilyHasThree = true;
        for (int i = 0; i < sim::skillCount(); ++i) {
            const sim::SkillRow& row = sim::skillAt(i);
            // A spell asks nothing of the hand: a staff grants nothing in 0.75.
            if (row.wizardry) {
                gated &= row.families == sim::arms::kNone && row.suits(sim::arms::kNone);
                continue;
            }
            gated &= row.families != 0;
            gated &= row.onSelf() ? row.families == sim::arms::kShield
                                  : (row.families & sim::arms::kShield) == 0;
        }
        check(gated, "every skill names the hand it is thrown with");
        // And no family is left with a dead bar, which is the whole reason the three rows past
        // 0.75 exist: three attacks at least, whatever he is holding.
        const uint32_t families[] = {sim::arms::kSword1, sim::arms::kSword2, sim::arms::kAxe1,
                                     sim::arms::kAxe2,   sim::arms::kMace1,  sim::arms::kSpear};
        for (uint32_t family : families) {
            int throwable = 0;
            for (int i = 0; i < sim::skillCount(); ++i) {
                if (sim::skillAt(i).suits(family)) ++throwable;
            }
            everyFamilyHasThree &= throwable >= 3;
        }
        check(everyFamilyHasThree, "and every family has at least three keys to press");
        // The families a weapon reports, off the cooked rows themselves: a Rapier is a
        // one-handed sword, a Giant Sword is two-handed, a Nikkea Axe is a two-handed axe, a
        // Berdysh is a spear whatever its width says, and a bow is no family at all.
        const auto familyNamed = [&](const char* name) {
            const int32_t at = tables.armNamed(name);
            return at < 0 ? 0u : sim::familyOf(&tables.arms[size_t(at)]);
        };
        checkEqual((long long)familyNamed("Sword03"), (long long)sim::arms::kSword1,
                   "a Rapier is a one-handed sword");
        checkEqual((long long)familyNamed("Sword16"), (long long)sim::arms::kSword2,
                   "a Giant Sword is a two-handed sword");
        checkEqual((long long)familyNamed("Axe03"), (long long)sim::arms::kAxe1,
                   "a Double Axe is a one-handed axe");
        checkEqual((long long)familyNamed("Axe07"), (long long)sim::arms::kAxe2,
                   "a Nikkea Axe is a two-handed axe");
        checkEqual((long long)familyNamed("Mace02"), (long long)sim::arms::kMace1,
                   "a Morning Star is a mace");
        checkEqual((long long)familyNamed("Spear08"), (long long)sim::arms::kSpear,
                   "a Berdysh is a spear");
        checkEqual((long long)familyNamed("Bow01"), 0LL, "and a bow is no family at all");
        checkEqual((long long)sim::familyOf(nullptr), 0LL, "as is an empty hand");
        // And the two that matter in play: 0.75's own carriers decide who may throw what.
        check(!sim::skillNumbered(sim::skill::kFallingSlash)->suits(sim::arms::kSword1),
              "an axe's Falling Slash cannot be thrown off a sword");
        check(sim::skillNumbered(sim::skill::kFallingSlash)->suits(sim::arms::kMace1),
              "but a mace throws it, as the Morning Star did");
        check(!sim::skillNumbered(sim::skill::kSlash)->suits(sim::arms::kSword1),
              "and Slash needs both hands on it");
    }

    int casts[sim::kSkills] = {};
    int caught[sim::kSkills] = {};   // bodies struck by each, over the whole hunt
    int widest[sim::kSkills] = {};   // and the most any one throw caught
    bool inShape = true, inOrder = true, ringOnly = true;
    int pressed = 0;
    uint32_t fighting = 0;
    sim::Findings findings;
    int32_t casting = 0;      // the skill whose blow is in the air
    float castAim = 0.0f;
    // **The hunt is run once a weapon**, because a skill is now thrown with its own family and
    // one hand can no longer reach both shapes: a one-handed sword throws the spin (Cyclone) and
    // a two-handed one throws the sweep (Slash). Same keys, same checks, same counters -- only
    // what is in his hand changes between the two runs.
    const auto hunt = [&](int ticks) {
    for (int tick = 0; tick < ticks; ++tick) {
        const sim::Body& hero = realm.hero();
        if (hero.alive()) {
            uint32_t nearest = 0;
            float closest = 1e30f;
            for (const sim::Body& one : realm.bodies()) {
                if (!one.monster() || !one.alive()) continue;
                const float off = std::max(std::fabs(one.x - hero.x), std::fabs(one.y - hero.y));
                if (off <= 12.0f && off < closest) {
                    closest = off;
                    nearest = one.id;
                }
            }
            if (nearest != 0) {
                if (nearest != fighting) {
                    fighting = nearest;
                    sim::Request request;
                    request.kind = sim::Request::Kind::Attack;
                    request.target = nearest;
                    realm.ask(request);
                }
                for (int n = 1; n <= sim::skillCount(); ++n) {
                    const int i = (pressed + n) % sim::skillCount();
                    const sim::SkillRow& row = sim::skillAt(i);
                    if (!realm.knows(row.number) || realm.cooling(row.number) > 0) continue;
                    realm.invoke(row.number, nearest);
                    pressed = i;
                    break;
                }
            } else if (!hero.walking) {
                sim::Request request;
                request.kind = sim::Request::Kind::WalkTo;
                request.column = 190;
                request.row = 110;
                realm.ask(request);
            }
        }
        realm.step();
        sim::audit(realm, findings);

        const sim::Body& hero2 = realm.hero();
        int hits = 0;
        float lastOff = -1.0f;
        for (const sim::Happening& happening : realm.happenings()) {
            if (happening.what == sim::What::Cast && happening.who == hero2.id) {
                const int index = sim::skillIndexOf(happening.a);
                if (index >= 0) ++casts[index];
                casting = happening.a;
                castAim = hero2.aim;
            }
            // A plain swing begun: whatever skill was in the air has landed, on somebody or on
            // nobody. A sweep whose one target a guard killed mid-clip strikes nothing, and left
            // set, `casting` scored his next ordinary blow as that sweep, against its old aim.
            if (happening.what == sim::What::Swung && happening.who == hero2.id &&
                happening.a == 0) {
                casting = 0;
            }
            if ((happening.what != sim::What::Hit && happening.what != sim::What::Missed) ||
                happening.who != hero2.id) {
                continue;
            }
            const sim::SkillRow* row = sim::skillNumbered(casting);
            if (!row || row->spread == sim::Spread::One) continue;
            const sim::Body* victim = realm.find(happening.whom);
            if (!victim) continue;
            // Measured after the tick, so a monster has had its own step since the blow: the
            // slack is one of those steps and no more.
            const float off = std::max(std::fabs(victim->x - hero2.x),
                                       std::fabs(victim->y - hero2.y));
            inShape &= off <= row->reach + 0.3f;
            if (row->spread == sim::Spread::Arc) {
                const float toward = std::atan2(victim->y - hero2.y, victim->x - hero2.x);
                ringOnly &= std::fabs(sim::wrapped(toward - castAim)) <= sim::kArcHalfAngle + 0.2f;
            }
            // Nearest first: a landing's victims come out in the order they were struck.
            inOrder &= off >= lastOff - 0.3f;
            lastOff = off;
            ++hits;
            const int index = sim::skillIndexOf(casting);
            if (index >= 0) ++caught[index];
        }
        if (hits > 0) {
            const int index = sim::skillIndexOf(casting);
            if (index >= 0) widest[index] = std::max(widest[index], hits);
            casting = 0;
        }
    }
    };
    hunt(6000);
    // And again with both hands on the hilt. The Giant Sword is what 0.75 carried Slash on, so
    // it is what throws it here; `given` again, because a level-60 knight cannot lift it.
    check(realm.equip(tables.armNamed("Sword16"), -1, true), "the Giant Sword is taken up");
    fighting = 0;
    hunt(6000);
    // And two shorter runs for the two families a sword reaches none of: the axe's overhead
    // (Falling Slash, which a sword may not throw at all) and the spear's stab (Death Stab,
    // which nothing else may). Without these the hunt would never press either key.
    check(realm.equip(tables.armNamed("Axe07"), -1, true), "then the Nikkea Axe");
    fighting = 0;
    hunt(3000);
    check(realm.equip(tables.armNamed("Spear08"), -1, true), "and then the Berdysh");
    fighting = 0;
    hunt(3000);

    for (int i = 0; i < sim::skillCount(); ++i) {
        const sim::SkillRow& row = sim::skillAt(i);
        if (casts[i] == 0) continue;
        std::printf("  %s: %d thrown", row.name, casts[i]);
        if (row.spread != sim::Spread::One) {
            std::printf(", %d bodies caught, %d at once at the widest", caught[i], widest[i]);
        }
        std::printf("\n");
    }
    const int ring = sim::skillIndexOf(sim::skill::kCyclone);
    const int arc = sim::skillIndexOf(sim::skill::kSlash);
    check(casts[ring] > 0, "the spin was thrown");
    check(casts[arc] > 0, "and the sweep");
    check(caught[ring] >= casts[ring], "a spin catches at least what it was aimed at");
    check(widest[ring] >= 2, "and catches a crowd when there is one");
    check(inShape, "nothing outside the shape was ever struck");
    check(ringOnly, "and nothing behind him by a sweep");
    check(inOrder, "and the nearest was struck first");

    checkEqual((long long)findings.castUnlearned, 0, "nothing cast what it had not learned");
    checkEqual((long long)findings.castEarly, 0, "and nothing cast while it was cooling");
    checkEqual((long long)findings.castForever, 0, "and no guard outlasts its own cooldown");
}

void testSpamClicks(const content::Tables& tables) {
    std::printf("spam clicks\n");
    sim::Realm realm;
    check(realm.raise(&tables, 11, 138, 124), "a realm raises for the clicking");
    uint32_t seed = 12345;
    const auto next = [&seed]() {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return seed;
    };
    constexpr float kToDegrees = 180.0f / 3.14159265f;
    float fastest = 0.0f, widest = 0.0f;
    int longestStall = 0, stall = 0, asked = 0;
    bool standable = true;
    int lastColumn = -1, lastRow = -1;
    for (int tick = 0; tick < 1200; ++tick) {
        const sim::Body& hero = realm.hero();
        bool clicked = false;
        if (tick < 1100 && next() % 3 != 0) {
            sim::Request walk;
            walk.kind = sim::Request::Kind::WalkTo;
            walk.column = hero.column() + int(next() % 13) - 6;
            walk.row = hero.row() + int(next() % 13) - 6;
            if (tables.grid.open(walk.column, walk.row, content::kWallCharacter)) {
                realm.ask(walk);
                clicked = true;
                lastColumn = walk.column;
                lastRow = walk.row;
                ++asked;
            }
        }
        const float x = hero.x, y = hero.y;
        realm.step();
        const float dx = realm.hero().x - x, dy = realm.hero().y - y;
        const float moved = std::sqrt(dx * dx + dy * dy);
        fastest = std::max(fastest, moved);
        if (moved > 1e-4f) {
            const float off = std::fabs(std::remainder(std::atan2(dy, dx) - realm.hero().facing,
                                                       6.28318530718f)) * kToDegrees;
            widest = std::max(widest, off);
            stall = 0;
        } else if (realm.hero().walking) {
            stall = clicked ? 1 : stall + 1;
            longestStall = std::max(longestStall, stall);
            if (std::getenv("STALL_TRACE")) {
                std::printf("    tick %d stall %d at %.3f,%.3f facing %.1f aim %.1f turning %d "
                            "route %zu onStep %zu next %d,%d\n", tick, stall, realm.hero().x,
                            realm.hero().y, realm.hero().facing * kToDegrees,
                            realm.hero().aim * kToDegrees, int(realm.hero().turning),
                            realm.hero().route.size(), realm.hero().onStep,
                            realm.hero().route.empty() ? -1 : realm.hero().route[realm.hero().onStep].column,
                            realm.hero().route.empty() ? -1 : realm.hero().route[realm.hero().onStep].row);
            }
        } else {
            stall = 0;
        }
        standable &= tables.grid.open(realm.hero().column(), realm.hero().row(),
                                      content::kWallCharacter);
    }
    std::printf("  %d clicks; fastest %.4f tiles a tick (pace %.4f), widest %.1f degrees off "
                "his facing, longest stall %d ticks\n",
                asked, fastest, realm.hero().speed, widest, longestStall);
    check(asked > 500, "the hand clicked hundreds of times");
    check(fastest <= realm.hero().speed + 1e-4f, "never faster than his pace");
    check(widest <= 90.0f, "never walking sideways or backwards");
    check(longestStall <= 2, "never walking on the spot longer than a turn");
    check(standable, "never on a tile the grid refuses");
    check(!realm.hero().walking && realm.hero().x == float(lastColumn) &&
              realm.hero().y == float(lastRow),
          "and ends standing exactly on the last tile asked for");
}

}  // namespace

// MOVEMENT_OPERATE: a click on a lean box walks him to its tile and leans him back at the box's own
// angle once the walk is over, a walk takes the pose off, and a box on a NoMove tile is refused
// before any route is planned. Perch 22 is a lean box by the wall at (122, 110); perch 39 the one
// at (113, 121), whose tile is NoMove.
void testPerches(const content::Tables& tables) {
    std::printf("a lean box is walked to and leant on\n");
    checkEqual(int64_t(tables.perches.size()), 110, "Lorencia has MU2's 110 perches");
    sim::Realm realm;
    check(realm.raise(&tables, 7, 125, 112), "a realm raises in the town");
    const content::Perch& box = tables.perches[22];
    check(box.pose == uint8_t(sim::Pose::Leaning) && box.turns, "perch 22 is a lean that turns");

    sim::Request lean;
    lean.kind = sim::Request::Kind::Perch;
    lean.target = 22;
    realm.ask(lean);
    bool leantEarly = false;
    for (int tick = 0; tick < 100 && realm.hero().pose == sim::Pose::Standing; ++tick) {
        realm.step();
        leantEarly |= realm.hero().pose != sim::Pose::Standing && realm.hero().walking;
    }
    check(realm.hero().pose == sim::Pose::Leaning, "he leans");
    check(!leantEarly, "and not before the walk is over");
    check(realm.hero().column() == box.column && realm.hero().row() == box.row,
          "on the box's own tile");
    checkNear(realm.hero().facing, box.aim, 1e-4, "facing the way the box does");

    sim::Request walk;
    walk.kind = sim::Request::Kind::WalkTo;
    walk.column = 126;
    walk.row = 112;
    realm.ask(walk);
    realm.step();
    check(realm.hero().pose == sim::Pose::Standing && realm.hero().walking,
          "a walk stands him up on its first tick");
    for (int tick = 0; tick < 100 && realm.hero().walking; ++tick) realm.step();

    lean.target = 39;
    realm.ask(lean);
    for (int tick = 0; tick < 40; ++tick) realm.step();
    check(realm.hero().pose == sim::Pose::Standing && realm.hero().column() == 126,
          "a box on a NoMove tile is refused where he stands");
}

// Baz's vault: opened by walking to him, shut by walking off; an item across and back, the Zen
// across and back, and every refusal leaving both sides as they were.
// Wear and the repair (sim/wear.h): the tables against MuMain's and OpenMU's own, the prices
// worked by hand from the formula, a worn shield's cut on his block, the wear a real fight
// leaves, and Hanzo putting it right.
// The jewels on a thing: OpenMU's two handlers and MuMain's CanUpgradeItem ranges.
void testRefine(const content::Tables& tables) {
    std::printf("refine\n");
    const int bless = tables.itemAt(14, 13), soul = tables.itemAt(14, 14);
    const int chaos = tables.itemAt(12, 15), kris = tables.itemAt(0, 0);
    const int arrows = tables.itemAt(4, 15), potion = tables.itemAt(14, 1);
    check(bless >= 0 && soul >= 0 && chaos >= 0 && kris >= 0 && arrows >= 0 && potion >= 0,
          "the rows the refining tests use exist");
    if (bless < 0 || soul < 0 || chaos < 0 || kris < 0 || arrows < 0 || potion < 0) return;
    const content::ItemRow& krisRow = tables.items[size_t(kris)];

    const auto held = [](int item, int plus) { return sim::Held{int32_t(item), int16_t(plus), 1}; };
    check(sim::refinable(tables, held(bless, 0), held(kris, 5)), "a Bless goes on a +5");
    check(!sim::refinable(tables, held(bless, 0), held(kris, 6)), "and not on a +6");
    check(sim::refinable(tables, held(soul, 0), held(kris, 8)), "a Soul goes on a +8");
    check(!sim::refinable(tables, held(soul, 0), held(kris, 9)), "and not on a +9");
    check(!sim::refinable(tables, held(chaos, 0), held(kris, 0)), "the Chaos goes on nothing");
    check(!sim::refinable(tables, held(bless, 0), held(arrows, 0)), "nor a Bless on arrows");
    check(!sim::refinable(tables, held(bless, 0), held(potion, 0)), "nor on a potion");
    check(!sim::refinable(tables, held(bless, 0), held(soul, 0)), "nor on another jewel");
    check(!sim::refinable(tables, held(kris, 0), held(kris, 0)), "and a Kris is no jewel");

    sim::Realm realm;
    check(realm.raise(&tables, 11, 138, 124), "a realm raises for the jewels");
    // A Bless: always, and the thing comes back whole at its new plus.
    const int blade = realm.give(kris, -1, 4, 3);
    const int jewel = realm.give(bless);
    check(blade >= 0 && jewel >= 0, "a worn-down +4 Kris and a Bless in the bag");
    check(realm.refine(jewel, blade), "the Bless is taken");
    checkEqual(realm.satchel()[blade].refinement, 5, "and the Kris is a +5");
    checkEqual(realm.satchel()[blade].durability, sim::maximumDurability(krisRow, 5),
               "and whole again at +5");
    check(realm.satchel()[jewel].empty(), "and the jewel is spent");
    const std::vector<sim::Happening>& said = realm.happenings();
    check(!said.empty() && said.back().what == sim::What::Refined && said.back().b == 4 &&
              said.back().c == 5,
          "and the realm says it went from +4 to +5");
    const int second = realm.give(bless);
    check(realm.refine(second, blade), "a second Bless on the +5");
    const int third = realm.give(bless);
    check(!realm.refine(third, blade), "and a third refused on the +6");
    check(!realm.satchel()[third].empty(), "which keeps its jewel");
    check(!realm.refine(blade, third), "and a Kris cannot be put on a jewel");

    // A Soul: half the time, and a miss below +7 takes one off. From +2 and +5, so that what
    // comes back is never a +7 the realm would refuse to throw away again (sim::expensive).
    int tries = 0, rose = 0, fellOne = 0, others = 0;
    for (int i = 0; i < 2000; ++i) {
        const int start = i % 2 == 0 ? 2 : 5;
        const int at = realm.give(kris, -1, start);
        const int gem = realm.give(soul);
        if (at < 0 || gem < 0 || !realm.refine(gem, at)) break;
        ++tries;
        const int now = realm.satchel()[at].refinement;
        if (now == start + 1) ++rose;
        else if (now == start - 1) ++fellOne;
        else ++others;
        realm.discard(at);
    }
    checkEqual(tries, 2000, "two thousand Souls, each spent");
    checkEqual(others, 0, "each one +1 or -1");
    checkNear(double(rose) / tries, 0.5, 0.05, "and half of them rose");
    check(fellOne > 0, "and a miss was seen");
    // And from +7 a miss is a +0: a fresh realm a try, until one misses.
    int reset = -1;
    for (uint32_t seed = 100; seed < 164 && reset < 0; ++seed) {
        sim::Realm once;
        if (!once.raise(&tables, seed, 138, 124)) break;
        const int at = once.give(kris, -1, 7);
        if (!once.refine(once.give(soul), at)) break;
        const int now = once.satchel()[at].refinement;
        if (now != 8) reset = now;
    }
    checkEqual(reset, 0, "a Soul that misses on a +7 leaves a +0");

    // Worn: taken where it is, and the hand is reckoned again.
    sim::Realm armed;
    check(armed.raise(&tables, 12, 138, 124), "a realm raises for a worn refinement");
    check(armed.equip(tables.armNamed("Axe01"), -1, true), "an axe in his hand");
    const int bonusBefore = armed.hero().weaponBonus;
    const int gem = armed.give(bless);
    check(armed.refine(gem, sim::kWeaponRight), "a Bless goes on the axe in his hand");
    checkEqual(armed.satchel()[sim::kWeaponRight].refinement, 1, "which is a +1 now");
    checkEqual(armed.hero().weaponBonus, sim::damageBonus(1), "and he hits for it");
    check(armed.hero().weaponBonus > bonusBefore, "harder than before");
    check(!armed.refine(sim::kWeaponRight, sim::kWorn + 20), "a worn slot is never the jewel");

    // IsHighValueItem: a jewel and a +7 stay in the bag; a +6 and a potion may be thrown.
    check(sim::expensive(tables, held(bless, 0)) && sim::expensive(tables, held(chaos, 0)),
          "a jewel is too dear to throw away");
    check(sim::expensive(tables, held(kris, 7)), "and so is a +7");
    check(!sim::expensive(tables, held(kris, 6)), "but not a +6");
    check(!sim::expensive(tables, held(potion, 0)), "nor a potion");
    const int kept = armed.give(soul);
    checkEqual(long(armed.discard(kept)), 0, "the realm refuses to throw a Soul on the ground");
    check(!armed.satchel()[kept].empty(), "and it is still in the bag");
    const int cheap = armed.give(kris, -1, 6);
    check(armed.discard(cheap) != 0, "a +6 Kris is thrown down");
}

// Luck and the additional option: what they give worn, what they sell for, and the Soul.
void testOptions(const content::Tables& tables) {
    std::printf("options\n");
    const int soul = tables.itemAt(14, 14), kris = tables.itemAt(0, 0);
    const int shield = tables.itemAt(6, 0), arrows = tables.itemAt(4, 15);
    const int leather = tables.itemNamed("ArmorMale01");
    check(soul >= 0 && kris >= 0 && shield >= 0 && arrows >= 0 && leather >= 0,
          "the rows the option tests use exist");
    if (soul < 0 || kris < 0 || shield < 0 || arrows < 0 || leather < 0) return;
    const content::ItemRow& krisRow = tables.items[size_t(kris)];
    check(sim::takesOptions(krisRow) && !sim::takesOptions(tables.items[size_t(arrows)]),
          "a Kris takes options and arrows do not");
    checkEqual(sim::optionValue(krisRow, 3), 12, "a weapon's option at level 3 is +12");
    checkEqual(sim::optionValue(tables.items[size_t(shield)], 3), 15, "a shield's is +15");

    // The option on the axe in his hand: both ends of the band, four a level.
    sim::Realm plain, optioned;
    check(plain.raise(&tables, 21, 138, 124) && optioned.raise(&tables, 21, 138, 124),
          "two realms raise for the options");
    const int axe = tables.itemAt(1, 0);
    plain.give(axe, sim::kWeaponRight);
    optioned.give(axe, sim::kWeaponRight, 0, -1, false, 2);
    checkEqual(optioned.hero().stats.minimumDamage - plain.hero().stats.minimumDamage, 8,
               "an axe +Option at level 2 hits 8 harder at the bottom");
    checkEqual(optioned.hero().stats.maximumDamage - plain.hero().stats.maximumDamage, 8,
               "and at the top");
    // On armour it is defence, which the class halves; on a shield it is the rate.
    plain.give(leather, sim::kArmour);
    optioned.give(leather, sim::kArmour, 0, -1, false, 2);
    checkEqual(optioned.hero().wornDefense - plain.hero().wornDefense, 8,
               "leather +Option at level 2 is 8 more defence worn");
    plain.give(shield, sim::kWeaponLeft);
    optioned.give(shield, sim::kWeaponLeft, 0, -1, false, 1);
    checkEqual(optioned.hero().wornDefenseRate - plain.hero().wornDefenseRate, 5,
               "a shield +Option at level 1 is 5 more defence rate");

    // Luck: a twentieth of critical chance each, and none without it.
    check(plain.hero().stats.criticalChance == 0.0, "nothing lucky, no critical chance");
    sim::Realm lucky;
    check(lucky.raise(&tables, 22, 138, 124), "a realm raises for the luck");
    lucky.give(axe, sim::kWeaponRight, 0, -1, true);
    lucky.give(leather, sim::kArmour, 0, -1, true);
    checkNear(lucky.hero().stats.criticalChance, 0.10, 1e-9, "two lucky things worn are 10%");
    lucky.give(kris, -1, 0, -1, true);
    checkNear(lucky.hero().stats.criticalChance, 0.10, 1e-9, "and a lucky thing carried is not");

    // ItemPriceCalculator: a quarter for luck, 60% for an option at +4, 0.7 x 2 at +8.
    const int64_t base = sim::buyingPrice(krisRow, 0, 1, false);
    // Compared as ratios: the price is rounded after the options, so the sums are a few Zen out.
    checkNear(double(sim::buyingPrice(krisRow, 0, 1, false, 1, 1, true, 0)) / double(base), 1.25,
              0.03, "luck is a quarter on the price");
    checkNear(double(sim::buyingPrice(krisRow, 0, 1, false, 1, 1, false, 1)) / double(base), 1.6,
              0.03, "an option at +4 is 60% on it");
    checkNear(double(sim::buyingPrice(krisRow, 0, 1, false, 1, 1, false, 2)) / double(base), 2.4,
              0.03, "and at +8, 0.7 x 2 more");

    // The Soul on a lucky thing: three in four.
    int rose = 0, tries = 0;
    for (int i = 0; i < 2000; ++i) {
        const int at = lucky.give(kris, -1, 2, -1, true);
        const int gem = lucky.give(soul);
        if (at < 0 || gem < 0 || !lucky.refine(gem, at)) break;
        ++tries;
        if (lucky.satchel()[at].refinement == 3) ++rose;
        check(lucky.satchel()[at].luck, "the thing keeps its luck through a refinement");
        lucky.discard(at);
    }
    checkEqual(tries, 2000, "two thousand Souls on lucky things");
    checkNear(double(rose) / tries, 0.75, 0.05, "and three in four of them rose");

    // Excellent: its lines, its price, and that it will not be thrown away.
    check(sim::excellentLine(tables.items[size_t(leather)], 0).find("Zen") != std::string::npos,
          "armour's first excellent option is the Zen");
    check(sim::excellentLine(krisRow, 5).find("Excellent Damage") != std::string::npos,
          "a weapon's sixth is the excellent damage rate");
    check(sim::excellentLine(tables.items[size_t(soul)], 0).empty(), "a jewel has none");
    const int64_t one = sim::buyingPrice(krisRow, 0, 1, false, 1, 1, false, 0, 1);
    const int64_t two = sim::buyingPrice(krisRow, 0, 1, false, 1, 1, false, 0, 2);
    check(one > base * 2, "an excellent Kris is priced 25 levels deeper and doubled");
    checkNear(double(two) / double(one), 2.0, 0.03, "and a second option doubles it again");
    sim::Held fine{int32_t(kris), 0, 20};
    fine.excellent = 1;
    check(sim::expensive(tables, fine), "an excellent thing is too dear to throw away");
    check(sim::excellentCount(0x21) == 2, "two bits are two options");
}

// Being excellent, and each of its options, where the realm reckons them.
void testExcellent(const content::Tables& tables) {
    std::printf("excellent\n");
    const int axe = tables.itemAt(1, 0), leather = tables.itemNamed("ArmorMale01");
    const int shield = tables.itemAt(6, 0);
    check(axe >= 0 && leather >= 0 && shield >= 0, "the rows the excellent tests use exist");
    if (axe < 0 || leather < 0 || shield < 0) return;
    const content::ItemRow& axeRow = tables.items[size_t(axe)];
    const content::ItemRow& leatherRow = tables.items[size_t(leather)];
    const content::ItemRow& shieldRow = tables.items[size_t(shield)];
    checkEqual(sim::excellentDamage(axeRow),
               axeRow.minimumDamage * 25 / std::max(1, axeRow.dropLevel) + 5,
               "an excellent weapon's band rises by min x 25 / drop level + 5");
    check(sim::excellentDefense(leatherRow) > 0 && sim::excellentDefense(shieldRow) == 0,
          "armour's defence rises and a shield's does not");
    check(sim::excellentBlock(shieldRow) > 0, "a shield's block rate rises instead");

    // A fresh realm for each piece worn, against a plain one.
    const auto wearing = [&](uint32_t seed, int item, int slot, uint8_t bits, sim::Realm& realm) {
        check(realm.raise(&tables, seed, 138, 124), "a realm raises");
        return realm.give(item, slot, 0, -1, false, 0, bits) == slot;
    };
    {
        sim::Realm plain, fine;
        wearing(31, axe, sim::kWeaponRight, 0, plain);
        wearing(31, axe, sim::kWeaponRight, 1, fine);
        checkEqual(fine.hero().stats.minimumDamage - plain.hero().stats.minimumDamage,
                   sim::excellentDamage(axeRow), "his blow rises by exactly that at the bottom");
        checkEqual(fine.hero().stats.maximumDamage - plain.hero().stats.maximumDamage,
                   sim::excellentDamage(axeRow), "and at the top");
        check(fine.satchel()[sim::kWeaponRight].durability ==
                  plain.satchel()[sim::kWeaponRight].durability + 15,
              "an excellent axe holds fifteen more durability");
        check(sim::asks(axeRow, 0, true).strength > sim::asks(axeRow, 0).strength,
              "and asks more strength, 25 drop levels deeper");
    }
    {
        sim::Realm plain, fine;
        wearing(32, leather, sim::kArmour, 0, plain);
        wearing(32, leather, sim::kArmour, 1 << 5, fine);  // Max HP +4%
        checkEqual(fine.hero().wornDefense - plain.hero().wornDefense,
                   sim::excellentDefense(leatherRow), "excellent leather is worth its defence");
        checkEqual(fine.hero().maxHealth, int(double(plain.hero().maxHealth) * 1.04),
                   "and its sixth option is 4% more life");
    }
    {
        sim::Realm plain, fine;
        wearing(33, leather, sim::kArmour, 0, plain);
        wearing(33, leather, sim::kArmour, (1 << 4) | (1 << 1), fine);  // mana +4%, rate x1.1
        checkEqual(fine.hero().maxMana, int(double(plain.hero().maxMana) * 1.04),
                   "its fifth is 4% more mana");
        checkNear(fine.hero().stats.defenseRate, plain.hero().stats.defenseRate * 1.1f, 0.01,
                  "and its second a tenth more defence rate");
    }
    {
        sim::Realm fine;
        wearing(34, leather, sim::kArmour, (1 << 0) | (1 << 2) | (1 << 3), fine);
        checkNear(fine.hero().excel.zenRate, 1.4, 1e-9, "the first is 40% more Zen");
        checkNear(fine.hero().excel.reflect, 0.05, 1e-9, "the third reflects a twentieth");
        checkNear(fine.hero().stats.damageDecrease, 0.04, 1e-9, "the fourth takes 4% off");
    }
    {
        sim::Realm plain, fine;
        wearing(35, axe, sim::kWeaponRight, 0, plain);
        wearing(35, axe, sim::kWeaponRight, (1 << 2) | (1 << 5), fine);  // speed, excellent hit
        check(fine.hero().swingMs < plain.hero().swingMs, "a weapon's third swings faster");
        checkNear(fine.hero().stats.excellentChance, 0.1, 1e-9,
                  "and its sixth is a tenth of excellent hits");
    }

    // The excellent hit: 1.2 x the top of the band, over a critical.
    sim::Fighter attacker, defender;
    attacker.level = 10;
    attacker.attackRate = 1000.0f;
    attacker.minimumDamage = 10;
    attacker.maximumDamage = 20;
    attacker.criticalChance = 1.0;
    attacker.excellentChance = 1.0;
    sim::Random dice(7);
    const sim::Blow blow = sim::strike(attacker, defender, dice);
    check(blow.hit && blow.excellent && !blow.critical, "an excellent hit wins over a critical");
    checkEqual(blow.rolled, 24, "and is 1.2 x the top of the band");
    // And the damage decrease, after the overrate and before the floor.
    attacker.excellentChance = 0.0;
    attacker.criticalChance = 1.0;
    defender.damageDecrease = 0.5;
    const sim::Blow halved = sim::strike(attacker, defender, dice);
    checkEqual(halved.damage, 10, "armour that takes half off leaves half of a 20");
}

void testWear(const content::Tables& tables) {
    std::printf("wear\n");
    const int shield = tables.itemAt(6, 0), leather = tables.itemNamed("ArmorMale01");
    check(shield >= 0 && leather >= 0, "the Small Shield and the Leather Armour are cooked");
    if (shield < 0 || leather < 0) return;
    const content::ItemRow& small = tables.items[size_t(shield)];
    const content::ItemRow& armour = tables.items[size_t(leather)];
    checkEqual(small.durability, 22, "the Small Shield's row carries its 22");
    check(sim::wears(small) && sim::wears(armour), "and both wear");
    const int potion = tables.itemAt(14, 1);
    check(potion >= 0 && !sim::wears(tables.items[size_t(potion)]), "a potion does not");

    // AdditionalDurabilityPerLevel: +1 a level to +4, +2 to +9.
    checkEqual(sim::maximumDurability(small, 0), 22, "+0 is the row's own");
    checkEqual(sim::maximumDurability(small, 4), 26, "+4 adds four");
    checkEqual(sim::maximumDurability(small, 5), 28, "+5 adds six");
    checkEqual(sim::maximumDurability(small, 9), 36, "+9 adds fourteen");

    // CalcDurabilityPercent: counted on what is GONE, and strictly past each line.
    checkNear(sim::wearCut(11, 22), 0.0, 1e-9, "half gone costs nothing");
    checkNear(sim::wearCut(10, 22), 0.2, 1e-6, "past half gone costs a fifth");
    checkNear(sim::wearCut(6, 22), 0.3, 1e-6, "past seven tenths, three tenths");
    checkNear(sim::wearCut(4, 22), 0.5, 1e-6, "past eight tenths, a half");
    checkNear(sim::wearCut(0, 22), 1.0, 1e-9, "and broken, all of it");
    // The tint bands, counted on what is LEFT, at or under each line.
    check(sim::wornBand(12, 22) == sim::Worn::Fine, "12 of 22 is not tinted");
    check(sim::wornBand(11, 22) == sim::Worn::Half, "11 of 22 is yellow");
    check(sim::wornBand(6, 22) == sim::Worn::Third, "6 of 22 is orange");
    check(sim::wornBand(4, 22) == sim::Worn::Fifth, "4 of 22 is red-orange");
    check(sim::wornBand(0, 22) == sim::Worn::Broken, "and 0 is red");

    // The price, by hand. The Small Shield buys at 110 (drop level 3, a fifth off a shield), so
    // the base is 36, its root 6 and the root of that 2.449: 3 x 6 x 2.449 = 44.09 for the
    // whole of it, plus one.
    checkEqual(sim::repairPrice(small, 0, false, 22, true), 0, "a whole shield costs nothing");
    checkEqual(sim::repairPrice(small, 0, false, 11, true), 23, "half of it: 22.05 + 1 = 23");
    checkEqual(sim::repairPrice(small, 0, false, 0, true), 63, "broken: 45.09 x 1.4 = 63");
    checkEqual(sim::repairPrice(small, 0, false, 11, false), 57, "and by his own hand, 2.5 times");
    // Leather Armour buys at 2400, a base of 800: 3 x 28.28 x 5.318 x 0.5 + 1 = 226.6, to 220.
    checkEqual(sim::repairPrice(armour, 0, false, 17, true), 220, "half a Leather Armour, 220");
    // Selling: 0.6 of the share gone comes off.
    checkEqual(sim::wornSellingPrice(1000, 50, 100), 700, "half worn sells for seven tenths");
    checkEqual(sim::wornSellingPrice(1000, 100, 100), 1000, "and whole for the whole");

    // On his arm: a shield worn past eight tenths blocks with half its rate.
    sim::Realm realm;
    check(realm.raise(&tables, 3, 200, 160, sim::Kin::DarkKnight, 1), "a realm raises for the wear");
    const float bare = realm.hero().stats.defenseRate;
    const int whole = realm.give(shield);
    check(realm.moveItem(whole, sim::kWeaponLeft), "a whole Small Shield goes on");
    const float withWhole = realm.hero().stats.defenseRate;
    sim::Realm worn;
    worn.raise(&tables, 3, 200, 160, sim::Kin::DarkKnight, 1);
    check(worn.moveItem(worn.give(shield, -1, 0, 4), sim::kWeaponLeft), "a worn one goes on");
    checkNear(withWhole - bare, 3.0, 1e-4, "the whole one adds its 3");
    checkNear(worn.hero().stats.defenseRate - bare, 2.0, 1e-4, "the worn one 3 - (int)1.5 = 2");
    sim::Realm broken;
    broken.raise(&tables, 3, 200, 160, sim::Kin::DarkKnight, 1);
    check(broken.moveItem(broken.give(shield, -1, 0, 0), sim::kWeaponLeft), "a broken one goes on");
    checkNear(broken.hero().stats.defenseRate - bare, 0.0, 1e-4, "and adds nothing");

    // A real fight: a level 5 wizard with his points in strength, in Pad Boots and Pad Gloves
    // (22 strength asked; a new one has 18), left in the hunting ground until it kills him.
    // Every point of health it took is owed on one of the two, at 2000 a point.
    sim::Realm fight;
    check(fight.raise(&tables, 3, 200, 160, sim::Kin::DarkWizard, 5), "a realm raises for the fight");
    fight.spend(fight.hero().pointsInHand, 0, 0, 0);
    const int boots = tables.itemNamed("BootMale03"), gloves = tables.itemNamed("GloveMale03");
    check(boots >= 0 && fight.moveItem(fight.give(boots), sim::kBoots), "the Pad Boots go on");
    check(gloves >= 0 && fight.moveItem(fight.give(gloves), sim::kGloves), "the Pad Gloves go on");
    long long lost = 0;
    int before = fight.hero().health, hardest = 0;
    for (int tick = 0; tick < 6000 && fight.hero().alive(); ++tick) {
        fight.step();
        if (fight.hero().health < before) lost += before - fight.hero().health;
        before = fight.hero().health;
        for (const sim::Happening& one : fight.happenings()) {
            if (one.what == sim::What::Hit && one.whom == fight.hero().id) {
                hardest = std::max(hardest, one.a);
            }
        }
    }
    check(!fight.hero().alive() && lost > 0, "he took a beating");
    // The wound is charged whole, as OpenMU charges HealthDamage, so the killing blow's overkill
    // is owed too: at least what he lost, and less than one blow more.
    const double owed =
        (fight.wearOwed(sim::kBoots) + fight.wearOwed(sim::kGloves)) * sim::kDamagePerDurability;
    check(owed + 1e-6 >= double(lost) && owed < double(lost + hardest),
          "and the two pieces owe his lost health over 2000, the last blow's overkill with it");
    check(fight.wearOwed(sim::kBoots) > 0.0 && fight.wearOwed(sim::kGloves) > 0.0,
          "shared between them by the draw");

    // Hanzo: one piece, then everything, and nothing without his counter.
    sim::Realm shop;
    check(shop.raise(&tables, 5, 138, 124), "a realm raises in town");
    const int bagged = shop.give(shield, -1, 0, 11);
    const int alsoBagged = shop.give(leather, -1, 0, 17);
    shop.earn(1000);
    check(!shop.repair(bagged), "nothing is mended without a counter");
    int hanzo = -1;
    for (size_t i = 0; i < tables.folk.size(); ++i) {
        if (tables.folk[i].number == 251) hanzo = int(i);
    }
    sim::Request talk;
    talk.kind = sim::Request::Kind::Talk;
    talk.target = uint32_t(hanzo);
    shop.ask(talk);
    for (int tick = 0; tick < 4000 && shop.trading() < 0; ++tick) shop.step();
    check(shop.trading() == hanzo && shop.mending(), "Hanzo's counter mends");
    checkEqual(shop.repairAllCost(), 23 + 220, "the strip sums the bag: 23 and 220");
    check(shop.repair(bagged), "the shield is mended");
    checkEqual(shop.satchel()[bagged].durability, 22, "back to 22");
    checkEqual(shop.money(), 1000 - 23, "for 23 Zen");
    check(!shop.repair(bagged), "and a whole one is not mended twice");
    checkEqual(shop.repairAll(), 1, "repair all takes the armour");
    checkEqual(shop.satchel()[alsoBagged].durability, 34, "back to 34");
    checkEqual(shop.money(), 1000 - 23 - 220, "for 220 more");

    // By his own hand, away from any counter: from level 50 only, and at two and a half times.
    sim::Realm young, grown;
    young.raise(&tables, 5, 138, 124, sim::Kin::DarkKnight, 49);
    grown.raise(&tables, 5, 138, 124, sim::Kin::DarkKnight, 50);
    const int youngSlot = young.give(shield, -1, 0, 11);
    const int grownSlot = grown.give(shield, -1, 0, 11);
    young.earn(1000);
    grown.earn(1000);
    check(!young.selfMending() && !young.repair(youngSlot), "at 49 he cannot mend it himself");
    check(grown.selfMending(), "at 50 he can");
    checkEqual(grown.repairCost(grownSlot), 57, "and it costs him 57, not Hanzo's 23");
    check(grown.repair(grownSlot), "the shield is mended in the field");
    checkEqual(grown.money(), 1000 - 57, "for 57 Zen");
    checkEqual(grown.repairAll(), 0, "and there is no repair-all away from a counter");
}

void testVault(const content::Tables& tables) {
    std::printf("vault\n");
    sim::Realm realm;
    check(realm.raise(&tables, 11, 144, 112), "the realm raises by the vault");
    int baz = -1;
    for (size_t i = 0; i < tables.folk.size() && baz < 0; ++i) {
        if (tables.folk[i].number == sim::kVaultKeeper) baz = int(i);
    }
    check(baz >= 0, "Baz is in the town's table");
    if (baz < 0) return;
    const int32_t sword = tables.itemNamed("Sword01");
    check(sword >= 0, "the row the test uses exists");
    if (sword < 0) return;
    const int slot = realm.give(sword);
    const int tall = tables.items[size_t(sword)].height;
    realm.earn(500);
    check(realm.deposit(slot) < 0 && !realm.depositZen(100), "nothing crosses a shut vault");

    sim::Request talk;
    talk.kind = sim::Request::Kind::Talk;
    talk.target = uint32_t(baz);
    realm.ask(talk);
    for (int tick = 0; tick < 2000 && realm.banking() < 0; ++tick) realm.step();
    check(realm.banking() == baz, "walked to Baz and the vault opened");
    check(realm.trading() < 0, "and no counter with it");

    check(realm.deposit(sim::kWeaponRight) < 0, "what is worn does not go in");
    const int cell = realm.deposit(slot, (sim::kVaultRows - tall + 1) * sim::kVaultColumns);
    check(cell < 0 && !realm.satchel()[slot].empty(), "a sword does not start where it runs off the foot");
    const int kept = realm.deposit(slot, 10);
    checkEqual(kept, 10, "it goes in where it was let go");
    check(realm.satchel()[slot].empty() && realm.vault()[kept].item == sword,
          "and leaves the bag for the vault");
    check(!realm.rearrange(kept, (sim::kVaultRows - tall + 1) * sim::kVaultColumns),
          "a move that would run off the vault's foot is refused");
    check(realm.rearrange(kept, 0), "a move inside the vault is taken");
    const int back = realm.withdraw(0);
    check(back >= sim::kWorn && realm.satchel()[back].item == sword && realm.vault()[0].empty(),
          "and it comes back to the bag");

    check(!realm.depositZen(501), "more Zen than he carries is refused");
    check(realm.depositZen(500) && realm.money() == 0 && realm.vault().zen() == 500,
          "what he carries goes in");
    check(!realm.withdrawZen(501), "more than is kept is refused");
    check(realm.withdrawZen(200) && realm.money() == 200 && realm.vault().zen() == 300,
          "and part of it comes out");

    // Stacks cross the counter as they do the bag: onto their kind first, the rest behind.
    const int32_t potion = tables.itemAt(14, 1);
    const int five = realm.give(potion, -1, 0, 5);
    const int stored = realm.deposit(five);
    check(stored >= 0 && realm.satchel()[five].empty(), "five potions go into the vault");
    check(realm.deposit(realm.give(potion, -1, 0, 18)) == stored &&
              realm.vault()[stored].durability == sim::kStackMost,
          "eighteen more fill that stack to twenty");
    const int spill = realm.give(potion, -1, 0, 7);
    check(realm.withdraw(stored, spill) == spill && realm.satchel()[spill].durability == 20 &&
              realm.vault()[stored].durability == 7,
          "and a stack let go on a bag stack tops it up and leaves the rest");

    sim::Request walk;
    walk.kind = sim::Request::Kind::WalkTo;
    walk.column = 140;
    walk.row = 120;
    realm.ask(walk);
    realm.step();
    check(realm.banking() < 0, "walking off shuts it");
    check(!realm.withdrawZen(100) && realm.vault().zen() == 300, "and what is kept stays kept");
}

// Health comes back in a safe zone alone: a hundredth of the pool every three seconds on a safe
// tile, nothing on the grass outside it. Realm::recover.
// The town's guards (sim/realm_watch.cpp): a level-one knight hunting just outside the east gate,
// too weak to finish much alone. The guard there goes for what the hunt wakes, says so, lands
// his blows, points the knight on after a kill they shared, never leaves his leash, is never struck
// by the knight, and is back at his post once it is over.
void testWardens(const content::Tables& tables) {
    std::printf("wardens\n");
    sim::Realm realm;
    check(realm.raise(&tables, 11, 178, 122), "a level-one knight raises at the east gate");
    int guards = 0;
    for (const sim::Body& one : realm.bodies()) guards += one.warden >= 0 ? 1 : 0;
    checkEqual(guards, 6, "Lorencia stands its six guards");

    int challenged = 0, guardBlows = 0, thanked = 0, pointed = 0, struckGuard = 0;
    bool leashed = true, standing = true;
    uint32_t fighting = 0;
    for (int tick = 0; tick < 6000; ++tick) {
        const sim::Body& hero = realm.hero();
        // For the first two minutes he fights what is near; then he walks back into town and
        // leaves the guards to it.
        if (tick < 2400 && hero.alive()) {
            uint32_t nearest = 0;
            float closest = 1e30f;
            for (const sim::Body& one : realm.bodies()) {
                if (!one.monster() || !one.alive()) continue;
                // Only what comes within the east guard's reach of his post at 173,125: the
                // fight the feature is for is one at the gate, not one up the road.
                if (std::max(std::fabs(one.x - 173.0f), std::fabs(one.y - 125.0f)) > 6.0f) continue;
                const float off = std::max(std::fabs(one.x - hero.x), std::fabs(one.y - hero.y));
                if (off <= 10.0f && off < closest) {
                    closest = off;
                    nearest = one.id;
                }
            }
            if (nearest != 0 && nearest != fighting) {
                fighting = nearest;
                sim::Request request;
                request.kind = sim::Request::Kind::Attack;
                request.target = nearest;
                realm.ask(request);
            }
        } else if (tick == 2400) {
            sim::Request request;
            request.kind = sim::Request::Kind::WalkTo;
            request.column = 150;
            request.row = 125;
            realm.ask(request);
        }
        realm.step();
        for (const sim::Happening& one : realm.happenings()) {
            const sim::Body* who = realm.find(one.who);
            const sim::Body* whom = realm.find(one.whom);
            if (one.what == sim::What::Shouted) {
                if (one.a == int32_t(sim::Shout::Challenge)) ++challenged;
                if (one.a == int32_t(sim::Shout::Pointing)) {
                    ++thanked;
                    if (one.b >= 0) ++pointed;
                }
            }
            if (one.what == sim::What::Hit && who && who->warden >= 0) ++guardBlows;
            if ((one.what == sim::What::Hit || one.what == sim::What::Missed) && who &&
                who->player && whom && whom->warden >= 0) {
                ++struckGuard;
            }
        }
        for (const sim::Body& one : realm.bodies()) {
            if (one.warden < 0) continue;
            standing &= one.alive();
            leashed &= std::max(std::abs(one.column() - one.homeColumn),
                                std::abs(one.row() - one.homeRow)) <= 8 + 2;
        }
    }
    std::printf("  %d challenges, %d guard blows landed, %d kills shared (%d pointing on)\n", challenged,
                guardBlows, thanked, pointed);
    check(challenged > 0, "a guard challenges a monster the hunt woke");
    check(guardBlows > 0, "and his blows land");
    check(thanked > 0, "and a kill they shared is said aloud");
    check(pointed > 0, "and points him on toward more of them");
    check(struckGuard == 0, "the knight never strikes a guard");
    check(standing, "no guard falls");
    check(leashed, "and none follows a monster past his leash");
    bool home = true;
    for (const sim::Body& one : realm.bodies()) {
        if (one.warden < 0 || one.quarry != 0) continue;
        home &= one.column() == one.homeColumn && one.row() == one.homeRow && !one.walking;
    }
    check(home, "and every guard with nothing to fight is back at his post");
}

void testRecovery(const content::Tables& tables) {
    std::printf("recovery\n");
    const auto hurt = [&](int column, int row, const char* raised) {
        sim::Realm realm;
        check(realm.raise(&tables, 13, column, row), raised);
        sim::HeroRecord saved = realm.record();
        saved.health = realm.hero().maxHealth / 2;
        realm.restore(saved);
        const int before = realm.hero().health;
        for (int tick = 0; tick < 61; ++tick) realm.step();
        return std::pair{realm.hero().health - before,
                         tables.grid.safe(realm.hero().column(), realm.hero().row())};
    };
    const auto [inTown, safe] = hurt(138, 124, "a realm raises in the town");
    check(safe, "he stands on a safe tile");
    check(inTown > 0, "and his health comes back there");
    const auto [outside, unsafe] = hurt(170, 66, "a realm raises on the grass east of town");
    check(!unsafe, "which is not safe");
    check(outside <= 0, "and nothing comes back there");
}

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
    testSpamClicks(tables);
    testDeterminism(tables);
    testInvariants(tables);
    testItems(tables);
    testLoot(tables);
    testStandsOverTheKill(tables);
    testSkills(tables);
    testCastLock(tables);
    testPerches(tables);
    testVault(tables);
    testRefine(tables);
    testOptions(tables);
    testExcellent(tables);
    testWear(tables);
    testRecovery(tables);
    testWardens(tables);

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
