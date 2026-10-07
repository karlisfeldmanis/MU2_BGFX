#include "game/roster.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/args.h"
#include "core/log.h"
#include "game/figures.h"
#include "sim/quests.h"

namespace mu::game {
namespace {

namespace fs = std::filesystem;

std::string lowered(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// The old single save, taken in as slot 0 the first time the roster is read. Copied as text
// with the name and slot put in after the opening brace, so nothing in it passes through a
// reader and a writer that might not agree on a field.
void adoptOldHero(const fs::path& folder) {
    const fs::path old = folder.parent_path() / "hero.json";
    std::error_code error;
    if (!fs::exists(old, error)) return;
    Saved saved;
    if (!loadSave(old.string(), saved)) return;
    std::ifstream in(old);
    std::stringstream text;
    text << in.rdbuf();
    std::string body = text.str();
    const size_t brace = body.find('{');
    if (brace == std::string::npos) return;
    // Named for his class: he was made before characters had names. "DarkKnight" is ten
    // letters, which is exactly what the rule allows.
    const std::string name = saved.hero.kin == sim::Kin::DarkWizard       ? "DarkWizard"
                             : saved.hero.kin == sim::Kin::FairyElf       ? "FairyElf"
                             : saved.hero.kin == sim::Kin::MagicGladiator ? "Gladiator"
                                                                          : "DarkKnight";
    body.insert(brace + 1, "\n  \"name\": \"" + name + "\",\n  \"slot\": 0,");
    fs::create_directories(folder, error);
    const fs::path to = folder / (name + ".json");
    std::ofstream out(to);
    out << body;
    if (!out) {
        core::logError("roster: could not take %s in as %s", old.string().c_str(),
                       to.string().c_str());
        return;
    }
    core::logf("roster: took the old hero.json in as %s in slot 0 (the old file is kept)",
               name.c_str());
}

}  // namespace

std::string rosterFolder() {
    return core::userFolder() + "/characters";
}

std::vector<Seat> readRoster(const std::string& folderPath) {
    const fs::path folder(folderPath);
    std::error_code error;
    const auto saves = [&]() {
        std::vector<fs::path> found;
        if (!fs::is_directory(folder, error)) return found;
        for (const fs::directory_entry& entry : fs::directory_iterator(folder, error)) {
            // The vault's own file is no character: it lives beside the account's folder, but
            // a --roster folder of any other name has it written in among the characters
            // (game/save.h's vaultPathBeside), where it was read as one called "vault".
            if (entry.is_regular_file() && entry.path().extension() == ".json" &&
                entry.path().filename() != "vault.json") {
                found.push_back(entry.path());
            }
        }
        std::sort(found.begin(), found.end());
        return found;
    };
    // Taken in only while there is no folder yet: once there is one, an empty roster is one
    // whose characters were all deleted, and the old hero coming back was a fifth undead.
    const bool first = !fs::exists(folder, error);
    std::vector<fs::path> files = saves();
    if (files.empty() && first) {
        adoptOldHero(folder);
        files = saves();
    }

    std::vector<Seat> roster;
    bool taken[kRosterSlots] = {};
    std::vector<Seat> unplaced;
    for (const fs::path& path : files) {
        Saved saved;
        if (!loadSave(path.string(), saved)) continue;
        Seat one;
        one.name = saved.name.empty() ? path.stem().string() : saved.name;
        one.path = path.string();
        one.world = saved.world;
        one.slot = saved.slot;
        one.kin = saved.hero.kin;
        one.second = sim::promoted(saved.hero.quests, int(saved.hero.kin));
        one.level = std::max(1, saved.hero.level);
        one.fresh = saved.fresh;
        one.items = std::move(saved.items);
        if (one.slot >= 0 && one.slot < kRosterSlots && !taken[one.slot]) {
            taken[one.slot] = true;
            roster.push_back(std::move(one));
        } else {
            unplaced.push_back(std::move(one));
        }
    }
    // A file with no slot, or one another file already holds, goes in the first free one.
    // More than five and the rest are not shown, and the log says who.
    for (Seat& one : unplaced) {
        int free = -1;
        for (int s = 0; s < kRosterSlots && free < 0; ++s) {
            if (!taken[s]) free = s;
        }
        if (free < 0) {
            core::logError("roster: %s has no pedestal left and is not shown", one.name.c_str());
            continue;
        }
        taken[free] = true;
        one.slot = free;
        roster.push_back(std::move(one));
    }
    std::sort(roster.begin(), roster.end(),
              [](const Seat& a, const Seat& b) { return a.slot < b.slot; });
    core::logf("roster: %zu character(s) in %s", roster.size(), folderPath.c_str());
    return roster;
}

Refusal refusalOf(const std::string& folder, const std::vector<Seat>& roster,
                  const std::string& name) {
    if (int(roster.size()) >= kRosterSlots) return Refusal::NoRoom;
    if (name.size() < 4) return Refusal::TooShort;
    if (name.size() > size_t(kNameLetters)) return Refusal::Symbols;
    for (const char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c))) return Refusal::Symbols;
    }
    const std::string low = lowered(name);
    for (const Seat& one : roster) {
        if (lowered(one.name) == low) return Refusal::Taken;
    }
    // A file of that name in the folder that the roster did not show -- a sixth character, or
    // one that did not read -- is taken all the same, or the new one would write over it.
    std::error_code error;
    if (fs::exists(fs::path(folder) / (name + ".json"), error)) return Refusal::Taken;
    return Refusal::None;
}

