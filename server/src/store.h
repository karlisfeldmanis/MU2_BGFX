#pragma once

// The server's characters on its disk (server-plan phase 3, docs/sprints/20-the-world-host.md):
// characters.db, a SQLite file of its own, apart from the content's mu.db. One row a character,
// by the token his client keeps: the sim::Kept in the wire's own bytes (net::putKept) and the
// world he is in -- the tile is the Kept's own column and row -- with his class, level and Zen
// beside it for whoever reads the file with sqlite3.
//
// Written when he leaves a world, every minute for whoever is in one, and when the server stops.
//
// **And whose he is** (phase 6, docs/sprints/23-the-account.md): the account's key, his name and
// his pedestal. A character made on the character screen is a row before he has ever played --
// layout 0 and no bytes, his class in `kin` -- and his first Hello makes him as a new character
// is made. A deleted one is kept, stamped with when, and never read again. A row of no account
// is the bench's: the bots' and `--new`'s, played by its token alone.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "sim/realm.h"

struct sqlite3;

namespace mu::server {

class Store {
public:
    Store() = default;
    ~Store();
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    // Opens or makes the file. False, with the reason in `error`, when it cannot.
    bool open(const std::string& path, std::string& error);

    // A character as kept: under his token, in his world.
    struct Row {
        uint64_t token = 0;
        std::string world;
        sim::Kept kept;
    };

    // Whose a token is, and whether he has played: false for a token it has never kept.
    struct Who {
        std::string account;  // empty for a character of no account
        std::string name;
        sim::Kin kin = sim::Kin::DarkKnight;
        bool fresh = false;    // made on the screen and never played
        bool deleted = false;
    };
    bool who(uint64_t token, Who& out);

    // An account's characters, not deleted, by slot: his row's columns and, once he has played,
    // his bytes read back.
    struct Listed {
        uint64_t token = 0;
        std::string name;
        int slot = 0;
        std::string world;
        bool fresh = false;
        sim::Kin kin = sim::Kin::DarkKnight;
        sim::Kept kept;  // only when not fresh
    };
    std::vector<Listed> list(const std::string& account);
    // Whether a character not deleted already has this name, any case.
    bool named(const std::string& name);
    // A new character on the account, never played, in `slot`. False when the disk said no.
    bool create(uint64_t token, const std::string& account, const std::string& name, sim::Kin kin,
                int slot, const std::string& world);
    // Stamped deleted, if it is the account's. False otherwise.
    bool remove(const std::string& account, uint64_t token);
    // A character of no account put on this one, under `name` in `slot`. False when the token is
    // unknown, already someone's, or deleted.
    bool claim(const std::string& account, uint64_t token, const std::string& name, int slot);

    // His character under `token`, and the world he is in (empty in a row from before worlds
    // were kept). False for a token it has never kept, or one kept in a shape this server no
    // longer reads (the shape it was written in is beside it).
    bool find(uint64_t token, sim::Kept& out, std::string& world);
    bool has(uint64_t token);

    // Each written, or overwritten, under its token, in one transaction. False when the disk
    // said no; the reason is in the log, and what was there before stands.
    bool keep(const std::vector<Row>& characters);
    bool keep(const Row& one) { return keep(std::vector<Row>{one}); }

    int count();

private:
    sqlite3* db_ = nullptr;
};

}  // namespace mu::server
