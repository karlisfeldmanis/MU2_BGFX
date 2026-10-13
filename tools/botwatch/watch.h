// **What botwatch reads** (tools/botwatch/botwatch.cpp): a netbot run's logs, the processes
// on its port, and -- when botwatch starts the run itself -- the server and netbot it started.
//
// Everything here is read on botwatch's poll thread twice a second and handed to the window
// as one Snapshot under a lock; the window draws the last one and never waits on a file or `ps`.
#pragma once

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "sim/item_link.h"

extern char** environ;

namespace botwatch {

namespace sim = mu::sim;

using Clock = std::chrono::steady_clock;

struct Bot {
    std::string name, kin, map, doing, hp;
    int level = 1, kills = 0, deaths = 0, potions = 0;
    int score = 0;  // his gear score, off his sheet (Bot::score)
    // Off his sheet's gear too (the user, 2026-10-13: "how many chars has wings 1nd and 2nd and
    // how many has dyno", "how many players wear excelent items"): his wings' tier, 0 for none;
    // whether he rides a Dinorant; how many worn pieces carry an excellent option.
    int wings = 0;
    bool dinorant = false;
    int excellent = 0;
    int64_t zen = 0;
    int clock = 0;  // his game clock, seconds, summed over the run's restarts
    // His log's own clock restarts at 0:00 when the run is resumed (botwatch-run-resumes): the
    // clock before it, carried, and his last line's, to see one. Else the game time stood at the
    // first stretch's end and the speed read 0 ("why speed meter is not wroking?", 2026-10-13).
    int carried = 0, lastAt = 0;
    int eventTier = 0;  // the castle or square he last went into (its entry line)
    // His guild and whether he is its master, off netbot --guilds' line (bots/guild/NAME.who;
    // the user, 2026-10-13: "show guild row in bots list").
    std::string guild;
    bool master = false;
};

struct Line {
    int at = 0;
    std::string who, what;
};

struct Trade {
    int at = 0;
    std::string buyer, item, seller;
    int64_t price = 0;
    sim::Held held;  // off the item's link, for its colour
};

// A listing up at the Broker, off "lists" lines, gone again with its "Your ... sold" message.
struct Offer {
    int at = 0;  // the seller's clock when he listed it
    std::string seller, item;
    int64_t price = 0;
    sim::Held held;  // off the item's link, for its colour
};

// What a link says of a piece past its name (the user, 2026-10-12: "show more info oon sold items
// for weapons and armors"): excellent options, luck, skill, its option, sockets and their runes.
inline std::string detailsOf(const sim::Held& h) {
    std::string out;
    const auto add = [&](const std::string& part) { out += (out.empty() ? "" : " · ") + part; };
    if (const int ex = sim::excellentCount(h.excellent); ex > 0) add("Excellent x" + std::to_string(ex));
    if (h.luck) add("Luck");
    if (h.skill) add("Skill");
    if (h.option > 0) add("Option +" + std::to_string(h.option * 4));
    for (int k = 0; k < std::min<int>(h.sockets, 3); ++k) {
        const sim::PowerRow* power = sim::powerOf(h.powers[k]);
        add(std::string("Socket: ") + (power ? power->name : "empty"));
    }
    int affixes = 0;
    for (const auto a : h.affixes) affixes += a != 0;
    if (affixes > 0) add(std::to_string(affixes) + (affixes == 1 ? " power" : " powers"));
    return out;
}

// The tables a link's group and number are looked up in, set by botwatch before the poll starts
// (a link carries MU's group and number, never a row: unresolved, every Broker card had no row).
inline const mu::content::Tables* g_linkTables = nullptr;

// An item as netbot writes it: a link ("{i:...|Label}", sim::itemLink) since 2026-10-12, its plain
// name before. The name, and what the link says of it, its row found in g_linkTables.
inline std::string itemOf(const std::string& text, sim::Held* held) {
    const std::vector<sim::ItemLink> links = sim::findLinks(text);
    if (links.empty()) return text;
    *held = links.front().held;
    if (g_linkTables && held->item < 0) {
        for (size_t i = 0; i < g_linkTables->items.size(); ++i) {
            const mu::content::ItemRow& row = g_linkTables->items[i];
            if (row.group == links.front().group && row.number == links.front().number) {
                held->item = int32_t(i);
                break;
            }
        }
    }
    return links.front().label;
}

// One Blood Castle or Devil Square run the bots went into (the user, 2026-10-13: "Events tab ... latest
// events where bots participated and how many"): its entries within kEventWindow of each other,
// by the bots' clocks summed over restarts, and how each came out.
struct EventRun {
    int at = 0;            // the first entry's clock
    bool castle = true;    // Blood Castle, else Devil Square
    int tier = 0;
    std::vector<std::string> bots;
    std::map<std::string, std::string> result;  // by bot: "won", "over", "rank 2 of 4, 1,240 exp"
    bool won = false;      // a castle won by any of them
};
constexpr int kEventWindow = 15 * 60;

struct Process {
    int pid = 0;
    std::string what;
    double cores = 0.0;
    int memoryMb = 0;
    int speed = 0;  // the server's --speed, 0 for the netbot
};

struct Sample {
    double wall = 0.0;  // seconds since botwatch started
    int game = 0;       // the furthest bot clock, seconds
    double runCores = 0.0, machineCores = 0.0;
};

// What the window draws: a copy, taken under the lock.
struct Snapshot {
    std::vector<Bot> bots;
    std::vector<Line> feed;
    std::vector<Trade> trades;
    std::vector<Offer> offers;  // up now, newest first
    std::vector<Process> processes;
    std::vector<Sample> samples;
    std::vector<EventRun> events;  // newest first
    int listed = 0, bought = 0, visits = 0, empty = 0;
    int64_t spent = 0;
    int slowLastMinute = 0;
    double worstSlowMs = 0.0;
    double machineCores = 0.0, load[3] = {};
    std::string economy;
    bool running = false;  // botwatch's own run is up
    int netbots = 0;       // its netbot processes
    double wall = 0.0;
};

// ---- a line of the bots' log -------------------------------------------------------------
// "  [H:MM:SS] Name what he did"
inline bool parseLine(const std::string& raw, int* at, std::string* who, std::string* rest) {
    int h = 0, m = 0, s = 0, used = 0;
    if (std::sscanf(raw.c_str(), " [%d:%d:%d]%n", &h, &m, &s, &used) != 3 || used <= 0) return false;
    size_t i = size_t(used);
    while (i < raw.size() && raw[i] == ' ') ++i;
    const size_t end = raw.find(' ', i);
    if (end == std::string::npos || end == i) return false;
    *at = h * 3600 + m * 60 + s;
    *who = raw.substr(i, end - i);
    *rest = raw.substr(end + 1);
    return true;
}

inline bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

class Watch {
public:
    Watch(std::string run, int port) : run_(std::move(run)), port_(port), start_(Clock::now()) {}
    ~Watch() { stopRun(); }

