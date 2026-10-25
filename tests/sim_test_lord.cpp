// The Dark Lord (docs/plans/dark-lord.md): his fifth stat, Command, on the hero, through Spend,
// a points reset and a character's bytes. Run from tests/sim_test.cpp's main.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "content/tables.h"
#include "net/wire.h"
#include "sim/realm.h"
#include "sim/rules.h"
#include "sim/skills.h"

using namespace mu;

void testDarkLord(void (*check)(bool, const char*), void (*checkEqual)(long long, long long, const char*)) {
    content::Tables lorencia;
    std::string error;
    check(content::loadTables(std::string(MU2_ASSET_DIR) + "/cooked/lorencia/lorencia.mur", lorencia, error),
          "Lorencia's tables load");

    // His Command: 25 to start, and a tenth of it in his attack rate (ObjBaseAttack.cpp:921-923).
    const sim::HeroPoints lord = sim::startingPoints(sim::Kin::DarkLord);
    checkEqual(lord.command, 25, "the lord starts with 25 Command");
    checkEqual(sim::startingPoints(sim::Kin::DarkKnight).command, 0, "and nobody else has any");
    sim::Fighter fighter;
    int health = 0;
    sim::reckon(sim::Kin::DarkLord, 1, lord, sim::Arms{}, &fighter, &health);
    sim::Fighter without;
    sim::HeroPoints none = lord;
    none.command = 0;
    sim::reckon(sim::Kin::DarkLord, 1, none, sim::Arms{}, &without, &health);
    check(fighter.attackRate - without.attackRate > 2.49f && fighter.attackRate - without.attackRate < 2.51f,
          "25 Command is 2.5 attack rate");
    checkEqual(sim::maximumShield(1, lord, 0) - sim::maximumShield(1, none, 0), 30,
               "and 1.2 shield a point of it, as the other four");

    // Spend: Command is a 4, and his alone.
    sim::Realm realm;
    check(realm.raise(&lorencia, 0, 130, 130, sim::Kin::DarkLord, 10), "a level-10 Dark Lord raises");
    const int inHand = realm.hero().pointsInHand;
    check(inHand >= 63, "nine levels at seven a level in hand");
    check(realm.spend(0, 0, 0, 0, 5), "he spends 5 on Command");
    checkEqual(realm.hero().points.command, 30, "his Command is 30");
    checkEqual(realm.hero().pointsInHand, inHand - 5, "five points fewer in hand");
    check(realm.previewSpend(4, 10).totalsAfter.command == 40, "the preview counts a Command press");
    sim::Realm knight;
    check(knight.raise(&lorencia, 0, 130, 130, sim::Kin::DarkKnight, 10), "a level-10 knight raises");
    check(!knight.spend(0, 0, 0, 0, 1), "a knight may not spend on Command");
    checkEqual(knight.hero().points.command, 0, "and has none");

    // Through a character's bytes (kKeptLayout 19), and a layout-18 record reads none.
    sim::Kept kept;
    kept.hero = realm.record();
    std::vector<uint8_t> bytes;
    net::putKept(bytes, kept);
    sim::Kept back;
    check(net::keptFrom(bytes, back), "his bytes read back");
    checkEqual(back.hero.points.command, 30, "his Command read back");
    checkEqual(int(back.hero.kin), int(sim::Kin::DarkLord), "and his class");
    // Layout 19 also carries his skills' cools from the 44th, Force's: found by the first byte a
    // mark on it moves, eight bytes each.
    sim::Kept forced = kept;
    forced.hero.coolsLeft[43] = 0x7F;
    std::vector<uint8_t> forcedBytes;
    net::putKept(forcedBytes, forced);
    size_t cool = 0;
    while (cool < bytes.size() && bytes[cool] == forcedBytes[cool]) ++cool;
    std::vector<uint8_t> older(bytes.begin(), bytes.end() - 4);
    older.erase(older.begin() + std::ptrdiff_t(cool),
                older.begin() + std::ptrdiff_t(cool + 8 * size_t(sim::kSkills - 43)));
    sim::Kept old;
    check(net::keptFrom(older, old, 18), "a layout-18 record reads");
    checkEqual(old.hero.points.command, 0, "with no Command in it");

    // A reborn character opens every class to his account (the user, 2026-10-23: "allow reborn
    // account to make any char"): his rebirths ride the Roster's Seat (protocol 49).
    net::Roster roster;
    net::Seat seat;
    seat.name = "Reborn";
    seat.level = 3;
    seat.rebirths = 2;
    seat.kin = uint8_t(sim::Kin::DarkKnight);
    roster.seats.push_back(seat);
    std::vector<uint8_t> said;
    net::put(said, roster);
    std::vector<uint8_t> body;
    net::Kind kind{};
    net::Roster heard;
    check(net::take(said, kind, body) == 1 && kind == net::Kind::Roster && net::parse(body, heard) &&
              heard.seats.size() == 1 && heard.seats[0].rebirths == 2 && heard.seats[0].level == 3,
          "a seat's rebirths cross the wire");

    // A points reset gives his Command back to 25 and the rest to his hand.
    check(realm.resetPoints() == sim::Realm::Reset::Done, "his points reset");
    checkEqual(realm.hero().points.command, 25, "Command back to 25");
    checkEqual(realm.hero().pointsInHand, inHand, "every point in hand again");

    // **Force** (60): born knowing it, MU's multiplier, and thrown it lands on a monster.
    {
        sim::Realm lordRealm;
        check(lordRealm.raise(&lorencia, 7, 130, 130, sim::Kin::DarkLord, 40), "a level-40 Dark Lord raises");
        check(lordRealm.knows(sim::skill::kForce), "he is born knowing Force");
        check(!knight.knows(sim::skill::kForce), "a knight is not");
        const sim::SkillRow* force = sim::skillNumbered(sim::skill::kForce);
        check(force != nullptr && force->primary() && force->kin == sim::Kin::DarkLord && force->mana == 10 &&
                  force->reach == 4.0f && force->suits(sim::arms::kNone),
              "Force: his primary, ten mana, four tiles, bare-handed too");
        sim::HeroPoints hundred;
        hundred.energy = 100;
        check(std::fabs(sim::force(*force, hundred, sim::Kin::DarkLord) - 2.05f) < 1e-4f,
              "(200 + Ene/20)%: 100 energy is 205%");
        // Its clip is in the world's tables: without it the cast took no time and the blow went a
        // tick after the press (the user, 2026-10-23: "monsters died faster than i casted").
        const int32_t strike = sim::castTicks(lorencia, sim::Kin::DarkLord, 20, nullptr, nullptr, *force, 0);
        check(strike >= 15, "Force's strike is a clip of over three quarters of a second");
        // Thrown at the nearest monster until one is struck.
        int loosed = 0, hits = 0, damage = 0;
        for (int tick = 0; tick < 3000 && hits < 3; ++tick) {
            uint32_t nearest = 0;
            float best = 1e9f;
            const sim::Body& me = lordRealm.hero();
            for (const sim::Body& one : lordRealm.bodies()) {
                if (!one.monster() || !one.alive()) continue;
                const float d = std::fabs(one.x - me.x) + std::fabs(one.y - me.y);
                if (d < best) best = d, nearest = one.id;
            }
            if (nearest != 0 && lordRealm.hero().blowSkill == 0 && tick % 20 == 0) {
                sim::Request request;
                request.kind = sim::Request::Kind::Attack;
                request.target = nearest;
                request.skill = sim::skill::kForce;
                lordRealm.ask(request);
            }
            lordRealm.step();
            for (const sim::Happening& one : lordRealm.happenings()) {
                if (one.who != lordRealm.hero().id) continue;
                if (one.what == sim::What::Loosed && one.a == sim::skill::kForce) ++loosed;
                if (one.what == sim::What::Hit && one.thrown) ++hits, damage += one.a;
            }
        }
        std::printf("  force: %d loosed, %d hits, %d damage\n", loosed, hits, damage);
        check(loosed > 0 && hits > 0 && damage > 0, "Force is thrown and lands");
    }

    // **Force Wave** (66, docs/plans/dark-lord-spells/force-wave.md): Force from a scepter strikes
    // every monster WebZen's spear table marks down his aim, the aimed body always, all on the
    // let-go. A short sword marked with the scepters' skill stands in for one until they are cooked.
    {
        content::Tables waved = lorencia;
        const int32_t stand = waved.itemNamed("Sword02");
        check(stand >= 0, "a sword the lord may hold");
        waved.items[size_t(stand)].skill = sim::skill::kForceWave;
        // Due east the table marks one to six tiles along his row and one to three on the next:
        // two on it, one off it, one behind him.
        const int offsets[5][2] = {{2, 0}, {5, 0}, {3, 1}, {6, 2}, {-3, 0}};
        const auto throwOnce = [&](bool scepter, int64_t* struck, int64_t* letGo) {
            sim::Realm lordRealm;
            check(lordRealm.raise(&waved, 9, 160, 60, sim::Kin::DarkLord, 100), "a level-100 Dark Lord raises");
            if (scepter) check(lordRealm.equip(waved.armNamed("Sword02"), -1, true), "the scepter in his hand");
            lordRealm.unseen(true);
            lordRealm.benchUndying(true);
            const int hx = lordRealm.hero().column(), hy = lordRealm.hero().row();
            uint32_t ids[5] = {};
            int found = 0;
            for (const sim::Body& one : lordRealm.bodies()) {
                if (found == 5) break;
                if (one.monster() && one.alive()) ids[found++] = one.id;
            }
            check(found == 5, "five monsters to strike");
            const auto hold = [&] {
                for (int i = 0; i < 5; ++i) {
                    auto* held = const_cast<sim::Body*>(lordRealm.find(ids[i]));
                    held->x = float(hx + offsets[i][0]);
                    held->y = float(hy + offsets[i][1]);
                }
            };
            sim::Request request;
            request.kind = sim::Request::Kind::Attack;
            request.target = ids[0];
            request.skill = sim::skill::kForce;
            for (int i = 0; i < 5; ++i) struck[i] = -1;
            *letGo = -1;
            for (int tick = 0; tick < 60; ++tick) {
                hold();
                if (tick == 0) lordRealm.ask(request);
                lordRealm.step();
                for (const sim::Happening& one : lordRealm.happenings()) {
                    if (one.who != lordRealm.hero().id) continue;
                    if (one.what == sim::What::Loosed && one.a == sim::skill::kForce && *letGo < 0) *letGo = tick;
                    if (one.what != sim::What::Hit && one.what != sim::What::Missed) continue;
                    for (int i = 0; i < 5; ++i) {
                        if (one.whom == ids[i] && struck[i] < 0) struck[i] = tick;
                    }
                }
                if (lordRealm.hero().blowSkill == 0 && *letGo >= 0 && tick > *letGo + 2) break;
            }
        };
        int64_t struck[5], letGo = -1;
        throwOnce(true, struck, &letGo);
        std::printf("  force wave: let go %lld, struck %lld %lld %lld %lld %lld\n", (long long)letGo,
                    (long long)struck[0], (long long)struck[1], (long long)struck[2], (long long)struck[3],
                    (long long)struck[4]);
        check(struck[0] >= 0 && struck[0] == struck[1] && struck[0] == struck[2],
              "the aimed body and the two on the strip, all on one tick");
        check(struck[3] < 0 && struck[4] < 0, "off the strip and behind him, nothing");
        throwOnce(false, struck, &letGo);
        check(struck[0] >= 0 && struck[1] < 0 && struck[2] < 0, "without a scepter Force strikes the aimed body alone");
    }

    // **Fire Burst** (61, docs/plans/dark-lord-spells/fire-burst.md): MU's row, a cooldown; the
    // aimed body struck when the fire gets there, and everything within three whole tiles of it
    // 10-15 ticks on, unsaid.
    {
        const sim::SkillRow* burst = sim::skillNumbered(sim::skill::kFireBurst);
        check(burst != nullptr && !burst->primary() && burst->kin == sim::Kin::DarkLord && burst->mana == 25 &&
                  burst->reach == 6.0f && burst->damage == 100 && burst->needLevel == 74 && burst->coolTicks == 100 &&
                  burst->clip == 80 && burst->suits(sim::arms::kNone),
              "Fire Burst: level 74, 25 mana, six tiles, a hundred damage, five seconds' cooldown");
        sim::Realm lordRealm;
        check(lordRealm.raise(&lorencia, 9, 160, 60, sim::Kin::DarkLord, 80), "a level-80 Dark Lord raises in the north field");
        check(!lordRealm.knows(sim::skill::kFireBurst), "he is not born knowing Fire Burst");
        check(lordRealm.learn(sim::skill::kFireBurst), "he learns it");
        lordRealm.unseen(true);
        lordRealm.benchUndying(true);
        // Four monsters stood off him: the aimed one four tiles east, one (2,2) and one (0,2) off it
        // (whole tiles under three), and one (3,0) off it, which is three and out.
        const sim::Body& me = lordRealm.hero();
        const int hx = me.column(), hy = me.row();
        const int offsets[4][2] = {{4, 0}, {6, 2}, {4, 2}, {7, 0}};
        uint32_t ids[4] = {};
        int found = 0;
        for (const sim::Body& one : lordRealm.bodies()) {
            if (found == 4) break;
            if (!one.monster() || !one.alive()) continue;
            auto& moved = const_cast<sim::Body&>(one);
            moved.x = float(hx + offsets[found][0]);
            moved.y = float(hy + offsets[found][1]);
            ids[found++] = one.id;
        }
        check(found == 4, "four monsters to burn");
        lordRealm.invoke(sim::skill::kFireBurst, ids[0]);
        int64_t letGo = -1;
        int64_t struck[4] = {-1, -1, -1, -1};
        int loosed = 0, splashed = 0;
        int64_t cooledAt = 0;
        for (int tick = 0; tick < 120; ++tick) {
            // Held where they were stood until the let-go: a monster that does not see him still
            // wanders, and the rule is measured at the let-go.
            for (int i = 0; i < 4 && letGo < 0; ++i) {
                auto* held = const_cast<sim::Body*>(lordRealm.find(ids[i]));
                held->x = float(hx + offsets[i][0]);
                held->y = float(hy + offsets[i][1]);
            }
            lordRealm.step();
            for (const sim::Happening& one : lordRealm.happenings()) {
                if (one.who != lordRealm.hero().id) continue;
                if (one.what == sim::What::Loosed && one.a == sim::skill::kFireBurst && one.c == 1) {
                    ++splashed;
                } else if (one.what == sim::What::Loosed && one.a == sim::skill::kFireBurst) {
                    ++loosed, letGo = tick;
                    cooledAt = lordRealm.cooling(sim::skill::kFireBurst);
                }
                if (one.what != sim::What::Hit && one.what != sim::What::Missed) continue;
                for (int i = 0; i < 4; ++i) {
                    if (one.whom == ids[i] && struck[i] < 0) struck[i] = tick;
                }
            }
        }
        std::printf("  fire burst: let go %lld, struck %lld %lld %lld %lld\n", (long long)letGo,
                    (long long)struck[0], (long long)struck[1], (long long)struck[2], (long long)struck[3]);
        checkEqual(loosed, 1, "one fire is said, at the aimed body");
        checkEqual(splashed, 2, "and each body its splash strikes, so its fire is drawn as it lands");
        check(letGo >= 0 && struck[0] > letGo && struck[0] - letGo <= 7,
              "the aimed body burns when the fire reaches it, four tiles in a third of a second");
        check(struck[1] - letGo >= 10 && struck[1] - letGo <= 15 && struck[2] - letGo >= 10 &&
                  struck[2] - letGo <= 15,
              "the two within three tiles of it burn 10-15 ticks after the let-go");
        check(struck[3] < 0, "the one three tiles off it does not");
        check(cooledAt > 0, "and it is cooling as it is let go");
    }

    // **Electric Spike** (65, docs/plans/dark-lord-spells/electric-spike.md): MU's row; WebZen's
    // table decides who is struck, each when the bolt gets there; his party near him adds.
    {
        const sim::SkillRow* spike = sim::skillNumbered(sim::skill::kElectricSpike);
        check(spike != nullptr && !spike->primary() && spike->kin == sim::Kin::DarkLord && spike->reach == 10.0f &&
                  spike->damage == 250 && spike->needLevel == 92 && spike->coolTicks == 100 && spike->clip == 152 &&
                  spike->aimsAtPointer(),
              "Electric Spike: level 92, ten tiles, 250 damage, five seconds, thrown at the ground");
        sim::Realm lordRealm;
        check(lordRealm.raise(&lorencia, 9, 160, 60, sim::Kin::DarkLord, 100), "a level-100 Dark Lord raises");
        check(lordRealm.learn(sim::skill::kElectricSpike), "he learns it");
        lordRealm.unseen(true);
        lordRealm.benchUndying(true);
        const sim::Body& me = lordRealm.hero();
        const int hx = me.column(), hy = me.row();
        checkEqual(lordRealm.partyNear(me), 0, "outside a party, no party bonus");
        check(lordRealm.setParty(me.id, 7), "he joins a party");
        checkEqual(lordRealm.partyNear(me), 1, "and counts himself in it, as WebZen does");
        check(lordRealm.setParty(me.id, 0), "and leaves it");
        // Thrown due east: three and nine tiles along it are in WebZen's table, three tiles off
        // the line and three tiles behind him are not.
        const int offsets[4][2] = {{3, 0}, {9, 0}, {3, 3}, {-3, 0}};
        uint32_t ids[4] = {};
        int found = 0;
        for (const sim::Body& one : lordRealm.bodies()) {
            if (found == 4) break;
            if (!one.monster() || !one.alive()) continue;
            ids[found++] = one.id;
        }
        check(found == 4, "four monsters to strike");
        const auto hold = [&] {
            for (int i = 0; i < 4; ++i) {
                auto* held = const_cast<sim::Body*>(lordRealm.find(ids[i]));
                held->x = float(hx + offsets[i][0]);
                held->y = float(hy + offsets[i][1]);
            }
        };
        hold();
        lordRealm.invokeAt(sim::skill::kElectricSpike, hx + 6, hy);
        int64_t letGo = -1;
        int64_t struck[4] = {-1, -1, -1, -1};
        int loosed = 0;
        for (int tick = 0; tick < 80; ++tick) {
            if (letGo < 0) hold();
            lordRealm.step();
            for (const sim::Happening& one : lordRealm.happenings()) {
                if (one.who != lordRealm.hero().id) continue;
                if (one.what == sim::What::Loosed && one.a == sim::skill::kElectricSpike) ++loosed, letGo = tick;
                if (one.what != sim::What::Hit && one.what != sim::What::Missed) continue;
                for (int i = 0; i < 4; ++i) {
                    if (one.whom == ids[i] && struck[i] < 0) struck[i] = tick;
                }
            }
        }
        std::printf("  electric spike: let go %lld, struck %lld %lld %lld %lld\n", (long long)letGo,
                    (long long)struck[0], (long long)struck[1], (long long)struck[2], (long long)struck[3]);
        checkEqual(loosed, 1, "one bolt is said");
        checkEqual(int(struck[0] - letGo), 4,
                   "the body three tiles down the line is struck as MU's tip passes it, frame 5");
        check(struck[1] > struck[0] && struck[1] - letGo <= 5, "and the one at nine a little later");
        check(struck[2] < 0 && struck[3] < 0, "off the table, beside it and behind him, nothing");
    }
    // Beside him and behind him, where WebZen's table marks a body the bolt never passes: the one
    // beside him is struck and said, so its zap is drawn; behind him nothing ("only frontal").
    {
        sim::Realm lordRealm;
        check(lordRealm.raise(&lorencia, 9, 160, 60, sim::Kin::DarkLord, 100), "a level-100 Dark Lord raises");
        check(lordRealm.learn(sim::skill::kElectricSpike), "he learns it");
        lordRealm.unseen(true);
        lordRealm.benchUndying(true);
        const int hx = lordRealm.hero().column(), hy = lordRealm.hero().row();
        const int offsets[2][2] = {{0, 1}, {-1, 0}};
        uint32_t ids[2] = {};
        int found = 0;
        for (const sim::Body& one : lordRealm.bodies()) {
            if (found == 2) break;
            if (!one.monster() || !one.alive()) continue;
            ids[found++] = one.id;
        }
        check(found == 2, "two monsters to strike");
        const auto hold = [&] {
            for (int i = 0; i < 2; ++i) {
                auto* held = const_cast<sim::Body*>(lordRealm.find(ids[i]));
                held->x = float(hx + offsets[i][0]);
                held->y = float(hy + offsets[i][1]);
            }
        };
        hold();
        lordRealm.invokeAt(sim::skill::kElectricSpike, hx + 6, hy);
        int64_t letGo = -1;
        int64_t struck[2] = {-1, -1};
        int said[2] = {0, 0};
        for (int tick = 0; tick < 80; ++tick) {
            if (letGo < 0) hold();
            lordRealm.step();
            for (const sim::Happening& one : lordRealm.happenings()) {
                if (one.who != lordRealm.hero().id) continue;
                const bool bolt = one.what == sim::What::Loosed && one.a == sim::skill::kElectricSpike;
                if (bolt && one.b == 0) letGo = tick;
                for (int i = 0; i < 2; ++i) {
                    if (bolt && one.b > 0 && one.whom == ids[i]) ++said[i];
                    if ((one.what == sim::What::Hit || one.what == sim::What::Missed) && one.whom == ids[i] &&
                        struck[i] < 0) {
                        struck[i] = tick;
                    }
                }
            }
        }
        std::printf("  electric spike beside/behind: let go %lld, struck %lld %lld, said %d %d\n",
                    (long long)letGo, (long long)struck[0], (long long)struck[1], said[0], said[1]);
        check(struck[0] > letGo && said[0] == 1, "the body beside him is struck, its zap said");
        check(struck[1] < 0 && said[1] == 0, "the one behind him is not");
    }

    // **Critical Damage** (64, docs/plans/dark-lord-spells/critical-damage.md): Cmd/25 + Ene/30 on
    // every critical and excellent hit, for 60 + Ene/10 s, on him and his party within ten tiles.
    {
        const sim::SkillRow* keen = sim::skillNumbered(sim::skill::kCriticalDamage);
        check(keen != nullptr && keen->onSelf() && keen->kin == sim::Kin::DarkLord && keen->mana == 50 &&
                  keen->needLevel == 82 && keen->sharpens,
              "Critical Damage: a self-cast of his, level 82, 50 mana");
        sim::HeroPoints points;
        points.command = 300;
        points.energy = 102;
        checkEqual(sim::keenOf(points), 12 + 3, "Cmd/25 + Ene/30: 300 and 102 give 15");
        checkEqual(sim::keenTicksOf(points), (60 + 10) * 20, "for 60 + Ene/10 seconds");

        // The flat add: on each hand's top for a critical swing, before the defence.
        sim::Fighter a, d;
        a.attackRate = 1000.0f;
        a.minimumDamage = 50;
        a.maximumDamage = 100;
        a.criticalChance = 1.0;
        a.criticalAdd = 30;
        d.defenseRate = 0.0f;
        sim::Random dice(7);
        const sim::Blow crit = sim::strike(a, d, dice);
        check(!crit.hit || crit.rolled == 130, "a critical swing rolls its top and the add");
        a.criticalChance = 0.0;
        sim::Random dice2(7);
        const sim::Blow plain = sim::strike(a, d, dice2);
        check(!plain.hit || plain.rolled <= 100, "an ordinary swing takes none of it");

        sim::Realm lordRealm;
        check(lordRealm.raise(&lorencia, 9, 160, 60, sim::Kin::DarkLord, 100), "a level-100 Dark Lord raises");
        check(lordRealm.learn(sim::skill::kCriticalDamage), "he learns it");
        lordRealm.unseen(true);
        const uint32_t lord = lordRealm.hero().id;
        const int hx = lordRealm.hero().column(), hy = lordRealm.hero().row();
        const uint32_t near = lordRealm.join(sim::Kin::DarkKnight, 100, hx + 2, hy);
        const uint32_t far = lordRealm.join(sim::Kin::DarkKnight, 100, hx + 14, hy);
        const uint32_t stranger = lordRealm.join(sim::Kin::DarkKnight, 100, hx, hy + 2);
        check(near && far && stranger, "three knights join");
        for (uint32_t one : {lord, near, far}) check(lordRealm.setParty(one, 5), "into his party");
        lordRealm.invoke(sim::skill::kCriticalDamage, 0);
        int cast = 0;
        for (int tick = 0; tick < 40; ++tick) {
            lordRealm.step();
            for (const sim::Happening& one : lordRealm.happenings()) {
                if (one.what == sim::What::Cast && one.who == lord && one.a == sim::skill::kCriticalDamage) ++cast;
            }
        }
        checkEqual(cast, 1, "it is cast once");
        const sim::Body& me = lordRealm.hero();
        const int32_t want = sim::keenOf(me.totalPoints());
        check(want > 0, "he has Command and energy to give");
        checkEqual(me.keen, want, "on him");
        checkEqual(lordRealm.find(near)->keen, want, "on the knight of his party two tiles off");
        checkEqual(lordRealm.find(near)->stats.criticalAdd, want, "and in the knight's blows");
        checkEqual(lordRealm.find(far)->keen, 0, "not on the one fourteen tiles off");
        checkEqual(lordRealm.find(stranger)->keen, 0, "nor on a stranger beside him");
        check(me.keenUntil - lordRealm.tick() > sim::keenTicksOf(me.totalPoints()) - 40,
              "held for its sixty seconds and more");
    }

    // **Summon** (63, docs/plans/dark-lord-spells/summon.md): his party on this map set down within
    // four tiles of where he stood, seven seconds on, unless they walk first.
    {
        const sim::SkillRow* row = sim::skillNumbered(sim::skill::kSummon);
        check(row != nullptr && row->onSelf() && !row->aura() && row->kin == sim::Kin::DarkLord &&
                  row->mana == 70 && row->needLevel == 98 && row->coolTicks == 600 && row->recalls,
              "Summon: a self-cast of his, level 98, 70 mana, thirty seconds");
        // PLAYER_ATTACK_TELEPORT in the world's tables: 8 keys at 0.28, over a second.
        const int32_t clip = sim::castTicks(lorencia, sim::Kin::DarkLord, 20, nullptr, nullptr, *row, 0);
        check(clip >= 20, "Summon's clip is in the tables, a second and more");

        // Alone, he has nobody to call: refused, nothing paid (ours; WebZen takes the mana).
        sim::Realm alone;
        check(alone.raise(&lorencia, 9, 160, 60, sim::Kin::DarkLord, 100), "a lone lord raises");
        check(alone.learn(sim::skill::kSummon), "he learns it");
        const int32_t full = alone.hero().mana;
        alone.invoke(sim::skill::kSummon, 0);
        for (int tick = 0; tick < 20; ++tick) alone.step();
        check(alone.hero().mana >= full, "no party: refused, nothing paid");

        sim::Realm realm;
        check(realm.raise(&lorencia, 9, 160, 60, sim::Kin::DarkLord, 100), "a level-100 Dark Lord raises");
        check(realm.learn(sim::skill::kSummon), "he learns it");
        realm.unseen(true);
        const uint32_t lord = realm.hero().id;
        const int hx = realm.hero().column(), hy = realm.hero().row();
        const uint32_t comes = realm.join(sim::Kin::DarkKnight, 100, hx + 14, hy);
        const uint32_t walks = realm.join(sim::Kin::DarkKnight, 100, hx, hy + 14);
        const uint32_t stranger = realm.join(sim::Kin::DarkKnight, 100, hx + 14, hy + 2);
        check(comes && walks && stranger, "three knights join");
        for (uint32_t one : {lord, comes, walks}) check(realm.setParty(one, 7), "into his party");
        const int sx = realm.find(stranger)->column(), sy = realm.find(stranger)->row();
        realm.invoke(sim::skill::kSummon, 0);
        int cast = 0, counts = 0, landed = 0, cancelled = 0, called = -1, paid = 0;
        for (int tick = 0; tick < 7 * 20 + 20; ++tick) {
            if (tick == 10) {
                realm.command({.kind = sim::Command::Kind::Order, .player = walks,
                               .a = int(sim::Request::Kind::WalkTo), .b = hx, .c = hy + 20});
            }
            const int32_t before = realm.hero().mana;
            realm.step();
            for (const sim::Happening& one : realm.happenings()) {
                if (one.what == sim::What::Cast && one.who == lord && one.a == sim::skill::kSummon) {
                    ++cast;
                    paid = before - realm.hero().mana;
                }
                if (one.what != sim::What::Recall) continue;
                const auto event = sim::RecallEvent(one.a);
                if (event == sim::RecallEvent::Called && one.who == lord) called = one.b;
                if (event == sim::RecallEvent::Count && one.who == comes) ++counts;
                if (event == sim::RecallEvent::Landed && one.who == comes) ++landed;
                if (event == sim::RecallEvent::Cancelled && one.who == walks) ++cancelled;
            }
        }
        checkEqual(cast, 1, "it is cast once");
        checkEqual(called, 2, "on the two of his party: \"Summoning 2 people\"");
        check(paid > 60 && paid <= 70, "for its 70 mana");
        check(realm.cooling(sim::skill::kSummon) > 0, "and it cools");
        checkEqual(counts, 5, "the one called hears 5, 4, 3, 2, 1");
        checkEqual(landed, 1, "and is set down once");
        const sim::Body& came = *realm.find(comes);
        const int gap = std::max(std::abs(came.column() - hx), std::abs(came.row() - hy));
        check(gap >= 1 && gap <= sim::kRecallReach, "within four tiles of the lord, never on him");
        checkEqual(cancelled, 1, "the one who walked off is not summoned");
        check(std::abs(realm.find(walks)->row() - hy) > sim::kRecallReach, "and is where he walked");
        check(realm.find(stranger)->column() == sx && realm.find(stranger)->row() == sy,
              "a stranger is never called");
    }

    // **In town**, as MU casts it: the one called lands on the nearest free tile round him.
    {
        sim::Realm realm;
        check(realm.raise(&lorencia, 9, 130, 130, sim::Kin::DarkLord, 100), "a lord raises in Lorencia");
        check(realm.learn(sim::skill::kSummon), "he learns it");
        realm.unseen(true);
        const uint32_t lord = realm.hero().id;
        const int hx = realm.hero().column(), hy = realm.hero().row();
        check(lorencia.grid.safe(hx, hy), "on a safe tile");
        const uint32_t comes = realm.join(sim::Kin::DarkKnight, 100, hx + 10, hy);
        for (uint32_t one : {lord, comes}) check(realm.setParty(one, 8), "into his party");
        realm.invoke(sim::skill::kSummon, 0);
        for (int tick = 0; tick < 7 * 20 + 20; ++tick) realm.step();
        const sim::Body& came = *realm.find(comes);
        checkEqual(std::max(std::abs(came.column() - hx), std::abs(came.row() - hy)), 1,
                   "cast in town, he lands beside the lord");
    }

    // **Fire Scream** (78, docs/plans/dark-lord-spells/fire-scream.md): three lanes of fire down his
    // facing, each body struck as a head passes it; 3% of its blows burst on the 3 x 3 round them.
    {
        const sim::SkillRow* scream = sim::skillNumbered(sim::skill::kFireScream);
        check(scream != nullptr && !scream->primary() && scream->kin == sim::Kin::DarkLord && scream->mana == 45 &&
                  scream->damage == 130 && scream->needLevel == 102 && scream->clip == 80 && scream->aimsAtPointer(),
              "Fire Scream: level 102, 130 damage, 45 mana, the strike, thrown at the ground");
        check(sim::skillNumbered(sim::skill::kExplosion) == nullptr, "Explosion has no row");
        sim::Realm lordRealm;
        check(lordRealm.raise(&lorencia, 9, 160, 60, sim::Kin::DarkLord, 100), "a level-100 Dark Lord raises");
        check(lordRealm.learn(sim::skill::kFireScream), "he learns it");
        lordRealm.unseen(true);
        lordRealm.benchUndying(true);
        const sim::Body& me = lordRealm.hero();
        const int hx = me.column(), hy = me.row();
        // Thrown due east: three and seven tiles down the middle lane and two tiles off it at three
        // (the side lane's) are struck; three off it, behind him, and nine out are not.
        const int offsets[6][2] = {{3, 0}, {7, 0}, {3, 2}, {3, 4}, {-3, 0}, {10, 0}};
        uint32_t ids[6] = {};
        int found = 0;
        for (const sim::Body& one : lordRealm.bodies()) {
            if (found == 6) break;
            if (!one.monster() || !one.alive()) continue;
            ids[found++] = one.id;
        }
        check(found == 6, "six monsters to strike");
        const auto hold = [&] {
            for (int i = 0; i < 6; ++i) {
                auto* held = const_cast<sim::Body*>(lordRealm.find(ids[i]));
                held->x = float(hx + offsets[i][0]);
                held->y = float(hy + offsets[i][1]);
                held->walking = false;
            }
        };
        hold();
        lordRealm.invokeAt(sim::skill::kFireScream, hx + 6, hy);
        int64_t letGo = -1;
        int64_t struck[6] = {-1, -1, -1, -1, -1, -1};
        int loosed = 0;
        for (int tick = 0; tick < 80; ++tick) {
            hold();
            lordRealm.step();
            for (const sim::Happening& one : lordRealm.happenings()) {
                if (one.who != lordRealm.hero().id) continue;
                if (one.what == sim::What::Loosed && one.a == sim::skill::kFireScream) ++loosed, letGo = tick;
                if (one.what != sim::What::Hit && one.what != sim::What::Missed) continue;
                for (int i = 0; i < 6; ++i) {
                    if (one.whom == ids[i] && struck[i] < 0) struck[i] = tick;
                }
            }
        }
        std::printf("  fire scream: let go %lld, struck %lld %lld %lld %lld %lld %lld\n", (long long)letGo,
                    (long long)struck[0], (long long)struck[1], (long long)struck[2], (long long)struck[3],
                    (long long)struck[4], (long long)struck[5]);
        checkEqual(loosed, 1, "the three lanes are said once");
        checkEqual(int(struck[0] - letGo), 3, "three tiles down the middle: struck as the head passes, MU's frame 4");
        check(struck[1] - letGo >= 12 && struck[1] - letGo <= 14, "seven tiles: as late as frame 16");
        check(struck[2] >= 0, "two tiles off the middle at three: the side lane's");
        check(struck[3] < 0 && struck[4] < 0 && struck[5] < 0, "four off, behind him and ten out: nothing");

        // Explosion: over many casts, about 3% of the blows burst, a tenth of the blow each.
        int blows = 0, bursts = 0, tenths = 0;
        for (int cast = 0; cast < 300; ++cast) {
            auto* hero = const_cast<sim::Body*>(&lordRealm.hero());
            hero->mana = hero->maxMana;
            for (int64_t& cool : hero->cools) cool = 0;
            lordRealm.invokeAt(sim::skill::kFireScream, hx + 6, hy);
            for (int tick = 0; tick < 30; ++tick) {
                hold();
                lordRealm.step();
                bool bursting = false;
                for (const sim::Happening& one : lordRealm.happenings()) {
                    if (one.who != lordRealm.hero().id) continue;
                    if (one.what == sim::What::Loosed && one.a == sim::skill::kExplosion) {
                        ++bursts;
                        bursting = true;
                        continue;
                    }
                    if (one.what != sim::What::Hit) continue;
                    if (bursting) ++tenths;
                    else ++blows;
                }
            }
        }
        std::printf("  fire scream: %d blows, %d bursts, %d burst hits\n", blows, bursts, tenths);
        check(bursts > 0 && bursts * 100 < blows * 8, "about three blows in a hundred burst");
        check(tenths >= bursts, "each burst strikes the body it burst off at least");
        check(sim::kExplosionRate == 301 && sim::kExplosionReach == 1 && sim::kExplosionTicks == 2,
              "WebZen's 301 in 10,000, one tile round, 100 ms on");
    }

    // **Earthshake** (62, docs/plans/dark-lord-spells/earthshake.md): the Dark Horse's, known while it
    // is worn with life; every monster within whole-tile distance under five, behind him too, struck
    // on the tick the first spike ring rises and pushed two tiles away; ten seconds' cooldown.
    {
        const sim::SkillRow* quake = sim::skillNumbered(sim::skill::kEarthshake);
        check(quake != nullptr && quake->mounted && quake->horn == 4 && quake->kin == sim::Kin::DarkLord &&
                  quake->mana == 0 && quake->damage == 150 && quake->coolTicks == 200 && quake->clip == 87 &&
                  quake->castsBare() && !quake->primary(),
              "Earthshake: the Dark Horse's, no mana, 150 damage, ten seconds, on 87, cast on air");
        // And WebZen's (200 + Ene/20)% on it, with Force, Fire Burst and Fire Scream (ObjAttack.cpp:1452-1463).
        {
            sim::HeroPoints thousand;
            thousand.energy = 1000;
            check(quake != nullptr && std::fabs(sim::force(*quake, thousand, sim::Kin::DarkLord) - 2.5f) < 1e-4f,
                  "Earthshake's (200 + Ene/20)%: 1000 energy is 250%");
        }
        const int32_t clip = sim::castTicks(lorencia, sim::Kin::DarkLord, 0, nullptr, nullptr, *quake, 0);
        check(clip >= 25 && clip <= 32, "PLAYER_ATTACK_DARKHORSE is in the tables, 11 keys at 0.3");
        const int horn = lorencia.itemAt(13, 4);
        check(horn >= 0, "the Dark Horse is in Lorencia's tables");

        sim::Realm lordRealm;
        check(lordRealm.raise(&lorencia, 9, 160, 60, sim::Kin::DarkLord, 250), "a level-250 Dark Lord raises");
        check(lordRealm.spend(lordRealm.hero().pointsInHand / 2, 0, 0, 0, lordRealm.hero().pointsInHand / 4),
              "into strength and Command");
        lordRealm.unseen(true);
        lordRealm.benchUndying(true);
        const auto loosedIn = [&](int ticks) {
            int loosed = 0;
            for (int tick = 0; tick < ticks; ++tick) {
                lordRealm.step();
                for (const sim::Happening& one : lordRealm.happenings()) {
                    if (one.who == lordRealm.hero().id && one.what == sim::What::Loosed &&
                        one.a == sim::skill::kEarthshake) {
                        ++loosed;
                    }
                }
            }
            return loosed;
        };
        const int hx = lordRealm.hero().column(), hy = lordRealm.hero().row();
        check(!lordRealm.knows(sim::skill::kEarthshake), "on foot he does not know it");
        lordRealm.invokeAt(sim::skill::kEarthshake, hx + 6, hy);
        checkEqual(loosedIn(40), 0, "and cannot cast it");
        check(lordRealm.give(horn, sim::kMount) == sim::kMount, "he wears the horse");
        lordRealm.step();
        check(lordRealm.hero().riding && lordRealm.knows(sim::skill::kEarthshake), "and rides it, and knows it");
        checkEqual(lordRealm.record().learned & (uint64_t(1) << sim::skillIndexOf(sim::skill::kEarthshake)),
                   uint64_t(0), "none of it in the save");
        auto& horse = const_cast<sim::Held&>(lordRealm.satchel()[sim::kMount]);
        const int life = horse.durability;
        horse.durability = 0;
        check(!lordRealm.knows(sim::skill::kEarthshake), "at no horse life he does not know it");
        lordRealm.invokeAt(sim::skill::kEarthshake, hx + 6, hy);
        checkEqual(loosedIn(40), 0, "and cannot cast it");
        horse.durability = life;
        lordRealm.step();

        // Cast east: ahead, behind, to the side, (3, 3) whose 18 is under 25 -- all struck; (4, 3)
        // and (5, 0), distance five, are not.
        const int offsets[6][2] = {{3, 0}, {-3, 0}, {0, -4}, {3, 3}, {4, 3}, {5, 0}};
        uint32_t ids[6] = {};
        int found = 0;
        for (const sim::Body& one : lordRealm.bodies()) {
            if (found == 6) break;
            if (one.monster() && one.alive()) ids[found++] = one.id;
        }
        check(found == 6, "six monsters to shake");
        const auto hold = [&] {
            for (int i = 0; i < 6; ++i) {
                auto* held = const_cast<sim::Body*>(lordRealm.find(ids[i]));
                held->x = float(hx + offsets[i][0]);
                held->y = float(hy + offsets[i][1]);
                held->walking = false;
            }
        };
        const sim::Body& me = lordRealm.hero();
        const sim::HeroPoints p = me.totalPoints();
        const int flat = p.strength / 10 + p.command / 5 + 10;
        const int low = me.stats.minimumDamage + 150 + flat, high = me.stats.maximumDamage + 225 + flat;
        hold();
        lordRealm.invokeAt(sim::skill::kEarthshake, hx + 6, hy);
        int64_t letGo = -1, wait = -1;
        int64_t struck[6] = {-1, -1, -1, -1, -1, -1};
        int shoved[6][2] = {};
        bool pushed[6] = {};
        int loosed = 0, banded = 0, hits = 0;
        for (int tick = 0; tick < 60; ++tick) {
            if (letGo < 0 || tick < letGo + wait) hold();
            lordRealm.step();
            for (const sim::Happening& one : lordRealm.happenings()) {
                if (one.what == sim::What::Shoved) {
                    for (int i = 0; i < 6; ++i) {
                        if (one.who != ids[i]) continue;
                        pushed[i] = true;
                        shoved[i][0] = one.a - hx;
                        shoved[i][1] = one.b - hy;
                    }
                }
                if (one.who != lordRealm.hero().id) continue;
                if (one.what == sim::What::Loosed && one.a == sim::skill::kEarthshake) {
                    ++loosed, letGo = tick, wait = one.b;
                }
                if (one.what != sim::What::Hit && one.what != sim::What::Missed) continue;
                if (one.what == sim::What::Hit && !one.critical && !one.excellent) {
                    ++hits;
                    banded += one.b >= low && one.b <= high;
                }
                for (int i = 0; i < 6; ++i) {
                    if (one.whom == ids[i] && struck[i] < 0) struck[i] = tick;
                }
            }
        }
        std::printf("  earthshake: let go %lld, wait %lld, struck %lld %lld %lld %lld %lld %lld; pushed to "
                    "(%d,%d) (%d,%d) (%d,%d) (%d,%d); %d of %d hits in %d..%d\n",
                    (long long)letGo, (long long)wait, (long long)struck[0], (long long)struck[1],
                    (long long)struck[2], (long long)struck[3], (long long)struck[4], (long long)struck[5],
                    shoved[0][0], shoved[0][1], shoved[1][0], shoved[1][1], shoved[2][0], shoved[2][1],
                    shoved[3][0], shoved[3][1], banded, hits, low, high);
        checkEqual(loosed, 1, "the stomp is said once");
        // Pressed on tick 0; the first spike ring at MU's frame 25, a second after the press.
        check(letGo >= 11 && letGo <= 15, "let go at key 5 of 11, two thirds of a second in");
        check(letGo + wait >= 18 && letGo + wait <= 22, "the spikes rise about twenty ticks after the press");
        for (int i = 0; i < 4; ++i) {
            checkEqual(struck[i], letGo + wait, "within five, all round him, struck as the spikes rise");
        }
        check(struck[4] < 0 && struck[5] < 0, "at whole-tile distance five, nothing");
        check(pushed[0] && shoved[0][0] == 5 && shoved[0][1] == 0, "the one ahead pushed two tiles on");
        check(pushed[1] && shoved[1][0] == -5 && shoved[1][1] == 0, "the one behind two tiles back");
        check(pushed[3] && shoved[3][0] == 5 && shoved[3][1] == 5, "and (3, 3) two tiles out on its diagonal");
        check(!pushed[4] && !pushed[5], "nothing outside pushed");
        check(hits > 0 && banded == hits,
              "150..225 + Str/10 + Command/5 + 10 a horse level on his swing, in place of Str/25 + Ene/50");
        // Cooling: a second press inside its ten seconds does nothing.
        check(lordRealm.cooling(sim::skill::kEarthshake) > 100, "it cools");
        check(lordRealm.coolsFor(sim::skill::kEarthshake) <= 200 && lordRealm.coolsFor(sim::skill::kEarthshake) > clip,
              "for MU's ten seconds, before his agility's haste");
        lordRealm.invokeAt(sim::skill::kEarthshake, hx + 6, hy);
        checkEqual(loosedIn(40), 0, "pressed again while it cools, nothing");
    }
}
