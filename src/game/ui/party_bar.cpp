#include "game/ui/party_bar.h"

#include <algorithm>
#include <cmath>

#include "game/play.h"
#include "game/portrait.h"
#include "game/ui/capsule.h"
#include "game/ui/controls.h"
#include "game/ui/panel.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"
#include "sim/realm_tuning.h"

namespace mu::game {

namespace {

// Measures in tip::unit(), a pixel at 1080 lines. The column's top sits this far above the
// screen's middle, clear of the exp lane's ten rows in the corner above it.
// 200 wide, from 240 (the user, 2026-10-10: "label is to wide").
constexpr float kLeft = 16.0f, kAboveMiddle = 188.0f, kWide = 200.0f;
constexpr size_t kRaidMost = 15;  // raid lines, then "+N more"

// A capsule's make: its three bands top to foot, its corner, the gap under it, and its words.
// The gaps 12 and 8 from 7 and 4 (the user, 2026-10-10, "increae gaps between labels"), then 20
// and 14 from 11 and 8 once the faces came (the user, 2026-10-11: "increase gaps between
// palrty/raid char labels" -- the 36-unit faces all but touched).
// The name clear of the face's tuck, and no class cell: the face says the class until his picture
// is taken (capsule::face).
constexpr capsule::Make kPlate{3.0f, 21.0f, 4.0f, 6.0f, 20.0f, 14.5f, 9.0f, 12.5f, 17.0f, 0.0f, 0.66f};  // a party member
// A raid line a size under a plate: "raid label sizes i think can bi bigger" (the user, the same day).
constexpr capsule::Make kLine{3.0f, 19.0f, 3.5f, 5.5f, 14.0f, 13.5f, 0.0f, 11.5f, 9.0f, 11.0f, 0.62f};    // a raider
// His face at the capsule's left end, as the target frame's (the user, 2026-10-11: "same idea
// with circle thumb so we have same logic"): its width, its ring, how far the capsule runs in under it.
constexpr float kFace = 36.0f, kRing = 2.0f, kTuck = 10.0f;
// The menu's button at the capsule's right end, and its gap from it.
constexpr float kMore = 16.0f, kMoreGap = 4.0f;
constexpr int kSelf = -2;  // onMore_: "Leave" on his own line
constexpr int kConvert = -3;  // onMore_: "Make raid" or "Make party" beside it
constexpr float kBetween = 16.0f;  // between the party and the rest of the raid, with no heading
constexpr uint32_t kChosen = gfx::rgba(0.878f, 0.714f, 0.361f, 0.95f);  // the target's edge, the leader's gold
constexpr float kTrailRate = 4.0f;  // the trail closes by e^-rate a second
constexpr float kPulseRate = 5.7f;  // radians a second: a breath about every 1.1 s

bool same(const net::PartyState& a, const net::PartyState& b) {
    if (a.members.size() != b.members.size()) return false;
    for (size_t i = 0; i < a.members.size(); ++i) {
        const auto& x = a.members[i];
        const auto& y = b.members[i];
        if (x.name != y.name || x.level != y.level || x.world != y.world || x.here != y.here ||
            x.health != y.health || x.mana != y.mana || x.shield != y.shield || x.life != y.life ||
            x.lifeMost != y.lifeMost) {
            return false;
        }
    }
    return true;
}

// A map's name as the minimap says it: the folder's, its first letter up.
std::string mapName(const std::string& world) {
    std::string name = world;
    if (!name.empty()) name[0] = char(std::toupper(uint8_t(name[0])));
    return name;
}

int percent(int32_t shown, int32_t most) {
    return most > 0 ? std::clamp(int(std::lround(100.0 * double(shown) / double(most))), 0, 100) : 0;
}

// One raider's line: his health as drawn, his mana and his SD where his body carries them.
PartyBar::Raider raiderRow(const Play& play, const sim::Body& body, const std::string& name) {
    PartyBar::Raider row;
    row.name = name;
    row.kin = int(body.kin);
    row.health = body.alive() ? std::max(1, percent(play.shownHealth(body.id), body.maxHealth)) : 0;
    row.mana = body.maxMana > 0 ? percent(body.mana, body.maxMana) : -1;
    row.shield = body.maxSd > 0 ? percent(body.sd, body.maxSd) : -1;
    return row;
}

// Within the dragon's field, as Play::raidFighting reckons him in it.
constexpr float kField = 18.0f;

// The rest of the raid while he fights the dragon: the --raid harness's raiders, or on a server
// every other player in the dragon's field who is not of his party.
std::vector<PartyBar::Raider> raidOf(const Play& play) {
    std::vector<PartyBar::Raider> rows;
    if (!play.raidFighting()) return rows;
    const sim::Realm& realm = play.realm();
    for (int i = 0; i < realm.raiderCount(); ++i) {
        const sim::Body* raider = realm.raiderAt(i);
        if (!raider || size_t(i + 1) >= play.raidParty().size()) continue;
        rows.push_back(raiderRow(play, *raider, play.raidParty()[size_t(i + 1)].name));
    }
    const sim::Body* dragon = realm.invader();
    const sim::Body& hero = realm.hero();
    if (dragon == nullptr) return rows;
    for (const sim::Body& body : realm.bodies()) {
        if (body.id == hero.id || body.raider >= 0 || !sim::within(body, *dragon, kField)) continue;
        if (hero.party != 0 && body.party == hero.party) continue;
        std::string name;
        if (!play.playerName(body.id, &name, nullptr)) continue;
        rows.push_back(raiderRow(play, body, name));
    }
    return rows;
}

}  // namespace

void PartyBar::open(const gfx::Interface& interface) { interface.adopt(canvas_); }

bool PartyBar::covers(float x, float y) const {
    if (took_) return true;
    if (!showing_) return false;
    for (const Hit& hit : hits_) {
        if (hit.box.has(x, y) || (hit.more.w > 0.0f && hit.more.has(x, y))) return true;
    }
    if (leave_.w > 0.0f && leave_.has(x, y)) return true;
    if (convert_.w > 0.0f && convert_.has(x, y)) return true;
    return false;
}

void PartyBar::update(float seconds, Play& play, const Pointer& pointer,
                       const std::unordered_map<std::string, Portrait>& faces, uint64_t facesTaken) {
    took_ = false;
    if (!play.isOpen()) {
        showing_ = false;
        return;
    }
    std::string me;
    play.playerName(play.realm().hero().id, &me, nullptr);
    const net::PartyState& party = play.party();
    std::vector<Raider> raid = raidOf(play);
    const float u = tip::unit();
    const bool inParty = party.members.size() > 1;

    // A click on a capsule makes him the target, from any map; one on its button asks for his
    // menu, which the desk opens (game/ui/player_menu.h). The button stands only while he is
    // reachable, on this map, and on a server: off one the --raid harness's raiders are no one
    // to whisper or invite.
    // And "Leave" on his own line over the column: he leaves at once.
    int onMore = -1;
    if (leave_.w > 0.0f && leave_.has(pointer.x, pointer.y)) {
        onMore = kSelf;
        if (pointer.pressed) {
            play.askParty({net::PartyWhat::Leave, std::string()});
            play.ui(Play::Ui::Click);
            took_ = true;
        }
    }
    // The leader's "Make raid" (a party) or "Make party" (a raid of five or fewer) beside it.
    if (onMore == -1 && convert_.w > 0.0f && convert_.has(pointer.x, pointer.y)) {
        onMore = kConvert;
        if (pointer.pressed) {
            play.askParty({party.raid ? net::PartyWhat::Unraid : net::PartyWhat::Raid, std::string()});
            play.ui(Play::Ui::Click);
            took_ = true;
        }
    }
    for (size_t i = 0; i < hits_.size() && onMore == -1; ++i) {
        const Hit& hit = hits_[i];
        if (hit.more.w > 0.0f && hit.more.has(pointer.x, pointer.y)) {
            onMore = int(i);
            if (pointer.pressed) {
                play.target(hit.name, hit.here);
                menuAsk_ = hit.name;
                menuX_ = hit.more.right() + 6.0f * u;
                menuY_ = hit.more.y;
                took_ = true;
            }
            break;
        }
        if (!hit.box.has(pointer.x, pointer.y)) continue;
        if (pointer.pressed) {
            if (hit.name != me) play.target(hit.name, hit.here);
            play.ui(Play::Ui::Click);
            took_ = true;
        }
        break;
    }
    const bool moreMoved = onMore != onMore_;
    onMore_ = onMore;
    const bool facesMoved = facesTaken != facesTaken_;
    facesTaken_ = facesTaken;
    // His own capsule reads his body, which moves between the party's once-a-second states.
    const sim::Body& hero = play.realm().hero();
    const Self self{hero.health, hero.maxHealth, hero.mana, hero.maxMana, hero.sd, hero.maxSd, hero.level,
                    play.shownAlive(hero.id)};
    const bool selfMoved = !(self == self_);
    self_ = self;
    const bool targetMoved = play.targetName() != targeted_;
    targeted_ = play.targetName();

    // The trails: up at once to a heal, down by easing to a blow. Anyone low keeps it moving too.
    bool moving = false;
    const auto ease = [&](const std::string& name, float share, bool alive) {
        auto [it, fresh] = trail_.try_emplace(name, share);
        float& t = it->second;
        if (fresh || share >= t || !alive) {
            t = share;
        } else {
            t = share + (t - share) * std::exp(-kTrailRate * seconds);
            if (t - share < 0.002f) t = share;
            else moving = true;
        }
        if (alive && share * 100.0f < capsule::kLow) moving = true;
    };
    for (const auto& member : party.members) {
        if (member.name != me) ease(member.name, float(member.health) / 100.0f, member.health > 0 && member.here != 0);
    }
    if (inParty) {
        ease(me, hero.maxHealth > 0 ? float(std::max(0, play.shownHealth(hero.id))) / float(hero.maxHealth) : 0.0f,
             self.alive);
    }
    for (const Raider& one : raid) ease(one.name, float(one.health) / 100.0f, one.health > 0);
    clock_ = moving ? clock_ + seconds : 0.0f;

    if (same(party, drawn_) && party.raid == raidDrawn_ && raid == raid_ && me == me_ && u == unit_ && !moving &&
        !targetMoved && !moreMoved && !facesMoved && !selfMoved) {
        return;  // redrawn on a change only, or while something moves
    }
    drawn_ = party;
    raidDrawn_ = party.raid;
    raid_ = std::move(raid);
    me_ = me;
    unit_ = u;
    canvas_.clear();
    hits_.clear();
    leave_ = {};
    convert_ = {};
    // The target's capsule wears a thin gold edge a little outside its rim.
    const auto chosen = [&](const gfx::Box& box) {
        const float g = 3.0f * u;
        canvas_.outline({box.x - g, box.y - g, box.w + 2.0f * g, box.h + 2.0f * g}, std::max(1.0f, 1.5f * u), kChosen);
    };
    std::vector<const net::PartyState::Member*> others;
    for (const auto& member : party.members) {
        if (member.name != me) others.push_back(&member);
    }
    showing_ = !others.empty() || !raid_.empty();
    if (!showing_) {
        trail_.clear();
        return;
    }
    const float pulse = 0.5f - 0.5f * std::cos(clock_ * kPulseRate);
    const auto trailOf = [&](const std::string& name) {
        const auto it = trail_.find(name);
        return it != trail_.end() ? it->second : 0.0f;
    };

    // The capsules' column, right of the faces; raid lines stand in it too.
    const float x = kLeft * u + (kFace - kTuck) * u;
    // Off a server too: a bench hero's menu trades and inspects (PlayerMenu::show).
    const auto button = [&](float top, float tall) -> gfx::Box {
        const gfx::Box more{x + (kWide + kMoreGap) * u, top, kMore * u, tall};
        capsule::more(canvas_, more, u, onMore_ == int(hits_.size()));
        return more;
    };
    float y = (540.0f - kAboveMiddle) * u;  // the unit is 1080 lines' pixel
    // Over the column, "Leave" at its right end: what he can do to himself (the user, 2026-10-11:
    // Leave party off the other players' menus). No "You lead the party" words: his own capsule,
    // first under it, says it with the crown on his face ("dublicated party leader icons").
    if (inParty && !others.empty()) {
        const float base = y - 8.0f * u;
        // Leave and the raid's switch are the server's: on one only.
        if (play.remote()) {
            const float size = 13.0f * u, w = controls::labelWidth(size, "Leave");
            const float right = x + kWide * u;
            leave_ = {right - w - 4.0f * u, base - 14.0f * u, w + 8.0f * u, 19.0f * u};
            controls::label(canvas_, right - w, base, size,
                            onMore_ == kSelf ? style::kDanger : capsule::faded(style::kDanger, 0.7f), "Leave");
            // The leader turns his party into a raid of ten, and a raid of five or fewer back
            // (the user, 2026-10-13: "party can be converted to raid with max 10 layers").
            const bool leading = party.members.front().name == me;
            if (leading && (!party.raid || int(party.members.size()) <= sim::kPartyMost)) {
                const char* word = party.raid ? "Make party" : "Make raid";
                const float cw = controls::labelWidth(size, word);
                const float cright = leave_.x - 14.0f * u;
                convert_ = {cright - cw - 4.0f * u, base - 14.0f * u, cw + 8.0f * u, 19.0f * u};
                controls::label(canvas_, cright - cw, base, size,
                                onMore_ == kConvert ? style::kBoneHi : capsule::faded(style::kBoneHi, 0.7f), word);
            }
        }
    }
    // **His own capsule first** (the user, 2026-10-11: "i need to see also my self party label on
    // party"), then the others in the party's order.
    std::vector<const net::PartyState::Member*> rows;
    for (const auto& member : party.members) {
        if (member.name == me && !others.empty()) rows.push_back(&member);
    }
    rows.insert(rows.end(), others.begin(), others.end());
    for (const auto* member : rows) {
        const bool mine = member->name == me;
        capsule::Look c;
        c.name = member->name;
        c.kin = member->kin;
        c.leader = member == &party.members.front();
        c.crown = false;  // over his face instead
        c.away = member->here == 0;
        c.dead = !c.away && member->health == 0;
        c.tag = c.away ? std::string() : std::to_string(member->level);
        // His health in points where the server says them; else its percent.
        c.number = c.away ? mapName(member->world)
                   : member->life >= 0 ? panel::grouped(member->life)
                                       : std::to_string(member->health) + "%";
        c.shield = member->shield != net::PartyState::kNoShield ? float(member->shield) / 100.0f : -1.0f;
        c.life = float(member->health) / 100.0f;
        c.mana = float(member->mana) / 100.0f;
        c.trail = trailOf(member->name);
        if (mine) {
            // His own, off his body: the party's state is a second behind it.
            const int32_t health = std::max(0, play.shownHealth(hero.id));
            c.away = false;
            c.dead = !self.alive;
            c.tag = std::to_string(hero.level);
            c.number = panel::grouped(health);
            c.life = hero.maxHealth > 0 ? std::clamp(float(health) / float(hero.maxHealth), 0.0f, 1.0f) : 0.0f;
            c.mana = hero.maxMana > 0 ? std::clamp(float(hero.mana) / float(hero.maxMana), 0.0f, 1.0f) : -1.0f;
            c.shield = hero.maxSd > 0 ? std::clamp(float(hero.sd) / float(hero.maxSd), 0.0f, 1.0f) : -1.0f;
            if (c.dead) c.life = c.mana = c.shield = 0.0f;
        }
        const float high = (kPlate.shield + kPlate.life + kPlate.mana) * u;
        const gfx::Box box{x, y, kWide * u, high};
        if (member->name == targeted_) chosen(box);
        const float tall = capsule::draw(canvas_, x, y, u, kWide * u, kPlate, c, pulse);
        // His face over the capsule's left end, the leader's in gold with the crown on it.
        const float r = kFace * 0.5f * u, cx = x - (kFace - kTuck) * u + r, cy = y + high * 0.5f;
        const auto face = faces.find(member->name);
        const bool taken = face != faces.end() && face->second.taken();
        capsule::face(canvas_, cx, cy, r, kRing * u, taken ? &face->second.picture() : nullptr, member->kin, c.dead,
                      c.away, c.leader);
        // No button on his own: what he can do to himself, Leave, is on the line over the column.
        const gfx::Box more = member->here != 0 && !mine ? button(y, high) : gfx::Box{};
        const gfx::Box whole{cx - r - kRing * u, std::min(y, cy - r - kRing * u), box.right() - (cx - r - kRing * u),
                             std::max(high, 2.0f * (r + kRing * u))};
        hits_.push_back({whole, member->name, true, member->here, more});
        y += tall;
    }

    // The rest of the raid, a line each, a little apart from the party; no heading (the user,
    // 2026-10-10: "get rid of party header": the leader's crown is in his capsule).
    if (!raid_.empty()) {
        if (!others.empty()) y += (kBetween - kPlate.gap) * u;
        const size_t shown = raid_.size() > kRaidMost ? kRaidMost - 1 : raid_.size();
        for (size_t i = 0; i < shown; ++i) {
            const Raider& one = raid_[i];
            capsule::Look c;
            c.name = one.name;
            c.kin = one.kin;
            c.dead = one.health == 0;
            c.number = std::to_string(one.health) + "%";
            c.shield = one.shield >= 0 ? float(one.shield) / 100.0f : -1.0f;
            c.life = float(one.health) / 100.0f;
            c.mana = one.mana >= 0 ? float(one.mana) / 100.0f : -1.0f;
            c.trail = trailOf(one.name);
            const float high = (kLine.shield + kLine.life + kLine.mana) * u;
            const gfx::Box box{x, y, kWide * u, high};
            if (one.name == targeted_) chosen(box);
            const float tall = capsule::draw(canvas_, x, y, u, kWide * u, kLine, c, pulse);
            // A raider is in this world by the row's making: always reachable.
            const gfx::Box more = button(y, high);
            hits_.push_back({box, one.name, false, 0, more});
            y += tall;
        }
        if (shown < raid_.size()) {
            controls::caps(canvas_, x + 8.0f * u, y + 10.0f * u, 11.0f * u, style::kAshInk,
                           "+" + std::to_string(raid_.size() - shown) + " more", 0.2f);
        }
    }

}

}  // namespace mu::game
