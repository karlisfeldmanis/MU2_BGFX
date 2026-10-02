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
#include "sim/event.h"
#include "sim/items.h"
#include "sim/random.h"
#include "sim/realm_tuning.h"
#include "sim/realm.h"
#include "sim/route.h"
#include "sim/rules.h"
#include "sim/skills.h"
#include "sim/swings.h"
#include "sim/traps.h"
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

    // The top of the band is a roll: min + rand()%(max-min+1) (ObjAttack.cpp:3117, 1.00.93).
    attacker.attackRate = 1000;
    attacker.minimumDamage = 4;
    attacker.maximumDamage = 7;
    defender.defenseRate = 0;
    defender.defense = 0;
    int lowest = 1 << 30, highest = 0;
    for (int i = 0; i < 2000; ++i) {
        blow = sim::strike(attacker, defender, dice);
        if (!blow.hit) continue;
        lowest = std::min(lowest, blow.rolled);
        highest = std::max(highest, blow.rolled);
    }
    checkEqual(lowest, 4, "a 4-7 swing rolls its 4");
    checkEqual(highest, 7, "and its 7");
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
    checkEqual((long long)findings.inTown, 0, "no monster walked into the safe zone");
    checkEqual((long long)findings.unpaidLevel, 0, "no level was won unpaid");
    for (const std::string& line : findings.first) std::printf("    %s\n", line.c_str());
}

