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
//                [--parties] [--no-trades] [--guilds] [--threads N] [--claude DIR] NAME:CLASS ...
//
// CLASS is dk, dw, elf, eelf (the energy elf, her points to energy for heals and buffs) or mg (a
// new elf is born in Noria). --party makes the run's bots one party
// (docs/game/party.md), the first named its leader; --parties lets them form their own, for the quests
// they cannot take alone (Board). --claude DIR makes the first named the Claude
// bot (claude.h): his chat and his choice of ground are the model's, through files in DIR.
// The run's bots trade at the Broker (Errands, errands.h) unless --no-trades: they list their
// spares and buy upgrades there, as players do. They never trade face to face.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <csignal>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <mutex>
#include <thread>
#include <utility>

#include "../bot/brain.h"
#include "claude.h"
#include "talk.h"
#include "client/remote_link.h"
#include "net/broker_wire.h"
#include "net/wire.h"
#include "sim/cradle.h"
#include "sim/item_link.h"
#include "sim/maps.h"

namespace {

volatile std::sig_atomic_t g_stop = 0;
void onSignal(int) { g_stop = 1; }

constexpr int kThinkNet = 5;            // ticks between decisions, as offline
constexpr int kStepsATurn = 50;          // ticks a bot steps before his thread goes on to the next
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

class NetBot;

// **One mirror a world on a thread** (the user, 2026-10-13: "how we can optimise bot app CPU
// usage?"): each bot stepped a mirror of his own, two thirds of netbot's time at ten bots on two
// worlds. Every line on a world hears the same Tick (server/src/world.cpp, tick), so this
// thread's bots on one share it, stepped once by whichever line has the next tick (Pool::advance),
// and each reads it as himself (Realm::lookAs, NetBot::mirror) before anything of his.
struct Shared {
    std::string world;
    int copy = 0;
    std::unique_ptr<sim::Realm> mirror;
    std::vector<NetBot*> members;
    // The mirror as bytes at `bytesAt`, laid once a tick for every bot's scratch: they think on
    // the same tick (kThinkNet).
    std::vector<uint8_t> bytes;
    int64_t bytesAt = -1;
    const std::vector<uint8_t>* snapshot() {
        if (bytesAt != mirror->tick()) {
            if (!mirror->snapshot(bytes)) return nullptr;
            bytesAt = mirror->tick();
        }
        return &bytes;
    }
};

struct Pool {
    std::vector<std::weak_ptr<Shared>> worlds;
    std::shared_ptr<Shared> find(const std::string& world) {
        std::erase_if(worlds, [](const std::weak_ptr<Shared>& one) { return one.expired(); });
        for (const auto& one : worlds) {
            if (auto shared = one.lock(); shared && shared->world == world) return shared;
        }
        return nullptr;
    }
    bool advance();  // below NetBot
};

class NetBot {
public:
    NetBot(std::string name, sim::Kin kin, std::string host, int port, std::string dir, bool quests,
           bool chatty, bool support = false, bool trades = true, bool magic = false, bool trader = false)
        : name_(std::move(name)), kin_(kin), host_(std::move(host)), port_(port), dir_(std::move(dir)),
          talk_(name_) {
        trader_ = trader;
        magic_ = magic;
        Options options;
        options.kin = kin;
        options.quests = quests;
        options.support = support;
        options.magic = magic;  // a magic gladiator (mmg): energy and spells, the wizard's way
        options.trader = trader;  // and tickets made to sell (Bot::forSale)
        options.tag = name_ + " ";
        // The dearest spares go to the Broker, not the blacksmith (Errands lists them).
        if (trades) options.brokerHold = 8;
        talk_.setChatty(chatty);
        brain_ = std::make_unique<Bot>(options, 0);
        loadToken();
    }

    // Joins his world (the server may answer that he is elsewhere: then that one), raises the
    // mirror and its scratch from the Welcome as the client does (game/play_open.cpp), and
    // replays the world's past -- or, when a bot of his thread is on that world already, reads
    // its mirror (Shared) and replays nothing.
    bool connect(Pool& pool) {
        std::string world = world_;
        bool alone = false;  // the shared mirror would not take him: a mirror of his own
        for (int tries = 0; tries < 4; ++tries) {
            // **Raised on the side and swapped in only once he is in**: replaced up front, a
            // join that failed (the server restarting) left the brain looking at the mirror just
            // freed, and the next report or market pass read it -- netbot fell over three times
            // on 2026-10-11 (NetBot::report, Market::tick).
            std::shared_ptr<Shared> shared = alone ? nullptr : pool.find(world);
            const bool fresh = shared == nullptr;
            if (fresh) {
                shared = std::make_shared<Shared>();
                shared->mirror = std::make_unique<sim::Realm>();
            }
            auto scratch = std::make_unique<sim::Realm>();
            auto link = std::make_unique<client::RemoteLink>(*shared->mirror);
            net::Hello hello;
            hello.world = world;
            hello.kin = uint8_t(kin_);
            hello.token = token_;
            // What everyone else's hover plate calls him (net::Who).
            hello.name = name_;
            // The server's item list, or it turns him away (net::Hello::items).
            if (const content::Tables* tables = tablesOf(world)) hello.items = tables->itemsPrint();
            net::Welcome welcome;
            net::Elsewhere elsewhere;
            if (!link->join(host_, port_, hello, welcome, &elsewhere)) {
                if (!elsewhere.world.empty()) {
                    world = elsewhere.world;
                    continue;
                }
                line("could not join %s:%d", host_.c_str(), port_);
                return false;
            }
            const content::Tables* tables = tablesOf(welcome.world);
            if (!tables) return false;
            if (!fresh) {
                // Its mirror is a step of this world's ticks, of the same copy, from no later than
                // where his line's begin; anything else and he raises his own.
                link->passThrough(shared->mirror->tick());
                const int64_t next = link->nextTick();
                if (shared->world != welcome.world || shared->copy != link->mapCopy() ||
                    (next >= 0 && next != shared->mirror->tick() + 1)) {
                    alone = true;
                    world = welcome.world;
                    continue;
                }
            }
            // The server's own sequence, off the same frame (sim::raiseFrom).
            // The scratch on the mirror's own tables, as it always was: an event map's grid
            // changes under its run (Blood Castle's bridge).
            if ((fresh && !sim::raiseFrom(*shared->mirror, tables, net::raiseOrder(welcome))) ||
                !sim::raiseFrom(*scratch, shared->mirror->tables(), net::raiseOrder(welcome))) {
                line("%s would not raise", world_.c_str());
                return false;
            }
            world_ = welcome.world;
            token_ = welcome.token;
            saveToken();
            leave();
            link_ = std::move(link);
            scratch_ = std::move(scratch);
            shared_ = std::move(shared);
            you_ = welcome.you;
            if (fresh) {
                shared_->world = welcome.world;
                shared_->copy = link_->mapCopy();
                link_->catchUp();
                pool.worlds.push_back(shared_);
            }
            shared_->members.push_back(this);
            pending_ = 0;
            trips_.clear();  // a new line, a new world: the old one's tickets answer nothing here
            // In it once its mirror has stepped his joining (heard).
            if (shared_->mirror->lookAs(you_)) arrived();
            return true;
        }
        return false;
    }

    // His player on the mirror: the brain on it, and said.
    void arrived() {
        ready_ = true;
        brain()->attach(*mirror(), shared_->mirror->tables());
        joinedAt_ = std::chrono::steady_clock::now();
        const sim::Body& me = mirror()->hero();
        line("in %s as #%u, %s level %d with %lld zen, %d players here, %zu bots on its mirror",
             world_.c_str(), me.id, className(me.kin, me.second), me.level, (long long)mirror()->money(),
             mirror()->playersHere(), shared_->members.size());
    }

    // Off his world's mirror and his line closed; the mirror goes with its last bot.
    void leave() {
        link_.reset();  // first: it holds the mirror
        if (shared_) std::erase(shared_->members, this);
        shared_.reset();
        ready_ = false;
        leaving_ = false;
    }

    // The sheet, the network in and out and his chat; the ticks are the world's (Pool::advance).
    // False once the line is gone or he has gone to another world.
    bool pumpLine() {
        if (!link_ || !link_->connected() || leaving_) return false;
        // His sheet for botwatch, every second (Bot::sheet): live enough to watch.
        if (const auto now = std::chrono::steady_clock::now(); now >= sheetAt_ && ready_) {
            sheetAt_ = now + std::chrono::seconds(1);
            brain()->look(*mirror());
            std::ofstream(dir_ + "/" + name_ + ".sheet") << brain()->sheet()
                                                          << (trader_ ? "who\tRole\ttrader: the market his trade\n" : "");
        }
        link_->pump();
        if (ready_) chat();
        if (link_->divergedAt() != 0 && !divergenceSaid_) {
            divergenceSaid_ = true;
            line("!! the mirror diverged from the server at tick %u", link_->divergedAt());
        }
        if (!link_->connected()) {
            line("the line is gone: %s", link_->cutWhy().c_str());
            return false;
        }
        return true;
    }

    // His line, while it can step his world's mirror.
    client::RemoteLink* stepper() const { return link_ && link_->connected() && !leaving_ ? link_.get() : nullptr; }

    // A tick of his world's mirror stepped: what it said heard, and a decision when one is due.
    void heard() {
        if (leaving_ || !link_) return;
        if (!ready_) {
            if (!shared_->mirror->lookAs(you_)) return;
            arrived();
        }
        brain()->heardOn(*mirror());
        if (!claude_) talk_.happened(*mirror(), you_, brain()->clockNow());
        if (claude_ && brain()->clockNow() % 20 == 0) drive();
        giftStep();
        takeStep();
        // Gone to another world: the next connect asks where he is (the thread's leave()).
        if (leaving()) {
            leaving_ = true;
            return;
        }
        // On the world's tick, not his own clock (which counts from his first join): as often,
        // every bot on the mirror on the same tick, so its bytes are laid once (Shared::snapshot).
        if (shared_->mirror->tick() % kThinkNet == 0) think();
    }
    bool gone() const { return leaving_; }

    void report() {
        if (!up()) return;
        brain()->status();
        // His world's router, by its first bot: what the mirror's (and the server's) walking costs.
        if (!shared_->members.empty() && shared_->members.front() == this) {
            const sim::Router& r = shared_->mirror->router();
            line("[router] %s: %llu searches, %llu failed (%llu cut short), %llu expansions, %.0f a search, worst %u",
                 shared_->world.c_str(), (unsigned long long)r.searches(), (unsigned long long)r.failures(),
                 (unsigned long long)r.cutShort(), (unsigned long long)r.expansions(),
                 r.searches() ? double(r.expansions()) / double(r.searches()) : 0.0, r.worstExpansions());
        }
    }

