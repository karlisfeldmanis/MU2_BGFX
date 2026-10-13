// **The guilds' board** (docs/guild.md, "Bots"; netbot --guilds). The user, 2026-10-13: "idea is
// that in guild bots has to act different their goal is to be stronger as group", with the picks:
// bots found their own guilds, at level 100 at the Guild Master in Devias as a player does; a
// master invites and a guildless bot asks; a master recruits for what the guild lacks (a tank, an
// energy elf to heal, damage) and for strength; the guild hunts together at the hardest ground
// its parties stand, the strong beside the weakest; spare gear, jewels and Zen go to the guildmate
// they help most, face to face (a trade, anywhere on a map they share: "tardes has to happpen
// anywhere"); the master writes a notice; and members leave, are removed and guilds disband.
// No rivalry between guilds yet.
//
// A guild spans the run's netbots, as the server's does, so the board of each netbot shares a
// folder (--dir's guild/): each bot's line (NAME.who), each guild's plan by its master's netbot
// (NAME.plan: its wants, power and parties), and the gifts' offers and claims. Its parties are
// the Board's groups (Board::guildPlan): a party is led by the netbot of its leader, and a member
// on another netbot follows him as he would a player.
//
// Included inside netbot.cpp's namespace after the Board; not a header to include anywhere else.
#pragma once

class Guilds {
public:
    explicit Guilds(const std::string& botsDir) : root_(botsDir + "/guild") {
        std::error_code ignored;
        std::filesystem::create_directories(root_ + "/offers", ignored);
        std::filesystem::create_directories(root_ + "/claims", ignored);
    }

    void tick(std::vector<std::unique_ptr<NetBot>>& bots, Board& board) {
        const auto now = std::chrono::steady_clock::now();
        if (now < next_) return;
        next_ = now + std::chrono::seconds(1);
        wall_ = int64_t(std::time(nullptr));
        local_.clear();
        mine_.clear();
        for (auto& one : bots) {
            if (!one->driven()) mine_.insert(one->name());
            if (one->up() && !one->driven()) local_[one->name()] = one.get();
        }
        for (auto& [name, one] : local_) {
            std::set<std::string> mates;
            for (const net::GuildState::Member& m : one->guildState().members) {
                if (m.name != name) mates.insert(m.name);
            }
            one->mates(std::move(mates));
            lines(*one);
        }
        if (now >= readAt_) {
            readAt_ = now + std::chrono::seconds(3);
            writeWho();
            readAll();
        }
        for (auto& [name, one] : local_) calls(*one);
        for (auto& [name, one] : local_) {
            // Just come to a map, his guild is not heard yet: read as guildless, a member asked
            // another guild in.
            if (one->upFor() < 10.0) continue;
            const net::GuildState& g = one->guildState();
            if (Founding& f = founding_[name]; f.on && g.id != 0) found(*one, f);
            if (g.id == 0) guildless(*one);
            else if (!g.members.empty() && g.members.front().name == name) master(*one);
            else member(*one);
        }
        if (now >= giftAt_) {
            giftAt_ = now + std::chrono::seconds(5);
            offers();
            claims();
            // With them: each reads every guildmate's claims, a hundred files a pass.
            gifts();
        }
        planBoard(board);
        // Every bot waits on this pass (main's all()): a slow one is said.
        if (const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - now).count(); ms > 200.0) {
            say("a slow pass: %.0f ms", ms);
        }
    }

