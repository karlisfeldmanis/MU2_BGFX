#include "game/helper.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>

#include "content/tables.h"
#include "core/log.h"
#include "game/play.h"
#include "game/ui/describe.h"
#include "game/ui/tracker.h"
#include "sim/items.h"
#include "sim/quests.h"
#include "sim/realm.h"
#include "sim/realm_tuning.h"
#include "sim/rules.h"
#include "sim/skills.h"
#include "sim/wear.h"

namespace mu::game {
namespace {

// How often the hand looks again, and how long a walk is left to work before it is asked again.
constexpr double kThink = 0.25;
constexpr double kReask = 2.5;
// As near as a monster must be before he is set on it, in tiles; farther he is walked to it, which
// runs (quest_auto.cpp's kSetOn, and why).
constexpr float kSetOn = 6.0f;
// A skill asked and not thrown (cooling, no mana, out of reach) is not asked again before this.
constexpr double kSkillRetry = 1.0;
constexpr double kAuraRetry = 4.0;
// Around the one he fights, the bodies that count toward "3+ near", in tiles.
constexpr float kCrowd = 3.0f;
// A thing walked to this long without going is left where it lies.
constexpr double kLootGiveUp = 6.0;
// He has wandered back when he is this near the spot, in tiles.
constexpr float kHome = 1.5f;
// A worn piece is mended at this share of its durability or under, in percent: MU Helper's
// DEFAULT_DURABILITY_THRESHOLD (MuHelper.cpp:24), and the slot's yellow band (sim::Worn::Half).
constexpr int kMendAt = 50;
// A repair asked and not answered yet (the server's round trip) is not asked again before this.
constexpr double kMendRetry = 3.0;
// His health under which a Heal set to "when hurt" is cast, when his auto-drink line is off.
constexpr int kHurtDefault = 50;

float tilesApart(float x0, float y0, float x1, float y1) { return std::max(std::fabs(x1 - x0), std::fabs(y1 - y0)); }

// A quest it may follow: taken, and every step one Auto-play can hunt (a thing to find is not).
bool followable(const sim::Realm& realm, int quest) {
    if (quest < 0 || quest >= sim::kQuests) return false;
    const sim::QuestState state = realm.quest(quest).state;
    if (state != sim::QuestState::Active && state != sim::QuestState::Ready) return false;
    const sim::QuestRow& row = sim::questAt(quest);
    for (int s = 0; s < row.stepCount; ++s) {
        if (row.steps[s].kind == sim::QuestStepKind::Find) return false;
    }
    return true;
}

// The quest the tracker shows when it may be followed, else the first taken one hunted on this
// map, else the first taken one; -1 for none. In the Dungeon with the Catacombs under way it
// plays the Catacombs, not Marlon's quest above it (the user, 2026-10-21: "if char is on dungeon
// and he has quest there auto-play quest folow has to work there").
int questToFollow(const sim::Realm& realm, int shown) {
    if (followable(realm, shown)) return shown;
    int first = -1;
    for (int q = 0; q < sim::kQuests; ++q) {
        if (!followable(realm, q)) continue;
        if (Tracker::here(realm, q)) return q;
        if (first < 0) first = q;
    }
    return first;
}

}  // namespace

HelperKind helperKind(const sim::SkillRow& row, bool rightSlot) {
    if (rightSlot) return HelperKind::Right;
    // Nova (a charge) is pressed once and bursts where he stands (Realm::invoke): a crowd's, not a
    // move; Fire Breath (mounted) is a blow at one body while he rides (the user, 2026-10-21: "i
    // draged ini new spell on quckslot but helper did not work with that").
    if (row.blinks) return HelperKind::Moves;
    if (row.channelled()) return HelperKind::Held;
    if (row.summons > 0) return HelperKind::Summon;
    if (row.mends) return HelperKind::Heal;
    if (row.boonTicks > 0 || row.mightTicks > 0) return HelperKind::Aura;
    if (row.spread != sim::Spread::One || row.splash > 0.0f || row.arrows > 1) return HelperKind::Area;
    return HelperKind::Single;
}

const std::vector<HelperUse>& helperUses(HelperKind kind) {
    static const std::vector<HelperUse> kRight{HelperUse::Always};
    static const std::vector<HelperUse> kSingle{HelperUse::Ready, HelperUse::Off};
    static const std::vector<HelperUse> kArea{HelperUse::Near3, HelperUse::Near2, HelperUse::Ready, HelperUse::Off};
    static const std::vector<HelperUse> kAura{HelperUse::KeepUp, HelperUse::Off};
    static const std::vector<HelperUse> kHeal{HelperUse::WhenHurt, HelperUse::Off};
    static const std::vector<HelperUse> kSummon{HelperUse::KeepOut, HelperUse::Off};
    static const std::vector<HelperUse> kNever{HelperUse::Never};
    switch (kind) {
        case HelperKind::Right: return kRight;
        case HelperKind::Single: return kSingle;
        case HelperKind::Area: return kArea;
        case HelperKind::Aura: return kAura;
        case HelperKind::Heal: return kHeal;
        case HelperKind::Summon: return kSummon;
        case HelperKind::Moves:
        case HelperKind::Held: return kNever;
    }
    return kNever;
}

const char* helperUseName(HelperUse use) {
    switch (use) {
        case HelperUse::Off: return "off";
        case HelperUse::Ready: return "when ready";
        case HelperUse::Near2: return "2+ near";
        case HelperUse::Near3: return "3+ near";
        case HelperUse::KeepUp: return "keep up";
        case HelperUse::WhenHurt: return "when hurt";
        case HelperUse::KeepOut: return "keep one out";
        case HelperUse::Always: return "always";
        case HelperUse::Never: return "not used";
    }
    return "";
}

const char* helperKindName(HelperKind kind) {
    switch (kind) {
        case HelperKind::Right: return "right click \xC2\xB7 every target";
        case HelperKind::Single: return "one target";
        case HelperKind::Area: return "hits a crowd";
        case HelperKind::Aura: return "buff";
        case HelperKind::Heal: return "heals";
        case HelperKind::Summon: return "summon";
        case HelperKind::Moves: return "moves him \xC2\xB7 never used";
        case HelperKind::Held: return "held key \xC2\xB7 never used";
    }
    return "";
}

HelperUse HelperSettings::use(int32_t skill, HelperKind kind) const {
    const std::vector<HelperUse>& offered = helperUses(kind);
    if (const auto found = uses.find(skill); found != uses.end()) {
        if (std::find(offered.begin(), offered.end(), found->second) != offered.end()) return found->second;
    }
    return offered.front();
}

namespace {
std::string lowered(std::string text) {
    for (char& c : text) c = char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
}  // namespace

bool HelperSettings::named(const std::string& label) const {
    if (names.empty()) return false;
    const std::string in = lowered(label);
    return std::any_of(names.begin(), names.end(),
                       [&](const std::string& name) { return in.find(lowered(name)) != std::string::npos; });
}

bool HelperSettings::addName(const std::string& typed) {
    size_t from = 0, to = typed.size();
    while (from < to && std::isspace(static_cast<unsigned char>(typed[from]))) ++from;
    while (to > from && std::isspace(static_cast<unsigned char>(typed[to - 1]))) --to;
    std::string name;
    for (size_t i = from; i < to && name.size() < kNameLong; ++i) {
        if (static_cast<unsigned char>(typed[i]) >= 32) name += typed[i];
    }
    if (name.empty() || names.size() >= kNameMost) return false;
    const std::string low = lowered(name);
    if (std::any_of(names.begin(), names.end(), [&](const std::string& one) { return lowered(one) == low; })) {
        return false;
    }
    names.push_back(name);
    return true;
}

// ---- kept beside the save --------------------------------------------------------------------

std::string helperPath(const std::string& savePath) {
    return std::filesystem::path(savePath).replace_extension(".helper").string();
}

bool loadHelper(const std::string& savePath, HelperSettings& out) {
    if (savePath.empty()) return false;
    const std::string path = helperPath(savePath);
    FILE* f = std::fopen(path.c_str(), "r");
    if (f == nullptr) return false;
    HelperSettings read;
    read.uses.clear();
    char key[32];
    bool ok = true;
    while (ok && std::fscanf(f, "%31s", key) == 1) {
        int a = 0, b = 0;
        if (!std::strcmp(key, "range") && std::fscanf(f, "%d", &a) == 1) read.range = std::clamp(a, 0, 2);
        else if (!std::strcmp(key, "back") && std::fscanf(f, "%d", &a) == 1) read.back = a != 0;
        else if (!std::strcmp(key, "picks") && std::fscanf(f, "%d", &a) == 1) read.picks = uint8_t((a & 127) | (read.picks & HelperSettings::kPickBoxes));
        else if (!std::strcmp(key, "boxes") && std::fscanf(f, "%d", &a) == 1)
            read.picks = uint8_t(a != 0 ? read.picks | HelperSettings::kPickBoxes : read.picks & ~HelperSettings::kPickBoxes);
        else if (!std::strcmp(key, "seen") && std::fscanf(f, "%d", &a) == 1) read.seen = a != 0;
        else if (!std::strcmp(key, "quest") && std::fscanf(f, "%d", &a) == 1) read.quest = a != 0;
        else if (!std::strcmp(key, "invites") && std::fscanf(f, "%d", &a) == 1) read.invites = uint8_t(std::clamp(a, 0, 2));
        else if (!std::strcmp(key, "party") && std::fscanf(f, "%d", &a) == 1) read.aidParty = a != 0;
        else if (!std::strcmp(key, "trades") && std::fscanf(f, "%d", &a) == 1) read.noTrades = a != 0;
        else if (!std::strcmp(key, "repair") && std::fscanf(f, "%d", &a) == 1) read.repair = a != 0;
        else if (!std::strcmp(key, "name")) {
            char line[64] = {};
            if (std::fgets(line, sizeof line, f) == nullptr) ok = false;
            else read.addName(line);
        }
        else if (!std::strcmp(key, "use") && std::fscanf(f, "%d %d", &a, &b) == 2) {
            if (b >= 0 && b <= int(HelperUse::Never)) read.uses[a] = HelperUse(b);
        } else {
            ok = false;
        }
    }
    std::fclose(f);
    if (!ok) {
        core::logError("helper: %s does not read; its defaults instead", path.c_str());
        return false;
    }
    out = read;
    return true;
}

bool writeHelper(const std::string& savePath, const HelperSettings& settings) {
    if (savePath.empty()) return false;
    const std::string path = helperPath(savePath);
    FILE* f = std::fopen(path.c_str(), "w");
    if (f == nullptr) {
        core::logError("helper: cannot write %s", path.c_str());
        return false;
    }
    std::fprintf(f, "range %d\nback %d\npicks %d\nseen %d\n", settings.range, settings.back ? 1 : 0,
                 int(settings.picks & 127), settings.seen ? 1 : 0);
    std::fprintf(f, "boxes %d\n", (settings.picks & HelperSettings::kPickBoxes) != 0 ? 1 : 0);
    std::fprintf(f, "quest %d\ninvites %d\nparty %d\ntrades %d\n", settings.quest ? 1 : 0, int(settings.invites),
                 settings.aidParty ? 1 : 0, settings.noTrades ? 1 : 0);
    std::fprintf(f, "repair %d\n", settings.repair ? 1 : 0);
    for (const auto& [skill, use] : settings.uses) std::fprintf(f, "use %d %d\n", int(skill), int(use));
    for (const std::string& name : settings.names) std::fprintf(f, "name %s\n", name.c_str());
    std::fclose(f);
    return true;
}

// ---- the pickup log ----------------------------------------------------------------------------

namespace {

// An entry onto the log: Zen into the run of it it follows, so the timeline is the things with
// what came between them.
void noted(HelperLog& log, HelperLog::Entry entry) {
    if (entry.isZen()) {
        log.zen += entry.zen;
        if (!log.entries.empty() && log.entries.back().isZen()) {
            log.entries.back().zen += entry.zen;
            log.entries.back().at = entry.at;
            return;
        }
    }
    log.entries.push_back(std::move(entry));
}

// The rest of a line, from `at`: its leading spaces and its newline off.
std::string restOf(const char* at) {
    while (*at == ' ') ++at;
    std::string text = at;
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    return text;
}

// A stop's line as the log says how it ended: "Helper stopped: you died." is "You died".
std::string endedBy(std::string why) {
    constexpr const char* kPrefix = "Helper stopped: ";
    if (why.rfind(kPrefix, 0) == 0) why = why.substr(std::strlen(kPrefix));
    while (!why.empty() && why.back() == '.') why.pop_back();
    if (!why.empty()) why[0] = char(std::toupper(static_cast<unsigned char>(why[0])));
    return why;
}

}  // namespace

std::string helperLogPath(const std::string& savePath) {
    return std::filesystem::path(savePath).replace_extension(".helperlog").string();
}

// One line a fact: `began <time> <place>`, `place <place>` when a followed quest took it on,
// `item <time> <count> <tone> <label>`, `zen <time> <amount>`, and
// `end <time> <seconds> <kills> <why>` -- none when it was cut off.
HelperLog loadHelperLog(const std::string& savePath) {
    HelperLog log;
    if (savePath.empty()) return log;
    FILE* f = std::fopen(helperLogPath(savePath).c_str(), "r");
    if (f == nullptr) return log;
    char line[512];
    while (std::fgets(line, sizeof line, f) != nullptr) {
        long long at = 0, amount = 0;
        int count = 0, tone = 0, kills = 0, used = 0;
        double seconds = 0.0;
        if (std::sscanf(line, "began %lld%n", &at, &used) == 1) {
            log = {};
            log.began = at;
            log.place = restOf(line + used);
        } else if (std::strncmp(line, "place ", 6) == 0) {
            log.place = restOf(line + 6);
        } else if (std::sscanf(line, "item %lld %d %d%n", &at, &count, &tone, &used) == 3) {
            noted(log, {at, restOf(line + used), uint8_t(tone), count, 0});
        } else if (std::sscanf(line, "zen %lld %lld", &at, &amount) == 2) {
            noted(log, {at, {}, 0, 0, amount});
        } else if (std::sscanf(line, "end %lld %lf %d%n", &at, &seconds, &kills, &used) == 3) {
            log.ended = at;
            log.seconds = seconds;
            log.kills = kills;
            log.why = restOf(line + used);
        }
    }
    std::fclose(f);
    return log;
}

void Helper::keepLog(const std::string& savePath, const std::string& place) {
    logPath_ = savePath.empty() ? std::string() : helperLogPath(savePath);
    place_ = place;
    if (!on_) log_ = loadHelperLog(savePath);
}

void Helper::clearLog() {
    if (on_) return;
    log_ = {};
    if (!logPath_.empty()) {
        std::error_code ignored;
        std::filesystem::remove(logPath_, ignored);
    }
}

void Helper::logLine(const std::string& text, bool fresh) {
    if (logPath_.empty()) return;
    FILE* f = std::fopen(logPath_.c_str(), fresh ? "w" : "a");
    if (f == nullptr) {
        core::logError("helper: cannot write %s", logPath_.c_str());
        return;
    }
    std::fprintf(f, "%s\n", text.c_str());
    std::fclose(f);
}

void Helper::picked(const Play& play, const sim::Held& what, int64_t zen) {
    if (!on_ || play.realm().tables() == nullptr) return;
    const content::Tables& tables = *play.realm().tables();
    HelperLog::Entry entry;
    entry.at = int64_t(std::time(nullptr));
    char text[320];
    if (what.empty()) {
        if (zen <= 0) return;
        entry.zen = zen;
        std::snprintf(text, sizeof text, "zen %lld %lld", (long long)entry.at, (long long)zen);
    } else {
        if (what.item < 0 || size_t(what.item) >= tables.items.size()) return;
        const content::ItemRow& row = tables.items[size_t(what.item)];
        entry.label = sim::itemLabel(row, what);
        if (what.refinement > 0) entry.label += " +" + std::to_string(what.refinement);
        entry.tone = uint8_t(qualityOf(row, what));
        entry.count = sim::stacks(row) ? std::max<int>(1, what.durability) : 1;
        ++stats_.picked;
        std::snprintf(text, sizeof text, "item %lld %d %d %s", (long long)entry.at, entry.count, int(entry.tone),
                      entry.label.c_str());
    }
    noted(log_, std::move(entry));
    logLine(text);
}

// ---- the hand ----------------------------------------------------------------------------------

void Helper::start(const Play& play, const Stats* carried) {
    const sim::Body& hero = play.realm().hero();
    on_ = true;
    stats_ = carried ? *carried : Stats{};
    quester_.reset();
    following_ = -1;
    aidTried_.clear();
    mendTried_.clear();
    anchorColumn_ = hero.column();
    anchorRow_ = hero.row();
    since_ = kThink;
    askedAt_ = -1e9;
    walkedColumn_ = walkedRow_ = -1;
    target_ = picking_ = 0;
    tried_.clear();
    lootTried_.clear();
    potions_ = play.realm().autoDrinkSlot(false) >= 0;
    core::logf("helper: started at %d,%d", anchorColumn_, anchorRow_);
    // A followed quest's travel goes on with its log, on the new map; any other start begins one.
    if (carried != nullptr && log_.kept() && !log_.ending()) {
        log_.place = place_;
        logLine("place " + place_);
    } else {
        log_ = {};
        log_.began = int64_t(std::time(nullptr));
        log_.place = place_;
        logLine("began " + std::to_string(log_.began) + " " + place_, true);
    }
}

void Helper::stop(const std::string& why) {
    if (on_) {
        core::logf("helper: stopped after %.0f s, %d kills, %d picked up", stats_.seconds, stats_.kills, stats_.picked);
        log_.ended = int64_t(std::time(nullptr));
        log_.why = why;
        log_.seconds = stats_.seconds;
        log_.kills = stats_.kills;
        char text[256];
        std::snprintf(text, sizeof text, "end %lld %.0f %d %s", (long long)log_.ended, stats_.seconds, stats_.kills,
                      why.c_str());
        logLine(text);
    }
    on_ = false;
    target_ = picking_ = 0;
}

std::string Helper::halt(const std::string& why) {
    stop(endedBy(why));
    core::logf("helper: %s", why.c_str());
    return why;
}

std::string Helper::finished() {
    ++stats_.quests;
    const std::string title = sim::questAt(following_).title;
    following_ = -1;
    return halt("Helper stopped: " + title + " complete.");
}

std::string Helper::update(Play& play, const HelperSettings& settings, double seconds, const int32_t* bar,
                           int barCount, int shown) {
    if (!on_) return {};
    clock_ += seconds;
    stats_.seconds += seconds;
    barCount_ = bar ? std::min(barCount, 8) : 0;
    for (int i = 0; i < barCount_; ++i) bar_[i] = bar[i];
    if (settings.quest) return follow(play, settings, seconds, shown);
    if (following_ >= 0) {
        // Follow my quest ticked off while it helps: it hunts round where he stands now.
        quester_.reset();
        following_ = -1;
        anchorColumn_ = play.realm().hero().column();
        anchorRow_ = play.realm().hero().row();
    }
    since_ += seconds;
    if (since_ < kThink || !play.isOpen()) return {};
    since_ = 0.0;
    const sim::Realm& realm = play.realm();
    const content::Tables* tables = realm.tables();
    if (tables == nullptr) return {};
    const sim::Body& hero = realm.hero();

    // What came of the last frame's asking: the one he fought gone, the thing walked to taken.
    if (target_ != 0) {
        const sim::Body* was = realm.find(target_);
        if (was == nullptr || !was->alive() || was->gone) {
            ++stats_.kills;
            target_ = 0;
        }
    }
    // Taken, by him or not: what he took is counted as it comes (Helper::picked).
    if (picking_ != 0 && std::none_of(realm.lying().begin(), realm.lying().end(),
                                      [&](const sim::Lying& one) { return one.id == picking_; })) {
        picking_ = 0;
    }

    if (!hero.alive()) return halt("Helper stopped: you died.");
    if (tables->grid.safe(hero.column(), hero.row())) return halt("Helper stopped: you are in a safe zone.");
    if (realm.satchel().free(*tables, 1, 1) < 0) return halt("Helper stopped: the bag is full.");
    // Out of the potions he set out with: a young hero who carries none may still be helped.
    if (potions_ && realm.autoDrink().health > 0 && realm.autoDrinkSlot(false) < 0) {
        return halt("Helper stopped: no healing potions left.");
    }
    if (hero.archer != 0) {
        const int wanted = hero.archer == 1 ? 15 : 7;  // Arrows, the Bolt (quest_auto.cpp)
        int shots = 0;
        const sim::Satchel& bag = realm.satchel();
        for (int slot = sim::kWeaponLeft; slot < sim::kSlots; ++slot) {
            if (slot != sim::kWeaponLeft && slot < sim::kWorn) continue;
            const sim::Held& h = bag[slot];
            if (h.empty() || size_t(h.item) >= tables->items.size()) continue;
            const content::ItemRow& row = tables->items[size_t(h.item)];
            if (row.group == sim::kGroupBows && row.number == wanted) shots += std::max<int>(0, h.durability);
        }
        if (shots == 0) return halt(hero.archer == 1 ? "Helper stopped: out of arrows." : "Helper stopped: out of bolts.");
    }
    mend(play, settings);
    if (realm.held()) return {};

    // MU Helper's order (MuHelper.cpp's Work): his buffs, his heal, then what lies about, then
    // back to the spot, then the fight.
    const float reach = float(settings.tiles());
    const sim::Body* attacker = nullptr;
    const sim::Body* nearest = nullptr;
    float attackerAway = 1e9f, best = 1e9f;
    for (const sim::Body& body : realm.bodies()) {
        if (!body.monster() || !body.alive() || body.gone) continue;
        const float away = tilesApart(hero.x, hero.y, body.x, body.y);
        if (body.quarry == hero.id && away < attackerAway) {
            attackerAway = away;
            attacker = &body;
        }
        if (tilesApart(float(anchorColumn_), float(anchorRow_), body.x, body.y) > reach) continue;
        // An Elite (sim/elite.h) is left to the player: never sought, only fought back.
        if (body.elite) continue;
        if (away < best && realm.reaches(hero, hero.column(), hero.row(), body.column(), body.row())) {
            best = away;
            nearest = &body;
        }
    }
    const sim::Body* held = target_ ? realm.find(target_) : nullptr;
    if (held && (!held->alive() || held->gone)) held = nullptr;
    const sim::Body* fightNow = attacker ? attacker : held ? held : nearest;

    if (support(play, settings)) return {};
    if (attacker == nullptr && loot(play, settings)) return {};

    if (fightNow == nullptr) {
        target_ = 0;
        if (!settings.back) return {};
        if (tilesApart(hero.x, hero.y, float(anchorColumn_), float(anchorRow_)) <= kHome) return {};
        const bool asked = walkedColumn_ == anchorColumn_ && walkedRow_ == anchorRow_;
        if (asked && (hero.walking || clock_ - askedAt_ < kReask)) return {};
        walkedColumn_ = anchorColumn_;
        walkedRow_ = anchorRow_;
        askedAt_ = clock_;
        play.walkTo(anchorColumn_, anchorRow_);
        return {};
    }

    const sim::Request& order = realm.order();
    const bool onIt = order.kind == sim::Request::Kind::Attack && order.target == fightNow->id;
    const float away = std::hypot(fightNow->x - hero.x, fightNow->y - hero.y);
    target_ = fightNow->id;
    if (!onIt && away > kSetOn) {
        const int column = fightNow->column(), row = fightNow->row();
        const bool drifted = std::abs(column - walkedColumn_) >= 2 || std::abs(row - walkedRow_) >= 2;
        if (drifted || (!hero.walking && clock_ - askedAt_ > kReask)) {
            walkedColumn_ = column;
            walkedRow_ = row;
            askedAt_ = clock_;
            play.walkTo(column, row);
        }
        return {};
    }
    if (!onIt) play.fight(target_, striker(play, settings));
    keys(play, settings, target_);
    return {};
}

// His auras kept up, her summon kept out, her heal when she is hurt: each on himself, on a skill
// he knows, ready, paid for and suited to his hands, each asked again only after a few seconds.
bool Helper::support(Play& play, const HelperSettings& settings) {
    const sim::Realm& realm = play.realm();
    const sim::Body& hero = realm.hero();
    if (realm.casting()) return false;
    const uint32_t offHand = realm.wearer().offHand;
    const int hurtLine = realm.autoDrink().health > 0 ? realm.autoDrink().health : kHurtDefault;
    for (int i = 0; i < barCount_; ++i) {
        const int32_t skill = bar_[i];
        const sim::SkillRow* row = skill > 0 ? sim::skillNumbered(skill) : nullptr;
        if (row == nullptr || !realm.knows(skill) || realm.cooling(skill) > 0) continue;
        const HelperKind kind = helperKind(*row, i == 5);  // Hud::kRightSlot
        const HelperUse use = settings.use(skill, kind);
        bool wanted = false;
        if (use == HelperUse::KeepUp) {
            wanted = row->boonTicks > 0 ? hero.boonUntil <= realm.tick() : hero.mightUntil <= realm.tick();
        } else if (use == HelperUse::WhenHurt) {
            wanted = hero.maxHealth > 0 && hero.health * 100 < hurtLine * hero.maxHealth;
        } else if (use == HelperUse::KeepOut) {
            const sim::Body* summon = realm.summoned();
            wanted = summon == nullptr || !summon->alive() || summon->gone;
        }
        if (!wanted || hero.mana < sim::manaCostOf(hero, *row) || !row->suits(offHand)) continue;
        if (const auto tried = tried_.find(skill); tried != tried_.end() && tried->second > clock_) continue;
        tried_[skill] = clock_ + kAuraRetry;
        core::logf("helper: %s", row->name);
        play.castHero(skill, hero.id);
        return true;
    }
    return settings.aidParty && aid(play, settings);
}

// Her Heal on the most hurt of her party in reach, under the line her own Heal is set by, and
// then her Greater Defense and Greater Damage on each one whose has run out: the aids her bar
// holds (SkillRow::aids), set to their uses, cast at him as a click on him casts them
// (Play::aidTarget), within sim::kAidReach -- she is not walked to him.
bool Helper::aid(Play& play, const HelperSettings& settings) {
    const sim::Realm& realm = play.realm();
    const sim::Body& hero = realm.hero();
    const net::PartyState& party = play.party();
    if (party.members.empty()) return false;
    const uint32_t offHand = realm.wearer().offHand;
    const int hurtLine = realm.autoDrink().health > 0 ? realm.autoDrink().health : kHurtDefault;
    std::vector<const sim::Body*> near;
    for (const net::PartyState::Member& member : party.members) {
        if (member.here == 0 || member.here == hero.id) continue;
        const sim::Body* one = realm.find(member.here);
        if (one == nullptr || !one->player || one->gone || !one->alive()) continue;
        if (tilesApart(hero.x, hero.y, one->x, one->y) > sim::kAidReach) continue;
        near.push_back(one);
    }
    if (near.empty()) return false;
    const auto cast = [&](int32_t skill, const sim::SkillRow& row, const sim::Body& on) {
        aidTried_[(uint64_t(on.id) << 32) | uint32_t(skill)] = clock_ + kAuraRetry;
        core::logf("helper: %s on party member %u", row.name, unsigned(on.id));
        play.castHero(skill, on.id);
        return true;
    };
    const auto ready = [&](int32_t skill, const sim::SkillRow*& row, HelperUse wantedUse) {
        row = skill > 0 ? sim::skillNumbered(skill) : nullptr;
        if (row == nullptr || !row->aids() || !realm.knows(skill) || realm.cooling(skill) > 0) return false;
        if (hero.mana < sim::manaCostOf(hero, *row) || !row->suits(offHand)) return false;
        return settings.use(skill, helperKind(*row, false)) == wantedUse;
    };
    const auto tried = [&](int32_t skill, const sim::Body& on) {
        const auto found = aidTried_.find((uint64_t(on.id) << 32) | uint32_t(skill));
        return found != aidTried_.end() && found->second > clock_;
    };
    // The heal first, on the most hurt.
    for (int i = 0; i < barCount_; ++i) {
        const sim::SkillRow* row = nullptr;
        if (i == 5 || !ready(bar_[i], row, HelperUse::WhenHurt) || !row->mends) continue;
        const sim::Body* worst = nullptr;
        for (const sim::Body* one : near) {
            if (one->maxHealth <= 0 || one->health * 100 >= hurtLine * one->maxHealth || tried(bar_[i], *one)) continue;
            if (worst == nullptr || one->health * worst->maxHealth < worst->health * one->maxHealth) worst = one;
        }
        if (worst) return cast(bar_[i], *row, *worst);
    }
    // Then the buffs, on whoever's has run out.
    for (int i = 0; i < barCount_; ++i) {
        const sim::SkillRow* row = nullptr;
        if (i == 5 || !ready(bar_[i], row, HelperUse::KeepUp) || row->mends) continue;
        for (const sim::Body* one : near) {
            const bool down = row->mightTicks > 0 ? one->mightUntil <= realm.tick() : one->boonUntil <= realm.tick();
            if (down && !tried(bar_[i], *one)) return cast(bar_[i], *row, *one);
        }
    }
    return false;
}

// **Follow my quest**: Auto-play's hand on the quest it follows (game/quest_auto.h), with the
// Helper's own aids and pickups before it each beat. A quest handed in, it stops.
std::string Helper::follow(Play& play, const HelperSettings& settings, double seconds, int shown) {
    if (!play.isOpen() || play.realm().tables() == nullptr) return {};
    const sim::Realm& realm = play.realm();
    const content::Tables& tables = *realm.tables();
    const sim::Body& hero = realm.hero();
    since_ += seconds;
    if (since_ >= kThink) {
        since_ = 0.0;
        if (picking_ != 0 && std::none_of(realm.lying().begin(), realm.lying().end(),
                                          [&](const sim::Lying& one) { return one.id == picking_; })) {
            picking_ = 0;
        }
        if (realm.satchel().free(tables, 1, 1) < 0) return halt("Helper stopped: the bag is full.");
        if (potions_ && realm.autoDrink().health > 0 && realm.autoDrinkSlot(false) < 0) {
            return halt("Helper stopped: no healing potions left.");
        }
        // A quest handed in stops it: it does not go on to another taken one (the user,
        // 2026-10-23: "dont allow to continoy to do other quests if its finsihed").
        if (following_ >= 0 && !followable(realm, following_)) {
            if (realm.quest(following_).state == sim::QuestState::Resting) return finished();
            following_ = -1;
        }
        if (following_ < 0) {
            following_ = questToFollow(realm, shown);
            if (following_ < 0) {
                return halt(stats_.quests > 0 ? "Helper stopped: no taken quest is left to follow."
                                              : "Helper stopped: no taken quest to follow.");
            }
            core::logf("helper: following %s", sim::questAt(following_).title);
        }
        if (hero.alive()) mend(play, settings);
        if (hero.alive() && !realm.held()) {
            if (support(play, settings)) return {};
            bool chased = false;
            for (const sim::Body& body : realm.bodies()) {
                chased |= body.monster() && body.alive() && !body.gone && body.quarry == hero.id;
            }
            // What lies about him that its window ticks, while nothing is after him.
            anchorColumn_ = hero.column();
            anchorRow_ = hero.row();
            if (!chased && loot(play, settings)) return {};
            picking_ = 0;
            // His keys at what Auto-play's hand fights, as its window sets them: it gave only
            // the right button's one skill, so the rest of his bar sat unused while it followed.
            const sim::Request& order = realm.order();
            if (order.kind == sim::Request::Kind::Attack && order.target != 0) keys(play, settings, order.target);
        }
    }
    if (picking_ != 0) return {};
    int playing = following_;
    quester_.pickBoxes((settings.picks & HelperSettings::kPickBoxes) != 0);
    const std::string said = quester_.update(play, &playing, seconds, bar_, barCount_);
    if (playing >= 0 || said.empty()) return {};
    // Auto-play's hand is done with it: handed in, or it can go no further.
    if (realm.quest(following_).state == sim::QuestState::Resting) return finished();
    constexpr const char* kPrefix = "Auto-play: ";
    const bool prefixed = said.rfind(kPrefix, 0) == 0;
    return halt("Helper stopped: " + (prefixed ? said.substr(std::strlen(kPrefix)) : said));
}

// The nearest thing lying within its range of the spot that its window ticks -- Zen always -- he
// may take, can walk to and has room for.
bool Helper::loot(Play& play, const HelperSettings& settings) {
    const sim::Realm& realm = play.realm();
    const content::Tables& tables = *realm.tables();
    const sim::Body& hero = realm.hero();
    // Her summon hunts round her and not round the spot (kSummonHunt, realm_summon.cpp): what it
    // kills while she stands at the edge of her range lies that far beyond it, and was left there
    // (the user, 2026-10-20: "When the elf summon kills an enemy and it drop jewels, she is not
    // picking the items").
    const sim::Body* summon = realm.summoned();
    const bool hunting = summon != nullptr && summon->alive() && !summon->gone;
    const float reach = float(settings.tiles()) + (hunting ? float(sim::kSummonHunt) : 2.0f);
    const sim::Lying* nearest = nullptr;
    float best = 1e9f;
    for (const sim::Lying& one : realm.lying()) {
        if (!realm.mayTake(one)) continue;
        if (const auto tried = lootTried_.find(one.id); tried != lootTried_.end() && tried->second < clock_) continue;
        if (tilesApart(float(anchorColumn_), float(anchorRow_), float(one.column), float(one.row)) > reach) continue;
        if (!one.what.empty()) {
            if (one.what.item < 0 || size_t(one.what.item) >= tables.items.size()) continue;
            const content::ItemRow& row = tables.items[size_t(one.what.item)];
            const uint8_t p = settings.picks;
            bool wanted = (p & HelperSettings::kPickAll) != 0;
            // A Firecracker, a Box of Luck or a Box of Kundun under its Boxes tick, on at first (the
            // user, 2026-10-21: "helper auto-play also pickup firecracrehs, box of kundum and box of luck").
            const bool box = sim::firecracker(row) || sim::boxOfLuck(row) || sim::boxOfKundun(row);
            wanted |= (p & HelperSettings::kPickBoxes) && box;
            const int place = sim::placeOf(row);
            const bool pet = place == sim::kPet || place == sim::kMount;
            wanted |= (p & HelperSettings::kPickJewels) && !pet && (row.jewel() || sim::refiningJewel(row));
            wanted |= (p & HelperSettings::kPickPets) && pet;
            wanted |= (p & HelperSettings::kPickRunes) && sim::creation(row);
            // An Ancient rune is picked with the Legendaries: it is rarer than any of them.
            wanted |= (p & HelperSettings::kPickLegendary) &&
                      (qualityOf(row, one.what) == tip::Tone::RuneLegendary ||
                       qualityOf(row, one.what) == tip::Tone::RuneAncient);
            wanted |= (p & HelperSettings::kPickExcellent) && one.what.excellent != 0;
            wanted |= (p & HelperSettings::kPickSockets) && one.what.sockets > 0;
            // Boxes off leaves them lying under All as well: they are what fills his bag. A name
            // he typed is asked for outright, Boxes or not.
            const bool named = settings.named(row.label);
            if (!named && (!wanted || (box && !(p & HelperSettings::kPickBoxes)))) continue;
            sim::Satchel bag = realm.satchel();
            if (sim::pour(tables, bag, sim::kWorn, sim::kSlots, one.what) < 0) continue;
        }
        const float away = tilesApart(hero.x, hero.y, float(one.column), float(one.row));
        if (away >= best || !realm.reaches(hero, hero.column(), hero.row(), one.column, one.row)) continue;
        best = away;
        nearest = &one;
    }
    if (nearest == nullptr) return false;
    if (lootTried_.find(nearest->id) == lootTried_.end()) {
        lootTried_[nearest->id] = clock_ + kLootGiveUp;
        core::logf("helper: picking up %s at %d,%d",
                   nearest->what.empty() ? "Zen" : tables.items[size_t(nearest->what.item)].label.c_str(),
                   nearest->column, nearest->row);
    }
    picking_ = nearest->id;
    const sim::Request& order = realm.order();
    if (order.kind == sim::Request::Kind::Pick && order.target == nearest->id) return true;
    sim::Request request;
    request.kind = sim::Request::Kind::Pick;
    request.target = nearest->id;
    play.orderHero(request);
    target_ = 0;
    return true;
}

// **Repair my gear**: each worn piece at kMendAt percent of its durability or under, mended by his
// own hand at its self-repair price while the purse holds it, as RepairEquipments asks each beat
// (MuHelper.cpp:698). Not a move: he goes on fighting. What does not mend (the Angel, the Imp)
// costs nought and is passed over.
void Helper::mend(Play& play, const HelperSettings& settings) {
    const sim::Realm& realm = play.realm();
    if (!settings.repair || !realm.selfMending()) return;
    const content::Tables& tables = *realm.tables();
    const sim::Satchel& bag = realm.satchel();
    for (int slot = 0; slot < sim::kWorn; ++slot) {
        const sim::Held& h = bag[slot];
        if (h.empty() || size_t(h.item) >= tables.items.size()) continue;
        const int most = sim::maximumDurability(tables.items[size_t(h.item)], h);
        if (most <= 0 || (h.durability * 100 + most - 1) / most > kMendAt) continue;
        const int64_t cost = realm.repairCost(slot);
        if (cost <= 0 || cost > realm.money()) continue;
        if (const auto tried = mendTried_.find(slot); tried != mendTried_.end() && tried->second > clock_) continue;
        mendTried_[slot] = clock_ + kMendRetry;
        core::logf("helper: repairing %s (%d/%d) for %lld Zen", tables.items[size_t(h.item)].label.c_str(),
                   int(h.durability), most, (long long)cost);
        play.repair(slot);
    }
}

// His keys while he fights: each attack on them thrown at the one he fights as its use says --
// when it is ready, or only with two or three about it.
void Helper::keys(Play& play, const HelperSettings& settings, uint32_t target) {
    const sim::Realm& realm = play.realm();
    const sim::Body& hero = realm.hero();
    const sim::Body* at = realm.find(target);
    // Not held back by a swing under way: the realm spends his next swing on the skill.
    if (at == nullptr) return;
    int crowd = 0;
    for (const sim::Body& body : realm.bodies()) {
        if (body.monster() && body.alive() && !body.gone && tilesApart(at->x, at->y, body.x, body.y) <= kCrowd) ++crowd;
    }
    // An attack asks the hand that swings it, a boon the arm (Desk's plate: SkillRow::suits).
    const uint32_t hand = realm.wearer().hand;
    const int keys = std::min(barCount_, 5);  // the five keys; the right button's slot fights already
    for (int i = 0; i < keys; ++i) {
        const int32_t skill = bar_[i];
        const sim::SkillRow* row = skill > 0 ? sim::skillNumbered(skill) : nullptr;
        if (row == nullptr || !realm.knows(skill) || realm.cooling(skill) > 0) continue;
        const HelperKind kind = helperKind(*row, false);
        if (kind != HelperKind::Single && kind != HelperKind::Area) continue;
        const HelperUse use = settings.use(skill, kind);
        const bool now = use == HelperUse::Ready || (use == HelperUse::Near2 && crowd >= 2) ||
                         (use == HelperUse::Near3 && crowd >= 3);
        // A charge pays every stage at once (Realm::invoke).
        const int32_t cost = sim::manaCostOf(hero, *row);  // as the realm charges it, talents and all
        const int32_t price = row->chargeTicks > 0 ? cost * row->chargeStages : cost;
        if (!now || hero.mana < price || !row->suits(hand)) continue;
        if (const auto tried = tried_.find(skill); tried != tried_.end() && tried->second > clock_) continue;
        tried_[skill] = clock_ + kSkillRetry;
        core::logf("helper: %s at %u, %d about it", row->name, unsigned(target), crowd);
        play.castSkill(skill, target);
        return;
    }
}

// What the right button fights with: its own skill, else the first attack on his keys he may throw
// (QuestAuto::striker's rule, asked of the hand that swings) and not turned off in the window (the
// user, 2026-10-22: "i turned off all skill on helper but dk keeps using them"); -1 keeps the
// quick slot's, and with none there he swings bare.
int32_t Helper::striker(Play& play, const HelperSettings& settings) const {
    if (play.quickSkill() != 0) return -1;
    const sim::Realm& realm = play.realm();
    const uint32_t hand = realm.wearer().hand;
    int32_t any = -1;
    for (int i = 0; i < std::min(barCount_, 5); ++i) {
        const int32_t skill = bar_[i];
        const sim::SkillRow* row = skill > 0 ? sim::skillNumbered(skill) : nullptr;
        if (row == nullptr || !realm.knows(skill) || !row->suits(hand)) continue;
        if (row->onSelf() || row->chargeTicks > 0 || row->channelled() || row->blinks) continue;
        if (settings.use(skill, helperKind(*row, false)) == HelperUse::Off) continue;
        if (row->primary()) return skill;
        if (any < 0) any = skill;
    }
    return any;
}

}  // namespace mu::game