bool makeCharacter(const std::string& folderPath, const std::vector<Seat>& roster,
                   const std::string& name, sim::Kin kin) {
    if (refusalOf(folderPath, roster, name) != Refusal::None) return false;
    bool taken[kRosterSlots] = {};
    for (const Seat& one : roster) {
        if (one.slot >= 0 && one.slot < kRosterSlots) taken[one.slot] = true;
    }
    int slot = -1;
    for (int s = 0; s < kRosterSlots && slot < 0; ++s) {
        if (!taken[s]) slot = s;
    }
    if (slot < 0) return false;
    const fs::path folder(folderPath);
    std::error_code error;
    fs::create_directories(folder, error);
    const fs::path path = folder / (name + ".json");
    // Where the class is born: the Fairy Elf in Noria, the elves' town, and the knight and the
    // wizard in Lorencia, as in MU and as MU2's server started them (the user, 2026-09-28:
    // "created elf, spawned at lorencia not noria"). She comes in on Noria's spawn gate
    // (game/world/maps.h). The source for MU's own table is not on this machine to cite.
    const char* home = kin == sim::Kin::FairyElf ? "noria" : "lorencia";
    std::FILE* f = std::fopen(path.string().c_str(), "wb");
    if (!f) {
        core::logError("roster: cannot write %s", path.string().c_str());
        return false;
    }
    std::fprintf(f,
                 "{\n  \"version\": 1,\n  \"name\": \"%s\",\n  \"slot\": %d,\n  \"fresh\": true,\n"
                 "  \"world\": \"%s\",\n  \"class\": %d,\n  \"level\": 1\n}\n",
                 name.c_str(), slot, home, int(kin));
    const bool ok = std::fclose(f) == 0;
    if (ok) {
        core::logf("roster: made %s, a %s, in slot %d, born in %s", name.c_str(), className(kin),
                   slot, home);
    }
    return ok;
}

bool dropCharacter(const std::string& folderPath, const Seat& who) {
    const fs::path bin = fs::path(folderPath) / "deleted";
    std::error_code error;
    fs::create_directories(bin, error);
    char stamp[32];
    const std::time_t now = std::time(nullptr);
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", std::localtime(&now));
    const fs::path to = bin / (who.name + "-" + stamp + ".json");
    fs::rename(who.path, to, error);
    if (error) {
        core::logError("roster: could not move %s aside: %s", who.path.c_str(),
                       error.message().c_str());
        return false;
    }
    core::logf("roster: deleted %s (kept as %s)", who.name.c_str(), to.string().c_str());
    return true;
}

const char* cradleWeapon(sim::Kin kin) {
    switch (kin) {
        case sim::Kin::DarkWizard: return "Staff01";  // the Skull Staff: ours, see roster.h
        case sim::Kin::FairyElf: return "Bow01";
        case sim::Kin::DarkKnight: return "Axe01";
        // Nothing yet: his starting kit is the port's own step (docs/mg-port.md).
        case sim::Kin::MagicGladiator: return "";
    }
    return "";
}

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
