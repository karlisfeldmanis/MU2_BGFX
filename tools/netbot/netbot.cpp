// The net bot: characters played on the server, the way a client plays them -- a Hello, the
// Welcome, a mirror stepped with every tick the server sends, and nothing asked but the commands a
// player's clicks become (sim/command.h). Whoever plays on the same server sees them hunt, shop,
// gear up, do their quests and travel.
//
// The brain is tools/bot's (tools/bot/brain.h), the one measured offline for hours in seconds.
// Each decision is made on a scratch realm laid from the mirror's snapshot, so the brain reads its
// own asks' answers at once as it does offline; what it asked goes to the server as commands, the
// server decides, and the next decision starts again from the mirror. A decision waits for the
// last one's answers, so a purchase is never asked twice. A gate, a paid trip, Go Back! or a
// death far from a town is followed as the client follows it: the line closes and he comes again
// with his token, and the server says where he is.
//
// And each says a little on the chat, and answers a whisper (talk.h): what he hears and says is
// in his log.
//
// Each bot keeps its character's token in --dir (saves/bots by default), so a bot run again comes
// back as the same character, at the level it left.
//
//   build/netbot [--server HOST] [--port N] [--dir DIR] [--hours H] [--no-quests] [--chatty] [--party]
//                [--parties] [--threads N] [--claude DIR] NAME:CLASS ...
//
// CLASS is dk, dw, elf, eelf (the energy elf, her points to energy for heals and buffs) or mg (a
// new elf is born in Noria). --party makes the run's bots one party
// (docs/party.md), the first named its leader; --parties lets them form their own, for the quests
// they cannot take alone (Board). --claude DIR makes the first named the Claude
// bot (claude.h): his chat and his choice of ground are the model's, through files in DIR.

#include <algorithm>
#include <chrono>
#include <csignal>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <mutex>
#include <thread>

#include "../bot/brain.h"
#include "claude.h"
#include "talk.h"
#include "game/remote_link.h"
#include "net/wire.h"
#include "sim/cradle.h"
#include "sim/maps.h"

namespace {

volatile std::sig_atomic_t g_stop = 0;
void onSignal(int) { g_stop = 1; }

constexpr int kThinkNet = 5;            // ticks between decisions, as offline
// A decision's answers not heard in two seconds: think anyway. **The wall's seconds, not the
// game's** (mu2_server --speed): at --speed 100 the game's two were 20 ms, under a round trip, and
// a bot asked again on a mirror that had not heard the answer -- one elf handed Noria's Song in
// 82 times and opened its Box of Luck as often, every ask after the first refused.
constexpr auto kAnswerWait = std::chrono::seconds(2);
// But a paid trip's answer is waited for this long: thought again after two seconds, the brain --
// still on the old map in the scratch -- paid the trip a second time, and the first's answer,
// its ticket gone with the last decision's commands, was never read as the trip (the Claude bot's
// run at --speed 100: 7 trips paid twice in six game hours, 2000-3000 zen each).
// Wall time, not ticks: under mu2_server --speed N ten game seconds are 10/N real ones, under
// one round trip on a loaded Mac (mu2-bgfx-64's note, as for kAnswerWait).
constexpr auto kTripWait = std::chrono::seconds(10);

class NetBot {
public:
    NetBot(std::string name, sim::Kin kin, std::string host, int port, std::string dir, bool quests,
           bool chatty, bool support = false)
        : name_(std::move(name)), kin_(kin), host_(std::move(host)), port_(port), dir_(std::move(dir)),
          talk_(name_) {
        Options options;
        options.kin = kin;
        options.quests = quests;
        options.support = support;
        options.tag = name_ + " ";
        talk_.setChatty(chatty);
        brain_ = std::make_unique<Bot>(options, 0);
        loadToken();
    }

    // Joins his world (the server may answer that he is elsewhere: then that one), raises the
    // mirror and its scratch from the Welcome as the client does (game/play_open.cpp), and
    // replays the world's past.
    bool connect() {
        std::string world = world_;
        for (int tries = 0; tries < 3; ++tries) {
            mirror_ = std::make_unique<sim::Realm>();
            scratch_ = std::make_unique<sim::Realm>();
            link_ = std::make_unique<game::RemoteLink>(*mirror_);
            net::Hello hello;
            hello.world = world;
            hello.kin = uint8_t(kin_);
            hello.token = token_;
            // What everyone else's hover plate calls him (net::Who).
            hello.name = name_;
            net::Welcome welcome;
            net::Elsewhere elsewhere;
            if (!link_->join(host_, port_, hello, welcome, &elsewhere)) {
                if (!elsewhere.world.empty()) {
                    world = elsewhere.world;
                    continue;
                }
                line("could not join %s:%d", host_.c_str(), port_);
                return false;
            }
            world_ = welcome.world;
            token_ = welcome.token;
            saveToken();
            const content::Tables* tables = tablesOf(world_);
            if (!tables) return false;
            for (sim::Realm* realm : {mirror_.get(), scratch_.get()}) {
                realm->configure(welcome.config);
                if (!realm->raise(tables, welcome.seed, welcome.column, welcome.row, sim::Kin(welcome.kin),
                                  welcome.level)) {
                    line("%s would not raise", world_.c_str());
                    return false;
                }
                if (welcome.castle > 0) realm->setCastle(welcome.castle);
                sim::outfit(*realm, welcome.weapon, welcome.shield);
                if (welcome.kept) realm->restoreKept(welcome.first);
            }
            link_->catchUp();
            you_ = mirror_->hero().id;
            pending_ = 0;
            trips_.clear();  // a new line, a new world: the old one's tickets answer nothing here
            brain_->attach(*mirror_, tables);
            const sim::Body& me = mirror_->hero();
            line("in %s as #%u, %s level %d with %lld zen, %d players here", world_.c_str(), me.id,
                 kinName(me.kin), me.level, (long long)mirror_->money(), mirror_->playersHere());
            return true;
        }
        return false;
    }

    // The network in and out, every tick the server has sent stepped, and a decision when one is
    // due. False once the line is gone or he has gone to another world.
    bool update() {
        if (!link_ || !link_->connected()) return false;
        link_->pump();
        chat();
        while (link_->due()) {
            link_->step();
            brain_->heardOn(*mirror_);
            if (!claude_) talk_.happened(*mirror_, you_, brain_->clockNow());
            if (claude_ && brain_->clockNow() % 20 == 0) drive();
            if (leaving()) {
                link_.reset();  // closes the line; the next connect asks where he is
                return false;
            }
            if (brain_->clockNow() % kThinkNet == 0) think();
        }
        if (link_->divergedAt() != 0 && !divergenceSaid_) {
            divergenceSaid_ = true;
            line("!! the mirror diverged from the server at tick %u", link_->divergedAt());
        }
        return link_->connected();
    }

    void report() {
        if (mirror_ && link_) brain_->status();
    }

