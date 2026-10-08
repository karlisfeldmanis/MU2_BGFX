// The net bot: Lorencia characters played on the server, the way a client plays them -- a Hello,
// the Welcome, a mirror stepped with every tick the server sends, and nothing asked but the
// commands a player's clicks become (sim/command.h). Whoever plays on the same server sees them
// walk out of town, fight, drink, pick up what falls, go back to Amy for potions and level.
//
// Not tools/bot: that one plays a realm of its own in this process, for hours in seconds, and
// measures the rules. This one plays the server's world in real time, for the world to have
// people in it. Its brain is a small one: hunt what his level takes, the nearest first; drink
// under half; pick up Zen, potions and jewels; spend his points by his class; rest in the safe
// zone when he is out of potions and low; buy potions from Lorencia's potion seller.
//
// Each bot keeps its character's token in --dir (saves/bots by default), so a bot run again comes
// back as the same character, at the level it left.
//
//   build/netbot [--server HOST] [--port N] [--dir DIR] [--hours H] NAME:CLASS [NAME:CLASS ...]
//
// CLASS is dk or dw (a new elf starts in Noria; a gladiator is not a new character's).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "content/tables.h"
#include "core/log.h"
#include "game/remote_link.h"
#include "net/wire.h"
#include "sim/command.h"
#include "sim/cradle.h"
#include "sim/items.h"
#include "sim/market.h"
#include "sim/realm.h"
#include "sim/skills.h"

using namespace mu;

namespace {

volatile std::sig_atomic_t g_stop = 0;
void onSignal(int) { g_stop = 1; }

constexpr int kThink = 5;            // ticks between decisions: four a second, as tools/bot
constexpr float kSight = 12.0f;      // MU's InfoRange
constexpr float kLootReach = 8.0f;
constexpr int64_t kGiveUp = 20 * 20; // a target not won in twenty seconds is left a while
constexpr int64_t kForget = 3 * 60 * 20;
constexpr int kPotionsWanted = 20;   // what a trip to Amy fills the bag to

const char* kinName(sim::Kin kin) {
    switch (kin) {
        case sim::Kin::DarkWizard: return "Dark Wizard";
        case sim::Kin::FairyElf: return "Fairy Elf";
        case sim::Kin::DarkKnight: return "Dark Knight";
        case sim::Kin::MagicGladiator: return "Magic Gladiator";
    }
    return "?";
}

class NetBot {
public:
    NetBot(std::string name, sim::Kin kin, std::string host, int port, std::string dir)
        : name_(std::move(name)), kin_(kin), host_(std::move(host)), port_(port), dir_(std::move(dir)) {
        loadToken();
    }

    const std::string& name() const { return name_; }

    // Joins `world` (the server may answer that he is elsewhere: then that one), raises the
    // mirror from the Welcome as the client does (game/play_open.cpp) and replays its past.
    bool connect() {
        std::string world = world_;
        for (int tries = 0; tries < 3; ++tries) {
            realm_ = std::make_unique<sim::Realm>();
            link_ = std::make_unique<game::RemoteLink>(*realm_);
            net::Hello hello;
            hello.world = world;
            hello.kin = uint8_t(kin_);
            hello.token = token_;
            net::Welcome welcome;
            net::Elsewhere elsewhere;
            if (!link_->join(host_, port_, hello, welcome, &elsewhere)) {
                if (!elsewhere.world.empty()) {
                    say("is in %s, not %s: goes there", elsewhere.world.c_str(), world.c_str());
                    world = elsewhere.world;
                    continue;
                }
                say("could not join %s:%d", host_.c_str(), port_);
                return false;
            }
            world_ = welcome.world;
            if (token_ != welcome.token) {
                token_ = welcome.token;
                saveToken();
            }
            const std::string path =
                std::string(MU2_ASSET_DIR) + "/cooked/" + world_ + "/" + world_ + ".mur";
            std::string error;
            tables_ = std::make_unique<content::Tables>();
            if (!content::loadTables(path, *tables_, error)) {
                say("no tables for %s: %s", world_.c_str(), error.c_str());
                return false;
            }
            realm_->configure(welcome.config);
            if (!realm_->raise(tables_.get(), welcome.seed, welcome.column, welcome.row,
                               sim::Kin(welcome.kin), welcome.level)) {
                say("the mirror of %s would not raise", world_.c_str());
                return false;
            }
            if (welcome.castle > 0) realm_->setCastle(welcome.castle);
            sim::outfit(*realm_, welcome.weapon, welcome.shield);
            if (welcome.kept) realm_->restoreKept(welcome.first);
            link_->catchUp();
            settle();
            const sim::Body& me = realm_->hero();
            say("in %s as #%u, %s level %d at %d,%d with %lld zen, %d players here", world_.c_str(),
                me.id, kinName(me.kin), me.level, me.column(), me.row(), (long long)realm_->money(),
                realm_->playersHere());
            lastLevel_ = me.level;
            return true;
        }
        return false;
    }

