#include "game/ui/party_bar.h"

#include <algorithm>
#include <cmath>

#include "game/play.h"
#include "game/ui/controls.h"
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
// A member's plate: padded round a name and three bars -- SD, health, mana (the user,
// 2026-10-10: "we need to show also shield value because that one of the main stats").
constexpr float kPlate = 50.0f, kPlateGap = 6.0f, kPad = 10.0f, kRadius = 6.0f;
constexpr float kName = 16.0f, kShield = 3.0f, kHealth = 6.0f, kMana = 3.0f, kBarGap = 1.5f;
// A raider's line: a name and three hairlines stacked, SD over health over mana (the user:
// "we need to show mana also in raids").
constexpr float kLine = 24.0f, kLineGap = 3.0f, kLineRadius = 4.0f, kLineName = 13.0f;
constexpr float kLineShield = 2.0f, kLineHealth = 4.0f, kLineMana = 2.0f, kLineBarGap = 1.0f;
constexpr float kLineBarLeft = 108.0f;  // where the bar starts, past the longest names
constexpr size_t kRaidMost = 15;          // then "+N more"
constexpr uint32_t kGold = gfx::rgba(0.878f, 0.714f, 0.361f);      // the leader: the tracker's gold
constexpr uint32_t kWell = gfx::rgba(0.0f, 0.0f, 0.0f, 0.55f);       // under a bar
constexpr uint32_t kPlateInk = gfx::rgba(0.020f, 0.012f, 0.012f, 0.62f);  // style::kVoid, see-through
constexpr uint32_t kLow = gfx::rgba(0.878f, 0.341f, 0.239f);         // a raider under 30%
constexpr uint32_t kSd = gfx::rgba(0.906f, 0.592f, 0.231f);          // SD: the HUD's amber bar
constexpr float kAway = 0.5f;  // a member on another map, this much of himself
constexpr float kDead = 0.45f;

bool same(const net::PartyState& a, const net::PartyState& b) {
    if (a.members.size() != b.members.size()) return false;
    for (size_t i = 0; i < a.members.size(); ++i) {
        const auto& x = a.members[i];
        const auto& y = b.members[i];
        if (x.name != y.name || x.level != y.level || x.world != y.world || x.here != y.here ||
            x.health != y.health || x.mana != y.mana || x.shield != y.shield) {
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

}  // namespace

void PartyBar::open(const gfx::Interface& interface) { interface.adopt(canvas_); }

void PartyBar::update(const Play& play) {
    if (!play.isOpen()) {
        showing_ = false;
        return;
    }
    std::string me;
    play.playerName(play.realm().hero().id, &me, nullptr);
    const net::PartyState& party = play.party();
    std::vector<Raider> raid = raidOf(play);
    const float u = tip::unit();
    if (same(party, drawn_) && raid == raid_ && me == me_ && u == unit_) {
        return;  // redrawn on a change only
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
    if (!showing_) return;

    const float x = kLeft * u, wide = kWide * u;
    float y = (540.0f - kAboveMiddle) * u;  // the unit is 1080 lines' pixel
    // Who leads: the server's first member (net::PartyState); in the --raid harness, the hero,
    // whose party it is (Play::raidParty's first).
    const std::string partyLeader = party.members.empty() ? std::string()
                                    : party.members.front().name == me ? "You"
                                                                       : party.members.front().name;
    if (!others.empty()) y = heading(canvas_, x + 2.0f * u, y, u, "Party", partyLeader);
    for (const auto* member : others) {
        const bool leader = member == &party.members.front();
        const float a = member->here != 0 ? 1.0f : kAway;
        const float r[4] = {kRadius * u, kRadius * u, kRadius * u, kRadius * u};
        tip::rounded(canvas_, {x, y, wide, kPlate * u}, r, faded(kPlateInk, a));
        const float left = x + kPad * u, inner = wide - 2.0f * kPad * u, base = y + (kPad + 13.0f) * u;
        float nameX = left;
        if (leader) {
            crown(canvas_, left, base - 2.0f * u, u, faded(kGold, a));
            nameX += 17.0f * u;
        }
        controls::label(canvas_, nameX, base, kName * u, faded(leader ? kGold : style::kBoneHi, a), member->name);
        const std::string right =
            member->here != 0 ? "Lv " + std::to_string(member->level) : mapName(member->world);
        controls::ranged(canvas_, left + inner, base, 14.0f * u, faded(style::kAshInk, a), right);
        // SD over health over mana; with no SD to show, its row stays empty so plates line up.
        float barY = base + 6.0f * u;
        const auto bar = [&](float tall, float share, uint32_t ink) {
            canvas_.rect({left, barY, inner, tall * u}, kWell);
            canvas_.rect({left, barY, inner * share, tall * u}, faded(ink, a));
            barY += (tall + kBarGap) * u;
        };
        if (member->shield != net::PartyState::kNoShield) bar(kShield, float(member->shield) / 100.0f, kSd);
        else barY += (kShield + kBarGap) * u;
        bar(kHealth, float(member->health) / 100.0f, style::kLife);
        bar(kMana, float(member->mana) / 100.0f, style::kMana);
        y += (kPlate + kPlateGap) * u;
    }
    if (raid_.empty()) return;

    // The rest of the raid: a heading, then a line each.
    if (!others.empty()) y += 8.0f * u;
    // The raid's leader: the harness's raid is the hero's; on a server, his party's leader while
    // the raid is one party and its strangers (no raid of parties on the server yet).
    const std::string raidLeader = play.realm().raiderCount() > 0 ? "You" : partyLeader;
    y = heading(canvas_, x + 2.0f * u, y, u, "Raid " + std::to_string(raid_.size() + others.size() + 1),
                raidLeader);
    const float r[4] = {kLineRadius * u, kLineRadius * u, kLineRadius * u, kLineRadius * u};
    const size_t shown = raid_.size() > kRaidMost ? kRaidMost - 1 : raid_.size();
    for (size_t i = 0; i < shown; ++i) {
        const Raider& one = raid_[i];
        const float a = one.health > 0 ? 1.0f : kDead;
        tip::rounded(canvas_, {x, y, wide, kLine * u}, r, faded(kPlateInk, a * 0.85f));
        controls::label(canvas_, x + 8.0f * u, y + 15.5f * u, kLineName * u, faded(style::kBone2, a), one.name);
        const float barX = x + kLineBarLeft * u, barW = wide - kLineBarLeft * u - 8.0f * u;
        float barY = y + (kLine - kLineShield - kLineHealth - kLineMana - 2.0f * kLineBarGap) * 0.5f * u;
        const auto bar = [&](float tall, int share, uint32_t ink) {
            if (share >= 0) {
                canvas_.rect({barX, barY, barW, tall * u}, kWell);
                canvas_.rect({barX, barY, barW * float(share) / 100.0f, tall * u}, faded(ink, a));
            }
            barY += (tall + kLineBarGap) * u;
        };
        bar(kLineShield, one.shield, kSd);
        bar(kLineHealth, one.health, one.health < 30 ? kLow : style::kLife);
        bar(kLineMana, one.mana, style::kMana);
        y += (kLine + kLineGap) * u;
    }
    if (shown < raid_.size()) {
        controls::caps(canvas_, x + 8.0f * u, y + 12.0f * u, 11.0f * u, style::kAshInk,
                       "+" + std::to_string(raid_.size() - shown) + " more", 0.2f);
    }
}

}  // namespace mu::game
