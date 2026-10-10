#include "game/ui/party_bar.h"

#include <algorithm>
#include <cmath>

#include "game/play.h"
#include "game/ui/controls.h"
#include "game/ui/panel.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"
#include "sim/realm_tuning.h"

namespace mu::game {

namespace {

uint32_t faded(uint32_t abgr, float alpha) {
    const float a = float(abgr >> 24) / 255.0f * std::clamp(alpha, 0.0f, 1.0f);
    return (abgr & 0xFFFFFFu) | (uint32_t(a * 255.0f + 0.5f) << 24);
}

// Measures in tip::unit(), a pixel at 1080 lines. The column's top sits this far above the
// screen's middle, clear of the exp lane's ten rows in the corner above it.
constexpr float kLeft = 16.0f, kAboveMiddle = 188.0f, kWide = 240.0f;
constexpr size_t kRaidMost = 15;  // raid lines, then "+N more"

// A capsule's make: its three bands top to foot, its corner, the gap under it, and its words.
// The gaps 12 and 8, from 7 and 4: the user, 2026-10-10, "increae gaps between labels".
struct Make {
    float shield, life, mana, radius, gap;
    float name, tag, number;  // the name's size, the level tag's, the number's
    float band;               // the dark band behind the name, a share of the width
};
constexpr Make kPlate{3.0f, 18.0f, 4.0f, 5.0f, 12.0f, 15.0f, 9.0f, 13.0f, 0.6f};  // a party member
constexpr Make kLine{2.0f, 14.0f, 3.0f, 4.0f, 8.0f, 12.5f, 0.0f, 11.0f, 0.55f};  // a raider

constexpr uint32_t kGold = gfx::rgba(0.878f, 0.714f, 0.361f);  // the leader: the tracker's gold
constexpr uint32_t kWell = gfx::rgba(0.024f, 0.016f, 0.016f, 0.78f);
constexpr uint32_t kBand = gfx::rgba(0.024f, 0.016f, 0.016f, 0.78f);  // behind the name, fading out
constexpr uint32_t kSeam = gfx::rgba(0.0f, 0.0f, 0.0f, 0.6f);         // between the bands
// The fills, each a top and a foot.
constexpr uint32_t kShieldTop = gfx::rgba(0.949f, 0.698f, 0.353f), kShieldFoot = gfx::rgba(0.906f, 0.592f, 0.231f);
constexpr uint32_t kLifeTop = gfx::rgba(0.761f, 0.176f, 0.141f), kLifeFoot = gfx::rgba(0.478f, 0.090f, 0.071f);
constexpr uint32_t kLowTop = gfx::rgba(0.878f, 0.341f, 0.239f), kLowFoot = gfx::rgba(0.631f, 0.169f, 0.102f);
constexpr uint32_t kAwayTop = gfx::rgba(0.490f, 0.353f, 0.333f), kAwayFoot = gfx::rgba(0.302f, 0.224f, 0.212f);
constexpr uint32_t kManaTop = gfx::rgba(0.239f, 0.471f, 0.878f), kManaFoot = gfx::rgba(0.137f, 0.310f, 0.659f);
constexpr uint32_t kTrail = gfx::rgba(0.941f, 0.702f, 0.604f, 0.85f);  // health just lost
constexpr uint32_t kPulse = gfx::rgba(1.0f, 0.416f, 0.290f);           // under 30%, breathing
constexpr uint32_t kDead = gfx::rgba(0.816f, 0.341f, 0.298f);          // the word
constexpr uint32_t kNumber = gfx::rgba(0.965f, 0.918f, 0.847f);
constexpr uint32_t kGhost = gfx::rgba(0.553f, 0.518f, 0.471f);         // a dead one's name
constexpr float kLow = 30.0f;     // percent: the fill turns hot and pulses
constexpr float kAway = 0.6f;     // a member on another map, this much of himself
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

// The leader's mark: a small crown in the leader's gold, three points over a band, drawn from
// rects (no art for it in MU's interface). `x`, `base`: its left and its foot, `s` the unit.
void crown(gfx::Canvas& canvas, float x, float base, float s, uint32_t ink) {
    canvas.rect({x, base - 3.0f * s, 12.0f * s, 3.0f * s}, ink);
    canvas.rect({x, base - 9.0f * s, 2.5f * s, 6.0f * s}, ink);
    canvas.rect({x + 4.75f * s, base - 10.0f * s, 2.5f * s, 7.0f * s}, ink);
    canvas.rect({x + 9.5f * s, base - 9.0f * s, 2.5f * s, 6.0f * s}, ink);
}

// A heading over a column: the word, and who leads it, his name in gold ("You" when it is he).
float heading(gfx::Canvas& canvas, float x, float y, float u, const std::string& word, const std::string& leader) {
    const float base = y + 11.0f * u, size = 11.0f * u;
    controls::caps(canvas, x, base, size, style::kAshInk, word, 0.2f);
    if (leader.empty()) return y + 18.0f * u;
    float at = x + controls::capsWidth(size, word, 0.2f) + 8.0f * u;
    crown(canvas, at, base, u, kGold);
    at += 17.0f * u;
    at += controls::label(canvas, at, base, 13.0f * u, kGold, leader);
    controls::label(canvas, at + 4.0f * u, base, 13.0f * u, style::kAshInk, leader == "You" ? "lead" : "leads");
    return y + 20.0f * u;
}

// What one capsule says.
struct Capsule {
    std::string name;
    std::string tag;     // the level after the name, or empty
    std::string number;  // at the right: his health, its percent, a map's name, or empty
    float shield = -1.0f, life = 0.0f, mana = -1.0f, trail = 0.0f;  // shares, -1 for none
    bool leader = false, away = false, dead = false;
};

// A band's fill from the left, rounded at the capsule's corners only where it reaches them.
void fill(gfx::Canvas& canvas, const gfx::Box& band, float share, uint32_t top, uint32_t foot, float a,
          float topRadius, float footRadius) {
    if (share <= 0.0f) return;
    const float w = band.w * std::min(share, 1.0f);
    const bool full = share >= 0.999f;
    const float r[4] = {topRadius, full ? topRadius : 0.0f, full ? footRadius : 0.0f, footRadius};
    if (topRadius > 0.0f || footRadius > 0.0f) {
        tip::rounded(canvas, {band.x, band.y, w, band.h}, r, faded(top, a), faded(foot, a));
    } else {
        canvas.shade({band.x, band.y, w, band.h}, faded(top, a), faded(top, a), faded(foot, a), faded(foot, a));
    }
}

// One member: the capsule at `x`, `y`, `make`'s size. Returns its height with the gap under it.
float capsule(gfx::Canvas& canvas, float x, float y, float u, const Make& make, const Capsule& c,
              float pulse) {
    const float a = c.away ? kAway : 1.0f;
    const float wide = kWide * u, r = make.radius * u;
    const float sh = make.shield * u, lh = make.life * u, mh = make.mana * u, tall = sh + lh + mh;
    const float corners[4] = {r, r, r, r};
    tip::rounded(canvas, {x, y, wide, tall}, corners, faded(kWell, a));
    // SD along the top, health the body, mana the foot.
    if (c.shield >= 0.0f) fill(canvas, {x, y, wide, sh}, c.shield, kShieldTop, kShieldFoot, a, r, 0.0f);
    const gfx::Box body{x, y + sh, wide, lh};
    const bool low = !c.dead && !c.away && c.life * 100.0f < kLow;
    fill(canvas, body, c.life, c.away ? kAwayTop : low ? kLowTop : kLifeTop,
         c.away ? kAwayFoot : low ? kLowFoot : kLifeFoot, a, 0.0f, 0.0f);
    if (c.trail > c.life + 0.002f) {
        canvas.rect({x + wide * c.life, body.y, wide * (std::min(c.trail, 1.0f) - c.life), lh}, faded(kTrail, a));
    }
    if (low && pulse > 0.0f) canvas.rect({x, body.y, wide * c.life, lh}, faded(kPulse, 0.45f * pulse));
    if (c.mana >= 0.0f) fill(canvas, {x, y + sh + lh, wide, mh}, c.mana, kManaTop, kManaFoot, a, 0.0f, r);
    canvas.rect({x, body.y, wide, 0.8f * u}, kSeam);
    canvas.rect({x, body.y + lh - 0.8f * u, wide, 0.8f * u}, kSeam);
    // The dark band behind the name, fading into the fill.
    const float band = wide * make.band;
    canvas.shade({x, body.y, band * 0.35f, lh}, faded(kBand, a), faded(kBand, a * 0.8f), faded(kBand, a * 0.8f),
                 faded(kBand, a));
    canvas.shade({x + band * 0.35f, body.y, band * 0.65f, lh}, faded(kBand, a * 0.8f), 0u, 0u, faded(kBand, a * 0.8f));

    // The words, on the health's middle.
    const float base = body.y + lh * 0.5f + make.name * u * 0.36f;
    float at = x + 8.0f * u;
    if (c.leader) {
        crown(canvas, at, base, make.name / 15.0f * u, faded(kGold, a));
        at += make.name / 15.0f * 17.0f * u;
    }
    const uint32_t ink = c.dead ? kGhost : c.leader ? kGold : style::kBoneHi;
    const float named = controls::label(canvas, at, base, make.name * u, faded(ink, a), c.name);
    if (c.dead) canvas.rect({at, base - make.name * u * 0.3f, named, 1.0f * u}, faded(kGhost, a));
    if (!c.tag.empty() && make.tag > 0.0f) {
        controls::caps(canvas, at + named + 5.0f * u, base, make.tag * u, faded(style::kAshInk, a), c.tag, 0.08f);
    }
    const float right = x + wide - 7.0f * u;
    if (c.dead) {
        controls::caps(canvas, right - controls::capsWidth(make.number * u, "Dead"), base, make.number * u, kDead, "Dead");
    } else if (!c.number.empty()) {
        controls::ranged(canvas, right, base, make.number * u, faded(c.away ? style::kBone2 : kNumber, a), c.number);
    }
    return tall + make.gap * u;
}

}  // namespace

void PartyBar::open(const gfx::Interface& interface) { interface.adopt(canvas_); }

void PartyBar::update(float seconds, const Play& play) {
    if (!play.isOpen()) {
        showing_ = false;
        return;
    }
    std::string me;
    play.playerName(play.realm().hero().id, &me, nullptr);
    const net::PartyState& party = play.party();
    std::vector<Raider> raid = raidOf(play);
    const float u = tip::unit();

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
        if (alive && share * 100.0f < kLow) moving = true;
    };
    for (const auto& member : party.members) {
        if (member.name != me) ease(member.name, float(member.health) / 100.0f, member.health > 0 && member.here != 0);
    }
    for (const Raider& one : raid) ease(one.name, float(one.health) / 100.0f, one.health > 0);
    clock_ = moving ? clock_ + seconds : 0.0f;

