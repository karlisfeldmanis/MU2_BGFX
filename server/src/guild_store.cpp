// The guilds on the disk (docs/game/guild.md): Store's guild rows, in characters.db beside the
// characters and the Broker's book. Kept out of store.cpp, which is the characters'.

#include <sqlite3.h>

#include <cstring>

#include "core/log.h"
#include "store.h"

namespace mu::server {
namespace {

constexpr const char* kGuildSchema =
    "CREATE TABLE IF NOT EXISTS guilds (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE COLLATE NOCASE,"
    " mark BLOB NOT NULL, notice TEXT NOT NULL DEFAULT '', made INTEGER NOT NULL);"
    "CREATE TABLE IF NOT EXISTS guild_members (token INTEGER PRIMARY KEY, guild INTEGER NOT NULL,"
    " place INTEGER NOT NULL, name TEXT NOT NULL, kin INTEGER NOT NULL, level INTEGER NOT NULL,"
    " seen INTEGER NOT NULL);";

bool run(sqlite3* db, const char* sql) {
    char* error = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        core::logError("store: %s", error ? error : "?");
        sqlite3_free(error);
        return false;
    }
    return true;
}

// One prepared statement, finished when it goes.
struct Query {
    sqlite3_stmt* s = nullptr;
    Query(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK) {
            core::logError("store: %s", sqlite3_errmsg(db));
            s = nullptr;
        }
    }
    ~Query() { sqlite3_finalize(s); }
    explicit operator bool() const { return s != nullptr; }
};

std::string textAt(sqlite3_stmt* s, int column) {
    const unsigned char* t = sqlite3_column_text(s, column);
    return t ? reinterpret_cast<const char*>(t) : "";
}

}  // namespace

bool Store::loadGuilds(std::vector<GuildRow>& out) {
    out.clear();
    if (db_ == nullptr || !run(db_, kGuildSchema)) return false;
    {
        Query q(db_, "SELECT id, name, mark, notice, made FROM guilds ORDER BY id");
        if (!q) return false;
        while (sqlite3_step(q.s) == SQLITE_ROW) {
            GuildRow g;
            g.id = uint32_t(sqlite3_column_int64(q.s, 0));
            g.name = textAt(q.s, 1);
            const void* blob = sqlite3_column_blob(q.s, 2);
            const int bytes = sqlite3_column_bytes(q.s, 2);
            if (blob != nullptr && bytes == int(g.mark.size())) std::memcpy(g.mark.data(), blob, g.mark.size());
            g.notice = textAt(q.s, 3);
            g.made = sqlite3_column_int64(q.s, 4);
            out.push_back(std::move(g));
        }
    }
    Query q(db_, "SELECT token, guild, name, kin, level, seen FROM guild_members ORDER BY guild, place");
    if (!q) return false;
    while (sqlite3_step(q.s) == SQLITE_ROW) {
        const uint32_t guild = uint32_t(sqlite3_column_int64(q.s, 1));
        for (GuildRow& g : out) {
            if (g.id != guild) continue;
            GuildRow::Member m;
            m.token = uint64_t(sqlite3_column_int64(q.s, 0));
            m.name = textAt(q.s, 2);
            m.kin = sqlite3_column_int(q.s, 3);
            m.level = sqlite3_column_int(q.s, 4);
            m.seen = sqlite3_column_int64(q.s, 5);
            g.members.push_back(std::move(m));
            break;
        }
    }
    return true;
}

bool Store::keepGuild(const GuildRow& guild) {
    // Its tables made here too, so a guild founded on a store loadGuilds never read is kept.
    if (db_ == nullptr || !run(db_, kGuildSchema) || !run(db_, "BEGIN")) return false;
    bool ok = true;
    {
        Query q(db_, "INSERT INTO guilds (id, name, mark, notice, made) VALUES (?, ?, ?, ?, ?)"
                     " ON CONFLICT(id) DO UPDATE SET name = excluded.name, mark = excluded.mark,"
                     " notice = excluded.notice");
        ok = bool(q);
        if (ok) {
            sqlite3_bind_int64(q.s, 1, sqlite3_int64(guild.id));
            sqlite3_bind_text(q.s, 2, guild.name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_blob(q.s, 3, guild.mark.data(), int(guild.mark.size()), SQLITE_TRANSIENT);
            sqlite3_bind_text(q.s, 4, guild.notice.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(q.s, 5, guild.made);
            ok = sqlite3_step(q.s) == SQLITE_DONE;
        }
    }
    if (ok) {
        Query q(db_, "DELETE FROM guild_members WHERE guild = ?");
        ok = bool(q);
        if (ok) {
            sqlite3_bind_int64(q.s, 1, sqlite3_int64(guild.id));
            ok = sqlite3_step(q.s) == SQLITE_DONE;
        }
    }
    for (size_t i = 0; ok && i < guild.members.size(); ++i) {
        const GuildRow::Member& m = guild.members[i];
        Query q(db_, "INSERT OR REPLACE INTO guild_members (token, guild, place, name, kin, level, seen)"
                     " VALUES (?, ?, ?, ?, ?, ?, ?)");
        ok = bool(q);
        if (!ok) break;
        sqlite3_bind_int64(q.s, 1, sqlite3_int64(m.token));
        sqlite3_bind_int64(q.s, 2, sqlite3_int64(guild.id));
        sqlite3_bind_int(q.s, 3, int(i));
        sqlite3_bind_text(q.s, 4, m.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(q.s, 5, m.kin);
        sqlite3_bind_int(q.s, 6, m.level);
        sqlite3_bind_int64(q.s, 7, m.seen);
        ok = sqlite3_step(q.s) == SQLITE_DONE;
    }
    if (!ok) {
        core::logError("store: guild %u not kept: %s", guild.id, sqlite3_errmsg(db_));
        run(db_, "ROLLBACK");
        return false;
    }
    return run(db_, "COMMIT");
}

bool Store::dropGuild(uint32_t id) {
    if (db_ == nullptr || !run(db_, kGuildSchema) || !run(db_, "BEGIN")) return false;
    bool ok = true;
    for (const char* sql : {"DELETE FROM guild_members WHERE guild = ?", "DELETE FROM guilds WHERE id = ?"}) {
        Query q(db_, sql);
        ok = ok && bool(q);
        if (!ok) break;
        sqlite3_bind_int64(q.s, 1, sqlite3_int64(id));
        ok = sqlite3_step(q.s) == SQLITE_DONE;
    }
    if (!ok) {
        run(db_, "ROLLBACK");
        return false;
    }
    return run(db_, "COMMIT");
}

}  // namespace mu::server