    const std::string& run() const { return run_; }
    int port() const { return port_; }

    // ---- a run of botwatch's own: the server and the netbot, niced, logging into the run --
    // **In waves** (the user, 2026-10-12: "we need to improve, so we can run 100 bots without
    // problems"): a hundred new characters in one minute crowded Lorencia's and Noria's nests,
    // earned nothing and died; and one netbot holds four threads at most, which a hundred bots
    // fill. So the bots are split into netbots of `perNetbot`, each its own process and log
    // (netbot-N.log), started `waveSeconds` apart.
    bool launch(const std::string& server, const std::string& netbot, int bots, int speed, int threads,
                int perNetbot, int waveSeconds, std::string* error) {
        std::unique_lock<std::mutex> hold(lock_);
        if (serverPid_ > 0) return true;
        std::string mk = "mkdir -p '" + run_ + "/bots'";
        if (std::system(mk.c_str()) != 0) {
            *error = "cannot make " + run_;
            return false;
        }
        const std::string port = std::to_string(port_);
        serverPid_ = spawn({"nice", "-n", "10", server, "--port", port, "--store", run_ + "/characters.db",
                            "--speed", std::to_string(speed)},
                           run_ + "/server.log");
        if (serverPid_ <= 0) {
            *error = "the server did not start: " + server;
            return false;
        }
        // Two of each in ten, and of the gladiators and elves one each way: a melee and a magic
        // gladiator, an agility and an energy elf (the user, 2026-10-12).
        static const char* kClasses[] = {"dk", "dw", "elf", "mg", "dk", "dw", "eelf", "mmg", "elf", "eelf"};
        static const char* kNames[] = {
            "Brann", "Tova",  "Mirel", "Oskar", "Lysa",  "Fenna", "Corvin", "Dace",  "Rhea",  "Halvar",
            "Ulric", "Gorm",  "Bjorn", "Sigrun", "Eirik", "Thane", "Vesna", "Ivar",  "Zora",  "Kael",
            "Ainar", "Sylwen", "Nimue", "Ilse", "Maren", "Ragna", "Torvin", "Edda",  "Kolbe", "Varn",
            "Asta",  "Cael",  "Dorn",  "Elva",  "Grim",  "Hild",  "Isen",  "Jora",  "Lorn",  "Moss",
            "Nell",  "Orla",  "Pike",  "Quill", "Rune",  "Sable", "Teal",  "Wren",  "Yara",  "Bastian"};
        constexpr int kNameCount = int(sizeof(kNames) / sizeof(kNames[0]));
        perNetbot = std::max(1, perNetbot);
        for (int first = 0, wave = 0; first < bots; first += perNetbot, ++wave) {
            // The server's four seconds to listen, and each wave's wait, without the lock.
            hold.unlock();
            std::this_thread::sleep_for(std::chrono::seconds(wave == 0 ? 4 : waveSeconds));
            hold.lock();
            if (serverPid_ <= 0) return false;  // stopped while it waited
            std::vector<std::string> args = {"nice", "-n", "10", netbot, "--server", "127.0.0.1", "--port", port,
                                             "--dir", run_ + "/bots", "--threads", std::to_string(threads),
                                             "--parties", "--guilds"};
            for (int i = first; i < std::min(bots, first + perNetbot); ++i) {
                std::string name = kNames[i % kNameCount];
                if (i >= kNameCount) name += std::to_string(i / kNameCount + 1);
                // One in ten a trader (NAME:CLASS/t, netbot): the market his trade.
            args.push_back(name + ":" + kClasses[i % 10] + (i % 10 == 3 ? "/t" : ""));
            }
            const pid_t pid = spawn(args, run_ + "/netbot-" + std::to_string(wave + 1) + ".log");
            if (pid <= 0) {
                *error = "netbot did not start: " + netbot;
                stopLocked();
                return false;
            }
            netbotPids_.push_back(pid);
        }
        return true;
    }