    // ---- the party (docs/game/party.md): what the board (Board, below) reads and says ----------
    const std::string& name() const { return name_; }
    sim::Kin kin() const { return kin_; }
    bool up() const { return link_ && link_->connected() && shared_ && ready_; }
    // His world's mirror and his brain, each looked at as him first: the mirror is this thread's
    // bots' on his world (Shared), and the brain reads it between decisions (Bot::look).
    sim::Realm* mirror() const {
        if (!shared_) return nullptr;
        shared_->mirror->lookAs(you_);
        return shared_->mirror.get();
    }
    Bot* brain() const {
        if (shared_ && ready_) shared_->mirror->lookAs(you_);
        return brain_.get();
    }
    // The Claude bot plays his own party (claude.h): the board leaves him be.
    bool driven() const { return claude_ != nullptr; }
    int level() const { return mirror()->hero().level; }
    int64_t clockNow() const { return brain()->clockNow(); }
    const std::string& world() const { return world_; }
    std::vector<Bot::Need> needs() { return brain()->needs(); }
    bool helps(int map, int breed) { return brain()->helps(map, breed); }
    int castlesOpen() const { return brain()->castlesOpen(); }
    int hornsForBoard() const { return brain()->hornsForBoard(); }
    double strength() const { return brain()->strength(); }
    void collectHorns(bool on) { brain()->collectHorns(on); }
    bool wantsWings() const { return brain()->wantsWings(); }
    const std::vector<Bot::Ground>& grounds() { return brain()->grounds(); }
    bool castleTime() const { return brain()->castleTime(); }
    bool inCastle() const { return brain()->inCastle(); }
    // Which castle or square he is inside now: 100 + the castle, 200 + the square, or 0.
    int eventInside() const {
        if (!mirror()) return 0;
        if (brain()->inCastle()) return 100 + mirror()->castleRun().castle;
        if (brain()->inSquare()) return 200 + mirror()->squareRun().square;
        return 0;
    }
    int squaresOpen() const { return brain()->squaresOpen(); }
    bool squareTime() const { return brain()->squareTime(); }
    bool inSquare() const { return brain()->inSquare(); }
    const net::PartyState& partyState() const { return link_->party(); }
    void askParty(net::PartyWhat what, const std::string& whom) { link_->askParty({what, whom}); }
    void sayOn(net::Channel channel, const std::string& text) {
        line("~ says: %s", text.c_str());
        link_->say({channel, {}, text});
    }
    // ---- players, not bots: whom he may ask into a party (Board::ask) ----------------------
    struct Person {
        std::string name;
        int level = 0;
    };
    // Every player who is not a bot, alive on his map, however far: an invite has no reach, as
    // WoW's has none (the user, 2026-10-11: "we dont need minimal tile distance to invite to
    // parties/raids/guild same like its on WoW"). His mirror holds his own map only.
    std::vector<Person> people() const {
        std::vector<Person> here;
        for (const sim::Body& b : mirror()->bodies()) {
            if (!b.player || b.gone || b.id == you_ || !b.alive()) continue;
            std::string name;
            bool isBot = true;
            if (!link_->nameOf(b.id, &name, &isBot) || isBot || name.empty()) continue;
            here.push_back({name, b.level});
        }
        return here;
    }
    // He asks `who` into a party; once `who` joins, he hands him the lead, so he follows the
    // player and not the player him (Board::follow).
    void invitePerson(const std::string& who) {
        invitedPerson_ = who;
        line("invites %s", who.c_str());
        askParty(net::PartyWhat::Invite, who);
    }
    // What the server answered his invites to players since the board last asked.
    enum class Answer { Declined, Busy, Joined };
    std::vector<std::pair<std::string, Answer>> takeAnswers() { return std::exchange(answers_, {}); }

    // The leader whose invite he takes, "" for none; and whether, with none, he takes anyone's.
    void expect(const std::string& leader) { expected_ = leader; }
    void openToAll(bool open) { openToAll_ = open; }
    // The party's hunt, its leader and tank by name: found on his own map, else none.
    void lead(Bot::Lead lead, const std::string& leader, const std::string& tank) {
        lead.leader = idOf(leader);
        lead.tank = idOf(tank);
        brain()->lead(std::move(lead));
    }

    // --claude DIR: he is the Claude bot (claude.h).
    void driveFrom(const std::string& dir) { claude_ = std::make_unique<bot::Claude>(dir); }

    // ---- trading (the Broker's errands, errands.h): what they read ------------------------
    int mapNow() const { return brain()->mapNow(); }
    bool alive() const { return mirror()->hero().alive(); }
    int column() const { return mirror()->hero().column(); }
    int row() const { return mirror()->hero().row(); }
    const content::Tables* tables() const { return mirror()->tables(); }
    std::vector<int> spares() { return brain()->spares(); }
    int bagRoom() const { return brain()->bagRoom(); }
    int trips() const { return brain()->trips(); }
    bool support() const { return brain()->support(); }
    // A trader (NAME:CLASS/t): the market his trade -- four visits to another's one, flips, and a
    // purse spent on his gear (errands.h).
    bool trader() const { return trader_; }
    bool magic() const { return magic_; }
    bool fuseBuyAt(const sim::Held& one, int64_t price) const { return brain()->buysAt(one, price); }
    // How many spares he keeps from the blacksmith: the room his listings have left (Errands).
    void holdForBroker(int count) { brain()->holdForBroker(count); }
    sim::Held held(int slot) const { return mirror()->satchel()[slot]; }
    // What `one` is worth to him, tried on his scratch (Bot::worth).
    double worth(const sim::Held& one) {
        if (!layScratch()) return 0.0;
        const double gain = brain()->worth(*scratch_, one);
        brain()->look(*mirror());
        return gain;
    }
    // ---- the Broker's errands (errands.h): the market, through the server -----------------
    void askBroker(const net::BrokerAsk& asked) { link_->askBroker(asked); }
    bool takeBrokerView(net::BrokerView& out) { return link_ && link_->takeBrokerView(out); }
    bool atBroker() const { return mirror()->atBroker(you_); }
    int64_t money() const { return mirror()->money(); }
    // To (column, row) and waiting there while he deals with the Broker; -1 lets him go.
    void errand(int column, int row) {
        errand_ = column >= 0;
        if (errand_) brain()->meet(column, row);
        else brain()->unmeet();
    }
    bool onErrand() const { return errand_; }

    // ---- the guild (guilds.h): what the guilds' board reads and asks ---------------------
    const net::GuildState& guildState() const { return link_->guild(); }
    void askGuild(const net::GuildAsk& asked) { link_->askGuild(asked); }
    std::vector<net::GuildCall> takeGuildCalls() { return std::exchange(guildCalls_, {}); }
    std::vector<std::string> takeGuildLines() { return std::exchange(guildLines_, {}); }
    void note(const std::string& text) const { line("%s", text.c_str()); }
    bool magicPath() const { return magic_; }
    // Told to grind a map (the founder's way to Devias), or -1 to choose again.
    bool order(int map) { return brain()->order(map); }
    int ordered() const { return brain()->ordered(); }
    // The Guild Master's tile on his map, if he stands on it.
    bool guildMasterTile(int* column, int* row) const {
        if (!mirror() || !mirror()->tables()) return false;
        for (const content::Townsperson& one : mirror()->tables()->folk) {
            if (one.number != sim::kGuildMaster) continue;
            *column = one.x, *row = one.y;
            return true;
        }
        return false;
    }
    // His guildmates by name, whose trades he takes (takeStep).
    void mates(std::set<std::string> names) { mates_ = std::move(names); }
    // **A gift to a guildmate** (the user, 2026-10-13: guild bots "act different, their goal is to
    // be stronger as group"; "trades has to happen anywhere"): he walks to `to` on this map,
    // asks him to trade, puts the things and the Zen in and accepts; `to` takes it (takeStep).
    // Played a tick at a time (giftStep): at mu2_server --speed 100 a trade's ask stands 0.2 s.
    void give(const std::string& to, std::vector<sim::Held> things, int64_t zen, std::string what) {
        gift_ = Gift{};
        gift_.active = true;
        gift_.to = to;
        gift_.things = std::move(things);
        gift_.zen = zen;
        gift_.what = std::move(what);
        gift_.since = brain()->clockNow();
        line("[guild] goes to give %s %s", to.c_str(), gift_.what.c_str());
    }
    bool giving() const { return gift_.active; }
    // The wall's seconds since this line joined: his guild is heard a moment after.
    double upFor() const { return std::chrono::duration<double>(std::chrono::steady_clock::now() - joinedAt_).count(); }
    // Busy with a guildmate's trade, his or the other's: the Broker's errands wait.
    bool guildBusy() const { return gift_.active || holding_ || founding_; }
    // On his way to found a guild (guilds.h): the Broker's errands leave him be.
    void founding(bool on) { founding_ = on; }
    // How the last gift ended, once: 1 given, -1 not, 0 none ended since.
    int takeGiftEnd() { return std::exchange(giftEnd_, 0); }
    bool inTown() const { return mirror() && mirror()->tables() && mirror()->tables()->grid.safe(column(), row()); }

private:
    // The things packed as the Broker's commands carry them: two the same pack the same.
    static bool sameThing(const sim::Held& a, const sim::Held& b) {
        sim::Command x, y;
        sim::packHeld(a, x);
        sim::packHeld(b, y);
        return x.a == y.a && x.b == y.b && x.c == y.c && x.d == y.d && x.target == y.target;
    }
    void sendDeal(sim::Command::Kind kind, int a = -1, int b = -1, uint32_t target = 0, int64_t zen = 0) {
        sim::Command one;
        one.kind = kind;
        one.player = you_;
        one.a = a;
        one.b = b;
        one.target = target;
        one.zen = zen;
        link_->send(one);
    }
    // His trade's news this tick: the DealEvent said to him, or -1.
    int dealNews(int32_t* b = nullptr, int32_t* c = nullptr) const {
        for (const sim::Happening& h : mirror()->happenings()) {
            if (h.what != sim::What::Deal || h.who != you_) continue;
            if (b) *b = h.b;
            if (c) *c = h.c;
            return h.a;
        }
        return -1;
    }
    void endGift(bool given, const char* why) {
        if (mirror()->dealing() != 0) sendDeal(sim::Command::Kind::Close, int(sim::Command::Window::Deal));
        if (!errand_) brain()->unmeet();
        line("[guild] %s %s %s%s%s", given ? "gives" : "could not give", gift_.to.c_str(), gift_.what.c_str(),
             given ? "" : ": ", given ? "" : why);
        giftEnd_ = given ? 1 : -1;
        gift_ = Gift{};
    }
    // The giver's side, a tick at a time.
    void giftStep() {
        if (!gift_.active) return;
        const int64_t now = brain()->clockNow();
        const int news = dealNews();
        if (news == int(sim::DealEvent::Done)) return endGift(true, "");
        if (!mirror()->hero().alive()) return endGift(false, "he died");
        if (mirror()->dealing() != 0) {
            if (!gift_.opened) {
                gift_.opened = true;
                gift_.openedAt = now;
                for (const sim::Held& thing : gift_.things) {
                    for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
                        if (!mirror()->satchel()[slot].empty() && sameThing(mirror()->satchel()[slot], thing)) {
                            sendDeal(sim::Command::Kind::DealPut, slot, -1);
                            break;
                        }
                    }
                }
                if (gift_.zen > 0) sendDeal(sim::Command::Kind::DealZen, -1, -1, 0, std::min(gift_.zen, mirror()->money()));
                return;
            }
            if (now - gift_.openedAt > 60 * 20) return endGift(false, "the trade stood a minute");
            if (now - gift_.openedAt > sim::kDealHoldTicks + 10 && !mirror()->dealAccepted(false) && now - gift_.sentAt > 20) {
                gift_.sentAt = now;
                sendDeal(sim::Command::Kind::DealAccept, 1);
            }
            return;
        }
        if (gift_.opened) return endGift(false, "the trade was called off");
        if (now - gift_.since > 4 * 60 * 20) return endGift(false, "he never reached him");
        const uint32_t whom = idOf(gift_.to);
        const sim::Body* him = nullptr;
        for (const sim::Body& b : mirror()->bodies()) {
            if (b.id == whom && whom != 0 && !b.gone) him = &b;
        }
        if (him == nullptr) return endGift(false, "he is not on this map");
        brain()->meet(him->column(), him->row());
        const sim::Body& me = mirror()->hero();
        if (std::max(std::abs(me.column() - him->column()), std::abs(me.row() - him->row())) <= sim::kDealReach - 1 &&
            now - gift_.sentAt > 5 * 20) {
            gift_.sentAt = now;
            sendDeal(sim::Command::Kind::DealAsk, -1, -1, whom);
        }
    }
    // The taker's side: a guildmate's ask said yes to, and his offer accepted once he has
    // accepted it, nothing put up in return. He stands still the while.
    void takeStep() {
        if (mates_.empty() || gift_.active) return;
        const int64_t now = brain()->clockNow();
        int32_t got = 0, zen = 0;
        if (dealNews(&got, &zen) == int(sim::DealEvent::Done) && holding_) {
            line("[guild] takes %d thing%s and %d zen from %s", got, got == 1 ? "" : "s", zen, holdingFor_.c_str());
        }
        const auto mate = [&](uint32_t id, std::string* who) {
            bool isBot = false;
            return id != 0 && link_->nameOf(id, who, &isBot) && mates_.count(*who) > 0;
        };
        std::string who;
        if (mirror()->dealing() != 0) {
            if (!holding_ || !mate(mirror()->dealing(), &who)) return;
            const sim::Body& me = mirror()->hero();
            brain()->meet(me.column(), me.row());
            if (mirror()->dealAccepted(true) && !mirror()->dealAccepted(false) && mirror()->dealHold() == 0 && now - takeSentAt_ > 20) {
                takeSentAt_ = now;
                sendDeal(sim::Command::Kind::DealAccept, 1);
            }
            return;
        }
        const uint32_t asker = mirror()->dealAskedBy();
        if (asker != 0 && !errand_ && mate(asker, &who)) {
            if (now - takeSentAt_ > 10) {
                takeSentAt_ = now;
                holding_ = true;
                holdingFor_ = who;
                const sim::Body& me = mirror()->hero();
                brain()->meet(me.column(), me.row());
                sendDeal(sim::Command::Kind::DealAnswer, 1, -1, asker);
            }
            return;
        }
        if (holding_ && now - takeSentAt_ > 3 * 20) {
            holding_ = false;
            if (!errand_) brain()->unmeet();
        }
    }
    // A player's body on his mirror by the name the server gave it, or 0.
    uint32_t idOf(const std::string& who) const {
        if (who.empty()) return 0;
        if (who == name_) return you_;
        for (const sim::Body& b : mirror()->bodies()) {
            std::string name;
            bool isBot = false;
            if (b.player && link_->nameOf(b.id, &name, &isBot) && name == who) return b.id;
        }
        return 0;
    }