    // The network in and out, every tick the server has sent stepped, and a think every kThink.
    // False once the line is gone.
    bool update() {
        if (!link_ || !link_->connected()) return false;
        link_->pump();
        while (link_->due()) {
            link_->step();
            ++ticks_;
            read();
            if (ticks_ % kThink == 0) think();
        }
        if (link_->divergedAt() != 0 && !divergenceSaid_) {
            divergenceSaid_ = true;
            say("!! the mirror diverged from the server at tick %u", link_->divergedAt());
        }
        return link_->connected();
    }

    void report() const {
        if (!realm_) return;
        const sim::Body& me = realm_->hero();
        say("level %d, %d/%d hp, %lld zen, %d kills, %d deaths, %d potions -- %s at %d,%d", me.level,
            me.health, me.maxHealth, (long long)realm_->money(), kills_, deaths_, countOf(sim::heals),
            modeName(), me.column(), me.row());
    }

private:
    enum class Mode { Hunt, Rest, Shop };

    // ---- the character's token ---------------------------------------------------------------
    std::string tokenPath() const { return dir_ + "/" + name_ + ".token"; }
    void loadToken() {
        std::ifstream in(tokenPath());
        if (in >> std::hex >> token_ >> world_) {
            say("comes back as %016llx in %s", (unsigned long long)token_, world_.c_str());
        } else {
            token_ = 0;
            world_ = "lorencia";
        }
    }
    void saveToken() const {
        std::error_code ignored;
        std::filesystem::create_directories(dir_, ignored);
        std::ofstream out(tokenPath());
        out << std::hex << token_ << " " << world_ << "\n";
    }

    void say(const char* fmt, ...) const {
        char line[512];
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(line, sizeof(line), fmt, args);
        va_end(args);
        const long long s = ticks_ / 20;
        std::printf("%02lld:%02lld:%02lld %-10s %s\n", s / 3600, s / 60 % 60, s % 60, name_.c_str(), line);
        std::fflush(stdout);
    }
    const char* modeName() const {
        return mode_ == Mode::Hunt ? "hunting" : mode_ == Mode::Rest ? "resting" : "shopping";
    }

    // What hangs off a world: its potion seller and its safe zone.
    void settle() {
        // The one who stands at a counter in the safe zone (Amy), before a wanderer (Martin).
        potionFolk_ = -1;
        bool atCounter = false;
        for (size_t i = 0; i < tables_->folk.size(); ++i) {
            int count = 0;
            bool sellsThem = false;
            const sim::Offer* shelf = sim::stockOf(tables_->folk[i].number, &count);
            for (int k = 0; k < count; ++k) {
                const int item = tables_->itemAt(shelf[k].group, shelf[k].number);
                if (item >= 0 && sim::heals(tables_->items[size_t(item)])) sellsThem = true;
            }
            if (!sellsThem) continue;
            int column = 0, row = 0;
            const bool counter = realm_->folkTile(int(i), &column, &row) && tables_->grid.safe(column, row) &&
                                 tables_->folk[i].name.find("Wandering") == std::string::npos;
            if (potionFolk_ < 0 || (counter && !atCounter)) {
                potionFolk_ = int(i);
                atCounter = counter;
            }
        }
        const int32_t* box = tables_->safeGate;
        restAt_[0] = (box[0] + box[2]) / 2;
        restAt_[1] = (box[1] + box[3]) / 2;
        mode_ = Mode::Hunt;
        banned_.clear();
        chasing_ = 0;
    }

