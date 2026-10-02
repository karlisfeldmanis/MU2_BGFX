#include "game/save.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "core/args.h"
#include "core/files.h"
#include "core/json.h"
#include "core/log.h"
#include "sim/skills.h"
#include "sim/wear.h"

namespace mu::game {
namespace {

// Bumped when a field changes meaning. A file of another version is not read at all -- a
// character half-understood is worse than a character started again, and the log says why.
constexpr int kVersion = 1;

void writeItem(std::FILE* f, const content::Tables& tables, int32_t item) {
    const content::ItemRow& row = tables.items[size_t(item)];
    std::fprintf(f, "\"group\": %d, \"number\": %d", row.group, row.number);
}

int32_t rowOf(const content::Tables& tables, int group, int number) {
    if (group < 0 || number < 0) return -1;
    return tables.itemAt(group, number);
}

// What a saved thing has left: the file's own count, except on gear saved before wear was
// recorded, which comes back whole (Saved::Item::worn).
int16_t durabilityOf(const content::Tables& tables, int32_t row, const Saved::Item& item) {
    const content::ItemRow& r = tables.items[size_t(row)];
    if (!item.worn && sim::wears(r)) {
        sim::Held held{row, int16_t(item.plus), 0};
        held.excellent = uint8_t(item.excellent);
        return int16_t(sim::maximumDurability(r, held));
    }
    return int16_t(item.durability);
}

Saved::Item readItem(const core::Json& one) {
    Saved::Item item;
    item.slot = int(one["slot"].numberOr(-1));
    item.group = int(one["group"].numberOr(-1));
    item.number = int(one["number"].numberOr(-1));
    item.plus = int(one["plus"].numberOr(0));
    item.durability = int(one["durability"].numberOr(0));
    item.skill = one["skill"].boolOr(false);
    item.luck = one["luck"].boolOr(false);
    item.option = std::clamp(int(one["option"].numberOr(0)), 0, sim::kMostOption);
    item.excellent = int(one["excellent"].numberOr(0)) & 63;
    item.sockets = std::clamp(int(one["sockets"].numberOr(0)), 0, sim::kMostSockets);
    const core::Json& powers = one["powers"];
    for (size_t i = 0; i < 3 && i < powers.size(); ++i) {
        item.powers[i] = std::clamp(int(powers.at(i).numberOr(0)), 0, 255);
    }
    item.worn = one["wear"].boolOr(false);
    return item;
}

// One carried or kept thing, as the items arrays write it. `first` says whether a comma goes in
// front, and is cleared.
void writeHeld(std::FILE* f, const content::Tables& tables, int slot, const sim::Held& held,
               bool* first) {
    std::fprintf(f, "%s\n    {\"slot\": %d, ", *first ? "" : ",", slot);
    writeItem(f, tables, held.item);
    std::fprintf(f,
                 ", \"plus\": %d, \"durability\": %d, \"wear\": true, \"skill\": %s, "
                 "\"luck\": %s, \"option\": %d, \"excellent\": %d, \"sockets\": %d, "
                 "\"powers\": [%d, %d, %d]}",
                 int(held.refinement), int(held.durability), held.skill ? "true" : "false",
                 held.luck ? "true" : "false", int(held.option), int(held.excellent),
                 int(held.sockets), int(held.powers[0]), int(held.powers[1]),
                 int(held.powers[2]));
    *first = false;
}

std::FILE* begin(const std::string& path, std::string* temporary) {
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
    *temporary = path + ".writing";
    std::FILE* f = std::fopen(temporary->c_str(), "wb");
    if (!f) core::logError("save: cannot write %s", temporary->c_str());
    return f;
}

// Flushed, closed and renamed over the last good file: a crash mid-write keeps that one.
bool finish(std::FILE* f, const std::string& temporary, const std::string& path) {
    const bool ok = std::fflush(f) == 0;
    std::fclose(f);
    if (!ok) {
        core::logError("save: writing %s failed", temporary.c_str());
        return false;
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        core::logError("save: cannot move %s into place: %s", temporary.c_str(),
                       error.message().c_str());
        return false;
    }
    return true;
}

}  // namespace

std::string defaultSavePath() {
    return core::userFolder() + "/hero.json";
}

bool loadSave(const std::string& path, Saved& out) {
    if (!core::fileExists(path)) return false;
    const core::Json doc = core::parseJsonFile(path);
    if (doc.isNull()) {
        core::logError("save: %s is not readable as a save; starting new", path.c_str());
        return false;
    }
    const int version = int(doc["version"].numberOr(0));
    if (version != kVersion) {
        core::logError("save: %s is version %d and this reads %d; starting new", path.c_str(),
                       version, kVersion);
        return false;
    }
    Saved saved;
    saved.name = doc["name"].stringOr("");
    saved.slot = int(doc["slot"].numberOr(-1));
    saved.fresh = doc["fresh"].boolOr(false);
    saved.world = doc["world"].stringOr("");
    sim::HeroRecord& hero = saved.hero;
    hero.kin = sim::Kin(int(doc["class"].numberOr(2)));
    hero.column = int(doc["column"].numberOr(0));
    hero.row = int(doc["row"].numberOr(0));
    hero.facing = float(doc["facing"].numberOr(0.0));
    hero.level = int(doc["level"].numberOr(1));
    hero.experience = uint64_t(doc["experience"].numberOr(0.0));
    hero.pointsInHand = int(doc["points_in_hand"].numberOr(0));
    hero.points.strength = int(doc["strength"].numberOr(0));
    hero.points.agility = int(doc["agility"].numberOr(0));
    hero.points.vitality = int(doc["vitality"].numberOr(0));
    hero.points.energy = int(doc["energy"].numberOr(0));
    hero.health = int(doc["health"].numberOr(0));
    hero.mana = int(doc["mana"].numberOr(0));
    hero.money = int64_t(doc["zen"].numberOr(0.0));
    // Absent in a file written before there were skills, which reads as nought and is right.
    hero.learned = uint32_t(doc["learned"].numberOr(0.0));
    // The travel rows he has opened (sim/travel.h), absent in a file written before the list,
    // which reads as nought: the realm opens his birth town over it (Realm::settleFound).
    hero.found = uint32_t(doc["found"].numberOr(0.0));
    // The buff standing on him, absent when none was: skill, damage factor and ticks left.
    // The realm checks all three on the way back in (Realm::restore).
    const core::Json& boon = doc["boon"];
    hero.boonSkill = int32_t(boon["skill"].numberOr(0.0));
    hero.boonDamageTaken = float(boon["damage_taken"].numberOr(1.0));
    hero.boonTicksLeft = int64_t(boon["ticks_left"].numberOr(0.0));
    // An Ale's ticks left, absent when none stood. The realm caps it at one Ale's length.
    hero.aleTicksLeft = int64_t(doc["ale_ticks_left"].numberOr(0.0));
    // The cooldowns running, as [skill number, ticks left] pairs, absent when none was. By MU's
    // number and not the table's index, as the bar is, so a row appended later lands on its own.
    const core::Json& cooling = doc["cooling"];
    for (size_t i = 0; i < cooling.size(); ++i) {
        const int index = sim::skillIndexOf(int32_t(cooling.at(i).at(0).numberOr(0.0)));
        if (index >= 0) hero.coolsLeft[index] = int64_t(cooling.at(i).at(1).numberOr(0.0));
    }

    // The quests, by the table's index: [state, [counts...], available at, completions].
    // Absent in a file written before there were quests, which reads as none taken.
    const core::Json& quests = doc["quests"];
    for (size_t i = 0; i < quests.size() && i < size_t(sim::kQuests); ++i) {
        const core::Json& one = quests.at(i);
        sim::QuestProgress& into = hero.quests[i];
        into.state = sim::QuestState(int(one.at(0).numberOr(0.0)));
        const core::Json& counts = one.at(1);
        for (size_t step = 0; step < counts.size() && step < size_t(sim::kQuestSteps); ++step) {
            into.counts[step] = uint16_t(counts.at(step).numberOr(0.0));
        }
        into.availableAt = int64_t(one.at(2).numberOr(0.0));
        into.completions = uint32_t(one.at(3).numberOr(0.0));
    }

    const core::Json& items = doc["items"];
    for (size_t i = 0; i < items.size(); ++i) saved.items.push_back(readItem(items.at(i)));
    // Absent in a file written before the bar could be arranged, which reads as four empty keys
    // and lets the first-free-key convenience fill them -- the behaviour that file was saved
    // under. No version bump for that reason.
    const core::Json& bar = doc["bar"];
    for (size_t key = 0; key < 6 && key < bar.size(); ++key) {
        saved.bar[key] = int32_t(bar.at(key).numberOr(0.0));
    }
    const core::Json& quick = doc["quick"];
    for (size_t key = 0; key < 5 && key < quick.size(); ++key) {
        saved.quickGroup[key] = int(quick.at(key)["group"].numberOr(-1));
        saved.quickNumber[key] = int(quick.at(key)["number"].numberOr(-1));
    }
    core::logf("save: loaded %s -- level %d, %llu experience, at %d,%d in %s", path.c_str(),
               hero.level, static_cast<unsigned long long>(hero.experience), hero.column,
               hero.row, saved.world.c_str());
    // The vault is read on its own and is not the character's, so what is already there stays.
    saved.vaultZen = out.vaultZen;
    saved.vaultItems = std::move(out.vaultItems);
    out = std::move(saved);
    return true;
}

// A ring saved with more sockets than a ring holds now (sim::mostSocketsOf) loses the rest and
// what was set in them. A Rune of Creation's own power, in the first with no socket, stays.
static void capSockets(const content::ItemRow& row, sim::Held& held) {
    const int most = sim::mostSocketsOf(row);
    if (held.sockets <= most) return;
    held.sockets = uint8_t(most);
    for (int i = most; i < 3; ++i) held.powers[i] = 0;
}

void resolveSave(const content::Tables& tables, Saved& saved) {
    int lost = 0;
    for (const Saved::Item& item : saved.items) {
        const int32_t row = rowOf(tables, item.group, item.number);
        if (item.slot < 0 || item.slot >= sim::kSlots || row < 0) {
            ++lost;
            continue;
        }
        sim::Held& held = saved.hero.slots[item.slot];
        held.item = row;
        held.refinement = int16_t(item.plus);
        held.durability = durabilityOf(tables, row, item);
        held.skill = item.skill;
        held.luck = item.luck;
        held.option = int8_t(item.option);
        held.excellent = uint8_t(item.excellent);
        held.sockets = uint8_t(item.sockets);
        for (int i = 0; i < 3; ++i) held.powers[i] = uint8_t(item.powers[i]);
        capSockets(tables.items[size_t(row)], held);
    }
    for (int key = 0; key < 5; ++key) {
        saved.quick[key] = rowOf(tables, saved.quickGroup[key], saved.quickNumber[key]);
    }
    if (lost > 0) {
        core::logError("save: %d item(s) are not in the cooked tables and were left out", lost);
    }
}

bool writeSave(const std::string& path, const content::Tables& tables, const Saved& saved) {
    std::string temporary;
    std::FILE* f = begin(path, &temporary);
    if (!f) return false;
    const sim::HeroRecord& hero = saved.hero;
    std::fprintf(f, "{\n  \"version\": %d,\n", kVersion);
    // The name is the roster's rule, letters and digits only, so it needs no escaping.
    if (!saved.name.empty()) std::fprintf(f, "  \"name\": \"%s\",\n", saved.name.c_str());
    if (saved.slot >= 0) std::fprintf(f, "  \"slot\": %d,\n", saved.slot);
    std::fprintf(f, "  \"world\": \"%s\",\n", saved.world.c_str());
    std::fprintf(f, "  \"class\": %d,\n  \"column\": %d,\n  \"row\": %d,\n  \"facing\": %.4f,\n",
                 int(hero.kin), hero.column, hero.row, double(hero.facing));
    std::fprintf(f, "  \"level\": %d,\n  \"experience\": %llu,\n  \"points_in_hand\": %d,\n",
                 hero.level, static_cast<unsigned long long>(hero.experience),
                 hero.pointsInHand);
    std::fprintf(f, "  \"strength\": %d,\n  \"agility\": %d,\n  \"vitality\": %d,\n"
                    "  \"energy\": %d,\n",
                 hero.points.strength, hero.points.agility, hero.points.vitality,
                 hero.points.energy);
    std::fprintf(f, "  \"health\": %d,\n  \"mana\": %d,\n  \"zen\": %lld,\n", hero.health,
                 hero.mana, static_cast<long long>(hero.money));
    if (hero.learned != 0) std::fprintf(f, "  \"learned\": %u,\n", hero.learned);
    if (hero.found != 0) std::fprintf(f, "  \"found\": %u,\n", hero.found);
    if (hero.boonSkill != 0 && hero.boonTicksLeft > 0) {
        std::fprintf(f,
                     "  \"boon\": {\"skill\": %d, \"damage_taken\": %.4f, \"ticks_left\": %lld},\n",
                     hero.boonSkill, double(hero.boonDamageTaken),
                     static_cast<long long>(hero.boonTicksLeft));
    }
    if (hero.aleTicksLeft > 0) {
        std::fprintf(f, "  \"ale_ticks_left\": %lld,\n",
                     static_cast<long long>(hero.aleTicksLeft));
    }
    std::fprintf(f, "  \"quests\": [");
    for (int i = 0; i < sim::kQuests; ++i) {
        const sim::QuestProgress& one = hero.quests[i];
        std::fprintf(f, "%s[%d, [", i ? ", " : "", int(one.state));
        for (int step = 0; step < sim::kQuestSteps; ++step) {
            std::fprintf(f, "%s%d", step ? ", " : "", int(one.counts[step]));
        }
        std::fprintf(f, "], %lld, %u]", static_cast<long long>(one.availableAt), one.completions);
    }
    std::fprintf(f, "],\n");
    bool cooling = false;
    for (int i = 0; i < sim::kSkills; ++i) {
        if (hero.coolsLeft[i] <= 0) continue;
        std::fprintf(f, "%s[%d, %lld]", cooling ? ", " : "  \"cooling\": [",
                     sim::skillAt(i).number, static_cast<long long>(hero.coolsLeft[i]));
        cooling = true;
    }
    if (cooling) std::fprintf(f, "],\n");
    std::fprintf(f, "  \"items\": [");
    bool first = true;
    for (int slot = 0; slot < sim::kSlots; ++slot) {
        const sim::Held& held = hero.slots[slot];
        if (held.empty() || size_t(held.item) >= tables.items.size()) continue;
        writeHeld(f, tables, slot, held, &first);
    }
    std::fprintf(f, "%s],\n  \"quick\": [", first ? "" : "\n  ");
    for (int key = 0; key < 5; ++key) {
        const int32_t item = saved.quick[key];
        std::fprintf(f, "%s{", key ? ", " : "");
        if (item >= 0 && size_t(item) < tables.items.size()) writeItem(f, tables, item);
        std::fprintf(f, "}");
    }
    std::fprintf(f, "],\n  \"bar\": [");
    for (int key = 0; key < 6; ++key) {
        std::fprintf(f, "%s%d", key ? ", " : "", saved.bar[key]);
    }
    std::fprintf(f, "]\n}\n");
    return finish(f, temporary, path);
}

std::string vaultPathBeside(const std::string& savePath) {
    std::filesystem::path folder = std::filesystem::path(savePath).parent_path();
    // A roster character's save is one folder down from the account's; the vault is not.
    if (folder.filename() == "characters") folder = folder.parent_path();
    return (folder / "vault.json").string();
}

bool loadVault(const std::string& path, Saved& saved) {
    saved.vaultZen = 0;
    saved.vaultItems.clear();
    if (!core::fileExists(path)) return false;
    const core::Json doc = core::parseJsonFile(path);
    if (doc.isNull() || int(doc["version"].numberOr(0)) != kVersion) {
        core::logError("save: %s is not a vault this reads; the vault starts empty", path.c_str());
        return false;
    }
    saved.vaultZen = int64_t(doc["zen"].numberOr(0.0));
    const core::Json& items = doc["items"];
    for (size_t i = 0; i < items.size(); ++i) saved.vaultItems.push_back(readItem(items.at(i)));
    core::logf("save: vault %s -- %zu item(s), %lld Zen", path.c_str(), saved.vaultItems.size(),
               static_cast<long long>(saved.vaultZen));
    return true;
}

sim::Vault resolveVault(const content::Tables& tables, const Saved& saved) {
    sim::Vault vault;
    vault.setZen(std::max<int64_t>(0, saved.vaultZen));
    int lost = 0;
    for (const Saved::Item& item : saved.vaultItems) {
        const int32_t row = rowOf(tables, item.group, item.number);
        const content::ItemRow* r = row >= 0 ? &tables.items[size_t(row)] : nullptr;
        // Put back only where it still fits: a file edited by hand, or a row whose size changed
        // in a recook, must not lay one thing across another.
        if (!r || !vault.room(tables, item.slot, r->width, r->height)) {
            ++lost;
            continue;
        }
        sim::Held held{row, int16_t(item.plus), durabilityOf(tables, row, item),
                       item.skill, item.luck, int8_t(item.option), uint8_t(item.excellent)};
        held.sockets = uint8_t(item.sockets);
        for (int i = 0; i < 3; ++i) held.powers[i] = uint8_t(item.powers[i]);
        capSockets(*r, held);
        vault.put(item.slot, held);
    }
    if (lost > 0) core::logError("save: %d vault item(s) could not be put back", lost);
    return vault;
}

bool writeVault(const std::string& path, const content::Tables& tables, const sim::Vault& vault) {
    std::string temporary;
    std::FILE* f = begin(path, &temporary);
    if (!f) return false;
    std::fprintf(f, "{\n  \"version\": %d,\n  \"zen\": %lld,\n  \"items\": [", kVersion,
                 static_cast<long long>(vault.zen()));
    bool first = true;
    for (int cell = 0; cell < sim::kVaultCells; ++cell) {
        const sim::Held& held = vault[cell];
        if (held.empty() || size_t(held.item) >= tables.items.size()) continue;
        writeHeld(f, tables, cell, held, &first);
    }
    std::fprintf(f, "%s]\n}\n", first ? "" : "\n  ");
    return finish(f, temporary, path);
}

}  // namespace mu::game