    // The bots first, so they leave cleanly, then the server, which writes its economy block.
    void stopRun() {
        std::lock_guard<std::mutex> hold(lock_);
        stopLocked();
    }
    bool running() {
        std::lock_guard<std::mutex> hold(lock_);
        reap();
        return serverPid_ > 0;
    }

    // ---- the poll ----------------------------------------------------------------------------
    void poll() {
        std::vector<Process> procs;
        double machine = 0.0;
        sampleProcesses(&procs, &machine);  // `ps` outside the lock
        std::lock_guard<std::mutex> hold(lock_);
        reap();
        readBots();
        readSheets();
        readServer();
        processes_ = std::move(procs);
        machine_ = machine;
        int game = 0;
        for (const auto& [name, b] : bots_) game = std::max(game, b.clock);
        double runCores = 0.0;
        for (const Process& p : processes_) runCores += p.cores;
        samples_.push_back({seconds(), game, runCores, machine});
        if (samples_.size() > 3600) samples_.pop_front();  // half an hour at two a second
    }

    Snapshot snapshot() {
        std::lock_guard<std::mutex> hold(lock_);
        Snapshot s;
        for (const auto& [name, b] : bots_) s.bots.push_back(b);
        s.feed.assign(feed_.begin(), feed_.end());
        std::stable_sort(s.feed.begin(), s.feed.end(), [](const Line& a, const Line& b) { return a.at > b.at; });
        s.trades.assign(trades_.begin(), trades_.end());
        // Up now: a listing lasts 24 game hours (Errands' List), by the seller's own clock.
        for (auto it = offers_.rbegin(); it != offers_.rend(); ++it) {
            const auto b = bots_.find(it->seller);
            if (b != bots_.end() && b->second.clock - it->at > 24 * 3600) continue;
            s.offers.push_back(*it);
        }
        s.processes = processes_;
        s.samples.assign(samples_.begin(), samples_.end());
        s.events.assign(events_.rbegin(), events_.rend());
        s.listed = listed_, s.bought = bought_, s.visits = visits_, s.empty = empty_, s.spent = spent_;
        const double now = seconds();
        for (const auto& [t, ms] : slow_) {
            if (serverClock_ - t < 60.0) {
                ++s.slowLastMinute;
                s.worstSlowMs = std::max(s.worstSlowMs, ms);
            }
        }
        s.machineCores = machine_;
        getloadavg(s.load, 3);
        s.economy = economy();
        s.running = serverPid_ > 0;
        s.netbots = int(netbotPids_.size());
        s.wall = now;
        return s;
    }

private:
    double seconds() const { return std::chrono::duration<double>(Clock::now() - start_).count(); }

