#include "game/save.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "core/files.h"
#include "core/json.h"
#include "core/log.h"

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

}  // namespace

std::string defaultSavePath() {
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/Library/Application Support/MU2/hero.json";
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

    const core::Json& items = doc["items"];
    for (size_t i = 0; i < items.size(); ++i) {
        const core::Json& one = items.at(i);
        Saved::Item item;
        item.slot = int(one["slot"].numberOr(-1));
        item.group = int(one["group"].numberOr(-1));
        item.number = int(one["number"].numberOr(-1));
        item.plus = int(one["plus"].numberOr(0));
        item.durability = int(one["durability"].numberOr(0));
        item.skill = one["skill"].boolOr(false);
        saved.items.push_back(item);
    }
    const core::Json& quick = doc["quick"];
    for (size_t key = 0; key < 5 && key < quick.size(); ++key) {
        saved.quickGroup[key] = int(quick.at(key)["group"].numberOr(-1));
        saved.quickNumber[key] = int(quick.at(key)["number"].numberOr(-1));
    }
    core::logf("save: loaded %s -- level %d, %llu experience, at %d,%d in %s", path.c_str(),
               hero.level, static_cast<unsigned long long>(hero.experience), hero.column,
               hero.row, saved.world.c_str());
    out = saved;
    return true;
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
        held.durability = int16_t(item.durability);
        held.skill = item.skill;
    }
    for (int key = 0; key < 5; ++key) {
        saved.quick[key] = rowOf(tables, saved.quickGroup[key], saved.quickNumber[key]);
    }
    if (lost > 0) {
        core::logError("save: %d item(s) are not in the cooked tables and were left out", lost);
    }
}

bool writeSave(const std::string& path, const content::Tables& tables, const Saved& saved) {
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
    const std::string temporary = path + ".writing";
    std::FILE* f = std::fopen(temporary.c_str(), "wb");
    if (!f) {
        core::logError("save: cannot write %s", temporary.c_str());
        return false;
    }
    const sim::HeroRecord& hero = saved.hero;
    std::fprintf(f, "{\n  \"version\": %d,\n  \"world\": \"%s\",\n", kVersion,
                 saved.world.c_str());
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
    std::fprintf(f, "  \"items\": [");
    bool first = true;
    for (int slot = 0; slot < sim::kSlots; ++slot) {
        const sim::Held& held = hero.slots[slot];
        if (held.empty() || size_t(held.item) >= tables.items.size()) continue;
        std::fprintf(f, "%s\n    {\"slot\": %d, ", first ? "" : ",", slot);
        writeItem(f, tables, held.item);
        std::fprintf(f, ", \"plus\": %d, \"durability\": %d, \"skill\": %s}",
                     int(held.refinement), int(held.durability), held.skill ? "true" : "false");
        first = false;
    }
    std::fprintf(f, "%s],\n  \"quick\": [", first ? "" : "\n  ");
    for (int key = 0; key < 5; ++key) {
        const int32_t item = saved.quick[key];
        std::fprintf(f, "%s{", key ? ", " : "");
        if (item >= 0 && size_t(item) < tables.items.size()) writeItem(f, tables, item);
        std::fprintf(f, "}");
    }
    std::fprintf(f, "]\n}\n");
    const bool ok = std::fflush(f) == 0;
    std::fclose(f);
    if (!ok) {
        core::logError("save: writing %s failed", temporary.c_str());
        return false;
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        core::logError("save: cannot move %s into place: %s", temporary.c_str(),
                       error.message().c_str());
        return false;
    }
    return true;
}

}  // namespace mu::game
