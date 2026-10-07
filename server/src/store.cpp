#include "store.h"

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
    " world TEXT NOT NULL DEFAULT '')";

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
    // A file from before worlds were kept (sprint 20, step 2) has no world column: given one,
    // empty, which is "wherever his next Hello names".
    bool hasWorld = false;
    {
        Statement q(db_, "PRAGMA table_info(characters)");
        while (q && sqlite3_step(q.s) == SQLITE_ROW) {
            const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(q.s, 1));
            if (name && std::string(name) == "world") hasWorld = true;
        }
    }
    if (!hasWorld && !exec(db_, "ALTER TABLE characters ADD COLUMN world TEXT NOT NULL DEFAULT ''")) {
        error = path + ": " + sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

bool Store::find(uint64_t token, sim::Kept& out, std::string& world) {
    if (db_ == nullptr) return false;
    Statement q(db_, "SELECT layout, kept, world FROM characters WHERE token = ?");
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
                "INSERT OR REPLACE INTO characters (token, layout, kept, kin, level, money, saved, world)"
                " VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
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
