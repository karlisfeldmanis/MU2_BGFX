#pragma once

// The server's characters on its disk (server phase 3, sprint 20):
// characters.db, a SQLite file of its own, apart from the content's mu.db. One row a character,
// by the token his client keeps: the sim::Kept in the wire's own bytes (net::putKept) and the
// world he is in -- the tile is the Kept's own column and row -- with his class, level and Zen
// beside it for whoever reads the file with sqlite3.
//
// Written when he leaves a world, every minute for whoever is in one, and when the server stops.
//
// **And whose he is** (phase 6, sprint 23): the account's key, his name and
// his pedestal. A character made on the character screen is a row before he has ever played --
// layout 0 and no bytes, his class in `kin` -- and his first Hello makes him as a new character
// is made. A deleted one is kept, stamped with when, and never read again. A row of no account
// is the bench's: the bots' and `--new`'s, played by its token alone.

#include <array>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "content/tables.h"
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

    // **The item rows a save's numbers mean** (2026-10-17). A Held carries its row's index in
    // the tables, and the tables are in name order, so a new item moves every row after it: the
    // Pendant of Haste (row 177, a3d4dc48) made every saved thing past it read as the row above
    // -- an Apple as Red Spirit Pants laid over its neighbours, a Ring of Ice +2 as a Loch's
    // Feather +2, Guardian Pants as Spirit Pants (muclassic, 2026-10-16). Each row now names the
    // item list it was written with (item_tables), and one written with another list has its
    // things renumbered by name as it is read; a thing whose item is gone reads as nothing.
    // Called after open and before any character or the book is read. False when the file
    // would not take the list.
    bool useItems(const content::Tables& tables, std::string& error);
    // Renumbers a book read by findBook to this server's items (sim::Book::renumber's map), or
    // null when it needs none.
    const std::vector<int32_t>* bookRenumber() const { return bookRenumber_; }

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
    // Every character of an account, not deleted, as its row's columns say (the website's All players,
    // tellOnline): never his bytes. `saved` is when he was last written, unix seconds; `fresh` one made
    // and never played.
    struct Roll {
        std::string account, name, world;
        sim::Kin kin = sim::Kin::DarkKnight;
        int level = 1;
        int64_t saved = 0;
        bool fresh = false;
        sim::Kept kept;  // only when not fresh and it read
        bool read = false;
    };
    std::vector<Roll> everyone();
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

    // **The Broker's book** (sim::Book, docs/game/broker.md): the market's listings and every Collect,
    // one row of its own bytes beside the characters. Written whenever it changed, in the pass
    // that writes the characters it moved things for. False when the disk said no, or none yet.
    bool keepBook(const std::vector<uint8_t>& bytes);
    bool findBook(std::vector<uint8_t>& bytes);

    // **The guilds** (docs/game/guild.md, server/src/guild_store.cpp): a row a guild and a row a
    // member, by his character's token; written whole whenever the guild changes, as WebZen's
    // ExDB kept them apart from the characters. The master is the first member.
    struct GuildRow {
        struct Member {
            uint64_t token = 0;
            std::string name;
            int kin = 0;
            int level = 1;
            int64_t seen = 0;  // unix seconds he last played
        };
        uint32_t id = 0;
        std::string name;
        std::array<uint8_t, 32> mark{};
        std::string notice;
        int64_t made = 0;
        std::vector<Member> members;
    };
    // Every guild on the disk, its tables made the first time. False when the disk said no.
    bool loadGuilds(std::vector<GuildRow>& out);
    bool keepGuild(const GuildRow& guild);
    bool dropGuild(uint32_t id);

    // **The game's passes** (signin.cpp, docs/plans/security.md): a launcher ticket's account,
    // the secret its proofs are made with and the highest count proven, until `until` (unix
    // seconds), so a restarted server still knows the players who were on it and nobody has to
    // press Play again. Written at each proof; the website tells a ticket only once.
    struct PassRow {
        std::string ticket;
        std::string account;
        std::string secret;
        uint64_t count = 0;
        int64_t until = 0;
    };
    // The ticket's row, when it has one not past its time.
    bool findPass(const std::string& ticket, PassRow& out);
    // Written or overwritten; the ones past their time go as it is written.
    bool keepPass(const PassRow& pass);

private:
    // The journal folded back into the file, on a thread of its own with its own connection, at
    // a lowered priority (checkpoints): what SQLite did inside whichever commit crossed 4 MB of
    // journal, on the one thread every world ticks on (sprint 25).
    void checkpoints(std::string path);
    // A read character's things to this server's rows, from the list his row was written with.
    void renumber(sim::Kept& kept, int items) const;

    sqlite3* db_ = nullptr;
    // useItems': this server's list in item_tables, and every other list's old-to-new rows.
    int items_ = 0;
    std::map<int, std::vector<int32_t>> renumbers_;
    const std::vector<int32_t>* bookRenumber_ = nullptr;
    std::thread checkpointer_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stopping_ = false;
};

}  // namespace mu::server