    // ---- the party (docs/party.md): what the board (Board, below) reads and says ----------
    const std::string& name() const { return name_; }
    sim::Kin kin() const { return kin_; }
    bool up() const { return link_ && link_->connected() && mirror_; }
    // The Claude bot plays his own party (claude.h): the board leaves him be.
    bool driven() const { return claude_ != nullptr; }
    int level() const { return mirror_->hero().level; }
    int64_t clockNow() const { return brain_->clockNow(); }
    const std::string& world() const { return world_; }
    std::vector<Bot::Need> needs() { return brain_->needs(); }
    bool helps(int map, int breed) { return brain_->helps(map, breed); }
    int castlesOpen() const { return brain_->castlesOpen(); }
    bool wantsWings() const { return brain_->wantsWings(); }
    const std::vector<Bot::Ground>& grounds() { return brain_->grounds(); }
    bool castleTime() const { return brain_->castleTime(); }
    bool inCastle() const { return brain_->inCastle(); }
    const net::PartyState& partyState() const { return link_->party(); }
    void askParty(net::PartyWhat what, const std::string& whom) { link_->askParty({what, whom}); }
    void sayOn(net::Channel channel, const std::string& text) {
        line("~ says: %s", text.c_str());
        link_->say({channel, {}, text});
    }
    // The leader whose invite he takes, "" for none; and whether, with none, he takes anyone's.
    void expect(const std::string& leader) { expected_ = leader; }
    void openToAll(bool open) { openToAll_ = open; }
    // The party's hunt, its leader and tank by name: found on his own map, else none.
    void lead(Bot::Lead lead, const std::string& leader, const std::string& tank) {
        lead.leader = idOf(leader);
        lead.tank = idOf(tank);
        brain_->lead(std::move(lead));
    }

    // --claude DIR: he is the Claude bot (claude.h).
    void driveFrom(const std::string& dir) { claude_ = std::make_unique<bot::Claude>(dir); }

private:
    // A player's body on his mirror by the name the server gave it, or 0.
    uint32_t idOf(const std::string& who) const {
        if (who.empty()) return 0;
        if (who == name_) return you_;
        for (const sim::Body& b : mirror_->bodies()) {
            std::string name;
            bool isBot = false;
            if (b.player && link_->nameOf(b.id, &name, &isBot) && name == who) return b.id;
        }
        return 0;
    }

    // **The Claude bot**, each second: the model's orders carried out, and the state it reads.
    void drive() {
        const int64_t now = brain_->clockNow();
        for (const std::string& order : claude_->orders()) {
            claude_->note(clock(now), "> " + order);
            const size_t space = order.find(' ');
            const std::string verb = order.substr(0, space);
            const std::string rest = space == std::string::npos ? "" : order.substr(space + 1);
            const size_t space2 = rest.find(' ');
            const std::string first = rest.substr(0, space2);
            const std::string after = space2 == std::string::npos ? "" : rest.substr(space2 + 1);
            std::string answer;
            if (verb == "say" && !rest.empty()) {
                link_->say({net::Channel::Map, {}, rest});
            } else if (verb == "party" && !rest.empty()) {
                link_->say({net::Channel::Party, {}, rest});
            } else if (verb == "whisper" && !after.empty()) {
                link_->say({net::Channel::Whisper, first, after});
            } else if (verb == "invite" && !first.empty()) {
                link_->askParty({net::PartyWhat::Invite, first});
            } else if (verb == "accept") {  // no name: the latest invite
                link_->askParty({net::PartyWhat::Accept, first});
            } else if (verb == "decline") {  // no name: the latest invite
                link_->askParty({net::PartyWhat::Decline, first});
            } else if (verb == "kick" && !first.empty()) {
                link_->askParty({net::PartyWhat::Kick, first});
            } else if (verb == "leave") {
                link_->askParty({net::PartyWhat::Leave, {}});
            } else if (verb == "auto") {
                brain_->order(-1);
                answer = "the brain chooses again";
            } else if (verb == "go") {
                int map = -1;
                for (const WorldRow& w : kWorlds) {
                    if (first == w.name) map = w.map;
                }
                answer = map < 0 ? "no world " + first
                         : brain_->order(map) ? "grinds " + first
                                              : first + " is out of his reach";
            } else {
                answer = "not an order: " + order;
            }
            if (!answer.empty()) claude_->note(clock(now), "  " + answer);
        }
        claude_->state(stateText());
    }

    std::string stateText() const {
        const sim::Body& me = mirror_->hero();
        const content::Tables& tables = *tables_.at(world_);
        std::string out;
        char text[512];
        const auto put = [&](const char* fmt, auto... args) {
            std::snprintf(text, sizeof(text), fmt, args...);
            out += text;
        };
        put("%s, %s level %d -- clock %s\n", name_.c_str(), kinName(me.kin), me.level, clock(brain_->clockNow()).c_str());
        put("health %d/%d  mana %d/%d  zen %lld  points to spend %d\n", me.health, me.maxHealth, me.mana,
            me.maxMana, (long long)mirror_->money(), me.pointsInHand);
        put("in %s at %d,%d%s\n", world_.c_str(), me.column(), me.row(), me.alive() ? "" : " -- DEAD");
        put("brain: %s%s; %d kills, %d deaths this run\n", brain_->aimLine().c_str(),
            brain_->ordered() >= 0 ? " (ordered)" : "", brain_->kills(), brain_->deaths());
        int potions = 0, carried = 0;
        out += "wears:";
        for (int slot = 0; slot < sim::kSlots; ++slot) {
            const sim::Held& one = mirror_->satchel()[slot];
            if (one.empty()) continue;
            if (slot >= sim::kWorn) ++carried;
            const content::ItemRow& row = tables.items[size_t(one.item)];
            if (slot < sim::kWorn) put(" %s%s", row.label.c_str(), one.refinement > 0 ? ("+" + std::to_string(one.refinement)).c_str() : "");
            else if (sim::heals(row)) ++potions;
        }
        put("\nbag: %d things, %d of them healing potions\n", carried, potions);
        // What is near: players by name, monsters by breed, nearest first.
        struct Near {
            float d;
            std::string what;
        };
        std::vector<Near> near;
        for (const sim::Body& b : mirror_->bodies()) {
            if (b.id == me.id || !b.alive()) continue;
            const float dx = b.x - me.x, dy = b.y - me.y, d = std::sqrt(dx * dx + dy * dy);
            if (d > 20.0f) continue;
            if (b.player) {
                std::string who;
                bool isBot = false;
                if (!link_->nameOf(b.id, &who, &isBot)) who = "#" + std::to_string(b.id);
                near.push_back({d, "player " + who + (isBot ? " (bot)" : "") + " level " + std::to_string(b.level)});
            } else if (b.monster() && b.kind >= 0) {
                const content::MonsterKind& kind = tables.kinds[size_t(b.kind)];
                near.push_back({d, kind.label + " level " + std::to_string(kind.level) + " " +
                                       std::to_string(b.health) + "/" + std::to_string(b.maxHealth)});
            }
        }
        std::sort(near.begin(), near.end(), [](const Near& a, const Near& b) { return a.d < b.d; });
        out += "near (20 tiles):\n";
        for (size_t i = 0; i < near.size() && i < 10; ++i) put("  %4.1f  %s\n", near[i].d, near[i].what.c_str());
        if (near.size() > 10) put("  ... %zu more\n", near.size() - 10);
        put("players on this map: %d\n", mirror_->playersHere());
        out += "quests:\n";
        const char* states[] = {"untaken", "active", "ready to hand in", "resting"};
        for (int q = 0; q < sim::kQuests; ++q) {
            const sim::QuestProgress& p = mirror_->quest(q);
            if (p.state == sim::QuestState::Active || p.state == sim::QuestState::Ready) {
                put("  %s: %s\n", sim::questAt(q).title, states[std::min(int(p.state), 3)]);
            }
        }
        const net::PartyState& party = link_->party();
        out += "party:";
        if (party.members.empty()) out += " none";
        for (const net::PartyState::Member& m : party.members) {
            put(" %s (%d, %s)", m.name.c_str(), m.level, m.world.c_str());
        }
        out += "\n";
        return out;
    }