    // **The plan, on the party's chat**, once the player he asked has come in (the user,
    // 2026-10-11: "when bot invites me to party and i accept he need to write oon party chat
    // what is the plan"): the quest he is on, what is left of it and where, and that the player
    // leads and he follows.
    void sayPlan(const std::string& who) {
        std::string plan = "no quest on me right now, I'm just levelling.";
        for (const Bot::Need& n : brain()->needs()) {
            if (!n.taken || n.left <= 0 || n.quest < 0) continue;
            const sim::QuestRow& quest = sim::questAt(n.quest);
            const WorldRow* at = worldOf(n.map);
            std::string where = at ? at->name : "";
            if (!where.empty()) where[0] = char(std::toupper(uint8_t(where[0])));
            plan = std::string("I'm on ") + quest.giverName + "'s quest " + quest.title + " -- " + std::to_string(n.left) +
                   " " + n.label + " still to kill" + (where.empty() ? "" : " in " + where) +
                   (!n.alone && n.together ? ", too tough for me alone." : ".");
            break;
        }
        sayOn(net::Channel::Party, "Thanks for joining, " + who + "! Plan: " + plan);
        sayOn(net::Channel::Party, "You lead; I'll follow you and fight beside you, wherever you hunt.");
    }

    // **The Claude bot**, each second: the model's orders carried out, and the state it reads.
    void drive() {
        const int64_t now = brain()->clockNow();
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
                brain()->order(-1);
                answer = "the brain chooses again";
            } else if (verb == "go") {
                int map = -1;
                for (const WorldRow& w : worldRows()) {
                    if (first == w.name) map = w.map;
                }
                answer = map < 0 ? "no world " + first
                         : brain()->order(map) ? "grinds " + first
                                              : first + " is out of his reach";
            } else {
                answer = "not an order: " + order;
            }
            if (!answer.empty()) claude_->note(clock(now), "  " + answer);
        }
        claude_->state(stateText());
    }

    std::string stateText() const {
        const sim::Body& me = mirror()->hero();
        const content::Tables& tables = *tables_.at(world_);
        std::string out;
        char text[512];
        const auto put = [&](const char* fmt, auto... args) {
            std::snprintf(text, sizeof(text), fmt, args...);
            out += text;
        };
        put("%s, %s level %d -- clock %s\n", name_.c_str(), className(me.kin, me.second), me.level, clock(brain()->clockNow()).c_str());
        put("health %d/%d  mana %d/%d  zen %lld  points to spend %d\n", me.health, me.maxHealth, me.mana,
            me.maxMana, (long long)mirror()->money(), me.pointsInHand);
        put("in %s at %d,%d%s\n", world_.c_str(), me.column(), me.row(), me.alive() ? "" : " -- DEAD");
        put("brain: %s%s; %d kills, %d deaths this run\n", brain()->aimLine().c_str(),
            brain()->ordered() >= 0 ? " (ordered)" : "", brain()->kills(), brain()->deaths());
        int potions = 0, carried = 0;
        out += "wears:";
        for (int slot = 0; slot < sim::kSlots; ++slot) {
            const sim::Held& one = mirror()->satchel()[slot];
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
        for (const sim::Body& b : mirror()->bodies()) {
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
        put("players on this map: %d\n", mirror()->playersHere());
        out += "quests:\n";
        const char* states[] = {"untaken", "active", "ready to hand in", "resting"};
        for (int q = 0; q < sim::kQuests; ++q) {
            const sim::QuestProgress& p = mirror()->quest(q);
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
        std::printf("  [%s] %s %s\n", clock(brain()->clockNow()).c_str(), name_.c_str(), text);
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
        const int64_t now = brain()->clockNow();
        party();
        // A guild's cards (docs/game/guild.md): an invite to him, or, a master, a player asking in --
        // answered by the guilds' board (guilds.h), which knows what the guild wants.
        for (net::GuildCall call; link_->takeGuildCall(call);) guildCalls_.push_back(std::move(call));
        for (const net::Heard& one : heard_) {
            if (one.name.empty() && one.channel == net::Channel::Guild) guildLines_.push_back(one.text);
            // The invite the board told him to wait for, taken; and, while the board has him in
            // no party of its own, anyone's -- a player's /invite is answered (Board::follow).
            // A raid's invite is said so (server: Party::word()).
            constexpr const char* kInvites = " invites you to a party.";
            constexpr const char* kRaidInvites = " invites you to a raid.";
            if (one.name.empty() && one.channel == net::Channel::Party && !invitedPerson_.empty()) {
                // The player he asked: refused, already in a party, or come in. A player who has
                // come in leads from now on (server: Promote).
                const std::string& who = invitedPerson_;
                if (one.text == who + " declined your invitation.") {
                    line("%s declined; let be a while", who.c_str());
                    answers_.push_back({who, Answer::Declined});
                    invitedPerson_.clear();
                } else if (one.text == who + " is already in a party." || one.text == who + " is not online.") {
                    answers_.push_back({who, Answer::Busy});
                    invitedPerson_.clear();
                } else if (one.text == who + " has joined the party." || one.text == who + " has joined the raid.") {
                    answers_.push_back({who, Answer::Joined});
                    link_->askParty({net::PartyWhat::Promote, who});
                    sayPlan(who);
                    invitedPerson_.clear();
                }
            }
            if (one.name.empty() && one.channel == net::Channel::Party) {
                size_t at = one.text.find(kInvites);
                if (at == std::string::npos) at = one.text.find(kRaidInvites);
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

    // His scratch laid from the mirror as it stands, as him: the world's bytes are laid once a
    // tick for every bot on it (Shared::snapshot).
    bool layScratch() {
        const std::vector<uint8_t>* bytes = shared_ ? shared_->snapshot() : nullptr;
        return bytes && scratch_->restoreSnapshot(*bytes) && scratch_->lookAs(you_);
    }

    // A decision on the scratch, laid from the mirror as it stands, once the last one's answers
    // are in; what it asked, to the server.
    void think() {
        if (pending_ != 0 && std::chrono::steady_clock::now() - pendingSince_ < kAnswerWait) return;
        // Every one kept until its answer, however late: cleared at the wait's end, an answer 45
        // s late (a net bot behind its server) found no trip of his and he stayed, the server
        // holding him due elsewhere (the Claude bot's runs, 2026-10-09).
        if (!trips_.empty() && std::chrono::steady_clock::now() - tripSince_ < kTripWait) return;
        if (!layScratch()) {
            line("the scratch would not lay");
            return;
        }
        asked_.clear();
        const uint32_t before = brain()->lastTicket();
        brain()->thinkOn(*scratch_, asked_);
        brain()->look(*mirror());
        for (const sim::Command& one : asked_) {
            link_->send(one);
            if (one.kind == sim::Command::Kind::Travel) {
                trips_.push_back(one);
                tripSince_ = std::chrono::steady_clock::now();
            }
        }
        if (brain()->lastTicket() != before) {
            pending_ = brain()->lastTicket();
            pendingSince_ = std::chrono::steady_clock::now();
        }
    }

    // Whether the tick sends him to another world, as the server reads it (server/src/main.cpp,
    // landingOf); and the last decision's answers heard.
    bool leaving() {
        // Blood Castle's run over and its rest out, or his win claimed: the server has him due in
        // Devias, and waits for the client to come (server/src/main.cpp, castleOut).
        if (brain()->mapNow() == int(sim::kBloodCastleMap) && mirror()->castleRun().sentOut) {
            toEventHome();
            return true;
        }
        for (const sim::Happening& h : mirror()->happenings()) {
            if (h.what == sim::What::Answered && h.who == you_ && uint32_t(h.c) == pending_) pending_ = 0;
            if (h.who != you_) continue;
            switch (h.what) {
                case sim::What::Gated:
                case sim::What::WentBack:
                    // **Into the world the gate leads to**, as the client asks (game/play.cpp,
                    // gated_): back on his old one, the server's redirect sent an event map's
                    // landing to its home town, and Rhea, let into Devil Square 1, woke in Noria
                    // (2026-10-12).
                    // A gate's `a` is its entry gate, whose exit names the map (server landingOf);
                    // WentBack's is the map itself. Read as a map, the castle's gate was none, and
                    // eleven cloaks given to the Messenger in a run woke their bots in Devias.
                    if (const sim::MapRow* to = h.what == sim::What::WentBack ? sim::mapNumbered(h.a) : gateMap(h.a);
                        to && to->world && *to->world) {
                        world_ = to->world;
                    }
                    return true;
                case sim::What::Warped:
                case sim::What::Rose:
                    if (h.c == 1) {
                        toEventHome();
                        return true;
                    }
                    break;
                case sim::What::Answered:
                    if (h.a == int32_t(sim::Command::Kind::Travel) && h.b > 0) {
                        for (const sim::Command& one : trips_) {
                            if (one.kind != sim::Command::Kind::Travel || one.ticket != uint32_t(h.c)) continue;
                            const sim::TravelRow& to = sim::travelAt(one.a);
                            if (to.map != int32_t(brain()->mapNow())) return true;
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

    // Out of a castle or square -- its run over, or dead in it -- he is the server's in its home
    // town (server placeOf): asked for the event's world again, 41 joins of a run were told so and
    // came again (2026-10-13).
    void toEventHome() {
        const sim::MapRow* here = sim::mapNumbered(brain()->mapNow());
        if (here == nullptr || !here->event) return;
        if (const sim::MapRow* town = sim::mapNumbered(here->home); town && town->world && *town->world) world_ = town->world;
    }

    // The map an entry gate leads to: its exit's (server/src/world.cpp landingOf).
    static const sim::MapRow* gateMap(int enter) {
        const sim::EnterGate* in = sim::enterGateNumbered(enter);
        const sim::ExitGate* out = in ? sim::exitGate(in->target) : nullptr;
        return out ? sim::mapNumbered(int(out->map)) : nullptr;
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
    std::chrono::steady_clock::time_point sheetAt_{};
    bool trader_ = false;
    bool magic_ = false;  // a magic gladiator (mmg), not a melee one
    uint64_t token_ = 0;
    std::string world_;
    bot::Talk talk_;
    std::vector<net::Heard> heard_;
    std::vector<net::Say> says_;

    std::unique_ptr<Bot> brain_;
    std::map<std::string, std::unique_ptr<content::Tables>> tables_;
    std::shared_ptr<Shared> shared_;  // his world's mirror, with this thread's other bots on it
    std::unique_ptr<sim::Realm> scratch_;
    bool ready_ = false;    // his player on the mirror, and the brain on it (arrived)
    bool leaving_ = false;  // gone to another world this turn: the thread lets him go (leave)
    std::unique_ptr<client::RemoteLink> link_;
    std::vector<sim::Command> asked_;
    std::vector<sim::Command> trips_;  // paid trips not yet answered (kTripWait)
    std::chrono::steady_clock::time_point tripSince_{};
    uint32_t you_ = 0;
    uint32_t pending_ = 0;
    std::chrono::steady_clock::time_point pendingSince_{};
    bool divergenceSaid_ = false;
    std::string expected_;  // the board's: whose invite he takes
    bool openToAll_ = false;  // ... and, with none, anyone's (Board::follow)
    std::string invitedPerson_;  // the player he last asked, until the server answers (invitePerson)
    std::vector<std::pair<std::string, Answer>> answers_;  // takeAnswers
    std::string partySaid_;
    std::unique_ptr<bot::Claude> claude_;  // --claude: the model drives him
    bool errand_ = false;      // at or on his way to the Broker (errands.h)
    // The guild (guilds.h): its cards and lines for the guilds' board, his guildmates, and a gift.
    std::vector<net::GuildCall> guildCalls_;
    std::vector<std::string> guildLines_;
    std::set<std::string> mates_;
    struct Gift {
        bool active = false, opened = false;
        std::string to, what;
        std::vector<sim::Held> things;
        int64_t zen = 0;
        int64_t since = 0, openedAt = 0, sentAt = -1000000;
    };
    Gift gift_;
    int giftEnd_ = 0;
    bool holding_ = false;  // standing still for a guildmate's trade (takeStep)
    bool founding_ = false;
    std::chrono::steady_clock::time_point joinedAt_{};
    std::string holdingFor_;
    int64_t takeSentAt_ = -1000000;
};

// Each of this thread's worlds a share of its ticks a turn (kStepsATurn, as each bot's backlog
// was before), each tick stepped once with whichever line has it, then heard by every bot on it.
// True while any world is still behind its lines.
bool Pool::advance() {
    bool behind = false;
    for (const auto& weak : worlds) {
        const std::shared_ptr<Shared> shared = weak.lock();
        if (!shared) continue;
        for (int stepped = 0;; ++stepped) {
            const int64_t at = shared->mirror->tick();
            client::RemoteLink* by = nullptr;
            for (NetBot* one : shared->members) {
                client::RemoteLink* line = one->stepper();
                if (!line) continue;
                line->passThrough(at);
                if (!by && line->nextTick() == at + 1) by = line;
            }
            if (!by) break;
            if (stepped == kStepsATurn) {
                behind = true;
                break;
            }
            by->step();
            for (NetBot* one : shared->members) one->heard();
        }
    }
    return behind;
}

// **The board** (docs/game/party.md): the run's bots' parties, formed and led. The user, 2026-10-09:
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
        // The board's own game clock: the median step of the bots' clocks since the last round.
        // A bot's clock is his map's tick, which differs from map to map, so a hold reckoned on
        // whichever bot came first ended at random (the raid parted and gathered every round).
        {
            std::vector<int64_t> steps;
            for (const auto& [name, one] : byName_) {
                const int64_t c = one->clockNow();
                const auto it = lastClock_.find(name);
                if (it != lastClock_.end() && c >= it->second && c - it->second < 20 * 60 * 20) steps.push_back(c - it->second);
                lastClock_[name] = c;
            }
            if (!steps.empty()) {
                std::nth_element(steps.begin(), steps.begin() + long(steps.size() / 2), steps.end());
                boardClock_ += steps[steps.size() / 2];
            }
        }
        guildGroups();
        std::erase_if(groups_, [](const Group& g) { return g.gone; });
        for (Group& g : groups_) lead(g, now);
        std::erase_if(groups_, [](const Group& g) { return g.gone; });
        merge();
        std::erase_if(groups_, [](const Group& g) { return g.gone; });
        follow();
        loose(now);
        if (forms_) elite();
        std::erase_if(groups_, [](const Group& g) { return g.gone; });
        if (forms_) duos();
        if (forms_) form(now);
        hornCollectors();
        ask(now);
    }

    // **Three collect the horns** (the user, 2026-10-13: "only the 2-3 collectors closest to 10
    // keep buying; the rest list theirs"): of the bots who could make a Dinorant, those owning
    // the most horns, ties by name; the rest sell theirs at the Broker to them. Said when it
    // changes.
    static constexpr int kHornCollectors = 3;
    std::set<std::string> collectors_;
    void hornCollectors() {
        std::vector<std::pair<int, std::string>> owners;
        for (const auto& [name, one] : byName_) {
            if (const int horns = one->hornsForBoard(); horns >= 0) owners.push_back({-horns, name});
        }
        std::sort(owners.begin(), owners.end());
        std::set<std::string> chosen;
        for (size_t i = 0; i < owners.size() && i < size_t(kHornCollectors); ++i) chosen.insert(owners[i].second);
        for (const auto& [name, one] : byName_) one->collectHorns(chosen.count(name) > 0);
        if (chosen != collectors_) {
            std::string said;
            for (const auto& [horns, name] : owners) {
                if (chosen.count(name)) said += (said.empty() ? "" : ", ") + name + " (" + std::to_string(-horns) + ")";
            }
            std::printf("  [board] horn collectors: %s\n", said.c_str());
            collectors_ = chosen;
        }
    }

    // **The guilds' parties** (guilds.h): each guild's master plans its online members into
    // parties, across every netbot of the run. `bound` is each member of one, by name, with its
    // leader; `parties` are those this netbot's bots lead. Set before each tick.
    struct GuildParty {
        std::string guild, leader;
        std::vector<std::string> members;  // the leader first
    };
    void guildPlan(std::map<std::string, std::string> bound, std::vector<GuildParty> parties) {
        bound_ = std::move(bound);
        guildParties_ = std::move(parties);
    }

private:
    std::map<std::string, std::string> bound_;  // guildPlan
    std::vector<GuildParty> guildParties_;
    void drop(const std::string& m) {
        if (NetBot* one = bot(m)) {
            one->askParty(net::PartyWhat::Leave, "");
            one->expect("");
            one->lead(Bot::Lead{}, "", "");
        }
    }
    // The guilds' plan made the board's groups: a guild party led here as any kept party
    // (forFarm, keeps -- the most experience an hour they stand together, a member's stuck quest
    // first, an event's door together), and its members out of every other group. A guildmate
    // led from another netbot waits for that leader's invite (follow() takes him there).
    void guildGroups() {
        for (Group& g : groups_) {
            if (g.gone) continue;
            if (g.guild) {
                // Its planned leader again, whatever rule of the board's handed it on.
                if (!g.planned.empty()) g.leader = g.planned;
                const GuildParty* want = nullptr;
                for (const GuildParty& p : guildParties_) {
                    if (p.leader == g.leader) want = &p;
                }
                // Not dropped while its leader is between maps (his line down a moment): only
                // once the plan has no such party.
                if (want == nullptr) {
                    for (const std::string& m : g.members) drop(m);
                    std::printf("  [board] %s's guild party parts\n", g.leader.c_str());
                    g.gone = true;
                } else if (want->members != g.members) {
                    for (const std::string& m : g.members) {
                        if (std::find(want->members.begin(), want->members.end(), m) == want->members.end()) drop(m);
                    }
                    g.members = want->members;
                    g.invited = -1000000;
                }
                continue;
            }
            const bool leaderBound = bound_.count(g.leader) > 0;
            std::vector<std::string> kept;
            for (const std::string& m : g.members) {
                if (bound_.count(m)) {
                    if (!leaderBound) drop(m);
                } else {
                    kept.push_back(m);
                }
            }
            if (leaderBound || (kept.size() < 2 && !g.fixed)) {
                for (const std::string& m : kept) drop(m);
                g.gone = true;
                continue;
            }
            g.members = std::move(kept);
        }
        for (const GuildParty& p : guildParties_) {
            if (!byName_.count(p.leader)) continue;
            bool have = false;
            for (const Group& g : groups_) have |= !g.gone && g.guild && g.leader == p.leader;
            if (have) continue;
            Group g;
            g.members = p.members;
            g.leader = p.leader;
            g.planned = p.leader;
            g.guild = true;
            g.guildName = p.guild;
            g.forFarm = true;
            g.keeps = true;
            std::string said;
            for (const std::string& m : p.members) said += (said.empty() ? "" : ", ") + m;
            std::printf("  [board] %s's guild party: %s\n", p.guild.c_str(), said.c_str());
            groups_.push_back(std::move(g));
        }
        // Each bound bot here takes his leader's invite, and leaves any other party for it.
        for (const auto& [name, leader] : bound_) {
            NetBot* one = bot(name);
            if (!one) continue;
            if (name != leader) one->expect(leader);
            const net::PartyState& state = one->partyState();
            if (!state.members.empty() && state.members.front().name != leader) {
                auto& at = leftAt_[name];
                if (at == 0 || one->clockNow() - at > 10 * 20) {
                    at = one->clockNow();
                    one->askParty(net::PartyWhat::Leave, "");
                }
            }
        }
    }

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
        int square = 0;           // the Devil Square they go into together, 0 for none
        bool forSquare = false;   // formed for it, at Charon's gate
        int saidSquare = 0;
        bool forFarm = false;     // formed to farm for wings (Board::farm)
        bool keeps = false;       // a strong party kept together past its quest (strong())
        bool duo = false;         // a tank and his energy elf, kept together for the run (duos())
        bool elite = false;       // the strongest of the run, together (elite())
        bool guild = false;       // a guild's party, as its master planned it (guildGroups())
        std::string guildName, planned;  // ... and its leader as planned
        std::pair<int, int> said{-1, -1};  // the hunt last said on Party
        std::pair<int, int> saidFarm{-1, -1};  // the strongest's ground last logged
    };

    NetBot* bot(const std::string& name) const {
        const auto it = byName_.find(name);
        return it == byName_.end() ? nullptr : it->second;
    }
    // Stuck: a breed he would take beside a party and not alone -- past the towns' own quests
    // (the user, 2026-10-09: "it make sense they go together on dungeons"). At level 1 every
    // breed reads too hard alone, and parties for Marlon's and Peia's held five bots 40-80 levels
    // under the same five solo; a knight made their tank at level 1 died broke and ended at 17.
    // Stuck: a breed he would take beside a party and not alone, and within his reach
    // (Need::reach) -- any quest's, the towns' too.
    static bool stuck(const Bot::Need& n) { return n.together && !n.alone && n.reach; }
    static bool tankish(sim::Kin kin) { return kin == sim::Kin::DarkKnight || kin == sim::Kin::MagicGladiator; }
    bool grouped(const std::string& name) const {
        if (bound_.count(name)) return true;
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
            // More than a party's five: his party made a raid first (net::PartyWhat::Raid), once
            // it stands and he leads it, so the sixth's invite is not refused as full.
            if (g.members.size() > size_t(sim::kPartyMost) && !state.raid && !state.members.empty() &&
                state.members.front().name == g.leader) {
                leader->askParty(net::PartyWhat::Raid, "");
                std::printf("  [board] %s makes the party a raid\n", g.leader.c_str());
            }
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
            if (!g.fixed && !g.duo && !g.guild && leader->clockNow() - g.emptySince > kSettles) {
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
            farmBusy = wanting >= 2 || g.keeps;
            std::map<std::pair<int, int>, int> levelOf;
            // **The most experience an hour** (the user, 2026-10-13: "best players somehow find
            // best grind methods and grind together as party of 5 to get exp boost"): each
            // member's kills of it an hour (Ground::perHour) summed, times a kill's worth at the
            // party's mean level (sim::killExperience, as partyExperience reckons it) -- not the
            // strongest breed they stand, which was slow to kill and paid little more.
            std::map<std::pair<int, int>, double> killsAnHour, supplied;
            int levels = 0, counted = 0;
            for (const std::string& m : g.members) {
                if (NetBot* one = bot(m); one && in(m)) {
                    levels += one->level();
                    ++counted;
                    for (const Bot::Ground& gr : one->grounds()) {
                        levelOf[{gr.map, gr.breed}] = gr.level;
                        killsAnHour[{gr.map, gr.breed}] += gr.perHour;
                        supplied[{gr.map, gr.breed}] = gr.supply;
                    }
                }
            }
            const int meanLevel = counted > 0 ? levels / counted : 1;
            const auto paysAnHour = [&](const std::pair<int, int>& key) {
                // No more than half of what its nests give an hour (Ground::supply): a party is
                // not everywhere on the map at once.
                return std::min(killsAnHour[key], kSupplyShare * supplied[key]) * sim::killExperience(levelOf[key], meanLevel);
            };
            const auto common = [&](const std::pair<int, int>& key) {
                return (g.tank.empty() || tankHas.count(key)) && have[key] * 2 >= up;
            };
            if (farmBusy && !(anyStuck && best.first >= 0)) {
                std::pair<int, int> top{-1, -1};
                double topScore = -1.0;
                for (const auto& [key, n] : have) {
                    if (!common(key)) continue;
                    const double score = paysAnHour(key);
                    if (score > topScore) topScore = score, top = key;
                }
                // The ground under way holds its twenty minutes, as a quest's hunt does.
                if (g.map >= 0 && have.count({g.map, g.breed}) && common({g.map, g.breed}) &&
                    leader->clockNow() - g.chosenAt < kHolds) {
                    top = {g.map, g.breed};
                }
                if (std::getenv("BOARD_TRACE") && top != std::pair<int, int>{g.map, g.breed}) {
                    std::printf("  [board] farm pick for %s: map %d breed %d (%s, level %d, %.0f exp an hour), tank %s\n",
                                g.leader.c_str(), top.first, top.second, label[top].c_str(), levelOf[top], topScore,
                                g.tank.c_str());
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
                if (g.elite && top.first >= 0 && top != g.saidFarm) {
                    g.saidFarm = top;
                    std::printf("  [board] the strongest with %s grind %s in map %d, about %.0f exp an hour (%zu of them)\n",
                                g.leader.c_str(), label[top].c_str(), top.first, topScore * sim::kExperienceRate,
                                g.members.size());
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
        // **Devil Square together**, the same way: the square two or more of them can go into.
        int square = 0;
        for (int s = sim::kSquaresBuilt; s >= 1 && square == 0; --s) {
            int can = 0;
            for (const std::string& m : g.members) {
                if (NetBot* one = bot(m); one && in(m) && (one->squaresOpen() & (1 << s))) ++can;
            }
            if (can >= 2) square = s;
        }
        for (const std::string& m : g.members) {
            if (NetBot* one = bot(m); one && square > 0 && (one->squareTime() || one->inSquare())) castleBusy = true;
        }
        if (square > 0 && square != g.saidSquare) {
            say(*leader, "Devil Square " + std::to_string(square) + " together at Charon's gate.");
            g.saidSquare = square;
        }
        g.square = square;
        // Ten game minutes with no one stuck, or nothing the party can stand: "stuck" turns on a
        // purse and a bag of potions, and a party broken up the moment it was not formed again
        // a minute later, all day.
        if ((anyStuck && best.first >= 0) || g.fixed || g.duo || castleBusy || farmBusy) g.clearSince = -1;
        else if (g.clearSince < 0) g.clearSince = leader->clockNow();
        // **A strong party stays together** (the user, 2026-10-12: "teach strong groups/parties
        // stick together for future quest clears or grinding"): three or more, within kKeepGap
        // levels, a tank among them -- past its quest it farms the strongest breed they stand
        // together (forFarm), and takes the next quest a member is stuck on as before. It
        // parts when the levels drift past kPartGap or fewer than three are left.
        if (g.keeps && !g.elite && !g.guild && !strong(g, kPartGap)) {
            g.keeps = false;
            g.forFarm = false;
            std::printf("  [board] %s's party has drifted apart\n", g.leader.c_str());
        }
        if (g.clearSince >= 0 && leader->clockNow() - g.clearSince > kSettles && !g.forCastle && !g.forSquare &&
            !g.keeps && strong(g, kKeepGap)) {
            g.keeps = true;
            g.forFarm = true;
            g.clearSince = -1;
            say(*leader, "Good group. Let's keep going together.");
            std::printf("  [board] %s's party stays together\n", g.leader.c_str());
        }
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
        // **A duo with nothing shared** (duos()): the tank his own way, the elf beside him on his
        // map, healing and raising him -- her lead's map is his, with no breeds, so she grinds
        // there by him (Bot::hunt's party keeping).
        if (g.duo && best.first < 0) {
            g.map = g.breed = -1;
            const int there = leaderMap(*leader);
            for (const std::string& m : g.members) {
                NetBot* one = bot(m);
                if (!one) continue;
                Bot::Lead lead;
                lead.castle = g.castle > 0 && (one->castlesOpen() & (1 << g.castle)) ? g.castle : 0;
                lead.square = g.square > 0 && (one->squaresOpen() & (1 << g.square)) ? g.square : 0;
                if (m != g.leader && in(m) && there >= 0) {
                    lead.map = there;
                    lead.size = int(state.members.size());
                    lead.healer = true;
                    lead.role = Bot::roleOf(one->kin(), false);
                }
                one->lead(std::move(lead), g.leader, g.tank);
            }
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
        // The castles each member holds a cloak for: the others' are halves he buys (mateCloakHalf).
        // And the squares each holds an invitation for (mateInviteHalf).
        std::map<std::string, std::pair<int, int>> tickets;
        for (const std::string& m : g.members) {
            if (NetBot* one = bot(m)) tickets[m] = {one->castlesOpen(), one->squaresOpen()};
        }
        const auto mates = [&](const std::string& me, bool square) {
            int bits = 0;
            for (const auto& [m, t] : tickets) {
                if (m != me) bits |= square ? t.second : t.first;
            }
            return bits;
        };
        for (const std::string& m : g.members) {
            NetBot* one = bot(m);
            if (!one) continue;
            // Not in yet, or a breed he cannot stand even beside them: his own way, in the
            // party still. The board opens a breed to the party when the tank and half of them
            // stand it; led there all the same, a wizard of the party died 140 times in 24 game
            // hours against 29 alone.
            if (!in(m) || (g.map >= 0 && !one->helps(g.map, g.breed))) {
                Bot::Lead alone;
                alone.mateCloaks = mates(m, false);
                alone.mateInvites = mates(m, true);
                if (g.elite) alone.raidCastle = eliteCastle_, alone.raidSquare = eliteSquare_;
                one->lead(std::move(alone), "", "");
                continue;
            }
            Bot::Lead lead;
            lead.mateCloaks = mates(m, false);
            lead.mateInvites = mates(m, true);
            if (g.elite) lead.raidCastle = eliteCastle_, lead.raidSquare = eliteSquare_;
            lead.map = g.map;
            lead.size = int(state.members.size());
            lead.healer = healer;
            lead.role = Bot::roleOf(one->kin(), m == g.tank);
            lead.castle = g.castle > 0 && (one->castlesOpen() & (1 << g.castle)) ? g.castle : 0;
            lead.square = g.square > 0 && (one->squaresOpen() & (1 << g.square)) ? g.square : 0;
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
        for (const WorldRow& w : worldRows()) {
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
                if (&a == &b || a.gone || b.gone || a.fixed || b.fixed || a.forCastle || b.forCastle ||
                    a.forSquare || b.forSquare || a.elite || b.elite || a.guild || b.guild) {
                    continue;
                }
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
                // An ordinary party now, which parts when its quests are done; its duos are
                // paired again then (duos).
                a.duo = false;
                std::printf("  [board] the party of %s joins %s's\n", b.leader.c_str(), a.leader.c_str());
                b.gone = true;
            }
        }
    }

    static bool healerKin(const NetBot& b) { return b.kin() == sim::Kin::FairyElf && b.support(); }

    // **Duos** (the user, 2026-10-13: "lets create duos for DK,melee MG so energy elfs always has
    // job", "because only 3 chars need for full party"): each energy elf in no party is put with
    // the free knight or melee gladiator nearest her level, for the run. The tank leads; on a
    // quest either is stuck on the duo hunts it as any party, and merges into a bigger one at a
    // shared quest (merge) -- a wizard beside them makes the three-class party. Otherwise he
    // goes his way and she keeps by him (lead). Taken into a bigger party that breaks up, they
    // are paired again here.
    // **Within kDuoGap levels, and no trader** (the user, 2026-10-13: "why energy elfs perform so
    // bad?", then "do it"): the nearest free tank was whoever was left -- Kael (128) went with
    // Sigrun (259), a trader at the Broker half the time, was on his map now and then and gained
    // eight levels in a night while the matched elves gained 25-43. A duo whose levels drift past
    // kDuoBreak parts, and each is paired again.
    static constexpr int kDuoGap = 40, kDuoBreak = 50;
    void duos() {
        for (Group& g : groups_) {
            if (g.gone || !g.duo || g.members.size() != 2) continue;
            const NetBot* a = bot(g.members[0]);
            const NetBot* b = bot(g.members[1]);
            if (!a || !b || std::abs(a->level() - b->level()) <= kDuoBreak) continue;
            std::printf("  [board] the duo of %s (%d) and %s (%d) parts: too far apart\n", a->name().c_str(), a->level(),
                        b->name().c_str(), b->level());
            for (const std::string& m : g.members) {
                if (NetBot* one = bot(m)) {
                    one->askParty(net::PartyWhat::Leave, "");
                    one->expect("");
                    one->lead(Bot::Lead{}, "", "");
                }
            }
            g.gone = true;
        }
        for (const auto& [name, elf] : byName_) {
            if (!healerKin(*elf) || grouped(name) || !elf->partyState().members.empty()) continue;
            NetBot* tank = nullptr;
            for (const auto& [other, one] : byName_) {
                if (grouped(other) || !one->partyState().members.empty() || one->trader()) continue;
                if (!(one->kin() == sim::Kin::DarkKnight || (one->kin() == sim::Kin::MagicGladiator && !one->magic()))) continue;
                if (std::abs(one->level() - elf->level()) > kDuoGap) continue;
                if (!tank || std::abs(one->level() - elf->level()) < std::abs(tank->level() - elf->level())) tank = one;
            }
            if (!tank) continue;
            Group g;
            g.leader = tank->name();
            g.tank = tank->name();
            g.members = {tank->name(), name};
            g.duo = true;
            std::printf("  [board] a duo: %s (%d) and %s (%d)\n", tank->name().c_str(), tank->level(), name.c_str(), elf->level());
            groups_.push_back(std::move(g));
        }
    }

    // **A tank's party takes an energy elf** (the user, 2026-10-13: "how often tanks ar looking for
    // energy elfs?" -- never, once a party was made): a party with a tank and no energy elf takes
    // a free one within kKeepGap of its leader who stands its ground, or the second of a party
    // with no tank.
    void healers(std::set<std::string>& free) {
        for (Group& g : groups_) {
            if (g.gone || g.forCastle || g.forSquare || g.elite || g.guild || g.members.size() >= size_t(sim::kPartyMost)) continue;
            bool tank = false, healer = false;
            for (const std::string& m : g.members) {
                if (const NetBot* one = bot(m)) tank |= tankish(one->kin()), healer |= healerKin(*one);
            }
            const NetBot* leader = bot(g.leader);
            if (!tank || healer || leader == nullptr) continue;
            const auto fits = [&](NetBot& one) {
                return healerKin(one) && std::abs(one.level() - leader->level()) <= kKeepGap &&
                       (g.map < 0 || one.helps(g.map, g.breed));
            };
            std::string taken;
            for (const std::string& name : free) {
                if (NetBot* one = bot(name); one && fits(*one)) {
                    taken = name;
                    free.erase(name);
                    break;
                }
            }
            if (taken.empty()) {
                for (Group& other : groups_) {
                    if (&other == &g || other.gone || other.fixed || other.forCastle || other.forSquare) continue;
                    int healers = 0;
                    bool otherTank = false;
                    for (const std::string& m : other.members) {
                        if (const NetBot* one = bot(m)) healers += healerKin(*one), otherTank |= tankish(one->kin());
                    }
                    if (otherTank || healers < 2) continue;
                    for (auto it = other.members.begin(); it != other.members.end(); ++it) {
                        NetBot* one = bot(*it);
                        if (*it == other.leader || !one || !fits(*one)) continue;
                        taken = *it;
                        other.members.erase(it);
                        break;
                    }
                    if (!taken.empty()) break;
                }
            }
            if (taken.empty()) continue;
            g.members.push_back(taken);
            std::printf("  [board] %s joins %s's party to heal\n", taken.c_str(), g.leader.c_str());
        }
    }

    // **Grind parties at any level** (the user, 2026-10-13): free bots with nothing they do alone
    // -- no quest step of theirs is theirs alone -- gathered round a tank, an energy elf second,
    // then the rest, all within kGrindGap of him, three to five. Kept (Group::keeps) and farming
    // the strongest breed the tank and half of them stand together (lead's forFarm), until they
    // drift kPartGap apart.
    void grind(std::set<std::string>& free) {
        std::vector<std::string> pool;
        for (const std::string& name : free) {
            bool alone = false;
            for (const Bot::Need& n : needs_[name]) alone |= n.alone;
            if (!alone) pool.push_back(name);
        }
        std::stable_sort(pool.begin(), pool.end(), [&](const std::string& a, const std::string& b) {
            if (tankish(bot(a)->kin()) != tankish(bot(b)->kin())) return tankish(bot(a)->kin());
            return bot(a)->level() > bot(b)->level();
        });
        std::set<std::string> used;
        for (const std::string& tank : pool) {
            if (used.count(tank) || !tankish(bot(tank)->kin())) continue;
            const int level = bot(tank)->level();
            std::vector<std::string> members{tank};
            const auto add = [&](auto test) {
                for (const std::string& name : pool) {
                    if (members.size() >= size_t(sim::kPartyMost) || used.count(name)) continue;
                    if (std::find(members.begin(), members.end(), name) != members.end()) continue;
                    NetBot& one = *bot(name);
                    if (std::abs(one.level() - level) <= kGrindGap && test(one)) {
                        members.push_back(name);
                        return true;
                    }
                }
                return false;
            };
            add([](NetBot& b) { return healerKin(b); });
            add([](NetBot& b) { return b.kin() == sim::Kin::FairyElf && !b.support(); });
            // The rest, never a second energy elf: hers goes to the next tank (healers).
            while (members.size() < 4 && add([](NetBot& b) { return !healerKin(b); })) {
            }
            if (members.size() < 3) continue;
            Group g;
            g.members = members;
            g.leader = tank;
            g.forFarm = true;
            g.keeps = true;
            std::string said;
            for (const std::string& m : members) {
                used.insert(m);
                free.erase(m);
                said += (said.empty() ? "" : ", ") + m;
            }
            std::printf("  [board] a party to grind: %s\n", said.c_str());
            groups_.push_back(std::move(g));
        }
    }
    static constexpr int kGrindGap = 10;  // levels from the tank a grind party takes

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
            // A guildmate's party led from another netbot is followed as a player's is.
            const auto bound = bound_.find(name);
            const bool remote = bound != bound_.end() && !byName_.count(bound->second);
            if (grouped(name) && !remote) continue;
            const net::PartyState& state = one->partyState();
            int64_t& kept = followedAt_[name];
            // **Between maps the party reads empty for a moment**, or its leader on no map: the
            // last lead kept kFollowGrace, as the board's own groups keep theirs. Dropped there, a
            // knight who had paid for Noria to follow chose a Lorencia quest a game minute on.
            const bool between = state.members.empty() ||
                                 (outsiders(*one) && state.members.front().world.empty());
            if (between && kept != 0 && one->clockNow() - kept < kFollowGrace) continue;
            if (!outsiders(*one)) {
                kept = 0;
                one->lead(Bot::Lead{}, "", "");  // his own way again, the player gone
                continue;
            }
            kept = one->clockNow();
            const net::PartyState::Member& head = state.members.front();
            Bot::Lead lead;
            for (const WorldRow& w : worldRows()) {
                if (head.world == w.name) lead.map = w.map;
            }
            // **Only where he can stand beside them** (Bot::grounds, as the board's parties ask
            // helps()), and only the breeds he can: led anywhere, a level-35 bot followed a
            // player of 200 to maps that killed him, paid to come back, and was led there again.
            // With nothing there for him -- or the party his to lead, its player gone -- his own
            // way, in the party still.
            for (const Bot::Ground& g : one->grounds()) {
                if (g.map == lead.map && lead.map >= 0) lead.breeds.push_back(g.breed);
            }
            if (head.name == name || lead.breeds.empty()) {
                one->lead(Bot::Lead{}, "", "");
                continue;
            }
            lead.size = int(state.members.size());
            lead.role = Bot::roleOf(one->kin(), false);
            for (const net::PartyState::Member& m : state.members) lead.healer |= m.kin == uint8_t(sim::Kin::FairyElf);
            one->lead(std::move(lead), head.name, "");
        }
    }
    static constexpr int64_t kFollowGrace = 30 * 20;  // game ticks
    static constexpr int kKeepGap = 20, kPartGap = 25;  // levels: a party kept, and parted
    // Three or more up, their levels within `gap`, a knight or gladiator among them.
    bool strong(const Group& g, int gap) const {
        int up = 0, low = 1 << 30, high = 0;
        bool tank = false;
        for (const std::string& m : g.members) {
            const NetBot* one = bot(m);
            if (!one || !one->up()) continue;
            ++up;
            low = std::min(low, one->level());
            high = std::max(high, one->level());
            tank = tank || tankish(one->kin());
        }
        return up >= 3 && tank && high - low <= gap;
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

    // **Players asked in** (the user, 2026-10-11: "i want they can invite me, but i also want to
    // refuse invite"): a bot in no party asks a player -- never a bot -- near his level into one, and hands him the lead once he comes in (NetBot::invitePerson). One bot at
    // a time asks a player, and he is let be after: a while after an invite he let lapse or one
    // the server bounced (in a party already), long after he declined. The player answers with
    // the invite's Accept and Decline (game/ui/chat.cpp).
    static constexpr auto kAskAgain = std::chrono::minutes(5);       // after no answer
    static constexpr auto kAskBusy = std::chrono::minutes(10);       // he was in a party
    static constexpr auto kAskDeclined = std::chrono::minutes(30);   // he said no
    static constexpr auto kBotAsksEvery = std::chrono::minutes(2);   // one bot's own pace
    void ask(std::chrono::steady_clock::time_point now) {
        for (const auto& [name, one] : byName_) {
            for (const auto& [who, answer] : one->takeAnswers()) {
                askable_[who] = now + (answer == NetBot::Answer::Declined ? kAskDeclined : kAskBusy);
            }
        }
        // **Server-wide** (the user, 2026-10-11: "party invite has to works globally without map
        // restrictions server wide"): every player any bot sees, on any map, pooled; an invite
        // goes by name and the bot then goes to him (follow). A bot on his map asks first.
        std::map<std::string, std::pair<int, int>> pool;  // name: his level, his map
        for (const auto& [name, one] : byName_) {
            for (const NetBot::Person& person : one->people()) pool[person.name] = {person.level, one->mapNow()};
        }
        for (const auto& [who, seen] : pool) {
            if (const auto at = askable_.find(who); at != askable_.end() && now < at->second) continue;
            const auto [level, map] = seen;
            NetBot* asker = nullptr;
            for (const auto& [name, one] : byName_) {
                if (grouped(name) || !one->partyState().members.empty() || !one->alive()) continue;
                if (const auto at = botAsked_.find(name); at != botAsked_.end() && now < at->second) continue;
                if (std::abs(one->level() - level) > std::max(10, one->level() / 4)) continue;
                if (asker == nullptr || (one->mapNow() == map && asker->mapNow() != map)) asker = one;
            }
            if (asker == nullptr) continue;
            asker->invitePerson(who);
            askable_[who] = now + kAskAgain;
            botAsked_[asker->name()] = now + kBotAsksEvery;
        }
    }

    // Parties formed from the stuck, a quest at a time, the most stuck first.
    // **The strongest, together** (the user, 2026-10-13: "we need that top 10 characters
    // communicate and try to go to hardest event and maps together", "not exactly top 10 but the
    // strongest"): the kElite strongest by Bot::score -- gear, stats, runes -- traders aside, in
    // parties of up to five that keep (Group::elite, kept as a strong party is: forFarm and keeps),
    // so past a shared quest they grind the strongest breed they stand together, Icarus's when
    // they fly, and go to an event's door together as any party (the door rule above). One raid
    // of up to sim::kRaidMost since the raid came (the user, 2026-10-13: "implement on bot server
    // also"): its leader makes it a raid once it holds more than a party's five (lead()). Then
    // the user's "Parties of 5, raid for goals": they grind as parties of five, for the full
    // party's tenth, and become one raid only while a goal stands (eliteGoal: a castle or square
    // door two of them are due at, or Tersia's Icarus chain two are stuck on), kHolds at least,
    // then part into fives again. A member
    // holds his place until he falls out of the kEliteKeep strongest; a newcomer is taken out of
    // any party but an event's.
    static constexpr int kElite = 10, kEliteKeep = 12;
    static constexpr double kSupplyShare = 0.5;  // of a breed's bodies an hour a party can take (lead's farm pick)
    int64_t eliteGoalUntil_ = -1;  // the raid's goal holds until this boardClock_ (eliteGoal)
    int64_t boardClock_ = 0;       // game ticks the board has seen pass (tick)
    std::map<std::string, int64_t> lastClock_;
    std::string eliteGoalWhy_;
    int eliteCastle_ = 0, eliteSquare_ = 0;  // the raid's door while its goal holds (Lead::raidCastle)
    void elite() {
        std::vector<std::pair<double, std::string>> ranked;
        for (const auto& [name, one] : byName_) {
            if (!one->trader() && !bound_.count(name)) ranked.push_back({-one->strength(), name});
        }
        std::sort(ranked.begin(), ranked.end());
        std::set<std::string> top, keep;
        for (size_t i = 0; i < ranked.size(); ++i) {
            if (i < size_t(kElite)) top.insert(ranked[i].second);
            if (i < size_t(kEliteKeep)) keep.insert(ranked[i].second);
        }
        const auto leave = [&](const std::string& m) {
            if (NetBot* one = bot(m)) {
                one->askParty(net::PartyWhat::Leave, "");
                one->expect("");
                one->lead(Bot::Lead{}, "", "");
            }
        };
        // A goal for the raid, held kHolds past its last sight so the raid does not part and
        // gather again as a door's time comes and goes.
        const int64_t clock = boardClock_;
        int goalCastle = 0, goalSquare = 0;
        if (const std::string why = eliteGoal(top, &goalCastle, &goalSquare); !why.empty()) {
            if (eliteGoalUntil_ < clock) std::printf("  [board] the strongest gather as one raid: %s\n", why.c_str());
            eliteGoalUntil_ = clock + kHolds;
            eliteGoalWhy_ = why;
            eliteCastle_ = goalCastle;
            eliteSquare_ = goalSquare;
        }
        const bool goal = clock < eliteGoalUntil_;
        if (!goal) eliteCastle_ = eliteSquare_ = 0;
        const size_t cap = size_t(goal ? sim::kRaidMost : sim::kPartyMost);
        std::set<std::string> placed;
        for (Group& g : groups_) {
            if (g.gone || !g.elite) continue;
            std::vector<std::string> out;
            // Only one seen and out of the strongest: a member whose line is down a moment (a map
            // change, a reconnect) is in no byName_ this half second, and was thrown out and
            // taken back each time (25 leaves in the raid's first game hour).
            for (const std::string& m : g.members) {
                if (byName_.count(m) && !keep.count(m)) out.push_back(m);
            }
            for (const std::string& m : out) {
                leave(m);
                g.members.erase(std::remove(g.members.begin(), g.members.end(), m), g.members.end());
                std::printf("  [board] %s leaves the strongest's party\n", m.c_str());
            }
            if (g.members.size() < 2) {
                for (const std::string& m : g.members) leave(m);
                g.gone = true;
                continue;
            }
            if (std::find(g.members.begin(), g.members.end(), g.leader) == g.members.end()) g.leader = g.members.front();
            placed.insert(g.members.begin(), g.members.end());
        }
        reshapeElite(goal, cap, leave);
        // Newcomers: free, or out of any party but an event's or the strongest's.
        std::vector<std::string> coming;
        for (const std::string& name : top) {
            if (placed.count(name) || bot(name)->eventInside() > 0) continue;
            Group* in = nullptr;
            for (Group& g : groups_) {
                if (!g.gone && std::find(g.members.begin(), g.members.end(), name) != g.members.end()) in = &g;
            }
            if (in && (in->forCastle || in->forSquare)) continue;
            if (!in && !bot(name)->partyState().members.empty()) continue;  // a party the board did not make
            if (in) {
                leave(name);
                in->members.erase(std::remove(in->members.begin(), in->members.end(), name), in->members.end());
                if (in->members.size() < 2) {
                    for (const std::string& m : in->members) leave(m);
                    in->gone = true;
                } else if (std::find(in->members.begin(), in->members.end(), in->leader) == in->members.end()) {
                    in->leader = in->members.front();
                }
            }
            coming.push_back(name);
        }
        if (coming.empty()) return;
        std::stable_sort(coming.begin(), coming.end(), [&](const std::string& a, const std::string& b) {
            return tankish(bot(a)->kin()) > tankish(bot(b)->kin());
        });
        // Into an elite party with room, then new ones of two or more.
        for (Group& g : groups_) {
            if (g.gone || !g.elite) continue;
            while (!coming.empty() && g.members.size() < cap) {
                std::printf("  [board] %s joins the strongest with %s\n", coming.front().c_str(), g.leader.c_str());
                g.members.push_back(coming.front());
                coming.erase(coming.begin());
            }
        }
        while (coming.size() >= 2) {
            Group g;
            const size_t n = std::min<size_t>(coming.size(), cap);
            g.members.assign(coming.begin(), coming.begin() + long(n));
            coming.erase(coming.begin(), coming.begin() + long(n));
            g.leader = g.members.front();
            g.elite = g.keeps = g.forFarm = true;
            std::string said;
            for (const std::string& m : g.members) said += (said.empty() ? "" : ", ") + m;
            std::printf("  [board] the strongest party together: %s\n", said.c_str());
            if (NetBot* lead = bot(g.leader)) say(*lead, "The strongest of us, together: the hardest ground we can stand.");
            groups_.push_back(std::move(g));
        }
    }

    // What the strongest gather as one raid for, said; empty for none: a Blood Castle or a Devil
    // Square door two or more of them are due at, or Tersia's Icarus chain -- a Clear step in
    // Icarus -- two or more are stuck on (the user, 2026-10-13: "bots also try to do icarus quest
    // line by forming raid", "bots should go with raids to bc5,6,7").
    std::string eliteGoal(const std::set<std::string>& top, int* castle, int* square) const {
        *castle = *square = 0;
        std::map<int, int> castles, squares;
        int icarus = 0;
        for (const std::string& name : top) {
            NetBot* one = bot(name);
            if (!one) continue;
            for (int c = sim::kCastlesBuilt; c >= 1; --c) {
                if ((one->castleTime() || one->inCastle()) && (one->castlesOpen() & (1 << c))) {
                    ++castles[c];
                    break;
                }
            }
            for (int q = sim::kSquaresBuilt; q >= 1; --q) {
                if ((one->squareTime() || one->inSquare()) && (one->squaresOpen() & (1 << q))) {
                    ++squares[q];
                    break;
                }
            }
            if (const auto it = needs_.find(name); it != needs_.end()) {
                for (const Bot::Need& n : it->second) {
                    if (n.map == int(sim::kIcarusMap) && n.together && n.reach && !n.alone) {
                        ++icarus;
                        break;
                    }
                }
            }
        }
        for (auto it = castles.rbegin(); it != castles.rend(); ++it) {
            if (it->second >= 2) {
                *castle = it->first;
                return "Blood Castle " + std::to_string(it->first) + ", " + std::to_string(it->second) + " due";
            }
        }
        for (auto it = squares.rbegin(); it != squares.rend(); ++it) {
            if (it->second >= 2) {
                *square = it->first;
                return "Devil Square " + std::to_string(it->first) + ", " + std::to_string(it->second) + " due";
            }
        }
        if (icarus >= 2) return "Tersia's Icarus chain, " + std::to_string(icarus) + " stuck on it";
        return "";
    }

    // The strongest's groups to the goal's shape: one raid while it stands (the smaller groups'
    // members leave theirs and are invited into the largest), else fives (a raid's members past
    // the fifth leave it for a party of their own). No one inside an event is moved.
    template <typename Leave>
    void reshapeElite(bool goal, size_t cap, const Leave& leave) {
        std::vector<Group*> elite;
        for (Group& g : groups_) {
            if (!g.gone && g.elite) elite.push_back(&g);
        }
        const auto busy = [&](const std::string& m) {
            const NetBot* one = bot(m);
            return one && one->eventInside() > 0;
        };
        if (goal && elite.size() >= 2) {
            std::sort(elite.begin(), elite.end(), [](const Group* a, const Group* b) { return a->members.size() > b->members.size(); });
            Group& raid = *elite.front();
            for (size_t i = 1; i < elite.size(); ++i) {
                Group& from = *elite[i];
                std::vector<std::string> moved;
                for (const std::string& m : from.members) {
                    if (raid.members.size() + moved.size() >= cap || busy(m)) continue;
                    moved.push_back(m);
                }
                if (moved.empty()) continue;
                for (const std::string& m : moved) {
                    leave(m);
                    from.members.erase(std::remove(from.members.begin(), from.members.end(), m), from.members.end());
                    raid.members.push_back(m);
                }
                if (from.members.size() < 2) {
                    for (const std::string& m : from.members) leave(m);
                    from.gone = true;
                } else if (std::find(from.members.begin(), from.members.end(), from.leader) == from.members.end()) {
                    from.leader = from.members.front();
                }
                std::string said;
                for (const std::string& m : moved) said += (said.empty() ? "" : ", ") + m;
                std::printf("  [board] %s join %s's raid (%s)\n", said.c_str(), raid.leader.c_str(), eliteGoalWhy_.c_str());
            }
        }
        if (!goal) {
            for (Group* g : elite) {
                if (g->members.size() <= cap) continue;
                std::vector<std::string> extra;
                for (size_t i = g->members.size(); i-- > 0 && g->members.size() - extra.size() > cap;) {
                    const std::string& m = g->members[i];
                    if (m == g->leader || busy(m)) continue;
                    extra.push_back(m);
                }
                if (extra.size() < 2) continue;  // one left over joins none: the raid keeps him
                for (const std::string& m : extra) {
                    leave(m);
                    g->members.erase(std::remove(g->members.begin(), g->members.end(), m), g->members.end());
                }
                Group part;
                part.members = extra;
                part.leader = extra.front();
                part.elite = part.keeps = part.forFarm = true;
                std::string said;
                for (const std::string& m : extra) said += (said.empty() ? "" : ", ") + m;
                std::printf("  [board] the raid with %s parts: %s grind as their own party\n", g->leader.c_str(), said.c_str());
                groups_.push_back(std::move(part));
                return;  // groups_ moved: the next round sees the rest
            }
        }
    }

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
        // **Free bots already inside the same castle or square party up there** (the user,
        // 2026-10-13: "if there is multiple bots on BC they should male a party", "same as DS"):
        // a cloak's holder who came to the door alone went in alone, beside others who had too.
        {
            std::map<int, std::vector<std::string>> inside;
            for (const std::string& name : free) {
                if (const int at = bot(name)->eventInside(); at > 0 && inside[at].size() < size_t(sim::kPartyMost)) {
                    inside[at].push_back(name);
                }
            }
            for (auto& [at, members] : inside) {
                if (members.size() < 2) continue;
                std::stable_sort(members.begin(), members.end(), [&](const std::string& a, const std::string& b) {
                    return tankish(bot(a)->kin()) > tankish(bot(b)->kin());
                });
                Group g;
                g.members = members;
                g.leader = members.front();
                (at < 200 ? g.forCastle : g.forSquare) = true;
                std::string said;
                for (const std::string& m : members) {
                    free.erase(m);
                    said += (said.empty() ? "" : ", ") + m;
                }
                std::printf("  [board] a party inside %s %d: %s\n", at < 200 ? "Blood Castle" : "Devil Square", at % 100,
                            said.c_str());
                groups_.push_back(std::move(g));
            }
        }
        // **A ticket's holder leaves his quest party for the door** (the user, 2026-10-13: "why
        // chars is not going to BC5?", "or stronger DS?"): only free bots went in together, and the
        // high ones were nearly always in a quest party or a duo -- eight held a Cloak +5 and one
        // party went to castle 5 in hours; Devil Square 3 and 4 saw 36 and 7 entries in a run. When
        // two or more are due at one castle's door or square's gate, those in a quest party leave
        // it (never an event's own) and come in free. A party left with one breaks up.
        {
            std::map<int, std::vector<std::string>> due;  // c, or 100 + s
            for (const auto& [name, one] : byName_) {
                const Group* in = nullptr;
                for (const Group& g : groups_) {
                    if (!g.gone && std::find(g.members.begin(), g.members.end(), name) != g.members.end()) in = &g;
                }
                if (in && (in->forCastle || in->forSquare)) continue;
                // The strongest go to a door as their raid (eliteGoal), not pulled out of it.
                if (in && in->elite) continue;
                // Nor a guild's party: it goes to the door together (lead()'s castle and square).
                // Pulled out, its leader's group fell to another and the plan made it again each
                // second (the guild run, 2026-10-13).
                if (bound_.count(name)) continue;
                if (!in && !free.count(name)) continue;
                if (one->eventInside() > 0) continue;
                for (int c = sim::kCastlesBuilt; c >= 1; --c) {
                    if (one->castleTime() && (one->castlesOpen() & (1 << c))) {
                        due[c].push_back(name);
                        break;
                    }
                }
                for (int sq = sim::kSquaresBuilt; sq >= 1; --sq) {
                    if (one->squareTime() && (one->squaresOpen() & (1 << sq))) {
                        due[100 + sq].push_back(name);
                        break;
                    }
                }
            }
            std::set<std::string> leaving;
            for (const auto& [at, names] : due) {
                if (names.size() < 2) continue;
                for (const std::string& name : names) {
                    if (!free.count(name)) leaving.insert(name);
                }
            }
            for (Group& g : groups_) {
                if (g.gone || g.forCastle || g.forSquare) continue;
                std::vector<std::string> out;
                for (const std::string& m : g.members) {
                    if (leaving.count(m)) out.push_back(m);
                }
                if (out.empty()) continue;
                for (const std::string& m : out) {
                    if (NetBot* one = bot(m)) {
                        one->askParty(net::PartyWhat::Leave, "");
                        one->expect("");
                        one->lead(Bot::Lead{}, "", "");
                    }
                    g.members.erase(std::remove(g.members.begin(), g.members.end(), m), g.members.end());
                    free.insert(m);
                    std::printf("  [board] %s leaves %s's party for an event's door\n", m.c_str(), g.leader.c_str());
                }
                if (g.members.size() < 2) {
                    for (const std::string& m : g.members) {
                        if (NetBot* one = bot(m)) {
                            one->askParty(net::PartyWhat::Leave, "");
                            one->expect("");
                            one->lead(Bot::Lead{}, "", "");
                        }
                    }
                    g.gone = true;
                } else if (std::find(g.members.begin(), g.members.end(), g.leader) == g.members.end()) {
                    g.leader = g.members.front();
                }
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
        // And free bots due at the same Devil Square's gate.
        for (int s = sim::kSquaresBuilt; s >= 1; --s) {
            std::vector<std::string> members;
            for (const std::string& name : free) {
                NetBot& one = *bot(name);
                if (one.squareTime() && (one.squaresOpen() & (1 << s)) && members.size() < size_t(sim::kPartyMost)) {
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
            g.forSquare = true;
            std::string said;
            for (const std::string& m : members) {
                free.erase(m);
                said += (said.empty() ? "" : ", ") + m;
            }
            std::printf("  [board] a party for Devil Square %d: %s\n", s, said.c_str());
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
                // Not the strongest's (elite()): it keeps its own, and threw these out each round.
                if (g.fixed || g.forCastle || g.forSquare || g.elite || !stuckOn(g).count(q)) continue;
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
            // **An energy elf first** (the user, 2026-10-12: "teach bots to make smarter parties
            // with energy elfs who can buf and heal"): her Greater Damage, Defense and Heal are
            // what lets the party take a breed none of them takes alone; an agility elf after.
            const auto isHealer = [](NetBot& b) { return b.kin() == sim::Kin::FairyElf && b.support(); };
            if (!has(isTank)) recruit(isTank);
            if (!has(isHealer)) recruit(isHealer);
            if (!has(isElf)) recruit(isElf);
            while (members.size() < 3) {
                const size_t was = members.size();
                // Not a second energy elf (the user, 2026-10-13: "When elf parties already have one
                // energy elf, a second one goes to a tank's party instead"): Noria's first parties
                // were elf, elf and energy elf twice over while Lorencia's knights had none.
                recruit([&](NetBot& b) { return !(isHealer(b) && has(isHealer)); });
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
        healers(free);
        grind(free);
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
    std::map<std::string, int64_t> followedAt_;      // when follow() last led him, his clock
    // Players by name: not asked into a party again before then (ask); and each bot's next ask.
    std::map<std::string, std::chrono::steady_clock::time_point> askable_, botAsked_;
    std::chrono::steady_clock::time_point next_{};
};

// The Broker's errands, their own file, on the NetBot above.
#include "errands.h"
// The guilds' board (--guilds), on the NetBot and the Board above.
#include "guilds.h"

}  // namespace

int main(int argc, char** argv) {
    std::string host = "37.27.158.226";
    int port = net::kDefaultPort;
    std::string dir = std::string(MU2_ASSET_DIR) + "/../saves/bots";
    double hours = 0.0;  // 0: until stopped
    bool quests = true;
    bool chatty = false;
    bool party = false, parties = false;
    bool trades = true;
    bool guilds = false;  // --guilds: they found, join and play guilds (guilds.h)
    int threads = 0;  // --threads N: 0 for one a ten bots, up to four
    std::string claude;
    std::vector<std::pair<std::string, sim::Kin>> wanted;
    std::set<std::string> energy;  // eelf: the energy elves (Options::support)
    std::set<std::string> magic;   // mmg: the magic gladiators (Options::magic)
    std::set<std::string> traders; // CLASS/t: the traders (NetBot::trader)
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
        else if (a == "--no-trades") trades = false;
        else if (a == "--guilds") guilds = true;
        else if (a == "--threads" && i + 1 < argc) threads = std::atoi(argv[++i]);
        else if (a == "--claude" && i + 1 < argc) claude = argv[++i];
        else if (a.find(':') != std::string::npos) {
            const std::string name = a.substr(0, a.find(':'));
            std::string cls = a.substr(a.find(':') + 1);
            if (cls.size() > 2 && cls.compare(cls.size() - 2, 2, "/t") == 0) {
                cls.resize(cls.size() - 2);
                traders.insert(name);
            }
            if (cls == "dk") wanted.emplace_back(name, sim::Kin::DarkKnight);
            else if (cls == "dw") wanted.emplace_back(name, sim::Kin::DarkWizard);
            else if (cls == "elf") wanted.emplace_back(name, sim::Kin::FairyElf);
            else if (cls == "eelf") {
                wanted.emplace_back(name, sim::Kin::FairyElf);
                energy.insert(name);
            }
            else if (cls == "mg") wanted.emplace_back(name, sim::Kin::MagicGladiator);
            else if (cls == "mmg") {
                // The magic gladiator (the user, 2026-10-12: "make also some magic MG not only
                // melee"): Bot's Options::magic, which bot.cpp's --path magic already plays.
                wanted.emplace_back(name, sim::Kin::MagicGladiator);
                magic.insert(name);
            }
            else {
                std::fprintf(stderr, "netbot: %s: the class is dk, dw, elf, eelf, mg or mmg\n", a.c_str());
                return 2;
            }
        } else {
            std::fprintf(stderr, "usage: netbot [--server HOST] [--port N] [--dir DIR] [--hours H] "
                                 "[--no-quests] [--chatty] [--party | --parties] [--no-trades] [--guilds] [--threads N] [--claude DIR] NAME:dk|dw|elf|eelf|mg|mmg[/t] ...\n");
            return 2;
        }
    }
    // The server's own bots (docs/game/party.md, the user's of 2026-10-09: "DK, Elf, DW, MG and a
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
        bots.push_back(std::make_unique<NetBot>(name, kin, host, port, dir, quests, chatty, energy.count(name) > 0, trades,
                                                magic.count(name) > 0, traders.count(name) > 0));
    }
    if (!claude.empty()) bots.front()->driveFrom(claude);
    Board board(parties);
    Errands errands;
    Guilds guildBoard(dir);
    if (party) {
        std::vector<std::string> names;
        for (const auto& one : wanted) names.push_back(one.first);
        board.fix(names);
    }
    // **The bots on --threads N threads** (the user, 2026-10-09: "cant we speed up this things?
    // we can control server thics"): each bot's brain is his own, and a thread's bots on one world
    // share its mirror (Shared), so a thread plays a share of them; one thread kept five bots at a
    // full core at mu2_server --speed 200, before they shared. The board and the minute's report
    // hold every bot's lock while they read and lead them.
    // A thread a ten bots by default: one thread's bots on a world share its mirror, so fewer
    // threads are less work -- ten bots on two worlds, 1.7 cores on two threads each with his own
    // mirror, 0.43 on one sharing them, keeping up with mu2_server --speed 100 (2026-10-13).
    if (threads <= 0) threads = int(std::min<size_t>((bots.size() + 9) / 10, 4));
    threads = std::clamp<int>(threads, 1, int(bots.size()));
    std::vector<std::mutex> locks(bots.size());
    std::atomic<bool> done{false};
    const auto start = std::chrono::steady_clock::now();
    const auto play = [&](int t) {
        std::vector<bool> up(bots.size(), false);
        std::vector<std::chrono::steady_clock::time_point> retryAt(bots.size());
        Pool pool;  // this thread's worlds, one mirror each (Shared)
        while (!done) {
            const auto now = std::chrono::steady_clock::now();
            // Every bot of the thread held for the turn, in index order as the board's all(): a
            // world's tick is heard by all of its bots at once.
            std::vector<std::unique_lock<std::mutex>> held;
            for (size_t i = size_t(t); i < bots.size(); i += size_t(threads)) held.emplace_back(locks[i]);
            // A line gone -- another world, or the server restarted: he comes back with his
            // token, and the server says where he is.
            const auto drop = [&](size_t i) {
                bots[i]->leave();
                up[i] = false;
                retryAt[i] = now + std::chrono::milliseconds(500);
            };
            for (size_t i = size_t(t); i < bots.size(); i += size_t(threads)) {
                if (!up[i]) {
                    if (now < retryAt[i]) continue;
                    up[i] = bots[i]->connect(pool);
                    if (!up[i]) retryAt[i] = now + std::chrono::seconds(10);
                    continue;
                }
                if (!bots[i]->pumpLine()) drop(i);
            }
            const bool behind = pool.advance();
            for (size_t i = size_t(t); i < bots.size(); i += size_t(threads)) {
                if (up[i] && bots[i]->gone()) drop(i);
            }
            held.clear();
            if (!behind) std::this_thread::sleep_for(std::chrono::milliseconds(5));
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
        // The board always: without --party or --parties it forms nothing, but a player's invite
        // is still taken and players near are still asked in (Board::follow, Board::ask).
        if (now >= boardAt) {
            all([&] {
                if (guilds) guildBoard.tick(bots, board);
                board.tick(bots);
                if (trades) errands.tick(bots);
            });
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
    for (const auto& bot : bots) bot->leave();  // each line before the mirror it holds
    return 0;
}
