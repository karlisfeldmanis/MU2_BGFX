// The Dark Lord's pets by their level (sim/lord_pets.h, docs/plans/dark-lord-pets/README.md): MU's
// exp table, the share of his kills, the bleed at death, the horse's numbers by its level and the
// Trainer's price. Run from tests/sim_test.cpp's main.
#include <cmath>
#include <cstdio>
#include <string>

#include "content/tables.h"
#include "sim/command.h"
#include "sim/lord_pets.h"
#include "sim/machine.h"
#include "sim/realm.h"

using namespace mu;

void testLordPets(void (*check)(bool, const char*), void (*checkEqual)(long long, long long, const char*)) {
    std::printf("the lord's pets\n");
    content::Tables tables;
    std::string error;
    check(content::loadTables(std::string(MU2_ASSET_DIR) + "/cooked/lorencia/lorencia.mur", tables, error),
          "Lorencia's tables load");
    const int horn = tables.itemAt(13, 4);
    check(horn >= 0, "the Dark Horse is in the table");
    if (horn < 0) return;
    const content::ItemRow& row = tables.items[size_t(horn)];

    // zzzitem.h:354-368: (10 + L) x L^3 x 100, nought at 1.
    checkEqual(sim::petExpAt(1), 0, "level 1 starts at nought");
    checkEqual(sim::petExpAt(2), 9600, "level 2 at 9,600");
    checkEqual(sim::petExpAt(10), 2000000, "level 10 at 2,000,000");
    checkEqual(sim::petExpAt(50), 750000000, "level 50 at 750,000,000");
    sim::Held horse;
    horse.item = horn;
    horse.durability = 255;
    checkEqual(sim::petLevel(horse), 1, "a newborn is level 1");
    sim::setPetExp(horse, sim::petExpAt(10));
    checkEqual(sim::petExp(horse), sim::petExpAt(10), "its exp is kept whole in the item");
    checkEqual(sim::petLevel(horse), 10, "and reads as level 10");

    // By its level: asks 218 + 2L, defence 5 + Agi/20 + 2L, cut (30 + L)/2 %.
    checkEqual(sim::asks(row, horse).level, 238, "a level 10 horse asks 238");
    checkEqual(sim::horseDefense(100, 10), 30, "defence 5 + 100/20 + 20");
    check(sim::petPower(row, horse).taken == 0.80, "and takes 80% of a blow");
    checkEqual(sim::petValue(row, horse), 20000000, "worth 2,000,000 a level");

    // The feed: refused whole where he could not wear the level it would reach.
    sim::Held young = horse;
    sim::setPetExp(young, 0);
    checkEqual(sim::feedPet(young, sim::petExpAt(3), [](int) { return true; }), 3, "a feed lifts it to 3");
    sim::Held held = young;
    checkEqual(sim::feedPet(held, sim::petExpAt(5), [](int level) { return level < 4; }), 0,
               "one past what he may wear is refused");
    checkEqual(sim::petExp(held), sim::petExp(young), "and nothing of it kept");
    sim::Held dead = young;
    dead.durability = 0;
    checkEqual(sim::feedPet(dead, 1000, [](int) { return true; }), 0, "a dead one gains nothing");

    // The bleed: a tenth of its level's span, which may take it a level down.
    sim::Held bled = horse;
    checkEqual(sim::bleedPet(bled), 9, "dying at the start of level 10 takes it to 9");
    checkEqual(sim::petExp(bled), sim::petExpAt(10) - (sim::petExpAt(11) - sim::petExpAt(10)) / 10,
               "a tenth of 10's span");

    // The Trainer's price (1.03.35): a level 10 horse at nought, about 1.79 million.
    check(std::abs(sim::mendPrice(20000000, 0) - 1794432) < 1000, "a dead level 10 horse mends for ~1.79M");
    checkEqual(sim::mendPrice(20000000, 255), 0, "a whole one for nothing");

    // In the realm: a fifth of what he gains goes to the horse he rides.
    sim::Realm realm;
    check(realm.raise(&tables, 5, 200, 160, sim::Kin::DarkLord, 250), "a lord raises in the field");
    check(realm.give(horn, sim::kMount) == sim::kMount, "and rides the horse");
    realm.step();
    uint32_t target = 0;
    float nearest = 1e9f;
    for (const sim::Body& one : realm.bodies()) {
        if (!one.monster() || !one.alive()) continue;
        const float d = std::hypot(one.x - realm.hero().x, one.y - realm.hero().y);
        if (d < nearest) nearest = d, target = one.id;
    }
    check(target != 0, "a monster near him");
    sim::Request attack;
    attack.kind = sim::Request::Kind::Attack;
    attack.target = target;
    realm.ask(attack);
    uint64_t share = 0;
    for (int tick = 0; tick < 4000 && share == 0; ++tick) {
        realm.step();
        for (const sim::Happening& h : realm.happenings()) {
            if (h.what == sim::What::Gained && h.who == realm.hero().id) share += uint64_t(h.a) * 20 / 100;
        }
    }
    check(share > 0, "he kills it and gains");
    checkEqual(sim::petExp(realm.satchel()[sim::kMount]), share, "the horse took a fifth of it");

    // ---- the Pet Trainer (226): his box, his mixes, his Restore Life --------------------------------
    const int spiritRow = tables.itemAt(13, sim::kSpiritNumber), ravenRow = tables.itemAt(13, sim::kDarkRavenNumber);
    const int chaos = tables.itemAt(12, 15), bless = tables.itemAt(14, 13), soul = tables.itemAt(14, 14),
              life = tables.itemAt(14, 16);
    check(spiritRow >= 0 && ravenRow >= 0, "the Spirit and the Dark Raven are in the tables");
    if (spiritRow < 0 || ravenRow < 0) return;
    checkEqual(sim::placeOf(tables.items[size_t(ravenRow)]), int(sim::kWeaponLeft), "the raven goes in the left hand");
    check(sim::itemLabel(tables.items[size_t(spiritRow)], sim::Held{spiritRow, 1, 1}) == "Spirit of Dark Raven",
          "a Spirit +1 is the Dark Raven's");
    {
        sim::Machine box;
        box.put(0, sim::Held{spiritRow, 0, 1});
        box.put(1, sim::Held{bless, 0, 5});
        box.put(2, sim::Held{soul, 0, 5});
        box.put(3, sim::Held{chaos, 0, 1});
        box.put(4, sim::Held{life, 0, 1});
        const sim::Judged horse = sim::judge(tables, box, sim::Service::DarkHorse);
        check(horse.ready && horse.rate == 60 && horse.zen == 5000000, "a Spirit +0, 5 Bless, 5 Soul, Chaos, Life: the horse at 60% for 5M");
        check(!sim::judge(tables, box, sim::Service::Combine).ready, "the Goblin's Combine knows none of it");
        check(!sim::judge(tables, box, sim::Service::DarkRaven).ready, "nor is it the raven's");
        box.put(4, sim::Held{});
        check(!sim::judge(tables, box, sim::Service::DarkHorse).ready, "without the Life it is not ready");
    }
    int trainer = -1;
    for (size_t i = 0; i < tables.folk.size(); ++i) {
        if (tables.folk[i].number == sim::kPetTrainer) trainer = int(i);
    }
    check(trainer >= 0, "the Pet Trainer stands in Lorencia");
    if (trainer < 0) return;
    sim::Realm town;
    check(town.raise(&tables, 13, 122, 113, sim::Kin::DarkLord, 250), "a lord raises by the Trainer");
    town.earn(20000000);
    check(!town.mix(sim::Service::DarkRaven), "nothing is bred away from him");
    sim::Request talk;
    talk.kind = sim::Request::Kind::Talk;
    talk.target = uint32_t(trainer);
    town.ask(talk);
    for (int tick = 0; tick < 400 && town.mixing() < 0; ++tick) town.step();
    checkEqual(town.mixing(), trainer, "talking to him opens his box");
    for (const int one : {town.give(spiritRow, -1, 1, 1), town.give(bless, -1, 0, 2), town.give(soul, -1, 0, 2),
                          town.give(chaos, -1, 0, 1), town.give(life, -1, 0, 1)}) {
        town.putIn(one);
    }
    check(town.judged(sim::Service::DarkRaven).ready, "the raven's box is ready at his counter");
    check(!town.mix(sim::Service::Upgrade), "the Goblin's pages are not his");
    const int64_t zen = town.money();
    check(town.mix(sim::Service::DarkRaven), "the raven's Resurrection runs");
    checkEqual(zen - town.money(), 1000000, "for 1,000,000 Zen");
    const sim::Held out = town.machine()[0];
    check(out.empty() || (out.item == ravenRow && out.durability == 255 && sim::petLevel(out) == 1),
          "and gives a whole level-1 raven, or nothing on a failure");
    // Restore Life: the horse he rides at 100 life.
    town.give(horn, sim::kMount, 0, 100);
    const sim::Judged mend = town.judged(sim::Service::MendPets);
    check(mend.ready && mend.zen == sim::mendPrice(2000000, 100), "his hurt horse's price is MU's");
    const int64_t had = town.money();
    check(town.mix(sim::Service::MendPets), "Restore Life runs");
    checkEqual(town.satchel()[sim::kMount].durability, 255, "the horse is whole");
    checkEqual(had - town.money(), mend.zen, "and it cost what it said");

    // ---- the Dark Raven: worn by his Command, striking on its own -------------------------------------
    sim::Realm field;
    check(field.raise(&tables, 5, 200, 160, sim::Kin::DarkLord, 250), "a lord raises in the field");
    const int carried = field.give(ravenRow);
    check(carried >= sim::kWorn && !field.moveItem(carried, sim::kWeaponLeft), "at 25 Command the raven will not go on");
    check(field.spend(0, 0, 0, 0, 300), "he spends 300 on Command");
    check(field.moveItem(carried, sim::kWeaponLeft), "and wears it");
    field.step();
    int dashes = 0, ravenHits = 0;
    const auto watch = [&](int ticks) {
        for (int tick = 0; tick < ticks; ++tick) {
            field.step();
            for (const sim::Happening& h : field.happenings()) {
                if (h.what == sim::What::Ravened && h.who == field.hero().id) ++dashes;
                if (h.what == sim::What::Hit && h.who == field.hero().id && h.thrown) ++ravenHits;
            }
        }
    };
    watch(200);
    checkEqual(dashes, 0, "in Normal it does nothing");
    sim::Command random;
    random.kind = sim::Command::Kind::RavenMode;
    random.a = int(sim::RavenMode::Random);
    field.command(random);
    // Walk him to the nearest monster, so one is within its five tiles.
    uint32_t near = 0;
    float best = 1e9f;
    for (const sim::Body& one : field.bodies()) {
        if (!one.monster() || !one.alive()) continue;
        const float d = std::hypot(one.x - field.hero().x, one.y - field.hero().y);
        if (d < best) best = d, near = one.id;
    }
    sim::Request go;
    go.kind = sim::Request::Kind::Attack;
    go.target = near;
    field.ask(go);
    watch(1200);
    check(dashes > 0, "in Random it strikes");
    check(ravenHits > 0, "and its blows land, his");
}