    void line(const char* fmt, ...) const __attribute__((format(printf, 2, 3))) {
        char text[512];
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(text, sizeof(text), fmt, args);
        va_end(args);
        std::printf("  [%s] %s %s\n", clock(brain_->clockNow()).c_str(), name_.c_str(), text);
        std::fflush(stdout);
    }

    const content::Tables* tablesOf(const std::string& world) {
        auto it = tables_.find(world);
        if (it != tables_.end()) return it->second.get();
        auto tables = std::make_unique<content::Tables>();
        const std::string path = std::string(MU2_ASSET_DIR) + "/cooked/" + world + "/" + world + ".mur";
        std::string error;
        if (!content::loadTables(path, *tables, error)) {
            line("no tables for %s: %s", world.c_str(), error.c_str());
            return nullptr;
        }
        return (tables_[world] = std::move(tables)).get();
    }

    // His party as the server says it: logged as it changes; and the leader's invites.
    void party() {
        const net::PartyState& state = link_->party();
        std::string said;
        for (const net::PartyState::Member& m : state.members) {
            // Not the levels: a line each level of each member drowned the log.
            said += (said.empty() ? "" : ", ") + m.name + " (" + m.world + (m.here != 0 ? ", here" : "") + ")";
        }
        if (said != partySaid_) {
            line("party: %s", said.empty() ? "none" : said.c_str());
            partySaid_ = said;
        }
    }

    // The chat: what he heard since the last pump, and what he has to say by now.
    void chat() {
        heard_.clear();
        link_->takeHeard(heard_);
        const int64_t now = brain_->clockNow();
        party();
        for (const net::Heard& one : heard_) {
            // The invite the board told him to wait for, taken; and, while the board has him in
            // no party of its own, anyone's -- a player's /invite is answered (Board::follow).
            constexpr const char* kInvites = " invites you to a party.";
            if (one.name.empty() && one.channel == net::Channel::Party) {
                const size_t at = one.text.find(kInvites);
                const std::string from = at != std::string::npos ? one.text.substr(0, at) : "";
                if (!from.empty() && (from == expected_ || (expected_.empty() && openToAll_ && link_->party().members.empty()))) {
                    link_->askParty({net::PartyWhat::Accept, from});
                }
            }
            if (one.name.empty()) {
                line("~ %s", one.text.c_str());
            } else if (one.name != name_) {
                line("~ hears %s%s: %s", one.channel == net::Channel::Whisper ? "a whisper from " : "",
                     one.name.c_str(), one.text.c_str());
            }
            if (claude_) {
                const char* channels[] = {"map", "party", "guild", "whisper", "server"};
                claude_->note(clock(now), std::string(channels[std::min(int(one.channel), 4)]) + " " +
                                              (one.name.empty() ? "(server)" : one.name) +
                                              (one.bot ? " (bot)" : "") + (one.to.empty() ? "" : " to " + one.to) +
                                              ": " + one.text);
                continue;
            }
            talk_.heard(one, now);
        }
        if (claude_) return;
        talk_.idle(now);
        says_.clear();
        talk_.take(now, says_);
        for (const net::Say& one : says_) {
            line("~ says%s%s: %s", one.to.empty() ? "" : " to ", one.to.c_str(), one.text.c_str());
            link_->say(one);
        }
    }

    // A decision on the scratch, laid from the mirror as it stands, once the last one's answers
    // are in; what it asked, to the server.
    void think() {
        const int64_t now = brain_->clockNow();
        if (pending_ != 0 && std::chrono::steady_clock::now() - pendingSince_ < kAnswerWait) return;
        // Every one kept until its answer, however late: cleared at the wait's end, an answer 45
        // s late (a net bot behind its server) found no trip of his and he stayed, the server
        // holding him due elsewhere (the Claude bot's runs, 2026-10-09).
        if (!trips_.empty() && std::chrono::steady_clock::now() - tripSince_ < kTripWait) return;
        if (!mirror_->snapshot(bytes_) || !scratch_->restoreSnapshot(bytes_) || !scratch_->lookAs(you_)) {
            line("the scratch would not lay");
            return;
        }
        asked_.clear();
        const uint32_t before = brain_->lastTicket();
        brain_->thinkOn(*scratch_, asked_);
        brain_->look(*mirror_);
        for (const sim::Command& one : asked_) {
            link_->send(one);
            if (one.kind == sim::Command::Kind::Travel) {
                trips_.push_back(one);
                tripSince_ = std::chrono::steady_clock::now();
            }
        }
        if (brain_->lastTicket() != before) {
            pending_ = brain_->lastTicket();
            pendingSince_ = std::chrono::steady_clock::now();
        }
    }

    // Whether the tick sends him to another world, as the server reads it (server/src/main.cpp,
    // landingOf); and the last decision's answers heard.
    bool leaving() {
        // Blood Castle's run over and its rest out, or his win claimed: the server has him due in
        // Devias, and waits for the client to come (server/src/main.cpp, castleOut).
        if (brain_->mapNow() == int(sim::kBloodCastleMap) && mirror_->castleRun().sentOut) return true;
        for (const sim::Happening& h : mirror_->happenings()) {
            if (h.what == sim::What::Answered && h.who == you_ && uint32_t(h.c) == pending_) pending_ = 0;
            if (h.who != you_) continue;
            switch (h.what) {
                case sim::What::Gated:
                case sim::What::WentBack: return true;
                case sim::What::Warped:
                case sim::What::Rose:
                    if (h.c == 1) return true;
                    break;
                case sim::What::Answered:
                    if (h.a == int32_t(sim::Command::Kind::Travel) && h.b > 0) {
                        for (const sim::Command& one : trips_) {
                            if (one.kind != sim::Command::Kind::Travel || one.ticket != uint32_t(h.c)) continue;
                            const sim::TravelRow& to = sim::travelAt(one.a);
                            if (to.map != int32_t(brain_->mapNow())) return true;
                        }
                    }
                    // Answered, and not a trip away: no longer waited for.
                    std::erase_if(trips_, [&](const sim::Command& one) { return one.ticket == uint32_t(h.c); });
                    break;
                default: break;
            }
        }
        return false;
    }

