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

    // Through a character's bytes (kKeptLayout 15), and a layout-14 record reads none.
    sim::Kept kept;
    kept.hero = realm.record();
    std::vector<uint8_t> bytes;
    net::putKept(bytes, kept);
    sim::Kept back;
    check(net::keptFrom(bytes, back), "his bytes read back");
    checkEqual(back.hero.points.command, 30, "his Command read back");
    checkEqual(int(back.hero.kin), int(sim::Kin::DarkLord), "and his class");
    // Layout 15 also carries Force's cool, the 41st: found by the first byte a mark on it moves.
    sim::Kept forced = kept;
    forced.hero.coolsLeft[sim::kSkills - 1] = 0x7F;
    std::vector<uint8_t> forcedBytes;
    net::putKept(forcedBytes, forced);
    size_t cool = 0;
    while (cool < bytes.size() && bytes[cool] == forcedBytes[cool]) ++cool;
    std::vector<uint8_t> older(bytes.begin(), bytes.end() - 4);
    older.erase(older.begin() + std::ptrdiff_t(cool), older.begin() + std::ptrdiff_t(cool + 8));
    sim::Kept old;
    check(net::keptFrom(older, old, 14), "a layout-14 record reads");
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
}
