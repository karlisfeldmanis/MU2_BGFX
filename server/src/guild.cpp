// The guild (docs/game/guild.md): founding at the Guild Master, invites and asks, members, the
// notice, and the guild's state and marks to whoever needs them. MU's rules (WebZen GameServer
// 1.00.93 and MuMain, traced in the doc); the realm knows nothing of guilds.

#include <ctime>

#include "host.h"

namespace mu::server {
namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

int64_t unixNow() { return int64_t(std::time(nullptr)); }

// His body in his realm, or nullptr before his welcome.
const sim::Body* bodyOf(const Session& one) {
    if (!one.welcomed || one.world == nullptr || one.player == 0) return nullptr;
    return one.world->realm->find(one.player);
}

int levelOf(const Session& one) {
    const sim::Body* him = bodyOf(one);
    return him ? him->level : 0;
}

Guild* guildNamed(const std::string& name) {
    const std::string wanted = lower(name);
    for (auto& [id, guild] : g_guilds) {
        if (lower(guild.name) == wanted) return &guild;
    }
    return nullptr;
}

Guild* mine(uint64_t token) {
    const auto at = g_guildOf.find(token);
    if (at == g_guildOf.end()) return nullptr;
    const auto guild = g_guilds.find(at->second);
    return guild == g_guilds.end() ? nullptr : &guild->second;
}

bool masterOf(const Guild& guild, uint64_t token) {
    return !guild.members.empty() && guild.members.front().token == token;
}

// MU's: the master's level / 10, master counted, 40 at most (GS protocol.cpp:11820).
int capOf(const Guild& guild) {
    const int level = guild.members.empty() ? 0 : guild.members.front().level;
    return std::clamp(level / 10, 1, kGuildMost);
}

void keep(const Guild& guild) {
    Store::GuildRow row;
    row.id = guild.id;
    row.name = guild.name;
    row.mark = guild.mark;
    row.notice = guild.notice;
    row.made = guild.made;
    row.members = guild.members;
    if (!g_store.keepGuild(row)) core::logError("guild %u (%s): not kept", guild.id, guild.name.c_str());
}

// The welcomed sessions in a guild, any map.
std::vector<Session*> online(const Guild& guild, std::vector<std::unique_ptr<Session>>& sessions) {
    std::vector<Session*> out;
    for (const Guild::Member& m : guild.members) {
        if (Session* one = sessionOfToken(m.token, sessions)) out.push_back(one);
    }
    return out;
}

void line(const Guild& guild, const std::string& text, std::vector<std::unique_ptr<Session>>& sessions) {
    for (Session* one : online(guild, sessions)) chatSystem(*one, net::Channel::Guild, text);
}

void sendState(Session& to, const net::GuildState& state) {
    if (!to.welcomed || !to.socket.open()) return;
    std::vector<uint8_t> out;
    net::put(out, state);
    if (!to.socket.send(out)) to.socket.close();
}

net::GuildState stateFor(const Guild& guild, const Session& hearer, std::vector<std::unique_ptr<Session>>& sessions) {
    net::GuildState state;
    state.id = guild.id;
    state.name = guild.name;
    state.mark = guild.mark;
    state.notice = guild.notice;
    state.cap = capOf(guild);
    const int64_t now = unixNow();
    for (const Guild::Member& m : guild.members) {
        net::GuildState::Member out;
        out.name = m.name;
        out.kin = uint8_t(m.kin);
        out.level = m.level;
        if (Session* one = sessionOfToken(m.token, sessions)) {
            out.world = one->world->name;
            out.here = one->world == hearer.world ? one->player : 0;
            out.bot = one->bot;
        } else {
            out.seen = int32_t(std::clamp<int64_t>(now - m.seen, 0, INT32_MAX));
        }
        state.members.push_back(std::move(out));
    }
    return state;
}

void sendGuild(const Guild& guild, std::vector<std::unique_ptr<Session>>& sessions) {
    for (Session* one : online(guild, sessions)) sendState(*one, stateFor(guild, *one, sessions));
}

// Whoever shares his map hears his guild changed: their plates over him (net::Who).
void reannounce(Session& one, std::vector<std::unique_ptr<Session>>& sessions) {
    if (one.world != nullptr && one.welcomed) announce(*one.world, sessions);
}

net::GuildCall callOf(const Guild& guild, const Session& from, bool ask) {
    net::GuildCall call;
    call.ask = ask;
    call.from = from.name;
    if (const sim::Body* him = bodyOf(from)) {
        call.fromKin = uint8_t(him->kin);
        call.fromLevel = him->level;
    }
    call.fromWorld = from.world ? from.world->name : std::string();
    call.fromInParty = g_partyOf.count(from.token) != 0;
    call.guild = guild.id;
    call.guildName = guild.name;
    call.mark = guild.mark;
    call.members = int32_t(guild.members.size());
    call.cap = capOf(guild);
    if (!guild.members.empty()) {
        call.master = guild.members.front().name;
        call.masterKin = uint8_t(guild.members.front().kin);
        call.masterLevel = guild.members.front().level;
    }
    return call;
}

void sendCall(Session& to, const net::GuildCall& call) {
    std::vector<uint8_t> out;
    net::put(out, call);
    if (!to.socket.send(out)) to.socket.close();
}

// MuMain's (NewUIGuildMakeWindow.cpp:21-27, :366): 4 to 8, letters and digits.
std::string nameRefused(const std::string& name) {
    if (name.size() < kGuildNameLeast) return "A guild's name has at least 4 letters.";
    if (name.size() > kGuildNameMost) return "A guild's name has at most 8 letters.";
    for (const char c : name) {
        if (!std::isalnum(uint8_t(c))) return "A guild's name is letters and digits only.";
    }
    return {};
}

// Whether he stands by Devias's Guild Master (sim::kGuildMaster), where the window opens.
bool byTheMaster(const Session& one) {
    const sim::Body* him = bodyOf(one);
    if (him == nullptr) return false;
    const content::Tables& tables = one.world->tables;
    for (size_t i = 0; i < tables.folk.size(); ++i) {
        if (tables.folk[i].number != sim::kGuildMaster) continue;
        int column = 0, row = 0;
        if (!one.world->realm->folkTile(int(i), &column, &row)) continue;
        const float dx = him->x - float(column), dy = him->y - float(row);
        if (dx * dx + dy * dy <= kGuildMasterReach * kGuildMasterReach) return true;
    }
    return false;
}

// He joins: told, the guild told, kept, and every plate over him redrawn.
void join(Guild& guild, Session& one, std::vector<std::unique_ptr<Session>>& sessions) {
    Guild::Member m;
    m.token = one.token;
    m.name = one.name;
    if (const sim::Body* him = bodyOf(one)) {
        m.kin = int(him->kin);
        m.level = him->level;
    }
    m.seen = unixNow();
    guild.members.push_back(m);
    g_guildOf[one.token] = guild.id;
    g_guildInvites.erase(one.token);
    g_guildAsks.erase(one.token);
    keep(guild);
    core::logf("guild %u (%s): %s joins (%zu)", guild.id, guild.name.c_str(), one.name.c_str(), guild.members.size());
    line(guild, one.name + " has joined " + guild.name + ".", sessions);
    sendGuild(guild, sessions);
    reannounce(one, sessions);
}

// One out of his guild: his line, the guild's, its row; the master's leaving is a disband.
void out(Guild& guild, uint64_t token, bool removed, std::vector<std::unique_ptr<Session>>& sessions) {
    std::string name;
    for (size_t i = 0; i < guild.members.size(); ++i) {
        if (guild.members[i].token != token) continue;
        name = guild.members[i].name;
        guild.members.erase(guild.members.begin() + long(i));
        break;
    }
    g_guildOf.erase(token);
    keep(guild);
    core::logf("guild %u (%s): %s %s", guild.id, guild.name.c_str(), name.c_str(), removed ? "removed" : "left");
    if (Session* one = sessionOfToken(token, sessions)) {
        sendState(*one, {});
        chatSystem(*one, net::Channel::Guild,
                   removed ? "You were removed from " + guild.name + "." : "You left " + guild.name + ".");
        reannounce(*one, sessions);
    }
    line(guild, name + (removed ? " was removed from " : " has left ") + guild.name + ".", sessions);
    sendGuild(guild, sessions);
}

void disband(Guild& guild, std::vector<std::unique_ptr<Session>>& sessions) {
    const uint32_t id = guild.id;
    const std::string name = guild.name;
    std::vector<Session*> were = online(guild, sessions);
    for (const Guild::Member& m : guild.members) g_guildOf.erase(m.token);
    for (auto it = g_guildInvites.begin(); it != g_guildInvites.end();) {
        it = it->second.guild == id ? g_guildInvites.erase(it) : std::next(it);
    }
    for (auto it = g_guildAsks.begin(); it != g_guildAsks.end();) {
        it = it->second.guild == id ? g_guildAsks.erase(it) : std::next(it);
    }
    g_guilds.erase(id);
    if (!g_store.dropGuild(id)) core::logError("guild %u (%s): not dropped", id, name.c_str());
    core::logf("guild %u (%s): disbanded", id, name.c_str());
    std::vector<const World*> redrawn;
    for (Session* one : were) {
        sendState(*one, {});
        chatSystem(*one, net::Channel::Guild, name + " has been disbanded.");
        if (std::find(redrawn.begin(), redrawn.end(), one->world) == redrawn.end()) {
            redrawn.push_back(one->world);
            reannounce(*one, sessions);
        }
    }
}

// The guild a call stands for, if it still stands: its guild there, and not full.
Guild* calledGuild(const GuildCalled& call) {
    const auto at = g_guilds.find(call.guild);
    return at == g_guilds.end() ? nullptr : &at->second;
}

}  // namespace

