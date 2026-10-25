// The Elites (sim/elite.h): Lorencia's one, raised with the realm on its own tables, its numbers
// against its breed's, killed, its spoils, and its time down before it rises again elsewhere.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include <string>

#include "content/tables.h"
#include "sim/command.h"
#include "sim/elite.h"
#include "sim/maps.h"
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
    checkEqual(elite->maxHealth, sim::eliteHealth(kind.health, 0), "Lorencia's sixteen times its breed's life");
    checkEqual(elite->stats.maximumDamage, int32_t(std::lround(float(kind.maximumDamage) * sim::kEliteDamage)),
               "two and a half times its damage");
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
    // Level 220 and life to spare since the Elites' life doubled and their blows went to x2.5
    // (2026-10-21): the 150 knight left the 8,400 one at 440 and fell.
    realm.raise(&lorencia, 7, column + 1, row, sim::Kin::DarkKnight, 220);
    realm.spend(550, 200, 300, 0);
    realm.give(lorencia.itemAt(0, 8), sim::kWeaponRight, 9);
    const sim::Body* target = eliteOf(realm);
    check(target != nullptr, "the Elite stands by him");
    if (!target) return;
    const uint32_t id = target->id;
    const uint64_t before = realm.hero().experience;
    int drops = 0, runes = 0, jewels = 0, excellents = 0;
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
                for (const sim::Lying& one : realm.lying()) {
                    if (one.id == uint32_t(h.a) && one.what.excellent != 0) ++excellents;
                }
            }
        }
    }
    check(died, "and kills it");
    std::printf("  it left %d things, %d jewels, %d excellent; he %s, the Elite at %d/%d\n", drops, jewels, excellents,
                realm.hero().alive() ? "stands" : "fell", died ? 0 : (eliteOf(realm) ? eliteOf(realm)->health : -1),
                died ? 0 : (eliteOf(realm) ? eliteOf(realm)->maxHealth : -1));
    check(drops >= sim::kEliteLeastPieces && drops <= sim::kEliteMostPieces + 2, "its spoils: 4 to 7 pieces and its jewels");
    check(jewels >= 1, "and a jewel, always");
    check(excellents >= 1 && excellents <= 2, "and one or two excellent pieces, never none");
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
        checkEqual(risen->maxHealth, sim::eliteHealth(lorencia.kinds[size_t(risen->kind)].health, 0),
                   "its numbers its new breed's");
    }

    // Every field map: its count of Elites standing, each one of its three strongest breeds, never a
    // boss, out of town (the user, 2026-10-21: "lets make elites on all maps").
    for (const char* world : {"lorencia", "noria", "devias", "dungeon", "losttower", "atlans", "tarkan", "icarus", "aida"}) {
        const sim::MapRow* row = sim::mapOf(world);
        content::Tables tables;
        std::string why;
        if (!row || !content::loadTables(std::string(MU2_ASSET_DIR) + "/cooked/" + world + "/" + world + ".mur", tables, why)) {
            check(false, "a field map's tables load");
            continue;
        }
        sim::Realm map;
        map.raise(&tables, 7, row->arrive[0], row->arrive[1], sim::Kin::DarkKnight, 1);
        int standing = 0;
        bool fair = true;
        std::string names;
        for (const sim::Body& one : map.bodies()) {
            if (!one.elite) continue;
            standing += one.alive() ? 1 : 0;
            const content::MonsterKind& kind = tables.kinds[size_t(one.kind)];
            names += " " + kind.label + " (level " + std::to_string(kind.level) + ", " + std::to_string(kind.health) +
                     " health, defence " + std::to_string(kind.defense) + "; as an Elite " + std::to_string(one.maxHealth) + ")";
            std::vector<int32_t> above;
            for (const content::MonsterNest& nest : tables.nests) {
                const content::MonsterKind& other = tables.kinds[nest.kind];
                if (sim::mayBeElite(other.number) && other.level > kind.level &&
                    std::find(above.begin(), above.end(), other.number) == above.end()) {
                    above.push_back(other.number);
                }
            }
            fair = fair && sim::mayBeElite(kind.number) && above.size() < size_t(sim::kEliteStrongest) &&
                   !tables.grid.safe(one.column(), one.row());
        }
        std::printf("  %s:%s\n", world, names.c_str());
        checkEqual(standing, sim::elitesOn(uint32_t(row->number)), "each field map stands its Elites");
        for (const sim::Body& one : map.bodies()) {
            if (one.elite) fair = fair && one.maxHealth == sim::eliteHealth(tables.kinds[size_t(one.kind)].health, uint32_t(row->number));
        }
        check(fair, "each one of its three strongest, no boss, out of town");
        // Two on a map are two breeds (the user, 2026-10-21: "not just 1 type").
        std::vector<int32_t> kinds;
        for (const sim::Body& one : map.bodies()) {
            if (one.elite && one.alive()) kinds.push_back(one.kind);
        }
        std::sort(kinds.begin(), kinds.end());
        check(std::adjacent_find(kinds.begin(), kinds.end()) == kinds.end(), "a map's Elites are different breeds");
    }

    // Kanturu's golden ones (sim::goldenOn): the Ruins' first Elite always a Satyros and the
    // Relics' a Twin Tale, the other never that breed; across seeds.
    struct Golden { const char* world; uint32_t map; int column, row; };
    for (const auto& [world, map, column, row] : {Golden{"kanturu", 37, 20, 218}, Golden{"kanturu_relics", 38, 77, 105}}) {
        content::Tables tables;
        if (!content::loadTables(std::string(MU2_ASSET_DIR) + "/cooked/" + world + "/" + world + ".mur", tables, error)) {
            check(false, "Kanturu's tables load");
            continue;
        }
        const int32_t golden = sim::goldenOn(map);
        check(golden != 0 && sim::goldenBoxOn(map) == 4, "a golden breed, leaving a Box of Kundun +4");
        int goldens = 0, others = 0, raised = 0;
        for (uint32_t seed = 1; seed <= 6; ++seed) {
            sim::Realm realm;
            realm.raise(&tables, seed, column, row, sim::Kin::DarkKnight, 400);
            for (const sim::Body& one : realm.bodies()) {
                if (!one.elite || !one.alive()) continue;
                ++raised;
                if (tables.kinds[size_t(one.kind)].number == golden) ++goldens;
                else ++others;
            }
        }
        std::printf("  %s: %d Elites over six seeds, %d golden\n", world, raised, goldens);
        checkEqual(goldens, 6, "every seed stands the one golden Elite");
        checkEqual(others, 6 * (sim::elitesOn(map) - 1), "and the rest are other breeds");
    }
}