    std::string tokenPath() const { return dir_ + "/" + name_ + ".token"; }
    void loadToken() {
        std::ifstream in(tokenPath());
        if (!(in >> std::hex >> token_ >> world_)) {
            token_ = 0;
            world_ = sim::homeWorld(kin_);
        }
    }
    void saveToken() const {
        std::error_code ignored;
        std::filesystem::create_directories(dir_, ignored);
        std::ofstream out(tokenPath());
        out << std::hex << token_ << " " << world_ << "\n";
    }

    std::string name_;
    sim::Kin kin_;
    std::string host_;
    int port_;
    std::string dir_;
    uint64_t token_ = 0;
    std::string world_;
    bot::Talk talk_;
    std::vector<net::Heard> heard_;
    std::vector<net::Say> says_;

    std::unique_ptr<Bot> brain_;
    std::map<std::string, std::unique_ptr<content::Tables>> tables_;
    std::unique_ptr<sim::Realm> mirror_, scratch_;
    std::unique_ptr<game::RemoteLink> link_;
    std::vector<uint8_t> bytes_;
    std::vector<sim::Command> asked_;
    std::vector<sim::Command> trips_;  // paid trips not yet answered (kTripWait)
    std::chrono::steady_clock::time_point tripSince_{};
    uint32_t you_ = 0;
    uint32_t pending_ = 0;
    std::chrono::steady_clock::time_point pendingSince_{};
    bool divergenceSaid_ = false;
    std::string expected_;  // the board's: whose invite he takes
    bool openToAll_ = false;  // ... and, with none, anyone's (Board::follow)
    std::string partySaid_;
    std::unique_ptr<bot::Claude> claude_;  // --claude: the model drives him
};

// **The board** (docs/party.md): the run's bots' parties, formed and led. The user, 2026-10-09:
// "lets implement party system, so bots can form 5-man party when they can kill some quest
// monsters, elfs can heal, dk can tank, and 2 dps", "it make sense they go together on
// dungeons", "we need to be smart about how they will fill quests, because some will need more
// kill some already will be finished some kills".
//
// Once a second it reads every bot's quests (Bot::needs) and the server's party state.
//  - **Forming** (--parties): a bot stuck on a quest's breed -- one he would take with a party
//    about him but not alone -- is put with the others stuck on that quest, a knight (or a melee
//    Magic Gladiator) to tank and an elf to heal if they are free and can stand there, and more
//    hands up to three. The tank leads; else the highest level. The leader invites, the others
//    take his invite. A bot stuck with no one to party says so on his map, now and then.
//  - **Leading**: the party hunts the breed the most of its members still want, on its map, and
//    every breed there that any still wants; a member done with it stays as the party's support.
//  - **Breaking up**: once no member is stuck on anything, each leaves (--party's one party,
//    the run's bots with the first named its leader, never breaks up).
// The board sees only this process's bots: a party is of bots run together.
class Board {
public:
    explicit Board(bool forms) : forms_(forms) {}
    // --party: these bots are one party, the first its leader, for as long as the run.
    void fix(std::vector<std::string> names) {
        Group g;
        g.members = std::move(names);
        g.leader = g.members.front();
        g.fixed = true;
        groups_.push_back(std::move(g));
    }

    void tick(std::vector<std::unique_ptr<NetBot>>& bots) {
        const auto now = std::chrono::steady_clock::now();
        if (now < next_) return;
        next_ = now + std::chrono::milliseconds(500);
        byName_.clear();
        needs_.clear();
        for (auto& bot : bots) {
            if (!bot->up() || bot->driven()) continue;
            byName_[bot->name()] = bot.get();
            needs_[bot->name()] = bot->needs();
        }
        for (Group& g : groups_) lead(g, now);
        std::erase_if(groups_, [](const Group& g) { return g.gone; });
        merge();
        std::erase_if(groups_, [](const Group& g) { return g.gone; });
        follow();
        loose(now);
        if (forms_) form(now);
    }

private:
    struct Group {
        std::string leader, tank;
        std::vector<std::string> members;  // the leader first
        bool fixed = false, gone = false;
        int map = -1, breed = -1;
        int64_t invited = -1000000;  // the leader's clock at his last invites
        int64_t clearSince = -1;  // the leader's clock when no member was stuck any more
        int64_t emptySince = -1;  // ... and when his party was last heard empty
        int64_t chosenAt = 0;     // ... and when the hunt was last changed
        int castle = 0;           // the Blood Castle they go into together, 0 for none
        bool forCastle = false;   // formed for it, at the door
        int saidCastle = 0;       // the castle last said on Party
        bool forFarm = false;     // formed to farm for wings (Board::farm)
        std::pair<int, int> said{-1, -1};  // the hunt last said on Party
    };

    NetBot* bot(const std::string& name) const {
        const auto it = byName_.find(name);
        return it == byName_.end() ? nullptr : it->second;
    }
    // Stuck: a breed he would take beside a party and not alone -- past the towns' own quests
    // (the user, 2026-10-09: "it make sense they go together on dungeons"). At level 1 every
    // breed reads too hard alone, and parties for Marlon's and Peia's held five bots 40-80 levels
    // under the same five solo; a knight made their tank at level 1 died broke and ended at 17.
    static bool stuck(const Bot::Need& n) { return n.together && !n.alone && !n.town; }
    static bool tankish(sim::Kin kin) { return kin == sim::Kin::DarkKnight || kin == sim::Kin::MagicGladiator; }
    bool grouped(const std::string& name) const {
        for (const Group& g : groups_) {
            if (std::find(g.members.begin(), g.members.end(), name) != g.members.end()) return true;
        }
        return false;
    }

