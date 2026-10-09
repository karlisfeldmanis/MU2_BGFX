#include "game/ui/party_bar.h"

#include <algorithm>
#include <cmath>

#include "game/play.h"
#include "game/ui/controls.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"

namespace mu::game {

namespace {

uint32_t faded(uint32_t abgr, float alpha) {
    const float a = float(abgr >> 24) / 255.0f * std::clamp(alpha, 0.0f, 1.0f);
    return (abgr & 0xFFFFFFu) | (uint32_t(a * 255.0f + 0.5f) << 24);
}

// Measures in tip::unit(), a pixel at 1080 lines.
constexpr float kLeft = 22.0f, kTop = 26.0f, kRow = 54.0f, kWide = 220.0f;
constexpr float kName = 17.0f, kHealth = 6.0f, kMana = 3.0f;
constexpr uint32_t kGold = gfx::rgba(0.878f, 0.714f, 0.361f);      // the leader: the tracker's gold
constexpr uint32_t kWell = gfx::rgba(0.0f, 0.0f, 0.0f, 0.55f);       // under a bar
constexpr float kAway = 0.5f;  // a member on another map, this much of himself

bool same(const net::PartyState& a, const net::PartyState& b) {
    if (a.members.size() != b.members.size()) return false;
    for (size_t i = 0; i < a.members.size(); ++i) {
        const auto& x = a.members[i];
        const auto& y = b.members[i];
        if (x.name != y.name || x.level != y.level || x.world != y.world || x.here != y.here ||
            x.health != y.health || x.mana != y.mana) {
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
    const float u = tip::unit();
    if (same(party, drawn_) && me == me_ && u == unit_) return;  // redrawn on a change only
    drawn_ = party;
    me_ = me;
    unit_ = u;
    canvas_.clear();
    std::vector<const net::PartyState::Member*> others;
    for (const auto& member : party.members) {
        if (member.name != me) others.push_back(&member);
    }
    showing_ = !others.empty();
    if (!showing_) return;

    // The tracker's cloud, its middle on the screen's left edge by the rows.
    const float tall = float(others.size()) * kRow * u;
    const float cy = kTop * u + tall * 0.5f, sx = 300.0f * u, sy = std::max(60.0f * u, tall * 0.5f);
    constexpr int kColumns = 8, kRows = 10;
    const auto at = [&](int i, int j) {
        const float gx = 3.0f * float(i) / kColumns, gy = -3.0f + 6.0f * float(j) / kRows;
        return faded(style::kVoid, 0.7f * std::exp(-0.5f * (gx * gx + gy * gy)));
    };
    for (int j = 0; j < kRows; ++j) {
        for (int i = 0; i < kColumns; ++i) {
            canvas_.shade({sx * 3.0f * float(i) / kColumns, cy + sy * (-3.0f + 6.0f * float(j) / kRows),
                           sx * 3.0f / kColumns, sy * 6.0f / kRows},
                          at(i, j), at(i + 1, j), at(i + 1, j + 1), at(i, j + 1));
        }
    }

    float y = kTop * u;
    for (const auto* member : others) {
        const bool leader = member == &party.members.front();
        const float a = member->here != 0 ? 1.0f : kAway;
        const float x = kLeft * u, base = y + kName * u;
        controls::label(canvas_, x, base, kName * u, faded(leader ? kGold : style::kBoneHi, a), member->name);
        const std::string right =
            member->here != 0 ? "Lv " + std::to_string(member->level) : mapName(member->world);
        controls::ranged(canvas_, x + kWide * u, base, 14.0f * u, faded(style::kAshInk, a), right);
        const float barY = base + 7.0f * u;
        canvas_.rect({x, barY, kWide * u, kHealth * u}, kWell);
        canvas_.rect({x, barY, kWide * u * float(member->health) / 100.0f, kHealth * u}, faded(style::kLife, a));
        canvas_.rect({x, barY + (kHealth + 2.0f) * u, kWide * u, kMana * u}, kWell);
        canvas_.rect({x, barY + (kHealth + 2.0f) * u, kWide * u * float(member->mana) / 100.0f, kMana * u},
                     faded(style::kMana, a));
        y += kRow * u;
    }
}

}  // namespace mu::game
