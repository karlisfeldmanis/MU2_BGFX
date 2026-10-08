#include "store.h"

#include <algorithm>
#include <ctime>

#include <sqlite3.h>

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
};

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

Store::~Store() { sqlite3_close(db_); }

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
    if (!exec(db_, "PRAGMA journal_mode=WAL") || !exec(db_, "PRAGMA synchronous=NORMAL") ||
        !exec(db_, kSchema)) {
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
    return true;
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

std::vector<Store::Listed> Store::list(const std::string& account) {
    std::vector<Listed> all;
    if (db_ == nullptr || account.empty()) return all;
    Statement q(db_,
                "SELECT token, name, slot, world, layout, kept, kin FROM characters"
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
    Statement q(db_, "SELECT layout, kept, world FROM characters WHERE token = ? AND layout > 0 AND deleted = 0");
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
                "INSERT INTO characters (token, layout, kept, kin, level, money, saved, world)"
                " VALUES (?, ?, ?, ?, ?, ?, ?, ?)"
                // Whose he is, his name and his pedestal are the account's to set, never a save's.
                " ON CONFLICT(token) DO UPDATE SET layout = excluded.layout, kept = excluded.kept,"
                " kin = excluded.kin, level = excluded.level, money = excluded.money,"
                " saved = excluded.saved, world = excluded.world");
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

}  // namespace mu::server