    // A party's hunt chosen and given to each member; invites sent; broken up when done.
    void lead(Group& g, std::chrono::steady_clock::time_point now) {
        NetBot* leader = bot(g.leader);
        if (!leader) return;  // his line is down: the party waits for him
        // Invites, every twenty seconds, to whoever is not in yet.
        const net::PartyState& state = leader->partyState();
        const auto in = [&](const std::string& name) {
            for (const net::PartyState::Member& m : state.members) {
                if (m.name == name) return true;
            }
            return false;
        };
        // **On the game's clock, not the wall's** (mu2_server --speed): at --speed 200 the
        // wall's ten minutes are fifty game hours, and a party was given up before its invite
        // was heard.
        if (leader->clockNow() - g.invited > 20 * 20) {
            g.invited = leader->clockNow();
            for (const std::string& m : g.members) {
                if (m == g.leader || in(m)) continue;
                if (NetBot* one = bot(m)) one->expect(g.leader);
                leader->askParty(net::PartyWhat::Invite, m);
            }
        }
        // No party heard: none has taken his invite yet, or he has just come to another map and
        // his line has not heard it -- nothing changes (a knight landing in Noria read as out of
        // his own party, dropped its hunt and paid 2,000 zen back to Lorencia). One no one has
        // joined in ten game minutes is given up.
        if (state.members.empty()) {
            if (g.emptySince < 0) g.emptySince = leader->clockNow();
            if (!g.fixed && leader->clockNow() - g.emptySince > kSettles) {
                std::printf("  [board] no one joined %s: given up\n", g.leader.c_str());
                for (const std::string& m : g.members) {
                    if (NetBot* one = bot(m)) one->expect("");
                }
                g.gone = true;
            }
            return;
        }
        g.emptySince = -1;
        // The breed the most still want, counted by members -- a quest under way twice an offered
        // one, so a quest is finished before the next is begun, and a stuck breed twice again --
        // then the leader's map, then the kills left. Only a breed the tank and half of them can
        // stand: the party's lowest is not left out of it, nor made to fail the rest.
        // The tank: the leader if he is one, else the first knight, else the first gladiator.
        g.tank.clear();
        for (const sim::Kin kin : {sim::Kin::DarkKnight, sim::Kin::MagicGladiator}) {
            for (const std::string& m : g.members) {
                NetBot* one = bot(m);
                if (g.tank.empty() && one && one->kin() == kin) g.tank = m;
            }
        }
        if (tankish(leader->kin())) g.tank = g.leader;
        std::map<std::pair<int, int>, std::pair<int, int>> want;  // (map, breed) -> (weight, left)
        std::map<std::pair<int, int>, std::string> label;
        bool anyStuck = false;
        stands_.clear();
        for (const std::string& m : g.members) {
            const auto it = needs_.find(m);
            if (it == needs_.end()) continue;
            std::set<std::pair<int, int>> counted;
            for (const Bot::Need& n : it->second) {
                if (!n.together) continue;
                anyStuck |= stuck(n);
                if (!counted.insert({n.map, n.breed}).second) continue;
                const auto open = stands_.find({n.map, n.breed});
                if (open == stands_.end()) {
                    int can = 0, up = 0;
                    bool tankCan = true;
                    for (const std::string& o : g.members) {
                        NetBot* one = bot(o);
                        if (!one) continue;
                        ++up;
                        const bool helps = one->helps(n.map, n.breed);
                        can += helps;
                        if (o == g.tank) tankCan = helps;
                    }
                    stands_[{n.map, n.breed}] = tankCan && can * 2 >= up;
                }
                if (!stands_[{n.map, n.breed}]) continue;
                auto& w = want[{n.map, n.breed}];
                w.first += (n.taken ? 2 : 1) * (stuck(n) ? 2 : 1);
                w.second += n.left;
                label[{n.map, n.breed}] = n.label;
            }
        }
        std::pair<int, int> best{-1, -1};
        std::tuple<int, int, int> most{0, 0, 0};
        const int home = leaderMap(*leader);
        for (const auto& [key, w] : want) {
            const std::tuple<int, int, int> score{w.first, key.first == home ? 1 : 0, w.second};
            if (score > most) {
                most = score;
                best = key;
            }
        }
        // **The hunt under way holds** until another outweighs it by members: a tie broken by
        // the leader's map flipped the party between Lorencia and Noria each time he landed,
        // 2,000 zen a trip.
        // And twenty game minutes at least, whatever: the weights turn on purses and potions,
        // and a party went between Devias's Ice Queens and the Dungeon's Skeleton Archers every
        // ten minutes.
        // And past them, until another outweighs it by kOutweighs -- a member's stuck quest under
        // way: by a point, a party went between the Balrog, the Ice Queens and the Lizard Kings
        // every twenty minutes, paying the trip each time.
        if (const auto now = want.find({g.map, g.breed});
            now != want.end() && (now->second.first + kOutweighs > std::get<0>(most) || leader->clockNow() - g.chosenAt < kHolds)) {
            best = now->first;
        }
        // **Farming for wings** (the user, 2026-10-10: "can party get to the icarus?", "and get
        // wings?"): a party formed for it, with no quest stuck, hunts the strongest breed the
        // tank and half of them stand together -- where the Jewel of Life falls, Icarus's when
        // enough of them fly -- and every such breed on that map. It holds while two want wings.
        std::vector<int> farmBreeds;
        bool farmBusy = false;
        if (g.forFarm) {
            int wanting = 0, up = 0;
            std::map<std::pair<int, int>, int> have;
            std::set<std::pair<int, int>> tankHas;
            for (const std::string& m : g.members) {
                NetBot* one = bot(m);
                if (!one || !in(m)) continue;
                ++up;
                wanting += one->wantsWings();
                for (const Bot::Ground& gr : one->grounds()) {
                    ++have[{gr.map, gr.breed}];
                    if (m == g.tank) tankHas.insert({gr.map, gr.breed});
                    label[{gr.map, gr.breed}] = gr.label;
                }
            }
            farmBusy = wanting >= 2;
            std::map<std::pair<int, int>, int> levelOf;
            for (const std::string& m : g.members) {
                if (NetBot* one = bot(m); one && in(m)) {
                    for (const Bot::Ground& gr : one->grounds()) levelOf[{gr.map, gr.breed}] = gr.level;
                }
            }
            const auto common = [&](const std::pair<int, int>& key) {
                return (g.tank.empty() || tankHas.count(key)) && have[key] * 2 >= up;
            };
            if (farmBusy && !(anyStuck && best.first >= 0)) {
                std::pair<int, int> top{-1, -1};
                std::pair<int, int> topScore{-1, -1};
                for (const auto& [key, n] : have) {
                    if (!common(key)) continue;
                    const std::pair<int, int> score{levelOf[key], n};
                    if (score > topScore) topScore = score, top = key;
                }
                // The ground under way holds its twenty minutes, as a quest's hunt does.
                if (g.map >= 0 && have.count({g.map, g.breed}) && common({g.map, g.breed}) &&
                    leader->clockNow() - g.chosenAt < kHolds) {
                    top = {g.map, g.breed};
                }
                if (std::getenv("BOARD_TRACE") && top != std::pair<int, int>{g.map, g.breed}) {
                    std::printf("  [board] farm pick for %s: map %d breed %d (%s, level %d), tank %s\n", g.leader.c_str(),
                                top.first, top.second, label[top].c_str(), levelOf[top], g.tank.c_str());
                    for (const std::string& m : g.members) {
                        NetBot* one = bot(m);
                        if (!one) continue;
                        std::vector<Bot::Ground> gs = one->grounds();
                        std::sort(gs.begin(), gs.end(), [](const Bot::Ground& a, const Bot::Ground& b) { return a.level > b.level; });
                        std::printf("    %s (%zu grounds):", m.c_str(), gs.size());
                        for (size_t i = 0; i < gs.size() && i < 6; ++i) std::printf(" %s/%d@%d", gs[i].label.c_str(), gs[i].level, gs[i].map);
                        std::printf("\n");
                    }
                }
                best = top;
                for (const auto& [key, n] : have) {
                    if (key.first == best.first && common(key)) farmBreeds.push_back(key.second);
                }
            }
        }
        // **Blood Castle together** (the user, 2026-10-09: "and maybe go to bc together?"): the
        // highest castle two or more of them can go into; each who can is sent into it at the
        // door's time, and the party holds while any of them is due there or inside.
        int castle = 0;
        bool castleBusy = false;
        for (int c = sim::kCastlesBuilt; c >= 1 && castle == 0; --c) {
            int can = 0;
            for (const std::string& m : g.members) {
                if (NetBot* one = bot(m); one && in(m) && (one->castlesOpen() & (1 << c))) ++can;
            }
            if (can >= 2) castle = c;
        }
        for (const std::string& m : g.members) {
            if (NetBot* one = bot(m); one && castle > 0 && (one->castleTime() || one->inCastle())) castleBusy = true;
        }
        // Said once a castle: a cloak spent or made turned it off and on all evening.
        if (castle > 0 && castle != g.saidCastle) {
            say(*leader, "Blood Castle " + std::to_string(castle) + " together at its door.");
            g.saidCastle = castle;
        }
        g.castle = castle;
        // Ten game minutes with no one stuck, or nothing the party can stand: "stuck" turns on a
        // purse and a bag of potions, and a party broken up the moment it was not formed again
        // a minute later, all day.
        if ((anyStuck && best.first >= 0) || g.fixed || castleBusy || farmBusy) g.clearSince = -1;
        else if (g.clearSince < 0) g.clearSince = leader->clockNow();
        if (g.clearSince >= 0 && leader->clockNow() - g.clearSince > kSettles) {
            say(*leader, "Thanks all, we're done here.");
            for (const std::string& m : g.members) {
                if (NetBot* one = bot(m)) {
                    one->askParty(net::PartyWhat::Leave, "");
                    one->expect("");
                    one->lead(Bot::Lead{}, "", "");
                }
            }
            std::printf("  [board] the party of %s breaks up\n", g.leader.c_str());
            g.gone = true;
            return;
        }
        bool healer = false;
        for (const std::string& m : g.members) {
            if (NetBot* one = bot(m); one && one->kin() == sim::Kin::FairyElf && in(m)) healer = true;
        }
        // Said once a hunt: a pick blinking off for a tick said the same "Next" four times.
        if (best != g.said && best.first >= 0) {
            g.said = best;
            const WorldRow* at = worldOf(best.first);
            say(*leader, "Next: " + label[best] + " in " + (at ? at->name : "?") +
                             (g.tank.empty() ? "" : ", " + g.tank + " tanks") + ".");
        }
        if (best != std::pair<int, int>{g.map, g.breed}) g.chosenAt = leader->clockNow();
        g.map = best.first;
        g.breed = best.second;
        for (const std::string& m : g.members) {
            NetBot* one = bot(m);
            if (!one) continue;
            // Not in yet, or a breed he cannot stand even beside them: his own way, in the
            // party still. The board opens a breed to the party when the tank and half of them
            // stand it; led there all the same, a wizard of the party died 140 times in 24 game
            // hours against 29 alone.
            if (!in(m) || (g.map >= 0 && !one->helps(g.map, g.breed))) {
                one->lead(Bot::Lead{}, "", "");
                continue;
            }
            Bot::Lead lead;
            lead.map = g.map;
            lead.size = int(state.members.size());
            lead.healer = healer;
            lead.role = Bot::roleOf(one->kin(), m == g.tank);
            lead.castle = g.castle > 0 && (one->castlesOpen() & (1 << g.castle)) ? g.castle : 0;
            // Every breed on that map any member still wants (and can stand) -- or, farming,
            // every one there they stand together.
            if (!farmBreeds.empty()) lead.breeds = farmBreeds;
            for (const auto& [key, w] : want) {
                if (key.first == g.map && farmBreeds.empty()) lead.breeds.push_back(key.second);
            }
            // His own quest among them: the chosen breed's first.
            for (const Bot::Need& n : needs_[m]) {
                if (n.map != g.map || std::find(lead.breeds.begin(), lead.breeds.end(), n.breed) == lead.breeds.end()) continue;
                if (lead.quest < 0 || n.breed == g.breed) lead.quest = n.quest;
                if (n.breed == g.breed) break;
            }
            one->lead(std::move(lead), g.leader, g.tank);
        }
    }
    int leaderMap(NetBot& leader) const {
        for (const WorldRow& w : kWorlds) {
            if (leader.world() == w.name) return w.map;
        }
        return -1;
    }
    void say(NetBot& leader, const std::string& text) { leader.sayOn(net::Channel::Party, text); }

