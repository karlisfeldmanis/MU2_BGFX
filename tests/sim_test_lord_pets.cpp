// The Dark Lord's pets by their level (sim/lord_pets.h, docs/plans/dark-lord-pets/README.md): MU's
// exp table, the share of his kills, the bleed at death, the horse's numbers by its level and the
// Trainer's price. Run from tests/sim_test.cpp's main.
#include <cmath>
#include <cstdio>
#include <string>

#include "content/tables.h"
#include "sim/lord_pets.h"
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
}
