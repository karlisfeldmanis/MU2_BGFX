// Chat: a line heard, said to a channel (Local by distance, Global to the server), a whisper, the rate a player may talk at. Split out of main.cpp (host.h).

#include <cmath>

#include "host.h"
#include "market_link.h"

namespace mu::server {


void hearLine(Session& one, const net::Heard& line) {
    if (!one.welcomed || !one.socket.open()) return;
    std::vector<uint8_t> out;
    net::put(out, line);
    if (!one.socket.send(out)) one.socket.close();
}

// The server's own line to one player, on a channel: "Valeria has joined the party." on Party.
void chatSystem(Session& one, net::Channel channel, const std::string& text) {
    net::Heard line;
    line.channel = channel;
    line.text = text;
    hearLine(one, line);
}

void heraldHere(const std::string& text, std::vector<std::unique_ptr<Session>>& sessions) {
    net::Heard line;
    line.channel = net::Channel::Global;
    line.text = text;
    int heard = 0;
    for (auto& other : sessions) {
        if (!other->welcomed || other->world == nullptr) continue;
        hearLine(*other, line);
        ++heard;
    }
    core::logf("chat global herald (%d heard): %s", heard, text.c_str());
}

void announceRebirth(const std::string& name, int count, std::vector<std::unique_ptr<Session>>& sessions) {
    const std::string text = net::rebirthLine(name, count, g_region);
    heraldHere(text, sessions);
    // And every other realm, over the market's line (market_link.h, marketHerald).
    marketHerald(text);
}

void welcomeNewcomer(Session& one, std::vector<std::unique_ptr<Session>>& sessions) {
    if (!one.newcomer || one.world == nullptr) return;
    one.newcomer = false;
    const sim::Body* him = one.world->realm->find(one.player);
    if (him == nullptr) return;
    heraldHere(net::newcomerLine(one.name, sim::className(int(him->kin), false), one.world->name), sessions);
}

// The welcomed player called `name` on any map, in any case, or nullptr.
Session* playerNamed(const std::string& name, std::vector<std::unique_ptr<Session>>& sessions) {
    const auto lower = [](std::string s) {
        for (char& c : s) c = char(std::tolower(uint8_t(c)));
        return s;
    };
    const std::string wanted = lower(name);
    for (auto& one : sessions) {
        if (one->welcomed && one->socket.open() && lower(one->name) == wanted) return one.get();
    }
    return nullptr;
}

// Whether anyone on the server but `except`, in a world or on his way into one, goes by `name`,
// in any case: a name is the whole of how chat, whispers and invites find a player.
bool nameInUse(const std::string& name, const std::vector<std::unique_ptr<Session>>& sessions,
               const Session* except) {
    const auto lower = [](std::string s) {
        for (char& c : s) c = char(std::tolower(uint8_t(c)));
        return s;
    };
    const std::string wanted = lower(name);
    for (const auto& one : sessions) {
        if (one.get() != except && one->world != nullptr && one->socket.open() && lower(one->name) == wanted) {
            return true;
        }
    }
    return false;
}

// Whether he may say another line now: the allowance refilled since his last, and one spent.
bool sayAllowed(Session& one) {
    const Clock::time_point now = Clock::now();
    if (one.saidAt == Clock::time_point{}) one.sayAllowance = kSayBurst;
    const float since = std::chrono::duration<float>(now - one.saidAt).count();
    one.sayAllowance = std::min(kSayBurst, one.sayAllowance + since / kSayEvery);
    one.saidAt = now;
    if (one.sayAllowance < 1.0f) return false;
    one.sayAllowance -= 1.0f;
    return true;
}

// One Say: to whom it goes, and as what each hears it.
void say(Session& one, const net::Say& said, std::vector<std::unique_ptr<Session>>& sessions) {
    const std::string text = net::saidText(said.text);
    if (text.empty() || one.player == 0) return;
    if (!sayAllowed(one)) {
        chatSystem(one, net::Channel::Server, "You are speaking too quickly.");
        return;
    }
    net::Heard line;
    line.channel = said.channel;
    line.name = one.name;
    line.text = text;
    line.bot = one.bot;
    // Who hears it, and the speaker's body where the hearer can see him.
    const auto tell = [&](Session& hearer) {
        line.from = hearer.world == one.world ? one.player : 0;
        hearLine(hearer, line);
    };
    switch (said.channel) {
        case net::Channel::Local: {
            // Those on his map within net::kLocalReach tiles of him, himself among them.
            const sim::Body* him = one.world->realm->find(one.player);
            if (him == nullptr) return;
            int heard = 0;
            for (auto& other : sessions) {
                if (other->world != one.world || other->player == 0) continue;
                const sim::Body* them = one.world->realm->find(other->player);
                if (them == nullptr) continue;
                const float dx = them->x - him->x, dy = them->y - him->y;
                if (dx * dx + dy * dy > net::kLocalReach * net::kLocalReach) continue;
                tell(*other);
                ++heard;
            }
            core::logf("chat local %s/%d %s (%d heard): %s", one.world->name.c_str(), one.world->copy,
                       one.name.c_str(), heard, text.c_str());
            break;
        }
        case net::Channel::Global: {
            // Everyone on the server, any map: from level net::kGlobalLevel, a line each
            // net::kGlobalEvery seconds.
            const sim::Body* him = one.world->realm->find(one.player);
            if (him == nullptr || him->level < net::kGlobalLevel) {
                chatSystem(one, net::Channel::Server,
                           "Global opens at level " + std::to_string(net::kGlobalLevel) + ".");
                return;
            }
            const Clock::time_point now = Clock::now();
            const float since = std::chrono::duration<float>(now - one.globalAt).count();
            if (one.globalAt != Clock::time_point{} && since < net::kGlobalEvery) {
                const int wait = int(std::ceil(net::kGlobalEvery - since));
                chatSystem(one, net::Channel::Server, "Wait " + std::to_string(wait) + " s to speak on Global.");
                return;
            }
            one.globalAt = now;
            int heard = 0;
            for (auto& other : sessions) {
                if (!other->welcomed || other->world == nullptr) continue;
                tell(*other);
                ++heard;
            }
            core::logf("chat global %s (%d heard): %s", one.name.c_str(), heard, text.c_str());
            break;
        }
        case net::Channel::Map:
            for (auto& other : sessions) {
                if (other->world == one.world) tell(*other);
            }
            core::logf("chat %s/%d %s: %s", one.world->name.c_str(), one.world->copy, one.name.c_str(),
                       text.c_str());
            break;
        case net::Channel::Party: {
            const std::vector<Session*> mates = partyMates(one, sessions);
            if (mates.size() < 2) {
                chatSystem(one, net::Channel::Party, "You are not in a party.");
                return;
            }
            for (Session* mate : mates) tell(*mate);
            core::logf("chat party %s: %s", one.name.c_str(), text.c_str());
            break;
        }
        case net::Channel::Guild: {
            // His guild's members online, any map (docs/game/guild.md).
            const std::vector<Session*> mates = guildMates(one, sessions);
            if (mates.empty()) {
                chatSystem(one, net::Channel::Guild, "You are not in a guild.");
                return;
            }
            for (Session* mate : mates) tell(*mate);
            core::logf("chat guild %s: %s", one.name.c_str(), text.c_str());
            break;
        }
        case net::Channel::Whisper: {
            Session* to = playerNamed(said.to, sessions);
            if (to == nullptr) {
                chatSystem(one, net::Channel::Whisper, net::saidText(said.to.substr(0, 16)) + " is not here.");
                return;
            }
            if (to == &one) return;
            tell(*to);
            line.to = to->name;  // and his own copy back: "To Valeria"
            tell(one);
            core::logf("chat whisper %s > %s: %s", one.name.c_str(), to->name.c_str(), text.c_str());
            break;
        }
        case net::Channel::Server:
            return;  // the server's alone
    }
}

}  // namespace mu::server