void loadGuilds() {
    std::vector<Store::GuildRow> rows;
    if (!g_store.loadGuilds(rows)) {
        core::logError("guilds: not read from the store");
        return;
    }
    for (Store::GuildRow& row : rows) {
        Guild guild;
        guild.id = row.id;
        guild.name = row.name;
        guild.mark = row.mark;
        guild.notice = row.notice;
        guild.made = row.made;
        guild.members = std::move(row.members);
        if (guild.members.empty()) continue;
        for (const Guild::Member& m : guild.members) g_guildOf[m.token] = guild.id;
        g_nextGuild = std::max(g_nextGuild, guild.id + 1);
        g_guilds[guild.id] = std::move(guild);
    }
    core::logf("guilds: %zu, %zu members", g_guilds.size(), g_guildOf.size());
}

uint32_t guildOf(uint64_t token) {
    const auto at = g_guildOf.find(token);
    return at == g_guildOf.end() ? 0 : at->second;
}

std::vector<Session*> guildMates(Session& one, std::vector<std::unique_ptr<Session>>& sessions) {
    const Guild* guild = mine(one.token);
    if (guild == nullptr) return {};
    return online(*guild, sessions);
}

void sendMarks(Session& to, const std::vector<uint32_t>& guilds) {
    if (!to.welcomed || !to.socket.open()) return;
    for (const uint32_t id : guilds) {
        if (id == 0 || std::find(to.marksSent.begin(), to.marksSent.end(), id) != to.marksSent.end()) continue;
        const auto at = g_guilds.find(id);
        if (at == g_guilds.end()) continue;
        net::GuildMark mark;
        mark.id = id;
        mark.name = at->second.name;
        mark.mark = at->second.mark;
        if (!at->second.members.empty()) mark.master = at->second.members.front().name;
        std::vector<uint8_t> out;
        net::put(out, mark);
        if (!to.socket.send(out)) {
            to.socket.close();
            return;
        }
        to.marksSent.push_back(id);
    }
}

