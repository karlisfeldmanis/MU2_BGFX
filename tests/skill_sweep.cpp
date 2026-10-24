// Which skills are too strong (the user, 2026-10-21: "make some tests which spells now are super
// powered"): a raid kit's level-400 hero (content/raid/party.json), raised on a hunting ground,
// attacks the nearest monster with one skill for a few minutes, every attack skill of his class
// in turn, bare and with each of the class's talent builds (tests/talents_fight.cpp's). The
// damage and kills an hour, each skill against the class's median, so an outlier stands out.
//   clang++ -std=c++20 -O2 -I src -DMU2_ASSET_DIR=\"$PWD/out/assets\" tests/skill_sweep.cpp
//     out/build/libmu_sim.a out/build/libmu_content_data.a out/build/libmu_core.a
//     out/build/libmu_zstd.a -lz -o out/build/skill_sweep
//   skill_sweep KIT MAP SEEDS MINUTES [KITS.json]
// The kits are content/raid/party.json's, or another file's of its shape: tests/kits/sweep.json
// holds the Magic Gladiator and a knight with two swords, whom no raider is.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "content/tables.h"
#include "sim/maps.h"
#include "sim/raid_party.h"
#include "sim/realm.h"
#include "sim/skills.h"
#include "sim/talents.h"

using namespace mu;
using Col = std::array<uint8_t, sim::kTalentNodes>;

// The bots' builds, as tests/talents_fight.cpp has them.
static sim::TalentRanks build(sim::Kin kin, const std::string& which) {
    const Col F{10, 10, 1, 10, 10, 1, 10, 10, 1}, S{10, 0, 1, 10, 0, 1, 10, 0, 1}, Z{};
    const Col f10{10, 0, 0, 0, 0, 0, 0, 0, 0}, fMore{10, 10, 1, 10, 9, 0, 0, 0, 0};
    sim::TalentRanks r{};
    auto put = [&](std::initializer_list<Col> cols) { size_t c = 0; for (const Col& col : cols) r[c++] = col; };
    if (kin == sim::Kin::DarkKnight) {
        if (which == "dual") put({Z, F, Z, S, F, fMore});
        else if (which == "tank") put({F, Z, F, Z, F, f10});
        else put({F, Z, Z, F, F, f10});
    } else if (kin == sim::Kin::DarkWizard) {
        if (which == "lightning") put({Z, F, F, F, Z, f10});
        else if (which == "spirit") put({Z, Z, S, F, F, fMore});
        else put({F, Z, S, F, Z, fMore});
    } else if (kin == sim::Kin::FairyElf) {
        if (which == "penetration") put({F, F, Z, Z, F, f10});
        else put({F, S, Z, Z, F, fMore});
    } else if (kin == sim::Kin::MagicGladiator) {
        // tests/talents_bench.cpp's two, seven columns.
        if (which == "caster") put({F, Z, F, S, F, S, Col{10, 10, 1, 3, 0, 0, 0, 0, 0}});
        else put({F, F, Z, Z, F, F, Col{10, 10, 1, 6, 0, 0, 0, 0, 0}});
    }
    return r;
}

static std::vector<std::string> buildsOf(sim::Kin kin) {
    if (kin == sim::Kin::DarkKnight) return {"whirlwind", "dual", "tank"};
    if (kin == sim::Kin::DarkWizard) return {"fire", "lightning", "spirit"};
    if (kin == sim::Kin::MagicGladiator) return {"spellblade", "caster"};
    return {"multishot", "penetration"};
}

// A skill that strikes: not a guard, a buff, a heal, a summon or a blink.
static bool strikes(const sim::SkillRow& row) {
    return row.boonTicks == 0 && row.mightTicks == 0 && row.summons == 0 && !row.mends && !row.blinks &&
           row.number != sim::skill::kDefense && row.number != sim::skill::kSoulBarrier && !row.sharpens;
}

struct Result {
    double damage = 0, kills = 0, deaths = 0, hits = 0;
};