    // The quests the members of a party are stuck on.
    std::set<int> stuckOn(const Group& g) {
        std::set<int> out;
        for (const std::string& m : g.members) {
            for (const Bot::Need& n : needs_[m]) {
                if (stuck(n)) out.insert(n.quest);
            }
        }
        return out;
    }
    // **Two parties at one quest are one** (a run had Dorrak and Sylwen at the Catacombs' Cyclops
    // and Brann, Maelis and Corvin beside them): the smaller's members leave it and are invited
    // into the larger, when five hold them all.
    void merge() {
        for (Group& a : groups_) {
            for (Group& b : groups_) {
                if (&a == &b || a.gone || b.gone || a.fixed || b.fixed || a.forCastle || b.forCastle) continue;
                if (a.members.size() < b.members.size() || a.members.size() + b.members.size() > size_t(sim::kPartyMost)) continue;
                const std::set<int> mine = stuckOn(a), theirs = stuckOn(b);
                bool shared = false;
                for (const int q : theirs) shared |= mine.count(q) > 0;
                if (!shared) continue;
                for (const std::string& m : b.members) {
                    if (NetBot* one = bot(m)) {
                        one->askParty(net::PartyWhat::Leave, "");
                        one->expect(a.leader);
                    }
                    a.members.push_back(m);
                }
                a.invited = -1000000;
                std::printf("  [board] the party of %s joins %s's\n", b.leader.c_str(), a.leader.c_str());
                b.gone = true;
            }
        }
    }

    // **A party to farm for wings** (Group::forFarm): free bots that want wings and are stuck on
    // no quest, two to five, a knight or gladiator first to tank and lead.
    void farm(std::set<std::string>& free, const std::map<int, std::vector<std::string>>& byQuest) {
        std::vector<std::string> farmers;
        for (const std::string& name : free) {
            bool stuckHere = false;
            for (const auto& [q, names] : byQuest) {
                stuckHere |= std::find(names.begin(), names.end(), name) != names.end();
            }
            if (!stuckHere && bot(name)->wantsWings()) farmers.push_back(name);
        }
        std::stable_sort(farmers.begin(), farmers.end(), [&](const std::string& a, const std::string& b) {
            return tankish(bot(a)->kin()) > tankish(bot(b)->kin());
        });
        while (farmers.size() >= 2) {
            Group g;
            const size_t n = std::min<size_t>(farmers.size(), size_t(sim::kPartyMost));
            g.members.assign(farmers.begin(), farmers.begin() + long(n));
            farmers.erase(farmers.begin(), farmers.begin() + long(n));
            if (farmers.size() == 1) {  // never one left alone
                farmers.insert(farmers.begin(), g.members.back());
                g.members.pop_back();
            }
            g.leader = g.members.front();
            g.forFarm = true;
            std::string said;
            for (const std::string& m : g.members) {
                free.erase(m);
                said += (said.empty() ? "" : ", ") + m;
            }
            std::printf("  [board] a party to farm for wings: %s\n", said.c_str());
            groups_.push_back(std::move(g));
        }
    }

