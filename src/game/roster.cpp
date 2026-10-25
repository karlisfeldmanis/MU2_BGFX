#include "game/roster.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <random>

#include "core/args.h"
#include "core/log.h"
#include "game/figures.h"
#include "net/pass.h"
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
        seat.kin = sim::Kin(std::min<int>(one.kin, int(sim::kLastKin)));
        seat.second = one.second;
        seat.level = std::max(1, one.level);
        seat.rebirths = std::max(0, one.rebirths);
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
            item.harmony = h.harmony;
            item.harmonyLevel = h.harmonyLevel;
            item.worn = true;
            seat.items.push_back(item);
        }
        seats.push_back(std::move(seat));
    }
    std::sort(seats.begin(), seats.end(), [](const Seat& a, const Seat& b) { return a.slot < b.slot; });
    return seats;
}

namespace {

// The launcher's ticket and its secret, once read (accountKey), and the lines sealed with them.
std::string g_ticket, g_secret;
std::atomic<uint64_t> g_count{0};

}  // namespace

std::string accountKey() {
    // Started by the launcher, signed in: its ticket is the account (docs/plans/security.md).
    // The secret is never logged or sent: whoever had it could play him.
    if (const char* handed = std::getenv("MU2_TICKET")) {
        const std::string said = handed;
        const size_t colon = said.find(':');
        const std::string ticket = said.substr(0, colon);
        const std::string secret = colon == std::string::npos ? std::string() : said.substr(colon + 1);
        if (net::isTicket(ticket) && net::isSecret(secret)) {
            if (g_ticket != ticket) {
                core::logf("account: the launcher's sign-in");
                if (const char* counted = std::getenv("MU2_PASS_COUNT")) {
                    g_count = std::strtoull(counted, nullptr, 10);
                }
            }
            g_ticket = ticket;
            g_secret = secret;
            return ticket;
        }
        core::logError("account: MU2_TICKET is not a launcher's ticket; the machine's key instead");
    }
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

uint64_t passCount() { return g_count; }

namespace {
std::string g_playing;
}

void notePlaying(const std::string& name) { g_playing = name; }
const std::string& playing() { return g_playing; }

std::string sealedKey(const std::string& key) {
    if (g_ticket.empty() || key != g_ticket) return key;
    return net::sealPass(g_ticket, g_secret, ++g_count);
}

std::vector<net::Claim> claimsFor(const std::string& server) {
    std::vector<net::Claim> claims;
    const fs::path folder = fs::path(core::userFolder()) / "characters";
    std::error_code error;
    if (!fs::is_directory(folder, error)) return claims;
    // increment(error) and is_regular_file(error): the throwing forms ended the lobby on a
    // folder that failed part-way (an antivirus or a sync driver in the way).
    for (fs::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
        const fs::directory_entry& entry = *it;
        std::error_code kind;
        if (!entry.is_regular_file(kind) || entry.path().extension() != ".json" ||
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

std::string layoutBase(const std::string& name, const std::string& server) {
    // A folder a realm: names are the realm's own, so Helsinki's Magic and Nuremberg's Magic
    // are two characters, and one file between them put the one's skills on the other's bar.
    std::string realm = server;
    for (char& c : realm) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-') c = '_';
    }
    const fs::path folder = fs::path(core::userFolder()) / "layouts" / (realm.empty() ? "local" : realm);
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
                        : kin == sim::Kin::DarkLord       ? "DarkLordBare"
                                                          : "DarkKnightBare";
    // A world whose figure table was cooked before this class's body was is no reason to draw
    // him as nobody: the knight's bare body, the same rig. Dungeon, Atlans, Lost Tower, Tarkan
    // and Blood Castle were, and every Magic Gladiator in them stood invisible ('nothing cooked
    // called MagicGladiatorBare'; recooked 2026-10-11, pipeline/cook.py --only figures).
    if (figures && !figures->body(first)) first = "DarkKnightBare";
    // The Magic Gladiator and the Dark Lord have no second class, and so no second body.
    if (!second || !sim::hasSecondClass(kin)) return first;
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
        case sim::Kin::DarkLord: return "Dark Lord";
    }
    return "Dark Knight";
}

}  // namespace mu::game