static Result fight(const content::Tables& tables, const sim::MapRow& map, const sim::RaiderKit& kit,
                    int32_t skill, const std::string& talents, int seeds, double minutes) {
    Result out;
    for (int s = 0; s < seeds; ++s) {
        sim::Realm realm;
        if (!realm.raise(&tables, 1000 + s, map.arrive[0], map.arrive[1], kit.kin, kit.level)) return out;
        realm.benchDressHero(kit);
        realm.learn(skill);
        if (kit.kin == sim::Kin::FairyElf) {
            const int bolts = realm.hero().archer == 2;
            const int quiver = tables.itemAt(4, bolts ? 7 : 15);
            if (quiver >= 0) {
                realm.give(quiver, sim::kWeaponLeft);
                for (int i = 0; i < 6; ++i) realm.give(quiver);
            }
        }
        for (const int number : {3, 3, 3, 3, 6, 6}) {
            const int item = tables.itemAt(14, number);
            if (item >= 0) realm.give(item, -1, 0, sim::stackMost(tables.items[size_t(item)]));
        }
        if (!talents.empty()) {
            const sim::TalentRanks want = build(kit.kin, talents == "whirlwind" || talents == "fire" || talents == "multishot" || talents == "spellblade" ? "" : talents);
            for (int pass = 0; pass < 3; ++pass)
                for (int c = 0; c < sim::kTalentColumnsMost; ++c)
                    for (int i = 0; i < sim::kTalentNodes; ++i) {
                        const int more = want[size_t(c)][size_t(i)] - realm.hero().talents[size_t(c)][size_t(i)];
                        if (more > 0) realm.talentTake(c, i, more);
                    }
        }
        const uint32_t me = realm.hero().id;
        const int64_t ticks = int64_t(minutes * 60.0 * 20.0);
        for (int64_t t = 0; t < ticks; ++t) {
            const sim::Body& hero = realm.hero();
            if (t % 5 == 0 && hero.alive()) {
                const sim::Request& now = realm.order();
                bool busy = false;
                if (now.kind == sim::Request::Kind::Attack)
                    for (const sim::Body& b : realm.bodies()) if (b.id == now.target && b.alive()) busy = true;
                if (!busy) {
                    float best = 1e9f;
                    uint32_t at = 0;
                    for (const sim::Body& b : realm.bodies()) {
                        if (!b.monster() || !b.alive()) continue;
                        const float d = std::hypot(b.x - hero.x, b.y - hero.y);
                        if (d < best && d < 30.0f) { best = d; at = b.id; }
                    }
                    if (at) realm.ask({.kind = sim::Request::Kind::Attack, .target = at, .skill = skill});
                }
            }
            realm.step();
            for (const sim::Happening& h : realm.happenings()) {
                if (h.what == sim::What::Died && h.whom == me && h.who != me) ++out.kills;
                if (h.what == sim::What::Died && h.who == me) ++out.deaths;
                if (h.what == sim::What::Hit && h.who == me) {
                    out.damage += h.a;
                    ++out.hits;
                }
            }
        }
    }
    const double perHour = 60.0 / minutes / double(seeds);
    out.damage *= perHour;
    out.kills *= perHour;
    out.deaths *= perHour;
    out.hits *= perHour;
    return out;
}