    // ---- the commands, the only hand he has -----------------------------------------------
    void send(sim::Command one) {
        one.player = realm_->hero().id;
        link_->send(one);
    }
    void order(sim::Request::Kind kind, int column, int row, uint32_t target = 0, int skill = 0) {
        sim::Command one;
        one.kind = sim::Command::Kind::Order;
        one.a = int(kind);
        one.b = column;
        one.c = row;
        one.d = skill;
        one.target = target;
        send(one);
    }
    void walkTo(int column, int row) { order(sim::Request::Kind::WalkTo, column, row); }

    // ---- what the tick said ---------------------------------------------------------------
    void read() {
        const uint32_t me = realm_->hero().id;
        for (const sim::Happening& h : link_->happenings()) {
            switch (h.what) {
                case sim::What::Died:
                    if (h.who == me) {
                        ++deaths_;
                        const sim::Body* by = realm_->find(h.whom);
                        const int breed = by && by->kind >= 0 ? by->kind : -1;
                        if (breed >= 0) feared_[breed] += 1;
                        say("** dies to %s", breed >= 0 ? tables_->kinds[size_t(breed)].label.c_str() : "?");
                    } else if (h.whom == me) {
                        ++kills_;
                    }
                    break;
                case sim::What::Refused:
                    if (h.who == me && std::getenv("NETBOT_DEBUG")) say("no way to %d,%d", h.a, h.b);
                    break;
                case sim::What::Levelled:
                    if (h.who == me) say("** level %d", h.a);
                    break;
                case sim::What::Bought:
                    if (h.audience == me) {
                        ++bought_;
                        lastPrice_ = h.b;
                    }
                    break;
                case sim::What::Answered:
                    // A purchase refused: the purse or the bag is done.
                    if (h.audience == me && h.a == int(sim::Command::Kind::Buy) && h.b < 0) shopDone_ = true;
                    break;
                default: break;
            }
        }
    }

    // ---- the brain ------------------------------------------------------------------------
    const content::ItemRow& rowOf(const sim::Held& held) const { return tables_->items[size_t(held.item)]; }