    static pid_t spawn(const std::vector<std::string>& args, const std::string& log) {
        posix_spawn_file_actions_t files;
        posix_spawn_file_actions_init(&files);
        posix_spawn_file_actions_addopen(&files, STDOUT_FILENO, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        posix_spawn_file_actions_adddup2(&files, STDOUT_FILENO, STDERR_FILENO);
        std::vector<char*> argv;
        for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        pid_t pid = 0;
        const int failed = posix_spawnp(&pid, argv[0], &files, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&files);
        return failed ? -1 : pid;
    }

    static void ending(pid_t pid) {
        if (pid <= 0) return;
        kill(pid, SIGINT);
        for (int i = 0; i < 80; ++i) {  // eight seconds to go cleanly
            if (waitpid(pid, nullptr, WNOHANG) == pid) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
    }

    void stopLocked() {
        for (const pid_t pid : netbotPids_) ending(pid);
        netbotPids_.clear();
        ending(serverPid_);
        serverPid_ = -1;
    }

    // Netbots that ended by themselves are let go; with none left, the server goes too, as a
    // measurement run's must.
    void reap() {
        if (netbotPids_.empty()) return;
        netbotPids_.erase(std::remove_if(netbotPids_.begin(), netbotPids_.end(),
                                         [](pid_t pid) { return waitpid(pid, nullptr, WNOHANG) == pid; }),
                          netbotPids_.end());
        if (netbotPids_.empty()) {
            ending(serverPid_);
            serverPid_ = -1;
        }
    }

    // What a log grew by since the last read, whole lines only.
    std::vector<std::string> tail(const std::string& name) {
        std::vector<std::string> lines;
        std::ifstream in(run_ + "/" + name, std::ios::binary);
        if (!in) return lines;
        in.seekg(0, std::ios::end);
        const std::streamoff size = in.tellg();
        std::streamoff& at = offsets_[name];
        if (size < at) at = 0;  // a new run wrote it afresh
        in.seekg(at);
        std::string text(size_t(size - at), '\0');
        in.read(text.data(), std::streamsize(text.size()));
        const size_t cut = text.rfind('\n');
        if (cut == std::string::npos) return lines;
        at += std::streamoff(cut + 1);
        size_t from = 0;
        while (from <= cut) {
            const size_t nl = text.find('\n', from);
            lines.push_back(text.substr(from, nl - from));
            from = nl + 1;
        }
        return lines;
    }

    Bot& bot(const std::string& name) {
        Bot& b = bots_[name];
        b.name = name;
        return b;
    }

    void note(int at, const std::string& who, const std::string& what) {
        feed_.push_front({at, who, what});
        if (feed_.size() > 200) feed_.pop_back();
    }

    // Every netbot's log: netbot.log for a run netbot started alone, netbot-N.log for botwatch's.
    std::vector<std::string> botLines() {
        std::vector<std::string> all;
        std::vector<std::string> names = {"netbot.log"};
        for (int n = 1; n <= 64; ++n) {
            const std::string name = "netbot-" + std::to_string(n) + ".log";
            if (std::ifstream(run_ + "/" + name)) names.push_back(name);
        }
        for (const std::string& name : names) {
            std::vector<std::string> lines = tail(name);
            all.insert(all.end(), std::make_move_iterator(lines.begin()), std::make_move_iterator(lines.end()));
        }
        return all;
    }

    void readBots() {
        for (const std::string& raw : botLines()) {
            // "  [broker] Name lists ... / buys X from Y at P (...)"
            if (startsWith(raw, "  [broker] ")) {
                const std::string body = raw.substr(11);
                const size_t sp = body.find(' ');
                if (sp == std::string::npos) continue;
                const std::string who = body.substr(0, sp), rest = body.substr(sp + 1);
                if (startsWith(rest, "lists ")) {
                    ++listed_;
                    if (const size_t atp = rest.rfind(" at "); atp != std::string::npos && atp > 6) {
                        Offer o;
                        o.seller = who;
                        o.item = itemOf(rest.substr(6, atp - 6), &o.held);
                        o.price = std::atoll(rest.c_str() + atp + 4);
                        o.at = bots_.count(who) ? bots_[who].clock : 0;
                        offers_.push_back(o);
                    }
                } else if (startsWith(rest, "buys ")) {
                    const size_t from = rest.rfind(" from "), atp = rest.rfind(" at ");
                    if (from != std::string::npos && atp != std::string::npos && atp > from) {
                        Trade t;
                        t.buyer = who;
                        t.item = itemOf(rest.substr(5, from - 5), &t.held);
                        t.seller = rest.substr(from + 6, atp - from - 6);
                        t.price = std::atoll(rest.c_str() + atp + 4);
                        t.at = bots_.count(who) ? bots_[who].clock : 0;
                        ++bought_;
                        spent_ += t.price;
                        trades_.push_front(t);
                        if (trades_.size() > 100) trades_.pop_back();
                    }
                } else if (startsWith(rest, "buy fell through") && who.size() > 2 && who.compare(who.size() - 2, 2, "'s") == 0) {
                    // "Isen's buy fell through": the "buys" line goes before the server answers, and
                    // one that fell through is no sale (1,415 of 1,867 in a 100-bot run).
                    const std::string buyer = who.substr(0, who.size() - 2);
                    for (auto it = trades_.begin(); it != trades_.end(); ++it) {
                        if (it->buyer != buyer) continue;
                        --bought_;
                        spent_ -= it->price;
                        trades_.erase(it);
                        break;
                    }
                } else if (startsWith(rest, "goes to the Broker")) {
                    ++visits_;
                } else if (startsWith(rest, "finds no upgrade")) {
                    ++empty_;
                }
                continue;
            }
            int at = 0;
            std::string who, rest;
            if (!parseLine(raw, &at, &who, &rest)) continue;
            // "~ Your Blood Bone +1 sold for 142 Zen. ...": that listing is down.
            if (startsWith(rest, "~ Your ")) {
                if (const size_t sold = rest.find(" sold for "); sold != std::string::npos) {
                    const std::string item = rest.substr(7, sold - 7);
                    for (auto it = offers_.begin(); it != offers_.end(); ++it) {
                        if (it->seller == who && it->item == item) {
                            offers_.erase(it);
                            break;
                        }
                    }
                }
                continue;
            }
            if (startsWith(rest, "party:") || startsWith(rest, "~ ")) continue;  // the board, chat
            Bot& b = bot(who);
            // Ten game minutes back is a restart, not lines a little out of order.
            if (at + 600 < b.lastAt) {
                b.carried += b.lastAt;
                b.lastAt = at;
            } else {
                b.lastAt = std::max(b.lastAt, at);
            }
            b.clock = std::max(b.clock, b.carried + at);
            // The events (eventEntry), by his clock summed over restarts.
            {
                const int clock = b.carried + at;
                int tier = 0, rank = 0, of = 0, points = 0;
                long long exp = 0;
                char kind[16] = {};
                if (std::sscanf(rest.c_str(), "** gives the Messenger his cloak: into Blood Castle %d", &tier) == 1) {
                    eventEntry(who, clock, true, tier);
                    b.eventTier = tier;
                } else if (std::sscanf(rest.c_str(), "** gives Charon his invitation: into Devil Square %d", &tier) == 1) {
                    eventEntry(who, clock, false, tier);
                    b.eventTier = tier;
                } else if (std::sscanf(rest.c_str(), "Blood Castle %d: ** Blood Castle wo%1[n]", &tier, kind) == 2) {
                    eventResult(who, true, tier, "won", true);
                } else if (std::sscanf(rest.c_str(), "Blood Castle %d: the run is ove%1[r]", &tier, kind) == 2) {
                    eventResult(who, true, tier, "time ran out", false);
                } else if (std::sscanf(rest.c_str(), "** Devil Square %d: rank %d of %d, %d points, %lld experience", &tier, &rank,
                                       &of, &points, &exp) == 5) {
                    eventResult(who, false, tier,
                                "rank " + std::to_string(rank) + " of " + std::to_string(of) + ", " + std::to_string(points) +
                                    " points",
                                rank == 1);
                }
            }
            int level = 0, hpNow = 0, hpMax = 0, kills = 0, deaths = 0, potions = 0;
            long long zen = 0;
            // "in noria as #12, Dark Wizard level 59 with 89136 zen, 1 players here"
            if (startsWith(rest, "in ")) {
                const size_t as = rest.find(" as #"), comma = rest.find(", ", as == std::string::npos ? 0 : as),
                             lv = rest.find(" level ", comma == std::string::npos ? 0 : comma);
                if (as != std::string::npos && comma != std::string::npos && lv != std::string::npos &&
                    std::sscanf(rest.c_str() + lv, " level %d with %lld zen", &level, &zen) == 2) {
                    b.map = rest.substr(3, as - 3);
                    b.kin = rest.substr(comma + 2, lv - comma - 2);
                    b.level = level;
                    b.zen = zen;
                    continue;
                }
            }
            // "level 47, 288/288 hp, 124294 zen, 444 kills, 2 deaths, 43 potions -- hunting in noria at 51,243"
            if (std::sscanf(rest.c_str(), "level %d, %d/%d hp, %lld zen, %d kills, %d deaths, %d potions", &level,
                            &hpNow, &hpMax, &zen, &kills, &deaths, &potions) == 7) {
                b.level = level, b.zen = zen, b.kills = kills, b.deaths = deaths, b.potions = potions;
                b.hp = std::to_string(hpNow) + "/" + std::to_string(hpMax);
                if (const size_t dash = rest.find(" -- "); dash != std::string::npos) {
                    b.doing = rest.substr(dash + 4);
                    // "hunting in dungeon at 18,220", "in town in lorencia at 140,140": where he is.
                    const size_t in = b.doing.rfind(" in "), at2 = b.doing.rfind(" at ");
                    if (in != std::string::npos && at2 != std::string::npos && at2 > in) {
                        b.map = b.doing.substr(in + 4, at2 - in - 4);
                    }
                }
                continue;
            }
            // "level 2 (0 kills, 42 zen)"
            if (std::sscanf(rest.c_str(), "level %d (%d kills, %lld zen)", &level, &kills, &zen) == 3) {
                b.level = level, b.kills = kills, b.zen = zen;
                note(at, who, "reaches level " + std::to_string(level));
                continue;
            }
            if (startsWith(rest, "grinds in ")) b.map = rest.substr(10);
            if (startsWith(rest, "killed by")) ++b.deaths;
            if (rest[0] != ' ') b.doing = rest;
            static const char* kNotable[] = {"** ", "killed by", "hands in", "quest done", "sets ", "learns",
                                             "no room", "takes ", "goes to town"};
            // The commonest drop, a few a minute a bot, would bury the rest.
            if (rest.find("Jewel of Chaos dropped") != std::string::npos) continue;
            for (const char* n : kNotable) {
                if (startsWith(rest, n)) {
                    note(at, who, rest);
                    break;
                }
            }
        }
    }

    // Slow passes by the server's own clock ("[  5.099] slow pass 15.8 ms"), so a log read
    // whole at the start is not all "the last minute".
    // Each bot's gear score, off the sheet netbot writes (bots/NAME.sheet, Bot::sheet).
    void readSheets() {
        for (auto& [name, b] : bots_) {
            std::ifstream in(run_ + "/bots/" + name + ".sheet");
            b.wings = 0;
            b.dinorant = false;
            b.excellent = 0;
            // NAME\tkin\tsupport\tmagic\ttrader\tlevel\tstrength\tguild id\tguild name\tmaster\t...
            {
                std::ifstream who(run_ + "/bots/guild/" + name + ".who");
                std::string line, field;
                b.guild.clear();
                b.master = false;
                if (std::getline(who, line)) {
                    std::vector<std::string> f;
                    std::stringstream fields(line);
                    while (std::getline(fields, field, '\t')) f.push_back(field);
                    if (f.size() > 9 && f[8] != "-") b.guild = f[8], b.master = f[9] == "1";
                }
            }
            for (std::string line; std::getline(in, line);) {
                if (startsWith(line, "who\tScore\t")) b.score = std::atoi(line.c_str() + 10);
                if (!startsWith(line, "gear\t")) continue;
                // "gear\tWings\tWings of Satan +5 · ..." -- the first wings are MU's three.
                if (startsWith(line, "gear\tWings\tWings of ")) {
                    const bool first = startsWith(line, "gear\tWings\tWings of Elf") ||
                                       startsWith(line, "gear\tWings\tWings of Heaven") ||
                                       startsWith(line, "gear\tWings\tWings of Satan");
                    b.wings = first ? 1 : 2;
                }
                if (startsWith(line, "gear\tMount\tHorn of Dinorant")) b.dinorant = true;
                // "Mace +6 · 2 excellent · +12 option" (Bot::sheet).
                if (line.find(" excellent") != std::string::npos) ++b.excellent;
            }
        }
    }

    void readServer() {
        for (const std::string& raw : tail("server.log")) {
            double t = 0.0;
            if (std::sscanf(raw.c_str(), "[%lf]", &t) == 1) serverClock_ = std::max(serverClock_, t);
            const size_t at = raw.find("slow pass ");
            if (at == std::string::npos) continue;
            slow_.push_back({t, std::atof(raw.c_str() + at + 10)});
            if (slow_.size() > 5000) slow_.pop_front();
        }
    }

    std::string economy() const {
        std::ifstream in(run_ + "/economy.log");
        std::vector<std::string> lines;
        for (std::string l; std::getline(in, l);) lines.push_back(l);
        std::string out;
        for (size_t i = lines.size() > 3 ? lines.size() - 3 : 0; i < lines.size(); ++i) out += lines[i] + "\n";
        return out;
    }

    // The run's server and netbot by their --port, and the whole machine's busy cores.
    void sampleProcesses(std::vector<Process>* mine, double* machine) const {
        FILE* ps = popen("ps -A -o pid=,pcpu=,rss=,command=", "r");
        if (!ps) return;
        const std::string flag = "--port " + std::to_string(port_);
        char buf[4096];
        while (std::fgets(buf, sizeof buf, ps)) {
            int pid = 0, rss = 0, used = 0;
            double cpu = 0.0;
            if (std::sscanf(buf, "%d %lf %d %n", &pid, &cpu, &rss, &used) < 3) continue;
            *machine += cpu / 100.0;
            const std::string command = buf + used;
            const std::string exe = command.substr(0, command.find(' '));
            const std::string base = exe.substr(exe.rfind('/') + 1);
            if ((base == "mu2_server" || base == "netbot") && command.find(flag) != std::string::npos) {
                int speed = 0;
                if (const size_t sp = command.find("--speed "); sp != std::string::npos) speed = std::atoi(command.c_str() + sp + 8);
                mine->push_back({pid, base, cpu / 100.0, rss / 1024, speed});
            }
        }
        pclose(ps);
    }

    std::string run_;
    int port_;
    Clock::time_point start_;
    std::mutex lock_;
    pid_t serverPid_ = -1;
    std::vector<pid_t> netbotPids_;
    std::map<std::string, std::streamoff> offsets_;
    std::map<std::string, Bot> bots_;
    std::deque<Line> feed_;
    std::deque<Trade> trades_;
    std::vector<Offer> offers_;
    std::deque<Sample> samples_;
    std::deque<EventRun> events_;  // oldest first, the last 300

    // An entry ("into Blood Castle 5") joins a run of its kind and tier begun within kEventWindow,
    // else begins one; a result goes to his latest run of that kind and tier.
    void eventEntry(const std::string& who, int clock, bool castle, int tier) {
        for (auto it = events_.rbegin(); it != events_.rend(); ++it) {
            if (it->castle != castle || it->tier != tier || std::abs(clock - it->at) > kEventWindow) continue;
            if (std::find(it->bots.begin(), it->bots.end(), who) == it->bots.end()) it->bots.push_back(who);
            return;
        }
        events_.push_back({clock, castle, tier, {who}, {}, false});
        if (events_.size() > 300) events_.pop_front();
    }
    void eventResult(const std::string& who, bool castle, int tier, const std::string& result, bool won) {
        for (auto it = events_.rbegin(); it != events_.rend(); ++it) {
            if (it->castle != castle || it->tier != tier) continue;
            if (std::find(it->bots.begin(), it->bots.end(), who) == it->bots.end()) continue;
            it->result[who] = result;
            it->won = it->won || won;
            return;
        }
    }
    std::deque<std::pair<double, double>> slow_;
    std::vector<Process> processes_;
    double machine_ = 0.0;
    double serverClock_ = 0.0;  // the server log's newest stamp, seconds
    int listed_ = 0, bought_ = 0, visits_ = 0, empty_ = 0;
    int64_t spent_ = 0;
};

}  // namespace botwatch