int main(int argc, char** argv) {
    if (argc < 5) {
        std::printf("skill_sweep KIT MAP SEEDS MINUTES [KITS.json]\n");
        return 2;
    }
    const std::string kitName = argv[1];
    const int mapNumber = std::atoi(argv[2]), seeds = std::atoi(argv[3]);
    const double minutes = std::atof(argv[4]);
    std::vector<sim::RaiderKit> party;
    sim::readParty(argc > 5 ? std::string(argv[5]) : std::string(MU2_ASSET_DIR) + "/../../content/raid/party.json", &party);
    const sim::RaiderKit* kit = nullptr;
    for (const auto& one : party) if (one.name == kitName) kit = &one;
    const sim::MapRow* map = sim::mapNumbered(mapNumber);
    if (!kit || !map) return 3;
    content::Tables tables;
    std::string error;
    const std::string world = map->world;
    if (!content::loadTables(std::string(MU2_ASSET_DIR) + "/cooked/" + world + "/" + world + ".mur", tables, error)) {
        std::printf("%s\n", error.c_str());
        return 4;
    }
    struct Line {
        std::string name;
        int32_t number;
        Result bare, best;
        std::string bestBuild;
    };
    std::vector<Line> lines;
    for (int i = 0; i < sim::kSkills; ++i) {
        const sim::SkillRow& row = sim::skillAt(i);
        // His class's, and a Gladiator's borrowed knight's and wizard's (sim::skillFor).
        if (!sim::skillFor(row, kit->kin) || !strikes(row)) continue;
        Line line{row.name, row.number, fight(tables, *map, *kit, row.number, "", seeds, minutes), {}, "-"};
        for (const std::string& b : buildsOf(kit->kin)) {
            const Result r = fight(tables, *map, *kit, row.number, b, seeds, minutes);
            if (r.damage > line.best.damage) {
                line.best = r;
                line.bestBuild = b;
            }
        }
        std::fprintf(stderr, "  %s done\n", row.name);
        lines.push_back(line);
    }
    // His plain attack, bare: a skill his kit cannot throw (a weapon family, the Dinorant) falls
    // back to it, and its row is marked rather than counted.
    const Result plain = fight(tables, *map, *kit, 0, "", seeds, minutes);
    const auto fellBack = [&](const Line& l) {
        return plain.damage > 0 && std::fabs(l.bare.damage - plain.damage) < plain.damage * 0.005;
    };
    std::vector<double> damages;
    for (const Line& l : lines) if (l.best.damage > 0 && !fellBack(l)) damages.push_back(l.best.damage);
    std::sort(damages.begin(), damages.end());
    const double median = damages.empty() ? 1.0 : damages[damages.size() / 2];
    std::sort(lines.begin(), lines.end(), [](const Line& a, const Line& b) { return a.best.damage > b.best.damage; });
    // What the kit put on him, so a piece his class refuses shows as missing.
    int worn = 0;
    {
        sim::Realm dressed;
        dressed.raise(&tables, 1000, map->arrive[0], map->arrive[1], kit->kin, kit->level);
        dressed.benchDressHero(*kit);
        for (int slot = 0; slot < sim::kWorn; ++slot) {
            const sim::Held& h = dressed.satchel()[slot];
            if (h.empty()) continue;
            ++worn;
            // The bench puts a kit on whole, asked nothing: what his points could not wear is said.
            if (!sim::fits(tables, dressed.wearer(), h)) {
                const content::ItemRow& r = tables.items[size_t(h.item)];
                const sim::Needs a = sim::asks(r, h.refinement, h.excellent != 0);
                std::printf("  (%s cannot wear %s +%d: it asks level %d, strength %d, agility %d, energy %d, vitality %d)\n",
                            kitName.c_str(), r.label.c_str(), int(h.refinement), a.level, a.strength, a.agility,
                            a.energy, a.vitality);
            }
        }
    }
    std::printf("%s on %s, %d seeds x %.1f min, level %d, %d of %zu pieces worn; per hour, best talent build; x median of the class\n",
                kitName.c_str(), world.c_str(), seeds, minutes, kit->level, worn, kit->pieces.size());
    std::printf("%-18s %4s | %9s %6s | %9s %6s %6s %5s %-12s | %6s %5s\n", "skill", "no", "bare dmg", "kills",
                "talent dmg", "kills", "deaths", "gain", "build", "dmg/hit", "x med");
    std::printf("%-18s %4s | %8.2fM %6.0f |\n", "(plain attack)", "-", plain.damage / 1e6, plain.kills);
    for (const Line& l : lines) {
        if (fellBack(l)) {
            std::printf("%-18s %4d | not thrown with this kit's weapon or mount\n", l.name.c_str(), l.number);
            continue;
        }
        const double gain = l.bare.damage > 0 ? l.best.damage / l.bare.damage : 0;
        const double perHit = l.best.hits > 0 ? l.best.damage / l.best.hits : 0;
        std::printf("%-18s %4d | %8.2fM %6.0f | %8.2fM %6.0f %6.1f %5.2f %-12s | %6.0f %5.2f\n", l.name.c_str(),
                    l.number, l.bare.damage / 1e6, l.bare.kills, l.best.damage / 1e6, l.best.kills, l.best.deaths, gain,
                    l.bestBuild.c_str(), perHit, l.best.damage / median);
    }
    return 0;
}