// Sprint 7: the items. The requirement formula against the worked numbers MU2's Rows.cs gives
// for it, the footprint walk, and equipping as a move through the same gate a window colours by.
void testItems(const content::Tables& tables) {
    std::printf("items\n");
    // 157: the catalogue's 118, the nine knight orbs added on 2026-09-23, the wizard's Scroll
    // of Soul Barrier on 2026-09-28, and the elf's Greater Defense, Greater Damage and Skillshot
    // orbs the same day (sprint 15), and the 25 Noria's shelves were missing: the Silk, Wind,
    // Spirit and Guardian sets, the Elven, Battle and Tiger Bows, the Golden Crossbow and the
    // Elven Shield, and the Rune of Creation (14, 22) on 2026-09-28. A count rather than a list, because what it is guarding is the cook -- a
    // recipe that stops being picked up is a row the shelf silently cannot sell.
    checkEqual(long(tables.items.size()), 157, "157 item rows cooked");
    // And Noria's three shops sell only what is cooked: Elf Lala, Eo the Craftsman and Potion
    // Girl Amy, every offer a row (the user, 2026-09-28: "fill Noria's vendors").
    // And Lumen's, whose Guardian Angel and Imp are ours (the user, 2026-09-30: "put imp and
    // guardian angel in lorencia tavern vendor").
    for (const int npc : {242, 243, 253, 255}) {
        int stocked = 0;
        const sim::Offer* offers = sim::stockOf(npc, &stocked);
        int unmade = 0;
        for (int i = 0; i < stocked; ++i) {
            if (tables.itemAt(offers[i].group, offers[i].number) < 0) ++unmade;
        }
        checkEqual((long long)unmade, 0LL, "every offer on a Noria shelf and Lumen's is a cooked item");
    }
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
    // A knight's one-handed axe goes in either hand (sim::offHanded), alone in the left it is
    // still what he swings.
    check(realm.moveItem(sim::kWorn + 3, sim::kWeaponLeft), "a knight's axe goes in the left hand");
    checkEqual(realm.hero().stats.minimumDamage, minimumArmed, "and swings from there");
    check(!realm.hero().dual, "one weapon is not two");
    check(realm.moveItem(sim::kWeaponLeft, sim::kWorn + 3), "and comes off again");
    check(realm.moveItem(sim::kWorn + 3, sim::kWeaponRight), "and back on, since 28 >= 21");
    {
        // Two axes: WebZen's bTwoHandWeapon, strength's band twice with a weapon on each.
        sim::Realm duo;
        check(duo.raise(&tables, 7, 138, 124), "a realm raises for two weapons");
        const int32_t axeArm = tables.armNamed("Axe01"), swordArm = tables.armNamed("Sword01");
        check(duo.equip(axeArm, -1, true), "one axe");
        const int single = duo.hero().stats.minimumDamage;
        check(duo.equip(axeArm, axeArm, true), "two axes, the second in the shield's place");
        check(duo.hero().dual, "and he holds two");
        checkEqual(duo.satchel()[sim::kWeaponLeft].item, axe, "the second in the left hand");
        checkEqual(duo.hero().stats.offhandMinimumDamage, single,
                   "a matched pair: the left band is the right's again");
        check(duo.moveItem(sim::kWeaponLeft, sim::kWorn + 30), "the second comes off");
        check(!duo.hero().dual && duo.hero().stats.offhandMaximumDamage == 0,
              "and the left band is gone");
        check(duo.equip(axeArm, swordArm, true), "an axe and a sword");
        checkEqual(duo.hero().stats.minimumDamage, int(double(single) * sim::kMixedPair),
                   "a mixed pair: each hand at 55%");
        check(!sim::offHanded(tables.items[size_t(axe)], sim::Kin::DarkWizard),
              "only a knight holds a second weapon");
        check(!sim::offHanded(tables.items[size_t(staff)], sim::Kin::DarkKnight),
              "and never a staff");
    }
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
            // Thirty tiles out, as nearestTo: what dies rises elsewhere in its nest.
            float best = 30.0f * 30.0f;
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
// Every skill is walked out of but Teleport and the auras (the user, 2026-10-02: "any spell ahs
// to be cancelable but without sliding bug when animation is played and char moves", then "dont
// allow to cancel any aura casts"), which ended the rule of 2026-09-23 that a skill plays to the
// end of its clip. The hold stays -- nothing but the click moves him under a cast -- and the
// click breaks it: he walks on the tick, and a blow not yet landed is dropped as a swing's is.
//
// Three halves: a self-cast run with nothing to fight, whose click is dropped; a
// spin thrown at a monster, for the blow the click drops; and Lightning, a channel, for the
// pulses it stops.
void testCastLock(const content::Tables& tables) {
    std::printf("a skill is walked out of\n");
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
    check(realm.casting() && realm.held(), "and its clip holds him, a hold no click breaks");

    // Left alone for a few ticks, the hold keeps him where he stands.
    const int stoodColumn = realm.hero().column(), stoodRow = realm.hero().row();
    for (int tick = 0; tick < 3; ++tick) realm.step();
    check(realm.casting() && !realm.hero().walking && realm.hero().column() == stoodColumn &&
              realm.hero().row() == stoodRow,
          "he stands under the clip while nothing tells him otherwise");

    // The click, mid-clip, at a tile ten away.
    sim::Request walk;
    walk.kind = sim::Request::Kind::WalkTo;
    walk.column = stoodColumn + 10;
    walk.row = stoodRow;
    realm.ask(walk);
    realm.step();
    check(realm.casting() && !realm.hero().walking && realm.hero().column() == stoodColumn,
          "and a click inside an aura is dropped: he stands until the clip ends");
    for (int tick = 0; tick < 200 && realm.casting(); ++tick) realm.step();
    check(!realm.casting() && !realm.hero().walking,
          "and does not set off on the dropped click when it does");
    check(realm.hero().boonUntil > realm.tick(), "with the guard he raised on him");

    // ---- and the blow the click drops ---------------------------------------------------------
    //
    // `accept` drops the unlanded blow of whatever order it replaces, a skill's as a swing's.
    // Cyclone is thrown at a monster and settles half a clip in, which is after the click below.
    sim::Realm fight;
    check(fight.raise(&tables, 7, 190, 110, sim::Kin::DarkKnight, 60), "a realm raises for the fight");
    check(fight.equip(tables.armNamed("Sword03"), -1, true), "with a one-handed sword, Cyclone's");
    check(fight.learn(sim::skill::kCyclone), "and the spin in his head");
    bool cast = false, landed = false, walkedOff = false;
    int64_t castTick = -1;
    uint32_t fighting = 0;
    for (int tick = 0; tick < 6000 && (castTick < 0 || fight.tick() < castTick + 40); ++tick) {
        const sim::Body& hero = fight.hero();
        if (hero.alive() && !cast) {
            uint32_t nearest = 0;
            float closest = 1e30f;
            for (const sim::Body& one : fight.bodies()) {
                if (!one.monster() || !one.alive()) continue;
                const float off = std::max(std::fabs(one.x - hero.x), std::fabs(one.y - hero.y));
                if (off <= 30.0f && off < closest) {
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
        if (cast && castTick == fight.tick() - 1) walkedOff = fight.hero().walking;
        for (const sim::Happening& one : fight.happenings()) {
            if (one.what == sim::What::Cast && one.who == fight.hero().id && !cast) {
                cast = true;
                castTick = fight.tick();
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
    check(walkedOff, "and the click walks him out of it on the next tick");
    check(!landed, "and its blow, not landed yet, is dropped with it");

    // ---- and a channel: Lightning stops pulsing ----------------------------------------------
    sim::Realm wiz;
    check(wiz.raise(&tables, 7, 190, 110, sim::Kin::DarkWizard, 12), "a wizard of twelve raises");
    check(wiz.learn(sim::skill::kLightning), "who knows Lightning");
    bool channelled = false, ended = false;
    uint32_t quarry = 0;
    for (int tick = 0; tick < 6000 && !channelled; ++tick) {
        const sim::Body& hero = wiz.hero();
        uint32_t nearest = 0;
        float closest = 1e30f;
        for (const sim::Body& one : wiz.bodies()) {
            if (!one.monster() || !one.alive()) continue;
            const float off = std::max(std::fabs(one.x - hero.x), std::fabs(one.y - hero.y));
            if (off <= 30.0f && off < closest) {
                closest = off;
                nearest = one.id;
            }
        }
        if (nearest != 0 && nearest != quarry) {
            quarry = nearest;
            sim::Request request;
            request.kind = sim::Request::Kind::Attack;
            request.target = nearest;
            wiz.ask(request);
        }
        if (nearest != 0 && wiz.cooling(sim::skill::kLightning) == 0) {
            wiz.invoke(sim::skill::kLightning, nearest);
        }
        wiz.step();
        channelled = wiz.hero().channelSkill == sim::skill::kLightning;
    }
    check(channelled, "Lightning is channelled round him");
    if (channelled) {
        sim::Request away;
        away.kind = sim::Request::Kind::WalkTo;
        away.column = wiz.hero().column() + 8;
        away.row = wiz.hero().row();
        wiz.ask(away);
        wiz.step();
        ended = wiz.hero().channelSkill == 0 && !wiz.casting() && wiz.hero().walking;
    }
    check(ended, "and a click ends the channel and walks him");

    // ---- the quick slot: a wizard's right button (the user, 2026-09-28) ---------------------
    //
    // An Attack carrying Energy Ball: he stops at six tiles and throws, the bolt is let go at the
    // bottom of the clip and lands when it has crossed the gap, and a hit pays mana back. Then
    // the same order with his mana gone: he closes to arm's length and swings the staff.
    // Thirty tiles, not twelve: a monster rises on a tile drawn anew from its nest (WebZen's
    // respawn, 2026-09-30), so a hunt that waited on the spot it cleared would stand idle.
    const auto nearestTo = [](const sim::Realm& realm) {
        const sim::Body& hero = realm.hero();
        uint32_t nearest = 0;
        float closest = 1e30f;
        for (const sim::Body& one : realm.bodies()) {
            if (!one.monster() || !one.alive()) continue;
            const float off = std::max(std::fabs(one.x - hero.x), std::fabs(one.y - hero.y));
            if (off <= 30.0f && off < closest) {
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
            const int levelBefore = wiz.hero().level;
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
                    // A kill that levels him refills him, which is the level and not the spell.
                    if (wiz.tick() % 60 != 0 && wiz.hero().level == levelBefore &&
                        wiz.hero().mana > manaBefore) {
                        ++paidBack;
                    }
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
        // Eighteen: Power Wave's level since the ladder of 2026-10-02.
        check(reader.raise(&tables, 7, 190, 110, sim::Kin::DarkWizard, 18), "a wizard raises");
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

    // ---- Lorencia's spiders bite and never poison, and the Antidote is kept ------------------
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
        int bites = 0, poisoned = 0;
        for (int tick = 0; tick < 1500 && bit.hero().alive(); ++tick) {
            bit.step();
            if (bit.hero().poisonUntil != 0) ++poisoned;
            for (const sim::Happening& one : bit.happenings()) {
                if (one.what != sim::What::Hit || one.whom != bit.hero().id) continue;
                if (one.poisoned) ++poisoned;
                else ++bites;
            }
            if (bites >= 5) break;
        }
        std::printf("  spiders: %d bites, %d poisoned\n", bites, poisoned);
        check(bites >= 1, "a spider bites him");
        checkEqual(poisoned, 0, "and never poisons him -- nothing in Lorencia does");

        sim::HeroRecord carrying = bit.record();
        const int bagged = sim::kWorn + 40;
        carrying.slots[bagged].item = antidote;
        carrying.slots[bagged].durability = 1;
        bit.restore(carrying);
        check(!bit.useItem(bagged), "with no poison on him the Antidote is kept");
    }

    // ---- Poison: a cooldown burst that goes on hurting ---------------------------------------
    {
        const sim::SkillRow& poison = *sim::skillNumbered(sim::skill::kPoison);
        check(poison.wizardry && poison.primary() && poison.force == 1.5f && poison.damage == 12 &&
                  poison.mana == 42 && poison.poisonTicks == 400 && poison.splash == 4.0f,
              "Poison is a standard spell of twelve damage at half again the band and forty-two mana, "
              "poisoning twenty seconds within four tiles");
        const int32_t scroll = tables.itemAt(15, 0);
        check(scroll >= 0 && tables.items[size_t(scroll)].teaches == sim::skill::kPoison &&
                  tables.items[size_t(scroll)].teachesEnergy == 140,
              "the Scroll of Poison teaches skill 1 at a hundred and forty energy");

        // Seed 5, not 7: on 7, under WebZen's round sight (realm_tuning.h, apart), no cast
        // caught two bodies at once.
        sim::Realm wiz;
        check(wiz.raise(&tables, 5, 190, 110, sim::Kin::DarkWizard, 30), "a wizard raises to hunt");
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
        check(closest >= 10, "paced by its own clip, with no cooldown");
        checkEqual(pulseKills, 0, "a pulse never kills");
        std::printf("  poison: %d pulses off the three-second beat (a recast restarts it)\n", offBeat);
    }

    // ---- Flame: a fire on the ground that strikes whoever is in it, twice -------------------
    {
        const sim::SkillRow& flame = *sim::skillNumbered(sim::skill::kFlame);
        check(flame.wizardry && flame.primary() && flame.damage == 25 && flame.mana == 50 &&
                  flame.burns == 2 && flame.burnTiles == 1.5f && flame.force == 1.0f,
              "Flame is a no-cooldown spell of twenty-five damage and fifty mana, "
              "striking twice within a tile and a half");
        // Each of the wizard's late rows by the next one's, so a row added on the end moves none.
        check(sim::skillIndexOf(sim::skill::kFlame) + 1 == sim::skillIndexOf(sim::skill::kEvilSpirit),
              "and its row is past the elf's, so no save's learned bit moves");
        const sim::SkillRow& spirit = *sim::skillNumbered(sim::skill::kEvilSpirit);
        check(spirit.wizardry && spirit.primary() && spirit.damage == 45 && spirit.mana == 90 &&
                  spirit.built && spirit.kin == sim::Kin::DarkWizard,
              "Evil Spirit is a no-cooldown spell of forty-five damage and ninety mana");
        check(sim::skillIndexOf(sim::skill::kEvilSpirit) + 1 ==
                  sim::skillIndexOf(sim::skill::kHellfire),
              "and its row is past Flame's");
        const int32_t evilScroll = tables.itemAt(15, 8);
        check(evilScroll >= 0 && tables.items[size_t(evilScroll)].teaches == sim::skill::kEvilSpirit &&
                  tables.items[size_t(evilScroll)].teachesEnergy == 220,
              "the Scroll of Evil Spirit teaches skill 9 at two hundred and twenty energy");
        const int32_t scroll = tables.itemAt(15, 4);
        check(scroll >= 0 && tables.items[size_t(scroll)].teaches == sim::skill::kFlame &&
                  tables.items[size_t(scroll)].teachesEnergy == 160,
              "the Scroll of Flame teaches skill 5 at a hundred and sixty energy");

        // Hellfire: the ground round him on fire, every monster within four tiles struck once
        // with his wizardry band (WebZen's SkillHellFire), said once a cast for the circle.
        const sim::SkillRow& hell = *sim::skillNumbered(sim::skill::kHellfire);
        check(hell.wizardry && hell.primary() && hell.damage == 120 && hell.mana == 160 &&
                  hell.spread == sim::Spread::Ring && hell.reach == 4.0f && hell.clip == 154 &&
                  hell.kin == sim::Kin::DarkWizard,
              "Hellfire is a no-cooldown ring of a hundred and twenty damage and 160 mana");
        check(sim::skillIndexOf(sim::skill::kHellfire) + 1 == sim::skillIndexOf(sim::skill::kTwister),
              "and its row is past Evil Spirit's");
        const int32_t hellScroll = tables.itemAt(15, 9);
        check(hellScroll >= 0 && tables.items[size_t(hellScroll)].teaches == sim::skill::kHellfire &&
                  tables.items[size_t(hellScroll)].teachesEnergy == 260,
              "the Scroll of Hellfire teaches skill 10 at two hundred and sixty energy");
        {
            sim::Realm burner;
            check(burner.raise(&tables, 7, 190, 110, sim::Kin::DarkWizard, 100),
                  "a wizard raises to burn");
            check(burner.learn(sim::skill::kHellfire), "who knows Hellfire");
            int circles = 0, struck = 0, outside = 0, mostOnOne = 0, offLanding = 0;
            int64_t swungAt = -1;
            // The landing: a third of the clip, key 6 of 18, where his hips come down.
            const int32_t clip = sim::castTicks(tables, sim::Kin::DarkWizard,
                                                burner.hero().points.agility, nullptr, nullptr, hell);
            const int64_t landing = std::lround(float(clip) * 6.0f / 18.0f);
            uint32_t fighting = 0;
            for (int tick = 0; tick < 4000 && burner.hero().alive(); ++tick) {
                const uint32_t nearest = nearestTo(burner);
                if (nearest != 0 && nearest != fighting) {
                    fighting = nearest;
                    sim::Request request;
                    request.kind = sim::Request::Kind::Attack;
                    request.target = nearest;
                    request.skill = sim::skill::kHellfire;
                    burner.ask(request);
                }
                burner.step();
                int onThis = 0;
                for (const sim::Happening& one : burner.happenings()) {
                    if (one.who != burner.hero().id) continue;
                    if (one.what == sim::What::Swung && one.a == sim::skill::kHellfire) {
                        swungAt = one.tick;
                    }
                    if (one.what == sim::What::Loosed && one.a == sim::skill::kHellfire) {
                        ++circles;
                        if (swungAt < 0 || int64_t(one.tick) - swungAt != landing) ++offLanding;
                    }
                    if (one.what != sim::What::Hit && one.what != sim::What::Missed) continue;
                    const sim::Body* at = burner.find(one.whom);
                    if (at == nullptr) continue;
                    ++struck;
                    ++onThis;
                    const float dx = at->x - burner.hero().x, dy = at->y - burner.hero().y;
                    if (std::max(std::fabs(dx), std::fabs(dy)) > 4.0f) ++outside;
                }
                mostOnOne = std::max(mostOnOne, onThis);
            }
            std::printf("  hellfire: %d circles, %d blows, most on one tick %d\n", circles, struck,
                        mostOnOne);
            check(circles > 5, "he casts it");
            check(struck >= circles, "and every circle strikes");
            checkEqual(outside, 0, "only within four tiles of him");
            std::printf("  hellfire: a %d-tick clip, let go %lld in\n", clip, (long long)landing);
            checkEqual(offLanding, 0, "and the circle is his landing, a third of the clip in");
        }

        // Twister: a storm stood at his feet and walked out along where he aimed, an eighth of a
        // tile a tick, striking everything within a tile and a half of it 11, 23 and 35 ticks
        // after the let-go -- MU's client, which WebZen takes as sent.
        const sim::SkillRow& twister = *sim::skillNumbered(sim::skill::kTwister);
        check(twister.wizardry && twister.primary() && twister.damage == 35 && twister.mana == 60 &&
                  twister.reach == 6.0f && twister.burns == 3 && twister.burnTiles == 1.5f &&
                  twister.walks == 0.125f && twister.clip == 147 && twister.clipOther == 148 &&
                  twister.kin == sim::Kin::DarkWizard,
              "Twister is a no-cooldown storm of thirty-five damage and sixty mana, striking "
              "three times");
        check(sim::skillElement(sim::skill::kTwister) == sim::Element::Wind, "and it is wind");
        check(sim::skillIndexOf(sim::skill::kTwister) + 1 == sim::skillIndexOf(sim::skill::kInferno),
              "and its row is past Hellfire's, the learned mask's thirty-second bit");
        const int32_t twisterScroll = tables.itemAt(15, 7);
        check(twisterScroll >= 0 &&
                  tables.items[size_t(twisterScroll)].teaches == sim::skill::kTwister &&
                  tables.items[size_t(twisterScroll)].teachesEnergy == 180,
              "the Scroll of Twister teaches skill 8 at a hundred and eighty energy");
        {
            sim::Realm blower;
            check(blower.raise(&tables, 7, 190, 110, sim::Kin::DarkWizard, 100),
                  "a wizard raises to blow");
            check(blower.learn(sim::skill::kTwister), "who knows Twister");
            struct Storm {
                int64_t at;
                float x, y, dx, dy;
            };
            Storm storms[64] = {};
            int casts = 0, struck = 0, offPath = 0, offBeat = 0, thrice = 0, swung = 0;
            uint32_t fighting = 0;
            for (int tick = 0; tick < 4000 && blower.hero().alive(); ++tick) {
                const uint32_t nearest = nearestTo(blower);
                if (nearest != 0 && nearest != fighting) {
                    fighting = nearest;
                    sim::Request request;
                    request.kind = sim::Request::Kind::Attack;
                    request.target = nearest;
                    request.skill = sim::skill::kTwister;
                    blower.ask(request);
                }
                blower.step();
                for (const sim::Happening& one : blower.happenings()) {
                    if (one.who != blower.hero().id) continue;
                    if (one.what == sim::What::Loosed && one.a == sim::skill::kTwister) {
                        const float way = float(one.c) / 1000.0f;
                        storms[casts % 64] =
                            Storm{int64_t(one.tick), one.x, one.y, std::cos(way), std::sin(way)};
                        ++casts;
                    }
                    if (one.what != sim::What::Hit && one.what != sim::What::Missed) continue;
                    // The storm's strikes are said as flown; a blow of his staff, once his mana
                    // is out, is not.
                    if (!one.thrown) {
                        ++swung;
                        continue;
                    }
                    const sim::Body* at = blower.find(one.whom);
                    if (at == nullptr) continue;
                    ++struck;
                    // Which storm, by its beat: one of them must be 11, 23 or 35 ticks old and
                    // stand within a tile and a half of the body (and a step it took since).
                    bool beat = false, near = false;
                    for (int s = std::max(0, casts - 64); s < casts; ++s) {
                        const Storm& storm = storms[s % 64];
                        const int64_t age = int64_t(one.tick) - storm.at;
                        if (age != 11 && age != 23 && age != 35) continue;
                        beat = true;
                        if (age == 35) ++thrice;
                        const float walked = float(age + 1) * 0.125f;
                        const float sx = storm.x + storm.dx * walked, sy = storm.y + storm.dy * walked;
                        if (std::hypot(at->x - sx, at->y - sy) <= 1.5f + 0.3f) near = true;
                    }
                    if (!beat) ++offBeat;
                    if (!near) ++offPath;
                }
            }
            std::printf("  twister: %d storms, %d blows, %d on a third beat, %d of his staff\n",
                        casts, struck, thrice, swung);
            check(casts > 5, "he casts it");
            // Not every storm: one cast at a body six tiles off meets it walking in, and what
            // walks through the storm between its beats is not struck, as in MU.
            check(struck > casts / 2, "and its storms strike");
            check(thrice > 0, "as late as its third strike, four and a half tiles out");
            checkEqual(offBeat, 0, "every blow on a storm's beat, 11, 23 or 35 ticks in");
            checkEqual(offPath, 0, "and only within a tile and a half of where it has walked");
        }

        // Inferno: 0.95d's, struck as WebZen strikes it -- Hellfire's SkillHellFire, every
        // monster within four tiles of him once, said once a cast for the ring.
        const sim::SkillRow& inferno = *sim::skillNumbered(sim::skill::kInferno);
        check(inferno.wizardry && inferno.primary() && inferno.damage == 100 &&
                  inferno.mana == 200 && inferno.spread == sim::Spread::Ring &&
                  inferno.reach == 4.0f && inferno.clip == 153 &&
                  inferno.kin == sim::Kin::DarkWizard,
              "Inferno is a no-cooldown ring of a hundred damage and two hundred mana");
        check(sim::skillElement(sim::skill::kInferno) == sim::Element::Fire, "and it is fire");
        check(sim::skillIndexOf(sim::skill::kInferno) + 1 == sim::skillIndexOf(sim::skill::kAquaBeam),
              "and its row is past Twister's, the learned mask's thirty-third bit");
        const int32_t infernoScroll = tables.itemAt(15, 13);
        check(infernoScroll >= 0 &&
                  tables.items[size_t(infernoScroll)].teaches == sim::skill::kInferno &&
                  tables.items[size_t(infernoScroll)].teachesEnergy == 578,
              "the Scroll of Inferno teaches skill 14 at five hundred and seventy-eight energy");
        {
            sim::Realm burner;
            check(burner.raise(&tables, 7, 190, 110, sim::Kin::DarkWizard, 100),
                  "a wizard raises for Inferno");
            check(burner.learn(sim::skill::kInferno), "who knows Inferno");
            check(burner.knows(sim::skill::kInferno) && burner.knows(sim::skill::kEnergyBall),
                  "past the thirty-second bit, and his first spell still known");
            int rings = 0, struck = 0, outside = 0, twice = 0;
            uint32_t fighting = 0;
            for (int tick = 0; tick < 4000 && burner.hero().alive(); ++tick) {
                const uint32_t nearest = nearestTo(burner);
                if (nearest != 0 && nearest != fighting) {
                    fighting = nearest;
                    sim::Request request;
                    request.kind = sim::Request::Kind::Attack;
                    request.target = nearest;
                    request.skill = sim::skill::kInferno;
                    burner.ask(request);
                }
                burner.step();
                uint32_t onThis[sim::kVictims] = {};
                int seen = 0;
                for (const sim::Happening& one : burner.happenings()) {
                    if (one.who != burner.hero().id) continue;
                    if (one.what == sim::What::Loosed && one.a == sim::skill::kInferno) ++rings;
                    if (one.what != sim::What::Hit && one.what != sim::What::Missed) continue;
                    // The ring's blows are said as flown; his staff's, once his mana is out,
                    // are not.
                    if (!one.thrown) continue;
                    const sim::Body* at = burner.find(one.whom);
                    if (at == nullptr) continue;
                    ++struck;
                    for (int i = 0; i < seen; ++i) twice += onThis[i] == one.whom;
                    if (seen < sim::kVictims) onThis[seen++] = one.whom;
                    const float dx = at->x - burner.hero().x, dy = at->y - burner.hero().y;
                    if (std::max(std::fabs(dx), std::fabs(dy)) > 4.0f) ++outside;
                }
            }
            std::printf("  inferno: %d rings, %d blows\n", rings, struck);
            check(rings > 3, "he casts it");
            check(struck >= rings, "and every ring strikes");
            checkEqual(outside, 0, "only within four tiles of him");
            checkEqual(twice, 0, "and each body once a ring");
        }

        // Aqua Beam: 0.75's row, struck as MU's client strikes it -- four circles of a tile and
        // a half along his aim from his hand (sim/skills.h kBeamStart), each body once.
        const sim::SkillRow& aqua = *sim::skillNumbered(sim::skill::kAquaBeam);
        check(aqua.wizardry && aqua.primary() && aqua.damage == 80 && aqua.mana == 140 &&
                  aqua.spread == sim::Spread::Beam && aqua.reach == 6.0f && aqua.clip == 152 &&
                  aqua.kin == sim::Kin::DarkWizard,
              "Aqua Beam is a no-cooldown beam of eighty damage and a hundred and forty mana");
        check(sim::skillIndexOf(sim::skill::kAquaBeam) == sim::kSkills - 1 && sim::kSkills == 34,
              "and its row is the table's last");
        const int32_t aquaScroll = tables.itemAt(15, 11);
        check(aquaScroll >= 0 &&
                  tables.items[size_t(aquaScroll)].teaches == sim::skill::kAquaBeam &&
                  tables.items[size_t(aquaScroll)].teachesEnergy == 345 &&
                  tables.items[size_t(aquaScroll)].teachesLevel == 148,
              "the Scroll of Aqua Beam teaches skill 12 at 345 energy and level 148");
        {
            sim::Realm beamer;
            check(beamer.raise(&tables, 7, 190, 110, sim::Kin::DarkWizard, 150),
                  "a wizard raises for Aqua Beam");
            check(beamer.learn(sim::skill::kAquaBeam), "who knows Aqua Beam");
            // With the energy the scroll asks spent, as its reader has it: unspent, a level-150
            // wizard's 360 mana is two beams and then his staff.
            sim::HeroRecord spent = beamer.record();
            spent.points.energy = 345 + 200;
            spent.mana = 1 << 20;
            beamer.restore(spent);
            int beams = 0, struck = 0, outside = 0, twice = 0;
            float aim = 0.0f, fromX = 0.0f, fromY = 0.0f;
            uint32_t fighting = 0;
            for (int tick = 0; tick < 4000 && beamer.hero().alive(); ++tick) {
                const uint32_t nearest = nearestTo(beamer);
                if (nearest != 0 && nearest != fighting) {
                    fighting = nearest;
                    sim::Request request;
                    request.kind = sim::Request::Kind::Attack;
                    request.target = nearest;
                    request.skill = sim::skill::kAquaBeam;
                    beamer.ask(request);
                }
                beamer.step();
                uint32_t onThis[sim::kVictims] = {};
                int seen = 0;
                for (const sim::Happening& one : beamer.happenings()) {
                    if (one.who != beamer.hero().id) continue;
                    if (one.what == sim::What::Loosed && one.a == sim::skill::kAquaBeam) {
                        ++beams;
                        aim = float(one.c) / 1000.0f;
                        fromX = one.x;
                        fromY = one.y;
                    }
                    if (one.what != sim::What::Hit && one.what != sim::What::Missed) continue;
                    if (!one.thrown) continue;  // his staff, once his mana is out
                    const sim::Body* at = beamer.find(one.whom);
                    if (at == nullptr) continue;
                    ++struck;
                    for (int i = 0; i < seen; ++i) twice += onThis[i] == one.whom;
                    if (seen < sim::kVictims) onThis[seen++] = one.whom;
                    bool inside = false;
                    for (int k = 1; k <= sim::kBeamPoints; ++k) {
                        const float along = sim::kBeamStart + sim::kBeamStep * float(k);
                        inside = inside || std::hypot(at->x - fromX - std::cos(aim) * along,
                                                      at->y - fromY - std::sin(aim) * along) <=
                                               sim::kBeamRadius + 0.3f;
                    }
                    if (!inside) ++outside;
                }
            }
            std::printf("  aqua beam: %d beams, %d blows\n", beams, struck);
            check(beams > 3, "he casts it");
            check(struck >= beams / 2, "and its beams strike");
            checkEqual(outside, 0, "only within the beam's four circles");
            checkEqual(twice, 0, "and each body once a beam");
        }

        // A skill with a direction, pressed from a key with the mouse over the ground, goes the
        // pointer's way -- body or no body (SkillRow::aimsAtPointer; the user, 2026-10-02).
        {
            sim::Realm pointer;
            check(pointer.raise(&tables, 7, 190, 110, sim::Kin::DarkWizard, 150),
                  "a wizard raises to aim at the pointer");
            check(pointer.learn(sim::skill::kAquaBeam) && pointer.learn(sim::skill::kTwister),
                  "who knows Aqua Beam and Twister");
            sim::HeroRecord full = pointer.record();
            full.points.energy = 600;
            full.mana = 1 << 20;
            pointer.restore(full);
            check(sim::skillNumbered(sim::skill::kAquaBeam)->aimsAtPointer() &&
                      sim::skillNumbered(sim::skill::kTwister)->aimsAtPointer() &&
                      !sim::skillNumbered(sim::skill::kHellfire)->aimsAtPointer() &&
                      !sim::skillNumbered(sim::skill::kFireBall)->aimsAtPointer(),
                  "a beam and a storm aim at the pointer; a ring and a single throw do not");
            const auto pressAt = [&](int32_t skill, int dc, int dr) {
                const int column = pointer.hero().column(), row = pointer.hero().row();
                pointer.invokeAt(skill, column + dc, row + dr);
                for (int tick = 0; tick < 200; ++tick) {
                    pointer.step();
                    for (const sim::Happening& one : pointer.happenings()) {
                        if (one.who == pointer.hero().id && one.what == sim::What::Loosed &&
                            one.a == skill) {
                            return float(one.c) / 1000.0f;
                        }
                    }
                }
                return 99.0f;
            };
            const float east = pressAt(sim::skill::kAquaBeam, 6, 0);
            check(std::fabs(east) < 0.05f, "Aqua Beam pressed toward the east goes east");
            const float north = pressAt(sim::skill::kTwister, 0, -6);
            check(std::fabs(north + 1.5708f) < 0.05f, "and Twister toward the north goes north");
            std::printf("  pointer: beam %.2f, storm %.2f radians\n", east, north);
        }

        sim::Realm wiz;
        check(wiz.raise(&tables, 7, 190, 110, sim::Kin::DarkWizard, 30), "a wizard raises to hunt");
        check(wiz.learn(sim::skill::kFlame), "who knows Flame");
        struct Lit {
            int64_t tick;
            float x, y;
        };
        std::vector<Lit> fires;
        int casts = 0, strikes = 0, offBeat = 0, outside = 0, widest = 0;
        int64_t lastCast = -1, closest = 1 << 30;
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
            if (nearest != 0 && wiz.cooling(sim::skill::kFlame) == 0) {
                wiz.invoke(sim::skill::kFlame, nearest);
            }
            wiz.step();
            int thisTick = 0;
            for (const sim::Happening& one : wiz.happenings()) {
                if (one.what == sim::What::Cast && one.who == wiz.hero().id &&
                    one.a == sim::skill::kFlame) {
                    ++casts;
                    if (lastCast >= 0) closest = std::min<int64_t>(closest, one.tick - lastCast);
                    lastCast = one.tick;
                }
                if (one.what == sim::What::Loosed && one.a == sim::skill::kFlame) {
                    if (const sim::Body* at = wiz.find(one.whom)) {
                        fires.push_back({int64_t(one.tick), float(at->column()), float(at->row())});
                    }
                }
            }
            for (const sim::Happening& one : wiz.happenings()) {
                if ((one.what != sim::What::Hit && one.what != sim::What::Missed) ||
                    one.who != wiz.hero().id || !one.thrown) {
                    continue;
                }
                // A strike of a fire: on the tick it was lit or a burn later, and inside it.
                const Lit* mine = nullptr;
                for (const Lit& fire : fires) {
                    const int64_t after = int64_t(one.tick) - fire.tick;
                    if (after == 0 || after == sim::kBurnEvery) mine = &fire;
                }
                if (mine == nullptr) {
                    ++offBeat;
                    continue;
                }
                ++strikes;
                ++thisTick;
                if (const sim::Body* struck = wiz.find(one.whom)) {
                    // Where it stood when struck: this tick's position, less a push it took.
                    if (std::hypot(struck->x - mine->x, struck->y - mine->y) > 1.5f + 1.0f) {
                        ++outside;
                    }
                }
            }
            widest = std::max(widest, thisTick);
        }
        std::printf("  flame: %d cast, %d fires, %d strikes, %d at once at the most, %lld ticks "
                    "apart at the closest\n",
                    casts, int(fires.size()), strikes, widest, (long long)closest);
        check(casts >= 5 && strikes > int(fires.size()), "he lights fires through a hunt and each "
                                                        "strikes more than once on the whole");
        checkEqual(offBeat, 0, "every strike lands on the lighting or a burn after it");
        checkEqual(outside, 0, "and on nobody outside the fire");
        check(wiz.cooling(sim::skill::kFlame) == 0 && closest < 80,
              "and with no cooldown, cast again as soon as the clip and the mana allow");
    }

    // ---- Ice: a cooldown spell that bursts round its target and halves the walk -------------
    {
        const sim::SkillRow& ice = *sim::skillNumbered(sim::skill::kIce);
        check(ice.wizardry && ice.primary() && ice.force == 1.5f && ice.damage == 10 &&
                  ice.mana == 38 && ice.chillTicks == 200 && ice.splash == 4.0f,
              "Ice is a standard spell of ten damage at half again the band and thirty-eight mana, chilling "
              "ten seconds within four tiles");
        const int32_t scroll = tables.itemAt(15, 6);
        check(scroll >= 0 && tables.items[size_t(scroll)].teaches == sim::skill::kIce &&
                  tables.items[size_t(scroll)].teachesEnergy == 120,
              "the Scroll of Ice teaches skill 7 at a hundred and twenty energy");

        // Seed 3, not 7: on 7, under WebZen's round sight, no iced body was seen to walk.
        sim::Realm wiz;
        check(wiz.raise(&tables, 3, 190, 110, sim::Kin::DarkWizard, 30), "a wizard raises to hunt");
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
        check(closest >= 10, "paced by its own clip, with no cooldown");
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
        check(rock.wizardry && rock.primary() && rock.thrown() && rock.damage == 21 &&
                  rock.fallTicks == 7 && rock.clip == 183 && rock.force == 2.0f,
              "Meteorite is a thrown standard spell at twenty-one damage, at twice the band, falling "
              "seven ticks, cast in the arm-up clip");
        const int32_t scroll = tables.itemAt(15, 1);
        check(scroll >= 0 && tables.items[size_t(scroll)].teaches == sim::skill::kMeteorite &&
                  tables.items[size_t(scroll)].teachesEnergy == 104,
              "the Scroll of Meteorite teaches skill 2 at a hundred and four energy");
        check(tables.action(183) != nullptr, "the tables carry the arm-up clip's length");
        // The level comes first since the ladder of 2026-10-02: forty-one is refused with all
        // the energy, forty-two reads it.
        {
            sim::Realm early;
            check(early.raise(&tables, 7, 190, 110, sim::Kin::DarkWizard, 41),
                  "a wizard of forty-one raises");
            sim::HeroRecord held = early.record();
            held.slots[sim::kWorn + 40].item = scroll;
            held.slots[sim::kWorn + 40].durability = 1;
            held.points.energy = 200;
            early.restore(held);
            check(!early.useItem(sim::kWorn + 40),
                  "and is refused the Scroll of Meteorite, which asks for level forty-two");
            check(tables.items[size_t(scroll)].teachesLevel == 42, "as its row says");
        }
        sim::Realm reader;
        check(reader.raise(&tables, 7, 190, 110, sim::Kin::DarkWizard, 42), "a wizard raises");
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
        check(closest >= 10, "paced by its own clip, with no cooldown");
        checkEqual(lateOrEarly, 0, "every rock lands its fall after the let-go");
        check(widest >= 2, "and one cast drops a rock on more than one body");
        checkEqual(movedWhile, 0, "and he does not move while he calls it");
    }

    // ---- Lightning: a channel -- three seconds of pulses into everything around him ----------
    {
        const sim::SkillRow& bolt = *sim::skillNumbered(sim::skill::kLightning);
        check(bolt.wizardry && bolt.channelled() && bolt.primary() && !bolt.thrown() &&
                  bolt.pushes && bolt.spread == sim::Spread::Ring && bolt.damage == 17 &&
                  bolt.mana == 40 && bolt.coolTicks == 0 && bolt.channelTicks == 42,
              "Lightning is a channel round him as long as its clip, with no cooldown, and it "
              "pushes");
        check(bolt.force == 1.5f && sim::force(bolt, sim::HeroPoints{}) == 1.5f &&
                  sim::force(*sim::skillNumbered(sim::skill::kFireBall), sim::HeroPoints{}) == 1.8f,
              "and each strike is at half again the band, its long clip being its wait, where Fire Ball's is at 1.8");
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
        // Channels cast with two or more bodies in reach: with no cooldown (2026-09-30) he casts
        // the moment one comes near, and a hunt may never gather company for one to go round.
        int withCompany = 0;
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
                    int near = 0;
                    const sim::Body& me = wiz.hero();
                    const float reach = sim::skillNumbered(sim::skill::kLightning)->reach;
                    for (const sim::Body& b : wiz.bodies()) {
                        if (b.monster() && b.alive() && std::hypot(b.x - me.x, b.y - me.y) <= reach) {
                            ++near;
                        }
                    }
                    if (near >= 2) ++withCompany;
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
        check(closest >= sim::skillNumbered(sim::skill::kLightning)->channelTicks,
              "and never twice inside one channel");
        // One body a strike, and round the ring: a channel with company strikes more than one.
        checkEqual(widest, 1, "and each strike goes to one body");
        check(mostOnOne <= 1, "and no body is struck more than once in a cast");
        check(sweptMost >= 2 || withCompany == 0,
              "and a channel cast among company goes round to more than one");
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

// Sprint 15: the elf's bow. Her band is the archery one, she shoots from six tiles, one arrow a
// shot, the damage a flight after the let-go, the first shot reloads the empty hand from the
// bag, and when the quiver runs out the attack stops with "no more arrows".
void testArchery(const content::Tables& tables) {
    std::printf("archery\n");
    sim::Realm realm;
    check(realm.raise(&tables, 7, 212, 198, sim::Kin::FairyElf, 10), "an elf raises among spiders");
    check(realm.equip(tables.armNamed("Bow01"), -1, true), "with the Short Bow");
    const sim::Satchel& bag = realm.satchel();
    check(realm.hero().archer == 1, "and she is an archer");
    check(!bag[sim::kWeaponRight].empty() && bag[sim::kWeaponLeft].durability == 255,
          "the bow in her weapon hand and 255 arrows in her left");
    // Her archery band: level-one points, agility 25 and strength 22, on the Short Bow.
    sim::Fighter melee;
    int health = 0;
    sim::reckon(sim::Kin::FairyElf, 10, sim::startingPoints(sim::Kin::FairyElf), sim::Arms{},
                &melee, &health);
    check(realm.hero().stats.minimumDamage != melee.minimumDamage,
          "the band is not her melee one");

    // The quiver into the bag: the first shot must take it back into the empty hand.
    check(realm.moveItem(sim::kWeaponLeft, sim::kWorn), "the arrows go into the bag");
    check(bag[sim::kWeaponLeft].empty(), "and her left hand is empty");

    int drawn = 0, loosed = 0, far = 0, arrowless = 0, landedOnTime = 0, flewAtAll = 0;
    int64_t firstShotTick = -1;
    struct Due {
        int64_t at;
        uint32_t whom;
    };
    std::vector<Due> due;
    for (int tick = 0; tick < 40000 && arrowless == 0; ++tick) {
        if (tick % 10 == 0) {
            uint32_t nearest = 0;
            float closest = 1e30f;
            const sim::Body& hero = realm.hero();
            for (const sim::Body& one : realm.bodies()) {
                if (!one.monster() || !one.alive()) continue;
                const float d = std::max(std::fabs(one.x - hero.x), std::fabs(one.y - hero.y));
                if (d < closest) {
                    closest = d;
                    nearest = one.id;
                }
            }
            sim::Request request;
            request.kind = sim::Request::Kind::Attack;
            request.target = nearest;
            if (nearest != 0) realm.ask(request);
        }
        realm.step();
        for (const sim::Happening& h : realm.happenings()) {
            if (h.who == realm.hero().id && h.what == sim::What::Swung) ++drawn;
            if (h.who == realm.hero().id && h.what == sim::What::Loosed) {
                ++loosed;
                if (firstShotTick < 0) firstShotTick = realm.tick();
                if (h.b > 0) {
                    ++flewAtAll;
                    due.push_back({realm.tick() + h.b, h.whom});
                }
                const sim::Body* at = realm.find(h.whom);
                if (at && std::max(std::fabs(at->x - realm.hero().x),
                                   std::fabs(at->y - realm.hero().y)) > 1.5f) {
                    ++far;
                }
            }
            if (h.who == realm.hero().id &&
                (h.what == sim::What::Hit || h.what == sim::What::Missed)) {
                for (Due& d : due) {
                    if (d.at == realm.tick() && d.whom == h.whom) {
                        ++landedOnTime;
                        d.at = -1;
                    }
                }
            }
            if (h.what == sim::What::Arrowless) ++arrowless;
        }
        if (firstShotTick == realm.tick()) {
            check(bag[sim::kWeaponLeft].durability == 254 && bag[sim::kWorn].empty(),
                  "the first shot reloads the hand from the bag and spends one");
        }
    }
    // Drawn, not let go: a shot whose target dies before the string is released has still
    // spent its arrow, as MU spends it at the draw.
    check(drawn == 255, "every arrow is one draw, and no draw without one");
    check(loosed <= drawn && loosed > 200, "and nearly every draw is let go");
    check(far > 0, "some are shot from past arm's length");
    check(flewAtAll > 0 && landedOnTime > 0, "and those land a flight after the let-go");
    check(arrowless == 1, "the empty quiver stops the attack with no more arrows");
    check(bag[sim::kWeaponLeft].empty(), "and leaves the hand empty");
    std::printf("  %d drawn, %d shots, %d from range, %d in the air, %d landed on time\n", drawn, loosed, far,
                flewAtAll, landedOnTime);

    // A quiver's plus: 3%, 5%, 7% of the band and one more (WebZen ObjCalCharacter.cpp:1457),
    // and its price off WebZen's table, the arrows 70 / 1,200 / 2,000 / 2,800.
    const int arrows = tables.itemAt(4, 15), bolt = tables.itemAt(4, 7);
    const int bare = realm.hero().stats.maximumDamage;
    check(realm.give(arrows, sim::kWeaponLeft, 2) == sim::kWeaponLeft, "a +2 quiver in hand");
    checkEqual(realm.hero().quiverPlus, 2, "and her band knows its plus");
    checkEqual(realm.hero().stats.maximumDamage, bare + int(float(bare) * 0.05f + 1.0f),
               "and it adds 5% and one to her damage");
    const int64_t prices[4] = {70, 1200, 2000, 2800}, bolts[4] = {100, 1400, 2200, 3000};
    for (int plus = 0; plus < 4; ++plus) {
        checkEqual((long long)sim::buyingPrice(tables.items[size_t(arrows)], plus, 1, false, 255, 255),
                   (long long)prices[plus], "a full quiver of arrows at its plus");
        checkEqual((long long)sim::buyingPrice(tables.items[size_t(bolt)], plus, 1, false, 255, 255),
                   (long long)bolts[plus], "and of bolts");
    }
}

// Sprint 15, step 4: the elf's four. Greater Defense is her guard on the knight's curve with no
// shield; Greater Damage adds its bonus to the band's fighter; Heal never overfills; Skillshot
// on the quick slot fans arrows into the spiders and pays one arrow a body struck.
void testElfSkills(const content::Tables& tables) {
    std::printf("the elf's skills\n");
    sim::Realm realm;
    check(realm.raise(&tables, 11, 212, 198, sim::Kin::FairyElf, 30), "an elf raises among spiders");
    check(realm.equip(tables.armNamed("Bow01"), -1, true), "with the Short Bow and a quiver");
    for (int32_t one : {sim::skill::kSkillshot, sim::skill::kHeal, sim::skill::kGreaterDefense,
                        sim::skill::kGreaterDamage}) {
        check(realm.learn(one), "she learns one of her four");
    }
    const auto cast = [&](int32_t skill) {
        // Her mana back first: seventy-three at level thirty does not pay for all three at once.
        const sim::SkillRow* row = sim::skillNumbered(skill);
        for (int wait = 0; wait < 6000 && row && realm.hero().mana < row->mana; ++wait) {
            realm.step();
        }
        realm.invoke(skill, realm.hero().id);
        for (int tick = 0; tick < 80; ++tick) {
            realm.step();
            for (const sim::Happening& h : realm.happenings()) {
                if (h.what == sim::What::Cast && h.who == realm.hero().id && h.a == skill) {
                    return true;
                }
            }
        }
        return false;
    };
    check(cast(sim::skill::kGreaterDefense), "Greater Defense is raised with no shield");
    checkNear(realm.hero().stats.damageTaken,
              1.0 - double(sim::wardShare(realm.hero().points)), 1e-6,
              "and it takes the ward's share of every blow");
    check(sim::wardShare(sim::startingPoints(sim::Kin::FairyElf)) > 0.13f &&
              sim::wardShare(sim::startingPoints(sim::Kin::FairyElf)) < 0.16f,
          "a new elf's ward stands beside a new knight's guard and a new wizard's barrier");
    const int before = realm.hero().stats.minimumDamage;
    for (int wait = 0; wait < 40; ++wait) realm.step();
    check(cast(sim::skill::kGreaterDamage), "Greater Damage is raised");
    check(realm.hero().stats.greaterDamage == sim::mightOf(realm.hero().points) &&
              realm.hero().stats.minimumDamage == before,
          "and it rides every blow after the defence, not the band");
    for (int wait = 0; wait < 40; ++wait) realm.step();
    check(cast(sim::skill::kHeal), "Heal is cast");
    check(realm.hero().health <= realm.hero().maxHealth, "and never past the most she has");
    // Greater Damage stands as long as Greater Defense (the user, 2026-10-02), then lapses.
    {
        const sim::SkillRow* might = sim::skillNumbered(sim::skill::kGreaterDamage);
        const sim::SkillRow* ward = sim::skillNumbered(sim::skill::kGreaterDefense);
        check(might && ward && might->mightTicks == ward->boonTicks,
              "Greater Damage lasts as long as Greater Defense");
    }
    for (int wait = 0; wait < 1210; ++wait) realm.step();
    check(realm.hero().stats.greaterDamage > 0, "and is still up after a minute");
    // And it is saved: a restart brings it back at the same bonus, for the time it had left.
    {
        const sim::HeroRecord saved = realm.record();
        check(saved.might == realm.hero().might && saved.mightTicksLeft > 0,
              "her save carries Greater Damage");
        sim::Realm again;
        check(again.raise(&tables, 11, 212, 198, sim::Kin::FairyElf, 30), "and she comes back");
        again.restore(saved);
        check(again.hero().stats.greaterDamage == realm.hero().stats.greaterDamage &&
                  again.hero().mightUntil - again.tick() == saved.mightTicksLeft,
              "with Greater Damage up, at the same bonus and time left");
        sim::HeroRecord edited = saved;
        edited.might = 100000;
        edited.mightTicksLeft = 100000000;
        sim::Realm forged;
        check(forged.raise(&tables, 11, 212, 198, sim::Kin::FairyElf, 30), "a forged save raises");
        forged.restore(edited);
        check(forged.hero().might <= sim::mightOf(forged.hero().points) &&
                  forged.hero().mightUntil - forged.tick() <=
                      sim::skillNumbered(sim::skill::kGreaterDamage)->mightTicks,
              "and an edited file gives no more than her energy, for no longer than the skill");
    }
    for (int wait = 0; wait < 6000; ++wait) realm.step();
    check(realm.hero().stats.greaterDamage == 0, "Greater Damage lapses after its five minutes");

    // Skillshot on the quick slot, at the nearest spider, over and over.
    const int quiver = realm.satchel()[sim::kWeaponLeft].durability;
    int fans = 0, flown = 0;
    for (int tick = 0; tick < 3000; ++tick) {
        if (tick % 10 == 0) {
            uint32_t nearest = 0;
            float closest = 1e30f;
            for (const sim::Body& one : realm.bodies()) {
                if (!one.monster() || !one.alive()) continue;
                const float d = std::max(std::fabs(one.x - realm.hero().x),
                                         std::fabs(one.y - realm.hero().y));
                if (d < closest) {
                    closest = d;
                    nearest = one.id;
                }
            }
            sim::Request request;
            request.kind = sim::Request::Kind::Attack;
            request.target = nearest;
            request.skill = sim::skill::kSkillshot;
            if (nearest != 0) realm.ask(request);
        }
        realm.step();
        for (const sim::Happening& h : realm.happenings()) {
            if (h.who != realm.hero().id) continue;
            if (h.what == sim::What::Loosed && h.a == sim::skill::kSkillshot) ++fans;
            if ((h.what == sim::What::Hit || h.what == sim::What::Missed) && h.thrown) ++flown;
        }
    }
    const int spent = quiver - realm.satchel()[sim::kWeaponLeft].durability;
    check(fans > 0, "Skillshot is loosed off the quick slot");
    check(spent > 0 && flown > 0, "and its arrows strike and are paid for");
    std::printf("  %d fans, %d arrows spent, %d landed\n", fans, spent, flown);

    // Her orbs: each teaches its row, Lala sells the Orb of Skillshot, and her guard's orb costs
    // what the knight's does (the user, 2026-09-28: "same stats as others").
    const int ward = tables.itemAt(12, 9), might = tables.itemAt(12, 10),
              shot = tables.itemAt(12, 21), guard = tables.itemAt(12, 3);
    check(ward >= 0 && might >= 0 && shot >= 0 && guard >= 0, "her three orbs are cooked");
    if (ward < 0 || might < 0 || shot < 0 || guard < 0) return;
    check(tables.items[size_t(ward)].teaches == sim::skill::kGreaterDefense &&
              tables.items[size_t(might)].teaches == sim::skill::kGreaterDamage &&
              tables.items[size_t(shot)].teaches == sim::skill::kSkillshot,
          "and each teaches its own skill");
    checkEqual((long long)sim::buyingPrice(tables.items[size_t(ward)], 0, 1, false),
               (long long)sim::buyingPrice(tables.items[size_t(guard)], 0, 1, false),
               "the Orb of Greater Defense costs what the Orb of Defense costs");
    check(tables.items[size_t(ward)].needLevel == tables.items[size_t(guard)].needLevel,
          "and asks the same level");
    int stocked = 0;
    const sim::Offer* lala = sim::stockOf(242, &stocked);
    bool sold = false;
    for (int i = 0; i < stocked; ++i) sold |= lala[i].group == 12 && lala[i].number == 21;
    check(sold, "Elf Lala sells the Orb of Skillshot");
    // WebZen's shelves (sim/market.cpp): nothing refined but the arrows and bolts, Harold and
    // Martin apart, the Barmaid's Ale first, and Hanzo's Bronze set.
    {
        bool refined = false, bronze = false;
        for (int npc : {251, 254, 250, 248, 253, 244, 245, 246, 243, 242}) {
            int n = 0;
            const sim::Offer* shelf = sim::stockOf(npc, &n);
            for (int i = 0; i < n; ++i) {
                if (shelf[i].refinement > 0 && shelf[i].group != 4) refined = true;
                if (npc == 251 && shelf[i].group == 8 && shelf[i].number == 0) bronze = true;
            }
        }
        check(!refined, "WebZen's shelves sell nothing refined but ammunition");
        check(bronze, "Hanzo sells the Bronze Armor");
        int harold = 0, martin = 0, caren = 0;
        sim::stockOf(250, &harold);
        sim::stockOf(248, &martin);
        const sim::Offer* bar = sim::stockOf(244, &caren);
        check(harold != martin, "Harold and Martin keep shelves of their own");
        check(caren > 0 && bar[0].group == 14 && bar[0].number == 9, "Caren's first is the Ale");
    }
}

// Sprint 15, step 5: her summon. Raised beside her off the breed's row and scaled by her energy;
// it hunts round her, its kills are hers, it holds a monster against her own shots, and a second
// cast dismisses it for nothing.
void testSummons(const content::Tables& tables) {
    std::printf("the elf's summon\n");
    // Among the Skeleton Warriors (525 health): a spider dies to one of the golem's blows and
    // never lives to turn on it. Found in a first realm, then stood three tiles off one.
    int standColumn = 212, standRow = 198;
    {
        sim::Realm look;
        look.raise(&tables, 5, 212, 198, sim::Kin::FairyElf, 40);
        for (const sim::Body& one : look.bodies()) {
            if (one.monster() && tables.kinds[size_t(one.kind)].number == 14) {
                standColumn = one.homeColumn + 3;
                standRow = one.homeRow;
                break;
            }
        }
    }
    sim::Realm realm;
    check(realm.raise(&tables, 5, standColumn, standRow, sim::Kin::FairyElf, 40),
          "an elf raises by the Skeleton Warriors");
    check(realm.equip(tables.armNamed("Bow01"), -1, true), "with the Short Bow");
    check(realm.spend(0, 0, 0, 150), "and puts 150 points into energy");
    check(realm.learn(sim::skill::kSummonGolem), "she knows Summon Stone Golem");
    const auto summonOf = [&]() -> const sim::Body* {
        for (const sim::Body& one : realm.bodies()) {
            if (one.summoner != 0) return &one;
        }
        return nullptr;
    };
    check(summonOf() != nullptr && !summonOf()->alive(), "a dormant summon body stands ready");
    for (int wait = 0; wait < 6000 && realm.hero().mana < 70; ++wait) realm.step();
    realm.invoke(sim::skill::kSummonGolem, realm.hero().id);
    for (int tick = 0; tick < 60 && !(summonOf() && summonOf()->alive()); ++tick) realm.step();
    const sim::Body* golem = summonOf();
    check(golem != nullptr && golem->alive() && golem->summoner == realm.hero().id,
          "the Stone Golem stands beside her");
    if (golem == nullptr || !golem->alive()) return;
    // The user, 2026-10-02: "give elf summon a 1 min cooldown after summon. its global for all
    // summons" -- a minute on every summon key, her agility notwithstanding.
    realm.learn(sim::skill::kSummonGoblin);
    check(realm.cooling(sim::skill::kSummonGolem) > sim::kSummonCool - 60 &&
              realm.cooling(sim::skill::kSummonGoblin) == realm.cooling(sim::skill::kSummonGolem),
          "a summon cools every summon key for a minute");
    const content::MonsterKind& kind = tables.kinds[size_t(golem->kind)];
    checkEqual((long long)kind.number, 32LL, "and it is the Stone Golem's breed");
    const int level = sim::summonLevel(kind.level, realm.hero().level, sim::skill::kSummonGolem);
    checkEqual((long long)golem->level, (long long)(kind.level + realm.hero().level * 2 / 10),
               "it stands at the breed's level and a fifth of hers");
    // The better summon scales better: beside a level-150 elf the Goblin is well under the
    // Golem, further under it than beside a level-60 one.
    const auto lead = [](int heroLevel) {
        const int goblin = sim::summonLevel(3, heroLevel, sim::skill::kSummonGoblin);
        const int golemAt = sim::summonLevel(18, heroLevel, sim::skill::kSummonGolem);
        return 465.0f * sim::summonClimb(sim::Ladder::Health, 18, golemAt) /
               (45.0f * sim::summonClimb(sim::Ladder::Health, 3, goblin));
    };
    std::printf("  a Golem over a Goblin, in health: %.1fx at level 60, %.1fx at 150\n",
                lead(60), lead(150));
    const auto gap = [](int heroLevel) {
        return sim::summonLevel(18, heroLevel, sim::skill::kSummonGolem) -
               sim::summonLevel(3, heroLevel, sim::skill::kSummonGoblin);
    };
    check(gap(150) > gap(60) && lead(60) > 2.0f && lead(150) > 2.0f,
          "and a Golem stays well over a Goblin, further up the ladder as she levels");
    checkEqual((long long)golem->maxHealth,
               (long long)int(float(kind.health) *
                              sim::summonClimb(sim::Ladder::Health, kind.level, level) *
                              sim::summonHealthRate(realm.hero().points)),
               "its health is the breed's, up the ladder to its level and scaled by her points");
    // Up the ladder: a Golem at 60 stands near the ladder's own 60 before her points.
    check(std::fabs(sim::summonClimb(sim::Ladder::Health, 18, 60) * 465.0f / 5000.0f - 1.0f) <
              0.15f,
          "the ladder carries a Golem's health to the level-60 monsters'");
    check(sim::summonClimb(sim::Ladder::Damage, 3, 3) == 1.0f, "and leaves a breed at its own");
    check(golem->stats.minimumDamage > kind.minimumDamage, "and it bites harder than the breed");
    const uint32_t golemId = golem->id;

    // Left alone, it hunts round her: the first monster that turns on it is the one she shoots
    // next, to see that it stays on the golem.
    const uint64_t before = realm.hero().experience;
    int blows = 0, kills = 0;
    uint32_t held = 0;
    for (int tick = 0; tick < 3000 && held == 0; ++tick) {
        realm.step();
        for (const sim::Happening& h : realm.happenings()) {
            if (h.who == golemId && h.what == sim::What::Hit) ++blows;
        }
        for (const sim::Body& one : realm.bodies()) {
            if (one.monster() && one.alive() && one.quarry == golemId && one.health > one.maxHealth / 2) {
                held = one.id;
            }
        }
    }
    check(blows > 0, "the golem fights what is round her");
    check(held != 0, "a monster turns on the golem");
    if (held != 0) {
        sim::Request shoot;
        shoot.kind = sim::Request::Kind::Attack;
        shoot.target = held;
        realm.ask(shoot);
        bool turned = false, struck = false;
        for (int tick = 0; tick < 200; ++tick) {
            realm.step();
            for (const sim::Happening& h : realm.happenings()) {
                if (h.who == realm.hero().id && h.whom == held &&
                    (h.what == sim::What::Hit || h.what == sim::What::Missed)) {
                    struck = true;
                }
            }
            const sim::Body* one = realm.find(held);
            const sim::Body* g = realm.find(golemId);
            if (one && one->alive() && g && g->alive() && one->quarry == realm.hero().id) turned = true;
        }
        check(struck, "she shoots it");
        check(!turned, "and it stays on the golem: the summon holds aggro");
    }
    // And its kills are hers.
    sim::Request stop;
    stop.kind = sim::Request::Kind::Stop;
    realm.ask(stop);
    for (int tick = 0; tick < 3000; ++tick) {
        realm.step();
        for (const sim::Happening& h : realm.happenings()) {
            if (h.what == sim::What::Died && h.whom == golemId) ++kills;
        }
    }
    check(kills > 0 && realm.hero().experience > before, "what the golem kills pays her");

    // A second cast dismisses it, for nothing.
    if (const sim::Body* g = realm.find(golemId); g && g->alive()) {
        for (int wait = 0; wait < 6000 && realm.hero().mana < 70; ++wait) realm.step();
        for (int wait = 0; wait < 100; ++wait) realm.step();
        const int mana = realm.hero().mana;
        bool dismissed = false;
        realm.invoke(sim::skill::kSummonGolem, realm.hero().id);
        for (int tick = 0; tick < 60 && !dismissed; ++tick) {
            realm.step();
            for (const sim::Happening& h : realm.happenings()) {
                dismissed |= h.what == sim::What::Dismissed && h.who == golemId;
            }
        }
        check(dismissed && !realm.find(golemId)->alive(), "a second cast dismisses it");
        check(realm.hero().mana >= mana, "and costs nothing");
    }
    // The Orb of Summoning is six orbs by its plus, named for what it raises.
    const int orb = tables.itemAt(12, 11);
    check(orb >= 0, "the Orb of Summoning is cooked");
    if (orb >= 0) {
        const content::ItemRow golemOrb = sim::asRead(tables.items[size_t(orb)], 1);
        check(golemOrb.label == "Orb of Golem" && golemOrb.teaches == sim::skill::kSummonGolem &&
                  golemOrb.teachesEnergy == 60,
              "at +1 it is the Orb of Golem, teaching Summon Stone Golem at 60 energy");
        check(sim::asRead(tables.items[size_t(orb)], 0).label == "Orb of Goblin",
              "and at +0 the Orb of Goblin");
    }
    std::printf("  golem %d health, %d-%d, %d blows, %d killed\n", golem->maxHealth,
                golem->stats.minimumDamage, golem->stats.maximumDamage, blows, kills);
}

// The user, 2026-10-02: "when char is using mount wich is fast summon cant keep up", and "summon
// teleport him self closer to char if he wall behind to much". An elf on Uniria rides forty
// tiles east off the town with her Golem raised: it walks back at her pace (kSummonCatchUp) and
// is never left past kSummonBlink, a teleport putting it behind her when it falls that far.

// The user, 2026-10-02: "he has to teleport to char when char is out of reach". An elf on foot
// walks a long loop through Lorencia's fields among the monsters, the Golem fighting what comes
// at her: it is never more than a tile past kSummonBlink from her, for a single tick.
void testSummonOnAHunt(const content::Tables& tables) {
    std::printf("the summon stays with her on a hunt\n");
    sim::Realm realm;
    realm.raise(&tables, 5, 200, 160, sim::Kin::FairyElf, 60);
    realm.spend(0, 0, 0, 200);
    realm.learn(sim::skill::kSummonGolem);
    for (int wait = 0; wait < 6000 && realm.hero().mana < 70; ++wait) realm.step();
    realm.invoke(sim::skill::kSummonGolem, realm.hero().id);
    const sim::Body* golem = nullptr;
    for (int tick = 0; tick < 60 && !(golem && golem->alive()); ++tick) {
        realm.step();
        golem = realm.summoned();
    }
    check(golem != nullptr, "the Golem stands");
    if (!golem) return;
    const uint32_t id = golem->id;
    const int legs[][2] = {{230, 160}, {230, 200}, {180, 210}, {150, 180}, {170, 130},
                           {220, 120}, {200, 160}, {120, 140}, {136, 143}, {200, 160}};
    float worst = 0.0f;
    int over = 0, longest = 0, run = 0, blinks = 0, refused = 0;
    for (const auto& leg : legs) {
        sim::Request walk;
        walk.kind = sim::Request::Kind::WalkTo;
        walk.column = leg[0];
        walk.row = leg[1];
        realm.ask(walk);
        for (int tick = 0; tick < 400; ++tick) {
            realm.step();
            for (const sim::Happening& h : realm.happenings()) {
                if (h.what == sim::What::Blinked && h.who == id) ++blinks;
                if (h.what == sim::What::Refused && h.who == id) ++refused;
            }
            const sim::Body* g = realm.find(id);
            if (!g || !g->alive()) break;
            const float d = std::max(std::fabs(g->x - realm.hero().x), std::fabs(g->y - realm.hero().y));
            worst = std::max(worst, d);
            if (d > float(sim::kSummonBlink) + 1.0f) {
                ++over; ++run; longest = std::max(longest, run);
            } else run = 0;
            if (!realm.hero().walking && tick > 40) break;
        }
    }
    std::printf("  worst %.1f, %d ticks past the blink, longest %d, %d blinks, %d refused, her health %d\n",
                worst, over, longest, blinks, refused, realm.hero().health);
    check(over == 0, "never left past the blink");
}

void testSummonKeepsUp(const content::Tables& tables) {
    std::printf("the summon keeps up with her ride\n");
    sim::Realm realm;
    realm.raise(&tables, 5, 200, 160, sim::Kin::FairyElf, 40);
    realm.spend(0, 0, 0, 150);
    realm.learn(sim::skill::kSummonGolem);
    const int horn = tables.itemAt(13, 2);
    check(horn >= 0 && realm.give(horn, sim::kMount) >= 0, "she wears the Horn of Uniria");
    for (int wait = 0; wait < 6000 && realm.hero().mana < 70; ++wait) realm.step();
    realm.invoke(sim::skill::kSummonGolem, realm.hero().id);
    const sim::Body* golem = nullptr;
    for (int tick = 0; tick < 60 && !(golem && golem->alive()); ++tick) {
        realm.step();
        golem = realm.summoned();
    }
    check(golem != nullptr && golem->alive(), "the Golem stands beside her");
    if (golem == nullptr || !golem->alive()) return;
    const uint32_t golemId = golem->id;
    sim::Request ride;
    ride.kind = sim::Request::Kind::WalkTo;
    ride.column = realm.hero().column() + 40;
    ride.row = realm.hero().row();
    realm.ask(ride);
    float worst = 0.0f;
    int blinks = 0;
    bool rode = false;
    for (int tick = 0; tick < 200; ++tick) {
        realm.step();
        rode |= realm.hero().riding;
        for (const sim::Happening& h : realm.happenings()) {
            if (h.what == sim::What::Blinked && h.who == golemId) ++blinks;
        }
        const sim::Body* g = realm.find(golemId);
        if (g == nullptr || !g->alive()) break;
        worst = std::max(worst, std::max(std::fabs(g->x - realm.hero().x),
                                         std::fabs(g->y - realm.hero().y)));
    }
    const sim::Body* g = realm.find(golemId);
    const float last = g ? std::max(std::fabs(g->x - realm.hero().x),
                                    std::fabs(g->y - realm.hero().y))
                         : 1e9f;
    std::printf("  rode %.1f tiles; the Golem at most %.2f behind, %d teleports, %.2f at the end\n",
                realm.hero().x - 200.0f, worst, blinks, last);
    check(rode, "she rides");
    check(g != nullptr && g->alive(), "and the Golem goes with her");
    check(worst <= float(sim::kSummonBlink) + 1.5f, "never left past the blink");
    check(last <= float(sim::kSummonTetherFighting) + 1.0f, "and at her side when she stops");
}

// The user, 2026-10-02: "summons should get aggro if he does some damage". A monster on her
// that her summon wounds (a miss draws nothing, as hers does not) turns on the summon and stays there while she shoots it: counted over
// a long hunt, every turn and every blow the monster throws after it.
void testSummonAggro(const content::Tables& tables) {
    std::printf("the summon takes aggro by its blows\n");
    int standColumn = 212, standRow = 198;
    {
        sim::Realm look;
        look.raise(&tables, 5, 212, 198, sim::Kin::FairyElf, 40);
        for (const sim::Body& one : look.bodies()) {
            if (one.monster() && tables.kinds[size_t(one.kind)].number == 14) {
                standColumn = one.homeColumn + 3;
                standRow = one.homeRow;
                break;
            }
        }
    }
    sim::Realm realm;
    check(realm.raise(&tables, 5, standColumn, standRow, sim::Kin::FairyElf, 40),
          "an elf raises by the Skeleton Warriors");
    realm.equip(tables.armNamed("Bow01"), -1, true);
    realm.spend(0, 0, 0, 150);
    realm.learn(sim::skill::kSummonGolem);
    for (int wait = 0; wait < 6000 && realm.hero().mana < 70; ++wait) realm.step();
    realm.invoke(sim::skill::kSummonGolem, realm.hero().id);
    for (int tick = 0; tick < 60; ++tick) realm.step();
    const sim::Body* golem = realm.summoned();
    check(golem != nullptr && golem->alive(), "the golem stands");
    if (golem == nullptr || !golem->alive()) return;
    const uint32_t golemId = golem->id, heroId = realm.hero().id;
    // Each golem blow on a monster that was on her: does the monster's next blow go to the golem?
    int peeled = 0, turned = 0, onHerAfter = 0, onGolemAfter = 0;
    std::vector<uint32_t> peeledIds;
    for (int tick = 0; tick < 6000; ++tick) {
        std::vector<std::pair<uint32_t, uint32_t>> before;
        for (const sim::Body& one : realm.bodies()) {
            if (one.monster() && one.alive()) before.push_back({one.id, one.quarry});
        }
        // She shoots whatever the golem is on, as an elf fighting beside it does.
        if (const sim::Body* g = realm.find(golemId); g && g->alive() && g->quarry != 0 &&
                                                      tick % 25 == 0) {
            sim::Request shoot;
            shoot.kind = sim::Request::Kind::Attack;
            shoot.target = g->quarry;
            realm.ask(shoot);
        }
        realm.step();
        if (!realm.hero().alive()) break;
        for (const sim::Happening& h : realm.happenings()) {
            if (h.who == golemId && h.what == sim::What::Hit) {
                for (const auto& [id, quarry] : before) {
                    if (id != h.whom || quarry != heroId) continue;
                    ++peeled;
                    const sim::Body* one = realm.find(id);
                    if (one && (one->quarry == golemId || !one->alive())) ++turned;
                    peeledIds.push_back(id);
                }
            }
            const bool peeledOne =
                std::find(peeledIds.begin(), peeledIds.end(), h.who) != peeledIds.end();
            if (peeledOne && (h.what == sim::What::Hit || h.what == sim::What::Missed)) {
                if (h.whom == heroId) ++onHerAfter;
                if (h.whom == golemId) ++onGolemAfter;
            }
        }
    }
    std::printf("  %d golem blows on a monster on her, %d turned; their blows after: %d on her, "
                "%d on the golem\n",
                peeled, turned, onHerAfter, onGolemAfter);
    check(peeled > 0, "the golem strikes a monster that was on her");
    check(turned == peeled, "and every one turns on the golem");
    check(onGolemAfter > onHerAfter, "and strikes the golem, not her");

    // And a restart keeps it (the user, the same day: "remember it so after restart it still
    // there how it was before"): recorded standing, restored into a fresh realm, raised on its
    // first tick at the health it was saved with.
    const sim::Body* still = realm.summoned();
    if (still == nullptr || !still->alive()) {
        for (int wait = 0; wait < 6000 && realm.hero().mana < 70; ++wait) realm.step();
        realm.invoke(sim::skill::kSummonGolem, realm.hero().id);
        for (int tick = 0; tick < 60; ++tick) realm.step();
        still = realm.summoned();
    }
    const sim::HeroRecord saved = realm.record();
    checkEqual((long long)saved.summonSkill, (long long)sim::skill::kSummonGolem,
               "the record carries her golem");
    sim::Realm again;
    again.raise(&tables, 5, saved.column, saved.row, sim::Kin::FairyElf, saved.level);
    again.restore(saved);
    check(again.summoned() == nullptr || !again.summoned()->alive(), "not raised by restore");
    again.step();
    bool spawned = false;
    for (const sim::Happening& h : again.happenings()) spawned |= h.what == sim::What::Spawned;
    const sim::Body* back = again.summoned();
    check(back != nullptr && back->alive() && spawned, "but on the first tick, said as Spawned");
    if (back != nullptr && still != nullptr) {
        checkEqual((long long)back->health, (long long)std::min(still->health, back->maxHealth),
                   "at the health it was saved with");
    }
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
    // Level 80, the top of the orb ladder since Rageful Blow's orb moved there (2026-10-02).
    check(realm.raise(&tables, 7, 190, 110, sim::Kin::DarkKnight, 80), "a realm raises for the keys");
    // A blade in his hand, because nothing is thrown bare-handed and `raise` dresses nobody:
    // `given`, so a level-80 knight's strength is not what this is testing.
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

    // Hanzo's counter, and the four orbs on it. This is the route end to end: walk to the
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
    checkEqual(orbs, 4, "the blacksmith stocks the four early orbs");
    checkEqual(read, 4, "and every one of them was bought and read");
    // The later five are not sold (2026-10-02); they drop, and are read here as if found.
    for (int i = 0; i < sim::skillCount(); ++i) {
        const sim::SkillRow& row = sim::skillAt(i);
        if (row.kin == sim::Kin::DarkKnight && !realm.knows(row.number)) realm.learn(row.number);
    }
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
        // Twenty-four: Uppercut's level since the ladder of 2026-10-02, twice its drop level.
        check(young.raise(&tables, 3, 138, 124, sim::Kin::DarkKnight, 24),
              "a knight of twenty-four raises in town");
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
        const int lunge = young.buy(orbOf(sim::skill::kLunge));
        check(uppercut >= 0 && lunge >= 0, "he buys the orb he is ready for and one he is not");
        check(young.useItem(uppercut),
              "twenty-four is enough for Uppercut, which asks for twenty-four");
        check(young.knows(sim::skill::kUppercut), "and he has it");
        check(!young.useItem(lunge), "but Lunge asks for forty and he is twenty-four");
        check(!young.knows(sim::skill::kLunge), "so he has not learned it");
        check(!young.satchel()[lunge].empty(), "and the orb is unspent, waiting for the level");

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
            // int(3.33 + 3) = 6, and the top is a roll (ObjAttack.cpp:3694): int(7.5 + 4) = 11.
            checkEqual(lowest, 6, "a 30-energy Energy Ball rolls from int(30/9 + 3) = 6");
            checkEqual(highest, 11, "to int(30/4 + 3 + 3/2) = 11");
            caster.wizardryRate = 1.0 + 23.0 / 100.0;  // a staff of magic power 46
            int top = 0;
            for (int n = 0; n < 2000; ++n) top = std::max(top, sim::cast(caster, dummy, 3, dice).rolled);
            checkEqual(top, 14, "and a 23-rise staff lifts the top to int(11.5 x 1.23) = 14");
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
        check(mage.raise(&tables, 3, 138, 124, sim::Kin::DarkWizard, 12),
              "a wizard of twelve raises");
        mage.earn(3000000);
        sim::Request talk;
        talk.kind = sim::Request::Kind::Talk;
        talk.target = uint32_t(pasi);
        mage.ask(talk);
        for (int tick = 0; tick < 4000 && mage.trading() < 0; ++tick) mage.step();
        check(mage.trading() == pasi, "walks to Pasi and is served");
        const int bought = mage.buy(scrollSlot);
        check(bought >= 0, "buys the scroll");
        check(mage.useItem(bought), "and reads it at level twelve");
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
            // The elf's buffs ask nothing of either hand (sprint 15).
            if (row.anyHand) {
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
        // Berdysh is a spear whatever its width says, and a bow is a bow (sprint 15: Skillshot's).
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
        checkEqual((long long)familyNamed("Bow01"), (long long)sim::arms::kBow,
                   "and a bow is the elf's own family");
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
    // Out to thirty tiles, as nearestTo: what dies rises elsewhere in its nest.
    const auto hunt = [&](int ticks) {
    for (int tick = 0; tick < ticks; ++tick) {
        const sim::Body& hero = realm.hero();
        if (hero.alive()) {
            uint32_t nearest = 0;
            float closest = 1e30f;
            for (const sim::Body& one : realm.bodies()) {
                if (!one.monster() || !one.alive()) continue;
                const float off = std::max(std::fabs(one.x - hero.x), std::fabs(one.y - hero.y));
                if (off <= 30.0f && off < closest) {
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
    // Asked of the shape and not of Cyclone alone: Twisting Slash is a ring too, and which of the
    // two meets a crowd in this hunt is the dice's.
    int widestRing = 0;
    for (int i = 0; i < sim::skillCount(); ++i) {
        if (sim::skillAt(i).spread == sim::Spread::Ring) widestRing = std::max(widestRing, widest[i]);
    }
    check(widestRing >= 2, "and a ring catches a crowd when there is one");
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

    // Outgrown: a plus that asks more than he has takes the thing off his hand into the bag.
    // The first knight's weapon he wears at some plus and not at the next one.
    {
        sim::Realm grown;
        check(grown.raise(&tables, 13, 138, 124), "a realm raises for an outgrown refinement");
        int found = -1, plus = 0;
        for (size_t i = 0; i < tables.items.size() && found < 0; ++i) {
            const content::ItemRow& row = tables.items[i];
            if (sim::placeOf(row) != sim::kWeaponRight || row.twoHanded()) continue;
            for (int p = 0; p < 5 && found < 0; ++p) {
                if (sim::fits(tables, grown.wearer(), held(int(i), p)) &&
                    !sim::fits(tables, grown.wearer(), held(int(i), p + 1))) {
                    found = int(i);
                    plus = p;
                }
            }
        }
        check(found >= 0, "a weapon he outgrows by one plus");
        if (found >= 0) {
            check(grown.give(found, sim::kWeaponRight, plus) == sim::kWeaponRight, "worn");
            const int gem2 = grown.give(bless);
            check(grown.refine(gem2, sim::kWeaponRight), "the Bless is taken on the worn weapon");
            check(grown.satchel()[sim::kWeaponRight].empty(), "and it comes off his hand");
            int into = -1;
            for (int s = sim::kWorn; s < sim::kSlots; ++s) {
                if (grown.satchel()[s].item == found) into = s;
            }
            check(into >= 0, "into the bag");
            if (into >= 0) checkEqual(int(grown.satchel()[into].refinement), plus + 1, "at its new plus");
            checkEqual(grown.hero().weapon, -1, "and he holds nothing");

            // A full bag: a single Bless in every cell, so there is nowhere for it to come down.
            sim::Realm full;
            check(full.raise(&tables, 13, 138, 124), "a realm raises for a full bag");
            full.give(found, sim::kWeaponRight, plus);
            for (int s = sim::kWorn; s < sim::kSlots; ++s) full.give(bless, s);
            const content::ItemRow& foundRow = tables.items[size_t(found)];
            int cells[sim::kSlots];
            const int count = full.satchel().covered(sim::kWorn, foundRow.width, foundRow.height, cells);
            if (count > 1) {
                check(!full.refine(cells[count - 1], sim::kWeaponRight), "a full bag refuses the jewel");
                checkEqual(int(full.satchel()[sim::kWeaponRight].refinement), plus,
                           "the weapon stays worn at its plus");
                check(!full.satchel()[cells[count - 1]].empty(), "and the jewel is kept");
                // Clear the footprint but the first cell: the last jewel there frees the rest of
                // the room, and the refine goes through into it.
                for (int i = 1; i < count; ++i) full.sell(cells[i]);
                check(full.refine(cells[0], sim::kWeaponRight), "the last jewel's own cell is room");
                check(full.satchel()[sim::kWeaponRight].empty(), "the weapon comes off");
                checkEqual(int(full.satchel()[cells[0]].item), found, "into the jewel's place");
            }
        }
    }

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

    // The jewels by name, as WebZen's CItem::Value (zzzitem.cpp:1787-1806), a third back.
    checkEqual((long long)sim::buyingPrice(tables.items[size_t(tables.itemAt(14, 13))], 0, 1, false),
               9000000ll, "a Bless is 9M");
    checkEqual((long long)sim::buyingPrice(tables.items[size_t(tables.itemAt(14, 14))], 0, 1, false),
               6000000ll, "a Soul is 6M");
    checkEqual((long long)sim::buyingPrice(tables.items[size_t(tables.itemAt(12, 15))], 0, 1, false),
               810000ll, "a Chaos is 810k");
    checkEqual((long long)sim::sellingPrice(tables.items[size_t(tables.itemAt(14, 13))], 0, 1, false),
               3000000ll, "and a Bless sells for 3M");

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

void testSets(const content::Tables& tables) {
    std::printf("sets\n");
    const int leather = tables.itemNamed("ArmorMale01");
    check(leather >= 0, "the leather armour the set tests use exists");
    if (leather < 0) return;
    const int set = tables.items[size_t(leather)].number;
    int piece[5];
    for (int at = 0; at < 5; ++at) piece[at] = tables.itemAt(sim::kGroupHelms + at, set);
    const int stranger = tables.itemAt(sim::kGroupHelms, set == 0 ? 1 : 0);
    check(piece[0] >= 0 && piece[4] >= 0 && stranger >= 0, "a whole set and a stranger's helm exist");
    if (piece[0] < 0 || piece[4] < 0 || stranger < 0) return;

    const auto dressed = [&](sim::Realm& realm, int pieces, uint8_t bits, int helm) {
        check(realm.raise(&tables, 41, 138, 124), "a realm raises");
        for (int at = 0; at < pieces; ++at) {
            realm.give(at == 0 ? helm : piece[at], sim::kHelm + at, 0, -1, false, 0, bits);
        }
    };
    {
        sim::Realm four, five, fine, odd;
        dressed(four, 4, 0, piece[0]);
        dressed(five, 5, 0, piece[0]);
        dressed(fine, 5, 1, piece[0]);
        dressed(odd, 5, 0, stranger);
        checkNear(sim::setDefense(tables, four.satchel()), 0.0, 1e-9, "four pieces are no set");
        checkNear(sim::setDefense(tables, five.satchel()), sim::kSetDefense, 1e-9,
                  "five of one number are a set, 5%");
        checkNear(sim::setDefense(tables, fine.satchel()), sim::kExcellentSetDefense, 1e-9,
                  "five excellent are 10%");
        checkNear(sim::setDefense(tables, odd.satchel()), 0.0, 1e-9, "a stranger's helm breaks it");
    }
    // On the final defence, after the halving, truncated.
    sim::Arms arms;
    arms.armourDefense = 200;
    sim::Fighter bare, whole;
    int health = 0;
    const sim::HeroPoints points = sim::startingPoints(sim::Kin::DarkKnight);
    sim::reckon(sim::Kin::DarkKnight, 1, points, arms, &bare, &health);
    arms.setDefense = sim::kSetDefense;
    sim::reckon(sim::Kin::DarkKnight, 1, points, arms, &whole, &health);
    checkEqual(whole.defense, bare.defense + int(double(bare.defense) * 0.05),
               "a set raises the final defence by 5%");
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

    // A weapon's wear per landed blow, WebZen's: defence x 2 over min + min/2 + option, past 564
    // (zzzitem.cpp:3831-3870). A Short Sword on a Lich's 20 and on a Gorgon's 75.
    {
        const content::ItemRow& sword = tables.items[size_t(tables.itemAt(0, 1))];
        const sim::Held bare{tables.itemAt(0, 1), 0, 20};
        const int divisor = sword.minimumDamage + sword.minimumDamage / 2;
        checkNear(sim::weaponWear(sword, bare, 20), double(40 / divisor) / 564.0, 1e-12,
                  "a sword on a Lich wears 40/(min x 1.5) of 564");
        check(sim::weaponWear(sword, bare, 75) > sim::weaponWear(sword, bare, 20),
              "and harder on a thicker hide");
        check(sim::weaponWear(sword, bare, 75) > 100.0 / sim::kHitsPerDurability,
              "a hundred times OpenMU's on the Dungeon's");
        checkEqual(sim::dropRateOf(25).maxPlus, 3, "the Ice Queen drops nothing past +3");
        checkEqual(sim::dropRateOf(3).moneyRate, 10, "a Spider leaves Zen on every itemless kill");
        checkEqual(sim::dropRateOf(3).regen, 5, "a Spider's RegTime is five seconds");
        checkEqual(sim::dropRateOf(25).regen, 10, "the Ice Queen's ten");
    }

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

// Devias's townsfolk (2026-09-29): Version075's nine, Apostle Devin, Sevina, the Messenger and Thompson, the three shelves, Zienna's
// counter, and the Guild Master answering with a line where MU opens a guild window.
// A monster's poison on him is 0.75's, one at a time (the user, 2026-10-01: "poison damage from
// monsters seems to overpowered"): a bite while one is on neither adds to it nor starts it
// again, and every pulse takes one share of what he has left.
void testPoisonOnce() {
    std::printf("poison once\n");
    content::Tables dungeon;
    std::string error;
    const std::string path = std::string(MU2_ASSET_DIR) + "/cooked/dungeon/dungeon.mur";
    check(content::loadTables(path, dungeon, error), "the Dungeon's tables load");
    sim::Realm realm;
    check(realm.raise(&dungeon, 7, 119, 47, sim::Kin::DarkKnight, 40), "a knight among the bulls");
    int pulses = 0, overShare = 0, restarted = 0;
    for (int tick = 0; tick < 6000 && realm.hero().alive(); ++tick) {
        if (realm.hero().health < realm.hero().maxHealth / 2) {
            sim::HeroRecord record = realm.record();
            record.health = realm.hero().maxHealth;
            realm.restore(record);
        }
        const int before = realm.hero().health;
        const int64_t until = realm.hero().poisonUntil;
        const bool on = until > realm.tick();
        realm.step();
        if (on && realm.hero().poisonUntil > until) ++restarted;
        for (const sim::Happening& one : realm.happenings()) {
            if (one.what != sim::What::Hit || one.whom != realm.hero().id || !one.poisoned) continue;
            ++pulses;
            if (one.a > std::max(1, int(float(before) * sim::kHeroPoisonShare))) ++overShare;
        }
    }
    std::printf("  poison: %d pulses, %d over one share, %d restarted while on\n", pulses,
                overShare, restarted);
    check(pulses > 0, "the bulls poison him");
    check(overShare == 0, "and a pulse never bites more than one share");
    check(restarted == 0, "and a bite while one is on does not start it again");
}

// A skill pressed late in a cast and then walked away from is not thrown after the walk (the
// user, 2026-09-30: "when i am done with casting spell, and move on it still is casted"): the
// order that moves him drops the wish still waiting.
void testWishDropsOnWalk(const content::Tables& tables) {
    std::printf("a wish and a walk\n");
    sim::Realm wiz;
    check(wiz.raise(&tables, 7, 190, 110, sim::Kin::DarkWizard, 40), "a wizard raises");
    check(wiz.learn(sim::skill::kLightning), "who knows Lightning");
    const auto fill = [&] {
        sim::HeroRecord record = wiz.record();
        record.mana = 100000;
        wiz.restore(record);
    };
    fill();
    // At the nearest monster, which is what the key names in play.
    uint32_t prey = 0;
    float nearest = 1e9f;
    for (const sim::Body& b : wiz.bodies()) {
        if (!b.monster() || !b.alive()) continue;
        const float d = std::hypot(b.x - wiz.hero().x, b.y - wiz.hero().y);
        if (d < nearest) {
            nearest = d;
            prey = b.id;
        }
    }
    sim::Request fight;
    fight.kind = sim::Request::Kind::Attack;
    fight.target = prey;
    wiz.ask(fight);
    wiz.invoke(sim::skill::kLightning, prey);
    bool cast = false;
    for (int t = 0; t < 600 && !cast; ++t) {
        if (t % 20 == 0) wiz.invoke(sim::skill::kLightning, prey);  // as he closes in
        wiz.step();
        for (const sim::Happening& one : wiz.happenings()) {
            cast |= one.what == sim::What::Cast && one.a == sim::skill::kLightning;
        }
    }
    check(cast && wiz.casting(), "he channels Lightning");
    // Pressed again near the channel's end, so the wish outlives it.
    while (wiz.casting() && wiz.hero().castUntil - wiz.tick() > 10) wiz.step();
    fill();
    wiz.invoke(sim::skill::kLightning, prey);
    while (wiz.casting()) wiz.step();
    sim::Request go;
    go.kind = sim::Request::Kind::WalkTo;
    go.column = wiz.hero().column() + 6;
    go.row = wiz.hero().row();
    wiz.ask(go);
    int again = 0;
    for (int t = 0; t < 40; ++t) {
        wiz.step();
        for (const sim::Happening& one : wiz.happenings()) {
            if (one.what == sim::What::Cast && one.a == sim::skill::kLightning) ++again;
        }
    }
    checkEqual(again, 0, "and a walk ordered as it ends drops the press he made during it");
}

// A Dungeon floor's trip waits on its link of the Golden Archer's chain (ours, the user,
// 2026-09-30): refused for the quest until the link is taken, then not for that.
void testTravelQuestLock() {
    std::printf("travel and the chain\n");
    content::Tables dungeon;
    std::string error;
    const std::string path = std::string(MU2_ASSET_DIR) + "/cooked/dungeon/dungeon.mur";
    check(content::loadTables(path, dungeon, error), "the Dungeon's tables load");
    sim::Realm realm;
    check(realm.raise(&dungeon, 7, 108, 247, sim::Kin::DarkKnight, 60), "a knight in the Dungeon");
    realm.earn(1000000);
    int second = -1, seen = 0;
    for (int i = 0; i < sim::kTravels; ++i) {
        if (sim::travelAt(i).map == 1 && seen++ == 1) second = i;
    }
    check(second >= 0, "the Dungeon has a second floor's row");
    if (second < 0) return;
    const int link = realm.travelQuest(second);
    check(link >= 0 && sim::questAt(link).giver == 236, "which waits on a Golden Archer link");
    checkEqual(int(realm.travelRefusal(second)), int(sim::TravelRefusal::Quest),
               "refused before the link is taken");
    sim::HeroRecord record = realm.record();
    record.quests[link].state = sim::QuestState::Active;
    realm.restore(record);
    check(realm.travelRefusal(second) != sim::TravelRefusal::Quest, "and not for it once taken");

    // The map he is on is a trip too (2026-10-02): the floor he stands on sets him at its landing.
    int first = -1;
    for (int i = 0; i < sim::kTravels && first < 0; ++i) {
        if (sim::travelAt(i).map == 1) first = i;
    }
    if (const int q = realm.travelQuest(first); q >= 0) {
        record = realm.record();
        record.quests[q].state = sim::QuestState::Active;
        realm.restore(record);
    }
    realm.setHeroDown(150, 150, 1, 0);
    checkEqual(int(realm.travelRefusal(first)), int(sim::TravelRefusal::None),
               "the floor he is on is not refused");
    check(realm.travel(first), "and is travelled to");
    const int dc = realm.hero().column() - sim::travelAt(first).column;
    const int dr = realm.hero().row() - sim::travelAt(first).row;
    check(dc * dc + dr * dr <= 64, "set down at its landing");

    // But not from inside the town he is in (the user, 2026-10-02): Lorencia's own row is refused
    // in its safe zone and taken from its field.
    content::Tables lorencia;
    check(content::loadTables(std::string(MU2_ASSET_DIR) + "/cooked/lorencia/lorencia.mur",
                              lorencia, error), "Lorencia's tables load");
    sim::Realm town;
    check(town.raise(&lorencia, 7, 190, 110, sim::Kin::DarkKnight, 60), "a knight in Lorencia");
    town.earn(1000000);
    int home = -1;
    for (int i = 0; i < sim::kTravels && home < 0; ++i) {
        if (sim::travelAt(i).map == 0) home = i;
    }
    town.setHeroDown(sim::travelAt(home).column, sim::travelAt(home).row, 1, 0);
    check(lorencia.grid.safe(town.hero().column(), town.hero().row()), "its landing is in town");
    checkEqual(int(town.travelRefusal(home)), int(sim::TravelRefusal::Here),
               "Lorencia is refused from inside Lorencia's safe zone");
    int fc = -1, fr = -1;
    for (int r = 20; r < 236 && fc < 0; ++r) {
        for (int c = 20; c < 236 && fc < 0; ++c) {
            if (!lorencia.grid.safe(c, r) && lorencia.grid.open(c, r)) fc = c, fr = r;
        }
    }
    town.setHeroDown(fc, fr, 1, 0);
    check(!lorencia.grid.safe(town.hero().column(), town.hero().row()), "he stands in the field");
    checkEqual(int(town.travelRefusal(home)), int(sim::TravelRefusal::None),
               "and Lorencia is taken from its field");
}

// Nothing through a wall (the user, 2026-10-02: "dont allow to cast multi-shot or other class
// skills throught walls"). An elf hunts the Lost Tower's first floor, all corridors, on Skillshot
// and then on her plain shot, at whatever monster is nearest, wall or not. Each time she looses,
// which monsters she could see is written down; an arrow that then lands on one she could not see
// in any of her last few looses went through stone. And she still hunts: walking to a tile in
// sight rather than standing at range behind the wall.
void testThroughWalls() {
    std::printf("nothing through walls\n");
    content::Tables tower;
    std::string error;
    check(content::loadTables(std::string(MU2_ASSET_DIR) + "/cooked/losttower/losttower.mur",
                              tower, error),
          "the Lost Tower's tables load");
    if (tower.items.empty()) return;
    sim::Router sight;
    sight.open(&tower.grid);
    const auto sees = [&](float x, float y, const sim::Body& to) {
        return (std::fabs(to.x - x) <= sim::kArmsLength && std::fabs(to.y - y) <= sim::kArmsLength) ||
               sight.sees(x, y, to.x, to.y, content::kWallNoMove);
    };
    // Where she starts: the open tile with the most monsters within six tiles that a wall hides,
    // over the floor as the seed lays it out.
    int startColumn = 204, startRow = 76, most = -1;
    {
        sim::Realm laid;
        laid.raise(&tower, 5, 204, 76, sim::Kin::FairyElf, 350);
        for (int r = 0; r < tower.grid.size(); r += 2) {
            for (int c = 0; c < tower.grid.size(); c += 2) {
                if (!tower.grid.open(c, r) || tower.grid.safe(c, r)) continue;
                int hidden = 0, near = 0;
                for (const sim::Body& one : laid.bodies()) {
                    if (!one.monster() || std::hypot(one.x - c, one.y - r) > 6.0f) continue;
                    ++near;
                    hidden += sees(float(c), float(r), one) ? 0 : 1;
                }
                if (near <= 5 && hidden > most) {
                    most = hidden;
                    startColumn = c;
                    startRow = r;
                }
            }
        }
    }
    for (const int32_t skill : {sim::skill::kSkillshot, sim::skill::kNone}) {
        sim::Realm realm;
        check(realm.raise(&tower, 5, startColumn, startRow, sim::Kin::FairyElf, 350),
              "an elf among the tower's walls");
        check(realm.equip(tower.armNamed("Bow05"), -1, true), "with the Tiger Bow and a quiver");
        // Her points in agility and vitality: the test is the stone, not whether she lives.
        const int spare = realm.hero().pointsInHand;
        realm.spend(0, spare / 2, spare - spare / 2, 0);
        if (skill != sim::skill::kNone) check(realm.learn(skill), "and Skillshot");
        std::vector<std::vector<uint32_t>> seenAt;  // the last few looses' monsters in sight
        int looses = 0, landed = 0, walled = 0, aimedHidden = 0;
        for (int tick = 0; tick < 6000 && realm.hero().alive(); ++tick) {
            if (tick % 10 == 0) {
                // The nearest she cannot see within a bow's reach, on purpose; else the nearest.
                uint32_t nearest = 0;
                float closest = 1e30f;
                bool hidden = false;
                for (const sim::Body& one : realm.bodies()) {
                    if (!one.monster() || !one.alive()) continue;
                    const float d = std::hypot(one.x - realm.hero().x, one.y - realm.hero().y);
                    const bool behind = d <= 6.0f && !sees(realm.hero().x, realm.hero().y, one);
                    if ((behind && !hidden) || (behind == hidden && d < closest)) {
                        closest = d;
                        nearest = one.id;
                        hidden = behind;
                    }
                }
                sim::Request request;
                request.kind = sim::Request::Kind::Attack;
                request.target = nearest;
                request.skill = skill;
                if (nearest != 0) realm.ask(request);
                aimedHidden += hidden ? 1 : 0;
            }
            realm.step();
            const sim::Body& hero = realm.hero();
            for (const sim::Happening& h : realm.happenings()) {
                if (h.who != hero.id) continue;
                if (h.what == sim::What::Loosed && h.a == skill) {
                    ++looses;
                    std::vector<uint32_t> visible;
                    for (const sim::Body& one : realm.bodies()) {
                        if (one.monster() && one.alive() && sees(hero.x, hero.y, one)) {
                            visible.push_back(one.id);
                        }
                    }
                    seenAt.push_back(visible);
                    if (seenAt.size() > 4) seenAt.erase(seenAt.begin());
                }
                if (h.what == sim::What::Hit && h.thrown) {
                    ++landed;
                    bool was = false;
                    for (const auto& visible : seenAt) {
                        was |= std::find(visible.begin(), visible.end(), h.whom) != visible.end();
                    }
                    walled += was ? 0 : 1;
                }
            }
            // A full quiver whenever it runs low: this is about the stone, not the arrows.
            if (realm.satchel()[sim::kWeaponLeft].durability < 20) {
                realm.equip(tower.armNamed("Bow05"), -1, true);
            }
        }
        // With Realm::seen answering yes to everything, 58 of Skillshot's and 75 plain arrows
        // landed through stone here (2026-10-02).
        std::printf("  %s: %d looses, %d landed, %d aimed behind a wall, %d through one\n",
                    skill ? "Skillshot" : "plain shot", looses, landed, aimedHidden, walled);
        check(landed > 20, "she hunts the corridors");
        check(aimedHidden > 0, "and is sent at monsters behind walls");
        checkEqual(walled, 0, "and no arrow lands on a monster she could not see");
    }
}

// The Lost Tower's way in (docs/lost-tower-quest.md, 2026-10-01): Devin's hand-in sends the hero
// to Tersia in the tower's hall; her chain of seven floors waits on Devin's, speaking to her
// opens the hall's row, and each deeper floor's row waits on its link handed in.
// Thompson in Devias only answers with a line.
void testTowerKeeper() {
    std::printf("the tower's keeper\n");
    content::Tables devias, tower;
    std::string error;
    check(content::loadTables(std::string(MU2_ASSET_DIR) + "/cooked/devias/devias.mur", devias,
                              error),
          "Devias's tables load");
    check(content::loadTables(std::string(MU2_ASSET_DIR) + "/cooked/losttower/losttower.mur",
                              tower, error),
          "the Lost Tower's tables load");
    const int shrine = sim::questOf(sim::kTersia);
    check(shrine >= 0, "Tersia gives a quest");
    checkEqual(sim::questOf(sim::kThompson), -1, "Thompson gives none");
    if (shrine < 0) return;
    check(sim::questAt(shrine).afterAny == (1u << 2), "hers waits on Devin's");
    int links = 0, unmade = 0, chained = 0;
    for (int q = 0; q < sim::kQuests; ++q) {
        const sim::QuestRow& row = sim::questAt(q);
        if (row.giver != sim::kTersia) continue;
        if (links > 0) chained += row.afterAny == (1u << (q - 1)) ? 1 : 0;
        ++links;
        for (int i = 0; i < row.paidCount; ++i) unmade += tower.itemNamed(row.paid[i].item) < 0;
    }
    checkEqual(links, 7, "seven links, a floor each");
    checkEqual(chained, 6, "each after the one before");
    checkEqual(unmade, 0, "and every thing she pays is a cooked item");
    // Links 2-7's first clears (2026-10-02): every class a rune it may set, in what that link pays
    // it where the link pays a socketed piece; a repeat pays neither.
    const int creationRow = tower.itemNamed("Jewel22");
    for (int link = 1; link < 7 && creationRow >= 0; ++link) {
        const sim::QuestRow& row = sim::questAt(shrine + link);
        for (int kin = 0; kin < 3; ++kin) {
            int runes = 0, fits = 0, pieces = 0, again = 0;
            for (int i = 0; i < row.paidCount; ++i) {
                const sim::QuestItem& rune = row.paid[i];
                if (rune.item == std::string("Jewel22") || rune.sockets > 0) {
                    again += sim::questPays(rune, kin, false) ? 1 : 0;
                }
                if (!sim::questPays(rune, kin, true)) continue;
                pieces += rune.sockets > 0 ? 1 : 0;
                const sim::PowerRow* power = sim::powerOf(rune.power);
                if (!power) continue;
                ++runes;
                check(power->takenBy(sim::Kin(kin)), "her rune is one its class may set");
                sim::Held jewel{int32_t(creationRow), 0, 1};
                jewel.powers[0] = rune.power;
                for (int j = 0; j < row.paidCount; ++j) {
                    const sim::QuestItem& piece = row.paid[j];
                    if (piece.sockets == 0 || !sim::questPays(piece, kin, true)) continue;
                    sim::Held target{int32_t(tower.itemNamed(piece.item)), 0, 1};
                    target.sockets = piece.sockets;
                    fits += sim::settable(tower, jewel, target, sim::Kin(kin)) ? 1 : 0;
                }
            }
            check(runes >= 1, "each of her links pays every class a rune");
            check(pieces == 0 || fits >= 1, "and it goes in the piece that link pays");
            checkEqual(again, 0, "a repeat pays neither");
        }
    }
    const auto folkOf = [](const content::Tables& tables, int number) {
        for (size_t i = 0; i < tables.folk.size(); ++i) {
            if (tables.folk[i].number == number) return int(i);
        }
        return -1;
    };
    const int tersia = folkOf(tower, sim::kTersia);
    checkEqual(folkOf(devias, sim::kThompson), -1, "Thompson is off the map (2026-10-02)");
    check(tersia >= 0, "Tersia stands in the Lost Tower");
    if (tersia < 0) return;
    // Spoken to: a dialog opened, or a line said.
    const auto talkTo = [](sim::Realm& realm, int folk, bool* offered, bool* greeted) {
        sim::Request talk;
        talk.kind = sim::Request::Kind::Talk;
        talk.target = uint32_t(folk);
        realm.ask(talk);
        *offered = *greeted = false;
        for (int tick = 0; tick < 400 && !*offered && !*greeted; ++tick) {
            realm.step();
            for (const sim::Happening& one : realm.happenings()) {
                *offered |= one.what == sim::What::Offered;
                *greeted |= one.what == sim::What::Shouted &&
                            one.a == int32_t(sim::Shout::Greet) && one.c == folk;
            }
        }
    };

    bool offered = false, greeted = false;

    int hall = -1;
    for (int i = 0; i < sim::kTravels; ++i) {
        if (sim::travelAt(i).map == 4 && hall < 0) hall = i;
    }
    const uint32_t deeper = sim::travelRowsOf(4) & ~(uint32_t(1) << hall);
    const content::Townsperson& l = tower.folk[size_t(tersia)];
    sim::Realm early;
    check(early.raise(&tower, 7, l.x, l.y + 2, sim::Kin::DarkKnight, 45), "a knight by Tersia");
    check(((early.found() >> hall) & 1u) == 0, "standing in the hall does not open its row");
    check(early.questLocked(shrine), "before Devin's is handed in, her quest waits");
    talkTo(early, tersia, &offered, &greeted);
    check(greeted && !offered, "and she answers with a line");
    check(((early.found() >> hall) & 1u) != 0, "but speaking to her opens the hall's row");
    check(early.travelQuest(hall) < 0, "which waits on no link");
    int shut = 0;
    for (int i = 0; i < sim::kTravels; ++i) {
        if (((deeper >> i) & 1u) && early.travelRefusal(i) == sim::TravelRefusal::Quest) ++shut;
    }
    checkEqual(shut, 6, "and the six floors past it wait on their links");

    sim::HeroRecord record = early.record();
    record.quests[2].state = sim::QuestState::Resting;
    record.quests[2].completions = 1;
    sim::Realm later;
    check(later.raise(&tower, 7, l.x, l.y + 2, sim::Kin::DarkKnight, 45), "and again, Devias cleared");
    later.restore(record);
    check(!later.questLocked(shrine) && later.questOffered(shrine), "her quest is offered");
    talkTo(later, tersia, &offered, &greeted);
    check(offered && !greeted, "her dialog opens");
    check(later.acceptQuest(shrine), "and it is taken");
    checkEqual(int(later.quest(shrine).state), int(sim::QuestState::Active),
               "under way: the first floor's two breeds");
    const int second = hall + 1;
    check(later.travelQuest(second) == shrine + 1, "the second floor's row waits on the second link");
    sim::HeroRecord on = later.record();
    on.quests[shrine + 1].state = sim::QuestState::Active;
    later.restore(on);
    checkEqual(int(later.travelRefusal(second)), int(sim::TravelRefusal::Quest),
               "and stays shut while it is only taken");
    on.quests[shrine + 1].state = sim::QuestState::Resting;
    on.quests[shrine + 1].completions = 1;
    later.restore(on);
    check(later.travelRefusal(second) != sim::TravelRefusal::Quest, "and opens handed in");
}

void testDeviasFolk() {
    std::printf("devias folk\n");
    content::Tables devias;
    std::string error;
    const std::string path = std::string(MU2_ASSET_DIR) + "/cooked/devias/devias.mur";
    check(content::loadTables(path, devias, error), "Devias's tables load");
    checkEqual(long(devias.folk.size()), 12L, "twelve townsfolk stand in Devias (Thompson off since 2026-10-02)");
    int master = -1;
    for (size_t i = 0; i < devias.folk.size(); ++i) {
        if (devias.folk[i].number == sim::kGuildMaster) master = int(i);
    }
    check(master >= 0, "the Guild Master is in the table");
    for (const int npc : {244, 245, 246}) {
        int stocked = 0, made = 0;
        const sim::Offer* offers = sim::stockOf(npc, &stocked);
        for (int i = 0; i < stocked; ++i) made += devias.itemAt(offers[i].group, offers[i].number) >= 0;
        check(made > 0, "Caren, Izabel and Zienna each have something on the shelf");
    }
    check(sim::repairsAt(246), "Zienna mends");
    const int white = sim::questOf(406);
    check(white >= 0, "Apostle Devin gives a quest");
    if (white >= 0) {
        const sim::QuestRow& row = sim::questAt(white);
        int unmade = 0;
        for (int i = 0; i < row.paidCount; ++i) unmade += devias.itemNamed(row.paid[i].item) < 0;
        checkEqual((long long)unmade, 0LL, "and every thing he pays is a cooked item");

        // His quest waits on Lorencia's or Noria's (the user, 2026-09-30): before either is
        // handed in he answers with a line; after, his dialog opens.
        int devin = -1;
        for (size_t i = 0; i < devias.folk.size(); ++i) {
            if (devias.folk[i].number == 406) devin = int(i);
        }
        const auto talkTo = [&](sim::Realm& realm, bool* offered, bool* greeted) {
            sim::Request talk;
            talk.kind = sim::Request::Kind::Talk;
            talk.target = uint32_t(devin);
            realm.ask(talk);
            *offered = *greeted = false;
            for (int tick = 0; tick < 400 && !*offered && !*greeted; ++tick) {
                realm.step();
                for (const sim::Happening& one : realm.happenings()) {
                    *offered |= one.what == sim::What::Offered;
                    *greeted |= one.what == sim::What::Shouted &&
                                one.a == int32_t(sim::Shout::Greet) && one.c == devin;
                }
            }
        };
        sim::Realm early;
        check(early.raise(&devias, 7, 183, 37), "a realm raises by Devin");
        check(early.questLocked(white) && !early.questOffered(white),
              "his quest is locked before Lorencia or Noria is cleared");
        bool offered = false, greeted = false;
        talkTo(early, &offered, &greeted);
        check(greeted && !offered, "and he answers with a line, no dialog");
        sim::HeroRecord cleared = early.record();
        cleared.quests[0].state = sim::QuestState::Resting;
        cleared.quests[0].completions = 1;
        sim::Realm later;
        check(later.raise(&devias, 7, 183, 37), "a realm raises by Devin again");
        later.restore(cleared);
        check(!later.questLocked(white) && later.questOffered(white),
              "once Marlon's is handed in, it is offered");
        talkTo(later, &offered, &greeted);
        check(offered && !greeted, "and his dialog opens");
    }
    if (master < 0) return;

    sim::Realm realm;
    check(realm.raise(&devias, 7, 213, 47), "a realm raises by the Guild Master");
    sim::Request talk;
    talk.kind = sim::Request::Kind::Talk;
    talk.target = uint32_t(master);
    realm.ask(talk);
    bool greeted = false;
    for (int tick = 0; tick < 400 && !greeted; ++tick) {
        realm.step();
        for (const sim::Happening& one : realm.happenings()) {
            greeted |= one.what == sim::What::Shouted && one.a == int32_t(sim::Shout::Greet) &&
                       one.c == master;
        }
    }
    check(greeted, "walked to the Guild Master and he answered");
    check(realm.trading() < 0 && realm.banking() < 0, "and opened nothing");

    // The Messenger of Archangel: spoken to with nothing, he answers.
    int messenger = -1;
    for (size_t i = 0; i < devias.folk.size(); ++i) {
        if (devias.folk[i].number == sim::kMessenger) messenger = int(i);
    }
    check(messenger >= 0, "the Messenger of Archangel is in the table");
    if (messenger < 0) return;
    sim::Realm gate;
    check(gate.raise(&devias, 7, 220, 27), "a realm raises by the Messenger");
    talk.target = uint32_t(messenger);
    gate.ask(talk);
    bool answered = false;
    for (int tick = 0; tick < 400 && !answered; ++tick) {
        gate.step();
        for (const sim::Happening& one : gate.happenings()) {
            answered |= one.what == sim::What::Shouted && one.a == int32_t(sim::Shout::Greet) &&
                        one.c == messenger;
        }
    }
    check(answered, "walked to the Messenger and he answered");

    // His door (sim/event.h): a cloak, the hour's entry and castle 1's band, or why not.
    const int32_t cloak = devias.itemAt(13, 18);
    check(cloak >= 0, "the Invisibility Cloak is in Devias's tables");
    if (cloak < 0) return;
    const auto at = [](int hour, int minute) {
        const time_t now = std::time(nullptr);
        struct tm local {};
        localtime_r(&now, &local);
        local.tm_hour = hour;
        local.tm_min = minute;
        local.tm_sec = 0;
        return int64_t(std::mktime(&local));
    };
    // Spoken to with the bag as `plus` (-1 none), at that time, at that level: the refusal's
    // reason, or -1 when he went through, with the gate and landing checked.
    const auto ask = [&](int plus, int64_t wall, int level) {
        sim::Realm r;
        r.raise(&devias, 7, 220, 27, sim::Kin::DarkKnight, level);
        if (plus >= 0) r.give(cloak, -1, plus);
        r.setWallClock(wall);
        talk.target = uint32_t(messenger);
        r.ask(talk);
        for (int tick = 0; tick < 400; ++tick) {
            r.step();
            for (const sim::Happening& one : r.happenings()) {
                if (one.what == sim::What::Shouted && one.a == int32_t(sim::Shout::Greet)) {
                    return int(one.b);
                }
                if (one.what == sim::What::Gated) {
                    const sim::EnterGate* in = sim::enterGateNumbered(one.a);
                    const sim::ExitGate* out = in ? sim::exitGate(in->target) : nullptr;
                    check(out && out->map == sim::kBloodCastleMap && out->box.holds(one.b, one.c),
                          "through the Messenger's door into Blood Castle 1's court");
                    bool kept = false;
                    for (int i = sim::kWorn; i < sim::kSlots; ++i) kept |= r.satchel()[i].item == cloak;
                    check(!kept, "and the cloak is spent");
                    return -1;
                }
            }
        }
        return -2;
    };
    checkEqual(ask(-1, at(10, 26), 50), int(sim::CastleRefusal::NoCloak), "no cloak, no door");
    checkEqual(ask(1, at(10, 10), 50), int(sim::CastleRefusal::NotYet), "a cloak at hh:10 waits");
    checkEqual(ask(1, 0, 50), int(sim::CastleRefusal::NotYet), "and with no clock at all");
    checkEqual(ask(2, at(10, 26), 50), int(sim::CastleRefusal::NotBuilt), "a +2 cloak's castle is not built");
    checkEqual(ask(1, at(10, 26), 10), int(sim::CastleRefusal::TooLow), "level 10 is under the band");
    checkEqual(ask(1, at(10, 26), 81), int(sim::CastleRefusal::TooHigh), "level 81 over it");
    checkEqual(ask(1, at(10, 26), 50), -1, "a +1 cloak at hh:26, level 50, goes in");
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
    // A guard is a warden body with a guard's row: Marlon on his rounds is one without.
    const auto isGuard = [&](const sim::Body& one) {
        return one.warden >= 0 && sim::wardenRow(tables.folk[size_t(one.warden)].number) != nullptr;
    };
    int guards = 0;
    for (const sim::Body& one : realm.bodies()) guards += isGuard(one) ? 1 : 0;
    // Six town guards and, since 2026-09-30, the Golden Archer at the Dungeon's arch.
    checkEqual(guards, 7, "Lorencia stands its six guards and the Golden Archer");

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
            if (!isGuard(one)) continue;
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
        if (!isGuard(one) || one.quarry != 0) continue;
        home &= one.column() == one.homeColumn && one.row() == one.homeRow && !one.walking;
    }
    check(home, "and every guard with nothing to fight is back at his post");
}

// The Golden Archer one-shots what comes to the Dungeon's arch, and keeps facing it, crossbow
// drawn, until his next shot would be due -- not turning his back on it the tick after (the
// user, 2026-10-01: "not facing the monster which he one shot ... holding bow when he shoots").
void testArcherHolds(const content::Tables& tables) {
    std::printf("the Golden Archer holds his shot\n");
    sim::Realm realm;
    check(realm.raise(&tables, 11, 127, 228), "a knight raises beside the Dungeon's arch");
    const sim::WardenRow* row = sim::wardenRow(236);
    uint32_t archer = 0;
    for (const sim::Body& one : realm.bodies()) {
        if (one.warden >= 0 && tables.folk[size_t(one.warden)].number == 236) archer = one.id;
    }
    check(archer != 0 && row != nullptr, "the Golden Archer stands at his post");
    if (archer == 0 || row == nullptr) return;
    int kills = 0, held = 0, turned = 0, slung = 0;
    int64_t killedAt = -1;
    float killAim = 0.0f;
    for (int tick = 0; tick < 6000; ++tick) {
        realm.step();
        const sim::Body& him = *realm.find(archer);
        for (const sim::Happening& one : realm.happenings()) {
            if (one.what != sim::What::Died || one.whom != archer) continue;
            ++kills;
            killedAt = tick;
            killAim = him.aim;
        }
        // Over the ticks after a kill, while he has taken nothing else on.
        if (killedAt >= 0 && tick > killedAt && tick < killedAt + row->attackTicks - 1 &&
            him.quarry == 0) {
            ++held;
            if (him.aim != killAim) ++turned;
            if (him.temper != sim::Temper::Fighting) ++slung;
        }
    }
    std::printf("  %d kills, %d ticks after one held\n", kills, held);
    check(kills > 0, "he shoots what comes to the arch");
    check(held > 0, "and stands after a kill with nothing else on him");
    checkEqual(turned, 0, "facing what he shot");
    checkEqual(slung, 0, "his crossbow still drawn");
}

// Marlon's rounds (realm_folk.cpp): from his spot to the tavern's bench, where he sits, to the two
// gate guards, who salute him, and home. The hero talking to him stops him where he stands for
// the talk and the quest window, and he goes on with his rounds after.
void testStrollers(const content::Tables& tables) {
    std::printf("strollers\n");
    sim::Realm realm;
    check(realm.raise(&tables, 3, 135, 124), "a knight raises in Lorencia's square");
    int folk = -1;
    for (size_t i = 0; i < tables.folk.size(); ++i) {
        if (tables.folk[i].number == 229) folk = int(i);
    }
    uint32_t marlon = 0;
    for (const sim::Body& one : realm.bodies()) {
        if (one.warden == folk) marlon = one.id;
    }
    check(folk >= 0 && marlon != 0, "Marlon stands as a body");
    if (marlon == 0) return;
    int lumen = -1;
    for (size_t i = 0; i < tables.folk.size(); ++i) {
        if (tables.folk[i].number == 255) lumen = int(i);
    }
    int sat = 0, salutes = 0, offered = 0, firstRound = 0, overTheCounter = 0;
    bool stillForTheTalk = true;
    int talkColumn = -1, talkRow = -1;
    for (int tick = 0; tick < 6000; ++tick) {
        // Mid-talk, the hero goes to her counter, and leaves it five seconds after it opens.
        if (tick == 1000) {
            sim::Request request;
            request.kind = sim::Request::Kind::Talk;
            request.target = uint32_t(lumen);
            realm.ask(request);
        }
        static int openedAt = -1;
        if (realm.trading() == lumen && openedAt < 0) openedAt = tick;
        if (openedAt >= 0 && tick == openedAt + 100) realm.closeTrade();
        if (tick == 2400) {
            sim::Request request;
            request.kind = sim::Request::Kind::Talk;
            request.target = uint32_t(folk);
            realm.ask(request);
        }
        if (tick == 2700) realm.closeQuest();
        realm.step();
        const sim::Body* him = realm.find(marlon);
        if (tick == 2401) {
            talkColumn = him->column();
            talkRow = him->row();
        }
        // Held from the click through the window and its moment after.
        if (tick > 2401 && tick < 2700) {
            stillForTheTalk &= him->column() == talkColumn && him->row() == talkRow;
        }
        for (const sim::Happening& one : realm.happenings()) {
            if (one.what == sim::What::Posed && one.who == marlon &&
                one.a == int32_t(sim::Pose::Sitting)) {
                ++sat;
            }
            if (one.what == sim::What::Shouted && one.a == int32_t(sim::Shout::Salute) &&
                one.whom == marlon) {
                ++salutes;
            }
            if (one.what == sim::What::Offered) ++offered;
            if (one.what == sim::What::Shouted && one.a == int32_t(sim::Shout::Chat)) {
                if (tick < 2400) ++firstRound;
                if (realm.trading() == lumen) ++overTheCounter;
            }
        }
    }
    std::printf("  sat %d times, saluted %d times, his window opened %d times\n", sat, salutes,
                offered);
    check(sat >= 2, "he sits at the tavern's bench each round");
    check(salutes >= 4, "and both gate guards salute him each round");
    check(offered == 1, "the hero's talk reaches him wherever he is on his rounds");
    check(stillForTheTalk, "and he stands where he was for the talk and the window");
    checkEqual(firstRound, sim::kChatLines, "he and Lumen say their whole talk at her bar");
    checkEqual(overTheCounter, 0, "and none of it while the hero is at her counter");
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

// Lorencia's gate 23 to Noria (sim/gates.h): a level 10 hero walked into it goes through to a
// tile of Noria's gate 24 and stops; a level 1 hero is told it asks level 10 and does not.
// The Dungeon's gates (sim/gates.cpp, docs/dungeon-port.md): Lorencia's stair down and the way
// back to the arch, and a stair between floors -- one map, so the realm puts him down in place
// (What::Climbed) and the tables stay the Dungeon's.
// The Dungeon's traps (sim/traps.h), each kind on its own rule: the hero stood in its reach is
// caught within two of its seconds and wounded, and stood out of it is not. The spot is found,
// not assumed: a trap's reach is a line of tiles and many of them are walls, so the first open
// tile in reach is where he stands.
void testTraps() {
    std::printf("the Dungeon's traps\n");
    content::Tables dungeon;
    std::string error;
    const std::string path = std::string(MU2_ASSET_DIR) + "/cooked/dungeon/dungeon.mur";
    check(content::loadTables(path, dungeon, error), "the Dungeon's tables load");
    size_t count = 0;
    const sim::TrapSpot* spots = sim::trapSpots(&count);
    // The Dungeon's own; the table carries the Lost Tower's 148 Meteorite Traps too.
    int64_t inDungeon = 0, inTower = 0;
    for (size_t i = 0; i < count; ++i) {
        if (spots[i].map == 1) ++inDungeon;
        if (spots[i].map == 4 && spots[i].number == 103) ++inTower;
    }
    checkEqual(inDungeon, 58, "OpenMU's 58 trap spots");
    checkEqual(inTower, 148, "and the Lost Tower's 148 Meteorite Traps");
    {
        sim::Realm realm;
        check(realm.raise(&dungeon, 7, 108, 246), "a realm raises in the Dungeon");
        checkEqual(int64_t(realm.traps().size()), 58, "and raises every trap");
    }
    check(sim::octantOf(0, -1) == sim::octantOf(0, -3) && sim::octantOf(0, -1) != sim::octantOf(0, 1),
          "a direction is an octant: along it agrees, against it does not");

    // Stood at (column, row), how many times in 50 ticks the trap at `index` caught him, and
    // whether he stood where asked (the realm puts him on the nearest open tile otherwise).
    const auto standAt = [&](int column, int row, size_t index, bool* stood, int* hurt) {
        sim::Realm realm;
        realm.raise(&dungeon, 7, column, row, sim::Kin::DarkKnight, 60);
        *stood = realm.hero().column() == column && realm.hero().row() == row;
        const int before = realm.hero().health + realm.hero().sd;
        int caught = 0;
        for (int tick = 0; tick < 50; ++tick) {
            realm.step();
            for (const sim::Happening& one : realm.happenings()) {
                if (one.what == sim::What::Trapped && size_t(one.b) == index) ++caught;
            }
        }
        *hurt = before - (realm.hero().health + realm.hero().sd);
        return caught;
    };

    // One trap of each kind with an open tile in its reach, tried in the table's order.
    const struct { int32_t number; const char* name; } kinds[] = {
        {101, "Iron Stick"}, {102, "Fire Trap"}, {100, "Lance Trap"}};
    for (const auto& want : kinds) {
        const sim::TrapKind* kind = sim::trapKind(want.number);
        bool tested = false;
        int reachable = 0;
        for (size_t i = 0; i < count; ++i) {
            const sim::TrapSpot& spot = spots[i];
            if (spot.number != want.number) continue;
            const int reach = kind->pressed ? 0 : kind->attackRange;
            bool found = false;
            for (int step = kind->pressed ? 0 : 1; step <= reach && !found; ++step) {
                const int column = spot.column + spot.dx * step, row = spot.row + spot.dy * step;
                bool stood = false;
                int hurt = 0;
                const int caught = standAt(column, row, i, &stood, &hurt);
                if (!stood) continue;
                found = true;
                ++reachable;
                if (tested) continue;
                tested = true;
                std::printf("  %s at %d,%d caught him %d time(s) at %d,%d for %d\n", want.name,
                            spot.column, spot.row, caught, column, row, hurt);
                check(caught >= 1, (std::string(want.name) + " catches him in its reach").c_str());
                check(hurt > 0, (std::string(want.name) + " wounds him").c_str());
                // And behind it, the way it does not face: never.
                const int backColumn = spot.column - spot.dx * std::max(step, 1);
                const int backRow = spot.row - spot.dy * std::max(step, 1);
                bool backStood = false;
                int backHurt = 0;
                const int back = standAt(backColumn, backRow, i, &backStood, &backHurt);
                if (backStood) {
                    checkEqual(back, 0, (std::string(want.name) + " leaves him be behind it").c_str());
                }
            }
        }
        std::printf("  %s: %d of its spots can reach a standing hero\n", want.name, reachable);
        check(tested, (std::string(want.name) + " has a spot with an open tile in reach").c_str());
    }
}

void testDungeonGates(const content::Tables& lorencia) {
    std::printf("dungeon gates\n");
    struct Seen { int gated = 0, climbed = 0, barred = 0, column = 0, row = 0; };
    const auto walk = [](const content::Tables& tables, int fromC, int fromR, int toC, int toR,
                         int level) {
        Seen seen;
        sim::Realm realm;
        check(realm.raise(&tables, 7, fromC, fromR, sim::Kin::DarkKnight, level),
              "a realm raises by the gate");
        sim::Request go;
        go.kind = sim::Request::Kind::WalkTo;
        go.column = toC;
        go.row = toR;
        realm.ask(go);
        for (int tick = 0; tick < 400 && seen.gated == 0 && seen.climbed == 0; ++tick) {
            realm.step();
            for (const sim::Happening& one : realm.happenings()) {
                if (one.who != realm.hero().id) continue;
                if (one.what == sim::What::Gated) seen.gated = one.a;
                if (one.what == sim::What::Climbed) seen.climbed = one.a;
                if (one.what == sim::What::Barred && seen.barred == 0) seen.barred = one.b;
                if (one.what == sim::What::Gated || one.what == sim::What::Climbed) {
                    seen.column = one.b;
                    seen.row = one.c;
                }
            }
        }
        if (seen.climbed != 0) {
            checkEqual(realm.hero().column(), seen.column, "he stands where the stair said");
            checkEqual(realm.hero().row(), seen.row, "on its row too");
        }
        return seen;
    };
    Seen s = walk(lorencia, 122, 229, 122, 233, 20);
    checkEqual(s.gated, 1, "a level 20 knight takes Lorencia's stair down, gate 1");
    check(s.column >= 107 && s.column <= 110 && s.row == 247, "out on the Dungeon's gate 2");
    s = walk(lorencia, 122, 229, 122, 233, 19);
    checkEqual(s.gated, 0, "a level 19 knight does not");
    checkEqual(s.barred, 20, "and is told it asks level 20");

    content::Tables dungeon;
    std::string error;
    const std::string path = std::string(MU2_ASSET_DIR) + "/cooked/dungeon/dungeon.mur";
    check(content::loadTables(path, dungeon, error), "the Dungeon's tables load");
    s = walk(dungeon, 108, 246, 108, 248, 1);
    checkEqual(s.gated, 3, "the way out, gate 3, asks nothing");
    check(s.column >= 121 && s.column <= 123 && s.row == 231, "out in front of Lorencia's arch");
    s = walk(dungeon, 242, 152, 239, 150, 20);
    checkEqual(s.climbed, 5, "gate 5 is a stair down, taken in place");
    checkEqual(s.gated, 0, "and not a map change");
    check(s.column >= 228 && s.column <= 237 && s.row >= 123 && s.row <= 130,
          "he is put down at Dungeon 2's gate 6");
    check(!(s.column >= 232 && s.column <= 233 && s.row >= 127 && s.row <= 128),
          "and not inside gate 7 beside it");
    s = walk(dungeon, 242, 152, 239, 150, 19);
    checkEqual(s.climbed, 0, "a level 19 knight is not");
    checkEqual(s.barred, 20, "and is told it asks level 20");

    // The Dungeon's perches: MOVEMENT_OPERATE's WD_1DUNGEON arm, 59 sits and 60 leans
    // (ZzzInterface.cpp:1705-1712). The seat at (244, 145) in the dragon room, walked to and sat on.
    checkEqual(int64_t(dungeon.perches.size()), 39, "the Dungeon has 30 seats and 9 lean boxes");
    int seat = -1;
    for (size_t i = 0; i < dungeon.perches.size(); ++i) {
        const content::Perch& one = dungeon.perches[i];
        if (one.column == 244 && one.row == 145) seat = int(i);
    }
    check(seat >= 0 && dungeon.perches[size_t(seat)].pose == uint8_t(sim::Pose::Sitting),
          "the dragon room's seat at (244, 145) sits");
    if (seat >= 0) {
        sim::Realm realm;
        check(realm.raise(&dungeon, 7, 243, 147, sim::Kin::DarkKnight, 20),
              "a realm raises in the dragon room");
        sim::Request sit;
        sit.kind = sim::Request::Kind::Perch;
        sit.target = seat;
        realm.ask(sit);
        for (int tick = 0; tick < 100 && realm.hero().pose == sim::Pose::Standing; ++tick)
            realm.step();
        check(realm.hero().pose == sim::Pose::Sitting, "he sits on it");
        check(realm.hero().column() == 244 && realm.hero().row() == 145, "on its own tile");
    }

    // A Town Portal read in the Dungeon owes him Lorencia: the map has no spawn gate, so
    // OpenMU's safe-zone map is Lorencia's (BaseMapInitializer.cs:91). The realm spends it and
    // says so with `c`; the map change is the mode's.
    {
        sim::Realm realm;
        check(realm.raise(&dungeon, 7, 243, 147, sim::Kin::DarkKnight, 20),
              "a realm raises in the dragon room");
        const int scroll = dungeon.itemAt(14, 10);
        const int slot = scroll >= 0 ? realm.give(scroll) : -1;
        check(slot >= 0 && realm.useItem(slot), "a Town Portal Scroll is read in the Dungeon");
        const std::vector<sim::Happening>& said = realm.happenings();
        check(!said.empty() && said.back().what == sim::What::Warped && said.back().c == 1,
              "and he is owed Lorencia");
        check(slot >= 0 && realm.satchel()[slot].empty(), "and the scroll is spent");
    }
}

void testGates(const content::Tables& tables) {
    std::printf("gates\n");
    const auto walkIn = [&](int level, int* gate, int* column, int* row, int* barredAt) {
        sim::Realm realm;
        check(realm.raise(&tables, 7, 215, 238, sim::Kin::DarkKnight, level),
              "a realm raises above Lorencia's south gate");
        sim::Request walk;
        walk.kind = sim::Request::Kind::WalkTo;
        walk.column = 215;
        walk.row = 247;
        realm.ask(walk);
        *gate = 0;
        *barredAt = 0;
        for (int tick = 0; tick < 400 && *gate == 0; ++tick) {
            realm.step();
            for (const sim::Happening& one : realm.happenings()) {
                if (one.who != realm.hero().id) continue;
                if (one.what == sim::What::Gated) {
                    *gate = one.a;
                    *column = one.b;
                    *row = one.c;
                }
                if (one.what == sim::What::Barred && *barredAt == 0) *barredAt = one.b;
            }
        }
        return !realm.hero().walking;
    };
    int gate = 0, column = 0, row = 0, barred = 0;
    const bool stopped = walkIn(10, &gate, &column, &row, &barred);
    checkEqual(gate, 23, "a level 10 knight goes through gate 23");
    check(column >= 148 && column <= 155 && row >= 5 && row <= 6,
          "and comes out on a tile of Noria's gate 24");
    check(stopped, "and stops at the gate");
    walkIn(1, &gate, &column, &row, &barred);
    checkEqual(gate, 0, "a level 1 knight does not");
    checkEqual(barred, 10, "and is told the gate asks level 10");

    // And back: from where gate 24 put him, up into Noria's gate 25, out on Lorencia's 26.
    content::Tables noria;
    std::string error;
    const std::string path = std::string(MU2_ASSET_DIR) + "/cooked/noria/noria.mur";
    check(content::loadTables(path, noria, error), "Noria's tables load");
    sim::Realm realm;
    check(realm.raise(&noria, 7, 151, 6, sim::Kin::DarkKnight, 10), "a realm raises in Noria");
    sim::Request walk;
    walk.kind = sim::Request::Kind::WalkTo;
    walk.column = 151;
    walk.row = 3;
    realm.ask(walk);
    gate = 0;
    for (int tick = 0; tick < 400 && gate == 0; ++tick) {
        realm.step();
        for (const sim::Happening& one : realm.happenings()) {
            if (one.who == realm.hero().id && one.what == sim::What::Gated) {
                gate = one.a;
                column = one.b;
                row = one.c;
            }
        }
    }
    checkEqual(gate, 25, "from Noria he goes through gate 25");
    check(column >= 213 && column <= 217 && row >= 244 && row <= 245,
          "and comes out on a tile of Lorencia's gate 26");
}

// Charon (2026-09-30): Devil Square is not written, so he answers that it is not ready.
void testCharon() {
    std::printf("charon\n");
    content::Tables noria;
    std::string error;
    const std::string path = std::string(MU2_ASSET_DIR) + "/cooked/noria/noria.mur";
    check(content::loadTables(path, noria, error), "Noria's tables load");
    int charon = -1;
    for (size_t i = 0; i < noria.folk.size(); ++i) {
        if (noria.folk[i].number == sim::kCharon) charon = int(i);
    }
    check(charon >= 0, "Charon is in Noria's table");
    if (charon < 0) return;
    sim::Realm square;
    check(square.raise(&noria, 7, 175, 107), "a realm raises by Charon");
    sim::Request talk;
    talk.kind = sim::Request::Kind::Talk;
    talk.target = uint32_t(charon);
    square.ask(talk);
    bool answered = false;
    for (int tick = 0; tick < 400 && !answered; ++tick) {
        square.step();
        for (const sim::Happening& one : square.happenings()) {
            answered |= one.what == sim::What::Shouted && one.a == int32_t(sim::Shout::Greet) &&
                        one.c == charon;
        }
    }
    check(answered, "walked to Charon and he said Devil Square is not ready");
}

// The Chaos Machine (sim/machine.h): the Goblin opens it, the judge names the box, +10 and +11
// raise past the jewels' cap or take the thing, and closing hands the box back to the bag.
void testChaosMachine() {
    std::printf("chaos machine\n");
    content::Tables noria;
    std::string error;
    const std::string path = std::string(MU2_ASSET_DIR) + "/cooked/noria/noria.mur";
    check(content::loadTables(path, noria, error), "Noria's tables load");
    int goblin = -1;
    for (size_t i = 0; i < noria.folk.size(); ++i) {
        if (noria.folk[i].number == sim::kChaosGoblin) goblin = int(i);
    }
    check(goblin >= 0, "the Chaos Goblin is in Noria's table");
    if (goblin < 0) return;
    const int32_t chaos = noria.itemAt(12, 15), bless = noria.itemAt(14, 13),
                  soul = noria.itemAt(14, 14);
    int32_t sword = -1;
    for (size_t i = 0; i < noria.items.size() && sword < 0; ++i) {
        if (noria.items[i].group == 0 && noria.items[i].width == 1) sword = int32_t(i);
    }
    check(chaos >= 0 && bless >= 0 && soul >= 0 && sword >= 0, "the three jewels and a sword");
    if (chaos < 0 || bless < 0 || soul < 0 || sword < 0) return;

    sim::Realm realm;
    check(realm.raise(&noria, 7, 182, 105, sim::Kin::DarkKnight, 50), "a realm raises by him");
    sim::Request talk;
    talk.kind = sim::Request::Kind::Talk;
    talk.target = uint32_t(goblin);
    realm.ask(talk);
    for (int tick = 0; tick < 400 && realm.mixing() < 0; ++tick) realm.step();
    check(realm.mixing() == goblin, "talking to the Goblin opens his machine");
    check(realm.judged().empty && realm.judged().recipe == sim::Recipe::None, "an empty box is nothing");
    realm.earn(50000000);

    // Loads a +10 or +11 box: the thing at `plus` and `jewels` of the Bless and the Soul.
    const auto load = [&](int plus, int jewels, bool luck) {
        check(realm.putIn(realm.give(sword, -1, plus, -1, luck)) >= 0, "the thing goes in");
        check(realm.putIn(realm.give(chaos)) >= 0, "a Chaos goes in");
        for (int i = 0; i < jewels; ++i) {
            check(realm.putIn(realm.give(bless)) >= 0 && realm.putIn(realm.give(soul)) >= 0,
                  "a Bless and a Soul go in");
        }
    };
    load(9, 1, false);
    sim::Judged j = realm.judged();
    check(j.recipe == sim::Recipe::PlusTen, "+9, a Chaos, a Bless and a Soul are +10");
    checkEqual(j.rate, 50, "at 50%");
    checkEqual(int(j.zen), 2000000, "for two million");
    check(realm.putIn(realm.give(chaos)) >= 0, "a second Chaos goes in");
    check(realm.judged().recipe == sim::Recipe::None, "and the box is no recipe with it");
    check(!realm.mix(), "so the Goblin will not run it");
    for (int cell = 0; cell < sim::kMachineCells; ++cell) {
        if (!realm.machine()[cell].empty() && realm.machine()[cell].item == chaos) {
            const int back = realm.takeOut(cell);
            check(back >= 0, "the Chaos comes back out");
            realm.discard(back);  // so the next Chaos given is a stack of one
            break;
        }
    }
    // Run until it makes one, every failure taking the thing and every jewel.
    bool made = false;
    for (int attempt = 0; attempt < 40 && !made; ++attempt) {
        if (attempt > 0) load(9, 1, false);
        const int64_t before = realm.money();
        check(realm.mix(), "the Goblin runs a +10 box");
        checkEqual(int(before - realm.money()), 2000000, "and takes his two million");
        made = realm.mixed();
        int left = 0, at = -1;
        for (int cell = 0; cell < sim::kMachineCells; ++cell) {
            if (!realm.machine()[cell].empty()) ++left, at = cell;
        }
        if (made) {
            checkEqual(left, 1, "a +10 leaves the thing alone in the box");
            checkEqual(int(realm.machine()[at].refinement), 10, "at +10, past the jewels' nine");
            check(realm.putIn(realm.give(chaos)) < 0, "and the box is locked while it stands");
            check(realm.takeOut(at) >= 0 && !realm.mixed(), "taken out, the box is his again");
        } else {
            checkEqual(left, 0, "a failed +10 takes the thing and the jewels");
        }
    }
    check(made, "forty tries make a +10");

    load(10, 2, true);
    j = realm.judged();
    check(j.recipe == sim::Recipe::PlusEleven, "+10 and two of each are +11");
    checkEqual(j.rate, 65, "at 45% and twenty more for luck, WebZen's");
    checkEqual(int(j.zen), 4000000, "for four million");

    // The Dinorant: ten whole Horns of Uniria and a Chaos, 70%, half a million.
    {
        sim::Machine horns;
        const int32_t uniria = noria.itemAt(13, 2);
        check(uniria >= 0, "the Horn of Uniria is in Noria's tables");
        if (uniria >= 0) {
            const content::ItemRow& row = noria.items[size_t(uniria)];
            sim::Held whole{uniria, 0, int16_t(sim::maximumDurability(row, sim::Held{uniria, 0, 0}))};
            for (int i = 0; i < sim::kDinorantHorns; ++i) horns.put(i, whole);
            horns.put(16, sim::Held{chaos, 0, 1});
            sim::Judged d = sim::judge(noria, horns);
            check(d.recipe == sim::Recipe::Dinorant, "ten horns and a Chaos are a Dinorant");
            checkEqual(d.rate, 70, "at 70%");
            checkEqual(int(d.zen), 500000, "for half a million");
            sim::Held tired = whole;
            tired.durability = int16_t(whole.durability - 1);
            horns.put(9, tired);
            d = sim::judge(noria, horns);
            check(d.recipe == sim::Recipe::None && d.nearest == sim::Recipe::Dinorant,
                  "a horn short of its life spoils it, and the box still looks like one");
        }
    }

    // Blood Castle's ticket: a Scroll of Archangel and a Blood Bone of one level and a Chaos,
    // 80%, 50,000 Zen at +1 (WebZen BloodCastle.cpp:1294-1577).
    {
        const int32_t scroll = noria.itemAt(13, 16), bone = noria.itemAt(13, 17);
        const int32_t cloak = noria.itemAt(13, 18);
        check(scroll >= 0 && bone >= 0 && cloak >= 0, "the scroll, the bone and the cloak are in the tables");
        if (scroll >= 0 && bone >= 0 && cloak >= 0) {
            sim::Machine box;
            box.put(0, sim::Held{scroll, 1, 128});
            box.put(1, sim::Held{bone, 1, 128});
            box.put(2, sim::Held{chaos, 0, 1});
            sim::Judged c = sim::judge(noria, box);
            check(c.recipe == sim::Recipe::Cloak, "a scroll, a bone and a Chaos are a cloak");
            checkEqual(c.rate, 80, "at 80%");
            checkEqual(int(c.zen), 50000, "for 50,000 at +1");
            box.put(1, sim::Held{bone, 3, 128});
            c = sim::judge(noria, box);
            check(c.recipe == sim::Recipe::None && c.nearest == sim::Recipe::Cloak,
                  "a +3 bone with a +1 scroll is not ready, and still looks like a cloak");
            box.put(0, sim::Held{scroll, 3, 128});
            checkEqual(int(sim::judge(noria, box).zen), 150000, "+3 for 150,000");
            box.put(4, sim::Held{bless, 0, 1});
            check(sim::judge(noria, box).recipe == sim::Recipe::None, "a Bless spoils it");
        }
        checkEqual(sim::castleMaterialLevel(31), 1, "a level 31 monster drops +1");
        checkEqual(sim::castleMaterialLevel(32), 2, "a level 32 one +2");
        checkEqual(sim::castleMaterialLevel(83), 6, "83 +6");
        checkEqual(sim::castleMaterialLevel(84), 7, "84 +7, which no box takes");
        // Run by the Goblin until one is made: the box goes either way.
        for (int cell = 0; cell < sim::kMachineCells; ++cell) {
            if (!realm.machine()[cell].empty()) realm.takeOut(cell);
        }
        bool cloaked = false;
        for (int attempt = 0; attempt < 20 && !cloaked && scroll >= 0; ++attempt) {
            check(realm.putIn(realm.give(scroll, -1, 1)) >= 0 && realm.putIn(realm.give(bone, -1, 1)) >= 0 &&
                      realm.putIn(realm.give(chaos)) >= 0,
                  "the scroll, the bone and a Chaos go in");
            const int64_t before = realm.money();
            check(realm.mix(), "the Goblin runs the cloak");
            checkEqual(int(before - realm.money()), 50000, "and takes 50,000");
            int left = 0, at = -1;
            for (int cell = 0; cell < sim::kMachineCells; ++cell) {
                if (!realm.machine()[cell].empty()) ++left, at = cell;
            }
            cloaked = realm.mixed();
            if (cloaked) {
                checkEqual(left, 1, "a cloak alone in the box");
                check(realm.machine()[at].item == cloak, "and it is the Invisibility Cloak");
                checkEqual(int(realm.machine()[at].refinement), 1, "of the scroll's level, +1");
                realm.takeOut(at);
            } else {
                checkEqual(left, 0, "a failure takes the box");
            }
        }
        check(cloaked, "twenty tries make a cloak");
    }

    // The Chaos Weapon runs now its three answers are cooked: one roll, either outcome checked.
    {
        for (int cell = 0; cell < sim::kMachineCells; ++cell) {
            if (!realm.machine()[cell].empty()) realm.takeOut(cell);
        }
        check(realm.machine().empty(), "the box is emptied first");
        check(realm.putIn(realm.give(sword, -1, 4, -1, false, 2)) >= 0 &&
                  realm.putIn(realm.give(chaos)) >= 0 && realm.putIn(realm.give(bless)) >= 0 &&
                  realm.putIn(realm.give(soul)) >= 0,
              "a +4 with an option and three jewels go in");
        check(realm.judged().recipe == sim::Recipe::ChaosWeapon, "the box is a Chaos Weapon");
        check(realm.mix(), "and the Goblin runs it");
        const auto& said = realm.happenings();
        const bool madeOne = !said.empty() && said.back().what == sim::What::Mixed && said.back().b == 1;
        int weapons = 0, jewels = 0;
        for (int cell = 0; cell < sim::kMachineCells; ++cell) {
            const sim::Held& h = realm.machine()[cell];
            if (h.empty()) continue;
            const content::ItemRow& r = noria.items[size_t(h.item)];
            if ((r.group == 2 && r.number == 6) || (r.group == 4 && r.number == 6) ||
                (r.group == 5 && r.number == 7)) {
                ++weapons;
                check(h.refinement >= 0 && h.refinement <= 4, "made at +0 to +4");
            }
            if (sim::jewelOfChaos(r) || sim::jewelOfBless(r) || sim::jewelOfSoul(r)) ++jewels;
        }
        checkEqual(jewels, 0, "the jewels are spent either way");
        checkEqual(weapons, madeOne ? 1 : 0, "a success leaves one Chaos weapon, a failure none");
        for (int cell = 0; cell < sim::kMachineCells; ++cell) {
            if (!realm.machine()[cell].empty()) realm.takeOut(cell);
        }
    }

    // Walking off hands the box back.
    sim::Request walk;
    walk.kind = sim::Request::Kind::WalkTo;
    walk.column = 178;
    walk.row = 105;
    realm.ask(walk);
    realm.step();
    check(realm.mixing() < 0 && realm.machine().empty(), "walking off closes it, the box in the bag");

    // The Chaos Weapon's box: a +4 with an option and a Chaos, its rate the box's old price.
    sim::Machine box;
    sim::Held optioned{sword, 4, 20};
    optioned.option = 1;
    box.put(0, optioned);
    box.put(8, sim::Held{chaos, 0, 1});
    j = sim::judge(noria, box);
    check(j.recipe == sim::Recipe::ChaosWeapon, "a +4 with an option and a Chaos is a Chaos Weapon");
    const int64_t worth = sim::mixValue(noria, optioned) + 40000;
    checkEqual(j.rate, int(std::min<int64_t>(100, worth / 20000)), "its rate is the old price over 20,000");
    checkEqual(int(j.zen), j.rate * 10000, "and ten thousand Zen a percent");
    box.put(9, sim::Held{bless, 0, 1});
    check(sim::judge(noria, box).rate > j.rate, "a Bless raises it");
    sim::Held plain{sword, 4, 20};
    box.put(1, plain);
    check(sim::judge(noria, box).recipe == sim::Recipe::None, "a thing with no option spoils it");
    check(sim::judge(noria, box).nearest == sim::Recipe::None, "and nothing is like it");

    // The rune services, at the Goblin again.
    const int32_t rune = noria.itemAt(14, 22);
    check(rune >= 0, "the Rune of Creation is in Noria's tables");
    if (rune < 0) return;
    realm.ask(talk);
    for (int tick = 0; tick < 400 && realm.mixing() < 0; ++tick) realm.step();
    check(realm.mixing() == goblin, "the Goblin opens his machine again");
    // Remove Rune: a sword with Stormcall in its second socket and a Chaos.
    const uint8_t set[3] = {0, uint8_t(sim::Power::Stormcall), 0};
    check(realm.putIn(realm.give(sword, -1, 3, -1, false, 0, 0, 2, set)) >= 0, "a runed sword goes in");
    check(realm.putIn(realm.give(chaos)) >= 0, "a Chaos goes in");
    j = realm.judged(sim::Service::RemoveRune);
    check(j.ready && j.socket == 1, "Remove Rune is ready and picks the socket with the rune");
    checkEqual(j.rate, 100, "it cannot fail");
    check(!realm.judged(sim::Service::Combine).ready, "and the same box is no Combine");
    const int64_t before = realm.money();
    check(realm.mix(sim::Service::RemoveRune, 1), "the Goblin removes it");
    checkEqual(int(before - realm.money()), int(j.zen), "for its rarity's Zen");
    int freed = -1, emptied = -1;
    for (int cell = 0; cell < sim::kMachineCells; ++cell) {
        const sim::Held& h = realm.machine()[cell];
        if (h.empty()) continue;
        if (h.item == rune && h.powers[0] == uint8_t(sim::Power::Stormcall)) freed = cell;
        if (h.item == sword) emptied = cell;
    }
    check(freed >= 0, "Stormcall comes back as a rune");
    check(emptied >= 0 && realm.machine()[emptied].powers[1] == 0 &&
              realm.machine()[emptied].sockets == 2,
          "and the sword keeps both sockets, empty");
    check(realm.takeOut(freed) >= 0 && realm.takeOut(emptied) >= 0 && realm.machine().empty(),
          "both come out");

    // Add Socket: a sword with none, a Chaos and a Soul, until it takes.
    bool socketed = false;
    for (int attempt = 0; attempt < 30 && !socketed; ++attempt) {
        check(realm.putIn(realm.give(sword, -1, 0)) >= 0 && realm.putIn(realm.give(chaos)) >= 0 &&
                  realm.putIn(realm.give(soul)) >= 0,
              "a sword, a Chaos and a Soul go in");
        j = realm.judged(sim::Service::AddSocket);
        check(j.ready, "Add Socket is ready");
        checkEqual(j.rate, sim::kAddSocketRate[0], "at the first socket's rate");
        check(realm.mix(sim::Service::AddSocket), "the Goblin tries");
        int left = 0, at = -1;
        for (int cell = 0; cell < sim::kMachineCells; ++cell) {
            if (!realm.machine()[cell].empty()) ++left, at = cell;
        }
        checkEqual(left, 1, "only the sword is left, made or not");
        socketed = realm.machine()[at].sockets == 1;
        realm.takeOut(at);
    }
    check(socketed, "thirty tries put a socket in");

    // Fuse Runes: three rares and a Chaos make an epic.
    int rares[3] = {}, found = 0;
    for (int p = 1; sim::powerOf(uint8_t(p)) && found < 3; ++p) {
        if (sim::powerOf(uint8_t(p))->rarity == sim::Rarity::Rare) rares[found++] = p;
    }
    check(found == 3, "three rare runes to fuse");
    for (int i = 0; i < 3; ++i) {
        const uint8_t power[1] = {uint8_t(rares[i])};
        check(realm.putIn(realm.give(rune, -1, 0, 1, false, 0, 0, 0, power)) >= 0, "a rare rune goes in");
    }
    check(!realm.judged(sim::Service::FuseRunes).ready, "not without a Chaos");
    check(realm.putIn(realm.give(chaos)) >= 0, "a Chaos goes in");
    check(realm.judged(sim::Service::FuseRunes).ready, "three rares and a Chaos are ready");
    check(realm.mix(sim::Service::FuseRunes), "the Goblin fuses them");
    const sim::PowerRow* fused = sim::powerOf(realm.machine()[0].powers[0]);
    check(realm.machine()[0].item == rune && fused && fused->rarity == sim::Rarity::Epic,
          "into one epic rune");
}

// Lorencia's one quest (sim/quests.h): Marlon offers it, a kill of his own counts, a hand-in pays
// only what his class may take and only into room, and twelve hours of wall clock bring it back.
void testQuests(const content::Tables& tables) {
    sim::Realm realm;
    check(realm.raise(&tables, 11, 131, 128, sim::Kin::DarkKnight, 60), "a quest realm raises");
    int marlon = -1;
    for (size_t i = 0; i < tables.folk.size(); ++i) {
        if (tables.folk[i].number == 229) marlon = int(i);
    }
    check(marlon >= 0, "Marlon is in the town's table");
    const int quest = sim::questOf(229);
    checkEqual(quest, 0, "and he gives the first quest");
    int goal = 0;
    for (int s = 0; s < sim::questAt(quest).stepCount; ++s) {
        if (sim::questAt(quest).steps[s].kind == sim::QuestStepKind::Clear) {
            goal += realm.questGoal(quest, s);
        }
    }
    checkEqual(goal, 210, "clearing Lorencia is the ladder's 210 kills");
    realm.setWallClock(1000000);
    check(realm.questOffered(quest), "never taken, it is offered");
    check(!realm.acceptQuest(quest), "but not from across the map");
    const auto talk = [&]() {
        sim::Request ask;
        ask.kind = sim::Request::Kind::Talk;
        ask.target = uint32_t(marlon);
        realm.ask(ask);
        for (int tick = 0; tick < 4000 && realm.questing() < 0; ++tick) realm.step();
    };
    talk();
    checkEqual(realm.questing(), marlon, "walked to Marlon and his window opened");
    check(realm.acceptQuest(quest), "and the quest is taken");
    check(realm.quest(quest).state == sim::QuestState::Active, "which is under way");
    check(!realm.questOffered(quest), "and no longer offered");

    // One kill of his own, of the nearest Spider (breed 3), counts once toward its step.
    const sim::Body* spider = nullptr;
    float best = 1e9f;
    for (const sim::Body& one : realm.bodies()) {
        if (!one.monster() || !one.alive() || tables.kinds[size_t(one.kind)].number != 3) continue;
        const float dx = one.x - realm.hero().x, dy = one.y - realm.hero().y;
        if (dx * dx + dy * dy < best) {
            best = dx * dx + dy * dy;
            spider = &one;
        }
    }
    check(spider != nullptr, "a Spider stands somewhere in Lorencia");
    if (spider) {
        const uint32_t id = spider->id;
        sim::Request attack;
        attack.kind = sim::Request::Kind::Attack;
        attack.target = id;
        realm.ask(attack);
        for (int tick = 0; tick < 6000; ++tick) {
            realm.step();
            const sim::Body* one = realm.find(id);
            if (!one || !one->alive()) break;
        }
        checkEqual(int(realm.quest(quest).counts[0]), 1, "the Spider he killed counted once");
    }

    // Ready, as a finished clear leaves it, and handed in.
    sim::HeroRecord record = realm.record();
    record.quests[quest].state = sim::QuestState::Ready;
    for (int s = 0; s < sim::kQuestSteps; ++s) {
        record.quests[quest].counts[s] = uint16_t(realm.questGoal(quest, s));
    }
    // Handed in at level 10, where the first clear's 100,000 is several levels (at 60 it is not
    // one): what makes the drawing rise once a level, in a row.
    record.level = 10;
    record.experience = sim::neededExperience(10);
    realm.restore(record);
    talk();
    checkEqual(realm.questing(), marlon, "back at Marlon with the clear done");
    // The knight's reward, and no choice: a lucky Falchion with a socket, a Rune of Creation
    // carrying Stormcall, three Jewels of Bless, twenty large potions and the purse.
    const auto tally = [&](const sim::Realm& r, bool* falchion, int* runes, int* bless, int* potions) {
        const int32_t sword = tables.itemNamed("Sword08"), rune = tables.itemNamed("Jewel22");
        const int32_t jewel = tables.itemNamed("Jewel01"), potion = tables.itemNamed("Potion04");
        *falchion = false;
        *runes = *bless = *potions = 0;
        for (int slot = 0; slot < sim::kSlots; ++slot) {
            const sim::Held& one = r.satchel()[slot];
            if (one.item == sword && one.luck && one.option == sim::kQuestOption &&
                one.sockets == 1 && one.powers[0] == 0) {
                *falchion = true;
            }
            if (one.item == rune && one.powers[0] == uint8_t(sim::Power::Stormcall)) ++*runes;
            if (one.item == jewel) *bless += sim::stacks(tables.items[size_t(jewel)]) ? one.durability : 1;
            if (one.item == potion) *potions += one.durability;
        }
    };
    bool falchion = false;
    int runes = 0, bless = 0, potions = 0;
    tally(realm, &falchion, &runes, &bless, &potions);
    const int blessBefore = bless, potionsBefore = potions;
    const int64_t purse = realm.money();
    const int levelBefore = realm.hero().level;
    const size_t saidBefore = realm.happenings().size();
    check(realm.completeQuest(quest, -1), "handed in with no choice to make");
    checkEqual(realm.money() - purse, int64_t(50000), "and the purse is paid");
    // The first clear's experience is paid between ticks, and each level it carries is said
    // there, once a level -- Play::completeQuest reads them off before the next step clears them.
    const auto levelsSaid = [&](size_t from) {
        int said = 0;
        for (size_t i = from; i < realm.happenings().size(); ++i) {
            const sim::Happening& one = realm.happenings()[i];
            said += one.what == sim::What::Levelled && one.who == realm.hero().id ? 1 : 0;
        }
        return said;
    };
    check(realm.hero().level - levelBefore >= 2, "the first clear carries more than one level");
    checkEqual(levelsSaid(saidBefore), realm.hero().level - levelBefore,
               "each level the hand-in paid is said, one Levelled a level");
    realm.step();
    checkEqual(levelsSaid(0), 0, "and the next step clears them, so the drawing reads them at once");
    tally(realm, &falchion, &runes, &bless, &potions);
    check(falchion, "a lucky Falchion with an empty socket is in his bag");
    checkEqual(runes, 1, "and a Rune of Creation carrying Stormcall");
    checkEqual(bless - blessBefore, 3, "three Jewels of Bless");
    checkEqual(potions - potionsBefore, 20, "and twenty large potions");
    check(realm.quest(quest).state == sim::QuestState::Resting, "the quest rests");
    check(!realm.questOffered(quest), "and is not offered again at once");
    realm.setWallClock(1000000 + sim::questAt(quest).repeatSeconds - 1);
    check(!realm.questOffered(quest), "nor a second before twelve hours are up");
    realm.setWallClock(1000000 + sim::questAt(quest).repeatSeconds);
    check(realm.questOffered(quest), "and it is offered again when they are");
    checkEqual(int(realm.quest(quest).completions), 1, "counted once handed in");

    // A repeat pays the purse and the stacks again, and the weapon and the rune never.
    check(realm.acceptQuest(quest), "taken again");
    record = realm.record();
    record.quests[quest].state = sim::QuestState::Ready;
    for (int s = 0; s < sim::kQuestSteps; ++s) {
        record.quests[quest].counts[s] = uint16_t(realm.questGoal(quest, s));
    }
    realm.restore(record);
    talk();
    int swords = 0;
    for (int slot = 0; slot < sim::kSlots; ++slot) {
        swords += realm.satchel()[slot].item == tables.itemNamed("Sword08") ? 1 : 0;
    }
    const int bless1 = bless, potions1 = potions;
    check(realm.completeQuest(quest, -1), "and handed in again");
    tally(realm, &falchion, &runes, &bless, &potions);
    int swordsAfter = 0;
    for (int slot = 0; slot < sim::kSlots; ++slot) {
        swordsAfter += realm.satchel()[slot].item == tables.itemNamed("Sword08") ? 1 : 0;
    }
    checkEqual(swordsAfter, swords, "no second Falchion");
    checkEqual(runes, 1, "nor a second rune");
    checkEqual(bless - bless1, 3, "but three more Jewels of Bless");
    checkEqual(potions - potions1, 20, "and twenty more potions");
}

// Sockets and the Rune of Creation (sim/items.h): which rune goes in what, setting fills the first
// free socket, and Stormcall and Meteor call their lightning or rock on ANOTHER monster -- and
// never without a power worn.
void testRunes(const content::Tables& tables) {
    std::printf("sockets\n");
    const int rune = tables.itemAt(14, 22), serpent = tables.itemAt(0, 8);
    const int plate = tables.itemAt(8, 9);
    check(rune >= 0 && serpent >= 0 && plate >= 0, "the Rune of Creation, a Serpent Sword and a plate");
    if (rune < 0 || serpent < 0 || plate < 0) return;
    const uint8_t storm = uint8_t(sim::Power::Stormcall), meteor = uint8_t(sim::Power::Meteor);
    const auto held = [](int item, uint8_t sockets, uint8_t first, uint8_t second = 0) {
        sim::Held h{int32_t(item), 0, 1};
        h.sockets = sockets;
        h.powers[0] = first;
        h.powers[1] = second;
        return h;
    };
    const sim::Kin dk = sim::Kin::DarkKnight;
    const sim::Held carried = held(rune, 0, storm);
    check(sim::settable(tables, carried, held(serpent, 1, 0), dk),
          "Stormcall goes in a knight's socketed sword");
    check(!sim::settable(tables, carried, held(serpent, 1, 0), sim::Kin::DarkWizard),
          "and not by a wizard");
    check(!sim::settable(tables, carried, held(serpent, 0, 0), dk), "nor in a sword with no socket");
    check(!sim::settable(tables, carried, held(serpent, 1, storm), dk), "nor in a full one");
    check(sim::settable(tables, carried, held(serpent, 2, storm), dk),
          "but in the second of two, one set");
    check(!sim::settable(tables, carried, held(plate, 1, 0), dk),
          "nor in armour: it is a weapon's power");
    check(!sim::settable(tables, held(rune, 0, 0), held(serpent, 1, 0), dk),
          "and a Rune of Creation with no power sets nothing");
    // Frost Arrow, the elf's: a bow's or a crossbow's socket, hers alone.
    {
        const int bow = tables.itemNamed("Bow04"), crossbow = tables.itemNamed("CrossBow03");
        const uint8_t frost = uint8_t(sim::Power::Frost);
        check(bow >= 0 && crossbow >= 0, "a Battle Bow and an Arquebus");
        if (bow >= 0 && crossbow >= 0) {
            const sim::Held frosty = held(rune, 0, frost);
            check(sim::settable(tables, frosty, held(bow, 1, 0), sim::Kin::FairyElf),
                  "Frost Arrow goes in an elf's socketed bow");
            check(sim::settable(tables, frosty, held(crossbow, 1, 0), sim::Kin::FairyElf),
                  "and her socketed crossbow");
            check(!sim::settable(tables, frosty, held(bow, 1, 0), dk), "and not by a knight");
            check(!sim::settable(tables, carried, held(bow, 1, 0), sim::Kin::FairyElf),
                  "nor Stormcall by her");
        }
    }
    // A ring and a pendant take every armour rune, Evil Spirit too, and no weapon's.
    {
        const int ring = tables.itemAt(13, 8), pendant = tables.itemAt(13, 12);
        check(ring >= 0 && pendant >= 0, "a Ring of Ice and a Pendant of Lightning");
        if (ring >= 0 && pendant >= 0) {
            for (const sim::Power power : {sim::Power::Renewal, sim::Power::Spirits}) {
                const sim::Held set = held(rune, 0, uint8_t(power));
                check(sim::settable(tables, set, held(ring, 1, 0), sim::Kin::DarkWizard),
                      "an armour rune goes in a socketed ring");
                check(sim::settable(tables, set, held(pendant, 1, 0), sim::Kin::FairyElf),
                      "and a socketed pendant");
            }
            check(!sim::settable(tables, carried, held(ring, 1, 0), dk),
                  "but not Stormcall: it is a weapon's power");
            check(!sim::settable(tables, carried, held(pendant, 1, 0), dk), "in either");
            checkEqual(sim::mostSocketsOf(tables.items[size_t(ring)]), 1, "a ring holds one socket");
            checkEqual(sim::mostSocketsOf(tables.items[size_t(pendant)]), sim::kMostSockets,
                       "a pendant three");
        }
    }
    // The Undying, every class's armour power: in armour or a shield by anyone, never a weapon;
    // x1.2 on maximum health each; and Devin's first clear pays it to every class.
    {
        const uint8_t undying = uint8_t(sim::Power::Undying);
        const sim::Held hardy = held(rune, 0, undying);
        const int shield = tables.itemAt(6, 0);
        for (sim::Kin kin : {sim::Kin::DarkWizard, sim::Kin::FairyElf, sim::Kin::DarkKnight}) {
            check(sim::settable(tables, hardy, held(plate, 1, 0), kin),
                  "the Undying goes in socketed armour, whoever wears it");
        }
        check(shield < 0 || sim::settable(tables, hardy, held(shield, 1, 0), dk),
              "and in a socketed shield");
        check(!sim::settable(tables, hardy, held(serpent, 1, 0), dk), "but not in a weapon");
        sim::Fighter fighter;
        int bare = 0, once = 0, twice = 0;
        sim::Arms arms;
        sim::reckon(dk, 30, sim::startingPoints(dk), arms, &fighter, &bare);
        arms.excel.undyingRate = sim::kUndyingHealth;
        sim::reckon(dk, 30, sim::startingPoints(dk), arms, &fighter, &once);
        arms.excel.undyingRate = sim::kUndyingHealth * sim::kUndyingHealth;
        sim::reckon(dk, 30, sim::startingPoints(dk), arms, &fighter, &twice);
        checkEqual(once, int(double(bare) * 1.2), "one Undying is x1.2 on maximum health");
        checkEqual(twice, int(double(bare) * 1.44), "and two are x1.44");
        const int devin = sim::questOf(406);
        check(devin >= 0, "Devin gives a quest");
        if (devin >= 0) {
            const sim::QuestRow& row = sim::questAt(devin);
            for (int kin = 0; kin < 3; ++kin) {
                int runes = 0, again = 0;
                for (int i = 0; i < row.paidCount; ++i) {
                    const sim::QuestItem& what = row.paid[i];
                    if (what.power != uint8_t(sim::Power::Renewal)) continue;
                    runes += sim::questPays(what, kin, true) ? 1 : 0;
                    again += sim::questPays(what, kin, false) ? 1 : 0;
                }
                checkEqual(runes, 1, "Devin's first clear pays every class one Renewal");
                checkEqual(again, 0, "and a repeat pays none");
            }
        }
    }
    // The Dungeon's three and Devin's Renewal: every class's, armour only, like the Undying; and
    // the Golden Archer's chain pays each of its three once, the first time, to every class.
    {
        const int helm = tables.itemNamed("HelmMale09"), boots = tables.itemNamed("BootElf05");
        for (sim::Power power : {sim::Power::KeenEye, sim::Power::Bloodwell, sim::Power::Frenzy,
                                 sim::Power::Renewal}) {
            const sim::Held one = held(rune, 0, uint8_t(power));
            check(sim::powerOf(uint8_t(power)) != nullptr, "the Dungeon's rune has a row");
            for (sim::Kin kin : {sim::Kin::DarkWizard, sim::Kin::FairyElf, sim::Kin::DarkKnight}) {
                check(helm < 0 || sim::settable(tables, one, held(helm, 1, 0), kin),
                      "it goes in a socketed helm, whoever wears it");
                check(boots < 0 || sim::settable(tables, one, held(boots, 1, 0), kin),
                      "and in socketed boots");
            }
            check(!sim::settable(tables, one, held(serpent, 1, 0), dk), "but not in a weapon");
        }
        const sim::Power chain[3] = {sim::Power::KeenEye, sim::Power::Bloodwell, sim::Power::Frenzy};
        for (int link = 0; link < 3; ++link) {
            const sim::QuestRow& row = sim::questAt(3 + link);
            checkEqual(int(row.giver), 236, "the chain is the Golden Archer's");
            for (int kin = 0; kin < 3; ++kin) {
                int runes = 0, pieces = 0, again = 0;
                for (int i = 0; i < row.paidCount; ++i) {
                    const sim::QuestItem& what = row.paid[i];
                    const bool first = sim::questPays(what, kin, true);
                    if (what.power == uint8_t(chain[link])) runes += first ? 1 : 0;
                    if (what.sockets > 0) pieces += first ? 1 : 0;
                    if (what.item == std::string("Jewel22") || what.sockets > 0) {
                        again += sim::questPays(what, kin, false) ? 1 : 0;
                    }
                }
                checkEqual(runes, 1, "each link's first clear pays every class its rune");
                // The Pit's two: its boots, and the ring (2026-10-01).
                checkEqual(pieces, link == 2 ? 2 : 1, "and one socketed piece of his own");
                checkEqual(again, 0, "and a repeat pays neither");
            }
        }
        // Evil Spirit: every class's, in a shield and nothing else, and the Pit pays it.
        const sim::Held spirit = held(rune, 0, uint8_t(sim::Power::Spirits));
        const int serpentShield = tables.itemNamed("Shield12"), legend = tables.itemNamed("Shield15");
        check(sim::powerOf(uint8_t(sim::Power::Spirits)) != nullptr, "Evil Spirit has a row");
        for (sim::Kin kin : {sim::Kin::DarkWizard, sim::Kin::FairyElf, sim::Kin::DarkKnight}) {
            check(legend < 0 || sim::settable(tables, spirit, held(legend, 1, 0), kin),
                  "Evil Spirit goes in a socketed shield, whoever wears it");
            check(helm < 0 || !sim::settable(tables, spirit, held(helm, 1, 0), kin),
                  "but not in a helm");
        }
        check(serpentShield < 0 || sim::settable(tables, spirit, held(serpentShield, 1, 0), dk),
              "and in the knight's Serpent Shield");
        check(!sim::settable(tables, spirit, held(serpent, 1, 0), dk), "nor in a weapon");
        // The Pit's ring (docs/jewellery.md): Evil Spirit and every armour rune, from any class.
        const int ice = tables.itemNamed("Ring01");
        check(ice >= 0, "the Ring of Ice is in the table");
        for (sim::Kin kin : {sim::Kin::DarkWizard, sim::Kin::FairyElf, sim::Kin::DarkKnight}) {
            check(ice < 0 || sim::settable(tables, spirit, held(ice, 1, 0), kin),
                  "Evil Spirit goes in the socketed ring, whoever wears it");
            check(ice < 0 || sim::settable(tables, held(rune, 0, uint8_t(sim::Power::Frenzy)),
                                           held(ice, 1, 0), kin),
                  "and Frenzy does too");
        }
        const sim::QuestRow& pit = sim::questAt(5);
        for (int kin = 0; kin < 3; ++kin) {
            int spirits = 0, shields = 0, boots = 0;
            for (int i = 0; i < pit.paidCount; ++i) {
                const sim::QuestItem& what = pit.paid[i];
                if (!sim::questPays(what, kin, true)) continue;
                if (what.power == uint8_t(sim::Power::Spirits)) ++spirits;
                const int item = tables.itemNamed(what.item);
                if (item >= 0 && sim::ring(tables.items[size_t(item)]) && what.sockets == 1) ++shields;
                if (item >= 0 && tables.items[size_t(item)].shield()) shields += 100;
                if (item >= 0 && tables.items[size_t(item)].group == sim::kGroupBoots &&
                    what.sockets == 2) {
                    ++boots;
                }
            }
            checkEqual(spirits, 0, "the Pit's first clear pays no Evil Spirit (2026-10-02)");
            checkEqual(shields, 1, "and a ring with one socket to set it in, and no shield");
            checkEqual(boots, 1, "and boots with two sockets");
        }
    }
    // The Golden Archer's chain in order (Realm::questHere): the Catacombs first, the Halls and
    // the Pit locked behind it; each handed in moves him on; all rested, the Pit's rest; and the
    // Catacombs again once its twelve hours are up.
    {
        sim::Realm realm;
        check(realm.raise(&tables, 11, 131, 128, sim::Kin::DarkKnight, 30), "a chain realm raises");
        realm.setWallClock(1000000);
        checkEqual(realm.questHere(236), 3, "the Golden Archer offers the Catacombs first");
        check(realm.questLocked(4) && realm.questLocked(5), "the Halls and the Pit wait on it");
        const auto rest = [&](int q, int64_t at) {
            sim::HeroRecord record = realm.record();
            record.quests[q].state = sim::QuestState::Resting;
            record.quests[q].availableAt = at;
            record.quests[q].completions = 1;
            realm.restore(record);
        };
        const int64_t later = 1000000 + 12 * 60 * 60;
        rest(3, later);
        checkEqual(realm.questHere(236), 4, "the Catacombs handed in, he offers the Halls");
        rest(4, later);
        checkEqual(realm.questHere(236), 5, "and then the Pit");
        rest(5, later);
        checkEqual(realm.questHere(236), 5, "all three rested, he says the Pit's rest");
        realm.setWallClock(later);
        checkEqual(realm.questHere(236), 3, "and the Catacombs come back round first");
    }
    // Every item any quest pays is one the table knows.
    for (int q = 0; q < sim::kQuests; ++q) {
        const sim::QuestRow& row = sim::questAt(q);
        for (int i = 0; i < row.paidCount; ++i) {
            check(tables.itemNamed(row.paid[i].item) >= 0, "a quest pays an item the table knows");
        }
    }
    check(sim::expensive(tables, carried), "a Rune of Creation cannot be dropped");
    check(sim::expensive(tables, held(serpent, 1, 0)), "nor a sword with an empty socket");
    check(!sim::expensive(tables, held(serpent, 0, 0)), "but a +0 sword without one can");

    {
        sim::Realm realm;
        check(realm.raise(&tables, 11, 138, 124), "a realm raises for the sockets");
        const int sword = realm.give(serpent, -1, 0, -1, false, 0, 0, 3);
        const uint8_t one[3] = {storm, 0, 0}, two[3] = {meteor, 0, 0};
        const int first = realm.give(rune, -1, 0, 1, false, 0, 0, 0, one);
        const int second = realm.give(rune, -1, 0, 1, false, 0, 0, 0, two);
        check(sword >= 0 && first >= 0 && second >= 0, "a three-socket sword and two runes");
        checkEqual(int(realm.satchel()[first].powers[0]), int(storm), "the first carries Stormcall");
        check(realm.refine(first, sword), "the first is set");
        check(realm.refine(second, sword), "and the second");
        checkEqual(long(realm.discard(sword)), 0L, "the socketed sword will not go on the ground");
        const sim::Held& now = realm.satchel()[sword];
        check(now.powers[0] == storm && now.powers[1] == meteor && now.powers[2] == 0,
              "into the first two sockets, the third still empty");
        check(realm.satchel()[first].empty() && realm.satchel()[second].empty(),
              "and both runes are gone");
        const std::vector<sim::Happening>& said = realm.happenings();
        check(!said.empty() && said.back().what == sim::What::Set &&
                  said.back().b == int(meteor) && said.back().c == 1,
              "and the realm says the second went in socket 1");
    }

    // A knight in the hunting ground, swinging at the nearest thing: with a power worn, some of
    // his landed swings call it, each on a monster other than the one he struck; with the same
    // sword and its sockets empty, none do.
    int lit = 0, grudged = 0, stood = 0;
    std::vector<uint32_t> litIds, cameIds;
    const auto hunt = [&](uint8_t power, int* swings, int* calls, int* onTarget, int* landed,
                          uint64_t seed = 3) {
        sim::Realm realm;
        realm.raise(&tables, seed, 200, 160, sim::Kin::DarkKnight, 60);
        const uint8_t powers[3] = {power, 0, 0};
        realm.give(serpent, sim::kWeaponRight, 9, -1, false, 0, 0, 1, powers);
        uint32_t fighting = 0;
        for (int tick = 0; tick < 4000 && realm.hero().alive(); ++tick) {
            const sim::Body& hero = realm.hero();
            uint32_t nearest = 0;
            float best = 1e9f;
            for (const sim::Body& one : realm.bodies()) {
                if (!one.monster() || !one.alive()) continue;
                const float dx = one.x - hero.x, dy = one.y - hero.y;
                if (dx * dx + dy * dy < best) {
                    best = dx * dx + dy * dy;
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
            realm.step();
            uint32_t struck = 0;
            for (const sim::Happening& h : realm.happenings()) {
                if (h.what == sim::What::Hit && h.whom == realm.hero().id &&
                    std::find(litIds.begin(), litIds.end(), h.who) != litIds.end() &&
                    std::find(cameIds.begin(), cameIds.end(), h.who) == cameIds.end()) {
                    cameIds.push_back(h.who);
                }
                if (h.who != realm.hero().id) continue;
                if (h.what == sim::What::Hit && !h.thrown) {
                    ++*swings;
                    struck = h.whom;
                    if (h.c > 0) ++stood;  // left it standing, so the rune has it to take
                }
                if (h.what == sim::What::Loosed &&
                    (h.a == sim::skill::kLightning || h.a == sim::skill::kMeteorite)) {
                    ++*calls;
                    if (h.whom == struck) ++*onTarget;
                }
                if (h.what == sim::What::Hit && h.thrown) {
                    ++*landed;
                    // What the lightning struck turns on him, as anything he hits does.
                    const sim::Body* hit = realm.find(h.whom);
                    if (hit && hit->alive() && hit->monster()) {
                        ++lit;
                        if (hit->quarry == realm.hero().id && hit->provoked) ++grudged;
                        litIds.push_back(h.whom);
                    }
                }
            }
        }
    };
    int swings = 0, calls = 0, onTarget = 0, landed = 0;
    // A few hunts, until one leaves a monster standing under the lightning: since the lightning
    // carries his energy (2026-10-01) one seed's three calls all killed what they struck.
    for (uint64_t seed = 3; seed < 13 && lit == 0; ++seed) {
        hunt(storm, &swings, &calls, &onTarget, &landed, seed);
    }
    std::printf("  %d landed swings, %d lightning calls, %d landed\n", swings, calls, landed);
    check(swings > 50, "the knight lands swings");
    check(calls > 0, "and Stormcall calls lightning");
    // At about its chance now that a lone monster takes it itself (2026-10-01): before, the
    // roll was thrown away with nobody else near, and a hunt saw one swing in twenty-five.
    // Measured on the swings that left their monster standing: a swing that killed the only
    // one near has nothing for the lightning to take, and a level-60 knight kills most at a blow.
    std::printf("  %d of them on the monster he struck, alone; %d swings left it standing\n",
                onTarget, stood);
    check(double(calls) >= double(stood) * 0.12 && double(calls) <= double(swings) * 0.28,
          "at about its chance, 20%");
    std::printf("  %d struck by lightning and standing, %d of them after him, %d came and hit "
                "him\n", lit, grudged, int(cameIds.size()));
    check(lit > 0 && grudged == lit, "and what the lightning struck turns on him");
    int bareSwings = 0, bareCalls = 0, bareOn = 0, bareLanded = 0;
    hunt(0, &bareSwings, &bareCalls, &bareOn, &bareLanded);
    checkEqual(bareCalls, 0, "and none from the same sword with its socket empty");

    // Meteor: the same, with a rock whose blow lands after its fall.
    int mSwings = 0, rocks = 0, mOn = 0, mLanded = 0;
    stood = 0;
    hunt(meteor, &mSwings, &rocks, &mOn, &mLanded);
    std::printf("  %d landed swings, %d rocks called, %d landed\n", mSwings, rocks, mLanded);
    check(rocks > 0, "Meteor calls rocks down");
    check(double(rocks) >= double(stood) * 0.08 && double(rocks) <= double(mSwings) * 0.22,
          "at about its chance, 15%, a lone one taking its own");
    check(mLanded > 0 && mLanded <= rocks, "and they land, one blow a rock at most");

    // Ice and Poison: on the very monster he struck, the spell's element and no blow of its own
    // -- a chill it walks slowed under, or pulses that go on hurting it; the same sword with its
    // socket empty leaves nothing on anything.
    struct Elements {
        int swings = 0, said = 0, onStruck = 0, took = 0, pulses = 0, wounds = 0, wounded = 0;
    };
    const auto elements = [&](uint8_t power) {
        Elements seen;
        sim::Realm realm;
        // Weaker than the knight above, whose +9 sword kills a Lorencia monster a swing and so
        // leaves nothing standing to chill or poison: a level 20 knight with a +0 blade.
        realm.raise(&tables, 3, 200, 160, sim::Kin::DarkKnight, 20);
        const uint8_t powers[3] = {power, 0, 0};
        realm.give(serpent, sim::kWeaponRight, 0, -1, false, 0, 0, 1, powers);
        uint32_t fighting = 0;
        // After a poisoning he stands off for five seconds, so the monster lives to its pulses
        // (the first is three seconds on) rather than dying to his next swing.
        int standOff = 0;
        for (int tick = 0; tick < 4000 && realm.hero().alive(); ++tick) {
            const sim::Body& hero = realm.hero();
            uint32_t nearest = 0;
            float best = 1e9f;
            for (const sim::Body& one : realm.bodies()) {
                if (!one.monster() || !one.alive()) continue;
                const float dx = one.x - hero.x, dy = one.y - hero.y;
                if (dx * dx + dy * dy < best) {
                    best = dx * dx + dy * dy;
                    nearest = one.id;
                }
            }
            if (standOff > 0) {
                --standOff;
            } else if (nearest != 0 && nearest != fighting) {
                fighting = nearest;
                sim::Request request;
                request.kind = sim::Request::Kind::Attack;
                request.target = nearest;
                realm.ask(request);
            }
            realm.step();
            uint32_t struck = 0;
            for (const sim::Happening& h : realm.happenings()) {
                if (h.who != realm.hero().id) continue;
                if (h.what == sim::What::Hit && h.poisoned) ++seen.pulses;
                // The Ice rune's frost wound, its own Hit in the rune's colour.
                if (h.what == sim::What::Hit && h.rune && !h.poisoned) {
                    ++seen.wounds;
                    seen.wounded += h.a;
                    continue;
                }
                if (h.what == sim::What::Hit && !h.thrown && !h.poisoned) {
                    ++seen.swings;
                    struck = h.whom;
                }
                if (h.what == sim::What::Loosed &&
                    (h.a == sim::skill::kIce || h.a == sim::skill::kPoison)) {
                    ++seen.said;
                    if (h.whom == struck) ++seen.onStruck;
                    if (h.a == sim::skill::kPoison && standOff == 0) {
                        standOff = 100;
                        fighting = 0;
                        sim::Request stop;
                        stop.kind = sim::Request::Kind::Stop;
                        realm.ask(stop);
                    }
                    for (const sim::Body& b : realm.bodies()) {
                        if (b.id != h.whom) continue;
                        const bool chilled = b.chilledUntil > realm.tick();
                        const bool poisoned = b.poisonUntil > realm.tick();
                        if (h.a == sim::skill::kIce ? chilled : poisoned) ++seen.took;
                    }
                }
            }
        }
        return seen;
    };
    const Elements ice = elements(uint8_t(sim::Power::Ice));
    std::printf("  %d landed swings, %d chills\n", ice.swings, ice.said);
    check(ice.said > 0, "Ice chills");
    checkEqual(ice.onStruck, ice.said, "always the monster he struck");
    checkEqual(ice.took, ice.said, "which walks chilled after it");
    check(double(ice.said) <= double(ice.swings) * 0.25, "at no more than its chance and some");
    std::printf("  %d frost wounds, %.1f each\n", ice.wounds,
                ice.wounds ? double(ice.wounded) / ice.wounds : 0.0);
    checkEqual(ice.wounds, ice.said, "and every chill wounds, as Stormcall and Poison do");
    check(ice.wounded >= ice.wounds, "for at least one each");
    const Elements venom = elements(uint8_t(sim::Power::Poison));
    std::printf("  %d landed swings, %d poisonings, %d pulses\n", venom.swings, venom.said,
                venom.pulses);
    check(venom.said > 0, "Poison poisons");
    checkEqual(venom.onStruck, venom.said, "always the monster he struck");
    checkEqual(venom.took, venom.said, "which goes on poisoned after it");
    check(venom.pulses > 0, "and its pulses hurt, his");
    checkEqual(venom.wounds, 0, "Poison's hurt is its pulses, no wound of its own");
    const Elements bare = elements(0);
    check(bare.said == 0 && bare.pulses == 0, "and none of either from an empty socket");

    // Arcane Echo, the wizard's: a staff's socket, his alone, and some of his casts let go twice
    // -- more Energy Balls in the air than he cast, and none extra from the same staff bare.
    const int staff = tables.itemNamed("Staff03");
    const uint8_t echo = uint8_t(sim::Power::Echo);
    check(staff >= 0, "a Serpent Staff");
    if (staff < 0) return;
    check(sim::settable(tables, held(rune, 0, echo), held(staff, 1, 0), sim::Kin::DarkWizard),
          "Arcane Echo goes in a wizard's socketed staff");
    check(!sim::settable(tables, held(rune, 0, echo), held(serpent, 1, 0), dk),
          "and not by a knight");
    const auto casts = [&](uint8_t power, int* cast, int* loosed, int32_t skill = sim::skill::kEnergyBall,
                           int* lightning = nullptr) {
        sim::Realm realm;
        realm.raise(&tables, 3, 200, 160, sim::Kin::DarkWizard, 60);
        const uint8_t powers[3] = {power, 0, 0};
        realm.give(staff, sim::kWeaponRight, 0, -1, false, 0, 0, 1, powers);
        uint32_t fighting = 0;
        for (int tick = 0; tick < 4000 && realm.hero().alive(); ++tick) {
            const sim::Body& hero = realm.hero();
            uint32_t nearest = 0;
            float best = 1e9f;
            for (const sim::Body& one : realm.bodies()) {
                if (!one.monster() || !one.alive()) continue;
                const float dx = one.x - hero.x, dy = one.y - hero.y;
                if (dx * dx + dy * dy < best) {
                    best = dx * dx + dy * dy;
                    nearest = one.id;
                }
            }
            if (nearest != 0 && nearest != fighting) {
                fighting = nearest;
                sim::Request request;
                request.kind = sim::Request::Kind::Attack;
                request.target = nearest;
                request.skill = skill;
                realm.ask(request);
            }
            realm.step();
            for (const sim::Happening& h : realm.happenings()) {
                if (lightning && h.who == realm.hero().id && h.what == sim::What::Loosed &&
                    h.a == sim::skill::kLightning) {
                    ++*lightning;
                }
                if (h.who != realm.hero().id || h.a != sim::skill::kEnergyBall) continue;
                if (h.what == sim::What::Cast) ++*cast;
                if (h.what == sim::What::Loosed) ++*loosed;
            }
        }
    };
    int cast = 0, loosed = 0;
    casts(echo, &cast, &loosed);
    int bareCast = 0, bareLoosed = 0;
    casts(0, &bareCast, &bareLoosed);
    std::printf("  %d Energy Balls cast, %d let go; bare %d and %d\n", cast, loosed, bareCast,
                bareLoosed);
    check(cast > 50 && bareCast > 50, "the wizard casts");
    // A cast whose body died under an earlier bolt lets nothing go, so the bare staff is the
    // measure: the echo's share is what the rune adds over it.
    check(bareLoosed <= bareCast, "one bolt a cast at most from the same staff bare");
    const double more = double(loosed) / cast - double(bareLoosed) / bareCast;
    check(more > 0.05, "and Arcane Echo lets some go twice");
    check(more < 0.25, "at no more than its chance and some");
    // And his plain staff swings with it set call nothing: Echo fell through to Stormcall's
    // lightning once, a knight's power on a wizard's staff.
    int swingCast = 0, swingLoosed = 0, lightning = 0;
    casts(echo, &swingCast, &swingLoosed, 0, &lightning);
    checkEqual(lightning, 0, "an Echo staff's plain swings call no lightning");

    // Pyroblaster, the wizard's second: his Fire Ball half again as hard, and some that land
    // start a chain, one fireball flying on from the monster struck to the next, four hops at
    // most, never back to one it has struck.
    const uint8_t pyro = uint8_t(sim::Power::Pyroblast);
    check(sim::settable(tables, held(rune, 0, pyro), held(staff, 1, 0), sim::Kin::DarkWizard),
          "Pyroblaster goes in a wizard's socketed staff");
    check(!sim::settable(tables, held(rune, 0, pyro), held(staff, 1, 0), dk), "and not by a knight");
    struct Blasts {
        int hits = 0, damage = 0, chains = 0, hops = 0, burstHits = 0, longest = 0;
        bool repeats = false;
    };
    const auto blasts = [&](uint8_t power) {
        Blasts out;
        sim::Realm realm;
        realm.raise(&tables, 3, 200, 160, sim::Kin::DarkWizard, 60);
        realm.learn(sim::skill::kFireBall);
        const uint8_t powers[3] = {power, 0, 0};
        realm.give(staff, sim::kWeaponRight, 0, -1, false, 0, 0, 1, powers);
        uint32_t fighting = 0, chainAt = 0;
        int length = 0, chainTick = -1;
        std::vector<uint32_t> struck;
        for (int tick = 0; tick < 4000 && realm.hero().alive(); ++tick) {
            const sim::Body& hero = realm.hero();
            uint32_t nearest = 0;
            float best = 1e9f;
            for (const sim::Body& one : realm.bodies()) {
                if (!one.monster() || !one.alive()) continue;
                const float dx = one.x - hero.x, dy = one.y - hero.y;
                if (dx * dx + dy * dy < best) {
                    best = dx * dx + dy * dy;
                    nearest = one.id;
                }
            }
            if (nearest != 0 && nearest != fighting) {
                fighting = nearest;
                sim::Request request;
                request.kind = sim::Request::Kind::Attack;
                request.target = nearest;
                request.skill = sim::skill::kFireBall;
                realm.ask(request);
            }
            realm.step();
            for (const sim::Happening& h : realm.happenings()) {
                if (h.who != realm.hero().id) continue;
                if (h.what == sim::What::Loosed && h.a == sim::skill::kFireBall && h.rune) {
                    // A hop off the monster the last one flew at, said on the tick that one
                    // lands, goes on the same chain; a fresh Fire Ball bursting off the same
                    // monster later starts another.
                    if (uint32_t(h.c) != chainAt || tick != chainTick) {
                        ++out.chains;
                        length = 0;
                        struck.assign(1, uint32_t(h.c));
                    }
                    for (const uint32_t one : struck) out.repeats |= one == h.whom;
                    struck.push_back(h.whom);
                    chainAt = h.whom;
                    chainTick = tick + h.b;
                    ++length;
                    ++out.hops;
                    out.longest = std::max(out.longest, length);
                }
                if (h.what == sim::What::Hit && h.thrown && h.rune) ++out.burstHits;
                if (h.what == sim::What::Hit && h.thrown && !h.rune && !h.critical &&
                    !h.excellent) {
                    ++out.hits;
                    out.damage += h.a;
                }
            }
        }
        return out;
    };
    const Blasts blasted = blasts(pyro), plain = blasts(0);
    std::printf("  Pyroblaster: %d hits at %.1f, %d chains of %d hops (longest %d), %d landed; bare %d at %.1f\n",
                blasted.hits, blasted.hits ? double(blasted.damage) / blasted.hits : 0.0,
                blasted.chains, blasted.hops, blasted.longest, blasted.burstHits, plain.hits,
                plain.hits ? double(plain.damage) / plain.hits : 0.0);
    check(blasted.hits > 30 && plain.hits > 30, "the wizard lands Fire Balls");
    if (blasted.hits > 0 && plain.hits > 0) {
        const double ratio = (double(blasted.damage) / blasted.hits) / (double(plain.damage) / plain.hits);
        check(ratio > 1.3 && ratio < 1.7, "a Pyroblaster's Fire Ball strikes about half again as hard");
    }
    check(blasted.chains > 0, "and some start a chain");
    check(blasted.longest <= sim::kPyroblastChain, "of four hops at most");
    check(!blasted.repeats, "never back to a monster the chain has struck");
    check(blasted.burstHits > 0, "and the chain's hops land");
    checkEqual(plain.chains, 0, "and a bare staff starts none");
}

// The element runes (sim::kElementRuneDamage): every class sets them in a weapon, a ring or a
// pendant, they drop only to a class with something of their element, and a wizard's Fire Ball
// with an Inferno in his staff, or in his ring, strikes a fifth harder than bare.
void testElementRunes(const content::Tables& tables) {
    std::printf("element runes\n");
    const int rune = tables.itemAt(14, 22), staff = tables.itemNamed("Staff03");
    const int ring = tables.itemAt(13, 8), plate = tables.itemAt(8, 9);
    check(rune >= 0 && staff >= 0 && ring >= 0 && plate >= 0,
          "the Rune of Creation, a staff, a Ring of Ice and a plate");
    if (rune < 0 || staff < 0 || ring < 0 || plate < 0) return;
    const auto held = [](int item, uint8_t sockets, uint8_t first) {
        sim::Held h{int32_t(item), 0, 1};
        h.sockets = sockets;
        h.powers[0] = first;
        return h;
    };
    const uint8_t inferno = uint8_t(sim::Power::Inferno);
    for (const sim::Kin kin : {sim::Kin::DarkKnight, sim::Kin::DarkWizard, sim::Kin::FairyElf}) {
        check(sim::settable(tables, held(rune, 0, inferno), held(staff, 1, 0), kin),
              "an Inferno goes in any class's weapon");
        check(sim::settable(tables, held(rune, 0, inferno), held(ring, 1, 0), kin),
              "and in a ring");
    }
    check(!sim::settable(tables, held(rune, 0, inferno), held(plate, 1, 0), sim::Kin::DarkKnight),
          "and not in armour");
    check(sim::elementServes(sim::Element::Wind, sim::Kin::DarkKnight) &&
              sim::elementServes(sim::Element::Wind, sim::Kin::DarkWizard) &&
              sim::elementServes(sim::Element::Ice, sim::Kin::FairyElf) &&
              !sim::elementServes(sim::Element::Fire, sim::Kin::FairyElf),
          "a Tempest drops to a knight and to a wizard (Twister), and only a Glacier to an elf");
    // The wizard's Fire Ball on the nearest monster, the plain landings averaged.
    const auto average = [&](int slot, int item) {
        sim::Realm realm;
        realm.raise(&tables, 3, 200, 160, sim::Kin::DarkWizard, 60);
        realm.learn(sim::skill::kFireBall);
        realm.give(staff, sim::kWeaponRight, 0, -1, false, 0, 0, 0, nullptr);
        if (item >= 0) {
            const uint8_t powers[3] = {inferno, 0, 0};
            realm.give(item, slot, 0, -1, false, 0, 0, 1, powers);
        }
        int hits = 0, damage = 0;
        uint32_t fighting = 0;
        for (int tick = 0; tick < 4000 && realm.hero().alive(); ++tick) {
            const sim::Body& hero = realm.hero();
            uint32_t nearest = 0;
            float best = 1e9f;
            for (const sim::Body& one : realm.bodies()) {
                if (!one.monster() || !one.alive()) continue;
                const float dx = one.x - hero.x, dy = one.y - hero.y;
                if (dx * dx + dy * dy < best) {
                    best = dx * dx + dy * dy;
                    nearest = one.id;
                }
            }
            if (nearest != 0 && nearest != fighting) {
                fighting = nearest;
                sim::Request request;
                request.kind = sim::Request::Kind::Attack;
                request.target = nearest;
                request.skill = sim::skill::kFireBall;
                realm.ask(request);
            }
            realm.step();
            for (const sim::Happening& h : realm.happenings()) {
                if (h.who != realm.hero().id || h.what != sim::What::Hit || !h.thrown) continue;
                if (h.rune || h.critical || h.excellent) continue;
                ++hits;
                damage += h.a;
            }
        }
        return std::pair<int, double>(hits, hits ? double(damage) / hits : 0.0);
    };
    const auto bare = average(0, -1);
    const auto ringed = average(sim::kRingRight, ring);
    std::printf("  Fire Ball: bare %d at %.1f, Inferno ring %d at %.1f\n", bare.first,
                bare.second, ringed.first, ringed.second);
    check(bare.first > 30 && ringed.first > 30, "the wizard lands Fire Balls");
    if (bare.second > 0.0) {
        const double ratio = ringed.second / bare.second;
        check(ratio > 1.1 && ratio < 1.3, "an Inferno ring's Fire Ball strikes about a fifth harder");
    }
}

// The rune groups (sim::PowerRow, the user, 2026-10-02: "group runes which is only for specific
// classes, for specific weapon slots") and their first four: the knight's Fireburst, Ring of Fire
// and Bulwark in his weapon alone, and every class's Kinship in a ring or the pendant alone.
void testGroupRunes(const content::Tables& tables) {
    std::printf("group runes\n");
    const int rune = tables.itemAt(14, 22), sword = tables.itemNamed("Sword01");
    const int ring = tables.itemAt(13, 8), plate = tables.itemAt(8, 9);
    const int angel = tables.itemAt(13, 0), imp = tables.itemAt(13, 1);
    check(rune >= 0 && sword >= 0 && ring >= 0 && plate >= 0 && angel >= 0 && imp >= 0,
          "the Rune of Creation, a sword, a ring, a plate and the two pets");
    if (rune < 0 || sword < 0 || ring < 0 || plate < 0 || angel < 0 || imp < 0) return;
    const auto held = [](int item, uint8_t sockets, sim::Power first) {
        sim::Held h{int32_t(item), 0, 1};
        h.sockets = sockets;
        h.powers[0] = uint8_t(first);
        return h;
    };
    const auto sets = [&](sim::Power power, int into, sim::Kin kin) {
        return sim::settable(tables, held(rune, 0, power), held(into, 1, sim::Power::None), kin);
    };
    using sim::Kin;
    using sim::Power;
    int groupless = 0;
    for (int p = 1; sim::powerOf(uint8_t(p)); ++p) {
        groupless += sim::powerOf(uint8_t(p))->classes == 0 || sim::powerOf(uint8_t(p))->slots == 0;
    }
    checkEqual(groupless, 0, "every rune names its classes and its sockets");
    for (const Power power : {Power::Fireburst, Power::FireRing, Power::Bulwark}) {
        check(sets(power, sword, Kin::DarkKnight), "a knight's rune goes in his sword");
        check(!sets(power, sword, Kin::DarkWizard) && !sets(power, sword, Kin::FairyElf),
              "and in no other class's");
        check(!sets(power, ring, Kin::DarkKnight) && !sets(power, plate, Kin::DarkKnight),
              "and not in a ring or armour");
    }
    for (const Kin kin : {Kin::DarkKnight, Kin::DarkWizard, Kin::FairyElf}) {
        check(sets(Power::Kinship, ring, kin), "Kinship goes in any class's ring");
        check(!sets(Power::Kinship, sword, kin) && !sets(Power::Kinship, plate, kin),
              "and not in a weapon or armour");
    }
    check(sets(Power::Undying, plate, Kin::FairyElf) && !sets(Power::Undying, sword, Kin::FairyElf),
          "the old groups hold: the Undying in armour and not a weapon");
    check(sets(Power::Spirits, ring, Kin::DarkWizard) && !sets(Power::Spirits, plate, Kin::DarkWizard),
          "Evil Spirit in a ring and not a plate");
    check(sets(Power::Inferno, ring, Kin::FairyElf) && sets(Power::Inferno, sword, Kin::FairyElf),
          "an element rune in a ring or a weapon");

    // Bulwark: Defense up with a bare sword only while it carries one.
    for (const bool worn : {false, true}) {
        sim::Realm knight;
        knight.raise(&tables, 7, 190, 110, Kin::DarkKnight, 30);
        knight.learn(sim::skill::kDefense);
        const uint8_t powers[3] = {uint8_t(worn ? Power::Bulwark : Power::None), 0, 0};
        knight.give(sword, sim::kWeaponRight, 0, -1, false, 0, 0, 1, powers);
        knight.invoke(sim::skill::kDefense, knight.hero().id);
        for (int tick = 0; tick < 60 && knight.cooling(sim::skill::kDefense) == 0; ++tick) {
            knight.step();
        }
        const bool raised = knight.hero().boonSkill == sim::skill::kDefense;
        check(raised == worn, worn ? "a Bulwark sword raises Defense with no shield"
                                   : "and a bare sword does not");
    }

    // Kinship: the Imp's gift without its life, the Angel's guard without its cut.
    for (const int pet : {imp, angel}) {
        sim::Realm realm;
        realm.raise(&tables, 7, 190, 110, Kin::DarkKnight, 30);
        realm.give(pet, sim::kPet);
        const sim::PetPower bare = realm.hero().pet;
        const uint8_t powers[3] = {uint8_t(Power::Kinship), 0, 0};
        realm.give(ring, sim::kRingRight, 0, -1, false, 0, 0, 1, powers);
        const sim::PetPower kin = realm.hero().pet;
        check(bare.lifeCost > 0 || bare.dealt < 1.0, "the pet has a price");
        check(kin.lifeCost == 0 && kin.dealt >= 1.0, "and Kinship lifts it");
        check(kin.dealt == std::max(1.0, bare.dealt) && kin.taken == bare.taken &&
                  kin.health == bare.health,
              "and keeps its gift");
    }

    // Fireburst and Ring of Fire on a knight's hunt: each answers about one landed swing in ten,
    // the burst's hops fly on to other monsters, and the ring strikes round him.
    const auto hunt = [&](Power power, int* swings, int* answers, int* blows) {
        sim::Realm realm;
        realm.raise(&tables, 5, 200, 160, Kin::DarkKnight, 40);
        const uint8_t powers[3] = {uint8_t(power), 0, 0};
        realm.give(sword, sim::kWeaponRight, 0, -1, false, 0, 0, 1, powers);
        uint32_t fighting = 0;
        for (int tick = 0; tick < 6000 && realm.hero().alive(); ++tick) {
            if (realm.hero().health < realm.hero().maxHealth / 3) {
                sim::HeroRecord record = realm.record();
                record.health = realm.hero().maxHealth;
                realm.restore(record);
            }
            const sim::Body& hero = realm.hero();
            uint32_t nearest = 0;
            float best = 1e9f;
            for (const sim::Body& one : realm.bodies()) {
                if (!one.monster() || !one.alive()) continue;
                const float dx = one.x - hero.x, dy = one.y - hero.y;
                if (dx * dx + dy * dy < best) {
                    best = dx * dx + dy * dy;
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
            realm.step();
            bool ringing = false;
            for (const sim::Happening& h : realm.happenings()) {
                if (h.who != realm.hero().id) continue;
                if (h.what == sim::What::Hit && !h.thrown) ++*swings;
                if (h.what == sim::What::Loosed && h.rune) {
                    const bool burst = h.a == sim::skill::kFireBall && h.c == int32_t(fighting);
                    if (burst || h.a == sim::skill::kInferno) ++*answers;
                    ringing = ringing || h.a == sim::skill::kInferno;
                }
                if (h.what == sim::What::Hit && h.thrown && (h.rune || ringing)) ++*blows;
            }
        }
    };
    int swings = 0, bursts = 0, hops = 0;
    hunt(Power::Fireburst, &swings, &bursts, &hops);
    std::printf("  Fireburst: %d landed swings, %d bursts, %d hops landed\n", swings, bursts, hops);
    check(bursts > 0 && hops >= bursts, "a Fireburst bursts and its fire balls land");
    if (swings > 0) {
        const double rate = double(bursts) / swings;
        check(rate > 0.05 && rate < 0.16, "about one landed swing in ten");
    }
    int ringSwings = 0, rings = 0, ringBlows = 0;
    hunt(Power::FireRing, &ringSwings, &rings, &ringBlows);
    std::printf("  Ring of Fire: %d landed swings, %d rings, %d blows\n", ringSwings, rings,
                ringBlows);
    check(rings > 0 && ringBlows >= rings, "a Ring of Fire goes off and strikes round him");
    if (ringSwings > 0) {
        const double rate = double(rings) / ringSwings;
        check(rate > 0.05 && rate < 0.16, "about one landed swing in ten");
    }
}

// The runes' rarity (sim::Rarity, the user, 2026-10-02: "we need also make group of rarity of
// runes"): a dropped Rune of Creation is Rare, Epic or Legendary at kRuneRarityShare, every
// class having all three -- from a monster that reaches every rarity. And each rarity starts at
// its kRuneRarityLevel (an Epic out of Dungeon 1 "does not make sense"; a Legendary starts at the
// Lost Tower's fifth floor): at 40 no Legendary, at 30 Rares alone.
void testRuneRarity(const content::Tables& tables) {
    std::printf("rune rarity\n");
    const int rune = tables.itemAt(14, 22);
    check(rune >= 0, "the Rune of Creation");
    if (rune < 0) return;
    for (const sim::Kin kin : {sim::Kin::DarkKnight, sim::Kin::DarkWizard, sim::Kin::FairyElf}) {
        for (const int level : {70, 40, 30}) {
            sim::Realm realm;
            realm.raise(&tables, 11, 138, 124, kin, 50);
            int tally[3] = {}, runes = 0, unsettable = 0;
            for (int i = 0; i < 1000000 && runes < 1500; ++i) {
                realm.dropFor(level);
                if (realm.lying().empty() || realm.lying().back().what.item != rune) continue;
                const sim::PowerRow* power = sim::powerOf(realm.lying().back().what.powers[0]);
                if (!power) continue;
                ++runes;
                ++tally[int(power->rarity)];
                unsettable += !power->takenBy(kin);
            }
            std::printf("  kin %d, level %d: %d runes, %d rare, %d epic, %d legendary\n", int(kin),
                        level, runes, tally[0], tally[1], tally[2]);
            checkEqual(unsettable, 0, "every rune drawn is one his class may set");
            double reachable = 0.0;
            for (int r = 0; r < 3; ++r)
                reachable += level >= sim::kRuneRarityLevel[r] ? sim::kRuneRarityShare[r] : 0.0;
            for (int r = 0; r < 3; ++r) {
                const double share = runes > 0 ? double(tally[r]) / runes : 0.0;
                const double want =
                    level >= sim::kRuneRarityLevel[r] ? sim::kRuneRarityShare[r] / reachable : 0.0;
                check(std::abs(share - want) < 0.05, "each rarity drops at its share, or not at all");
            }
        }
    }
}

// The pets at WebZen's word (docs/pets.md, 2026-09-30): the Angel's 30%, the Imp's 3 life a
// landed blow, and their wear.
// The Dungeon's runes (sim/items.h) and Devin's Renewal, by what they DO in a fight, each against
// the same seeded fight without it: Keen Eye's crits, Bloodwell's life on a wound, Frenzy's
// quicker swing and cast, and Renewal's health off a safe tile with nothing near.
void testDungeonRunes(const content::Tables& tables) {
    std::printf("dungeon runes\n");
    const int helm = tables.itemNamed("HelmMale09"), pants = tables.itemNamed("PantMale10");
    const int boots = tables.itemNamed("BootMale02");
    check(helm >= 0 && pants >= 0 && boots >= 0, "a Brass Helm, Plate Pants and Dragon Boots");
    if (helm < 0 || pants < 0 || boots < 0) return;
    struct Tally {
        int blows = 0, crits = 0, dealt = 0, healed = 0, frenzied = 0;
        int swingBare = 0, fastest = 1 << 30, mostStacks = 0;
        double critChance = 0.0;
        sim::Excellence excel;
    };
    // testPets' fight: the knight on the nearest monster, the ticks he lands and is not struck
    // (and does not level, which fills him) counted for what they give back.
    const auto fight = [&](int item, int slot, sim::Power power, Tally* t) {
        sim::Realm realm;
        realm.raise(&tables, 5, 200, 160, sim::Kin::DarkKnight, 40);
        if (item >= 0) {
            const uint8_t powers[3] = {uint8_t(power), 0, 0};
            realm.give(item, slot, 0, -1, false, 0, 0, 1, powers);
        }
        t->swingBare = realm.hero().swingTicks;
        t->critChance = realm.hero().stats.criticalChance;
        t->excel = realm.hero().excel;
        uint32_t fighting = 0;
        // And not the ticks a kill's life lands (kKillLifeTicks after it), which is no steal.
        std::vector<int64_t> killLife;
        for (int tick = 0; tick < 20000 && realm.hero().alive(); ++tick) {
            // Kept on his feet, so both fights last: filled when low, and that tick not counted.
            if (realm.hero().health < realm.hero().maxHealth / 3) {
                sim::HeroRecord record = realm.record();
                record.health = realm.hero().maxHealth;
                realm.restore(record);
                fighting = 0;
                realm.step();
                continue;
            }
            const sim::Body& hero = realm.hero();
            uint32_t nearest = 0;
            float best = 1e9f;
            for (const sim::Body& one : realm.bodies()) {
                if (!one.monster() || !one.alive()) continue;
                const float dx = one.x - hero.x, dy = one.y - hero.y;
                if (dx * dx + dy * dy < best) {
                    best = dx * dx + dy * dy;
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
            const int before = hero.health;
            realm.step();
            int wound = 0;
            bool struck = false, levelled = false;
            for (const sim::Happening& h : realm.happenings()) {
                if (h.what == sim::What::Levelled) levelled = true;
                if (h.what == sim::What::Died && h.whom == realm.hero().id) {
                    killLife.push_back(realm.tick() + sim::kKillLifeTicks);
                }
                if (h.what != sim::What::Hit || h.reflected) continue;
                if (h.who == realm.hero().id) {
                    ++t->blows;
                    t->crits += h.critical ? 1 : 0;
                    wound += h.a;
                } else if (h.whom == realm.hero().id) {
                    struck = true;
                }
            }
            bool sipped = false;
            for (int64_t due : killLife) sipped |= std::abs(realm.tick() - due) <= 1;
            if (wound > 0 && !struck && !levelled && !sipped && realm.hero().alive()) {
                t->dealt += wound;
                t->healed += std::max(0, realm.hero().health - before);
            }
            if (realm.hero().frenzyUntil > realm.tick()) {
                ++t->frenzied;
                t->fastest = std::min(t->fastest, realm.hero().swingTicks);
                t->mostStacks = std::max(t->mostStacks, realm.hero().frenzyStacks);
            }
        }
    };
    Tally bare, keen, blood, frenzy;
    fight(-1, 0, sim::Power::None, &bare);
    fight(helm, sim::kHelm, sim::Power::KeenEye, &keen);
    fight(pants, sim::kPants, sim::Power::Bloodwell, &blood);
    fight(boots, sim::kBoots, sim::Power::Frenzy, &frenzy);
    const auto rate = [](int n, int of) { return double(n) / double(std::max(1, of)); };
    std::printf("  bare: %d blows, %.3f crit; keen eye: %d blows, %.3f crit\n", bare.blows,
                rate(bare.crits, bare.blows), keen.blows, rate(keen.crits, keen.blows));
    std::printf("  bloodwell: %d dealt, %d healed (%.3f); bare healed %d of %d\n", blood.dealt,
                blood.healed, rate(blood.healed, blood.dealt), bare.healed, bare.dealt);
    std::printf("  frenzy: %d ticks frenzied, swing %d ticks bare, %d at its fastest\n",
                frenzy.frenzied, frenzy.swingBare, frenzy.fastest);

    // Keen Eye: exactly its tenth on the chance, and about a tenth more crits in the fight.
    check(std::fabs(keen.critChance - bare.critChance - sim::kKeenEyeCritical) < 1e-9,
          "Keen Eye adds a tenth to his critical chance");
    check(bare.blows > 500 && keen.blows > 500, "both knights fight long enough to count");
    const double more = rate(keen.crits, keen.blows) - rate(bare.crits, bare.blows);
    check(more > 0.05 && more < 0.15, "and about a tenth more of his blows are critical");

    // And the rule alone, off the fight: a tenth of the blows that land, at Keen Eye's chance.
    {
        sim::Fighter hitter, target;
        hitter.level = 40;
        hitter.attackRate = 200.0f;
        hitter.minimumDamage = 10;
        hitter.maximumDamage = 20;
        hitter.criticalChance = sim::kKeenEyeCritical;
        target.level = 20;
        target.defenseRate = 20.0f;
        sim::Random dice(7);
        int hits = 0, crits = 0;
        for (int i = 0; i < 100000; ++i) {
            const sim::Blow blow = sim::strike(hitter, target, dice);
            hits += blow.hit ? 1 : 0;
            crits += blow.critical ? 1 : 0;
        }
        std::printf("  keen eye alone: %d of %d hits critical (%.3f)\n", crits, hits,
                    rate(crits, hits));
        check(std::fabs(rate(crits, hits) - sim::kKeenEyeCritical) < 0.01,
              "at its chance a tenth of landed blows are critical");
    }

    // Bloodwell: its shares on him, about 3% of every wound back as life, none without it.
    check(std::fabs(blood.excel.lifeSteal - sim::kBloodwellLife) < 1e-9 &&
              std::fabs(blood.excel.killMana - sim::kBloodwellMana) < 1e-9,
          "Bloodwell carries 3% life a wound and 5% mana a kill");
    const double back = rate(blood.healed, blood.dealt);
    check(blood.dealt > 1000 && back > 0.02 && back < 0.04,
          "about 3% of what he deals comes back as life");
    checkEqual(bare.healed, 0, "and none without it, off a safe tile");

    // Frenzy: it fires, and while it stands his swing is shorter.
    check(frenzy.frenzied > 0, "Frenzy fires in a fight");
    check(frenzy.fastest < frenzy.swingBare, "and his swing is shorter while it stands");
    // Diablo 3's shape: a stack a wound, up to five, never more.
    checkEqual(frenzy.mostStacks, sim::kFrenzyMostStacks, "and it stacks to five in a fight");
    // And the cast: a wizard's Energy Ball under five stacks is quicker.
    if (const sim::SkillRow* ball = sim::skillNumbered(sim::skill::kEnergyBall)) {
        const int agility = sim::startingPoints(sim::Kin::DarkWizard).agility;
        const int32_t plain = sim::castTicks(tables, sim::Kin::DarkWizard, agility, nullptr,
                                             nullptr, *ball);
        const int32_t quick = sim::castTicks(tables, sim::Kin::DarkWizard, agility, nullptr,
                                             nullptr, *ball,
                                             sim::kFrenzyStackSpeed * sim::kFrenzyMostStacks);
        std::printf("  energy ball: %d ticks, %d with a Frenzy\n", plain, quick);
        check(plain > 0 && quick < plain, "and a wizard's Energy Ball casts quicker under it");
    }

    // Renewal: with no monster on the map and off a safe tile, 3% of his health every three
    // seconds; nothing without it.
    content::Tables quiet = tables;
    quiet.nests.clear();
    const auto renew = [&](bool worn) {
        sim::Realm realm;
        realm.raise(&quiet, 5, 200, 160, sim::Kin::DarkKnight, 40);
        if (worn) {
            const uint8_t powers[3] = {uint8_t(sim::Power::Renewal), 0, 0};
            realm.give(boots, sim::kBoots, 0, -1, false, 0, 0, 1, powers);
        }
        sim::HeroRecord record = realm.record();
        record.health = 1;
        realm.restore(record);
        check(!quiet.grid.safe(realm.hero().column(), realm.hero().row()), "he stands off a safe tile");
        const int from = realm.hero().health;
        for (int tick = 0; tick < 30 * 20; ++tick) realm.step();
        return std::pair<int, int>(realm.hero().health - from, realm.hero().maxHealth);
    };
    const auto [gained, most] = renew(true);
    const auto [bareGained, bareMost] = renew(false);
    std::printf("  renewal: %d of %d back in 30 s; bare %d\n", gained, most, bareGained);
    const int expected = int(double(most) * sim::kRenewalShare * 10.0);
    check(std::abs(gained - expected) <= int(double(most) * sim::kRenewalShare) + 1,
          "Renewal gives back 3% of his health every three seconds");
    checkEqual(bareGained, 0, "and none without it, off a safe tile");
    (void)bareMost;
}

// The four 0.75 pieces (docs/jewellery.md): where they go, what they carry, what they do worn.
void testJewellery(const content::Tables& tables) {
    std::printf("rings and pendants\n");
    const int ice = tables.itemAt(13, 8), poison = tables.itemAt(13, 9);
    const int lightning = tables.itemAt(13, 12), fire = tables.itemAt(13, 13);
    check(ice >= 0 && poison >= 0 && lightning >= 0 && fire >= 0, "the four are in the table");
    if (ice < 0 || poison < 0 || lightning < 0 || fire < 0) return;
    const content::ItemRow& ring = tables.items[size_t(ice)];
    const content::ItemRow& pendant = tables.items[size_t(fire)];
    check(ring.dropsFromMonsters() && !ring.jewel(), "a ring drops as an item, not a jewel");
    checkEqual(ring.dropLevel, 20, "the Ring of Ice from level 20");
    checkEqual(sim::placeOf(ring), int(sim::kRingRight), "a ring goes on the right hand");
    check(sim::placesIn(ring, sim::Kin::DarkWizard, sim::kRingLeft), "or the left");
    checkEqual(sim::placeOf(pendant), int(sim::kAmulet), "a pendant is the amulet");
    check(sim::placeOf(tables.items[size_t(tables.itemAt(13, 0))]) == sim::kPet,
          "the Angel is still a pet");
    check(!sim::takesOptions(ring) && sim::excellentable(ring), "no luck, but excellent");
    checkEqual(sim::optionValue(ring, 3), 3, "its option is 3% regeneration at 3");
    check(sim::excellentLine(ring, 5) == "Increase Max HP +4%", "a ring's family is the armour's");
    check(sim::excellentLine(tables.items[size_t(lightning)], 3) == "Increase Wizardry Dmg +2%",
          "the Pendant of Lightning's is the staff's");
    checkEqual(sim::resistanceOf(ring, 0), 0, "+0 resists nothing");
    checkEqual(sim::resistanceOf(ring, 4), 4, "+4 resists four");

    sim::Realm realm;
    realm.raise(&tables, 5, 200, 160, sim::Kin::DarkKnight, 40);
    check(realm.give(ice, sim::kRingRight, 3, -1, true, 2) >= 0, "a ring is worn on the right");
    check(realm.give(poison, sim::kRingLeft, 4, -1, false, 1) >= 0, "and one on the left");
    check(realm.give(fire, sim::kAmulet, 2, -1, false, 0) >= 0, "and the pendant");
    const sim::Excellence& e = realm.hero().excel;
    checkEqual(e.iceResistance, 3, "ice: the +3 ring's");
    checkEqual(e.poisonResistance, 4, "poison: the +4 ring's");
    checkEqual(e.lifeRegen, 3, "the two options summed");
    check(!realm.satchel()[sim::kRingRight].luck, "luck asked for is not kept");
    check(sim::sellingPrice(ring, 0, 1, false, 0, 0, false, 2, 0) >
              sim::sellingPrice(ring, 0, 1, false, 0, 0, false, 0, 0),
          "the option prices a ring up");
}

void testPets(const content::Tables& tables) {
    std::printf("pets\n");
    const int angel = tables.itemAt(13, 0), imp = tables.itemAt(13, 1);
    check(angel >= 0 && imp >= 0, "the Guardian Angel and the Imp are in the table");
    if (angel < 0 || imp < 0) return;
    const sim::PetPower a = sim::petPower(tables.items[size_t(angel)]);
    const sim::PetPower i = sim::petPower(tables.items[size_t(imp)]);
    check(a.taken == 0.7 && a.health == 50 && a.lifeCost == 0, "the Angel takes 30% and adds 50");
    check(a.dealt == 0.8, "and costs 20% of his own damage (ours)");
    check(i.dealt == 1.3 && i.lifeCost == 3, "the Imp adds 30% for 3 life a blow");
    check(a.wear == 0.03 && i.wear == 0.02, "and they wear at gObjSpriteDamage's rates");

    // Life across the ticks the hero lands a blow and takes none, per blow, with and without
    // the Imp: the difference is its price. The regeneration is the same in both and cancels.
    const auto fight = [&](int pet, int* landed, int* paid, int* petLife) {
        sim::Realm realm;
        realm.raise(&tables, 5, 200, 160, sim::Kin::DarkKnight, 40);
        if (pet >= 0) realm.give(pet, sim::kPet);
        uint32_t fighting = 0;
        for (int tick = 0; tick < 3000 && realm.hero().alive(); ++tick) {
            const sim::Body& hero = realm.hero();
            uint32_t nearest = 0;
            float best = 1e9f;
            for (const sim::Body& one : realm.bodies()) {
                if (!one.monster() || !one.alive()) continue;
                const float dx = one.x - hero.x, dy = one.y - hero.y;
                if (dx * dx + dy * dy < best) {
                    best = dx * dx + dy * dy;
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
            const int before = hero.health;
            realm.step();
            int hits = 0;
            bool struck = false;
            for (const sim::Happening& h : realm.happenings()) {
                if (h.what != sim::What::Hit) continue;
                if (h.who == realm.hero().id) ++hits;
                else if (h.whom == realm.hero().id) struck = true;
            }
            if (hits > 0 && !struck && realm.hero().alive()) {
                *landed += hits;
                *paid += before - realm.hero().health;
            }
        }
        *petLife = realm.satchel()[sim::kPet].empty() ? 0 : realm.satchel()[sim::kPet].durability;
    };
    int landed = 0, paid = 0, life = 0, bareLanded = 0, barePaid = 0, bareLife = 0;
    fight(imp, &landed, &paid, &life);
    fight(-1, &bareLanded, &barePaid, &bareLife);
    const double price = double(paid) / std::max(1, landed) - double(barePaid) / std::max(1, bareLanded);
    std::printf("  %d blows with the Imp, %.2f life a blow over bare; its life %d\n", landed, price,
                life);
    check(landed > 20 && bareLanded > 20, "the knight fights");
    check(price > 2.5 && price < 3.5, "the Imp costs 3 life a landed blow");
}

// The Horn of Uniria (docs/mount.md): the mount's slot, no power but the ride, faster than the
// run off a safe tile and gone in town, worn by a hundredth of each hit taken.
void testMount(const content::Tables& tables) {
    std::printf("the Horn of Uniria\n");
    const int horn = tables.itemAt(13, 2);
    check(horn >= 0, "the Horn of Uniria is in the table");
    if (horn < 0) return;
    const content::ItemRow& row = tables.items[size_t(horn)];
    checkEqual(sim::placeOf(row), int(sim::kMount), "it goes in the mount's slot, not the pets'");
    checkEqual(row.dropLevel, 25, "from level 25");
    const sim::PetPower power = sim::petPower(row);
    check(power.mount && power.taken == 1.0 && power.dealt == 1.0 && power.health == 0,
          "it is ridden and does nothing else");
    check(power.wear == 0.01, "and wears at gObjSpriteDamage's damage/100");
    checkEqual(int(sim::buyingPrice(row, 0, 1, false)), 15700, "dropLevel^3 + 100, to the hundred");
    check(sim::kRideFactor > sim::kRunFactor, "the ride is faster than the run");

    // Twenty ticks of the same walk east off the town, bare and mounted.
    const auto walked = [&](bool mounted, bool* rode) {
        sim::Realm realm;
        realm.raise(&tables, 5, 200, 160, sim::Kin::DarkKnight, 40);
        if (mounted) realm.give(horn, sim::kMount);
        sim::Request walk;
        walk.kind = sim::Request::Kind::WalkTo;
        walk.column = realm.hero().column() + 30;
        walk.row = realm.hero().row();
        realm.ask(walk);
        const float fromX = realm.hero().x, fromY = realm.hero().y;
        *rode = false;
        for (int tick = 0; tick < 20; ++tick) {
            realm.step();
            *rode |= realm.hero().riding;
        }
        const float dx = realm.hero().x - fromX, dy = realm.hero().y - fromY;
        return std::sqrt(dx * dx + dy * dy);
    };
    bool bareRode = false, rode = false;
    const float bare = walked(false, &bareRode), ridden = walked(true, &rode);
    std::printf("  20 ticks: %.2f tiles on foot, %.2f mounted\n", bare, ridden);
    check(!bareRode && rode, "he rides only with the horn worn");
    check(ridden > bare * 1.1f, "and covers more ground on it");

    // On a safe tile the horse is gone and he walks: the first safe tile of the grid.
    int safeColumn = -1, safeRow = -1;
    for (int r = 0; r < 256 && safeColumn < 0; ++r) {
        for (int c = 0; c < 256; ++c) {
            if (tables.grid.safe(c, r) && tables.grid.open(c, r)) {
                safeColumn = c;
                safeRow = r;
                break;
            }
        }
    }
    check(safeColumn >= 0, "the town has a safe tile");
    sim::Realm town;
    town.raise(&tables, 5, safeColumn, safeRow, sim::Kin::DarkKnight, 40);
    town.give(horn, sim::kMount);
    town.step();
    check(!town.hero().riding, "and in town he is on foot");

    // A pet and a mount at once, each in its own slot: the Angel's guard and the ride both.
    sim::Realm both;
    both.raise(&tables, 5, 200, 160, sim::Kin::DarkKnight, 40);
    const int angel = tables.itemAt(13, 0);
    check(angel >= 0 && both.give(angel, sim::kPet) >= 0 && both.give(horn, sim::kMount) >= 0,
          "the Angel and the horn are worn together");
    both.step();
    check(both.hero().riding && both.hero().pet.taken < 1.0,
          "and he rides with the Angel's guard on him");
}

// The Horn of Dinorant (docs/mount.md, 0.95d's): ridden as Uniria is, and MU's powers with it.
void testDinorant(const content::Tables& tables) {
    std::printf("the Horn of Dinorant\n");
    const int horn = tables.itemAt(13, 3);
    check(horn >= 0, "the Horn of Dinorant is in the table");
    if (horn < 0) return;
    const content::ItemRow& row = tables.items[size_t(horn)];
    checkEqual(sim::placeOf(row), int(sim::kMount), "it goes in the mount's slot");
    checkEqual(row.dropLevel, 160, "from level 160, the user's");
    check(!row.dropsFromMonsters(), "and never dropped");
    const sim::PetPower power = sim::petPower(row);
    check(power.mount && power.dealt == 1.15 && power.taken == 0.9 && power.lifeCost == 1,
          "ridden, x1.15 for 1 life a blow, x0.9 taken");
    check(power.wear == 1.0 / 200.0, "and wears at damage/200");
    sim::Realm realm;
    realm.raise(&tables, 5, 200, 160, sim::Kin::DarkKnight, 160);
    realm.give(horn, sim::kMount);
    realm.step();
    check(realm.hero().riding, "he rides it off a safe tile");
}

// A Thunder Lich's Lightning pushes him a tile straight away, slid as a pushed monster slides
// (the user, 2026-10-01). Undying beside one in the Dungeon, standing still, until it strikes.
void testLichPush() {
    std::printf("a Thunder Lich pushes him\n");
    content::Tables dungeon;
    std::string error;
    const std::string path = std::string(MU2_ASSET_DIR) + "/cooked/dungeon/dungeon.mur";
    check(content::loadTables(path, dungeon, error), "the Dungeon's tables load");
    sim::Realm realm;
    check(realm.raise(&dungeon, 5, 108, 246, sim::Kin::DarkKnight, 60), "a realm raises in the Dungeon");
    realm.undying(true);
    // One with open floor three and four tiles east of it, where he stands and where he goes.
    const auto clear = [&](int column, int row) {
        return dungeon.grid.open(column, row, content::kWallCharacter) && !dungeon.grid.safe(column, row);
    };
    const sim::Body* lich = nullptr;
    for (const sim::Body& body : realm.bodies()) {
        if (body.monster() && body.kind >= 0 && dungeon.kinds[size_t(body.kind)].number == 9 &&
            clear(body.column() + 1, body.row()) && clear(body.column() + 2, body.row()) &&
            clear(body.column() + 3, body.row()) && clear(body.column() + 4, body.row())) {
            lich = &body;
            break;
        }
    }
    check(lich != nullptr, "a Thunder Lich stands in the Dungeon");
    if (!lich) return;
    realm.setHeroDown(lich->column() + 3, lich->row(), -1, 0);
    int shoved = 0;
    float slid = 0.0f;
    int64_t swungAt = -1, shovedAt = -1;
    for (int tick = 0; tick < 600 && shoved == 0; ++tick) {
        const float x = realm.hero().x, y = realm.hero().y;
        realm.step();
        for (const sim::Happening& h : realm.happenings()) {
            // The beast's blow on him that pushes: the last before the shove.
            if (h.what == sim::What::Hit && h.whom == realm.hero().id &&
                h.who != realm.hero().id && shovedAt < 0) swungAt = realm.tick();
            if (h.what == sim::What::Shoved && h.who == realm.hero().id) {
                ++shoved;
                shovedAt = realm.tick();
            }
            if (std::getenv("PUSH_TRACE") && h.what != sim::What::Stepped) std::printf("    %s\n", sim::describe(h, realm).c_str());
        }
        if (shoved) {
            for (int t = 0; t < 8; ++t) realm.step();
            slid = std::max(std::fabs(realm.hero().x - x), std::fabs(realm.hero().y - y));
        }
    }
    check(shoved > 0, "its Lightning pushed him");
    // When its bolt lands, which the drawing shows 0.6 s into the swing, not at the strike.
    check(swungAt >= 0 && shovedAt - swungAt == sim::kBeastPushDelay,
          "the push waits for the bolt");
    check(slid >= 0.9f && slid <= 1.1f, "a tile, slid and landed");
    check(realm.hero().x == float(realm.hero().column()), "he stands on his tile after it");
}

// Evil Spirit (sim/items.h kSpiritChance): a knight with the rune in his shield among Lorencia's
// monsters lets the spirits go only on a tick a monster missed him, at about the rune's share of
// those misses; each release strikes, within kSpiritDelayTicks, about two in three of the monsters
// within kSpiritReach (WebZen's SkillEvil), once each; and the wizard's spell lets the same go
// for its ninety mana with no cooldown.
void testEvilSpirit(const content::Tables& tables) {
    std::printf("evil spirit\n");
    const int shield = tables.itemNamed("Shield12");
    check(shield >= 0, "a Serpent Shield");
    if (shield < 0) return;
    const auto nearestTo = [](const sim::Realm& realm) {
        const sim::Body& hero = realm.hero();
        uint32_t nearest = 0;
        float best = 1e9f;
        for (const sim::Body& one : realm.bodies()) {
            if (!one.monster() || !one.alive()) continue;
            const float dx = one.x - hero.x, dy = one.y - hero.y;
            if (dx * dx + dy * dy < best) {
                best = dx * dx + dy * dy;
                nearest = one.id;
            }
        }
        return nearest;
    };
    {
        sim::Realm realm;
        realm.raise(&tables, 5, 200, 160, sim::Kin::DarkKnight, 40);
        realm.undying(true);
        const uint8_t powers[3] = {uint8_t(sim::Power::Spirits), 0, 0};
        check(realm.give(shield, sim::kWeaponLeft, 0, -1, false, 0, 0, 1, powers) >= 0,
              "he wears the shield with the rune");
        checkEqual(realm.hero().excel.spirits, 1, "and it counts as one Evil Spirit");
        int misses = 0, releases = 0, offMiss = 0, blows = 0, late = 0, near = 0;
        std::vector<int64_t> released;
        uint32_t fighting = 0;
        for (int tick = 0; tick < 20000; ++tick) {
            const uint32_t nearest = nearestTo(realm);
            if (nearest != 0 && nearest != fighting) {
                fighting = nearest;
                sim::Request request;
                request.kind = sim::Request::Kind::Attack;
                request.target = nearest;
                realm.ask(request);
            }
            realm.step();
            bool missed = false;
            for (const sim::Happening& h : realm.happenings()) {
                if (h.what == sim::What::Missed && h.whom == realm.hero().id) missed = true;
            }
            misses += missed ? 1 : 0;
            for (const sim::Happening& h : realm.happenings()) {
                if (h.what == sim::What::Spirits) {
                    ++releases;
                    offMiss += missed ? 1 : 0;
                    released.push_back(realm.tick());
                    for (const sim::Body& one : realm.bodies()) {
                        const float dx = one.x - realm.hero().x, dy = one.y - realm.hero().y;
                        near += one.monster() && one.alive() &&
                                dx * dx + dy * dy < sim::kSpiritReach * sim::kSpiritReach;
                    }
                }
                if ((h.what == sim::What::Hit || h.what == sim::What::Missed) &&
                    h.who == realm.hero().id && h.rune && h.thrown) {
                    ++blows;
                    bool held = false;
                    for (int64_t at : released) {
                        held |= realm.tick() > at && realm.tick() <= at + sim::kSpiritDelayTicks;
                    }
                    late += held ? 0 : 1;
                }
            }
        }
        std::printf("  %d misses on him, %d releases, %d near, %d spirit blows\n", misses,
                    releases, near, blows);
        check(releases > 0 && offMiss == releases, "the spirits go only on a monster's miss");
        const double share = double(releases) / double(std::max(1, misses));
        check(share > 0.05 && share < sim::kSpiritChance + 0.03,
              "at about the rune's chance of his misses, fewer while one goes");
        const double struck = double(blows) / double(std::max(1, near));
        check(struck > 0.5 && struck < 0.8, "about two in three of those near are struck, once");
        checkEqual(late, 0, "each within its two seconds");
    }
    {
        sim::Realm realm;
        realm.raise(&tables, 5, 200, 160, sim::Kin::DarkWizard, 60);
        realm.undying(true);
        check(realm.learn(sim::skill::kEvilSpirit), "the wizard learns Evil Spirit");
        sim::HeroRecord record = realm.record();
        record.mana = realm.hero().maxMana;
        realm.restore(record);
        int casts = 0, blows = 0;
        int64_t cooled = 0;
        for (int tick = 0; tick < 4000 && casts < 3; ++tick) {
            const uint32_t nearest = nearestTo(realm);
            const sim::Body* aim = realm.find(nearest);
            if (aim && std::hypot(aim->x - realm.hero().x, aim->y - realm.hero().y) < 3.5f &&
                realm.hero().mana >= 90) {
                realm.invoke(sim::skill::kEvilSpirit, nearest);
            } else if (nearest != 0) {
                sim::Request request;
                request.kind = sim::Request::Kind::Attack;
                request.target = nearest;
                realm.ask(request);
            }
            realm.step();
            for (const sim::Happening& h : realm.happenings()) {
                if (h.what == sim::What::Spirits && h.who == realm.hero().id) {
                    ++casts;
                    cooled = std::max(cooled, realm.cooling(sim::skill::kEvilSpirit));
                }
                if (h.what == sim::What::Hit && h.who == realm.hero().id && h.thrown && !h.rune) {
                    ++blows;
                }
            }
            if (realm.hero().mana < 90) {
                sim::HeroRecord again = realm.record();
                again.mana = realm.hero().maxMana;
                realm.restore(again);
            }
        }
        check(casts >= 3, "the wizard's Evil Spirit lets the spirits go");
        checkEqual(int(cooled), 0, "with no cooldown");
        check(blows > 0, "and they strike in his own colour");
    }
}

// What deaths leave, counted over many of them with no hunt (Realm::dropFor): the three jewels
// at kJewelChance shared among those a level reaches, the Rune of Creation at kCreationChance
// from kCreationLevel with a power the killer may set, the group's others still at MU's 1 in
// 1000, and the item chance untouched.
void testDrops(const content::Tables& tables) {
    std::printf("drops\n");
    const int bless = tables.itemAt(14, 13), soul = tables.itemAt(14, 14);
    const int chaos = tables.itemAt(12, 15), rune = tables.itemAt(14, 22);
    check(bless >= 0 && soul >= 0 && chaos >= 0 && rune >= 0, "the four jewels are in the table");
    if (bless < 0 || soul < 0 || chaos < 0 || rune < 0) return;
    sim::Realm realm;
    check(realm.raise(&tables, 11, 138, 124, sim::Kin::DarkKnight, 50), "a realm raises");
    constexpr int kDeaths = 400000;
    const auto near = [](int got, double want, double slack) {
        return std::abs(double(got) - want) <= want * slack;
    };
    for (const int level : {10, 14, 26, 40, 70}) {
        int b = 0, s = 0, c = 0, r = 0, others = 0, items = 0, unsettable = 0;
        for (int i = 0; i < kDeaths; ++i) {
            realm.dropFor(level);
            if (realm.lying().empty()) continue;
            const sim::Held& what = realm.lying().back().what;
            const content::ItemRow& row = tables.items[size_t(what.item)];
            if (what.item == bless) ++b;
            else if (what.item == soul) ++s;
            else if (what.item == chaos) ++c;
            else if (what.item == rune) {
                ++r;
                const sim::PowerRow* power = sim::powerOf(what.powers[0]);
                unsettable += !power || !power->takenBy(sim::Kin::DarkKnight);
            } else if (row.jewel() || (row.group == 14 && (row.number == 9 || row.number == 10))) {
                ++others;
            } else {
                ++items;
            }
        }
        std::printf("  level %d, %d deaths: bless %d, soul %d, chaos %d, rune %d, group %d, items %d\n",
                    level, kDeaths, b, s, c, r, others, items);
        const bool hasBless = level >= 25, hasSoul = level >= 30, hasChaos = level >= 12 && level <= 66;
        const int reached = int(hasBless) + int(hasSoul) + int(hasChaos);
        const double each = reached ? kDeaths * sim::kJewelChance / reached : 0.0;
        const auto said = [level](const char* what) {
            static std::string line;
            line = std::string(what) + " at level " + std::to_string(level);
            return line.c_str();
        };
        check(hasBless ? near(b, each, 0.15) : b == 0, said("the Bless at its share"));
        check(hasSoul ? near(s, each, 0.15) : s == 0, said("the Soul at its share"));
        check(hasChaos ? near(c, each, 0.15) : c == 0, said("the Chaos at its share"));
        check(level >= sim::kCreationLevel ? near(r, kDeaths * sim::kCreationChance, 0.25) : r == 0,
              said("the Rune of Creation at its chance"));
        checkEqual((long long)unsettable, 0LL, said("every dropped rune holds a power he may set"));
        check(near(items, kDeaths * 0.1, 0.05), said("the item chance is untouched"));
        // The group's others also fall as items within fifteen levels of theirs (the Portal at
        // 30, the pets at 23 and 28), so the group is checked only past that, where all fall.
        if (level > 45) {
            check(near(others, kDeaths * sim::kJewelGroupChance, 0.25),
                  said("the group's others at 1 in 1000"));
        }
    }
}

// Putting a thing on takes down whatever it cannot be worn with, into the bag (the user,
// 2026-10-02): a two-handed weapon over a sword and shield takes both off, a crossbow over a bow
// takes the bow and its arrows, and every bow and crossbow goes in the one weapon slot.
void testWearingTakesDown(const content::Tables& tables) {
    std::printf("wearing takes down\n");
    // The first row of its kind this character can wear, or -1.
    const auto first = [&](const sim::Realm& realm, auto&& wanted) {
        for (size_t i = 0; i < tables.items.size(); ++i) {
            const content::ItemRow& row = tables.items[i];
            if (wanted(row) && sim::fits(tables, realm.wearer(), sim::Held{int32_t(i), 0, 1})) {
                return int32_t(i);
            }
        }
        return int32_t(-1);
    };
    const auto bow = [](const content::ItemRow& r) {
        return r.group == sim::kGroupBows && !sim::ammunition(r);
    };
    const auto count = [](const sim::Satchel& bag) {
        int n = 0;
        for (int s = sim::kWorn; s < sim::kSlots; ++s) n += !bag[s].empty();
        return n;
    };

    sim::Realm knight;
    check(knight.raise(&tables, 3, 138, 124, sim::Kin::DarkKnight, 60), "a knight raises");
    knight.spend(knight.hero().pointsInHand * 3 / 4, knight.hero().pointsInHand / 4, 0, 0);
    const int32_t sword = first(knight, [](const content::ItemRow& r) {
        return r.weapon() && !r.shield() && r.group < sim::kGroupBows && !r.twoHanded();
    });
    const int32_t shield = first(knight, [](const content::ItemRow& r) { return r.shield(); });
    const int32_t big = first(knight, [](const content::ItemRow& r) {
        return r.weapon() && r.group < sim::kGroupBows && r.twoHanded();
    });
    check(sword >= 0 && shield >= 0 && big >= 0, "a sword, a shield and a two-hander he wears");
    if (sword >= 0 && shield >= 0 && big >= 0) {
        knight.give(sword, sim::kWeaponRight);
        knight.give(shield, sim::kWeaponLeft);
        const int at = knight.give(big);
        check(sim::movable(tables, knight.wearer(), knight.satchel(), at, sim::kWeaponRight),
              "the two-hander lights the weapon slot");
        check(knight.moveItem(at, sim::kWeaponRight), "and goes on");
        checkEqual(knight.satchel()[sim::kWeaponRight].item, big, "in the weapon hand");
        check(knight.satchel()[sim::kWeaponLeft].empty(), "the shield came off");
        checkEqual(count(knight.satchel()), 2, "and both it and the sword are in the bag");

        // And the shield back on over the two-hander: the two-hander comes down.
        int shieldAt = -1;
        for (int s = sim::kWorn; s < sim::kSlots; ++s) {
            if (knight.satchel()[s].item == shield) shieldAt = s;
        }
        check(knight.moveItem(shieldAt, sim::kWeaponLeft), "the shield goes back on");
        check(knight.satchel()[sim::kWeaponRight].empty(), "and takes the two-hander off");

        // A bag with no room for what would come down refuses, whole.
        sim::Realm full;
        full.raise(&tables, 3, 138, 124, sim::Kin::DarkKnight, 60);
        full.spend(full.hero().pointsInHand * 3 / 4, full.hero().pointsInHand / 4, 0, 0);
        full.give(sword, sim::kWeaponRight);
        full.give(shield, sim::kWeaponLeft);
        const int bigAt = full.give(big);
        const int32_t apple = tables.itemAt(14, 0);
        while (full.give(apple, -1, 0, 1) >= 0) {}
        check(!full.moveItem(bigAt, sim::kWeaponRight), "a full bag refuses the two-hander");
        checkEqual(full.satchel()[sim::kWeaponLeft].item, shield, "and the shield stays on");
    }

    sim::Realm elf;
    check(elf.raise(&tables, 5, 212, 198, sim::Kin::FairyElf, 60), "an elf raises");
    elf.spend(elf.hero().pointsInHand / 4, elf.hero().pointsInHand * 3 / 4, 0, 0);
    const int32_t shortBow = first(elf, [&](const content::ItemRow& r) {
        return bow(r) && r.number <= 6;
    });
    const int32_t cross = first(elf, [&](const content::ItemRow& r) {
        return bow(r) && r.number > 6;
    });
    check(shortBow >= 0 && cross >= 0, "a bow and a crossbow she wears");
    if (shortBow >= 0 && cross >= 0) {
        checkEqual(sim::placeOf(tables.items[size_t(shortBow)]), int(sim::kWeaponRight),
                   "a bow goes in the weapon slot");
        checkEqual(sim::placeOf(tables.items[size_t(cross)]), int(sim::kWeaponRight),
                   "and so does a crossbow");
        const int32_t arrows = tables.itemAt(sim::kGroupBows, 15);
        elf.give(shortBow, sim::kWeaponRight);
        elf.give(arrows, sim::kWeaponLeft, 0, 100);
        const int at = elf.give(cross);
        check(elf.moveItem(at, sim::kWeaponRight), "the crossbow goes on over the bow");
        checkEqual(elf.satchel()[sim::kWeaponRight].item, cross, "in the weapon slot");
        check(elf.satchel()[sim::kWeaponLeft].empty(), "and the arrows, which it cannot shoot, came off");
        checkEqual(count(elf.satchel()), 2, "the bow and the arrows are in the bag");
        const int32_t bolts = tables.itemAt(sim::kGroupBows, 7);
        const int boltAt = elf.give(bolts, -1, 0, 100);
        check(elf.moveItem(boltAt, sim::kWeaponLeft), "the bolts go on beside it");
        checkEqual(elf.satchel()[sim::kWeaponRight].item, cross, "and the crossbow stays");
    }
}

// A thrown Firecracker (sim/items.h, Realm::crack): WebZen's FireCrackerOpenEven.
void testFirecracker(const content::Tables& tables) {
    std::printf("firecracker\n");
    const int cracker = tables.itemAt(14, 11);
    check(cracker >= 0, "the Firecracker has a row (14, 11)");
    if (cracker < 0) return;
    int present = 0;
    for (const sim::BagRow& row : sim::kFirecrackerBag) present += tables.itemAt(row.group, row.number) >= 0;
    std::printf("  %d of eventitembag5's %zu rows are items here\n", present,
                std::size(sim::kFirecrackerBag));
    check(present > 0, "and some of its bag is here to draw");

    {
        sim::Realm realm;
        realm.raise(&tables, 11, 138, 124, sim::Kin::DarkKnight, 50);
        const int sword = realm.give(tables.itemAt(0, 1));
        check(!realm.cracks(sword), "a sword is thrown, not cracked");
        check(!realm.crack(sword).opened && !realm.satchel()[sword].empty(),
              "and crack refuses it and leaves it be");
        check(!realm.crack(-1).opened && !realm.crack(sim::kSlots).opened, "nor a slot that is none");
    }

    const int tries = 20000;
    int items = 0, zens = 0, lucky = 0, optioned = 0, bare = 0, badPlus = 0, badZen = 0, kept = 0;
    int plusSeen[10] = {};
    // A fresh realm every two hundred, so the ground under him does not fill, each on its own
    // seed so the two hundred are not the same two hundred again.
    auto fresh = std::make_unique<sim::Realm>();
    for (int i = 0; i < tries; ++i) {
        if (i % 200 == 0) {
            fresh = std::make_unique<sim::Realm>();
            fresh->raise(&tables, uint32_t(11 + i), 138, 124, sim::Kin::DarkKnight, 50);
        }
        sim::Realm& realm = *fresh;
        const int slot = realm.give(cracker);
        if (!realm.cracks(slot)) {
            ++kept;
            continue;
        }
        const int64_t purse = realm.money();
        const sim::Cracked cracked = realm.crack(slot);
        kept += !realm.satchel()[slot].empty();
        if (cracked.id == 0) {
            ++zens;
            badZen += realm.money() - purse != sim::kFirecrackerZen;
            continue;
        }
        ++items;
        const sim::Lying& lying = realm.lying().back();
        const content::ItemRow& row = tables.items[size_t(lying.what.item)];
        if (!sim::takesOptions(row)) {
            bare += lying.what.refinement == 0 && !lying.what.luck && lying.what.option == 0;
            continue;
        }
        const int plus = lying.what.refinement;
        badPlus += plus < sim::kFirecrackerPlus ||
                   plus >= sim::kFirecrackerPlus + sim::kFirecrackerPluses;
        if (plus >= 0 && plus < 10) ++plusSeen[plus];
        lucky += lying.what.luck;
        optioned += lying.what.option > 0;
    }
    std::printf("  %d thrown: %d items, %d Zen; +5..+9 %d %d %d %d %d\n", tries, items, zens,
                plusSeen[5], plusSeen[6], plusSeen[7], plusSeen[8], plusSeen[9]);
    checkEqual(kept, 0, "every one thrown is spent");
    check(std::abs(double(items) / tries - 0.2) < 0.015, "two in ten give an item");
    checkEqual(badZen, 0, "the rest give 2,004 Zen into the purse");
    checkEqual(badPlus, 0, "an item comes +5 to +9");
    for (int plus = 5; plus <= 9; ++plus) check(plusSeen[plus] > 0, "each of +5 to +9 comes");
    int optionable = 0;
    for (int plus = 0; plus < 10; ++plus) optionable += plusSeen[plus];
    if (optionable > 0) {
        check(std::abs(double(lucky) / optionable - 0.5) < 0.05, "luck half the time");
        // The option where the skill or the luck is missing (three in four), then four in five
        // of those draw +0, +4 or +8 and one in five +12: 0.75 x (0.2 + 0.8 x 2/3) = 0.55.
        check(std::abs(double(optioned) / optionable - 0.55) < 0.05, "an option 55% of the time");
    }
    checkEqual(bare, items - optionable, "a jewel out of one comes bare");

    // Where it falls: a dungeon's kill of level 17 or over, one in kFirecrackerOdds; never in
    // Lorencia, never from a monster under 17.
    const auto crackersIn = [&](const content::Tables& map, int level, int kills) {
        int found = 0;
        auto at = std::make_unique<sim::Realm>();
        for (int i = 0; i < kills; ++i) {
            if (i % 500 == 0) {
                at = std::make_unique<sim::Realm>();
                at->raise(&map, uint32_t(77 + i), 138, 124, sim::Kin::DarkKnight, 50);
            }
            at->dropFor(level);
            for (const sim::Lying& one : at->lying()) found += one.what.item == cracker;
        }
        return found;
    };
    checkEqual(crackersIn(tables, 40, 30000), 0, "Lorencia's kills leave no Firecracker");
    content::Tables dungeon;
    std::string error;
    if (!content::loadTables(std::string(MU2_ASSET_DIR) + "/cooked/dungeon/dungeon.mur", dungeon,
                             error)) {
        check(false, "the Dungeon's tables load");
        return;
    }
    const int kills = 90000;
    const int fallen = crackersIn(dungeon, 40, kills);
    std::printf("  the Dungeon: %d Firecrackers off %d kills at level 40 (one in %d wanted)\n", fallen,
                kills, sim::kFirecrackerOdds);
    const double want = double(kills) / sim::kFirecrackerOdds;
    check(std::abs(fallen - want) < want * 0.2, "a dungeon's kill leaves one one time in 300");
    checkEqual(crackersIn(dungeon, sim::kFirecrackerFromLevel - 1, 30000), 0,
               "never from a monster under 17");
}

// Blood Castle's grid changes under the run (sim/event.h): the realm copies the castle's tables at
// raise, so opening the bridge and the door changes its grid and nobody else's, the router sees it
// on the next plan, and a raise again starts closed. Lorencia's tables are shared and refuse.
void testCastleGrid(const content::Tables& lorencia) {
    std::printf("blood castle's live grid\n");
    content::Tables castle;
    std::string error;
    check(content::loadTables(std::string(MU2_ASSET_DIR) + "/cooked/bloodcastle/bloodcastle.mur",
                              castle, error),
          "Blood Castle's tables load");
    if (castle.grid.empty()) return;
    const std::vector<uint16_t> cooked = castle.grid.words();
    sim::Realm realm;
    check(realm.raise(&castle, 3, 13, 8), "the castle's realm raises");
    check(realm.tables() != &castle, "and keeps its own copy of the tables");
    const auto reaches = [&](int column, int row) {
        sim::Router router;
        router.open(&realm.tables()->grid);
        std::vector<sim::Step> route;
        return router.plan(13, 8, column, row, content::kWallCharacter, route);
    };
    check(reaches(14, 40), "the road down to the bridge is open from the court");
    check(!reaches(14, 85), "the courtyard is shut behind the raised bridge and the door");
    const sim::GridBox& bridge = sim::kCastleBridge;
    check(realm.changeGrid(bridge.x1, bridge.y1, bridge.x2, bridge.y2, bridge.bits, false),
          "the bridge's gap is filled");
    check(!reaches(14, 85), "the door still holds");
    for (const sim::GridBox& box : sim::kCastleDoor) {
        realm.changeGrid(box.x1, box.y1, box.x2, box.y2, box.bits, false);
    }
    check(reaches(14, 85), "with the door down the courtyard plans");
    check(castle.grid.words() == cooked, "the cooked tables are untouched");
    check(sim::castleEntryLeft(10 * 3600 + 25 * 60) == 300, "the Messenger opens at hh:25");
    check(sim::castleEntryLeft(10 * 3600 + 29 * 60 + 59) == 1, "until hh:29:59");
    check(sim::castleEntryLeft(10 * 3600 + 30 * 60) == 0, "and is shut at hh:30");
    check(sim::castleEntryLeft(10) == 0, "and past midnight");
    check(realm.raise(&castle, 3, 13, 8), "a raise again");
    check(!reaches(14, 85), "starts with the castle closed");
    sim::Realm town;
    check(town.raise(&lorencia, 3, 138, 124), "Lorencia raises");
    check(!town.changeGrid(138, 124, 138, 124, content::kNoMove, true),
          "and its shared grid refuses a change");
    check(town.tables() == &lorencia, "on the shared tables");
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
    testDrops(tables);
    testLichPush();
    testStandsOverTheKill(tables);
    testSkills(tables);
    testCastLock(tables);
    testPerches(tables);
    testVault(tables);
    testDeviasFolk();
    testTowerKeeper();
    testThroughWalls();
    testCastleGrid(tables);
    testCharon();
    testChaosMachine();
    testRefine(tables);
    testOptions(tables);
    testExcellent(tables);
    testSets(tables);
    testWear(tables);
    testRecovery(tables);
    testWardens(tables);
    testArcherHolds(tables);
    testStrollers(tables);
    testArchery(tables);
    testWearingTakesDown(tables);
    testElfSkills(tables);
    testSummons(tables);
    testSummonAggro(tables);
    testSummonKeepsUp(tables);
    testSummonOnAHunt(tables);
    testGates(tables);
    testDungeonGates(tables);
    testTraps();
    testQuests(tables);
    testDungeonRunes(tables);
    testElementRunes(tables);
    testGroupRunes(tables);
    testRuneRarity(tables);
    testEvilSpirit(tables);
    testRunes(tables);
    testJewellery(tables);
    testPets(tables);
    testMount(tables);
    testDinorant(tables);
    testPoisonOnce();
    testWishDropsOnWalk(tables);
    testTravelQuestLock();
    testFirecracker(tables);

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
