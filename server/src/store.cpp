#include "store.h"

#include <algorithm>
#include <chrono>
#include <ctime>

#include <sqlite3.h>
#ifdef __linux__
#include <sys/resource.h>
#endif

#include "core/log.h"
#include "net/wire.h"

namespace mu::server {

namespace {

// The shape a character's bytes are written in (net::putKept). Every shape since protocol 3's is
// still read: a row written before the way back was kept reads with none.
constexpr int kLayout = net::kKeptLayout;
constexpr int kOldestLayout = 3;

constexpr const char* kSchema =
    "CREATE TABLE IF NOT EXISTS characters ("
    " token INTEGER PRIMARY KEY,"  // the u64's bits, as SQLite's signed 64
    " layout INTEGER NOT NULL,"
    " kept BLOB NOT NULL,"
    " kin INTEGER NOT NULL,"
    " level INTEGER NOT NULL,"
    " money INTEGER NOT NULL,"
    " saved INTEGER NOT NULL,"  // unix seconds
    " world TEXT NOT NULL DEFAULT '',"
    " account TEXT NOT NULL DEFAULT '',"  // phase 6: whose, by the account's key
    " name TEXT NOT NULL DEFAULT '',"
    " slot INTEGER NOT NULL DEFAULT -1,"
    " deleted INTEGER NOT NULL DEFAULT 0)";  // unix seconds, 0 for not

// Columns a file from before them is given, each with its default.
constexpr const char* kAdded[][2] = {
    {"world", "ALTER TABLE characters ADD COLUMN world TEXT NOT NULL DEFAULT ''"},
    {"account", "ALTER TABLE characters ADD COLUMN account TEXT NOT NULL DEFAULT ''"},
    {"name", "ALTER TABLE characters ADD COLUMN name TEXT NOT NULL DEFAULT ''"},
    {"slot", "ALTER TABLE characters ADD COLUMN slot INTEGER NOT NULL DEFAULT -1"},
    {"deleted", "ALTER TABLE characters ADD COLUMN deleted INTEGER NOT NULL DEFAULT 0"},
    // The item list (item_tables) his things are numbered by, 0 for a row from before lists
    // were kept (Store::useItems).
    {"items", "ALTER TABLE characters ADD COLUMN items INTEGER NOT NULL DEFAULT 0"},
};

// **The list every row had before lists were kept**: this server's rows less the one added on
// 2026-10-16, the Pendant of Haste (a3d4dc48), which the server took at 19:20 UTC that day with
// nothing to renumber. Every item before it was added since the server last started fresh
// (2026-10-11) was there before any character was. Stored once, as list 1, the first time a
// file is opened by a server that keeps lists, so a later new item never moves it.
constexpr const char* kAddedOn20261016 = "NecklaceHaste";

// A list as item_tables keeps it: the rows' names in order, one a line.
std::string namesOf(const content::Tables& tables, const char* without = nullptr) {
    std::string names;
    for (const content::ItemRow& row : tables.items) {
        if (without != nullptr && row.name == without) continue;
        names += row.name;
        names += '\n';
    }
    return names;
}

// Old row to this server's, by name; -1 for an item this server no longer has.
std::vector<int32_t> renumbering(const std::string& names, const content::Tables& tables) {
    std::vector<int32_t> map;
    size_t at = 0;
    while (at < names.size()) {
        const size_t end = names.find('\n', at);
        map.push_back(tables.itemNamed(names.substr(at, end - at)));
        if (end == std::string::npos) break;
        at = end + 1;
    }
    return map;
}

bool identity(const std::vector<int32_t>& map, size_t rows) {
    if (map.size() != rows) return false;
    for (size_t i = 0; i < map.size(); ++i) {
        if (map[i] != int32_t(i)) return false;
    }
    return true;
}

sim::Held renumbered(const sim::Held& what, const std::vector<int32_t>& map) {
    if (what.empty()) return what;
    if (what.item >= int32_t(map.size()) || map[size_t(what.item)] < 0) return sim::Held{};
    sim::Held moved = what;
    moved.item = map[size_t(what.item)];
    return moved;
}

std::string text(sqlite3_stmt* s, int column) {
    const auto* t = reinterpret_cast<const char*>(sqlite3_column_text(s, column));
    return t ? t : "";
}

// One statement, finalized on every way out.
struct Statement {
    sqlite3_stmt* s = nullptr;
    Statement(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK) {
            core::logError("characters.db: %s", sqlite3_errmsg(db));
            s = nullptr;
        }
    }
    ~Statement() { sqlite3_finalize(s); }
    explicit operator bool() const { return s != nullptr; }
};

bool exec(sqlite3* db, const char* sql) {
    char* error = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        core::logError("characters.db: %s: %s", sql, error ? error : "?");
        sqlite3_free(error);
        return false;
    }
    return true;
}

}  // namespace

