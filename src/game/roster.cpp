#include "game/roster.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <random>

#include "core/args.h"
#include "core/log.h"
#include "game/figures.h"
#include "sim/cradle.h"
#include "sim/quests.h"

namespace mu::game {
namespace {

namespace fs = std::filesystem;

}  // namespace

std::vector<Seat> seatsOf(const net::Roster& roster, const content::Tables& tables) {
    std::vector<Seat> seats;
    for (const net::Seat& one : roster.seats) {
        Seat seat;
        seat.name = one.name;
        seat.token = one.token;
        seat.world = one.world;
        seat.slot = one.slot;
        seat.kin = sim::Kin(std::min<int>(one.kin, int(sim::Kin::MagicGladiator)));
        seat.second = one.second;
        seat.level = std::max(1, one.level);
        for (const net::Seat::Worn& w : one.worn) {
            const sim::Held& h = w.held;
            if (h.empty() || size_t(h.item) >= tables.items.size()) continue;
            const content::ItemRow& row = tables.items[size_t(h.item)];
            Saved::Item item;
            item.slot = w.slot;
            item.group = row.group;
            item.number = row.number;
            item.plus = h.refinement;
            item.durability = h.durability;
            item.skill = h.skill;
            item.luck = h.luck;
            item.option = h.option;
            item.excellent = h.excellent;
            item.sockets = h.sockets;
            for (int i = 0; i < 3; ++i) {
                item.powers[i] = h.powers[i];
                item.affixes[i] = h.affixes[i];
            }
            item.wing = h.wing;
            item.worn = true;
            seat.items.push_back(item);
        }
        seats.push_back(std::move(seat));
    }
    std::sort(seats.begin(), seats.end(), [](const Seat& a, const Seat& b) { return a.slot < b.slot; });
    return seats;
}

std::string accountKey() {
    const fs::path path = fs::path(core::userFolder()) / "account.key";
    std::string key;
    if (std::FILE* f = std::fopen(path.string().c_str(), "rb")) {
        char line[net::kMostKey + 2] = {};
        if (std::fgets(line, sizeof line, f)) key = line;
        std::fclose(f);
        while (!key.empty() && (key.back() == '\n' || key.back() == '\r' || key.back() == ' ')) key.pop_back();
        if (key.size() >= 16) return key;
    }
    std::random_device entropy;
    char made[33] = {};
    for (int i = 0; i < 4; ++i) std::snprintf(made + i * 8, 9, "%08x", unsigned(entropy()));
    key = made;
    std::FILE* f = std::fopen(path.string().c_str(), "wb");
    if (!f || std::fprintf(f, "%s\n", key.c_str()) < 0 || std::fclose(f) != 0) {
        core::logError("account: cannot write %s; this run's account is its own", path.string().c_str());
    } else {
        core::logf("account: a new key in %s", path.string().c_str());
    }
    return key;
}

std::vector<net::Claim> claimsFor(const std::string& server) {
    std::vector<net::Claim> claims;
    const fs::path folder = fs::path(core::userFolder()) / "characters";
    std::error_code error;
    if (!fs::is_directory(folder, error)) return claims;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder, error)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json" ||
            entry.path().filename() == "vault.json") {
            continue;
        }
        const uint64_t token = loadServerToken(entry.path().string(), server);
        if (token == 0) continue;
        Saved saved;
        if (!loadSave(entry.path().string(), saved)) continue;
        claims.push_back({token, saved.name.empty() ? entry.path().stem().string() : saved.name, saved.slot});
        if (claims.size() >= size_t(kRosterSlots)) break;
    }
    return claims;
}

std::string layoutBase(const std::string& name) {
    const fs::path folder = fs::path(core::userFolder()) / "layouts";
    std::error_code error;
    fs::create_directories(folder, error);
    return (folder / (name + ".json")).string();
}

Refusal refusalOf(const std::string& name) {
    if (name.size() < size_t(sim::kNameFewest)) return Refusal::TooShort;
    if (!sim::goodName(name)) return Refusal::Symbols;
    return Refusal::None;
}

const char* cradleWeapon(sim::Kin kin) { return sim::cradleWeapon(kin); }

const char* bareBody(sim::Kin kin, bool second, const Figures* figures) {
    // index.json's spellings: the elf's first body has no suffix.
    const char* first = kin == sim::Kin::DarkWizard       ? "DarkWizardBare"
                        : kin == sim::Kin::FairyElf       ? "FairyElf"
                        : kin == sim::Kin::MagicGladiator ? "MagicGladiatorBare"
                                                          : "DarkKnightBare";
    // The Magic Gladiator has no second class, and so no second body.
    if (!second || kin == sim::Kin::MagicGladiator) return first;
    const char* promoted = kin == sim::Kin::DarkWizard ? "SoulMaster"
                           : kin == sim::Kin::FairyElf ? "MuseElf"
                                                       : "BladeKnight";
    return figures && !figures->body(promoted) ? first : promoted;
}

const char* className(sim::Kin kin) {
    switch (kin) {
        case sim::Kin::DarkWizard: return "Dark Wizard";
        case sim::Kin::FairyElf: return "Fairy Elf";
        case sim::Kin::DarkKnight: return "Dark Knight";
        case sim::Kin::MagicGladiator: return "Magic Gladiator";
    }
    return "Dark Knight";
}

}  // namespace mu::game
