#include "game/helper.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "content/tables.h"
#include "core/log.h"
#include "game/play.h"
#include "game/ui/describe.h"
#include "sim/items.h"
#include "sim/realm.h"
#include "sim/skills.h"

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
// His health under which a Heal set to "when hurt" is cast, when his auto-drink line is off.
constexpr int kHurtDefault = 50;

float tilesApart(float x0, float y0, float x1, float y1) { return std::max(std::fabs(x1 - x0), std::fabs(y1 - y0)); }

}  // namespace

HelperKind helperKind(const sim::SkillRow& row, bool rightSlot) {
    if (rightSlot) return HelperKind::Right;
    if (row.blinks || row.chargeTicks > 0 || row.mounted) return HelperKind::Moves;
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
        else if (!std::strcmp(key, "picks") && std::fscanf(f, "%d", &a) == 1) read.picks = uint8_t(a & 63);
        else if (!std::strcmp(key, "seen") && std::fscanf(f, "%d", &a) == 1) read.seen = a != 0;
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
                 int(settings.picks), settings.seen ? 1 : 0);
    for (const auto& [skill, use] : settings.uses) std::fprintf(f, "use %d %d\n", int(skill), int(use));
    std::fclose(f);
    return true;
}

// ---- the hand ----------------------------------------------------------------------------------

void Helper::start(const Play& play) {
    const sim::Body& hero = play.realm().hero();
    on_ = true;
    stats_ = {};
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
}

void Helper::stop() {
    if (on_) core::logf("helper: stopped after %.0f s, %d kills, %d picked up", stats_.seconds, stats_.kills, stats_.picked);
    on_ = false;
    target_ = picking_ = 0;
}

std::string Helper::halt(const std::string& why) {
    stop();
    core::logf("helper: %s", why.c_str());
    return why;
}

std::string Helper::update(Play& play, const HelperSettings& settings, double seconds, const int32_t* bar,
                           int barCount) {
    if (!on_) return {};
    clock_ += seconds;
    stats_.seconds += seconds;
    barCount_ = bar ? std::min(barCount, 8) : 0;
    for (int i = 0; i < barCount_; ++i) bar_[i] = bar[i];
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
    if (picking_ != 0 && std::none_of(realm.lying().begin(), realm.lying().end(),
                                      [&](const sim::Lying& one) { return one.id == picking_; })) {
        ++stats_.picked;
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
    if (!onIt) play.fight(target_, striker(play));
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
        if (!wanted || hero.mana < row->mana || !row->suits(offHand)) continue;
        if (const auto tried = tried_.find(skill); tried != tried_.end() && tried->second > clock_) continue;
        tried_[skill] = clock_ + kAuraRetry;
        core::logf("helper: %s", row->name);
        play.castHero(skill, hero.id);
        return true;
    }
    return false;
}

// The nearest thing lying within its range of the spot that its window ticks -- Zen always -- he
// may take, can walk to and has room for.
bool Helper::loot(Play& play, const HelperSettings& settings) {
    const sim::Realm& realm = play.realm();
    const content::Tables& tables = *realm.tables();
    const sim::Body& hero = realm.hero();
    const float reach = float(settings.tiles()) + 2.0f;
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
            wanted |= (p & HelperSettings::kPickJewels) && (row.jewel() || sim::refiningJewel(row));
            wanted |= (p & HelperSettings::kPickRunes) && sim::creation(row);
            wanted |= (p & HelperSettings::kPickLegendary) && qualityOf(row, one.what) == tip::Tone::RuneLegendary;
            wanted |= (p & HelperSettings::kPickExcellent) && one.what.excellent != 0;
            wanted |= (p & HelperSettings::kPickSockets) && one.what.sockets > 0;
            if (!wanted) continue;
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
        if (!now || hero.mana < row->mana || !row->suits(hand)) continue;
        if (const auto tried = tried_.find(skill); tried != tried_.end() && tried->second > clock_) continue;
        tried_[skill] = clock_ + kSkillRetry;
        core::logf("helper: %s at %u, %d about it", row->name, unsigned(target), crowd);
        play.castSkill(skill, target);
        return;
    }
}

// What the right button fights with: its own skill, else the first attack on his keys he may throw
// (QuestAuto::striker's rule, asked of the hand that swings); -1 keeps the quick slot's.
int32_t Helper::striker(Play& play) const {
    if (play.quickSkill() != 0) return -1;
    const sim::Realm& realm = play.realm();
    const uint32_t hand = realm.wearer().hand;
    int32_t any = -1;
    for (int i = 0; i < std::min(barCount_, 5); ++i) {
        const int32_t skill = bar_[i];
        const sim::SkillRow* row = skill > 0 ? sim::skillNumbered(skill) : nullptr;
        if (row == nullptr || !realm.knows(skill) || !row->suits(hand)) continue;
        if (row->onSelf() || row->chargeTicks > 0 || row->channelled() || row->blinks) continue;
        if (row->primary()) return skill;
        if (any < 0) any = skill;
    }
    return any;
}

}  // namespace mu::game