    if (same(party, drawn_) && raid == raid_ && me == me_ && u == unit_ && !moving) {
        return;  // redrawn on a change only, or while something moves
    }
    drawn_ = party;
    raid_ = std::move(raid);
    me_ = me;
    unit_ = u;
    canvas_.clear();
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
    // Who leads: the server's first member (net::PartyState); in the --raid harness, the hero,
    // whose party it is (Play::raidParty's first).
    const std::string partyLeader = party.members.empty() ? std::string()
                                    : party.members.front().name == me ? "You"
                                                                       : party.members.front().name;
    if (!others.empty()) y = heading(canvas_, x + 2.0f * u, y + 2.0f * u, u, "Party", partyLeader);
    for (const auto* member : others) {
        Capsule c;
        c.name = member->name;
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
        y += capsule(canvas_, x, y, u, kPlate, c, pulse);
    }
    if (raid_.empty()) return;

    // The rest of the raid: a heading, then a line each.
    if (!others.empty()) y += 8.0f * u;
    // The raid's leader: the harness's raid is the hero's; on a server, his party's leader while
    // the raid is one party and its strangers (no raid of parties on the server yet).
    const std::string raidLeader = play.realm().raiderCount() > 0 ? "You" : partyLeader;
    y = heading(canvas_, x + 2.0f * u, y, u, "Raid " + std::to_string(raid_.size() + others.size() + 1),
                raidLeader);
    y += 2.0f * u;
    const size_t shown = raid_.size() > kRaidMost ? kRaidMost - 1 : raid_.size();
    for (size_t i = 0; i < shown; ++i) {
        const Raider& one = raid_[i];
        Capsule c;
        c.name = one.name;
        c.dead = one.health == 0;
        c.number = std::to_string(one.health) + "%";
        c.shield = one.shield >= 0 ? float(one.shield) / 100.0f : -1.0f;
        c.life = float(one.health) / 100.0f;
        c.mana = one.mana >= 0 ? float(one.mana) / 100.0f : -1.0f;
        c.trail = trailOf(one.name);
        y += capsule(canvas_, x, y, u, kLine, c, pulse);
    }
    if (shown < raid_.size()) {
        controls::caps(canvas_, x + 8.0f * u, y + 10.0f * u, 11.0f * u, style::kAshInk,
                       "+" + std::to_string(raid_.size() - shown) + " more", 0.2f);
    }
}

}  // namespace mu::game
