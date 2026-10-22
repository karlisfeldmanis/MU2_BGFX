// Rebirth (sim/rebirth.h): Lugard's rite on Devias's own tables, through the realm's command as
// the window sends it, and the count through a character's bytes.

#include <cstdio>
#include <string>

#include "content/tables.h"
#include "net/wire.h"
#include "sim/command.h"
#include "sim/quests.h"
#include "sim/rebirth.h"
#include "sim/realm.h"
#include "sim/talents.h"

using namespace mu;

void testRebirth(void (*check)(bool, const char*), void (*checkEqual)(long long, long long, const char*)) {
    std::printf("rebirth\n");
    content::Tables devias;
    std::string error;
    const std::string path = std::string(MU2_ASSET_DIR) + "/cooked/devias/devias.mur";
    check(content::loadTables(path, devias, error), "Devias's tables load");
    int lugard = -1;
    for (size_t i = 0; i < devias.folk.size(); ++i) {
        if (devias.folk[i].number == sim::kLugard) lugard = int(i);
    }
    check(lugard >= 0, "Lugard is in Devias's table");
    if (lugard < 0) return;
    checkEqual(sim::rebirthZen(1), 10000000, "the first rebirth asks 10,000,000");
    checkEqual(sim::rebirthZen(3), 30000000, "the third 30,000,000");

    using Kind = sim::Command::Kind;
    const auto talkTo = [&](sim::Realm& realm) {
        sim::Request talk;
        talk.kind = sim::Request::Kind::Talk;
        talk.target = uint32_t(lugard);
        realm.ask(talk);
        for (int tick = 0; tick < 400 && realm.rebirthing() < 0; ++tick) realm.step();
    };
    const auto run = [&](sim::Realm& realm) -> int {
        sim::Command c{.kind = Kind::Rebirth};
        c.player = realm.hero().id;
        c.ticket = 77;
        realm.command(c);
        realm.step();
        for (const sim::Happening& one : realm.happenings()) {
            if (one.what == sim::What::Answered && one.c == 77) return one.b;
        }
        return -99;
    };

    // Below the top: his window opens, and the rite is refused.
    sim::Realm low;
    check(low.raise(&devias, 5, 210, 45, sim::Kin::DarkKnight, 399), "a level-399 knight by Lugard");
    low.earn(sim::rebirthZen(1));
    checkEqual(low.rebirth(), -1, "refused away from Lugard");
    talkTo(low);
    checkEqual(low.rebirthing(), lugard, "talking to Lugard opens his window");
    checkEqual(run(low), -1, "level 399 is refused");
    checkEqual(low.hero().level, 399, "and he keeps it");

    // At the top: refused without the Zen, then reborn.
    sim::Realm realm;
    check(realm.raise(&devias, 5, 210, 45, sim::Kin::DarkKnight, sim::kRebirthLevel), "a level-400 knight");
    talkTo(realm);
    checkEqual(run(realm), -1, "refused without the Zen");
    checkEqual(realm.rebirthing(), lugard, "his window stays up on a refusal");
    realm.earn(sim::rebirthZen(1) + 123);
    const sim::HeroPoints points = realm.hero().points;
    const int inHand = realm.hero().pointsInHand;
    const int talentsBefore = sim::talentPoints(sim::Kin::DarkKnight, sim::kRebirthLevel, 0);
    check(realm.rebirthStrips() >= 0, "the bag has room for what he could no longer wear");
    checkEqual(run(realm), 1, "the first rebirth is made");
    bool said = false;
    for (const sim::Happening& one : realm.happenings()) said = said || (one.what == sim::What::Reborn && one.a == 1);
    check(said, "the realm says Reborn, 1");
    checkEqual(realm.hero().level, 1, "his body is level 1");
    checkEqual((long long)realm.hero().experience, 0, "with no experience");
    checkEqual(realm.hero().rebirths, 1, "one rebirth");
    checkEqual(realm.money(), 123, "the Zen paid");
    checkEqual(realm.hero().points.strength, points.strength, "his strength kept");
    checkEqual(realm.hero().points.energy, points.energy, "his energy kept");
    checkEqual(realm.hero().pointsInHand, inHand, "his points in hand kept");
    checkEqual(realm.rebirthing(), -1, "the window shut on it");
    checkEqual(realm.hero().health, realm.hero().maxHealth, "risen whole");
    check(sim::talentPoints(sim::Kin::DarkKnight, 1, 1) > talentsBefore, "a talent point more than at 400");
    checkEqual(realm.talentsInHand(), sim::talentPoints(sim::Kin::DarkKnight, 1, 1), "every talent point his");

    // The count through a character's bytes, and an older layout's without it.
    sim::Kept kept;
    kept.hero = realm.record();
    checkEqual(kept.hero.rebirths, 1, "the record carries it");
    std::vector<uint8_t> bytes;
    net::putKept(bytes, kept);
    sim::Kept back;
    check(net::keptFrom(bytes, back), "the bytes read back");
    checkEqual(back.hero.rebirths, 1, "the count read back");
    sim::Realm again;
    check(again.raise(&devias, 5, 210, 45, sim::Kin::DarkKnight, 1), "a realm raises him again");
    again.restore(back.hero);
    checkEqual(again.hero().rebirths, 1, "restored with it");

    // A quest handed in before the rebirth pays its first clear's experience again, once a life,
    // to one born in its town (sim::questReborn); its gear and runes stay the first clear's.
    const sim::QuestRow& marlon = sim::questAt(sim::questIndexOf("marlon"));
    const int knight = int(sim::Kin::DarkKnight);
    sim::QuestProgress done;
    done.state = sim::QuestState::Resting;
    done.completions = 3;
    check(!sim::questReborn(marlon, knight, done, 0), "never reborn: no bonus");
    check(sim::questReborn(marlon, knight, done, 1), "his first hand-in after the rebirth pays it");
    checkEqual(sim::questExperience(marlon, true), marlon.firstExperience, "the first clear's experience");
    check(!sim::questReborn(marlon, int(sim::Kin::FairyElf), done, 1), "not to a stranger to the town");
    done.life = 1;
    check(!sim::questReborn(marlon, knight, done, 1), "once a life");
    check(sim::questReborn(marlon, knight, done, 2), "and again in the next");
    sim::QuestProgress never;
    check(!sim::questReborn(marlon, knight, never, 1), "a quest never handed in has its true first clear");
    // And the life through a character's bytes.
    sim::Kept lived;
    lived.hero = realm.record();
    lived.hero.quests[0] = done;
    std::vector<uint8_t> livedBytes;
    net::putKept(livedBytes, lived);
    sim::Kept livedBack;
    check(net::keptFrom(livedBytes, livedBack) && livedBack.hero.quests[0].life == 1,
          "a quest's life read back");
}
