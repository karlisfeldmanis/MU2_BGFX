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
#include "sim/realm_tuning.h"
#include "sim/realm.h"
#include "sim/route.h"
#include "sim/rules.h"
#include "sim/skills.h"
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
        if (one.player || !one.alive()) continue;
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
    check(sim::force(cyclone, sim::HeroPoints{2000, 0, 0, 0}) >
              sim::force(cyclone, sim::HeroPoints{28, 0, 0, 0}),
          "strength is force");

    sim::Realm realm;
    check(realm.raise(&tables, 7, 190, 110, sim::Kin::DarkKnight, 60), "a realm raises for the keys");
    // A blade in his hand, because nothing is thrown bare-handed and `raise` dresses nobody:
    // `given`, so a level-60 knight's strength is not what this is testing.
    check(realm.equip(tables.armNamed("Sword03"), -1, true), "and a blade is put in his hand");
    // Every row is built since 2026-09-23, so a knight is raised holding all six: the bar's
    // four keys are filled from the list and `built` stopped meaning "and a key is free".
    bool all = true;
    for (int i = 0; i < sim::skillCount(); ++i) all &= realm.knows(sim::skillAt(i).number);
    check(all, "a knight of 60 has met every skill");

    // ---- the ladder (docs/skills-dk.md §3.3) -------------------------------------------------
    //
    // A skill is met at the level its orb asks for, which is 0.75's own ladder of carriers, so a
    // young knight has an empty bar and fills it as he levels. Checked at three heights, and the
    // last one on a level-up rather than on a raise: the two routes must agree.
    {
        sim::Realm young;
        check(young.raise(&tables, 3, 190, 110, sim::Kin::DarkKnight, 1),
              "a knight of the first level raises");
        int met = 0;
        for (int i = 0; i < sim::skillCount(); ++i) met += young.knows(sim::skillAt(i).number);
        checkEqual(met, 0, "and knows nothing at all");

        sim::Realm middling;
        check(middling.raise(&tables, 3, 190, 110, sim::Kin::DarkKnight, 20),
              "a knight of twenty raises");
        check(middling.knows(sim::skill::kDefense), "and has met the guard at 6");
        check(middling.knows(sim::skill::kUppercut), "the rising blow at 12");
        check(middling.knows(sim::skill::kFallingSlash), "the overhead at 13");
        check(middling.knows(sim::skill::kLunge), "and the jab at 20, the level he is");
        check(!middling.knows(sim::skill::kCyclone), "but not the spin, which waits for 36");
        check(!middling.knows(sim::skill::kSlash), "nor the sweep, which waits for 52");
        check(!middling.knows(sim::skill::kDeathStab), "nor the spear's stab at 60");

        // And the third door: a save is restored onto a realm raised at level 1, so the ladder
        // has to be walked again when the level arrives -- otherwise a knight of 40 comes back
        // from disk with a beginner's bar until his next level.
        sim::Realm loaded;
        check(loaded.raise(&tables, 3, 190, 110, sim::Kin::DarkKnight, 1), "a realm for a save");
        sim::HeroRecord saved = loaded.record();
        saved.level = 40;
        saved.learned = 0;
        loaded.restore(saved);
        check(loaded.knows(sim::skill::kCyclone), "a restored knight of 40 has the spin");
        check(loaded.knows(sim::skill::kTwistingSlash), "and the whirl he met at 28");
        check(!loaded.knows(sim::skill::kSlash), "and not the sweep he has not reached");
    }
    check(!realm.learn(sim::skill::kSlash), "and learning one twice is refused");

    // ---- the families, before the hunt (docs/skills-dk.md §3.1b) -----------------------------
    //
    // The gate itself, asked of the table rather than of a fight: every attack names the hands
    // that may throw it, no attack is thrown bare-handed or off a bow, and the guard asks for a
    // shield. This is what keeps a row added later from quietly being throwable with anything.
    {
        bool gated = true, everyFamilyHasThree = true;
        for (int i = 0; i < sim::skillCount(); ++i) {
            const sim::SkillRow& row = sim::skillAt(i);
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
                if (one.player || !one.alive()) continue;
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

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