    int countOf(bool (*kind)(const content::ItemRow&)) const {
        int n = 0;
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_->satchel()[slot];
            if (!one.empty() && kind(rowOf(one))) n += std::max<int>(1, one.durability);
        }
        return n;
    }
    int slotOf(bool (*kind)(const content::ItemRow&)) const {
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_->satchel()[slot];
            if (!one.empty() && kind(rowOf(one))) return slot;
        }
        return -1;
    }
    int freeCells() const {
        int n = 0;
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) n += realm_->satchel()[slot].empty() ? 1 : 0;
        return n;
    }
    bool wizard() const { return kin_ == sim::Kin::DarkWizard; }
    bool safeHere() const {
        const sim::Body& me = realm_->hero();
        return tables_->grid.safe(me.column(), me.row());
    }

    // The strongest breed he will take on: a third under his level and three over at the start,
    // less one for every time that breed has killed him.
    int reachFor(int breed) const {
        const int level = realm_->hero().level;
        int most = level * 2 / 3 + 3;
        const auto it = feared_.find(breed);
        if (it != feared_.end()) most -= 2 * it->second;
        return most;
    }
    bool takes(const sim::Body& b) const {
        if (!b.monster() || !b.alive() || b.gone || b.kind < 0) return false;
        const auto it = banned_.find(b.id);
        if (it != banned_.end() && it->second > ticks_) return false;
        if (tables_->grid.safe(b.column(), b.row())) return false;
        return tables_->kinds[size_t(b.kind)].level <= reachFor(b.kind);
    }

    void spendPoints() {
        const sim::Body& me = realm_->hero();
        int points = me.pointsInHand;
        if (points <= 0) return;
        // Strength, agility, vitality, energy: the knight's arm, the wizard's mind.
        const int dk[4] = {5, 2, 3, 0}, dw[4] = {1, 2, 2, 5};
        const int* w = wizard() ? dw : dk;
        for (int k = 0; k < std::min(points, 10); ++k) {
            // The stat furthest under its share of what he has spent.
            const int had[4] = {me.points.strength, me.points.agility, me.points.vitality, me.points.energy};
            const int sum = had[0] + had[1] + had[2] + had[3] + 1;
            int pick = 0;
            double worst = 1e9;
            for (int i = 0; i < 4; ++i) {
                if (w[i] == 0) continue;
                const double share = double(had[i]) / sum / (w[i] / 10.0);
                if (share < worst) {
                    worst = share;
                    pick = i;
                }
            }
            sim::Command one;
            one.kind = sim::Command::Kind::Spend;
            one.a = pick;
            send(one);
            points -= 1;
            if (points == 0) break;
            // One a think: the next is weighed on what the tick made of the last.
            break;
        }
    }

    bool drink() {
        const sim::Body& me = realm_->hero();
        if (ticks_ < nextSip_) return false;
        int slot = -1;
        if (me.health * 2 < me.maxHealth) slot = slotOf(sim::heals);
        if (slot < 0 && wizard() && me.mana * 3 < me.maxMana) slot = slotOf(sim::restores);
        if (slot < 0) return false;
        sim::Command one;
        one.kind = sim::Command::Kind::Use;
        one.a = slot;
        send(one);
        nextSip_ = ticks_ + 20;
        return true;
    }

    // Zen, potions and jewels within reach that are his or anyone's.
    const sim::Lying* loot() const {
        const sim::Body& me = realm_->hero();
        const sim::Lying* best = nullptr;
        float nearest = kLootReach;
        for (const sim::Lying& l : realm_->lying()) {
            if (l.owner != 0 && l.owner != me.id) continue;
            const auto it = banned_.find(l.id);
            if (it != banned_.end() && it->second > ticks_) continue;
            if (!l.what.empty()) {
                const content::ItemRow& row = rowOf(l.what);
                const bool wanted = sim::heals(row) || sim::restores(row) || row.group == 14;
                if (!wanted || freeCells() < 2) continue;
            }
            const float d = std::hypot(float(l.column) - me.x, float(l.row) - me.y);
            if (d < nearest) {
                nearest = d;
                best = &l;
            }
        }
        return best;
    }

    // Leaves a target or a thing on the ground he has chased too long.
    bool chased(uint32_t id) {
        if (id != chasing_) {
            chasing_ = id;
            chasedSince_ = ticks_;
            return false;
        }
        if (ticks_ - chasedSince_ > kGiveUp) {
            banned_[id] = ticks_ + kForget;
            chasing_ = 0;
            return true;
        }
        return false;
    }

    void think() {
        const sim::Body& me = realm_->hero();
        if (!me.alive()) {
            mode_ = Mode::Hunt;
            return;
        }
        if (me.level != lastLevel_) lastLevel_ = me.level;
        spendPoints();
        if (drink()) return;

        const int potions = countOf(sim::heals);
        if (mode_ == Mode::Hunt) {
            if (potions == 0 && me.health * 10 < me.maxHealth * 4) {
                mode_ = Mode::Rest;
                say("low and out of potions: back to town");
            } else if (potions < 3 && potionFolk_ >= 0 && realm_->money() >= 300 && freeCells() > 4) {
                mode_ = Mode::Shop;
                shopDone_ = false;
                shopSince_ = ticks_;
                bought_ = 0;
                say("to %s for potions (%lld zen)", tables_->folk[size_t(potionFolk_)].name.c_str(),
                    (long long)realm_->money());
            }
        }
        switch (mode_) {
            case Mode::Rest: rest(); break;
            case Mode::Shop: shop(); break;
            case Mode::Hunt: hunt(); break;
        }
    }

    void rest() {
        const sim::Body& me = realm_->hero();
        if (!safeHere()) {
            if (!me.walking) walkTo(restAt_[0], restAt_[1]);
            return;
        }
        if (me.health * 10 >= me.maxHealth * 9) {
            mode_ = Mode::Hunt;
            if (potionFolk_ >= 0 && countOf(sim::heals) < 3 && realm_->money() >= 300) {
                mode_ = Mode::Shop;
                shopDone_ = false;
                shopSince_ = ticks_;
                bought_ = 0;
            }
        }
    }

    void shop() {
        const bool there = realm_->trading() == potionFolk_;
        if (ticks_ - shopSince_ > 120 * 20) {
            say("could not reach %s", tables_->folk[size_t(potionFolk_)].name.c_str());
            mode_ = Mode::Hunt;
            return;
        }
        if (!there) {
            order(sim::Request::Kind::Talk, 0, 0, uint32_t(potionFolk_));
            return;
        }
        // Health to half the count, a wizard's mana to half, then health to the full count.
        const int heals = countOf(sim::heals), restores = countOf(sim::restores);
        bool (*want)(const content::ItemRow&) = nullptr;
        if (!shopDone_ && freeCells() > 2 && realm_->money() >= std::max<int64_t>(lastPrice_, 20)) {
            if (heals < kPotionsWanted / 2) want = sim::heals;
            else if (wizard() && restores < kPotionsWanted / 2) want = sim::restores;
            else if (heals < kPotionsWanted) want = sim::heals;
        }
        if (want) {
            const int npc = tables_->folk[size_t(potionFolk_)].number;
            int count = 0;
            const sim::Offer* shelf = sim::stockOf(npc, &count);
            // The smallest of its kind: the one a low level's purse runs to.
            for (int k = 0; k < count; ++k) {
                const int item = tables_->itemAt(shelf[k].group, shelf[k].number);
                if (item < 0 || !want(tables_->items[size_t(item)])) continue;
                sim::Command one;
                one.kind = sim::Command::Kind::Buy;
                one.a = shelf[k].slot;
                one.ticket = ++ticket_;
                send(one);
                return;
            }
        }
        sim::Command close;
        close.kind = sim::Command::Kind::Close;
        close.a = int(sim::Command::Window::Trade);
        send(close);
        say("bought %d, has %d healing%s, %lld zen left", bought_, heals,
            wizard() ? (" and " + std::to_string(restores) + " mana potions").c_str() : " potions",
            (long long)realm_->money());
        mode_ = Mode::Hunt;
    }

    void hunt() {
        const sim::Body& me = realm_->hero();
        if (const sim::Lying* l = loot()) {
            if (!chased(l->id)) order(sim::Request::Kind::Pick, l->column, l->row, l->id);
            return;
        }
        // The nearest he takes within sight, the one he is on first.
        const sim::Body* target = nullptr;
        float nearest = kSight;
        for (const sim::Body& b : realm_->bodies()) {
            if (!takes(b)) continue;
            float d = std::hypot(b.x - me.x, b.y - me.y);
            if (b.id == chasing_) d -= 2.0f;
            if (d < nearest) {
                nearest = d;
                target = &b;
            }
        }
        if (target) {
            if (chased(target->id)) return;
            const int skill = wizard() && realm_->knows(sim::skill::kEnergyBall) ? sim::skill::kEnergyBall : 0;
            order(sim::Request::Kind::Attack, target->column(), target->row(), target->id, skill);
            return;
        }
        // Nothing near: toward the nearest he takes anywhere on the map -- a nest's box is
        // Lorencia-wide for its weakest breeds, and its middle was where he already stood.
        if (me.walking && ticks_ < roamUntil_) return;
        const sim::Body* far = nullptr;
        float farthest = 1e9f;
        for (const sim::Body& b : realm_->bodies()) {
            if (!takes(b)) continue;
            const float d = std::hypot(b.x - me.x, b.y - me.y);
            if (d < farthest) {
                farthest = d;
                far = &b;
            }
        }
        int column = 0, row = 0;
        if (far) {
            column = far->column();
            row = far->row();
        } else {
            // Everything he takes is dead: a tile in a nest of his breeds, to wait for them.
            std::vector<const content::MonsterNest*> nests;
            for (const content::MonsterNest& n : tables_->nests) {
                if (tables_->kinds[n.kind].level <= reachFor(int(n.kind))) nests.push_back(&n);
            }
            if (nests.empty()) return;
            const content::MonsterNest* n = nests[size_t(std::rand()) % nests.size()];
            column = n->x1 + std::rand() % std::max(1, n->x2 - n->x1 + 1);
            row = n->y1 + std::rand() % std::max(1, n->y2 - n->y1 + 1);
        }
        if (std::getenv("NETBOT_DEBUG")) say("roams to %d,%d%s", column, row, far ? "" : " (a nest)");
        walkTo(column, row);
        roamUntil_ = ticks_ + 10 * 20;
    }

    std::string name_;
    sim::Kin kin_;
    std::string host_;
    int port_;
    std::string dir_;
    uint64_t token_ = 0;
    std::string world_ = "lorencia";

    std::unique_ptr<content::Tables> tables_;
    std::unique_ptr<sim::Realm> realm_;
    std::unique_ptr<game::RemoteLink> link_;
    int64_t ticks_ = 0;
    bool divergenceSaid_ = false;

    Mode mode_ = Mode::Hunt;
    int potionFolk_ = -1;
    int restAt_[2] = {0, 0};
    uint32_t chasing_ = 0;
    int64_t chasedSince_ = 0;
    std::map<uint32_t, int64_t> banned_;
    std::map<int, int> feared_;
    int64_t nextSip_ = 0;
    int64_t roamUntil_ = 0;
    int64_t shopSince_ = 0;
    bool shopDone_ = false;
    int bought_ = 0;
    int64_t lastPrice_ = 0;
    uint32_t ticket_ = 0;
    int kills_ = 0, deaths_ = 0;
    int lastLevel_ = 0;
};

}  // namespace