    // A bot in a party the board did not make leaves it: it was a run before this one's.
    // **A party with someone not of this run** -- a player who /invited him, a bot of another
    // netbot -- is his to keep: he goes where its leader is, grinds there, and keeps near him.
    bool outsiders(NetBot& one) const {
        for (const net::PartyState::Member& m : one.partyState().members) {
            if (!byName_.count(m.name)) return true;
        }
        return false;
    }
    void follow() {
        for (const auto& [name, one] : byName_) {
            one->openToAll(!grouped(name));
            if (grouped(name)) continue;
            if (!outsiders(*one)) {
                one->lead(Bot::Lead{}, "", "");  // his own way again, the player gone
                continue;
            }
            const net::PartyState& state = one->partyState();
            const net::PartyState::Member& head = state.members.front();
            Bot::Lead lead;
            for (const WorldRow& w : kWorlds) {
                if (head.world == w.name) lead.map = w.map;
            }
            lead.size = int(state.members.size());
            lead.role = Bot::roleOf(one->kin(), false);
            for (const net::PartyState::Member& m : state.members) lead.healer |= m.kin == uint8_t(sim::Kin::FairyElf);
            one->lead(std::move(lead), head.name == name ? "" : head.name, "");
        }
    }

    void loose(std::chrono::steady_clock::time_point now) {
        for (const auto& [name, one] : byName_) {
            if (grouped(name) || one->partyState().members.empty() || outsiders(*one)) continue;
            auto& at = leftAt_[name];
            if (at != 0 && one->clockNow() - at < 10 * 20) continue;
            at = one->clockNow();
            one->askParty(net::PartyWhat::Leave, "");
        }
    }

    // Parties formed from the stuck, a quest at a time, the most stuck first.
    void form(std::chrono::steady_clock::time_point now) {
        std::map<int, std::vector<std::string>> byQuest;
        std::map<int, Bot::Need> first;
        std::set<std::string> free;
        for (const auto& [name, one] : byName_) {
            if (grouped(name) || !one->partyState().members.empty()) continue;
            free.insert(name);
            std::set<int> quests;
            for (const Bot::Need& n : needs_[name]) {
                if (!stuck(n) || !quests.insert(n.quest).second) continue;
                byQuest[n.quest].push_back(name);
                if (!first.count(n.quest)) first[n.quest] = n;
            }
        }
        // Free bots due at the same castle's door go in together.
        for (int c = sim::kCastlesBuilt; c >= 1; --c) {
            std::vector<std::string> members;
            for (const std::string& name : free) {
                NetBot& one = *bot(name);
                if (one.castleTime() && (one.castlesOpen() & (1 << c)) && members.size() < size_t(sim::kPartyMost)) {
                    members.push_back(name);
                }
            }
            if (members.size() < 2) continue;
            std::stable_sort(members.begin(), members.end(), [&](const std::string& a, const std::string& b) {
                return tankish(bot(a)->kin()) > tankish(bot(b)->kin());
            });
            Group g;
            g.members = members;
            g.leader = members.front();
            g.forCastle = true;
            std::string said;
            for (const std::string& m : members) {
                free.erase(m);
                said += (said.empty() ? "" : ", ") + m;
            }
            std::printf("  [board] a party for Blood Castle %d: %s\n", c, said.c_str());
            groups_.push_back(std::move(g));
        }
        std::vector<std::pair<size_t, int>> order;
        for (const auto& [q, names] : byQuest) order.push_back({names.size(), q});
        std::sort(order.rbegin(), order.rend());
        for (const auto& [count, q] : order) {
            const Bot::Need& need = first[q];
            std::vector<std::string> members;
            for (const std::string& name : byQuest[q]) {
                if (free.count(name) && members.size() < size_t(sim::kPartyMost)) members.push_back(name);
            }
            if (members.empty()) continue;
            // A party at this quest with room takes them in.
            bool joined = false;
            for (Group& g : groups_) {
                if (g.fixed || g.forCastle || !stuckOn(g).count(q)) continue;
                for (const std::string& m : members) {
                    if (g.members.size() >= size_t(sim::kPartyMost)) break;
                    g.members.push_back(m);
                    free.erase(m);
                    g.invited = -1000000;
                    std::printf("  [board] %s joins %s's party for %s\n", m.c_str(), g.leader.c_str(), sim::questAt(q).title);
                    joined = true;
                }
                if (joined) break;
            }
            if (joined) continue;
            const auto has = [&](auto test) {
                for (const std::string& m : members) {
                    if (test(*bot(m))) return true;
                }
                return false;
            };
            const auto recruit = [&](auto test) {
                for (const std::string& name : free) {
                    if (members.size() >= size_t(sim::kPartyMost)) return;
                    if (std::find(members.begin(), members.end(), name) != members.end()) continue;
                    NetBot& one = *bot(name);
                    if (test(one) && one.helps(need.map, need.breed)) {
                        members.push_back(name);
                        return;
                    }
                }
            };
            const auto isTank = [](NetBot& b) { return tankish(b.kin()); };
            const auto isElf = [](NetBot& b) { return b.kin() == sim::Kin::FairyElf; };
            if (!has(isTank)) recruit(isTank);
            if (!has(isElf)) recruit(isElf);
            while (members.size() < 3) {
                const size_t was = members.size();
                recruit([](NetBot&) { return true; });
                if (members.size() == was) break;
            }
            if (members.size() < 2) {
                // Alone with it: said on his map, once in ten minutes.
                NetBot& one = *bot(members.front());
                auto& at = lfgAt_[one.name()];
                if (at == 0 || one.clockNow() - at > 10 * 60 * 20) {
                    at = one.clockNow();
                    const WorldRow* w = worldOf(need.map);
                    one.sayOn(net::Channel::Map, "LFG " + std::string(sim::questAt(q).title) + ", " + need.label +
                                                     " in " + (w ? w->name : "?") + ", level " +
                                                     std::to_string(one.level()));
                }
                continue;
            }
            // The tank leads -- a knight before a gladiator -- else the highest level.
            std::stable_sort(members.begin(), members.end(), [&](const std::string& a, const std::string& b) {
                const auto rank = [&](const std::string& n) {
                    const sim::Kin k = bot(n)->kin();
                    return k == sim::Kin::DarkKnight ? 0 : k == sim::Kin::MagicGladiator ? 1 : 2;
                };
                if (rank(a) != rank(b)) return rank(a) < rank(b);
                return bot(a)->level() > bot(b)->level();
            });
            Group g;
            g.members = members;
            g.leader = members.front();
            std::string said;
            for (const std::string& m : members) {
                free.erase(m);
                said += (said.empty() ? "" : ", ") + m;
            }
            std::printf("  [board] a party for %s (%s): %s\n", sim::questAt(q).title, need.label.c_str(), said.c_str());
            groups_.push_back(std::move(g));
        }
        // Then the free who want wings (the quests' parties first).
        farm(free, byQuest);
    }