// Welcomed into a world: his guild's state to him, and to the rest that he is on.
void guildArrived(Session& one, std::vector<std::unique_ptr<Session>>& sessions) {
    Guild* guild = mine(one.token);
    if (guild == nullptr) return;
    for (Guild::Member& m : guild->members) {
        if (m.token != one.token) continue;
        m.name = one.name;
        if (const sim::Body* him = bodyOf(one)) {
            m.kin = int(him->kin);
            m.level = him->level;
        }
        m.seen = unixNow();
    }
    guild->dirty = true;
}

void guildAsk(Session& one, const net::GuildAsk& ask, std::vector<std::unique_ptr<Session>>& sessions) {
    const auto tell = [&](const std::string& text) {
        core::logf("guild: to %s: %s", one.name.c_str(), text.c_str());
        chatSystem(one, net::Channel::Guild, text);
    };
    const auto now = Clock::now();
    Guild* guild = mine(one.token);
    switch (ask.what) {
        case net::GuildWhat::Found: {
            if (guild != nullptr) return tell("You already stand with " + guild->name + ".");
            if (levelOf(one) < kGuildFoundLevel) return tell("A guild master must be level 100 or more.");
            if (!byTheMaster(one)) return tell("Found a guild with the Guild Master in Devias.");
            std::string name = ask.name;
            while (!name.empty() && name.back() == ' ') name.pop_back();
            while (!name.empty() && name.front() == ' ') name.erase(name.begin());
            if (const std::string why = nameRefused(name); !why.empty()) return tell(why);
            if (guildNamed(name) != nullptr) return tell(name + " is taken. Choose another name.");
            if (std::all_of(ask.mark.begin(), ask.mark.end(), [](uint8_t b) { return b == 0; })) {
                return tell("Draw your guild's mark first.");
            }
            Guild made;
            made.id = g_nextGuild++;
            made.name = name;
            made.mark = ask.mark;
            made.made = unixNow();
            Guild& kept = g_guilds[made.id] = std::move(made);
            core::logf("guild %u (%s): founded by %s", kept.id, kept.name.c_str(), one.name.c_str());
            join(kept, one, sessions);
            tell("You founded " + kept.name + ". Press G to open your guild.");
            return;
        }
        case net::GuildWhat::Invite: {
            if (guild == nullptr) return tell("You are not in a guild.");
            if (!masterOf(*guild, one.token)) return tell("Only the guild master can invite.");
            Session* them = playerNamed(ask.name, sessions);
            if (them == nullptr) return tell("No one called " + net::saidText(ask.name).substr(0, 16) + " is online.");
            if (them == &one) return tell("You lead " + guild->name + " already.");
            if (const Guild* theirs = mine(them->token)) {
                return tell(them->name + (theirs == guild ? " is already in " : " is in ") + theirs->name + ".");
            }
            if (levelOf(*them) < kGuildJoinLevel) return tell(them->name + " must be level 6 to join a guild.");
            if (int(guild->members.size()) >= capOf(*guild)) {
                return tell(guild->name + " is full: " + std::to_string(capOf(*guild)) + " at your level.");
            }
            g_guildInvites[them->token] = {one.token, guild->id, them->name, now};
            core::logf("guild %u (%s): %s invites %s", guild->id, guild->name.c_str(), one.name.c_str(), them->name.c_str());
            sendCall(*them, callOf(*guild, one, false));
            tell("You invited " + them->name + ". They have 60 s to answer.");
            return;
        }
        case net::GuildWhat::Ask: {
            if (guild != nullptr) return tell("You already stand with " + guild->name + ".");
            if (levelOf(one) < kGuildJoinLevel) return tell("You must be level 6 to join a guild.");
            Session* master = playerNamed(ask.name, sessions);
            Guild* theirs = master ? mine(master->token) : nullptr;
            if (master == nullptr || theirs == nullptr || !masterOf(*theirs, master->token)) {
                return tell(net::saidText(ask.name).substr(0, 16) + " is not a guild master.");
            }
            if (int(theirs->members.size()) >= capOf(*theirs)) return tell(theirs->name + " is full.");
            g_guildAsks[one.token] = {master->token, theirs->id, one.name, now};
            core::logf("guild %u (%s): %s asks to join", theirs->id, theirs->name.c_str(), one.name.c_str());
            sendCall(*master, callOf(*theirs, one, true));
            tell("You asked to join " + theirs->name + ".");
            return;
        }
        case net::GuildWhat::Accept:
        case net::GuildWhat::Decline: {
            const bool yes = ask.what == net::GuildWhat::Accept;
            // A master answering an ask, by the asker's name.
            if (guild != nullptr && masterOf(*guild, one.token)) {
                for (auto it = g_guildAsks.begin(); it != g_guildAsks.end(); ++it) {
                    if (it->second.from != one.token) continue;
                    if (!ask.name.empty() && lower(it->second.name) != lower(ask.name)) continue;
                    const uint64_t asker = it->first;
                    const std::string askerName = it->second.name;
                    g_guildAsks.erase(it);
                    Session* them = sessionOfToken(asker, sessions);
                    if (!yes) {
                        if (them) chatSystem(*them, net::Channel::Guild, guild->name + " declined your request.");
                        return tell("You declined " + askerName + ".");
                    }
                    if (them == nullptr) return tell(askerName + " is no longer online.");
                    if (mine(asker) != nullptr) return tell(askerName + " has joined another guild.");
                    if (int(guild->members.size()) >= capOf(*guild)) return tell(guild->name + " is full.");
                    join(*guild, *them, sessions);
                    return;
                }
                return tell(ask.name.empty() ? "No one is asking to join." : ask.name + " is not asking to join.");
            }
            // An invite to him.
            const auto invite = g_guildInvites.find(one.token);
            if (invite == g_guildInvites.end() || now - invite->second.at > kGuildCallFor) {
                return tell("You have no guild invitation.");
            }
            const GuildCalled call = invite->second;
            g_guildInvites.erase(invite);
            Session* master = sessionOfToken(call.from, sessions);
            Guild* theirs = calledGuild(call);
            if (!yes) {
                if (master) chatSystem(*master, net::Channel::Guild, one.name + " declined to join " + (theirs ? theirs->name : "the guild") + ".");
                return tell("You declined the invitation.");
            }
            if (guild != nullptr) return tell("You already stand with " + guild->name + ".");
            if (theirs == nullptr) return tell("That guild is no more.");
            if (int(theirs->members.size()) >= capOf(*theirs)) return tell(theirs->name + " is full.");
            join(*theirs, one, sessions);
            return;
        }
        case net::GuildWhat::Leave:
            if (guild == nullptr) return tell("You are not in a guild.");
            if (masterOf(*guild, one.token)) return tell("A master disbands his guild rather than leave it.");
            out(*guild, one.token, false, sessions);
            return;
        case net::GuildWhat::Remove: {
            if (guild == nullptr) return tell("You are not in a guild.");
            if (!masterOf(*guild, one.token)) return tell("Only the guild master can remove members.");
            for (size_t i = 1; i < guild->members.size(); ++i) {
                if (lower(guild->members[i].name) != lower(ask.name)) continue;
                out(*guild, guild->members[i].token, true, sessions);
                return;
            }
            return tell(net::saidText(ask.name).substr(0, 16) + " is not in " + guild->name + ".");
        }
        case net::GuildWhat::Disband:
            if (guild == nullptr) return tell("You are not in a guild.");
            if (!masterOf(*guild, one.token)) return tell("Only the guild master can disband it.");
            // The name typed, as the window asks: a slip cannot end a guild.
            if (lower(ask.name) != lower(guild->name)) return tell("Type the guild's name to disband it.");
            disband(*guild, sessions);
            return;
        case net::GuildWhat::Notice: {
            if (guild == nullptr) return tell("You are not in a guild.");
            if (!masterOf(*guild, one.token)) return tell("Only the guild master sets the notice.");
            std::string text = net::saidText(ask.text);
            if (text.size() > kGuildNoticeMost) text.resize(kGuildNoticeMost);
            // Cut on a whole UTF-8 letter.
            while (!text.empty() && (uint8_t(text.back()) & 0xC0) == 0x80) text.pop_back();
            if (!text.empty() && (uint8_t(text.back()) & 0x80) != 0) text.pop_back();
            guild->notice = text;
            keep(*guild);
            if (!text.empty()) line(*guild, "Notice: " + text, sessions);
            sendGuild(*guild, sessions);
            return;
        }
    }
}

