// **Botwatch** -- a window that shows a netbot run as it plays: each bot's level, Zen, map,
// health and what he is doing, the Broker's trades, the economy log, how fast the simulation
// goes and how many cores the server and the bots take (the user, 2026-10-12: "we need to build
// little app, where we can see how bots are playing the game, how many cores are used and how
// fast simulation is going", then "app has to be here", "c++").
//
//   build/botwatch                               a window; Start runs a private server and bots
//   build/botwatch --run DIR --port 44531        watch a run that is already going
//   build/botwatch --bots 30 --speed 100 --start start one at once
//   build/botwatch --bots 100 --per-netbot 25 --waves 60 --start
//                                                four netbots of 25, a minute apart
//   build/botwatch --run DIR --shot out.png      a picture of it after five seconds, and close
//
// A run botwatch starts is its own: a fresh store in saves/botwatch/<when>, the server and the
// netbot niced, and both stopped by Stop, by --minutes, or when the window closes
// ([[stop-test-servers]]). It reads netbot.log, server.log and economy.log in the run's
// directory, and `ps`; nothing else. ImGui on bgfx, as the studio's panels (gfx/gui.h).
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <csignal>
#include <ctime>

#include <GLFW/glfw3.h>
#include <bgfx/bgfx.h>

#include "gfx/gui.h"
#include "gfx/views.h"
#include "gfx/window.h"
#include "content/tables.h"
#include "imgui.h"
#include "sim/broker.h"
#include "sim/items.h"
#include "watch.h"

namespace {

using namespace botwatch;

// Killed rather than closed (a TERM, a ^C, the terminal gone): the loop ends and the App's
// destructor stops the run as a closed window does. Before, the server and the netbots outlived it.
volatile std::sig_atomic_t g_quit = 0;
void onSignal(int) { g_quit = 1; }

std::string clockText(int s) {
    char text[32];
    std::snprintf(text, sizeof text, "%d:%02d", s / 3600, (s / 60) % 60);
    return text;
}

std::string zenText(int64_t z) {
    char text[32];
    if (z >= 1000000) std::snprintf(text, sizeof text, "%.1fM", double(z) / 1e6);
    else if (z >= 1000) std::snprintf(text, sizeof text, "%lldk", (long long)(z / 1000));
    else std::snprintf(text, sizeof text, "%lld", (long long)z);
    return text;
}

// Game seconds a wall second, between the last sample and the one `back` seconds before it.
constexpr int kMostSpeed = 50;  // the fastest botwatch runs a world (Settings::speed)

double speedOf(const std::vector<Sample>& s, double back = 30.0) {
    if (s.size() < 2) return 0.0;
    const Sample& now = s.back();
    const Sample* old = &s.front();
    for (const Sample& one : s) {
        if (now.wall - one.wall <= back) {
            old = &one;
            break;
        }
    }
    const double wall = now.wall - old->wall;
    return wall > 0.0 ? (now.game - old->game) / wall : 0.0;
}

const ImVec4 kDim(0.55f, 0.57f, 0.61f, 1.0f);
const ImVec4 kGold(0.88f, 0.69f, 0.29f, 1.0f);
const ImVec4 kRed(0.88f, 0.42f, 0.37f, 1.0f);
const ImVec4 kGreen(0.42f, 0.77f, 0.54f, 1.0f);

// Each class its colour in the bots table (the user, 2026-10-12: "use color coding for classes").
ImVec4 kinColour(const std::string& kin) {
    if (kin == "Dark Knight" || kin == "Blade Knight") return ImVec4(0.90f, 0.45f, 0.38f, 1.0f);  // red
    if (kin == "Dark Wizard" || kin == "Soul Master") return ImVec4(0.45f, 0.62f, 0.95f, 1.0f);   // blue
    if (kin == "Fairy Elf" || kin == "Muse Elf") return ImVec4(0.45f, 0.82f, 0.50f, 1.0f);        // green
    if (kin == "Magic Gladiator") return ImVec4(0.78f, 0.52f, 0.92f, 1.0f);  // purple
    return kDim;
}

// **A Broker item's card** (the user, 2026-10-13: "we need to implemnet simple tooltip on Broker so i
// can see how good is items"): what the game's card leads with -- its damage or defence at +0, its
// drop level, what it asks to wear (sim::asks, the plus and excellence counted), the options off
// its link, and what a merchant pays. The rows are Lorencia's cooked tables, read once.
const mu::content::Tables* itemTables() {
    static std::unique_ptr<mu::content::Tables> tables;
    static bool tried = false;
    if (!tried) {
        tried = true;
        auto t = std::make_unique<mu::content::Tables>();
        std::string error;
        const std::string path = std::string(MU2_SOURCE_DIR) + "/out/assets/cooked/lorencia/lorencia.mur";
        if (mu::content::loadTables(path, *t, error)) {
            tables = std::move(t);
            std::fprintf(stderr, "botwatch: item tables from %s, %zu items\n", path.c_str(), tables->items.size());
        } else {
            std::fprintf(stderr, "botwatch: no item tables from %s: %s\n", path.c_str(), error.c_str());
        }
    }
    return tables.get();
}
// A rune's card lines (the user, 2026-10-13: "on tooltips also show what type of rune it is not only
// name"): its rarity, what takes it, who sets it, and what it does.
void runeLines(const sim::PowerRow& p, const char* lead) {
    const ImVec4 tone = p.rarity == sim::Rarity::Legendary ? ImVec4(1.0f, 0.5f, 0.0f, 1)
                        : p.rarity == sim::Rarity::Epic    ? ImVec4(0.64f, 0.21f, 0.93f, 1)
                        : p.rarity == sim::Rarity::Rare    ? ImVec4(0.0f, 0.55f, 1.0f, 1)
                                                           : ImVec4(0.85f, 0.85f, 0.85f, 1);
    std::string where;
    const auto add = [&](uint8_t bit, const char* word) {
        if (p.slots & bit) where += (where.empty() ? "" : ", ") + std::string(word);
    };
    add(sim::kInWeapon, "weapon");
    add(sim::kInShield, "shield");
    add(sim::kInArmour, "armour");
    add(sim::kInJewellery, "jewellery");
    std::string who;
    const char* kins[] = {"DW", "Elf", "DK", "MG"};  // sim::Kin order
    for (int k = 0; k < 4; ++k) {
        if (p.classes & sim::classBit(sim::Kin(k))) who += (who.empty() ? "" : "/") + std::string(kins[k]);
    }
    ImGui::TextColored(tone, "%s%s rune: %s", lead, sim::rarityName(p.rarity), p.name);
    ImGui::TextDisabled("  in %s · for %s%s%s", where.c_str(), who.c_str(), p.second ? " · 2nd class only" : "",
                        p.once ? " · one per item" : "");
    if (p.tells && *p.tells) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 420.0f);
        ImGui::TextWrapped("  %s", p.tells);
        ImGui::PopTextWrapPos();
    }
}
void itemTip(const std::string& name, const sim::Held& h) {
    const mu::content::Tables* tables = itemTables();
    if (!ImGui::BeginItemTooltip()) return;
    ImGui::TextColored(h.excellent ? ImVec4(0.1f, 1.0f, 0.5f, 1.0f) : ImVec4(1, 1, 1, 1), "%s%s",
                       h.excellent ? "Excellent " : "", name.c_str());
    if (tables && h.item >= 0 && size_t(h.item) < tables->items.size()) {
        const mu::content::ItemRow& row = tables->items[size_t(h.item)];
        ImGui::TextDisabled("drop level %d", row.dropLevel);
        if (row.maximumDamage > 0) ImGui::Text("Damage %d - %d at +0, speed %d", row.minimumDamage, row.maximumDamage, row.attackSpeed);
        if (row.magicPower > 0) ImGui::Text("Wizardry +%d%% at +0", row.magicPower);
        if (row.defense > 0) ImGui::Text("Defense %d at +0", row.defense);
        if (row.defenseRate > 0) ImGui::Text("Block rate %d", row.defenseRate);
        const sim::Needs n = sim::asks(row, h.refinement, h.excellent != 0);
        std::string needs;
        const auto add = [&](const char* what, int v) {
            if (v > 0) needs += (needs.empty() ? "" : " · ") + std::string(what) + " " + std::to_string(v);
        };
        add("level", n.level);
        add("strength", n.strength);
        add("agility", n.agility);
        add("energy", n.energy);
        add("vitality", n.vitality);
        if (!needs.empty()) ImGui::Text("Needs %s", needs.c_str());
        if (!row.tells.empty()) ImGui::TextWrapped("%s", row.tells.c_str());
        if (!row.teachesName.empty()) ImGui::Text("Teaches %s (level %d, energy %d)", row.teachesName.c_str(), row.teachesLevel, row.teachesEnergy);
        const std::string details = detailsOf(h);
        if (!details.empty()) ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "%s", details.c_str());
        if (sim::creation(row)) {
            if (const sim::PowerRow* p = sim::powerOf(h.powers[0])) runeLines(*p, "");
        } else {
            for (int k = 0; k < std::min<int>(h.sockets, 3); ++k) {
                if (const sim::PowerRow* p = h.powers[k] ? sim::powerOf(h.powers[k]) : nullptr) runeLines(*p, "Socket: ");
                else ImGui::TextDisabled("Socket: empty");
            }
        }
        ImGui::TextDisabled("a merchant pays %lld Zen", (long long)(sim::npcValue(*tables, h) / 3));
    } else {
        const std::string details = detailsOf(h);
        if (!details.empty()) ImGui::Text("%s", details.c_str());
    }
    ImGui::EndTooltip();
}

