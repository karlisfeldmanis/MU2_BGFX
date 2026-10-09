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
//                [--claude DIR] NAME:CLASS ...
//
// CLASS is dk, dw, elf or mg (a new elf is born in Noria). --party makes the run's bots one party
// (docs/party.md), the first named its leader. --claude DIR makes the first named the Claude
// bot (claude.h): his chat and his choice of ground are the model's, through files in DIR.

#include <algorithm>
#include <chrono>
#include <csignal>
#include <ctime>
#include <filesystem>
#include <fstream>
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
constexpr int64_t kAnswerWait = 2 * 20; // a decision's answers not heard in two seconds: think anyway

class NetBot {
public:
    NetBot(std::string name, sim::Kin kin, std::string host, int port, std::string dir, bool quests,
           bool chatty)
        : name_(std::move(name)), kin_(kin), host_(std::move(host)), port_(port), dir_(std::move(dir)),
          talk_(name_) {
        Options options;
        options.kin = kin;
        options.quests = quests;
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

    // **The party** (docs/party.md), --party: the bots of this run are one party, the first named
    // its leader. A first cut, for the server's party to be tried end to end: the leader invites
    // whoever of them is not in yet, every twenty seconds, and each takes an invite from one of
    // them. Forming a party for a quest it cannot take alone is the brain's, later.
    void partyWith(std::vector<std::string> mates, bool leads) {
        mates_ = std::move(mates);
        leads_ = leads;
    }

    // --claude DIR: he is the Claude bot (claude.h).
    void driveFrom(const std::string& dir) { claude_ = std::make_unique<bot::Claude>(dir); }

private:
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
            } else if (verb == "accept" && !first.empty()) {
                link_->askParty({net::PartyWhat::Accept, first});
            } else if (verb == "decline" && !first.empty()) {
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
            said += (said.empty() ? "" : ", ") + m.name + " (" + std::to_string(m.level) + ", " + m.world +
                    (m.here != 0 ? ", here" : "") + ")";
        }
        if (said != partySaid_) {
            line("party: %s", said.empty() ? "none" : said.c_str());
            partySaid_ = said;
        }
        const int64_t now = brain_->clockNow();
        if (!leads_ || mates_.empty() || now - invitedAt_ < 20 * 20) return;
        invitedAt_ = now;
        for (const std::string& mate : mates_) {
            bool in = false;
            for (const net::PartyState::Member& m : state.members) in |= m.name == mate;
            if (in || mate == name_) continue;
            link_->askParty({net::PartyWhat::Invite, mate});
        }
    }

    // The chat: what he heard since the last pump, and what he has to say by now.
    void chat() {
        heard_.clear();
        link_->takeHeard(heard_);
        const int64_t now = brain_->clockNow();
        party();
        for (const net::Heard& one : heard_) {
            // An invite from one of his own party's bots, taken (--party).
            constexpr const char* kInvites = " invites you to a party.";
            if (one.name.empty() && one.channel == net::Channel::Party && !mates_.empty()) {
                const size_t at = one.text.find(kInvites);
                const std::string from = at != std::string::npos ? one.text.substr(0, at) : "";
                if (std::find(mates_.begin(), mates_.end(), from) != mates_.end()) {
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
        if (pending_ != 0 && now - pendingSince_ < kAnswerWait) return;
        if (!mirror_->snapshot(bytes_) || !scratch_->restoreSnapshot(bytes_) || !scratch_->lookAs(you_)) {
            line("the scratch would not lay");
            return;
        }
        asked_.clear();
        const uint32_t before = brain_->lastTicket();
        brain_->thinkOn(*scratch_, asked_);
        brain_->look(*mirror_);
        for (const sim::Command& one : asked_) link_->send(one);
        if (brain_->lastTicket() != before) {
            pending_ = brain_->lastTicket();
            pendingSince_ = now;
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
                        for (const sim::Command& one : asked_) {
                            if (one.kind != sim::Command::Kind::Travel || one.ticket != uint32_t(h.c)) continue;
                            const sim::TravelRow& to = sim::travelAt(one.a);
                            if (to.map != int32_t(brain_->mapNow())) return true;
                        }
                    }
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
    uint32_t you_ = 0;
    uint32_t pending_ = 0;
    int64_t pendingSince_ = 0;
    bool divergenceSaid_ = false;
    std::vector<std::string> mates_;  // --party: the run's bots, his party
    bool leads_ = false;
    int64_t invitedAt_ = -1000000;
    std::string partySaid_;
    std::unique_ptr<bot::Claude> claude_;  // --claude: the model drives him
};

}  // namespace

int main(int argc, char** argv) {
    std::string host = "37.27.158.226";
    int port = net::kDefaultPort;
    std::string dir = std::string(MU2_ASSET_DIR) + "/../saves/bots";
    double hours = 0.0;  // 0: until stopped
    bool quests = true;
    bool chatty = false;
    bool party = false;
    std::string claude;
    std::vector<std::pair<std::string, sim::Kin>> wanted;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--server" && i + 1 < argc) host = argv[++i];
        else if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (a == "--dir" && i + 1 < argc) dir = argv[++i];
        else if (a == "--hours" && i + 1 < argc) hours = std::atof(argv[++i]);
        else if (a == "--no-quests") quests = false;
        else if (a == "--chatty") chatty = true;
        else if (a == "--party") party = true;
        else if (a == "--claude" && i + 1 < argc) claude = argv[++i];
        else if (a.find(':') != std::string::npos) {
            const std::string name = a.substr(0, a.find(':')), cls = a.substr(a.find(':') + 1);
            if (cls == "dk") wanted.emplace_back(name, sim::Kin::DarkKnight);
            else if (cls == "dw") wanted.emplace_back(name, sim::Kin::DarkWizard);
            else if (cls == "elf") wanted.emplace_back(name, sim::Kin::FairyElf);
            else if (cls == "mg") wanted.emplace_back(name, sim::Kin::MagicGladiator);
            else {
                std::fprintf(stderr, "netbot: %s: the class is dk, dw, elf or mg\n", a.c_str());
                return 2;
            }
        } else {
            std::fprintf(stderr, "usage: netbot [--server HOST] [--port N] [--dir DIR] [--hours H] "
                                 "[--no-quests] [--chatty] [--party] [--claude DIR] NAME:dk|dw|elf|mg ...\n");
            return 2;
        }
    }
    if (wanted.empty()) wanted = {{"Ragnar", sim::Kin::DarkKnight}, {"Elora", sim::Kin::DarkWizard}};
    // The realm's own log is every blow; the bots say what matters themselves.
    if (!std::getenv("BOT_LOG")) core::logSilence(true);
    std::setvbuf(stdout, nullptr, _IOLBF, 0);  // the brain's lines as they come, into a file too
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    std::vector<std::unique_ptr<NetBot>> bots;
    for (const auto& [name, kin] : wanted) {
        bots.push_back(std::make_unique<NetBot>(name, kin, host, port, dir, quests, chatty));
    }
    if (!claude.empty()) bots.front()->driveFrom(claude);
    if (party) {
        std::vector<std::string> names;
        for (const auto& one : wanted) names.push_back(one.first);
        for (size_t i = 0; i < bots.size(); ++i) bots[i]->partyWith(names, i == 0);
    }
    std::vector<bool> up(bots.size(), false);
    std::vector<std::chrono::steady_clock::time_point> retryAt(bots.size());
    const auto start = std::chrono::steady_clock::now();
    auto reportAt = start + std::chrono::minutes(1);
    while (!g_stop) {
        const auto now = std::chrono::steady_clock::now();
        if (hours > 0 && now - start > std::chrono::duration<double>(hours * 3600.0)) break;
        for (size_t i = 0; i < bots.size(); ++i) {
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
        if (now >= reportAt) {
            for (const auto& bot : bots) bot->report();
            reportAt = now + std::chrono::minutes(1);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    for (const auto& bot : bots) bot->report();
    return 0;
}