// How often the journal is folded back: each pass copies only what was written since the last.
constexpr auto kCheckpointEvery = std::chrono::seconds(10);

Store::~Store() {
    {
        std::lock_guard<std::mutex> hold(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    if (checkpointer_.joinable()) checkpointer_.join();
    sqlite3_close(db_);  // the last connection: SQLite folds the journal back as it closes
}

void Store::checkpoints(std::string path) {
#ifdef __linux__
    setpriority(PRIO_PROCESS, 0, 10);  // this thread alone, on Linux: the ticks come first
#endif
    sqlite3* db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        core::logError("characters.db: the checkpoint thread cannot open it: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }
    sqlite3_busy_timeout(db, 2000);
    // **One read first** (2026-10-12): a connection that has read nothing has not opened the
    // journal, and sqlite3_wal_checkpoint_v2 on it answers OK and copies nothing -- a 150-bot
    // run's journal grew to 2 GB in half an hour with characters.db at 4 KB, and only the close
    // folded it back. Reading the schema opens the journal, and it stays open for the thread.
    exec(db, "SELECT count(*) FROM sqlite_master");
    std::unique_lock<std::mutex> lock(mutex_);
    while (!wake_.wait_for(lock, kCheckpointEvery, [&] { return stopping_; })) {
        lock.unlock();
        const auto at = std::chrono::steady_clock::now();
        int pages = 0, copied = 0;
        // PASSIVE: never waits on the server's own writes, and they never wait on it.
        if (sqlite3_wal_checkpoint_v2(db, nullptr, SQLITE_CHECKPOINT_PASSIVE, &pages, &copied) != SQLITE_OK) {
            core::logError("characters.db: checkpoint: %s", sqlite3_errmsg(db));
        } else {
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - at).count();
            if (ms > 50.0) core::logf("characters.db: a checkpoint of %d pages took %.0f ms", copied, ms);
        }
        lock.lock();
    }
    sqlite3_close(db);
}

bool Store::open(const std::string& path, std::string& error) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        error = path + ": " + sqlite3_errmsg(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    // The journal beside it, and a write that survives the server's crash though not the box's
    // power: a minute's play at most, which the minute's writing risks anyway.
    sqlite3_busy_timeout(db_, 2000);
    // And never folded back inside one of the server's own commits (checkpoints).
    if (!exec(db_, "PRAGMA journal_mode=WAL") || !exec(db_, "PRAGMA synchronous=NORMAL") ||
        !exec(db_, "PRAGMA wal_autocheckpoint=0") || !exec(db_, kSchema) ||
        !exec(db_, "CREATE TABLE IF NOT EXISTS broker (id INTEGER PRIMARY KEY, book BLOB NOT NULL,"
                   " saved INTEGER NOT NULL)") ||
        !exec(db_, "CREATE TABLE IF NOT EXISTS passes (ticket TEXT PRIMARY KEY, account TEXT NOT NULL,"
                   " secret TEXT NOT NULL, count INTEGER NOT NULL, until INTEGER NOT NULL)")) {
        error = path + ": " + sqlite3_errmsg(db_);
        return false;
    }
    // A file from before a column has it added, at its default: no world is "wherever his next
    // Hello names" (sprint 20, step 2), and no account is the bench's (phase 6).
    std::vector<std::string> has;
    {
        Statement q(db_, "PRAGMA table_info(characters)");
        while (q && sqlite3_step(q.s) == SQLITE_ROW) has.push_back(text(q.s, 1));
    }
    for (const auto& [column, sql] : kAdded) {
        if (std::find(has.begin(), has.end(), column) != has.end()) continue;
        if (!exec(db_, sql)) {
            error = path + ": " + sqlite3_errmsg(db_);
            return false;
        }
    }
    // And the Broker's book its list, as a character has his.
    has.clear();
    {
        Statement q(db_, "PRAGMA table_info(broker)");
        while (q && sqlite3_step(q.s) == SQLITE_ROW) has.push_back(text(q.s, 1));
    }
    if (std::find(has.begin(), has.end(), "items") == has.end() &&
        !exec(db_, "ALTER TABLE broker ADD COLUMN items INTEGER NOT NULL DEFAULT 0")) {
        error = path + ": " + sqlite3_errmsg(db_);
        return false;
    }
    if (!exec(db_, "CREATE TABLE IF NOT EXISTS item_tables (id INTEGER PRIMARY KEY, names TEXT NOT NULL)")) {
        error = path + ": " + sqlite3_errmsg(db_);
        return false;
    }
    if (path != ":memory:") checkpointer_ = std::thread(&Store::checkpoints, this, path);
    return true;
}

bool Store::useItems(const content::Tables& tables, std::string& error) {
    if (db_ == nullptr || tables.items.empty()) {
        error = "characters.db: no item rows to number its things by";
        return false;
    }
    const auto add = [&](const std::string& names, int id) {
        Statement q(db_, id > 0 ? "INSERT INTO item_tables (id, names) VALUES (?, ?)"
                                : "INSERT INTO item_tables (names) VALUES (?)");
        if (!q) return 0;
        int at = 1;
        if (id > 0) sqlite3_bind_int(q.s, at++, id);
        sqlite3_bind_text(q.s, at, names.c_str(), int(names.size()), SQLITE_TRANSIENT);
        return sqlite3_step(q.s) == SQLITE_DONE ? int(sqlite3_last_insert_rowid(db_)) : 0;
    };
    std::map<int, std::string> lists;
    {
        Statement q(db_, "SELECT id, names FROM item_tables");
        while (q && sqlite3_step(q.s) == SQLITE_ROW) lists[sqlite3_column_int(q.s, 0)] = text(q.s, 1);
    }
    if (lists.find(1) == lists.end()) {
        const std::string before = namesOf(tables, kAddedOn20261016);
        if (add(before, 1) != 1) {
            error = std::string("characters.db: item_tables: ") + sqlite3_errmsg(db_);
            return false;
        }
        lists[1] = before;
    }
    const std::string mine = namesOf(tables);
    items_ = 0;
    for (const auto& [id, names] : lists) {
        if (names == mine) items_ = id;
    }
    if (items_ == 0) {
        items_ = add(mine, 0);
        if (items_ == 0) {
            error = std::string("characters.db: item_tables: ") + sqlite3_errmsg(db_);
            return false;
        }
        lists[items_] = mine;
    }
    renumbers_.clear();
    for (const auto& [id, names] : lists) {
        if (id == items_) continue;
        std::vector<int32_t> map = renumbering(names, tables);
        if (!identity(map, tables.items.size())) renumbers_[id] = std::move(map);
    }
    // A row from before lists were kept was written with list 1.
    if (const auto first = renumbers_.find(1); first != renumbers_.end()) renumbers_[0] = first->second;
    bookRenumber_ = nullptr;
    {
        Statement q(db_, "SELECT items FROM broker WHERE id = 1");
        if (q && sqlite3_step(q.s) == SQLITE_ROW) {
            const auto found = renumbers_.find(sqlite3_column_int(q.s, 0));
            if (found != renumbers_.end()) bookRenumber_ = &found->second;
        }
    }
    core::logf("characters.db: %zu item rows, list %d; %zu older list(s) renumbered as read", tables.items.size(),
               items_, renumbers_.count(0) ? renumbers_.size() - 1 : renumbers_.size());
    return true;
}

void Store::renumber(sim::Kept& kept, int items) const {
    const auto found = renumbers_.find(items);
    if (found == renumbers_.end()) return;
    const std::vector<int32_t>& map = found->second;
    for (sim::Held& h : kept.hero.slots) h = renumbered(h, map);
    for (int cell = 0; cell < sim::kVaultCells; ++cell) kept.vault.put(cell, renumbered(kept.vault[cell], map));
    for (int cell = 0; cell < sim::kMachineCells; ++cell) {
        kept.machine.put(cell, renumbered(kept.machine[cell], map));
    }
}

bool Store::who(uint64_t token, Who& out) {
    if (db_ == nullptr) return false;
    Statement q(db_, "SELECT account, name, kin, layout, deleted FROM characters WHERE token = ?");
    if (!q) return false;
    sqlite3_bind_int64(q.s, 1, sqlite3_int64(token));
    if (sqlite3_step(q.s) != SQLITE_ROW) return false;
    out.account = text(q.s, 0);
    out.name = text(q.s, 1);
    out.kin = sim::Kin(std::clamp(sqlite3_column_int(q.s, 2), 0, int(sim::Kin::MagicGladiator)));
    out.fresh = sqlite3_column_int(q.s, 3) == 0;
    out.deleted = sqlite3_column_int64(q.s, 4) != 0;
    return true;
}

std::vector<Store::Roll> Store::everyone() {
    std::vector<Roll> all;
    if (db_ == nullptr) return all;
    Statement q(db_,
                "SELECT account, name, world, kin, level, saved, layout, kept, items FROM characters"
                " WHERE account != '' AND deleted = 0 ORDER BY saved DESC");
    if (!q) return all;
    while (sqlite3_step(q.s) == SQLITE_ROW) {
        Roll one;
        one.account = text(q.s, 0);
        one.name = text(q.s, 1);
        one.world = text(q.s, 2);
        one.kin = sim::Kin(std::clamp(sqlite3_column_int(q.s, 3), 0, int(sim::Kin::MagicGladiator)));
        one.level = sqlite3_column_int(q.s, 4);
        one.saved = sqlite3_column_int64(q.s, 5);
        const int layout = sqlite3_column_int(q.s, 6);
        one.fresh = layout == 0;
        if (!one.fresh && layout >= kOldestLayout && layout <= kLayout) {
            const auto* bytes = static_cast<const uint8_t*>(sqlite3_column_blob(q.s, 7));
            const std::vector<uint8_t> kept(bytes, bytes + sqlite3_column_bytes(q.s, 7));
            one.read = net::keptFrom(kept, one.kept, layout);
            if (one.read) renumber(one.kept, sqlite3_column_int(q.s, 8));
        }
        all.push_back(std::move(one));
    }
    return all;
}

std::vector<Store::Listed> Store::list(const std::string& account) {
    std::vector<Listed> all;
    if (db_ == nullptr || account.empty()) return all;
    Statement q(db_,
                "SELECT token, name, slot, world, layout, kept, kin, items FROM characters"
                " WHERE account = ? AND deleted = 0 ORDER BY slot");
    if (!q) return all;
    sqlite3_bind_text(q.s, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(q.s) == SQLITE_ROW) {
        Listed one;
        one.token = uint64_t(sqlite3_column_int64(q.s, 0));
        one.name = text(q.s, 1);
        one.slot = sqlite3_column_int(q.s, 2);
        one.world = text(q.s, 3);
        const int layout = sqlite3_column_int(q.s, 4);
        one.kin = sim::Kin(std::clamp(sqlite3_column_int(q.s, 6), 0, int(sim::Kin::MagicGladiator)));
        one.fresh = layout == 0;
        if (!one.fresh) {
            const auto* bytes = static_cast<const uint8_t*>(sqlite3_column_blob(q.s, 5));
            const std::vector<uint8_t> kept(bytes, bytes + sqlite3_column_bytes(q.s, 5));
            if (layout < kOldestLayout || layout > kLayout || !net::keptFrom(kept, one.kept, layout)) {
                core::logError("characters.db: %s (%016llx) does not read; not listed", one.name.c_str(),
                               (unsigned long long)one.token);
                continue;
            }
            renumber(one.kept, sqlite3_column_int(q.s, 7));
        }
        all.push_back(std::move(one));
    }
    return all;
}

bool Store::named(const std::string& name) {
    if (db_ == nullptr) return false;
    Statement q(db_, "SELECT 1 FROM characters WHERE name = ? COLLATE NOCASE AND deleted = 0");
    if (!q) return false;
    sqlite3_bind_text(q.s, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    return sqlite3_step(q.s) == SQLITE_ROW;
}

bool Store::create(uint64_t token, const std::string& account, const std::string& name, sim::Kin kin,
                   int slot, const std::string& world) {
    if (db_ == nullptr) return false;
    Statement q(db_,
                "INSERT INTO characters (token, layout, kept, kin, level, money, saved, world, account,"
                " name, slot) VALUES (?, 0, x'', ?, 1, 0, ?, ?, ?, ?, ?)");
    if (!q) return false;
    sqlite3_bind_int64(q.s, 1, sqlite3_int64(token));
    sqlite3_bind_int(q.s, 2, int(kin));
    sqlite3_bind_int64(q.s, 3, int64_t(std::time(nullptr)));
    sqlite3_bind_text(q.s, 4, world.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(q.s, 5, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(q.s, 6, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(q.s, 7, slot);
    if (sqlite3_step(q.s) != SQLITE_DONE) {
        core::logError("characters.db: %s", sqlite3_errmsg(db_));
        return false;
    }
    return true;
}

bool Store::remove(const std::string& account, uint64_t token) {
    if (db_ == nullptr || account.empty()) return false;
    Statement q(db_, "UPDATE characters SET deleted = ? WHERE token = ? AND account = ? AND deleted = 0");
    if (!q) return false;
    sqlite3_bind_int64(q.s, 1, int64_t(std::time(nullptr)));
    sqlite3_bind_int64(q.s, 2, sqlite3_int64(token));
    sqlite3_bind_text(q.s, 3, account.c_str(), -1, SQLITE_TRANSIENT);
    return sqlite3_step(q.s) == SQLITE_DONE && sqlite3_changes(db_) == 1;
}

bool Store::claim(const std::string& account, uint64_t token, const std::string& name, int slot) {
    if (db_ == nullptr || account.empty()) return false;
    Statement q(db_,
                "UPDATE characters SET account = ?, name = ?, slot = ?"
                " WHERE token = ? AND account = '' AND deleted = 0");
    if (!q) return false;
    sqlite3_bind_text(q.s, 1, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(q.s, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(q.s, 3, slot);
    sqlite3_bind_int64(q.s, 4, sqlite3_int64(token));
    return sqlite3_step(q.s) == SQLITE_DONE && sqlite3_changes(db_) == 1;
}

bool Store::find(uint64_t token, sim::Kept& out, std::string& world) {
    if (db_ == nullptr) return false;
    Statement q(db_, "SELECT layout, kept, world, items FROM characters WHERE token = ? AND layout > 0 AND deleted = 0");
    if (!q) return false;
    sqlite3_bind_int64(q.s, 1, sqlite3_int64(token));
    if (sqlite3_step(q.s) != SQLITE_ROW) return false;
    const int layout = sqlite3_column_int(q.s, 0);
    const auto* bytes = static_cast<const uint8_t*>(sqlite3_column_blob(q.s, 1));
    const std::vector<uint8_t> kept(bytes, bytes + sqlite3_column_bytes(q.s, 1));
    if (layout < kOldestLayout || layout > kLayout || !net::keptFrom(kept, out, layout)) {
        core::logError("characters.db: %016llx was written at protocol %d and this is %d; not read",
                       (unsigned long long)token, layout, kLayout);
        return false;
    }
    renumber(out, sqlite3_column_int(q.s, 3));
    const auto* named = reinterpret_cast<const char*>(sqlite3_column_text(q.s, 2));
    world = named ? named : "";
    return true;
}

bool Store::has(uint64_t token) {
    if (db_ == nullptr) return false;
    Statement q(db_, "SELECT 1 FROM characters WHERE token = ?");
    if (!q) return false;
    sqlite3_bind_int64(q.s, 1, sqlite3_int64(token));
    return sqlite3_step(q.s) == SQLITE_ROW;
}

bool Store::keep(const std::vector<Row>& characters) {
    if (db_ == nullptr || characters.empty()) return db_ != nullptr;
    if (!exec(db_, "BEGIN")) return false;
    Statement q(db_,
                "INSERT INTO characters (token, layout, kept, kin, level, money, saved, world, items)"
                " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"
                // Whose he is, his name and his pedestal are the account's to set, never a save's.
                " ON CONFLICT(token) DO UPDATE SET layout = excluded.layout, kept = excluded.kept,"
                " kin = excluded.kin, level = excluded.level, money = excluded.money,"
                " saved = excluded.saved, world = excluded.world, items = excluded.items");
    bool ok = bool(q);
    const int64_t now = int64_t(std::time(nullptr));
    std::vector<uint8_t> bytes;
    for (const auto& [token, world, kept] : characters) {
        if (!ok) break;
        bytes.clear();
        net::putKept(bytes, kept);
        sqlite3_bind_int64(q.s, 1, sqlite3_int64(token));
        sqlite3_bind_int(q.s, 2, kLayout);
        sqlite3_bind_blob(q.s, 3, bytes.data(), int(bytes.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int(q.s, 4, int(kept.hero.kin));
        sqlite3_bind_int(q.s, 5, kept.hero.level);
        sqlite3_bind_int64(q.s, 6, kept.hero.money);
        sqlite3_bind_int64(q.s, 7, now);
        sqlite3_bind_text(q.s, 8, world.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(q.s, 9, items_);
        ok = sqlite3_step(q.s) == SQLITE_DONE;
        if (!ok) core::logError("characters.db: %s", sqlite3_errmsg(db_));
        sqlite3_reset(q.s);
    }
    return exec(db_, ok ? "COMMIT" : "ROLLBACK") && ok;
}

int Store::count() {
    if (db_ == nullptr) return 0;
    Statement q(db_, "SELECT count(*) FROM characters");
    return q && sqlite3_step(q.s) == SQLITE_ROW ? sqlite3_column_int(q.s, 0) : 0;
}

bool Store::keepBook(const std::vector<uint8_t>& bytes) {
    if (db_ == nullptr) return false;
    Statement q(db_, "INSERT OR REPLACE INTO broker (id, book, saved, items) VALUES (1, ?, ?, ?)");
    if (!q) return false;
    sqlite3_bind_blob(q.s, 1, bytes.data(), int(bytes.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int64(q.s, 2, sqlite3_int64(std::time(nullptr)));
    sqlite3_bind_int(q.s, 3, items_);
    if (sqlite3_step(q.s) != SQLITE_DONE) {
        core::logError("characters.db: the Broker's book: %s", sqlite3_errmsg(db_));
        return false;
    }
    return true;
}

bool Store::findBook(std::vector<uint8_t>& bytes) {
    if (db_ == nullptr) return false;
    Statement q(db_, "SELECT book FROM broker WHERE id = 1");
    if (!q || sqlite3_step(q.s) != SQLITE_ROW) return false;
    const auto* at = static_cast<const uint8_t*>(sqlite3_column_blob(q.s, 0));
    bytes.assign(at, at + sqlite3_column_bytes(q.s, 0));
    return true;
}

bool Store::findPass(const std::string& ticket, PassRow& out) {
    if (db_ == nullptr) return false;
    Statement q(db_, "SELECT account, secret, count, until FROM passes WHERE ticket = ? AND until >= ?");
    if (!q) return false;
    sqlite3_bind_text(q.s, 1, ticket.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(q.s, 2, int64_t(std::time(nullptr)));
    if (sqlite3_step(q.s) != SQLITE_ROW) return false;
    out.ticket = ticket;
    out.account = text(q.s, 0);
    out.secret = text(q.s, 1);
    out.count = uint64_t(sqlite3_column_int64(q.s, 2));
    out.until = sqlite3_column_int64(q.s, 3);
    return true;
}

bool Store::keepPass(const PassRow& pass) {
    if (db_ == nullptr) return false;
    {
        Statement old(db_, "DELETE FROM passes WHERE until < ?");
        if (old) {
            sqlite3_bind_int64(old.s, 1, int64_t(std::time(nullptr)));
            sqlite3_step(old.s);
        }
    }
    Statement q(db_, "INSERT OR REPLACE INTO passes (ticket, account, secret, count, until) VALUES (?, ?, ?, ?, ?)");
    if (!q) return false;
    sqlite3_bind_text(q.s, 1, pass.ticket.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(q.s, 2, pass.account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(q.s, 3, pass.secret.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(q.s, 4, sqlite3_int64(pass.count));
    sqlite3_bind_int64(q.s, 5, pass.until);
    return sqlite3_step(q.s) == SQLITE_DONE;
}

}  // namespace mu::server