// An item's name in the game's colour for it (game/ui/describe.cpp qualityOf, tip.cpp colourOf;
// the user, 2026-10-12: "show color coding for items in broker how its in game"), from what its
// link carries: a Rune of Creation its rune's rarity, a powered ring or pendant its count of
// powers, excellent green, the events' tickets and parts, jewels and +7 and over gold.
ImVec4 itemColour(const std::string& name, const sim::Held& h) {
    const ImVec4 white(1, 1, 1, 1), blue(0.0f, 0.44f, 0.87f, 1), purple(0.64f, 0.21f, 0.93f, 1),
        legendary(0.12f, 1.0f, 0.0f, 1), excellent(0.1f, 1.0f, 0.5f, 1), gold(1.0f, 0.82f, 0.0f, 1),
        orange(1.0f, 0.5f, 0.0f, 1);
    const auto starts = [&](const char* p) { return name.rfind(p, 0) == 0; };
    if (starts("Rune of Creation")) {
        const sim::PowerRow* power = sim::powerOf(h.powers[0]);
        if (!power || power->rarity == sim::Rarity::Legendary) return orange;
        return power->rarity == sim::Rarity::Epic ? purple : power->rarity == sim::Rarity::Common ? white : blue;
    }
    int affixes = 0;
    for (const auto a : h.affixes) affixes += a != 0;
    if (affixes > 0) {
        const ImVec4 byCount[4] = {white, blue, purple, legendary};
        const int at = std::clamp(affixes, 1, 4) - 1;
        return h.excellent != 0 && at < 2 ? purple : byCount[at];
    }
    if (h.excellent != 0) return excellent;
    if (starts("Firecracker")) return blue;
    if (starts("Scroll of Archangel") || starts("Blood Bone") || starts("Invisibility Cloak") || starts("Devil's")) {
        return gold;
    }
    if (starts("Jewel of") || h.refinement >= 7) return gold;
    return white;
}

