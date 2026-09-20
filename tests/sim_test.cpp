// Sprint 5's tests: the rules against OpenMU's own numbers, the router against Lorencia's own
// grid, and two realms raised on one seed that must live the same life.
//
// This is what replaces Instrument, Audit and the bot. It needs no window, no device and no
// .glb -- only the cooked tables -- which is foundation 9 of PLAN.md and is the whole reason
// the sim was built before the thing that draws it.
//
//     cmake --build build --target sim_test && build/sim_test

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "content/tables.h"
#include "sim/audit.h"
#include "sim/random.h"
#include "sim/realm.h"
#include "sim/route.h"
#include "sim/rules.h"

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
    sim::reckon(sim::Kin::DarkKnight, 1, sim::startingPoints(sim::Kin::DarkKnight), &fighter,
                &health);
    checkEqual(fighter.attackRate, int(5 + 20 * 1.5 + 28 * 0.25), "the knight's attack rate");
    checkEqual(fighter.defenseRate, 6, "the knight's defense rate is agility/3");
    checkEqual(fighter.defense, 3, "the knight's defence is halved");
    checkEqual(fighter.minimumDamage, 4, "the knight's fists, low");
    checkEqual(fighter.maximumDamage, 7, "the knight's fists, high");
    checkEqual(health, 35 + 2 + 75, "the knight's health");

    sim::reckon(sim::Kin::DarkWizard, 1, sim::startingPoints(sim::Kin::DarkWizard), &fighter,
                &health);
    checkEqual(health, 30 + 1 + 30, "the wizard's health");
    checkEqual(fighter.minimumDamage, 18 / 8, "the wizard's fists, low");

    sim::reckon(sim::Kin::FairyElf, 1, sim::startingPoints(sim::Kin::FairyElf), &fighter,
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
    testRandom();
    testRouter(tables);
    testDeterminism(tables);
    testInvariants(tables);

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
