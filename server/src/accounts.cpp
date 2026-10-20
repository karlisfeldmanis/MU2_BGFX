// The character screen's server half: an account's roster, making and deleting a character.
// Split out of main.cpp (host.h).

#include "host.h"

namespace mu::server {


// An account's key: letters and digits the client made (game/account.h), long enough not to be
// guessed and short enough to be a key.
bool keyShape(const std::string& key) {
    if (key.size() < 16 || key.size() > net::kMostKey) return false;
    for (char c : key) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
    }
    return true;
}

// Whether a token is being played on some connection now.
bool inPlay(const std::vector<std::unique_ptr<Session>>& sessions, uint64_t token) {
    for (const auto& other : sessions) {
        if (other->token == token && other->world != nullptr && other->socket.open()) return true;
    }
    return false;
}

// The account's characters as the screen stands them: his pedestal, his name, his class and level,
// promoted or not, the world he comes into, and what he wears. One never played is level 1 in his
// class's town with his class's weapon, as he will be made.
net::Roster rosterOf(const std::string& account, net::Refused refused) {
    net::Roster roster;
    roster.refused = refused;
    for (const server::Store::Listed& one : g_store.list(account)) {
        net::Seat seat;
        seat.token = one.token;
        seat.slot = one.slot;
        seat.name = one.name;
        seat.kin = uint8_t(one.kin);
        if (one.fresh) {
            seat.level = sim::kNewLevel;
            seat.world = sim::homeWorld(one.kin);
            const int32_t weapon = g_items.itemNamed(sim::cradleWeapon(one.kin));
            if (weapon >= 0) seat.worn.push_back({uint8_t(sim::kWeaponRight), sim::Held{weapon, 0, 1}});
        } else {
            const sim::HeroRecord& hero = one.kept.hero;
            seat.level = hero.level;
            seat.second = sim::promoted(hero.quests, int(hero.kin));
            seat.world = one.world.empty() ? sim::homeWorld(hero.kin) : one.world;
            for (int slot = 0; slot < sim::kWorn; ++slot) {
                if (!hero.slots[slot].empty()) seat.worn.push_back({uint8_t(slot), hero.slots[slot]});
            }
        }
        roster.seats.push_back(std::move(seat));
    }
    return roster;
}

bool answer(Session& one, net::Refused refused) {
    std::vector<uint8_t> out;
    net::put(out, rosterOf(one.account, refused));
    return one.socket.send(out);
}

// The first free pedestal of an account, or -1.
int freeSlot(const std::string& account) {
    bool taken[sim::kRosterSlots] = {};
    for (const server::Store::Listed& one : g_store.list(account)) {
        if (one.slot >= 0 && one.slot < sim::kRosterSlots) taken[one.slot] = true;
    }
    for (int s = 0; s < sim::kRosterSlots; ++s) {
        if (!taken[s]) return s;
    }
    return -1;
}

// A game of another build, told so before anything of its sign-in is read: another protocol
// (net::parse reads no further than the version of one), another item list, which would read
// every thing by the wrong row (Tables::itemsPrint), or other rules, whose mirror would part from
// this realm (net::rulesPrint). Answered Refused::Outdated with no characters, and nothing taken
// from it after. Also what a game cut off mid-session asks before it comes back (PlayMode's
// knock), so a deploy under it is "update", not a world that will not open.
bool outdated(Session& one, const net::Account& said) {
    if (said.version != net::kVersion) {
        core::logError("%s: version %u, this server speaks %u; told to update", one.who.c_str(), said.version,
                       net::kVersion);
    } else if (said.items != g_items.itemsPrint()) {
        core::logError("%s: item list %016llx, this server's %016llx; told to update", one.who.c_str(),
                       (unsigned long long)said.items, (unsigned long long)g_items.itemsPrint());
    } else if (said.rules != net::rulesPrint(g_items)) {
        core::logError("%s: rules %016llx, this server's %016llx; told to update", one.who.c_str(),
                       (unsigned long long)said.rules, (unsigned long long)net::rulesPrint(g_items));
    } else {
        return false;
    }
    std::vector<uint8_t> out;
    net::put(out, net::Roster{net::Refused::Outdated, {}});
    if (one.socket.send(out)) one.socket.flush();
    return true;
}

