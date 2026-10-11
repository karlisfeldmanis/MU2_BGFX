// Chat: a line heard, said to a channel, a whisper, the rate a player may talk at. Split out of main.cpp (host.h).

#include "host.h"

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
        case net::Channel::Guild:
            // No guilds yet (docs/server-plan.md phase 7).
            chatSystem(one, net::Channel::Guild, "You are not in a guild.");
            return;
        case net::Channel::Whisper: {
            Session* to = playerNamed(said.to, sessions);
            if (to == nullptr) {
                chatSystem(one, net::Channel::Whisper, net::saidText(said.to).substr(0, 16) + " is not here.");
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