// Each second: calls run out, each member online seen and his level read, and the state owed
// sent -- every guild's every five seconds, for where its members are.
void guildTick(std::vector<std::unique_ptr<Session>>& sessions) {
    const auto now = Clock::now();
    for (auto it = g_guildInvites.begin(); it != g_guildInvites.end();) {
        if (now - it->second.at <= kGuildCallFor) {
            ++it;
            continue;
        }
        if (Session* master = sessionOfToken(it->second.from, sessions)) {
            chatSystem(*master, net::Channel::Guild, "Your invitation to " + it->second.name + " ran out.");
        }
        it = g_guildInvites.erase(it);
    }
    for (auto it = g_guildAsks.begin(); it != g_guildAsks.end();) {
        if (now - it->second.at <= kGuildCallFor) {
            ++it;
            continue;
        }
        if (Session* asker = sessionOfToken(it->first, sessions)) {
            const Guild* guild = calledGuild(it->second);
            chatSystem(*asker, net::Channel::Guild,
                       "Your request to join " + (guild ? guild->name : std::string("the guild")) + " ran out.");
        }
        it = g_guildAsks.erase(it);
    }
    static int beat = 0;
    const bool all = ++beat % 5 == 0;
    const int64_t seen = unixNow();
    for (auto& [id, guild] : g_guilds) {
        bool anyone = false;
        for (Guild::Member& m : guild.members) {
            Session* one = sessionOfToken(m.token, sessions);
            if (one == nullptr) continue;
            anyone = true;
            m.seen = seen;
            if (const sim::Body* him = bodyOf(*one)) {
                if (him->level != m.level || int(him->kin) != m.kin) {
                    m.level = him->level;
                    m.kin = int(him->kin);
                    guild.dirty = true;
                }
            }
        }
        if (guild.dirty || (all && anyone)) {
            // Its row when it changed, and each minute for when its members were last seen.
            if (guild.dirty || beat % 60 == 0) keep(guild);
            sendGuild(guild, sessions);
            guild.dirty = false;
        }
    }
}

void guildLeave(uint64_t token, std::vector<std::unique_ptr<Session>>& sessions) {
    const auto at = g_guilds.find(guildOf(token));
    if (at != g_guilds.end()) out(at->second, token, false, sessions);
}

}  // namespace mu::server