// An Account: the key taken, the machine's old characters claimed onto it, the Roster said.
// Its build was checked as it came (outdated).
bool account(Session& one, const net::Account& said) {
    if (!keyShape(said.key)) {
        core::logError("%s: an account key of no shape", one.who.c_str());
        return false;
    }
    one.account = said.key;
    // Characters this machine played here before accounts: put on it, under the names its saves
    // gave them, when they are nobody's -- a name already taken, or no pedestal free, leaves him.
    for (const net::Claim& claim : said.claims) {
        if (claim.token == 0 || !sim::goodName(claim.name) || g_store.named(claim.name)) continue;
        int slot = claim.slot;
        for (const server::Store::Listed& mine : g_store.list(one.account)) {
            if (mine.slot == slot) slot = -1;
        }
        if (slot < 0 || slot >= sim::kRosterSlots) slot = freeSlot(one.account);
        if (slot < 0) continue;
        if (g_store.claim(one.account, claim.token, claim.name, slot)) {
            core::logf("%s: %s claimed onto his account, slot %d", one.who.c_str(), claim.name.c_str(), slot);
        }
    }
    // A website account by its number; a machine key by its first six letters, never whole.
    one.who += reservedKey(one.account) ? " (account " + std::to_string(std::atoll(one.account.c_str() + 7)) + ")"
                                        : " (" + one.account.substr(0, 6) + ")";
    core::logf("%s: at the character screen, %zu character(s)", one.who.c_str(),
               g_store.list(one.account).size());
    return answer(one, net::Refused::None);
}

bool create(Session& one, const net::Create& asked, std::mt19937_64& seeds,
            const std::vector<std::unique_ptr<Session>>& sessions) {
    const auto mine = g_store.list(one.account);
    const sim::Kin kin = sim::Kin(std::min<int>(asked.kin, int(sim::Kin::MagicGladiator)));
    bool gladiator = false;
    for (const server::Store::Listed& m : mine) {
        gladiator = gladiator || (!m.fresh && m.kept.hero.level >= sim::kGladiatorLevel);
    }
    net::Refused refused = net::Refused::None;
    if (!sim::goodName(asked.name)) refused = net::Refused::BadName;
    else if (kin == sim::Kin::MagicGladiator && !gladiator) refused = net::Refused::BadName;
    // Taken by a character kept, or by one playing now: a bot's name is in no account's list.
    else if (g_store.named(asked.name) || nameInUse(asked.name, sessions, nullptr)) refused = net::Refused::Taken;
    const int slot = freeSlot(one.account);
    if (refused == net::Refused::None && slot < 0) refused = net::Refused::NoRoom;
    if (refused == net::Refused::None) {
        uint64_t token = 0;
        do token = seeds(); while (token == 0 || g_store.has(token));
        if (!g_store.create(token, one.account, asked.name, kin, slot, sim::homeWorld(kin))) {
            return false;
        }
        core::logf("%s: made %s, class %d, slot %d", one.who.c_str(), asked.name.c_str(), int(kin), slot);
    }
    return answer(one, refused);
}

bool drop(Session& one, const net::Delete& asked, const std::vector<std::unique_ptr<Session>>& sessions) {
    net::Refused refused = net::Refused::None;
    if (inPlay(sessions, asked.token)) {
        refused = net::Refused::Playing;
    } else if (!g_store.remove(one.account, asked.token)) {
        refused = net::Refused::NotYours;
    } else {
        core::logf("%s: deleted %016llx", one.who.c_str(), (unsigned long long)asked.token);
    }
    return answer(one, refused);
}

}  // namespace mu::server
