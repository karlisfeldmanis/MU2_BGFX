// The save, read and written back (sprint 16, step 3). A save names a quest, a travel row and a
// skill by what it is -- a quest's key, a row's key, MU's skill number -- and never by where it
// sits in a table, so a row inserted later moves nobody's progress. This holds that true:
//
//   1. tests/fixtures/save_v2.json, a character written in the version 2 shape (quests an array in
//      the table's order, `learned` and `found` bitmasks, `followed` an index), reads;
//   2. written back, it comes out as version 3 with keys, and reads to the same character;
//   3. the version 3 file with its quests in another order and a key no table has reads to the
//      same character again -- which is what inserting a row would do to an index.
//
// `cmake --build build --target save_test && build/save_test`; it runs in `checks`.

#include <cstdio>
#include <filesystem>
#include <string>

#include "content/tables.h"
#include "core/files.h"
#include "game/save.h"
#include "sim/quests.h"

namespace {

int g_failures = 0;

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::printf("  FAIL %s\n", what.c_str());
        ++g_failures;
    }
}

// The fields a save carries and a key could get wrong, compared whole.
bool same(const mu::game::Saved& a, const mu::game::Saved& b, std::string& why) {
    const mu::sim::HeroRecord& x = a.hero;
    const mu::sim::HeroRecord& y = b.hero;
    if (x.level != y.level || x.experience != y.experience || x.money != y.money) {
        why = "level, experience or zen";
        return false;
    }
    if (x.learned != y.learned) {
        why = "learned";
        return false;
    }
    if (x.found != y.found) {
        why = "found";
        return false;
    }
    for (int i = 0; i < mu::sim::kQuests; ++i) {
        const mu::sim::QuestProgress& p = x.quests[i];
        const mu::sim::QuestProgress& q = y.quests[i];
        bool counts = true;
        for (int s = 0; s < mu::sim::kQuestSteps; ++s) counts = counts && p.counts[s] == q.counts[s];
        if (p.state != q.state || !counts || p.availableAt != q.availableAt ||
            p.completions != q.completions) {
            why = std::string("quest ") + mu::sim::questAt(i).key;
            return false;
        }
    }
    if (a.followed != b.followed) {
        why = "followed";
        return false;
    }
    if (a.items.size() != b.items.size() || a.world != b.world || a.name != b.name) {
        why = "items, world or name";
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    using namespace mu;
    const std::string fixture = std::string(MU2_SOURCE_DIR) + "/tests/fixtures/save_v2.json";
    const fs::path scratch = fs::temp_directory_path() / "mu2_save_test";
    fs::create_directories(scratch);

    content::Tables tables;
    std::string error;
    check(content::loadTables(std::string(MU2_ASSET_DIR) + "/cooked/lorencia/lorencia.mur", tables,
                              error),
          "Lorencia's tables load: " + error);

    // 1. The version 2 file.
    game::Saved old;
    check(game::loadSave(fixture, old), "the version 2 fixture reads");
    game::resolveSave(tables, old);
    check(old.hero.level == 103 && old.name == "DKTest2", "and is DKTest2 at level 103");
    check(old.hero.learned == 137438953551ull, "its learned bits read as written");
    check(old.hero.found == 63u, "its travel rows read as written");
    check(old.followed == 3, "it follows the quest at index 3");
    check(old.hero.quests[0].completions == 1 && old.hero.quests[2].counts[0] == 40,
          "its quests read by their index");

    // 2. Written back: version 3, by keys, and the same character.
    const std::string v3 = (scratch / "v3.json").string();
    check(game::writeSave(v3, tables, old), "it writes");
    std::string text;
    {
        const std::vector<uint8_t> bytes = core::readFile(v3);
        check(!bytes.empty(), "and the file is there");
        text.assign(bytes.begin(), bytes.end());
    }
    check(text.find("\"version\": 3") != std::string::npos, "as version 3");
    check(text.find("\"quests\": {\"marlon\": [") != std::string::npos, "its quests by key");
    check(text.find("\"found\": [\"lorencia\", ") != std::string::npos, "its rows by key");
    check(text.find("\"followed\": \"catacombs\"") != std::string::npos, "its followed by key");
    check(text.find("\"learned\": [") != std::string::npos, "its skills by number");
    game::Saved again;
    check(game::loadSave(v3, again), "the version 3 file reads");
    game::resolveSave(tables, again);
    std::string why;
    check(same(old, again, why), "to the same character (" + why + ")");

    // 3. The quests reordered and a key no table has: an inserted row, as a file sees one.
    std::string moved = text;
    const size_t open = moved.find("\"quests\": {") + 11;
    const size_t close = moved.find("},", open);
    std::string body = moved.substr(open, close - open);
    // Each entry is `"key": [s, [c...], at, n]`; split on the "], \"" between entries.
    std::vector<std::string> entries;
    size_t at = 0;
    while (true) {
        const size_t next = body.find("], \"", at);
        if (next == std::string::npos) {
            entries.push_back(body.substr(at));
            break;
        }
        entries.push_back(body.substr(at, next + 1 - at));
        at = next + 3;
    }
    std::string reversed = "\"no_such_quest\": [3, [9, 9, 9, 9, 9, 9, 9, 9, 9], 0, 7]";
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) reversed += ", " + *it;
    moved.replace(open, close - open, reversed);
    const std::string shuffled = (scratch / "shuffled.json").string();
    if (std::FILE* f = std::fopen(shuffled.c_str(), "wb")) {
        std::fwrite(moved.data(), 1, moved.size(), f);
        std::fclose(f);
    }
    check(entries.size() == size_t(sim::kQuests), "every quest was written, one entry each");
    game::Saved third;
    check(game::loadSave(shuffled, third), "the reordered file reads");
    game::resolveSave(tables, third);
    check(same(old, third, why), "to the same character, whatever the order (" + why + ")");

    // 4. Any saves named on the command line, each read from a COPY, written as version 3 and
    // read back the same (`save_test saves/hero.json saves/characters/*.json`). The files named
    // are only ever read.
    for (int i = 1; i < argc; ++i) {
        const std::string copy = (scratch / "copy.json").string();
        fs::copy_file(argv[i], copy, fs::copy_options::overwrite_existing);
        game::Saved before, after;
        if (!game::loadSave(copy, before)) {
            check(false, std::string(argv[i]) + " reads");
            continue;
        }
        game::resolveSave(tables, before);
        const std::string out = (scratch / "out.json").string();
        check(game::writeSave(out, tables, before), std::string(argv[i]) + " writes");
        check(game::loadSave(out, after), std::string(argv[i]) + " reads back");
        game::resolveSave(tables, after);
        check(same(before, after, why), std::string(argv[i]) + " is the same character (" + why + ")");
        std::printf("  %s: level %d, %d quests begun, learned %llx, found %x, followed %d -- %s\n",
                    argv[i], before.hero.level,
                    [&] { int n = 0; for (const auto& q : before.hero.quests) n += q.state != sim::QuestState{}; return n; }(),
                    static_cast<unsigned long long>(before.hero.learned), before.hero.found,
                    before.followed, same(before, after, why) ? "same" : "DIFFERS");
    }

    fs::remove_all(scratch);
    std::printf("save_test: %d failures\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
