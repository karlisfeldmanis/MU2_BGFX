#pragma once

// The server's characters on its disk (server-plan phase 3, docs/sprints/20-the-world-host.md):
// characters.db, a SQLite file of its own, apart from the content's mu.db. One row a character,
// by the token his client keeps: the sim::Kept in the wire's own bytes (net::putKept) and the
// world he is in -- the tile is the Kept's own column and row -- with his class, level and Zen
// beside it for whoever reads the file with sqlite3.
//
// Written when he leaves a world, every minute for whoever is in one, and when the server stops.
// Until accounts (phase 6) the token is all there is to a login.

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