// Blood Castle or Devil Square, off his map as the log names it ("bloodcastle", "Blood Castle"),
// or null (the user, 2026-10-13: "if char is inside bc or ds show on list").
const char* eventOf(const std::string& map) {
    std::string m;
    for (const char c : map) m += char(std::tolower(uint8_t(c)));
    if (m.find("castle") != std::string::npos) return "Blood Castle";
    if (m.find("square") != std::string::npos) return "Devil Square";
    return nullptr;
}
const ImVec4 kEvent(1.0f, 0.36f, 0.30f, 1.0f);
// **Excellent in green** (the user, 2026-10-13: "sho excelent buys/sells in green on broker"): the
// whole row of an excellent offer or sale tinted, and its name in excellent green whatever its
// sockets say.
const ImVec4 kExcellent(0.1f, 1.0f, 0.5f, 1.0f);
// "[exc] " before an excellent item's name (the user, 2026-10-13: "add tag [exc] for exelent items").
std::string excTagged(const std::string& name, bool excellent) { return excellent ? "[exc] " + name : name; }
void excellentRow(const sim::Held& h) {
    if (h.excellent != 0) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImVec4(0.1f, 0.8f, 0.4f, 0.22f)));
}
// **Wings stand out** (the user, 2026-10-13: "highlight wings on broker ... so we can easy spot
// that somebody sell or buys wings"): the row tinted amber, the name in amber with a "[wings]" tag.
const ImVec4 kWings(1.0f, 0.62f, 0.15f, 1.0f);
bool wingsNamed(const std::string& name) { return name.rfind("Wings of ", 0) == 0; }
void wingsRow(const std::string& name) {
    if (wingsNamed(name)) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImVec4(1.0f, 0.55f, 0.1f, 0.28f)));
}

void tile(const char* label, const std::string& value, const std::string& under, ImVec4 colour = ImVec4(1, 1, 1, 1)) {
    ImGui::BeginGroup();
    ImGui::TextColored(kDim, "%s", label);
    ImGui::PushStyleColor(ImGuiCol_Text, colour);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.8f);
    ImGui::TextUnformatted(value.c_str());
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::TextColored(kDim, "%s", under.c_str());
    ImGui::EndGroup();
}

struct Settings {
    // 50: the fastest 70 bots keep up with (2026-10-13: 70 at 70x fell behind, 4 errands a bot a
    // game hour lost; at 50x 0.1). One thread shares its mirrors (netbot Shared).
    int bots = 30, speed = 50, threads = 1, port = 44540;
    int perNetbot = 10, waveSeconds = 30;  // bots a netbot process, and the wait between them
    float minutes = 30.0f;
};

class App {
public:
    App(std::string root, std::string run, int port, Settings settings)
        : root_(std::move(root)), settings_(settings) {
        if (!run.empty()) watch_ = std::make_unique<Watch>(run, port);
    }
    ~App() {
        quit_ = true;
        if (poller_.joinable()) poller_.join();
        if (watch_) watch_->stopRun();
    }

