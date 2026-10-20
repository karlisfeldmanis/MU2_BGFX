// The Jeweler (sim/jeweler.h): Moss's Recut, Salvage and Combine on Noria's own tables, through
// the realm's commands, as the window sends them.

#include <cstdio>
#include <string>

#include "content/tables.h"
#include "sim/command.h"
#include "sim/jeweler.h"
#include "sim/realm.h"

using namespace mu;

namespace {

void (*check)(bool held, const char* what) = nullptr;
void (*checkEqual)(long long got, long long wanted, const char* what) = nullptr;

int countOf(const sim::Realm& realm, int32_t item) {
    int n = 0;
    for (int s = sim::kWorn; s < sim::kSlots; ++s) {
        const sim::Held& h = realm.satchel()[s];
        if (!h.empty() && h.item == item) n += std::max<int>(1, h.durability);
    }
    return n;
}

}  // namespace

void testJeweler(void (*checkOf)(bool, const char*),
                 void (*checkEqualOf)(long long, long long, const char*)) {
    check = checkOf;
    checkEqual = checkEqualOf;
    std::printf("jeweler\n");
    content::Tables noria;
    std::string error;
    const std::string path = std::string(MU2_ASSET_DIR) + "/cooked/noria/noria.mur";
    check(content::loadTables(path, noria, error), "Noria's tables load");
    int moss = -1;
    for (size_t i = 0; i < noria.folk.size(); ++i) {
        if (noria.folk[i].number == sim::kJeweler) moss = int(i);
    }
    check(moss >= 0, "Moss the Gambler is in Noria's table");
    if (moss < 0) return;
    const int32_t wisdom = noria.itemAt(13, 21), fortune = noria.itemAt(13, 23),
                  chaos = noria.itemAt(12, 15), soul = noria.itemAt(14, 14), ice = noria.itemAt(13, 8);
    check(wisdom >= 0 && fortune >= 0 && chaos >= 0 && soul >= 0 && ice >= 0, "the rings and jewels");
    if (wisdom < 0 || fortune < 0 || chaos < 0 || soul < 0 || ice < 0) return;

    // Prices and rules.
    checkEqual(sim::recutZen(0), 50000, "a recut at +0 is 50,000");
    checkEqual(sim::recutZen(9), 500000, "and at +9 500,000");
    checkEqual(sim::combineZen(1), 500000, "white to blue 500,000");
    checkEqual(sim::combineZen(2), 2000000, "blue to purple 2,000,000");
    checkEqual(sim::combineZen(3), 0, "purple does not combine");
    for (const auto& odds : sim::kSalvageOdds) {
        int sum = 0;
        for (int p : odds) sum += p;
        checkEqual(sum, 100, "each colour's salvage odds sum to 100");
    }

    sim::Realm realm;
    check(realm.raise(&noria, 7, 186, 104, sim::Kin::DarkKnight, 50), "a realm raises by him");
    realm.earn(50000000);
    const uint8_t purple[3] = {uint8_t(sim::Affix::Wisdom), uint8_t(sim::Affix::FireResistance), 0};
    const int ring = realm.give(fortune, -1, 7, -1, true, 0, 0, 0, nullptr, purple, true);
    check(ring >= sim::kWorn, "a bound lucky Ring of Fortune +7, purple, in the bag");
    checkEqual(realm.recut(ring, 1), -1, "refused away from the Jeweler");

    sim::Request talk;
    talk.kind = sim::Request::Kind::Talk;
    talk.target = uint32_t(moss);
    realm.ask(talk);
    for (int tick = 0; tick < 400 && realm.jeweling() < 0; ++tick) realm.step();
    checkEqual(realm.jeweling(), moss, "talking to Moss opens his window");

    // Recut: needs a Jewel of Chaos, whatever the colour.
    realm.give(soul, -1, 0, 2);
    checkEqual(realm.recut(ring, 1), -1, "a recut without a Chaos is refused, a Soul or none");
    realm.give(chaos, -1, 0, 3);
    checkEqual(realm.recut(ring, 2), -1, "an empty line is not recut");
    const int64_t before = realm.money();
    sim::Command recut;
    recut.kind = sim::Command::Kind::JewelRecut;
    recut.a = ring;
    recut.b = 1;
    realm.command(recut);
    realm.step();
    const sim::Held after = realm.satchel()[ring];
    check(after.affixes[1] != 0 && after.affixes[1] != uint8_t(sim::Affix::FireResistance) &&
              after.affixes[1] != uint8_t(sim::Affix::Wisdom) &&
              after.affixes[1] != uint8_t(sim::Affix::Fortune) &&
              after.affixes[1] != uint8_t(sim::Affix::Haste),
          "the line is a new power, none it carries, not its signature, not Haste");
    checkEqual(after.affixes[0], uint8_t(sim::Affix::Wisdom), "the other line stays");
    check(after.bound && after.luck && after.refinement == 7, "the piece stays itself, bound");
    checkEqual(before - realm.money(), sim::recutZen(7), "the recut's Zen is paid");
    checkEqual(countOf(realm, chaos), 2, "and one Chaos spent");
    checkEqual(countOf(realm, soul), 2, "and no Soul");

    // Salvage: the piece goes, one prize comes, free of the binding.
    const int prize = realm.salvage(ring);
    check(prize >= 1 && prize <= sim::kSalvagePrizes, "a salvage answers its prize");
    check(realm.satchel()[ring].empty() || realm.satchel()[ring].item != fortune, "the ring is gone");
    const sim::ItemKey key = sim::salvagePrize(prize - 1);
    const int32_t got = noria.itemAt(key.group, key.number);
    check(countOf(realm, got) >= 1, "and its prize is in the bag");
    for (int s = sim::kWorn; s < sim::kSlots; ++s) {
        if (realm.satchel()[s].item == got) check(!realm.satchel()[s].bound, "the prize is free");
    }
    checkEqual(realm.salvage(realm.give(ice)), -1, "an unpowered ring is not his to salvage");

    // Combine: three whites into one blue at the lowest plus; any bound makes it bound.
    const int a = realm.give(wisdom, -1, 2), b = realm.give(wisdom, -1, 5),
              c = realm.give(fortune, -1, 4, -1, false, 0, 0, 0, nullptr, nullptr, true);
    const uint8_t blueOnes[3] = {uint8_t(sim::Affix::Leech), 0, 0};
    const int blue = realm.give(wisdom, -1, 0, -1, false, 0, 0, 0, nullptr, blueOnes);
    checkEqual(realm.combine(a, b, blue), -1, "three of mixed colours are refused");
    checkEqual(realm.combine(a, a, b), -1, "one piece twice is refused");
    const int chaosBefore = countOf(realm, chaos);
    const int64_t had = realm.money();
    const int made = realm.combine(a, b, c);
    check(made > 0, "three whites combine");
    if (made > 0) {
        const sim::Held& piece = realm.satchel()[made - 1];
        const content::ItemRow& row = noria.items[size_t(piece.item)];
        checkEqual(sim::affixCount(row, piece), 2, "into a blue");
        checkEqual(piece.refinement, 2, "at the lowest plus of the three");
        check(piece.bound, "bound, as one of the three was");
        check(sim::jewelerTakes(row), "a powered ring or pendant");
    }
    checkEqual(had - realm.money(), 500000, "for 500,000 Zen");
    checkEqual(chaosBefore - countOf(realm, chaos), 1, "and a Jewel of Chaos");
    check(realm.satchel()[b].empty() || realm.satchel()[b].item != wisdom ||
              realm.satchel()[b].refinement != 5,
          "and the three are gone");

    // Walking away shuts him.
    sim::Command close;
    close.kind = sim::Command::Kind::Close;
    close.a = int(sim::Command::Window::Jeweler);
    realm.command(close);
    realm.step();
    checkEqual(realm.jeweling(), -1, "Close shuts his window");
}
