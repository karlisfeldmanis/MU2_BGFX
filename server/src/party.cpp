// Parties: invites, members, the party's line and its state sent to each member. Split out of main.cpp (host.h).

#include "host.h"

namespace mu::server {


// The welcomed sessions in his party, himself first, on any map. The party's section owns this
// (docs/party.md); until a party exists he is a party of one.
std::vector<Session*> partyMates(Session& one, std::vector<std::unique_ptr<Session>>& sessions) {
    const auto at = g_partyOf.find(one.token);
    if (at == g_partyOf.end()) return {&one};
    std::vector<Session*> out{&one};
    for (const Party::Member& m : g_parties[at->second].members) {
        if (m.token == one.token) continue;
        for (auto& other : sessions) {
            if (other->token == m.token && other->welcomed && other->socket.open()) {
                out.push_back(other.get());
                break;
            }
        }
    }
    return out;
}

// ---- the party (docs/party.md): its asks and its upkeep ----------------------------------------

// The welcomed connection his character is on, or nullptr.
Session* sessionOfToken(uint64_t token, std::vector<std::unique_ptr<Session>>& sessions) {
    for (auto& one : sessions) {
        if (one->token == token && one->welcomed && one->socket.open() && one->world != nullptr) {
            return one.get();
        }
    }
    return nullptr;
}

// His realm told his party: the next tick of his world carries it.
void partySet(Session& one, uint32_t party) {
    if (!one.welcomed || one.world == nullptr || one.player == 0) return;
    // Asked as his own, so every mirror finds the same asker: player 0 is whoever is first in
    // that realm, which a mirror and the server do not agree on (a divergence at the first hash).
    one.world->queued.push_back({.kind = sim::Command::Kind::PartySet, .player = one.player,
                                 .a = int(party), .target = one.player});
}

// A line on the Party channel to every member on a connection.
void partyLine(const Party& party, const std::string& text, std::vector<std::unique_ptr<Session>>& sessions) {
    for (const Party::Member& m : party.members) {
        if (Session* one = sessionOfToken(m.token, sessions)) chatSystem(*one, net::Channel::Party, text);
    }
}

void sendPartyState(Session& to, const net::PartyState& state) {
    if (!to.welcomed || !to.socket.open()) return;
    std::vector<uint8_t> out;
    net::put(out, state);
    if (!to.socket.send(out)) to.socket.close();
}

// The bar's state to every member: each member's level, class, map and health as his own realm
// has him, his body only to those in his world.
void sendParty(const Party& party, std::vector<std::unique_ptr<Session>>& sessions) {
    for (const Party::Member& to : party.members) {
        Session* hearer = sessionOfToken(to.token, sessions);
        if (hearer == nullptr) continue;
        net::PartyState state;
        for (const Party::Member& m : party.members) {
            net::PartyState::Member out;
            out.name = m.name;
            if (Session* one = sessionOfToken(m.token, sessions)) {
                out.world = one->world->name;
                out.bot = one->bot;
                out.here = one->world == hearer->world ? one->player : 0;
                if (const sim::Body* him = one->world->realm->find(one->player)) {
                    out.kin = uint8_t(him->kin);
                    out.level = him->level;
                    out.health = uint8_t(std::clamp(him->maxHealth > 0 ? 100 * him->health / him->maxHealth : 0, 0, 100));
                    out.mana = uint8_t(std::clamp(him->maxMana > 0 ? 100 * him->mana / him->maxMana : 0, 0, 100));
                    out.shield = him->maxSd > 0 ? uint8_t(std::clamp(100 * him->sd / him->maxSd, 0, 100))
                                                : net::PartyState::kNoShield;
                    out.life = him->health;
                    out.lifeMost = him->maxHealth;
                }
            }
            state.members.push_back(std::move(out));
        }
        sendPartyState(*hearer, state);
    }
}

// One out of his party: told so, his realm told, the rest told why. A party of one left is no
// party; a leader gone hands it to the next.
void partyOut(uint64_t token, const std::string& why, std::vector<std::unique_ptr<Session>>& sessions) {
    const auto at = g_partyOf.find(token);
    if (at == g_partyOf.end()) return;
    Party& party = g_parties[at->second];
    g_partyOf.erase(at);
    g_partyBody.erase(token);
    g_partyAway.erase(token);
    std::string name;
    const bool leader = !party.members.empty() && party.members.front().token == token;
    for (size_t i = 0; i < party.members.size(); ++i) {
        if (party.members[i].token != token) continue;
        name = party.members[i].name;
        party.members.erase(party.members.begin() + long(i));
        break;
    }
    if (Session* one = sessionOfToken(token, sessions)) {
        partySet(*one, 0);
        sendPartyState(*one, {});
        chatSystem(*one, net::Channel::Party, why == "was kicked" ? "You were removed from the party." : "You left the party.");
    }
    partyLine(party, name + " " + (why.empty() ? "has left the party." : why + " from the party."), sessions);
    core::logf("party %u: %s %s", party.id, name.c_str(), why.empty() ? "left" : why.c_str());
    if (party.members.size() < 2) {
        for (const Party::Member& m : party.members) {
            g_partyOf.erase(m.token);
            g_partyBody.erase(m.token);
            g_partyAway.erase(m.token);
            if (Session* one = sessionOfToken(m.token, sessions)) {
                partySet(*one, 0);
                sendPartyState(*one, {});
                chatSystem(*one, net::Channel::Party, "The party has been disbanded.");
            }
        }
        g_parties.erase(party.id);
        return;
    }
    if (leader) partyLine(party, party.members.front().name + " is now the party leader.", sessions);
    sendParty(party, sessions);
}

// What he asked of the party (net::PartyAsk), answered on the Party channel.
void partyAsk(Session& one, const net::PartyAsk& ask, std::vector<std::unique_ptr<Session>>& sessions) {
    const auto tell = [&](const std::string& text) { chatSystem(one, net::Channel::Party, text); };
    const auto now = Clock::now();
    const auto mine = g_partyOf.find(one.token);
    Party* party = mine != g_partyOf.end() ? &g_parties[mine->second] : nullptr;
    switch (ask.what) {
        case net::PartyWhat::Invite: {
            Session* them = playerNamed(ask.name, sessions);
            if (them == nullptr) return tell(ask.name + " is not online.");
            if (them == &one) return tell("You cannot invite yourself.");
            if (g_partyOf.count(them->token)) return tell(them->name + " is already in a party.");
            if (party != nullptr && party->members.front().token != one.token) {
                return tell("Only the party leader can invite.");
            }
            if (party != nullptr && int(party->members.size()) >= sim::kPartyMost) return tell("Your party is full.");
            g_invites[them->token] = {one.token, now};
            chatSystem(*them, net::Channel::Party, one.name + " invites you to a party. Type /accept or /decline.");
            tell("You invited " + them->name + " to the party.");
            return;
        }
        case net::PartyWhat::Accept: {
            const auto invite = g_invites.find(one.token);
            if (invite == g_invites.end() || now - invite->second.at > kInviteFor) {
                return tell("You have no party invitation.");
            }
            const uint64_t from = invite->second.from;
            Session* inviter = sessionOfToken(from, sessions);
            // "/accept NAME" takes NAME's invitation and no other: a second ask that came in
            // between put a bot waiting on his own party's leader into a stranger's.
            if (!ask.name.empty() && inviter != nullptr && playerNamed(ask.name, sessions) != inviter) {
                return tell("You have no party invitation from " + ask.name + ".");
            }
            g_invites.erase(invite);
            if (party != nullptr) return tell("You are already in a party.");
            if (inviter == nullptr) return tell("Whoever invited you is no longer online.");
            const auto theirs = g_partyOf.find(from);
            Party* joined = theirs != g_partyOf.end() ? &g_parties[theirs->second] : nullptr;
            // Asked by a leader who has since left his party or joined another's as a member:
            // his invitation was his party's, and is no longer his to give.
            if (joined != nullptr && joined->members.front().token != from) {
                return tell(inviter->name + " no longer leads a party.");
            }
            if (joined != nullptr && int(joined->members.size()) >= sim::kPartyMost) return tell("That party is full.");
            if (joined == nullptr) {
                Party made;
                made.id = g_nextParty++;
                made.members.push_back({from, inviter->name});
                joined = &(g_parties[made.id] = made);
                g_partyOf[from] = made.id;
                core::logf("party %u: made by %s", made.id, inviter->name.c_str());
            }
            joined->members.push_back({one.token, one.name});
            g_partyOf[one.token] = joined->id;
            core::logf("party %u: %s joins (%zu)", joined->id, one.name.c_str(), joined->members.size());
            // Their realms are told at the next upkeep (partyTick), which finds each body.
            partyLine(*joined, one.name + " has joined the party.", sessions);
            sendParty(*joined, sessions);
            return;
        }
        case net::PartyWhat::Decline: {
            const auto invite = g_invites.find(one.token);
            if (invite == g_invites.end()) return tell("You have no party invitation.");
            if (Session* inviter = sessionOfToken(invite->second.from, sessions)) {
                chatSystem(*inviter, net::Channel::Party, one.name + " declined your invitation.");
            }
            g_invites.erase(invite);
            return tell("You declined the invitation.");
        }
        case net::PartyWhat::Leave:
            if (party == nullptr) return tell("You are not in a party.");
            partyOut(one.token, "", sessions);
            return;
        case net::PartyWhat::Kick: {
            if (party == nullptr) return tell("You are not in a party.");
            if (party->members.front().token != one.token) return tell("Only the party leader can remove members.");
            for (const Party::Member& m : party->members) {
                std::string a = m.name, b = ask.name;
                for (char& c : a) c = char(std::tolower(uint8_t(c)));
                for (char& c : b) c = char(std::tolower(uint8_t(c)));
                if (a != b || m.token == one.token) continue;
                const uint64_t token = m.token;
                partyOut(token, "was kicked", sessions);
                return;
            }
            return tell(ask.name + " is not in your party.");
        }
        case net::PartyWhat::Promote: {
            if (party == nullptr) return tell("You are not in a party.");
            if (party->members.front().token != one.token) return tell("Only the party leader can choose another.");
            for (size_t i = 1; i < party->members.size(); ++i) {
                std::string a = party->members[i].name, b = ask.name;
                for (char& c : a) c = char(std::tolower(uint8_t(c)));
                for (char& c : b) c = char(std::tolower(uint8_t(c)));
                if (a != b) continue;
                // The leader is the first; the old one steps to second.
                std::swap(party->members[0], party->members[i]);
                core::logf("party %u: %s hands it to %s", party->id, one.name.c_str(), party->members[0].name.c_str());
                partyLine(*party, party->members.front().name + " is now the party leader.", sessions);
                sendParty(*party, sessions);
                return;
            }
            return tell(ask.name + " is not in your party.");
        }
    }
}

// Each second: every member whose body in a realm is new (he came in, or changed maps) put in
// his party there; a member on no connection for kPartyAway let go; and the bar's state to all.
void partyTick(std::vector<std::unique_ptr<Session>>& sessions) {
    const auto now = Clock::now();
    std::vector<uint64_t> gone;
    for (auto& [id, party] : g_parties) {
        for (const Party::Member& m : party.members) {
            Session* one = sessionOfToken(m.token, sessions);
            if (one == nullptr) {
                const auto away = g_partyAway.try_emplace(m.token, now).first;
                if (now - away->second > kPartyAway) gone.push_back(m.token);
                continue;
            }
            g_partyAway.erase(m.token);
            const std::pair<const World*, uint32_t> body{one->world, one->player};
            const auto told = g_partyBody.find(m.token);
            if (told == g_partyBody.end() || told->second != body) {
                partySet(*one, id);
                g_partyBody[m.token] = body;
            }
        }
    }
    for (const uint64_t token : gone) partyOut(token, "went away", sessions);
    for (auto& [id, party] : g_parties) sendParty(party, sessions);
    for (auto it = g_invites.begin(); it != g_invites.end();) {
        it = now - it->second.at > kInviteFor ? g_invites.erase(it) : std::next(it);
    }
}

}  // namespace mu::server