    void startPolling() {
        poller_ = std::thread([this] {
            while (!quit_) {
                if (Watch* w = current()) {
                    w->poll();
                    auto fresh = std::make_shared<const Snapshot>(w->snapshot());
                    const Snapshot& s = *fresh;
                    std::lock_guard<std::mutex> hold(shown_);
                    snap_ = std::move(fresh);
                    fresh_ = true;
                    glfwPostEmptyEvent();  // the window waits for input or this
                    // --minutes: a run of its own is stopped when its time is up.
                    if (ownRun_ && s.running && deadline_ > 0.0 && s.wall > deadline_) {
                        status_ = "time is up: the run is stopped";
                        std::thread([w] { w->stopRun(); }).detach();
                        deadline_ = 0.0;
                    }
                }
                // Twice a second (the user, 2026-10-13: "update bot app more frekvently so we have live
                // feel"): it was every two.
                for (int i = 0; i < 5 && !quit_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    }

    // A fresh run of its own: a new store under saves/botwatch, the server and the bots started.
    void start() {
        if (starting_) return;
        char stamp[32];
        const std::time_t now = std::time(nullptr);
        std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", std::localtime(&now));
        const std::string run = root_ + "/out/saves/botwatch/" + stamp;
        {
            std::lock_guard<std::mutex> hold(shown_);
            if (watch_) old_.push_back(std::move(watch_));  // its poll may still be reading it
            watch_ = std::make_unique<Watch>(run, settings_.port);
            snap_ = std::make_shared<const Snapshot>();
            status_ = "starting " + std::to_string(settings_.bots) + " bots at speed " + std::to_string(settings_.speed);
            ownRun_ = true;
            starting_ = true;
        }
        Watch* w = watch_.get();
        const Settings s = settings_;
        const std::string server = root_ + "/build/mu2_server", netbot = root_ + "/build/netbot";
        std::thread([this, w, s, server, netbot] {
            std::string error;
            const bool ok = w->launch(server, netbot, s.bots, s.speed, s.threads, s.perNetbot, s.waveSeconds, &error);
            std::lock_guard<std::mutex> hold(shown_);
            status_ = ok ? "running in " + w->run() : error;
            if (ok && s.minutes > 0.0f) deadline_ = w->snapshot().wall + s.minutes * 60.0;
            starting_ = false;
        }).detach();
    }

    void stop() {
        if (Watch* w = current()) {
            std::thread([w] { w->stopRun(); }).detach();
            std::lock_guard<std::mutex> hold(shown_);
            status_ = "stopping: the bots first, then the server";
        }
    }

    // True once after the poll handed over a new snapshot: what woke the window was data,
    // not the user.
    bool takeFresh() { return fresh_.exchange(false); }

    void draw(int width, int height) {
        std::shared_ptr<const Snapshot> held;
        std::string status;
        bool starting = false;
        {
            std::lock_guard<std::mutex> hold(shown_);
            held = snap_;
            status = status_;
            starting = starting_;
        }
        const Snapshot& s = *held;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(float(width), float(height)));
        ImGui::Begin("botwatch", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

        controls(s, status, starting);
        ImGui::Separator();
        tiles(s);
        ImGui::Separator();
        charts(s);
        const float rest = ImGui::GetContentRegionAvail().y;
        if (ImGui::BeginTable("lower", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV,
                              ImVec2(0, rest))) {
            ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthStretch, 0.62f);
            ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthStretch, 0.38f);
            ImGui::TableNextColumn();
            botsTable(s, rest);
            ImGui::TableNextColumn();
            side(s, rest);
            ImGui::EndTable();
        }
        ImGui::End();
    }

    void openTab(const std::string& name) { openTab_ = name; }
    void choose(const std::string& name) {
        chosen_ = name;
        showSheet_ = true;
    }

private:
    Watch* current() {
        std::lock_guard<std::mutex> hold(shown_);
        return watch_.get();
    }

    void controls(const Snapshot& s, const std::string& status, bool starting) {
        ImGui::TextColored(kGold, "BOTWATCH");
        ImGui::SameLine();
        if (watch_) ImGui::TextColored(kDim, "%s  ·  port %d", watch_->run().c_str(), watch_->port());
        else ImGui::TextColored(kDim, "no run: set the numbers and Start");

        const bool live = s.running || starting;
        ImGui::BeginDisabled(live);
        ImGui::SetNextItemWidth(110);
        ImGui::InputInt("bots", &settings_.bots, 5, 10);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        ImGui::InputInt("speed", &settings_.speed, 10, 50);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        ImGui::InputFloat("minutes", &settings_.minutes, 5.0f, 15.0f, "%.0f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        ImGui::InputInt("threads", &settings_.threads, 1, 1);
        ImGui::SetItemTooltip("think threads in each netbot process, up to four");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::InputInt("a netbot", &settings_.perNetbot, 5, 25);
        ImGui::SetItemTooltip("bots in each netbot process; more bots, more processes");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::InputInt("s apart", &settings_.waveSeconds, 10, 60);
        ImGui::SetItemTooltip("real seconds between one netbot's bots joining and the next's");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        ImGui::InputInt("port", &settings_.port, 1, 10);
        settings_.bots = std::clamp(settings_.bots, 1, 200);
        // 50 at most (the user, 2026-10-13: "we dont need sim speed faster tht 50x").
        settings_.speed = std::clamp(settings_.speed, 1, kMostSpeed);
        settings_.threads = std::clamp(settings_.threads, 1, 4);
        settings_.minutes = std::max(0.0f, settings_.minutes);
        settings_.perNetbot = std::clamp(settings_.perNetbot, 1, 200);
        settings_.waveSeconds = std::clamp(settings_.waveSeconds, 0, 3600);
        ImGui::SameLine();
        if (ImGui::Button("  Start  ")) start();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!live);
        if (ImGui::Button("  Stop  ")) stop();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (s.running) ImGui::TextColored(kGreen, "running, %d netbot%s", s.netbots, s.netbots == 1 ? "" : "s");
        else ImGui::TextColored(kDim, "stopped");
        if (!status.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(kDim, "  %s", status.c_str());
        }
    }

    void tiles(const Snapshot& s) {
        const double speed = speedOf(s.samples);
        int game = 0;
        int64_t zen = 0;
        int deaths = 0;
        std::vector<int> levels;
        for (const Bot& b : s.bots) {
            game = std::max(game, b.clock);
            zen += b.zen;
            deaths += b.deaths;
            levels.push_back(b.level);
        }
        std::sort(levels.begin(), levels.end());
        double runCores = 0.0;
        for (const Process& p : s.processes) runCores += p.cores;
        const int cores = int(std::thread::hardware_concurrency());
        char a[64], b[96];
        int wings1 = 0, wings2 = 0, dinorants = 0, excellent = 0, excellentPieces = 0;
        for (const Bot& one : s.bots) {
            wings1 += one.wings == 1;
            wings2 += one.wings == 2;
            dinorants += one.dinorant;
            excellent += one.excellent > 0;
            excellentPieces += one.excellent;
        }
        if (ImGui::BeginTable("tiles", 10, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn();
            int asked = 0;
            for (const Process& p : s.processes) asked = std::max(asked, p.speed);
            std::snprintf(a, sizeof a, "%.0fx", speed);
            if (asked > 0) std::snprintf(b, sizeof b, "asked %dx: %.0f%% of it", asked, 100.0 * speed / asked);
            else std::snprintf(b, sizeof b, "game seconds a real second");
            tile("Simulation speed", speed > 0 ? a : "-", b, kGold);
            ImGui::TableNextColumn();
            std::snprintf(b, sizeof b, "after %.0f real min", s.wall / 60.0);
            tile("Game time", clockText(game), b);
            ImGui::TableNextColumn();
            std::snprintf(a, sizeof a, "%.1f", runCores);
            std::string parts;
            for (const Process& p : s.processes) {
                char one[48];
                std::snprintf(one, sizeof one, "%s%s %.1f", parts.empty() ? "" : " · ", p.what.c_str(), p.cores);
                parts += one;
            }
            tile("Cores, this run", a, parts.empty() ? "no process on the port" : parts);
            ImGui::TableNextColumn();
            std::snprintf(a, sizeof a, "%.1f / %d", s.machineCores, cores);
            std::snprintf(b, sizeof b, "load %.1f %.1f %.1f", s.load[0], s.load[1], s.load[2]);
            tile("Cores, whole Mac", a, b, s.machineCores > cores * 0.8 ? kRed : ImVec4(1, 1, 1, 1));
            ImGui::TableNextColumn();
            int castle = 0, square = 0;
            for (const Bot& one : s.bots) {
                if (const char* event = eventOf(one.map)) (*event == 'B' ? castle : square) += 1;
            }
            if (castle + square > 0) {
                std::snprintf(b, sizeof b, "median level %d · %d in BC, %d in DS",
                              levels.empty() ? 0 : levels[levels.size() / 2], castle, square);
            } else {
                std::snprintf(b, sizeof b, "median level %d", levels.empty() ? 0 : levels[levels.size() / 2]);
            }
            tile("Bots", std::to_string(s.bots.size()), b, castle + square > 0 ? kEvent : ImVec4(1, 1, 1, 1));
            ImGui::TableNextColumn();
            tile("Zen held", zenText(zen), std::to_string(deaths) + " deaths");
            ImGui::TableNextColumn();
            std::snprintf(b, sizeof b, "%d listed · %s spent · %d empty visits", s.listed, zenText(s.spent).c_str(),
                          s.empty);
            tile("Broker sales", std::to_string(s.bought), b, kGreen);
            ImGui::TableNextColumn();
            // Wings and mounts, and excellent gear worn, off the sheets (the user, 2026-10-13).
            std::snprintf(a, sizeof a, "%d · %d", wings1, wings2);
            std::snprintf(b, sizeof b, "1st · 2nd wings · %d ride a Dinorant", dinorants);
            tile("Wings", a, b, kGold);
            ImGui::TableNextColumn();
            std::snprintf(b, sizeof b, "bots wearing it · %d pieces", excellentPieces);
            tile("Excellent worn", std::to_string(excellent), b, kExcellent);
            ImGui::TableNextColumn();
            std::snprintf(b, sizeof b, "the server's last minute, worst %.0f ms", s.worstSlowMs);
            tile("Server slow passes", std::to_string(s.slowLastMinute), b,
                 s.slowLastMinute > 30 ? kRed : ImVec4(1, 1, 1, 1));
            ImGui::EndTable();
        }
    }

    void charts(const Snapshot& s) {
        // Speed: each sample against the one forty before (about 20 s at two a second), the last
        // six minutes.
        std::vector<float> speed, run, machine;
        const size_t from = s.samples.size() > 720 ? s.samples.size() - 720 : 0;
        for (size_t i = from; i < s.samples.size(); ++i) {
            run.push_back(float(s.samples[i].runCores));
            machine.push_back(float(s.samples[i].machineCores));
            if (i >= 40) {
                const double dt = s.samples[i].wall - s.samples[i - 40].wall;
                speed.push_back(dt > 0 ? float(std::max(0.0, (s.samples[i].game - s.samples[i - 40].game) / dt)) : 0.f);
            }
        }
        const float w = (ImGui::GetContentRegionAvail().x - 16) / 3.0f;
        auto top = [](const std::vector<float>& v) {
            float m = 1.0f;
            for (float x : v) m = std::max(m, x);
            return m * 1.15f;
        };
        ImGui::PushStyleColor(ImGuiCol_PlotLines, kGold);
        ImGui::PlotLines("##speed", speed.data(), int(speed.size()), 0, "speed, last 6 min", 0.0f, top(speed),
                         ImVec2(w, 90));
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.42f, 0.66f, 0.91f, 1.0f));
        ImGui::PlotLines("##run", run.data(), int(run.size()), 0, "cores, this run", 0.0f, top(run), ImVec2(w, 90));
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PlotLines("##machine", machine.data(), int(machine.size()), 0, "cores, whole Mac", 0.0f,
                         float(std::thread::hardware_concurrency()), ImVec2(w, 90));
    }

    void botsTable(const Snapshot& s, float height) {
        std::vector<Bot> bots = s.bots;
        const ImGuiTableFlags flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                      ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable;
        if (!ImGui::BeginTable("bots", 11, flags, ImVec2(0, height - 4))) return;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Class", ImGuiTableColumnFlags_WidthFixed, 110);
        // His guild, its master in gold (the user, 2026-10-13: "show guild row in bots list").
        ImGui::TableSetupColumn("Guild", ImGuiTableColumnFlags_WidthFixed, 75);
        ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort |
                                             ImGuiTableColumnFlags_PreferSortDescending, 50);
        // Sorted by a click on its header, highest first (the user, 2026-10-13: "allow to order bot
        // list by gear score").
        ImGui::TableSetupColumn("Gear score", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 75);
        ImGui::TableSetupColumn("Zen", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 60);
        ImGui::TableSetupColumn("Map", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Health", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 80);
        ImGui::TableSetupColumn("Kills", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 55);
        ImGui::TableSetupColumn("Deaths", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 55);
        ImGui::TableSetupColumn("Doing", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort);
        ImGui::TableHeadersRow();
        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsCount > 0) {
            const int column = specs->Specs[0].ColumnIndex;
            const bool down = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
            std::stable_sort(bots.begin(), bots.end(), [&](const Bot& a, const Bot& b) {
                int c = 0;
                switch (column) {
                    case 0: c = a.name.compare(b.name); break;
                    case 1: c = a.kin.compare(b.kin); break;
                    case 2: c = a.guild != b.guild ? a.guild.compare(b.guild) : int(b.master) - int(a.master); break;
                    case 3: c = a.level - b.level; break;
                    case 4: c = a.score - b.score; break;
                    case 5: c = a.zen < b.zen ? -1 : a.zen > b.zen; break;
                    case 6: c = a.map.compare(b.map); break;
                    case 8: c = a.kills - b.kills; break;
                    case 9: c = a.deaths - b.deaths; break;
                    default: break;
                }
                return down ? c > 0 : c < 0;
            });
        }
        for (const Bot& b : bots) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            // A click opens his sheet in the Character tab (the user, 2026-10-12).
            const ImVec4 colour = kinColour(b.kin);
            ImGui::PushStyleColor(ImGuiCol_Text, colour);
            if (ImGui::Selectable(b.name.c_str(), b.name == chosen_, ImGuiSelectableFlags_SpanAllColumns)) {
                chosen_ = b.name;
                sheetAt_ = 0.0;
                showSheet_ = true;
            }
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            ImGui::TextColored(colour, "%s", b.kin.c_str());
            ImGui::TableNextColumn();
            if (b.guild.empty()) ImGui::TextColored(kDim, "-");
            else if (b.master) ImGui::TextColored(kGold, "%s *", b.guild.c_str());
            else ImGui::TextUnformatted(b.guild.c_str());
            if (b.master && ImGui::IsItemHovered()) ImGui::SetTooltip("the guild's master");
            ImGui::TableNextColumn();
            ImGui::Text("%d", b.level);
            ImGui::TableNextColumn();
            if (b.score > 0) ImGui::TextColored(kGold, "%d", b.score);
            else ImGui::TextColored(kDim, "-");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(zenText(b.zen).c_str());
            ImGui::TableNextColumn();
            // Which castle or square (the user, 2026-10-13: "show in which BC,DS bot is inside").
            if (const char* event = eventOf(b.map)) {
                if (b.eventTier > 0) ImGui::TextColored(kEvent, "%s %d", event, b.eventTier);
                else ImGui::TextColored(kEvent, "%s", event);
            }
            else ImGui::TextUnformatted(b.map.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(b.hp.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%d", b.kills);
            ImGui::TableNextColumn();
            if (b.deaths > 0) ImGui::TextColored(kRed, "%d", b.deaths);
            else ImGui::TextColored(kDim, "0");
            ImGui::TableNextColumn();
            ImGui::TextColored(kDim, "%s", b.doing.c_str());
        }
        ImGui::EndTable();
    }

    void side(const Snapshot& s, float height) {
        if (!ImGui::BeginTabBar("side")) return;
        const float inner = height - ImGui::GetFrameHeightWithSpacing() - 8;
        if (!chosen_.empty() &&
            ImGui::BeginTabItem("Character", nullptr, showSheet_ ? ImGuiTabItemFlags_SetSelected : 0)) {
            showSheet_ = false;
            character(inner);
            ImGui::EndTabItem();
        }
        const auto open = [&](const char* name) {
            const bool now = openTab_ == name;
            if (now) openTab_.clear();
            return now ? ImGuiTabItemFlags_SetSelected : 0;
        };
        if (ImGui::BeginTabItem("What happened", nullptr, open("What happened"))) {
            ImGui::BeginChild("feed", ImVec2(0, inner));
            for (const Line& l : s.feed) {
                ImGui::TextColored(kDim, "%s", clockText(l.at).c_str());
                ImGui::SameLine();
                ImGui::TextColored(kGold, "%s", l.who.c_str());
                ImGui::SameLine();
                ImGui::TextWrapped("%s", l.what.c_str());
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Broker", nullptr, open("Broker"))) {
            // **What is on sale** (the user, 2026-10-12: "allow to click on broker items so i can
            // see what they sell"): a seller or an item clicked here or in the sales below shows
            // what is up of his, or of it.
            const float lineH = ImGui::GetTextLineHeightWithSpacing();
            std::vector<const Offer*> shown;
            for (const Offer& o : s.offers) {
                if ((offerSeller_.empty() || o.seller == offerSeller_) && (offerItem_.empty() || o.item == offerItem_)) {
                    shown.push_back(&o);
                }
            }
            if (!offerSeller_.empty() || !offerItem_.empty()) {
                ImGui::TextColored(kGold, "%s%s%s", offerSeller_.c_str(), !offerSeller_.empty() && !offerItem_.empty() ? " · " : "",
                                   offerItem_.c_str());
                ImGui::SameLine();
                ImGui::TextColored(kDim, "%zu up", shown.size());
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear")) offerSeller_.clear(), offerItem_.clear();
            } else {
                ImGui::TextColored(kDim, "On sale: %zu listings. Click a seller or an item.", shown.size());
            }
            if (ImGui::BeginTable("offers", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                                  ImVec2(0, inner * 0.42f - lineH))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Since", ImGuiTableColumnFlags_WidthFixed, 45);
                ImGui::TableSetupColumn("Seller", ImGuiTableColumnFlags_WidthFixed, 70);
                ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch, 1.2f);
                ImGui::TableSetupColumn("Price", ImGuiTableColumnFlags_WidthFixed, 70);
                ImGui::TableHeadersRow();
                int n = 0;
                for (const Offer* o : shown) {
                    ImGui::PushID(n++);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(kDim, "%s", clockText(o->at).c_str());
                    ImGui::TableNextColumn();
                    if (ImGui::Selectable(o->seller.c_str(), o->seller == offerSeller_)) offerSeller_ = o->seller, offerItem_.clear();
                    ImGui::TableNextColumn();
                    excellentRow(o->held);
                    wingsRow(o->item);
                    ImGui::PushStyleColor(ImGuiCol_Text, wingsNamed(o->item) ? kWings : o->held.excellent ? kExcellent : itemColour(o->item, o->held));
                    if (ImGui::Selectable(((wingsNamed(o->item) ? "[wings] " : "") + excTagged(o->item, o->held.excellent != 0)).c_str(), o->item == offerItem_)) offerItem_ = o->item, offerSeller_.clear();
                    ImGui::PopStyleColor();
                    itemTip(o->item, o->held);
                    const std::string details = detailsOf(o->held);
                    ImGui::TableNextColumn();
                    ImGui::TextColored(kDim, "%s", details.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%lld", (long long)o->price);
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::TextColored(kDim, "Sales");
            if (ImGui::BeginTable("trades", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                                  ImVec2(0, inner * 0.42f - lineH))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("When", ImGuiTableColumnFlags_WidthFixed, 45);
                ImGui::TableSetupColumn("Buyer", ImGuiTableColumnFlags_WidthFixed, 70);
                ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch, 1.2f);
                ImGui::TableSetupColumn("Seller", ImGuiTableColumnFlags_WidthFixed, 70);
                ImGui::TableSetupColumn("Price", ImGuiTableColumnFlags_WidthFixed, 70);
                ImGui::TableHeadersRow();
                int n = 0;
                for (const Trade& t : s.trades) {
                    ImGui::PushID(n++);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(kDim, "%s", clockText(t.at).c_str());
                    ImGui::TableNextColumn();
                    if (ImGui::Selectable(t.buyer.c_str(), false)) offerSeller_ = t.buyer, offerItem_.clear();
                    ImGui::TableNextColumn();
                    excellentRow(t.held);
                    wingsRow(t.item);
                    ImGui::PushStyleColor(ImGuiCol_Text, wingsNamed(t.item) ? kWings : t.held.excellent ? kExcellent : itemColour(t.item, t.held));
                    if (ImGui::Selectable(((wingsNamed(t.item) ? "[wings] " : "") + excTagged(t.item, t.held.excellent != 0)).c_str(), false)) offerItem_ = t.item, offerSeller_.clear();
                    ImGui::PopStyleColor();
                    itemTip(t.item, t.held);
                    const std::string details = detailsOf(t.held);
                    ImGui::TableNextColumn();
                    ImGui::TextColored(kDim, "%s", details.c_str());
                    ImGui::TableNextColumn();
                    if (ImGui::Selectable(t.seller.c_str(), false)) offerSeller_ = t.seller, offerItem_.clear();
                    ImGui::PopID();
                    ImGui::TableNextColumn();
                    ImGui::Text("%lld", (long long)t.price);
                }
                ImGui::EndTable();
            }
            ImGui::TextColored(kDim, "%d listed  ·  %d sold for %s  ·  %d visits, %d found nothing", s.listed, s.bought,
                               zenText(s.spent).c_str(), s.visits, s.empty);
            ImGui::EndTabItem();
        }
        // **Events** (the user, 2026-10-13: "add new tab near Broken named Events, in events tab we
        // show latest events where bots participated and how many"): each Blood Castle and Devil
        // Square run the bots went into, newest first, who went and how each came out.
        if (ImGui::BeginTabItem("Events", nullptr, open("Events"))) {
            int castles = 0, won = 0, squares = 0, firsts = 0, castleBots = 0, squareBots = 0;
            for (const EventRun& e : s.events) {
                if (e.castle) castles += 1, won += e.won, castleBots += int(e.bots.size());
                else squares += 1, firsts += e.won, squareBots += int(e.bots.size());
            }
            ImGui::TextColored(kDim, "Blood Castle: %d runs, %d won, %d bots went  ·  Devil Square: %d runs, %d won (rank 1), %d bots went",
                               castles, won, castleBots, squares, firsts, squareBots);
            if (ImGui::BeginTable("events", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                                  ImVec2(0, 0))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("When", ImGuiTableColumnFlags_WidthFixed, 55);
                ImGui::TableSetupColumn("Event", ImGuiTableColumnFlags_WidthFixed, 110);
                ImGui::TableSetupColumn("Bots", ImGuiTableColumnFlags_WidthFixed, 40);
                ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthFixed, 110);
                ImGui::TableSetupColumn("Who, and how", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                int n = 0;
                for (const EventRun& e : s.events) {
                    ImGui::PushID(n++);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(kDim, "%s", clockText(e.at).c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextColored(kEvent, "%s %d", e.castle ? "Blood Castle" : "Devil Square", e.tier);
                    ImGui::TableNextColumn();
                    ImGui::Text("%zu", e.bots.size());
                    ImGui::TableNextColumn();
                    if (e.result.empty()) ImGui::TextColored(kDim, "under way");
                    else if (e.won) ImGui::TextColored(kGreen, "%s", e.castle ? "won" : "a bot first");
                    else ImGui::TextColored(kRed, "%s", e.castle ? "time ran out" : "no bot first");
                    ImGui::TableNextColumn();
                    std::string who;
                    for (const std::string& b : e.bots) {
                        const auto r = e.result.find(b);
                        who += (who.empty() ? "" : "  ·  ") + b + (r != e.result.end() ? " (" + r->second + ")" : "");
                    }
                    ImGui::TextWrapped("%s", who.c_str());
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Economy", nullptr, open("Economy"))) {
            ImGui::TextWrapped("%s", s.economy.empty()
                                         ? "Nothing yet: the server writes a block each real hour and when it stops."
                                         : s.economy.c_str());
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Processes")) {
            if (ImGui::BeginTable("procs", 4, ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Process");
                ImGui::TableSetupColumn("PID");
                ImGui::TableSetupColumn("Cores");
                ImGui::TableSetupColumn("Memory");
                ImGui::TableHeadersRow();
                for (const Process& p : s.processes) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(p.what.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", p.pid);
                    ImGui::TableNextColumn();
                    ImGui::Text("%.2f", p.cores);
                    ImGui::TableNextColumn();
                    ImGui::Text("%d MB", p.memoryMb);
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    // **The Character tab**: the chosen bot's sheet (bots/NAME.sheet, Bot::sheet), read again
    // twice a second -- netbot writes it every second.
    void character(float height) {
        const double now = ImGui::GetTime();
        if (now - sheetAt_ > 0.5 && watch_) {
            sheetAt_ = now;
            sheet_.clear();
            std::ifstream in(watch_->run() + "/bots/" + chosen_ + ".sheet");
            for (std::string line; std::getline(in, line);) {
                const size_t a = line.find('\t'), b = a == std::string::npos ? a : line.find('\t', a + 1);
                if (b == std::string::npos) continue;
                // A fourth column, the thing's link, for its card (Bot::sheet).
                std::string value = line.substr(b + 1), link;
                if (const size_t c = value.find('\t'); c != std::string::npos) link = value.substr(c + 1), value.resize(c);
                sheet_.push_back({line.substr(0, a), line.substr(a + 1, b - a - 1), value, link});
            }
        }
        ImGui::BeginChild("sheet", ImVec2(0, height));
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.4f);
        ImGui::TextColored(kGold, "%s", chosen_.c_str());
        ImGui::PopFont();
        if (sheet_.empty()) {
            ImGui::TextColored(kDim, "No sheet yet: netbot writes one every second while he plays.");
            ImGui::EndChild();
            return;
        }
        static const char* kSections[][2] = {{"who", ""}, {"stats", "Stats"}, {"gear", "Gear"},
                                              {"skills", "Skills"}, {"runes", "Runes carried"}, {"quests", "Quests"},
                                              {"bag", "Bag"}};
        for (const auto& [key, title] : kSections) {
            bool any = false;
            for (const auto& r : sheet_) any = any || r[0] == key;
            if (!any) continue;
            if (*title) {
                ImGui::Spacing();
                ImGui::TextColored(kDim, "%s", title);
                ImGui::Separator();
            }
            if (ImGui::BeginTable(key, 2, ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 150);
                ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
                for (const auto& r : sheet_) {
                    if (r[0] != key) continue;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(kDim, "%s", r[1].c_str());
                    ImGui::TableNextColumn();
                    const bool excellent = r[0] == "gear" && r[2].find(" excellent") != std::string::npos;
                    const bool rare = excellent || r[2].find('[') != std::string::npos;
                    if (excellent) ImGui::TextColored(kExcellent, "%s", excTagged(r[2], true).c_str());
                    else if (rare) ImGui::TextColored(kGreen, "%s", r[2].c_str());
                    else ImGui::TextWrapped("%s", r[2].c_str());
                    // Its card, off the link the sheet carries (the user, 2026-10-13).
                    if (!r[3].empty()) {
                        sim::Held held;
                        const std::string label = itemOf(r[3], &held);
                        itemTip(label, held);
                    }
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndChild();
    }

    std::string chosen_;
    std::string offerSeller_, offerItem_;  // the Broker tab's filter on what is up
    std::string openTab_;                  // --tab: the side tab to open on
    bool showSheet_ = false;
    double sheetAt_ = 0.0;
    std::vector<std::array<std::string, 4>> sheet_;

    std::string root_;
    Settings settings_;
    std::unique_ptr<Watch> watch_;
    std::vector<std::unique_ptr<Watch>> old_;
    std::thread poller_;
    std::atomic<bool> quit_{false};
    std::mutex shown_;
    std::shared_ptr<const Snapshot> snap_ = std::make_shared<const Snapshot>();  // shared, not copied a frame
    std::atomic<bool> fresh_{false};
    std::string status_;
    bool ownRun_ = false, starting_ = false;
    double deadline_ = 0.0;
};

void style() {
    ImGuiStyle& st = ImGui::GetStyle();
    ImGui::StyleColorsDark(&st);
    st.WindowPadding = ImVec2(14, 12);
    st.FrameRounding = 4.0f;
    st.CellPadding = ImVec2(6, 4);
    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.067f, 0.075f, 0.09f, 1.0f);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.11f, 0.14f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.55f, 0.42f, 0.16f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.70f, 0.54f, 0.21f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.80f, 0.62f, 0.25f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.16f, 0.18f, 0.22f, 1.0f);
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.10f, 0.11f, 0.14f, 1.0f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.025f);
    c[ImGuiCol_Tab] = ImVec4(0.10f, 0.11f, 0.14f, 1.0f);
    c[ImGuiCol_TabSelected] = ImVec4(0.20f, 0.22f, 0.27f, 1.0f);
    c[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.28f, 0.34f, 1.0f);
}

}  // namespace

int main(int argc, char** argv) {
    std::string root = MU2_SOURCE_DIR, run;
    int port = 44540;
    Settings settings;
    bool startNow = false;
    std::string shot, show;  // --show NAME: his sheet open from the start
    std::string tab;         // --tab NAME: a side tab open from the start ("Broker", "Economy")
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--run" && i + 1 < argc) run = argv[++i];
        else if (a == "--port" && i + 1 < argc) port = settings.port = std::atoi(argv[++i]);
        else if (a == "--bots" && i + 1 < argc) settings.bots = std::atoi(argv[++i]);
        else if (a == "--speed" && i + 1 < argc) settings.speed = std::clamp(std::atoi(argv[++i]), 1, kMostSpeed);
        else if (a == "--minutes" && i + 1 < argc) settings.minutes = float(std::atof(argv[++i]));
        else if (a == "--threads" && i + 1 < argc) settings.threads = std::atoi(argv[++i]);
        else if (a == "--per-netbot" && i + 1 < argc) settings.perNetbot = std::atoi(argv[++i]);
        else if (a == "--waves" && i + 1 < argc) settings.waveSeconds = std::atoi(argv[++i]);
        else if (a == "--start") startNow = true;
        else if (a == "--shot" && i + 1 < argc) shot = argv[++i];
        else if (a == "--show" && i + 1 < argc) show = argv[++i];
        else if (a == "--tab" && i + 1 < argc) tab = argv[++i];
        else {
            std::fprintf(stderr, "usage: botwatch [--run DIR] [--port N] [--bots N] [--speed N] [--minutes M] "
                                 "[--threads N] [--per-netbot N] [--waves S] [--start] [--show NAME] [--tab NAME] [--shot PNG]\n");
            return 2;
        }
    }

    mu::gfx::Window window;
    mu::gfx::WindowDesc desc;
    desc.width = 1600;
    desc.height = 1000;
    desc.vsync = true;
    desc.title = "Botwatch";
    if (!window.open(desc)) return 1;
    mu::gfx::Gui gui;
    if (!gui.open(window, root + "/third_party/extern/OpenSans-SemiBold.ttf", 15.0f)) return 1;
    style();

    std::signal(SIGTERM, onSignal);
    std::signal(SIGINT, onSignal);
    std::signal(SIGHUP, onSignal);
    {
        g_linkTables = itemTables();  // the Broker's item cards, read once up front (and said in the log)
        App app(root, run, port, settings);
        app.startPolling();
        if (startNow) app.start();
        if (!show.empty()) app.choose(show);
        if (!tab.empty()) app.openTab(tab);
        auto last = Clock::now();
        const auto opened = last;
        int shotFrames = -1;  // frames left after the shot was asked for
        // **Drawn when something changed** (the user, 2026-10-12: "optimise bot app so we can
        // use more cpu and gpu"): drawing every vsync took two cores off the bots it watches, most
        // of it Metal presenting the same table. The window now waits for input, at full rate only
        // for half a second after it; otherwise once a new snapshot comes (every two seconds) and
        // twice a second for the clock.
        double activeUntil = 0.0;
        for (;;) {
            const double waitFrom = glfwGetTime();
            const bool busy = waitFrom < activeUntil || !shot.empty();
            if (!busy) glfwWaitEventsTimeout(0.5);
            if (!window.pump() || window.escapePressed() || g_quit) break;
            if (!busy && glfwGetTime() - waitFrom < 0.49 && !app.takeFresh()) activeUntil = glfwGetTime() + 0.5;
            if (!shot.empty() && shotFrames < 0 && Clock::now() - opened > std::chrono::seconds(5)) {
                bgfx::requestScreenShot(BGFX_INVALID_HANDLE, shot.c_str());
                shotFrames = 3;
            } else if (shotFrames > 0 && --shotFrames == 0) {
                break;
            }
            const auto now = Clock::now();
            const float dt = std::chrono::duration<float>(now - last).count();
            last = now;
            bgfx::setViewRect(0, 0, 0, uint16_t(window.width()), uint16_t(window.height()));
            bgfx::setViewClear(0, BGFX_CLEAR_COLOR, 0x111317ff);
            bgfx::touch(0);
            gui.begin(window, dt);
            app.draw(window.width(), window.height());
            gui.end();
            bgfx::frame();
        }
    }  // the app's run stopped before the window goes
    gui.close();
    window.close();
    return 0;
}