int main(int argc, char** argv) {
    std::string host = "37.27.158.226";
    int port = net::kDefaultPort;
    std::string dir = std::string(MU2_ASSET_DIR) + "/../saves/bots";
    double hours = 0.0;  // 0: until stopped
    std::vector<std::pair<std::string, sim::Kin>> wanted;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--server" && i + 1 < argc) host = argv[++i];
        else if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (a == "--dir" && i + 1 < argc) dir = argv[++i];
        else if (a == "--hours" && i + 1 < argc) hours = std::atof(argv[++i]);
        else if (a.find(':') != std::string::npos) {
            const std::string name = a.substr(0, a.find(':')), cls = a.substr(a.find(':') + 1);
            if (cls == "dk") wanted.emplace_back(name, sim::Kin::DarkKnight);
            else if (cls == "dw") wanted.emplace_back(name, sim::Kin::DarkWizard);
            else {
                std::fprintf(stderr, "netbot: %s: the class is dk or dw\n", a.c_str());
                return 2;
            }
        } else {
            std::fprintf(stderr,
                         "usage: netbot [--server HOST] [--port N] [--dir DIR] [--hours H] NAME:dk|dw ...\n");
            return 2;
        }
    }
    if (wanted.empty()) wanted = {{"Ragnar", sim::Kin::DarkKnight}, {"Elora", sim::Kin::DarkWizard}};
    // The realm's own log is every blow; the bots say what matters themselves.
    if (!std::getenv("BOT_LOG")) core::logSilence(true);
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    std::srand(unsigned(std::time(nullptr)));

    std::vector<std::unique_ptr<NetBot>> bots;
    for (const auto& [name, kin] : wanted) {
        bots.push_back(std::make_unique<NetBot>(name, kin, host, port, dir));
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
            // A line gone -- the server restarted, or the realm sent him through a gate: he comes
            // back with his token, and the server says where he is.
            if (!bots[i]->update()) {
                up[i] = false;
                retryAt[i] = now + std::chrono::seconds(2);
            }
        }
        if (now >= reportAt) {
            for (const auto& bot : bots) bot->report();
            reportAt = now + std::chrono::minutes(1);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    for (const auto& bot : bots) bot->report();
    return 0;
}