    static constexpr int64_t kSettles = 10 * 60 * 20;
    static constexpr int64_t kHolds = 20 * 60 * 20;
    static constexpr int kOutweighs = 2;
    bool forms_;
    std::vector<Group> groups_;
    std::map<std::string, NetBot*> byName_;
    std::map<std::string, std::vector<Bot::Need>> needs_;
    std::map<std::pair<int, int>, bool> stands_;  // this group: (map, breed) the party can stand
    std::map<std::string, int64_t> leftAt_, lfgAt_;  // each bot's clock
    std::chrono::steady_clock::time_point next_{};
};

}  // namespace

int main(int argc, char** argv) {
    std::string host = "37.27.158.226";
    int port = net::kDefaultPort;
    std::string dir = std::string(MU2_ASSET_DIR) + "/../saves/bots";
    double hours = 0.0;  // 0: until stopped
    bool quests = true;
    bool chatty = false;
    bool party = false, parties = false;
    int threads = 0;  // --threads N: 0, up to four
    std::string claude;
    std::vector<std::pair<std::string, sim::Kin>> wanted;
    std::set<std::string> energy;  // eelf: the energy elves (Options::support)
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--server" && i + 1 < argc) host = argv[++i];
        else if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (a == "--dir" && i + 1 < argc) dir = argv[++i];
        else if (a == "--hours" && i + 1 < argc) hours = std::atof(argv[++i]);
        else if (a == "--no-quests") quests = false;
        else if (a == "--chatty") chatty = true;
        else if (a == "--party") party = true;
        else if (a == "--parties") parties = true;
        else if (a == "--threads" && i + 1 < argc) threads = std::atoi(argv[++i]);
        else if (a == "--claude" && i + 1 < argc) claude = argv[++i];
        else if (a.find(':') != std::string::npos) {
            const std::string name = a.substr(0, a.find(':')), cls = a.substr(a.find(':') + 1);
            if (cls == "dk") wanted.emplace_back(name, sim::Kin::DarkKnight);
            else if (cls == "dw") wanted.emplace_back(name, sim::Kin::DarkWizard);
            else if (cls == "elf") wanted.emplace_back(name, sim::Kin::FairyElf);
            else if (cls == "eelf") {
                wanted.emplace_back(name, sim::Kin::FairyElf);
                energy.insert(name);
            }
            else if (cls == "mg") wanted.emplace_back(name, sim::Kin::MagicGladiator);
            else {
                std::fprintf(stderr, "netbot: %s: the class is dk, dw, elf, eelf or mg\n", a.c_str());
                return 2;
            }
        } else {
            std::fprintf(stderr, "usage: netbot [--server HOST] [--port N] [--dir DIR] [--hours H] "
                                 "[--no-quests] [--chatty] [--party | --parties] [--threads N] [--claude DIR] NAME:dk|dw|elf|eelf|mg ...\n");
            return 2;
        }
    }
    // The server's own bots (docs/party.md, the user's of 2026-10-09: "DK, Elf, DW, MG and a
    // second DK"): Ragnar and Elora, and an elf, a gladiator and a second knight beside them.
    if (wanted.empty()) {
        wanted = {{"Ragnar", sim::Kin::DarkKnight}, {"Elora", sim::Kin::DarkWizard}, {"Ilaria", sim::Kin::FairyElf},
                  {"Varkas", sim::Kin::MagicGladiator}, {"Hrothgar", sim::Kin::DarkKnight}};
    }
    // The realm's own log is every blow; the bots say what matters themselves.
    if (!std::getenv("BOT_LOG")) core::logSilence(true);
    std::setvbuf(stdout, nullptr, _IOLBF, 0);  // the brain's lines as they come, into a file too
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    std::vector<std::unique_ptr<NetBot>> bots;
    for (const auto& [name, kin] : wanted) {
        bots.push_back(std::make_unique<NetBot>(name, kin, host, port, dir, quests, chatty, energy.count(name) > 0));
    }
    if (!claude.empty()) bots.front()->driveFrom(claude);
    Board board(parties);
    if (party) {
        std::vector<std::string> names;
        for (const auto& one : wanted) names.push_back(one.first);
        board.fix(names);
    }
    // **The bots on --threads N threads** (the user, 2026-10-09: "cant we speed up this things?
    // we can control server thics"): each bot's mirror and brain are his own, so a thread plays a
    // share of them; one thread kept five bots at a full core at mu2_server --speed 200. The
    // board and the minute's report hold every bot's lock while they read and lead them.
    if (threads <= 0) threads = int(std::min<size_t>(bots.size(), 4));
    threads = std::clamp<int>(threads, 1, int(bots.size()));
    std::vector<std::mutex> locks(bots.size());
    std::atomic<bool> done{false};
    const auto start = std::chrono::steady_clock::now();
    const auto play = [&](int t) {
        std::vector<bool> up(bots.size(), false);
        std::vector<std::chrono::steady_clock::time_point> retryAt(bots.size());
        while (!done) {
            const auto now = std::chrono::steady_clock::now();
            for (size_t i = size_t(t); i < bots.size(); i += size_t(threads)) {
                std::lock_guard<std::mutex> hold(locks[i]);
                if (!up[i]) {
                    if (now < retryAt[i]) continue;
                    up[i] = bots[i]->connect();
                    if (!up[i]) retryAt[i] = now + std::chrono::seconds(10);
                    continue;
                }
                // A line gone -- another world, or the server restarted: he comes back with his
                // token, and the server says where he is.
                if (!bots[i]->update()) {
                    up[i] = false;
                    retryAt[i] = now + std::chrono::milliseconds(500);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    };
    std::vector<std::thread> players;
    for (int t = 0; t < threads; ++t) players.emplace_back(play, t);
    const auto all = [&](auto fn) {
        std::vector<std::unique_lock<std::mutex>> held;
        for (std::mutex& one : locks) held.emplace_back(one);
        fn();
    };
    auto reportAt = start + std::chrono::minutes(1);
    auto boardAt = start;
    while (!g_stop) {
        const auto now = std::chrono::steady_clock::now();
        if (hours > 0 && now - start > std::chrono::duration<double>(hours * 3600.0)) break;
        if ((party || parties) && now >= boardAt) {
            all([&] { board.tick(bots); });
            boardAt = now + std::chrono::seconds(1);
        }
        if (now >= reportAt) {
            all([&] {
                for (const auto& bot : bots) bot->report();
            });
            reportAt = now + std::chrono::minutes(1);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    done = true;
    for (std::thread& one : players) one.join();
    for (const auto& bot : bots) bot->report();
    return 0;
}