private:
    enum Role { kTank = 0, kHealer = 1, kDamage = 2 };
    static Role roleOf(int kin, bool support, bool magic) {
        if (kin == int(sim::Kin::DarkKnight) || (kin == int(sim::Kin::MagicGladiator) && !magic)) return kTank;
        if (kin == int(sim::Kin::FairyElf) && support) return kHealer;
        return kDamage;
    }
    static const char* roleName(Role r) { return r == kTank ? "a tank" : r == kHealer ? "a healer" : "damage"; }
    // One bot as his netbot wrote him (NAME.who).
    struct Who {
        std::string name, guildName, party;
        int kin = 0, level = 0, map = -1;
        bool support = false, magic = false, trader = false, master = false, alive = true, event = false;
        double strength = 0.0;
        uint32_t guild = 0;
        int64_t money = 0, clock = 0, wall = 0;
        Role role() const { return roleOf(kin, support, magic); }
    };
    // A guild as its master's netbot planned it (NAME.plan).
    struct Plan {
        uint32_t id = 0;
        std::string name, master;
        int64_t wall = 0;
        int size = 0, cap = 0, meanLevel = 0;
        int need[3] = {0, 0, 0};
        double power = 0.0;
        std::vector<std::vector<std::string>> parties;
    };
    // What a master's netbot keeps of his guild between ticks.
    struct Run {
        int64_t recruitAt = -1000000000, composedAt = -1000000000, noticeAt = -1000000000, kickAt = -1000000000;
        int64_t smallSince = -1;
        int64_t writtenAt = 0;  // the wall's second the plan was last written
        std::set<std::string> online;
        std::vector<std::vector<std::string>> parties;
        std::string notice;
        std::map<std::string, int64_t> invited;  // candidate: the master's clock at the invite
    };
    // A guildless bot's founding: Devias, the Guild Master, a name.
    struct Founding {
        bool on = false;
        int64_t since = 0, sent = -1000000000, next = -1000000000;
        int name = 0;  // into kNames, past the ones taken
    };

    static constexpr int64_t kMinute = 60 * 20;  // game ticks
    static constexpr int64_t kRecruitEvery = 5 * kMinute;
    static constexpr int64_t kInviteAgain = 60 * kMinute;
    static constexpr int64_t kAskEvery = 30 * kMinute;
    static constexpr int64_t kRecompose = 6 * 60 * kMinute, kRecomposeLeast = 5 * kMinute;
    static constexpr int64_t kSmallFor = 6 * 60 * kMinute;  // two or fewer this long: disband or leave
    static constexpr int64_t kKickEvery = 120 * kMinute;
    static constexpr int kFoundLevel = 100, kJoinLevel = 6;
    static constexpr int kBotsAGuild = 10;   // the run's bots over this: the most guilds founded
    static constexpr int kSpan = 40;         // levels: a guild party's strongest over its weakest
    static constexpr double kReplace = 1.5;  // a candidate this much stronger takes the weakest's place
    static constexpr int64_t kStale = 30;    // wall seconds: a bot line older is offline
    static inline const char* kNames[] = {"Vanguard", "Ember", "Ironoak", "Duskfall", "Stormcry", "Ashguard",
                                          "Nightjar", "Wolfhold", "Thornwal", "Ravenhal", "Goldmane", "Frostfen",
                                          "Highmoor", "Oathkeep", "Stoneyew", "Sunspear", "Greywind", "Redcloak",
                                          "Blackfen", "Emberly"};
    static constexpr int kNameCount = int(sizeof(kNames) / sizeof(kNames[0]));

    void writeFile(const std::string& path, const std::string& text) const {
        const std::string tmp = path + ".tmp";
        {
            std::ofstream out(tmp);
            out << text;
        }
        std::error_code ignored;
        std::filesystem::rename(tmp, path, ignored);
    }
    static std::vector<std::string> split(const std::string& line, char by = '\t') {
        std::vector<std::string> out;
        std::string cur;
        for (const char c : line) {
            if (c == by) out.push_back(std::move(cur)), cur.clear();
            else cur += c;
        }
        out.push_back(std::move(cur));
        return out;
    }
    static std::string packed(const sim::Held& one) {
        sim::Command c;
        sim::packHeld(one, c);
        char text[96];
        std::snprintf(text, sizeof(text), "%d %d %d %d %u", c.a, c.b, c.c, c.d, c.target);
        return text;
    }
    static bool unpacked(const std::string& text, sim::Held* out) {
        sim::Command c;
        unsigned target = 0;
        if (std::sscanf(text.c_str(), "%d %d %d %d %u", &c.a, &c.b, &c.c, &c.d, &target) != 5) return false;
        c.target = target;
        *out = sim::unpackHeld(c);
        return !out->empty();
    }
    void say(const char* fmt, ...) const __attribute__((format(printf, 2, 3))) {
        char text[600];
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(text, sizeof(text), fmt, args);
        va_end(args);
        std::printf("  [guild] %s\n", text);
    }
    bool fresh(const Who& w) const { return wall_ - w.wall <= kStale; }
    const Who* who(const std::string& name) const {
        const auto it = who_.find(name);
        return it == who_.end() || !fresh(it->second) ? nullptr : &it->second;
    }
    Role roleOfBot(const NetBot& one) const { return roleOf(int(one.kin()), one.support(), one.magicPath()); }

    // ---- the shared folder --------------------------------------------------------------------
    void writeWho() {
        for (auto& [name, one] : local_) {
            if (one->upFor() < 10.0) continue;  // his guild not heard yet
            const net::GuildState& g = one->guildState();
            const net::PartyState& party = one->partyState();
            char text[512];
            std::snprintf(text, sizeof(text), "%s\t%d\t%d\t%d\t%d\t%d\t%.1f\t%u\t%s\t%d\t%d\t%d\t%lld\t%lld\t%d\t%lld\t%s\n",
                          name.c_str(), int(one->kin()), one->support(), one->magicPath(), one->trader(), one->level(),
                          one->strength(), g.id, g.id ? g.name.c_str() : "-",
                          g.id && !g.members.empty() && g.members.front().name == name, one->mapNow(), one->alive(),
                          (long long)one->money(), (long long)one->clockNow(), one->eventInside() != 0,
                          (long long)wall_, party.members.empty() ? "-" : party.members.front().name.c_str());
            writeFile(root_ + "/" + name + ".who", text);
        }
    }
    void readAll() {
        who_.clear();
        plans_.clear();
        std::error_code e;
        for (const auto& entry : std::filesystem::directory_iterator(root_, e)) {
            const std::string path = entry.path().string();
            const std::string ext = entry.path().extension().string();
            if (ext == ".who") {
                std::ifstream in(path);
                std::string line;
                if (!std::getline(in, line)) continue;
                const std::vector<std::string> f = split(line);
                if (f.size() < 17) continue;
                Who w;
                w.name = f[0];
                w.kin = std::atoi(f[1].c_str());
                w.support = f[2] == "1", w.magic = f[3] == "1", w.trader = f[4] == "1";
                w.level = std::atoi(f[5].c_str());
                w.strength = std::atof(f[6].c_str());
                w.guild = uint32_t(std::strtoul(f[7].c_str(), nullptr, 10));
                w.guildName = f[8];
                w.master = f[9] == "1";
                w.map = std::atoi(f[10].c_str());
                w.alive = f[11] == "1";
                w.money = std::atoll(f[12].c_str());
                w.clock = std::atoll(f[13].c_str());
                w.event = f[14] == "1";
                w.wall = std::atoll(f[15].c_str());
                w.party = f[16];
                who_[w.name] = std::move(w);
            } else if (ext == ".plan") {
                std::ifstream in(path);
                Plan p;
                for (std::string line; std::getline(in, line);) {
                    const std::vector<std::string> f = split(line);
                    if (f[0] == "guild" && f.size() >= 3) p.id = uint32_t(std::strtoul(f[1].c_str(), nullptr, 10)), p.name = f[2];
                    else if (f[0] == "master" && f.size() >= 2) p.master = f[1];
                    else if (f[0] == "wall" && f.size() >= 2) p.wall = std::atoll(f[1].c_str());
                    else if (f[0] == "size" && f.size() >= 3) p.size = std::atoi(f[1].c_str()), p.cap = std::atoi(f[2].c_str());
                    else if (f[0] == "need" && f.size() >= 4) {
                        for (int r = 0; r < 3; ++r) p.need[r] = std::atoi(f[size_t(r) + 1].c_str());
                    } else if (f[0] == "power" && f.size() >= 3) p.power = std::atof(f[1].c_str()), p.meanLevel = std::atoi(f[2].c_str());
                    else if (f[0] == "party" && f.size() >= 3) p.parties.push_back(std::vector<std::string>(f.begin() + 1, f.end()));
                }
                // A plan whose master has not written it in a minute is a guild gone or asleep.
                if (p.id != 0 && wall_ - p.wall <= 60) plans_[p.id] = std::move(p);
            }
        }
    }

    // ---- what the server says, and the cards -------------------------------------------------
    void lines(NetBot& one) {
        for (const std::string& text : one.takeGuildLines()) {
            one.note("[guild] hears: " + text);
            Founding& f = founding_[one.name()];
            if (f.on && text.find(" is taken.") != std::string::npos) ++f.name, f.sent = -1000000000;
        }
    }
    void calls(NetBot& one) {
        const net::GuildState& g = one.guildState();
        int room = g.cap - int(g.members.size());  // taken this second, before the state says so
        for (const net::GuildCall& call : one.takeGuildCalls()) {
            net::GuildAsk answer;
            answer.name = call.from;
            if (!call.ask) {
                // An invite: taken while he is in none and not founding one of his own.
                const bool take = g.id == 0 && !one.trader() && !founding_[one.name()].on;
                answer.what = take ? net::GuildWhat::Accept : net::GuildWhat::Decline;
                one.note(std::string("[guild] ") + (take ? "joins " : "declines ") + call.guildName + ", " + call.from + "'s guild");
                one.askGuild(answer);
                continue;
            }
            // A player asks into his guild: what it lacks, or a member as strong as its middle.
            int have[3], want[3], need[3];
            lacks(g, have, want, need);
            std::string why;
            bool take = false;
            const Who* asker = who(call.from);
            const Role role = asker ? asker->role() : roleOf(call.fromKin, false, false);
            if (room <= 0) {
                why = "the guild is full";
            } else if (need[role] > 0) {
                take = true, why = std::string("we need ") + roleName(role);
            } else if (asker && asker->strength >= medianStrength(g)) {
                take = true, why = "strong enough";
            } else {
                why = std::string("no need for ") + roleName(role) + " and weaker than our middle";
            }
            if (take) {
                --room;
                need[role] = std::max(0, need[role] - 1);
            }
            answer.what = take ? net::GuildWhat::Accept : net::GuildWhat::Decline;
            say("%s %s %s's ask to join %s: %s", one.name().c_str(), take ? "takes" : "declines", call.from.c_str(),
                g.name.c_str(), why.c_str());
            one.askGuild(answer);
        }
    }
    // What the guild lacks, against a cap's worth: three tenths tanks, two healers, the rest damage.
    // Off his own guild's state, not the plan: the plan is read a few seconds late, and the first
    // asks were declined as wanting nothing.
    void lacks(const net::GuildState& g, int have[3], int want[3], int need[3]) const {
        have[0] = have[1] = have[2] = 0;
        for (const net::GuildState::Member& m : g.members) {
            const Who* w = who(m.name);
            ++have[w ? w->role() : roleOf(m.kin, false, false)];
        }
        const int cap = std::max(g.cap, 1);
        want[kTank] = (cap * 3 + 9) / 10;
        want[kHealer] = (cap * 2 + 9) / 10;
        want[kDamage] = cap - want[kTank] - want[kHealer];
        for (int r = 0; r < 3; ++r) need[r] = std::max(0, want[r] - have[r]);
    }
    double medianStrength(const net::GuildState& g) const {
        std::vector<double> s;
        for (const net::GuildState::Member& m : g.members) {
            if (const Who* w = who(m.name)) s.push_back(w->strength);
        }
        if (s.empty()) return 0.0;
        std::nth_element(s.begin(), s.begin() + long(s.size() / 2), s.end());
        return s[s.size() / 2];
    }

    // ---- a guildless bot: found one, or ask into one -------------------------------------------
    int mostGuilds() const {
        int bots = 0;
        for (const auto& [name, w] : who_) bots += fresh(w) && !w.trader;
        return std::max(2, (bots + kBotsAGuild - 1) / kBotsAGuild);
    }
    // The guild with room that needs his role most, then the strongest; 0 for none.
    uint32_t wanting(Role role) const {
        uint32_t best = 0;
        std::pair<int, double> most{0, 0.0};
        for (const auto& [id, p] : plans_) {
            if (p.size >= p.cap || p.need[role] <= 0) continue;
            const std::pair<int, double> score{p.need[role], p.power};
            if (score > most) most = score, best = id;
        }
        return best;
    }
    void guildless(NetBot& one) {
        const std::string& name = one.name();
        if (one.trader() || one.level() < kJoinLevel || one.eventInside() != 0 || !one.alive()) return;
        Founding& f = founding_[name];
        const int64_t clock = one.clockNow();
        const Role role = roleOfBot(one);
        if (f.on) return found(one, f);
        bool another = false;  // one founder at a time on this netbot: five set out at once
        for (const auto& [other, of] : founding_) another |= of.on && other != name;
        if (one.level() >= kFoundLevel && clock >= f.next && !another && wanting(role) == 0 &&
            int(plans_.size()) < mostGuilds()) {
            f.on = true;
            f.since = clock;
            f.name = int(std::hash<std::string>{}(name) % size_t(kNameCount));
            say("%s, level %d, sets out to found a guild (%zu of %d guilds stand)", name.c_str(), one.level(),
                plans_.size(), mostGuilds());
            return found(one, f);
        }
        int64_t& asked = askedAt_[name];
        if (asked != 0 && clock - asked < kAskEvery) return;
        const uint32_t to = wanting(role);
        if (to == 0) return;
        asked = clock;
        const Plan& p = plans_.at(to);
        if (p.master == name) return;  // his own, its state not heard
        net::GuildAsk ask;
        ask.what = net::GuildWhat::Ask;
        ask.name = p.master;
        one.askGuild(ask);
        say("%s (%s, level %d) asks %s to join %s", name.c_str(), roleName(role), one.level(), p.master.c_str(), p.name.c_str());
    }
    static uint32_t hashOf(const std::string& s) {
        uint32_t h = 2166136261u;
        for (const char c : s) h = (h ^ uint8_t(c)) * 16777619u;
        return h;
    }
    // A mark of two colours, mirrored as a crest is, off the guild's name.
    static net::GuildMarkBits markOf(const std::string& name) {
        uint32_t h = hashOf(name);
        const int a = 2 + int(h % 14), b = 2 + int((h / 14) % 14);
        net::GuildMarkBits mark{};
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 4; ++x) {
                h = h * 1103515245u + 12345u;
                const int pick = int((h >> 16) % 3);
                const int pixel = (y == 3 || y == 4) && x == 3 ? a : pick == 0 ? 0 : pick == 1 ? a : (b == a ? 1 : b);
                for (const int at : {x, 7 - x}) {
                    uint8_t& two = mark[size_t((y * 8 + at) / 2)];
                    if (at % 2 == 0) two = uint8_t((two & 0x0F) | (pixel << 4));
                    else two = uint8_t((two & 0xF0) | pixel);
                }
            }
        }
        return mark;
    }
    void found(NetBot& one, Founding& f) {
        const int64_t clock = one.clockNow();
        const auto stop = [&](const char* why) {
            say("%s %s", one.name().c_str(), why);
            one.order(-1);
            one.errand(-1, -1);
            one.founding(false);
            f.on = false;
            f.next = clock + 120 * kMinute;
        };
        if (one.guildState().id != 0) {
            say("%s founded %s", one.name().c_str(), one.guildState().name.c_str());
            one.order(-1);
            one.errand(-1, -1);
            one.founding(false);
            f.on = false;
            return;
        }
        one.founding(true);
        if (clock - f.since > 40 * kMinute) return stop("gave up founding: the Guild Master was not reached in 40 game minutes");
        if (int(plans_.size()) >= mostGuilds()) return stop("leaves founding: enough guilds stand now");
        if (one.world() != "devias") {
            if (one.ordered() < 0) {
                int devias = -1;
                for (const WorldRow& w : worldRows()) {
                    if (std::string(w.name) == "devias") devias = w.map;
                }
                if (devias < 0 || !one.order(devias)) return stop("cannot reach Devias to found a guild");
            }
            return;
        }
        int column = 0, row = 0;
        if (!one.guildMasterTile(&column, &row)) return stop("found no Guild Master in Devias");
        one.errand(column, row + 1);
        const int far = std::max(std::abs(one.column() - column), std::abs(one.row() - row));
        if (far > 6 || clock - f.sent < kMinute) return;
        // Taken names are skipped as the server says so (lines()), and any a plan holds.
        std::set<std::string> taken;
        for (const auto& [id, p] : plans_) taken.insert(p.name);
        for (int tries = 0; tries < kNameCount && taken.count(kNames[f.name % kNameCount]); ++tries) ++f.name;
        const std::string name = kNames[f.name % kNameCount];
        f.sent = clock;
        net::GuildAsk ask;
        ask.what = net::GuildWhat::Found;
        ask.name = name;
        ask.mark = markOf(name);
        one.askGuild(ask);
        one.note("[guild] asks the Guild Master to found " + name);
    }

    // ---- a member: leave a guild that never grew ----------------------------------------------
    void member(NetBot& one) {
        const net::GuildState& g = one.guildState();
        const int64_t clock = one.clockNow();
        int64_t& since = smallSince_[one.name()];
        if (int(g.members.size()) > 2) {
            since = 0;
            return;
        }
        if (since == 0) since = clock;
        if (clock - since < kSmallFor) return;
        const uint32_t to = wanting(roleOfBot(one));
        if (to == 0 || to == g.id) return;
        since = 0;
        say("%s leaves %s: two of us for six game hours, and %s needs %s", one.name().c_str(), g.name.c_str(),
            plans_.at(to).name.c_str(), roleName(roleOfBot(one)));
        net::GuildAsk leave;
        leave.what = net::GuildWhat::Leave;
        one.askGuild(leave);
        askedAt_[one.name()] = 0;  // he asks the other at once
    }

    // ---- a master: recruit, plan the parties, the notice, remove, disband ----------------------
    void master(NetBot& one) {
        const net::GuildState& g = one.guildState();
        Run& run = runs_[g.id];
        const int64_t clock = one.clockNow();
        int have[3], want[3], need[3];
        lacks(g, have, want, need);
        double power = 0.0;
        int levels = 0;
        std::vector<std::pair<double, std::string>> byStrength;
        for (const net::GuildState::Member& m : g.members) {
            const Who* w = who(m.name);
            levels += w ? w->level : m.level;
            if (w) power += w->strength, byStrength.push_back({w->strength, m.name});
        }
        const int size = int(g.members.size());
        // **Recruit**: the best guildless bot -- what we lack first, then strength.
        if (clock - run.recruitAt >= kRecruitEvery) {
            run.recruitAt = clock;
            const double middle = medianStrength(g);
            const Who* best = nullptr;
            double bestScore = -1.0;
            for (const auto& [name, w] : who_) {
                if (!fresh(w) || w.guild != 0 || w.trader || w.level < kJoinLevel || w.event) continue;
                bool ours = false;  // his line a few seconds old: joined since
                for (const net::GuildState::Member& m : g.members) ours |= m.name == name;
                if (ours) continue;
                if (const auto at = run.invited.find(name); at != run.invited.end() && clock - at->second < kInviteAgain) continue;
                if (pendingInvite_.count(name) && wall_ - pendingInvite_[name] < 60) continue;
                const double score = (need[w.role()] > 0 ? 2.0 : 0.0) + std::min(2.0, w.strength / std::max(1.0, middle));
                if (score > bestScore) bestScore = score, best = &w;
            }
            if (best != nullptr && size < g.cap) {
                run.invited[best->name] = clock;
                pendingInvite_[best->name] = wall_;
                net::GuildAsk invite;
                invite.what = net::GuildWhat::Invite;
                invite.name = best->name;
                one.askGuild(invite);
                say("%s invites %s (%s, level %d) into %s, %d of %d%s", one.name().c_str(), best->name.c_str(),
                    roleName(best->role()), best->level, g.name.c_str(), size, g.cap,
                    need[best->role()] > 0 ? ": we lack one" : "");
            } else if (best != nullptr && size >= g.cap && clock - run.kickAt >= kKickEvery && byStrength.size() >= 2) {
                // **Full**: the weakest, not the master, makes room for one much stronger who does
                // not leave a role short.
                std::sort(byStrength.begin(), byStrength.end());
                for (const auto& [s, name] : byStrength) {
                    if (name == one.name()) continue;
                    const Who* w = who(name);
                    if (w && have[w->role()] <= want[w->role()] && w->role() != best->role()) break;
                    if (best->strength < s * kReplace) break;
                    run.kickAt = clock;
                    net::GuildAsk remove;
                    remove.what = net::GuildWhat::Remove;
                    remove.name = name;
                    one.askGuild(remove);
                    say("%s removes %s (strength %.0f) from %s to make room for %s (%.0f)", one.name().c_str(), name.c_str(),
                        s, g.name.c_str(), best->name.c_str(), best->strength);
                    run.recruitAt = clock - kRecruitEvery;  // invite him next
                    run.invited.erase(best->name);
                    break;
                }
            }
        }
        // **The parties**: online members, the strongest leading, the weakest within kSpan beside him.
        // Online, alive or not: counted living, each death planned the parties again, and a
        // party formed and parted thirty times in a game hour (the guild run, 2026-10-13).
        std::set<std::string> online;
        for (const net::GuildState::Member& m : g.members) {
            if (const Who* w = who(m.name); w && !w->trader) online.insert(m.name);
        }
        // The plan stands until one of it is gone; one come online joins a party with room
        // within kSpan of its head; all of it again only past kRecompose.
        bool again = clock - run.composedAt >= kRecompose;
        std::set<std::string> placed;
        for (const auto& p : run.parties) {
            for (const std::string& m : p) {
                placed.insert(m);
                again |= !online.count(m);
            }
        }
        std::vector<std::string> loose;
        for (const std::string& m : online) {
            if (!placed.count(m)) loose.push_back(m);
        }
        if (!again && !loose.empty() && clock - run.composedAt >= kRecomposeLeast) {
            std::vector<std::string> left;
            for (const std::string& m : loose) {
                const Who* w = who(m);
                bool in = false;
                for (auto& p : run.parties) {
                    const Who* head = who(p.front());
                    if (!in && w && head && p.size() < size_t(sim::kPartyMost) && head->level - w->level <= kSpan &&
                        w->level <= head->level) {
                        p.push_back(m);
                        in = true;
                    }
                }
                if (!in) left.push_back(m);
            }
            again = left.size() >= 2;
            run.composedAt = clock;
        }
        if (again) {
            const auto before = run.parties;
            run.parties = compose(online);
            run.online = online;
            run.composedAt = clock;
            if (run.parties != before) {
                std::string said;
                for (const auto& p : run.parties) {
                    said += said.empty() ? "" : "; ";
                    for (size_t i = 0; i < p.size(); ++i) said += (i ? ", " : "") + p[i];
                }
                say("%s plans %s's parties: %s", one.name().c_str(), g.name.c_str(), said.empty() ? "none" : said.c_str());
            }
        }
        // **The notice**, as it changes, half a game hour apart at most.
        std::string notice;
        if (!run.parties.empty()) {
            notice = "Hunt with ";
            for (size_t i = 0; i < run.parties.size(); ++i) notice += (i ? ", " : "") + run.parties[i].front();
            notice += ". Spares to guildmates.";
        } else {
            notice = "Recruiting " + std::string(need[kHealer] ? "healers" : need[kTank] ? "tanks" : "all") + ". Spares to guildmates.";
        }
        if (notice.size() > 60) notice = notice.substr(0, 57) + "...";
        if (notice != run.notice && clock - run.noticeAt >= 30 * kMinute) {
            run.notice = notice;
            run.noticeAt = clock;
            net::GuildAsk ask;
            ask.what = net::GuildWhat::Notice;
            ask.text = notice;
            one.askGuild(ask);
            say("%s sets %s's notice: %s", one.name().c_str(), g.name.c_str(), notice.c_str());
        }
        // **Disband** one that never grew, while another has room.
        if (size <= 2) {
            if (run.smallSince < 0) run.smallSince = clock;
            bool elsewhere = false;
            for (const auto& [id, p] : plans_) elsewhere |= id != g.id && p.size < p.cap;
            if (clock - run.smallSince >= kSmallFor && elsewhere) {
                say("%s disbands %s: %d member%s for six game hours", one.name().c_str(), g.name.c_str(), size, size == 1 ? "" : "s");
                net::GuildAsk ask;
                ask.what = net::GuildWhat::Disband;
                ask.name = g.name;
                one.askGuild(ask);
                std::error_code ignored;
                std::filesystem::remove(root_ + "/" + g.name + ".plan", ignored);
                runs_.erase(g.id);
                founding_[one.name()].next = clock + kSmallFor;  // not founding again at once
                return;
            }
        } else {
            run.smallSince = -1;
        }
        // The plan, for every netbot, as often as they read it.
        if (wall_ - run.writtenAt < 3) return;
        run.writtenAt = wall_;
        std::string text = "guild\t" + std::to_string(g.id) + "\t" + g.name + "\nmaster\t" + one.name() + "\nwall\t" +
                           std::to_string(wall_) + "\nsize\t" + std::to_string(size) + "\t" + std::to_string(g.cap) +
                           "\nneed\t" + std::to_string(need[0]) + "\t" + std::to_string(need[1]) + "\t" + std::to_string(need[2]) +
                           "\npower\t" + std::to_string(power) + "\t" + std::to_string(size ? levels / size : 0) + "\n";
        for (const auto& p : run.parties) {
            text += "party";
            for (const std::string& m : p) text += "\t" + m;
            text += "\n";
        }
        writeFile(root_ + "/" + g.name + ".plan", text);
    }
    // Parties of up to five: the strongest left leads; beside him a tank and a healer if the
    // guild has them within kSpan levels, then the weakest within it -- carried, and the ground
    // one they all stand (the Board picks the most experience an hour the party stands).
    std::vector<std::vector<std::string>> compose(const std::set<std::string>& online) const {
        std::vector<const Who*> left;
        for (const std::string& n : online) {
            if (const Who* w = who(n); w && !w->event) left.push_back(w);
        }
        std::sort(left.begin(), left.end(), [](const Who* a, const Who* b) {
            return a->level != b->level ? a->level > b->level : a->name < b->name;
        });
        std::vector<std::vector<std::string>> out;
        while (left.size() >= 2) {
            const Who* head = left.front();
            left.erase(left.begin());
            std::vector<const Who*> party{head};
            const auto within = [&](const Who* w) { return head->level - w->level <= kSpan; };
            const auto take = [&](auto pred) {
                // The weakest that fits: the strong pull the weak up.
                for (auto it = left.rbegin(); it != left.rend(); ++it) {
                    if (within(*it) && pred(*it)) {
                        party.push_back(*it);
                        left.erase(std::next(it).base());
                        return true;
                    }
                }
                return false;
            };
            const auto has = [&](Role r) {
                for (const Who* w : party) {
                    if (w->role() == r) return true;
                }
                return false;
            };
            if (!has(kTank)) take([](const Who* w) { return w->role() == kTank; });
            if (!has(kHealer)) take([](const Who* w) { return w->role() == kHealer; });
            while (party.size() < size_t(sim::kPartyMost) && take([](const Who*) { return true; })) {}
            if (party.size() < 2) continue;
            std::vector<std::string> names;
            for (const Who* w : party) names.push_back(w->name);
            out.push_back(std::move(names));
        }
        return out;
    }

    // ---- the gifts: spares offered, claimed by the guildmate they help most, handed over -----
    // Zen past what a bot of his level keeps is offered; a bot under a level's share claims it.
    static int64_t zenKeep(int level) { return 3000000 + int64_t(level) * 20000; }
    static int64_t zenShort(int level) { return 200000 + int64_t(level) * 15000; }
    void offers() {
        for (auto& [name, one] : local_) {
            if (one->upFor() < 10.0) continue;  // his guild not heard yet
            const net::GuildState& g = one->guildState();
            const std::string path = root_ + "/offers/" + name;
            if (g.id == 0 || !one->alive() || one->eventInside() != 0) {
                std::error_code ignored;
                std::filesystem::remove(path, ignored);
                continue;
            }
            std::string text = "guild\t" + std::to_string(g.id) + "\nwall\t" + std::to_string(wall_) + "\n";
            const content::Tables* tables = one->tables();
            for (const int slot : one->spares()) {
                const sim::Held held = one->held(slot);
                if (held.empty() || !tables) continue;
                text += "item\t" + packed(held) + "\t" + tables->items[size_t(held.item)].label + "\n";
            }
            const int64_t spare = one->money() - zenKeep(one->level());
            if (spare > 0) text += "zen\t" + std::to_string(std::min<int64_t>(spare / 3, 5000000)) + "\n";
            writeFile(path, text);
        }
    }
    struct Offer {
        std::string giver;
        std::vector<std::pair<std::string, std::string>> items;  // packed, label
        int64_t zen = 0;
    };
    Offer readOffer(const std::string& giver, uint32_t guild) const {
        Offer o;
        o.giver = giver;
        std::ifstream in(root_ + "/offers/" + giver);
        bool ours = false, recent = false;
        for (std::string line; std::getline(in, line);) {
            const std::vector<std::string> f = split(line);
            if (f[0] == "guild" && f.size() >= 2) ours = uint32_t(std::strtoul(f[1].c_str(), nullptr, 10)) == guild;
            else if (f[0] == "wall" && f.size() >= 2) recent = wall_ - std::atoll(f[1].c_str()) <= 60;
            else if (f[0] == "item" && f.size() >= 3) o.items.push_back({f[1], f[2]});
            else if (f[0] == "zen" && f.size() >= 2) o.zen = std::atoll(f[1].c_str());
        }
        if (!ours || !recent) o.items.clear(), o.zen = 0;
        return o;
    }
    static constexpr int kWorthsATick = 12;
    void claims() {
        int worths = 0;
        for (auto& [name, one] : local_) {
            if (one->upFor() < 10.0) continue;  // his guild not heard yet
            const net::GuildState& g = one->guildState();
            const std::string path = root_ + "/claims/" + name;
            if (g.id == 0 || one->eventInside() != 0) {
                std::error_code ignored;
                std::filesystem::remove(path, ignored);
                continue;
            }
            std::string text = "guild\t" + std::to_string(g.id) + "\nwall\t" + std::to_string(wall_) + "\n";
            // **One of a kind**: each copy tried alone helped him, and Isen was handed four Imps
            // in a trade and passed them on (the guild run, 2026-10-13). The best of each item.
            std::map<int, std::pair<double, std::string>> best;  // item -> gain, its claim line
            for (const net::GuildState::Member& m : g.members) {
                if (m.name == name) continue;
                const Offer o = readOffer(m.name, g.id);
                for (const auto& [key, label] : o.items) {
                    // Each thing tried on him once in ten minutes of the wall's, and at most
                    // kWorthsATick a pass: a trial lays his whole world on the scratch, under every
                    // bot's lock, and six full guilds' offers held the bots' play to half the
                    // server's pace (the guild run, 2026-10-13).
                    auto& seen = worthSeen_[name][m.name + "|" + key];
                    if ((seen.first == 0 || wall_ - seen.first > 600) && worths < kWorthsATick) {
                        ++worths;
                        sim::Held held;
                        seen = {wall_, unpacked(key, &held) ? one->worth(held) : 0.0};
                    }
                    sim::Held held;
                    if (seen.second > 0.0 && unpacked(key, &held) && seen.second > best[held.item].first) {
                        char gain[64];
                        std::snprintf(gain, sizeof(gain), "%.3f", seen.second);
                        best[held.item] = {seen.second, "item\t" + m.name + "\t" + key + "\t" + gain + "\n"};
                    }
                }
            }
            for (const auto& [item, claim] : best) text += claim.second;
            if (one->money() < zenShort(one->level())) text += "zen\t" + std::to_string(zenShort(one->level()) - one->money()) + "\n";
            writeFile(path, text);
        }
    }
    void gifts() {
        for (auto& [name, one] : local_) {
            if (const int end = one->takeGiftEnd(); end != 0) {
                giftNext_[name] = one->clockNow() + (end > 0 ? kMinute : 3 * kMinute);
                if (end < 0) pairNext_[name + "|" + giftTo_[name]] = one->clockNow() + 15 * kMinute;
                else ++given_;
            }
        }
        std::set<std::string> taking;  // receivers of a gift under way here
        for (auto& [name, one] : local_) {
            if (one->giving()) taking.insert(giftTo_[name]);
        }
        for (auto& [name, one] : local_) {
            const net::GuildState& g = one->guildState();
            if (g.id == 0 || one->giving() || one->guildBusy() || one->onErrand() || !one->alive() || one->eventInside() != 0) continue;
            if (const auto at = giftNext_.find(name); at != giftNext_.end() && one->clockNow() < at->second) continue;
            const Offer mine = readOffer(name, g.id);
            if (mine.items.empty() && mine.zen <= 0) continue;
            // Each guildmate on his map: what of mine he claims, and the Zen he is short.
            std::string bestTo;
            double bestGain = 0.0;
            std::vector<std::pair<std::string, std::string>> bestItems;
            int64_t bestZen = 0;
            for (const net::GuildState::Member& m : g.members) {
                if (m.name == name || taking.count(m.name)) continue;
                const Who* w = who(m.name);
                if (!w || !w->alive || w->event || w->map != one->mapNow()) continue;
                if (const auto at = pairNext_.find(name + "|" + m.name); at != pairNext_.end() && one->clockNow() < at->second) continue;
                std::ifstream in(root_ + "/claims/" + m.name);
                double gain = 0.0;
                int64_t zen = 0;
                std::vector<std::pair<std::string, std::string>> items;
                bool ours = false;
                for (std::string line; std::getline(in, line);) {
                    const std::vector<std::string> f = split(line);
                    if (f[0] == "guild" && f.size() >= 2) ours = uint32_t(std::strtoul(f[1].c_str(), nullptr, 10)) == g.id;
                    else if (f[0] == "item" && f.size() >= 4 && f[1] == name) {
                        for (const auto& [key, label] : mine.items) {
                            if (key == f[2]) items.push_back({key, label}), gain += std::atof(f[3].c_str());
                        }
                    } else if (f[0] == "zen" && f.size() >= 2) {
                        zen = std::min<int64_t>(mine.zen, std::atoll(f[1].c_str()));
                    }
                }
                if (!ours) continue;
                // Zen counts as the gift's weight by the share of his level's need it fills.
                const double weight = gain + (zen > 0 ? double(zen) / double(zenShort(w->level)) * 10.0 : 0.0);
                if (weight > bestGain) bestGain = weight, bestTo = m.name, bestItems = items, bestZen = zen;
            }
            if (bestTo.empty()) continue;
            std::vector<sim::Held> things;
            std::string what;
            for (const auto& [key, label] : bestItems) {
                sim::Held held;
                if (!unpacked(key, &held) || things.size() >= 8) continue;
                things.push_back(held);
                what += (what.empty() ? "" : ", ") + label;
            }
            if (bestZen > 0) {
                char zen[48];
                std::snprintf(zen, sizeof(zen), "%s%lld zen", what.empty() ? "" : " and ", (long long)bestZen);
                what += zen;
            }
            if (things.empty() && bestZen <= 0) continue;
            giftTo_[name] = bestTo;
            taking.insert(bestTo);
            one->give(bestTo, std::move(things), bestZen, what);
        }
    }

    // ---- the Board's share: each bound member's leader, and the parties led here --------------
    void planBoard(Board& board) {
        std::map<std::string, std::string> bound;
        std::vector<Board::GuildParty> parties;
        for (const auto& [id, p] : plans_) {
            for (const auto& party : p.parties) {
                if (party.size() < 2) continue;
                for (const std::string& m : party) bound[m] = party.front();
                if (mine_.count(party.front())) parties.push_back({p.name, party.front(), party});
            }
        }
        board.guildPlan(std::move(bound), std::move(parties));
    }

    std::string root_;
    std::chrono::steady_clock::time_point next_{}, readAt_{}, giftAt_{};
    int64_t wall_ = 0;
    std::map<std::string, NetBot*> local_;
    std::set<std::string> mine_;  // this netbot's bots, up or between maps
    std::map<std::string, Who> who_;
    std::map<uint32_t, Plan> plans_;
    std::map<uint32_t, Run> runs_;
    std::map<std::string, Founding> founding_;
    std::map<std::string, int64_t> askedAt_, smallSince_, pendingInvite_;
    std::map<std::string, std::map<std::string, std::pair<int64_t, double>>> worthSeen_;
    std::map<std::string, int64_t> giftNext_, pairNext_;
    std::map<std::string, std::string> giftTo_;
    int given_ = 0;
};
