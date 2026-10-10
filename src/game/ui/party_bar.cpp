#include "game/ui/party_bar.h"

#include <algorithm>
#include <cmath>

#include "game/play.h"
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
// The gaps 12 and 8 from 7 and 4 (the user, 2026-10-10, "increae gaps between labels").
constexpr capsule::Make kPlate{3.0f, 21.0f, 4.0f, 6.0f, 11.0f, 14.5f, 9.0f, 12.5f, 10.0f, 12.0f, 0.66f};  // a party member
// A raid line a size under a plate: "raid label sizes i think can bi bigger" (the user, the same day).
constexpr capsule::Make kLine{3.0f, 19.0f, 3.5f, 5.5f, 8.0f, 13.5f, 0.0f, 11.5f, 9.0f, 11.0f, 0.62f};    // a raider
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
        if (hit.box.has(x, y)) return true;
    }
    return false;
}

void PartyBar::update(float seconds, Play& play, const Pointer& pointer) {
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
    const bool leading = inParty && party.members.front().name == me;

    // A left click on a capsule makes him the target, from any map; a right click asks for his
    // menu, which the desk opens (game/ui/player_menu.h). Off a server the --raid harness's
    // raiders are no one to whisper or invite, so the menu is a server's only.
    for (const Hit& hit : hits_) {
        if (!hit.box.has(pointer.x, pointer.y)) continue;
        if (pointer.pressed) {
            play.target(hit.name, hit.here);
            play.ui(Play::Ui::Click);
            took_ = true;
        } else if (pointer.rightPressed && play.remote()) {
            play.target(hit.name, hit.here);
            menuAsk_ = hit.name;
            menuX_ = hit.box.right() + 8.0f * u;
            menuY_ = pointer.y - 12.0f * u;
            took_ = true;
        }
        break;
    }
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
    for (const Raider& one : raid) ease(one.name, float(one.health) / 100.0f, one.health > 0);
    clock_ = moving ? clock_ + seconds : 0.0f;

    if (same(party, drawn_) && raid == raid_ && me == me_ && u == unit_ && !moving && !targetMoved) {
        return;  // redrawn on a change only, or while something moves
    }
    drawn_ = party;
    raid_ = std::move(raid);
    me_ = me;
    unit_ = u;
    canvas_.clear();
    hits_.clear();
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

    const float x = kLeft * u;
    float y = (540.0f - kAboveMiddle) * u;  // the unit is 1080 lines' pixel
    // He leads: his own capsule is not drawn, so the badge stands over the column with "You lead".
    if (leading && !others.empty()) {
        const float base = y - 8.0f * u, s = 1.1f * u;
        capsule::crown(canvas_, x + 1.0f * u, base + 1.0f * u, s, capsule::kCrownDrop);
        capsule::crown(canvas_, x, base, s, capsule::kGold);
        const float named = controls::label(canvas_, x + 18.0f * u, base, 13.0f * u, capsule::kGold, "You");
        controls::label(canvas_, x + 18.0f * u + named + 4.0f * u, base, 13.0f * u, style::kBone2, "lead the party");
    }
    for (const auto* member : others) {
        capsule::Look c;
        c.name = member->name;
        c.kin = member->kin;
        c.leader = member == &party.members.front();
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
        const gfx::Box box{x, y, kWide * u, (kPlate.shield + kPlate.life + kPlate.mana) * u};
        if (member->name == targeted_) chosen(box);
        const float tall = capsule::draw(canvas_, x, y, u, kWide * u, kPlate, c, pulse);
        hits_.push_back({box, member->name, true, member->here});
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
            const gfx::Box box{x, y, kWide * u, (kLine.shield + kLine.life + kLine.mana) * u};
            if (one.name == targeted_) chosen(box);
            const float tall = capsule::draw(canvas_, x, y, u, kWide * u, kLine, c, pulse);
            hits_.push_back({box, one.name, false, 0});
            y += tall;
        }
        if (shown < raid_.size()) {
            controls::caps(canvas_, x + 8.0f * u, y + 10.0f * u, 11.0f * u, style::kAshInk,
                           "+" + std::to_string(raid_.size() - shown) + " more", 0.2f);
        }
    }

}

}  // namespace mu::game
