// armory_dump --data <dir of .mupak> --out <dir> [--commit C] [--protocol P] [--check-odds [kills]]
// armory_dump --assets <out/assets> --out <dir> ...      (a local look, the loose cooked files)
//
// Mounts the release's packs as the game does (core::mountPacks; mupack --check), reads every
// world's cooked tables through core::readFile, and writes the Armory's C++ half: cards.json,
// skills.json, talents.json, gear_powers.json, drops.json and rules.json. docs/plans/armory.md.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include "core/files.h"
#include "core/log.h"
#include "core/pack.h"
#include "dump.h"
#include "sim/elite.h"
#include "sim/items.h"
#include "sim/talents.h"

using namespace armory;
namespace fs = std::filesystem;

namespace {

int usage() {
    std::fprintf(stderr,
                 "armory_dump (--data DIR | --assets DIR) --out DIR [--commit C] [--protocol P] "
                 "[--check-odds [KILLS]] [--only cards,drops,...] [--verbose]\n");
    return 2;
}

// The worlds the packs hold: one world-<name>.mupak each, and its cooked tables inside.
std::vector<std::string> worldsIn(const std::string& data, bool packs) {
    std::vector<std::string> names;
    std::error_code error;
    if (packs) {
        for (fs::directory_iterator it(data, error), end; !error && it != end; it.increment(error)) {
            const std::string file = it->path().filename().string();
            if (file.rfind("world-", 0) == 0 && it->path().extension() == ".mupak") {
                names.push_back(file.substr(6, file.size() - 6 - 6));
            }
        }
    } else {
        for (fs::directory_iterator it(data + "/cooked", error), end; !error && it != end; it.increment(error)) {
            const std::string name = it->path().filename().string();
            if (fs::exists(it->path() / (name + ".mur"))) names.push_back(name);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

const J* member(const J& o, const std::string& key) { return o.find(key); }

void spotChecks(const Dump& d, const J& cards, const J& drops) {
    const content::Tables& t = d.items();
    std::printf("spot checks (the code's numbers; the brief's in brackets):\n");
    // Sword of Destruction +11 excellent.
    int32_t sword = -1;
    for (size_t i = 0; i < t.items.size(); ++i) {
        if (t.items[i].label == "Sword of Destruction") sword = int32_t(i);
    }
    if (sword >= 0) {
        const J* card = member(*member(cards, "items"), t.items[size_t(sword)].name);
        for (const auto& [state, one] : card->members()) {
            if (state.rfind("+11 excellent", 0) != 0) continue;
            const J* sheet = one.find("sheet");
            const J* hero = sheet->find("hero");
            const J* needs = one.find("needs");
            std::printf("  Sword of Destruction %s: %s damage, Strength %d, Agility %d, sells for %s  "
                        "[148 ~ 156, 692, 272, 604,100]\n",
                        state.c_str(), hero ? hero->find("value")->str().c_str() : "?",
                        int(needs->find("strength")->real()), int(needs->find("agility")->real()),
                        sheet->find("sell") ? sheet->find("sell")->str().c_str() : "?");
        }
    } else {
        std::printf("  no Sword of Destruction in the tables\n");
    }
    // Every (monster, place) entry, and an item's chance in it.
    const auto chanceIn = [&](const J& place, int32_t item) -> double {
        for (const J& it : place.find("items")->items()) {
            if (it.find("item")->str() == t.items[size_t(item)].name) return it.find("chance")->real();
        }
        return 0.0;
    };
    const J* monsters = member(drops, "monsters");
    if (sword >= 0 && monsters) {
        for (const J& m : monsters->items()) {
            for (const J& pl : m.find("places")->items()) {
                if (pl.find("place")->str() != "bloodcastle-4" || pl.find("name")->str() != "Red Skeleton Knight") continue;
                const double p = chanceIn(pl, sword);
                std::printf("  Sword of Destruction from a Blood Castle 4 Red Skeleton Knight (level %d): 1 in %.0f  [263]\n",
                            int(pl.find("level")->real()), p > 0 ? 1.0 / p : 0.0);
            }
        }
    }
    // Loch's Feather in Tarkan.
    int32_t feather = -1;
    for (size_t i = 0; i < t.items.size() && feather < 0; ++i) {
        if (sim::lochsFeather(t.items[i])) feather = int32_t(i);
    }
    if (feather >= 0 && monsters) {
        double low = 1e9, high = 0;
        int from = 0;
        for (const J& m : monsters->items()) {
            for (const J& pl : m.find("places")->items()) {
                if (pl.find("place")->str() != "tarkan" || pl.find("elite")->boolean()) continue;
                const double p = chanceIn(pl, feather);
                if (p <= 0) continue;
                low = std::min(low, 1.0 / p);
                high = std::max(high, 1.0 / p);
                ++from;
            }
        }
        std::printf("  Loch's Feather in Tarkan: 1 in %.0f to 1 in %.0f per kill, from %d of its monsters  [520]\n",
                    low, high, from);
    }
    // Talents.
    for (sim::Kin k : kKins) {
        std::printf("  talents at level 400, %s: %d, +%d per rebirth  [79/+80, MG 111/+112]\n", kinName(k),
                    sim::talentPoints(k, 400, 0), sim::talentPoints(k, 400, 1) - sim::talentPoints(k, 400, 0));
    }
    std::printf("  Lorencia Elite: x%g life, x%g damage  [x16, x2.5]\n", double(sim::eliteLifeOn(0)),
                double(sim::kEliteDamage));
}

}  // namespace

int main(int argc, char** argv) {
    std::string data, assets, out, only;
    Dump d;
    bool checking = false;
    int kills = 4000000;
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--data") data = next();
        else if (a == "--assets") assets = next();
        else if (a == "--out") out = next();
        else if (a == "--commit") d.commit = next();
        else if (a == "--protocol") d.protocol = next();
        else if (a == "--only") only = next();
        else if (a == "--verbose") verbose = true;
        else if (a == "--check-odds") {
            checking = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') kills = std::max(1000, std::atoi(argv[++i]));
        } else return usage();
    }
    if ((data.empty() == assets.empty()) || out.empty()) return usage();
    std::string root;
    if (!data.empty()) {
        if (core::mountPacks(data) == 0) {
            std::fprintf(stderr, "armory_dump: no packs mount in %s\n", data.c_str());
            return 1;
        }
        root = data + "/assets";
        d.packs = true;
    } else {
        root = assets;
    }
    for (const std::string& name : worldsIn(data.empty() ? assets : data, !data.empty())) {
        const std::string path = root + "/cooked/" + name + "/" + name + ".mur";
        if (!core::fileExists(path)) continue;
        World w;
        w.name = name;
        w.tables = std::make_unique<content::Tables>();
        std::string error;
        if (!content::loadTables(path, *w.tables, error)) {
            std::fprintf(stderr, "armory_dump: %s: %s\n", name.c_str(), error.c_str());
            return 1;
        }
        d.worlds.push_back(std::move(w));
    }
    if (d.worlds.empty() || !d.world("lorencia")) {
        std::fprintf(stderr, "armory_dump: no worlds' tables read from %s\n", root.c_str());
        return 1;
    }
    std::printf("armory_dump: %zu worlds from %s:", d.worlds.size(), data.empty() ? "the loose cooked files" : "the packs");
    for (const World& w : d.worlds) std::printf(" %s", w.name.c_str());
    std::printf("\n");
    // The realm's own log lines (an Elite raised, what it leaves) are the check's noise.
    if (!verbose) core::logSilence(true);
    fs::create_directories(out);
    const auto wanted = [&](const char* name) { return only.empty() || ("," + only + ",").find("," + std::string(name) + ",") != std::string::npos; };
    J cards, drops;
    struct File {
        const char* name;
        J (*make)(const Dump&);
        J* keep;
    };
    const File files[] = {{"cards", dumpCards, &cards},         {"skills", dumpSkills, nullptr},
                          {"talents", dumpTalents, nullptr},    {"gear_powers", dumpGearPowers, nullptr},
                          {"drops", dumpDrops, &drops},         {"rules", dumpRules, nullptr}};
    for (const File& f : files) {
        if (!wanted(f.name) && !(f.keep && checking)) continue;
        J tree = f.make(d);
        if (wanted(f.name)) {
            const std::string path = out + "/" + f.name + ".json";
            if (!tree.save(path)) {
                std::fprintf(stderr, "armory_dump: cannot write %s\n", path.c_str());
                return 1;
            }
            std::printf("  wrote %s\n", path.c_str());
        }
        if (f.keep) *f.keep = std::move(tree);
    }
    if (!cards.null() && !drops.null()) spotChecks(d, cards, drops);
    if (checking) return checkOdds(d, kills, verbose);
    return 0;
}
