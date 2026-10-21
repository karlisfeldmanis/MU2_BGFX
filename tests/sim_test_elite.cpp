// The Elites (sim/elite.h): Lorencia's one, raised with the realm on its own tables, its numbers
// against its breed's, killed, its spoils, and its time down before it rises again elsewhere.

#include <algorithm>
#include <cstdio>
#include <vector>
#include <string>

#include "content/tables.h"
#include "sim/command.h"
#include "sim/elite.h"
#include "sim/realm.h"

using namespace mu;

namespace {

void (*check)(bool held, const char* what) = nullptr;
void (*checkEqual)(long long got, long long wanted, const char* what) = nullptr;

const sim::Body* eliteOf(const sim::Realm& realm) {
    for (const sim::Body& one : realm.bodies()) {
        if (one.elite) return &one;
    }
    return nullptr;
}

}  // namespace

void testElites(void (*checkOf)(bool, const char*),
                void (*checkEqualOf)(long long, long long, const char*)) {
    check = checkOf;
    checkEqual = checkEqualOf;
    std::printf("elites\n");
    content::Tables lorencia;
    std::string error;
    const std::string path = std::string(MU2_ASSET_DIR) + "/cooked/lorencia/lorencia.mur";
    check(content::loadTables(path, lorencia, error), "Lorencia's tables load");

    sim::Realm first;
    check(first.raise(&lorencia, 7, 135, 128, sim::Kin::DarkKnight, 1), "Lorencia raises");
    int elites = 0;
    for (const sim::Body& one : first.bodies()) elites += one.elite ? 1 : 0;
    checkEqual(elites, sim::elitesOn(0), "Lorencia stands its one Elite");
    const sim::Body* elite = eliteOf(first);
    if (!elite) return;
    const content::MonsterKind& kind = lorencia.kinds[size_t(elite->kind)];
    std::printf("  an Elite %s at (%d, %d)\n", kind.label.c_str(), elite->column(), elite->row());
    check(elite->alive() && elite->monster(), "standing from the first tick, a monster to the fight");
    checkEqual(elite->maxHealth, kind.health * sim::kEliteLife, "eight times its breed's life");
    checkEqual(elite->stats.maximumDamage, kind.maximumDamage * sim::kEliteDamage, "twice its damage");
    check(elite->stats.defense >= kind.defense, "and more defence");
    check(!lorencia.grid.safe(elite->column(), elite->row()), "out in the field, not in town");
    bool ofTheMap = false;
    for (const content::MonsterNest& nest : lorencia.nests) ofTheMap |= int32_t(nest.kind) == elite->kind;
    check(ofTheMap, "a breed of the map's own nests");
    // One of its three strongest breeds, bosses aside: no more than two others stand above it.
    std::vector<int32_t> above;
    for (const content::MonsterNest& nest : lorencia.nests) {
        const content::MonsterKind& other = lorencia.kinds[nest.kind];
        if (sim::mayBeElite(other.number) && other.level > kind.level &&
            std::find(above.begin(), above.end(), other.number) == above.end()) {
            above.push_back(other.number);
        }
    }
    check(above.size() < size_t(sim::kEliteStrongest), "one of the map's three strongest breeds");

    // The same seed, the same Elite: a mirror raised beside it agrees.
    sim::Realm again;
    again.raise(&lorencia, 7, 135, 128, sim::Kin::DarkKnight, 1);
    const sim::Body* twin = eliteOf(again);
    check(twin && twin->id == elite->id && twin->kind == elite->kind && twin->column() == elite->column() &&
              twin->row() == elite->row(),
          "one seed, one Elite, wherever it is raised");

    // A strong knight beside it, a +9 sword in hand, fights it to the end.
    const int column = elite->column(), row = elite->row();
    sim::Realm realm;
    realm.raise(&lorencia, 7, column + 1, row, sim::Kin::DarkKnight, 150);
    realm.spend(500, 150, 90, 0);
    realm.give(lorencia.itemAt(0, 8), sim::kWeaponRight, 9);
    const sim::Body* target = eliteOf(realm);
    check(target != nullptr, "the Elite stands by him");
    if (!target) return;
    const uint32_t id = target->id;
    const uint64_t before = realm.hero().experience;
    int drops = 0, runes = 0, jewels = 0;
    bool died = false;
    for (int tick = 0; tick < 6000 && !died && realm.hero().alive(); ++tick) {
        if (tick % 20 == 0) {
            sim::Request attack;
            attack.kind = sim::Request::Kind::Attack;
            attack.target = id;
            realm.ask(attack);
        }
        realm.step();
        for (const sim::Happening& h : realm.happenings()) {
            if (h.who == id && h.what == sim::What::Died) died = true;
            if (h.who == id && h.what == sim::What::Dropped && h.b >= 0) {
                ++drops;
                const content::ItemRow& row = lorencia.items[size_t(h.b)];
                runes += sim::creation(row) ? 1 : 0;
                jewels += sim::refiningJewel(row) ? 1 : 0;
            }
        }
    }
    check(died, "and kills it");
    std::printf("  it left %d things, %d jewels\n", drops, jewels);
    check(drops >= sim::kEliteLeastPieces && drops <= sim::kEliteMostPieces + 2, "its spoils: 4 to 7 pieces and its jewels");
    check(jewels >= 1, "and a jewel, always");
    checkEqual(runes, 0, "and never a rune");
    check(realm.hero().experience > before, "worth his while");

    // Down a while, then up again as an Elite, maybe another breed somewhere else.
    for (int tick = 0; tick < int(sim::kEliteRiseLeastTicks) - 40; ++tick) realm.step();
    const sim::Body* down = eliteOf(realm);
    check(down && !down->alive(), "still down short of twenty minutes");
    for (int tick = 0; tick < int(sim::kEliteRiseMostTicks - sim::kEliteRiseLeastTicks) + 100; ++tick) realm.step();
    const sim::Body* risen = eliteOf(realm);
    check(risen && risen->alive() && risen->id == id, "up again by forty, the same body");
    if (risen) {
        std::printf("  risen as an Elite %s at (%d, %d)\n", lorencia.kinds[size_t(risen->kind)].label.c_str(),
                    risen->column(), risen->row());
        checkEqual(risen->maxHealth, lorencia.kinds[size_t(risen->kind)].health * sim::kEliteLife,
                   "its numbers its new breed's");
    }
}
