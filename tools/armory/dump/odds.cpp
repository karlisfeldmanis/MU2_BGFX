// --check-odds: the real Realm::leave, many times over, against drops.json's analytic odds.
//
// leave() is the Realm's own and private; the dump reaches it the one way C++ allows without
// touching the game's source -- an explicit instantiation may name a private member, and a friend
// it defines hands the member's pointer out (Steal, below). Nothing of the Realm is changed: its
// own dice roll its own branches, and what lies on the ground afterwards is counted.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "dump.h"
#include "sim/event.h"
#include "sim/items.h"
#include "sim/maps.h"
#include "sim/realm.h"

namespace armory {
namespace {

using namespace mu::sim;

template <class Tag, typename Tag::type Member>
struct Steal {
    friend typename Tag::type stolen(Tag) { return Member; }
};
struct LeaveTag {
    using type = void (Realm::*)(const Body&, const Body&);
    friend type stolen(LeaveTag);
};
struct MineTag {
    using type = Body& (Realm::*)();
    friend type stolen(MineTag);
};
struct LyingTag {
    using type = std::vector<Lying> Realm::*;
    friend type stolen(LyingTag);
};
struct HappeningsTag {
    using type = std::vector<Happening> Realm::*;
    friend type stolen(HappeningsTag);
};
template struct Steal<LeaveTag, &Realm::leave>;
template struct Steal<MineTag, &Realm::mine>;
template struct Steal<LyingTag, &Realm::lying_>;
template struct Steal<HappeningsTag, &Realm::happenings_>;

struct Sample {
    std::string label;
    Ground ground;
    int kills = 0;
};

int32_t hardest(const content::Tables& t) {
    int32_t best = -1;
    for (const content::MonsterNest& nest : t.nests) {
        if (nest.kind >= t.kinds.size()) continue;
        if (best < 0 || t.kinds[nest.kind].level > t.kinds[size_t(best)].level) best = int32_t(nest.kind);
    }
    return best;
}

}  // namespace

int checkOdds(const Dump& d, int kills, bool verbose) {
    std::vector<Sample> samples;
    if (const World* bc = d.world("bloodcastle")) {
        int32_t kind = -1;
        for (const int32_t number : kCastleBreeds[3]) {
            for (size_t i = 0; i < bc->tables->kinds.size(); ++i) {
                if (bc->tables->kinds[i].number != number) continue;
                if (kind < 0 || bc->tables->kinds[i].label == "Red Skeleton Knight") kind = int32_t(i);
            }
        }
        if (kind >= 0) {
            Ground g{bc, uint32_t(kind), 4};
            samples.push_back({"Blood Castle 4 " + bc->tables->kinds[size_t(kind)].label, g, kills});
        }
    }
    for (const char* name : {"tarkan", "lorencia", "icarus", "atlans"}) {
        const World* w = d.world(name);
        if (!w) continue;
        const int32_t kind = hardest(*w->tables);
        if (kind < 0) continue;
        samples.push_back({std::string(name) + " " + w->tables->kinds[size_t(kind)].label, Ground{w, uint32_t(kind)}, kills});
        if (std::string(name) == "lorencia") {
            Ground g{w, uint32_t(kind)};
            g.elite = true;
            samples.push_back({"lorencia Elite " + w->tables->kinds[size_t(kind)].label, g, std::max(1, kills / 10)});
        }
    }
    const auto leave = stolen(LeaveTag{});
    const auto mine = stolen(MineTag{});
    const auto lying = stolen(LyingTag{});
    const auto happenings = stolen(HappeningsTag{});
    constexpr double kBand = 4.5;  // standard deviations: many items are held at once
    int drifted = 0;
    const auto start = std::chrono::steady_clock::now();
    std::printf("odds check: the real Realm::leave against drops.json, %d kills a monster (%.1f sigma band)\n", kills, kBand);
    for (const Sample& s : samples) {
        const World& w = *s.ground.world;
        const MapRow* row = mapOf(w.name);
        Realm realm;
        const Kin kin = Kin::DarkKnight;
        if (!realm.raise(w.tables.get(), 20261024, row ? row->arrive[0] : 128, row ? row->arrive[1] : 128, kin, 400)) {
            std::printf("  %s: the realm does not raise\n", s.label.c_str());
            ++drifted;
            continue;
        }
        if (s.ground.castle > 0) realm.setCastle(s.ground.castle);
        const content::Tables& t = *realm.tables();
        const std::vector<double> want = perKill(d, s.ground, kin);
        std::vector<int> got(t.items.size(), 0);
        std::vector<uint8_t> seen(t.items.size(), 0);
        for (int i = 0; i < s.kills; ++i) {
            (realm.*lying).clear();
            (realm.*happenings).clear();
            Body& hero = (realm.*mine)();
            Body dead = hero;
            dead.player = false;
            dead.kind = int32_t(s.ground.kind);
            dead.level = t.kinds[s.ground.kind].level;
            dead.elite = s.ground.elite;
            (realm.*leave)(dead, hero);
            for (const Lying& one : (realm.*lying)) {
                if (one.what.empty() || size_t(one.what.item) >= seen.size() || seen[size_t(one.what.item)]) continue;
                seen[size_t(one.what.item)] = 1;
                ++got[size_t(one.what.item)];
            }
            for (const Lying& one : (realm.*lying)) {
                if (!one.what.empty() && size_t(one.what.item) < seen.size()) seen[size_t(one.what.item)] = 0;
            }
        }
        if (verbose) {
            // The commonest few, for a reader to see the two side by side.
            std::vector<size_t> order;
            for (size_t i = 0; i < t.items.size(); ++i) {
                if (want[i] > 0.0) order.push_back(i);
            }
            std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return want[a] > want[b]; });
            std::vector<size_t> shown(order.begin(), order.begin() + std::min<size_t>(order.size(), 8));
            for (size_t i : order) {
                const std::string& l = t.items[i].label;
                if ((l == "Sword of Destruction" || sim::lochsFeather(t.items[i])) &&
                    std::find(shown.begin(), shown.end(), i) == shown.end()) {
                    shown.push_back(i);
                }
            }
            for (const size_t i : shown) {
                std::printf("    %-28s dump 1 in %8.1f, fell 1 in %8.1f\n", t.items[i].label.c_str(), 1.0 / want[i],
                            got[i] ? double(s.kills) / got[i] : 0.0);
            }
        }
        int bad = 0, held = 0;
        for (size_t i = 0; i < t.items.size(); ++i) {
            const double p = want[i];
            const double n = double(s.kills);
            if (p <= 0.0) {
                if (got[i] > 0) {
                    std::printf("  DRIFT %s: %s fell %d times, the dump says never\n", s.label.c_str(),
                                t.items[i].label.c_str(), got[i]);
                    ++bad;
                }
                continue;
            }
            ++held;
            const double mean = n * p, sd = std::sqrt(n * p * (1.0 - p));
            const double z = (double(got[i]) - mean) / std::max(sd, 1e-9);
            if (std::fabs(z) > kBand && std::fabs(double(got[i]) - mean) > 3.0) {
                std::printf("  DRIFT %s: %s fell %d times, the dump says %.1f (1 in %.0f), %.1f sigma\n",
                            s.label.c_str(), t.items[i].label.c_str(), got[i], mean, 1.0 / p, z);
                ++bad;
            }
        }
        std::printf("  %-40s level %3d, %d kills: %d items held, %s\n", s.label.c_str(), t.kinds[s.ground.kind].level,
                    s.kills, held, bad ? "DRIFT" : "within the band");
        drifted += bad;
    }
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("odds check: %s in %.1f s\n", drifted ? "FAILED" : "passed", seconds);
    return drifted ? 1 : 0;
}

}  // namespace armory
