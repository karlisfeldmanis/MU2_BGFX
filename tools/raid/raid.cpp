// The raid, headless: the Golden Dragon against source/raid/party.json, played to a kill, a wipe
// or its thirty minutes, as many times as asked, and what happened (docs/golden-dragon-raid.md
// §2a, "how tough is measured").
//
// Each run raises Lorencia with the party standing in one of WebZen's three Dragon Event boxes
// (DragonEvent.cpp:103-109), the boxes taken in turn, begins the invasion, and lets the dragon
// land by the realm's own rule. Every one of the ten is played by the raiders' mind
// (Realm::setRaid's hand); whoever falls stands up in town and runs back, and a run reaching
// the hard enrage is lost.
//
//   build/raid [--runs N] [--seed S] [--party PATH] [--players N] [--box A|B|C]
//              [--stage 1-4] [--verbose]
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "content/tables.h"
#include "core/log.h"
#include "sim/raid_party.h"
#include "sim/realm.h"

using namespace mu;

namespace {

struct Box {
    char name;
    int x1, y1, x2, y2;
};
// WebZen's Lorencia boxes for its Dragon Event (DragonEvent.cpp:103-109).
constexpr Box kBoxes[3] = {
    {'A', 135, 61, 146, 70},
    {'B', 120, 204, 126, 219},
    {'C', 67, 116, 77, 131},
};

const char* roleName(sim::RaidRole role) {
    switch (role) {
        case sim::RaidRole::Tank: return "tank";
        case sim::RaidRole::Melee: return "melee";
        case sim::RaidRole::Healer: return "healer";
        case sim::RaidRole::Archer: return "archer";
        case sim::RaidRole::Wizard: return "wizard";
    }
    return "?";
}

struct Options {
    int runs = 10;
    uint64_t seed = 1;
    std::string party = std::string(MU2_SOURCE_DIR) + "/source/raid/party.json";
    int players = sim::kRaidPlayers;
    int box = -1;
    int stage = 0;
    bool verbose = false;
};

struct Death {
    std::string name, cause;
    int64_t at = 0;
};

struct Outcome {
    uint64_t seed = 0;
    char box = '?';
    bool won = false;
    bool wiped = false;
    int64_t ticks = 0;        // landing to the end
    int stage = 0;
    std::vector<Death> deaths;
    std::map<std::string, int64_t> dealt;  // by role, on the dragon
    std::map<std::string, int64_t> byName, onMinions;
    std::map<std::string, int> missed, bitten;
    std::map<std::string, int> casts;
    int potions = 0;
    int dragonHealth = 0;
};

std::string clock(int64_t ticks) {
    char out[16];
    std::snprintf(out, sizeof(out), "%lld:%02lld", (long long)(ticks / 20 / 60),
                  (long long)(ticks / 20 % 60));
    return out;
}

Outcome runOnce(const content::Tables& tables, const std::vector<sim::RaiderKit>& party,
                const Options& options, uint64_t seed, const Box& box) {
    Outcome out;
    out.seed = seed;
    out.box = box.name;
    sim::Realm realm;
    realm.setRaid(options.players, party, true);
    const int column = (box.x1 + box.x2) / 2, row = (box.y1 + box.y2) / 2;
    if (!realm.raise(&tables, seed, column, row, party[0].kin, party[0].level)) {
        std::printf("raid: the realm would not raise\n");
        return out;
    }
    realm.invade();
    // Until it has landed and its stages begun.
    for (int i = 0; i < 40 * 20 && realm.raidStage() == sim::RaidStage::None; ++i) realm.step();
    if (realm.raidStage() == sim::RaidStage::None) {
        std::printf("raid: seed %llu, the dragon never landed\n", (unsigned long long)seed);
        return out;
    }
    if (options.stage > 1) realm.raidSkipTo(sim::RaidStage(options.stage));
    const sim::Body* dragon = realm.invader();
    const uint32_t dragonId = dragon->id;
    // Who is who: the party's ids, names and roles.
    std::map<uint32_t, int> partyOf;
    partyOf[realm.hero().id] = 0;
    for (int i = 0; i < realm.raiderCount(); ++i) partyOf[realm.raiderAt(i)->id] = i + 1;
    std::map<uint32_t, bool> minion;
    for (int i = 0; i < realm.minionCount(); ++i) minion[realm.minionAt(i)->id] = true;
    std::map<uint32_t, bool> down;
    std::map<uint32_t, std::string> lastHurt;
    const int64_t began = realm.tick();
    const int64_t most = 30 * 60 * 20;
    while (realm.tick() - began < most) {
        realm.step();
        for (const sim::Happening& h : realm.happenings()) {
            if (h.what == sim::What::Hit && partyOf.count(h.whom)) {
                lastHurt[h.whom] = h.who == dragonId ? (h.thrown ? "fire" : (h.boss ? "flame of evil" : "bite"))
                                   : minion.count(h.who) ? "minion" : "other";
            }
            if (h.what == sim::What::Hit && h.whom == dragonId && partyOf.count(h.who)) {
                out.dealt[roleName(party[size_t(partyOf[h.who])].role)] += h.a;
                out.byName[party[size_t(partyOf[h.who])].name] += h.a;
            }
            if (h.what == sim::What::Hit && minion.count(h.whom) && partyOf.count(h.who)) {
                out.onMinions[party[size_t(partyOf[h.who])].name] += h.a;
            }
            if (h.what == sim::What::Raid && h.a == int32_t(sim::RaidEvent::Raider) &&
                partyOf.count(h.who)) {
                out.casts[party[size_t(partyOf[h.who])].name + (h.b == int32_t(sim::RaiderAct::Cast)
                                                                    ? ":" + std::to_string(h.c)
                                                                    : h.b == 0 ? ":swing" : h.b == 2 ? ":drink" : ":dodge")]++;
            }
            if (h.what == sim::What::Missed && h.whom == dragonId && partyOf.count(h.who)) {
                ++out.missed[party[size_t(partyOf[h.who])].name];
            }
            if (h.what == sim::What::Hit && h.who == dragonId && partyOf.count(h.whom) && !h.thrown) {
                ++out.bitten[party[size_t(partyOf[h.whom])].name];
            }
            if (h.what == sim::What::Died && partyOf.count(h.who)) {
                down[h.who] = true;
                out.deaths.push_back({party[size_t(partyOf[h.who])].name, lastHurt[h.who],
                                      realm.tick() - began});
            }
            if (h.what == sim::What::Raid && h.who == dragonId &&
                h.a == int32_t(sim::RaidEvent::Stage)) {
                out.stage = std::max(out.stage, int(h.b));
                if (options.verbose) {
                    std::printf("  %s stage %d\n", clock(realm.tick() - began).c_str(), h.b);
                }
            }
            if (options.verbose && h.what == sim::What::Raid && h.who == dragonId &&
                (h.a == int32_t(sim::RaidEvent::Wave) || h.a == int32_t(sim::RaidEvent::Aloft))) {
                std::printf("  %s %s %d\n", clock(realm.tick() - began).c_str(),
                            h.a == int32_t(sim::RaidEvent::Wave) ? "minions" : "aloft", h.b);
            }
        }
        // Killed, and not gone with its thirty minutes (Dismissed): a win.
        bool gone = false;
        for (const sim::Happening& h : realm.happenings()) {
            if (h.who == dragonId && h.what == sim::What::Died) out.won = true;
            if (h.who == dragonId && h.what == sim::What::Dismissed) gone = true;
        }
        if (out.won || gone) break;
        if (std::getenv("RAID_WATCH") && (realm.tick() - began) % 100 == 0) {
            const sim::Body* d = realm.invader();
            std::printf("  %s dragon %.0f,%.0f hp %d aloft %d |", clock(realm.tick() - began).c_str(), d->x, d->y,
                        d->health, int(realm.raidAloft()));
            const sim::Body& h0 = realm.hero();
            std::printf(" hero %.0f,%.0f %d", h0.x, h0.y, h0.health);
            for (int i = 0; i < realm.raiderCount(); ++i) {
                const sim::Body* r = realm.raiderAt(i);
                std::printf(" %d:%.0f,%.0f %d", i + 1, r->x, r->y, r->health);
            }
            std::printf("\n");
        }
        // The fallen stand up in town and come back (Realm::reviveRaider), so a fight is lost
        // when the hard enrage's Inferno has landed on it.
        if (realm.tick() - realm.raidLandedAt() > sim::kHardEnrage + sim::kInfernoTell) {
            out.wiped = true;
            break;
        }
    }
    out.ticks = realm.tick() - began;
    out.dragonHealth = realm.invader()->health;
    for (int i = 0; i <= realm.raiderCount(); ++i) {
        out.potions += party[size_t(i)].potions - realm.raidPotions(i);
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (!std::strcmp(argv[i], "--runs")) options.runs = std::max(1, std::atoi(next()));
        else if (!std::strcmp(argv[i], "--seed")) options.seed = std::strtoull(next(), nullptr, 10);
        else if (!std::strcmp(argv[i], "--party")) options.party = next();
        else if (!std::strcmp(argv[i], "--players")) options.players = std::max(1, std::atoi(next()));
        else if (!std::strcmp(argv[i], "--box")) options.box = std::max(0, std::min(2, next()[0] - 'A'));
        else if (!std::strcmp(argv[i], "--stage")) options.stage = std::atoi(next());
        else if (!std::strcmp(argv[i], "--verbose")) options.verbose = true;
        else {
            std::printf("usage: raid [--runs N] [--seed S] [--party PATH] [--players N] "
                        "[--box A|B|C] [--stage 1-4] [--verbose]\n");
            return 2;
        }
    }
    if (!std::getenv("RAID_LOG")) core::logSilence(true);
    content::Tables tables;
    std::string error;
    const std::string path = std::string(MU2_ASSET_DIR) + "/cooked/lorencia/lorencia.mur";
    if (!content::loadTables(path, tables, error)) {
        std::printf("raid: %s: %s\n", path.c_str(), error.c_str());
        return 1;
    }
    std::vector<sim::RaiderKit> party;
    if (!sim::readParty(options.party, &party)) {
        std::printf("raid: %s did not read\n", options.party.c_str());
        return 1;
    }
    {
        sim::Realm check;
        check.raise(&tables, 1, 140, 65);
        bool legal = true;
        for (const sim::RaiderKit& kit : party) {
            const std::string why = check.kitRefusal(kit);
            if (!why.empty()) {
                std::printf("raid: refused: %s\n", why.c_str());
                legal = false;
            }
        }
        if (!legal) return 1;
    }
    std::printf("raid: %zu in the party, the dragon tough for %d (%d health)\n", party.size(),
                options.players, sim::raidHealth(options.players));
    std::vector<Outcome> outcomes;
    for (int run = 0; run < options.runs; ++run) {
        const uint64_t seed = options.seed + uint64_t(run);
        const Box& box = kBoxes[options.box >= 0 ? options.box : run % 3];
        if (options.verbose) std::printf("run %d, seed %llu, box %c\n", run, (unsigned long long)seed, box.name);
        const Outcome one = runOnce(tables, party, options, seed, box);
        outcomes.push_back(one);
        std::printf("seed %-4llu box %c  %-5s %6s  stage %d  dragon %6d  deaths %zu  potions %3d ",
                    (unsigned long long)one.seed, one.box,
                    one.won ? "WIN" : one.wiped ? "WIPE" : "TIME", clock(one.ticks).c_str(),
                    one.stage, one.dragonHealth, one.deaths.size(), one.potions);
        for (const Death& d : one.deaths) {
            std::printf(" %s(%s %s)", d.name.c_str(), d.cause.c_str(), clock(d.at).c_str());
        }
        std::printf("\n");
        if (options.verbose) {
            for (const sim::RaiderKit& kit : party) {
                const auto get = [&](const auto& m) {
                    const auto it = m.find(kit.name);
                    return it == m.end() ? 0 : int64_t(it->second);
                };
                std::printf("    %-7s %-6s dragon %8lld  minions %7lld  missed %4lld  bitten %4lld\n",
                            kit.name.c_str(), roleName(kit.role), (long long)get(one.byName),
                            (long long)get(one.onMinions), (long long)get(one.missed),
                            (long long)get(one.bitten));
                std::printf("      ");
                for (const auto& [what, n] : one.casts) {
                    if (what.rfind(kit.name + ":", 0) == 0) std::printf(" %s %d", what.c_str() + kit.name.size() + 1, n);
                }
                std::printf("\n");
            }
        }
    }
    // The sum: wins, the kill's time, deaths in a win, what killed, who dealt.
    int wins = 0;
    std::vector<int64_t> times;
    double winDeaths = 0.0;
    std::map<std::string, int> causes;
    std::map<std::string, int64_t> dealt;
    int64_t all = 0;
    for (const Outcome& one : outcomes) {
        if (one.won) {
            ++wins;
            times.push_back(one.ticks);
            winDeaths += double(one.deaths.size());
        }
        for (const Death& d : one.deaths) ++causes[d.cause];
        for (const auto& [role, amount] : one.dealt) {
            dealt[role] += amount;
            all += amount;
        }
    }
    std::sort(times.begin(), times.end());
    std::printf("\n%d of %d won", wins, int(outcomes.size()));
    if (!times.empty()) {
        std::printf(", the kill in %s median (%s to %s), %.1f deaths a win",
                    clock(times[times.size() / 2]).c_str(), clock(times.front()).c_str(),
                    clock(times.back()).c_str(), winDeaths / double(wins));
    }
    std::printf("\ndeaths by cause:");
    for (const auto& [cause, n] : causes) std::printf(" %s %d", cause.c_str(), n);
    std::printf("\ndamage on the dragon by role:");
    for (const auto& [role, amount] : dealt) {
        std::printf(" %s %.0f%%", role.c_str(), all > 0 ? 100.0 * double(amount) / double(all) : 0.0);
    }
    std::printf("\n");
    return 0;
}
