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
// 200 wide, from 240 (the user, 2026-10-10: "label is to wide").
constexpr float kLeft = 16.0f, kAboveMiddle = 188.0f, kWide = 200.0f;
constexpr size_t kRaidMost = 15;  // raid lines, then "+N more"

// A capsule's make: its three bands top to foot, its corner, the gap under it, and its words.
// The gaps 12 and 8 from 7 and 4 (the user, 2026-10-10, "increae gaps between labels"), and the
// rest from "work on paddings,lineheight,spacing,colors,gradients, shadows,outlines" the same day.
struct Make {
    float shield, life, mana, radius, gap;
    float name, tag, number;  // the name's size, the level tag's, the number's
    float pad;                // the words in from the capsule's ends
    float cell;               // the class's letters in their cell
    float band;               // the dark band behind the name, a share of the width
};
constexpr Make kPlate{3.0f, 21.0f, 4.0f, 6.0f, 11.0f, 14.5f, 9.0f, 12.5f, 10.0f, 12.0f, 0.66f};  // a party member
// A raid line a size under a plate: "raid label sizes i think can bi bigger" (the user, the same day).
constexpr Make kLine{3.0f, 19.0f, 3.5f, 5.5f, 8.0f, 13.5f, 0.0f, 11.5f, 9.0f, 11.0f, 0.62f};    // a raider
constexpr float kBetween = 16.0f;  // between the party and the rest of the raid, with no heading
constexpr uint32_t kGold = gfx::rgba(0.878f, 0.714f, 0.361f);  // the leader: the tracker's gold
constexpr uint32_t kWellTop = gfx::rgba(0.010f, 0.006f, 0.006f, 0.86f);  // the empty capsule,
constexpr uint32_t kWellFoot = gfx::rgba(0.040f, 0.026f, 0.022f, 0.80f); // a touch lighter at its foot
constexpr uint32_t kRim = gfx::rgba(0.0f, 0.0f, 0.0f, 0.72f);             // a hairline round it
constexpr uint32_t kBand = gfx::rgba(0.071f, 0.016f, 0.016f, 0.86f);      // behind the name, fading out
constexpr uint32_t kShadow = gfx::rgba(0.0f, 0.0f, 0.0f, 0.22f);          // under the capsule
constexpr uint32_t kSeam = gfx::rgba(0.0f, 0.0f, 0.0f, 0.55f);            // between the bands
constexpr uint32_t kSheen = gfx::rgba(1.0f, 0.93f, 0.85f, 0.14f);         // the health's upper light
constexpr uint32_t kGleam = gfx::rgba(1.0f, 0.95f, 0.88f, 0.10f);         // the capsule's top edge
// The fills, each a top and a foot.
constexpr uint32_t kShieldTop = gfx::rgba(1.0f, 0.780f, 0.420f), kShieldFoot = gfx::rgba(0.851f, 0.525f, 0.180f);
constexpr uint32_t kLifeTop = gfx::rgba(0.800f, 0.200f, 0.157f), kLifeFoot = gfx::rgba(0.420f, 0.071f, 0.055f);
constexpr uint32_t kLowTop = gfx::rgba(0.937f, 0.388f, 0.259f), kLowFoot = gfx::rgba(0.588f, 0.145f, 0.086f);
constexpr uint32_t kAwayTop = gfx::rgba(0.459f, 0.380f, 0.365f), kAwayFoot = gfx::rgba(0.255f, 0.204f, 0.196f);
constexpr uint32_t kManaTop = gfx::rgba(0.302f, 0.553f, 0.957f), kManaFoot = gfx::rgba(0.118f, 0.267f, 0.612f);
constexpr uint32_t kTrail = gfx::rgba(0.965f, 0.784f, 0.682f, 0.80f);  // health just lost
constexpr uint32_t kPulse = gfx::rgba(1.0f, 0.478f, 0.333f);           // under 30%, breathing
constexpr uint32_t kDead = gfx::rgba(0.878f, 0.400f, 0.349f);          // the word
constexpr uint32_t kNumber = gfx::rgba(0.980f, 0.945f, 0.890f);
constexpr uint32_t kTag = gfx::rgba(0.788f, 0.733f, 0.659f, 0.85f);    // the level after the name
// The class's letters in its cell (the user, 2026-10-10: "we need to find place ehre to show
// class"): MU's own short names, as the item card's requirement rows say them, each in a muted
// tint of its own so the elf and the tank are found at a glance.
constexpr const char* kKinShort[] = {"DW", "FE", "DK", "MG"};
constexpr uint32_t kKinInk[] = {
    gfx::rgba(0.627f, 0.757f, 1.0f),    // Dark Wizard, a cold blue
    gfx::rgba(0.682f, 0.918f, 0.604f),  // Fairy Elf, a leaf green
    gfx::rgba(1.0f, 0.651f, 0.529f),    // Dark Knight, rust
    gfx::rgba(0.859f, 0.714f, 1.0f),    // Magic Gladiator, violet
};
constexpr uint32_t kCell = gfx::rgba(0.0f, 0.0f, 0.0f, 0.55f);  // the class cell, darker than the band
constexpr uint32_t kCrownDrop = gfx::rgba(0.0f, 0.0f, 0.0f, 0.85f);  // under the leader's badge
// The menu (PartyBar::Menu): a sheet of the windows' ash, a row lit under the pointer.
constexpr float kMenuWide = 150.0f, kMenuRow = 26.0f, kMenuHead = 24.0f, kMenuPad = 12.0f;
constexpr uint32_t kMenuSheet = gfx::rgba(0.051f, 0.039f, 0.031f, 0.97f);
constexpr uint32_t kMenuLit = gfx::rgba(1.0f, 0.94f, 0.86f, 0.07f);
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

// The leader's mark: a small crown in the leader's gold, three points over a band, drawn from
// rects (no art for it in MU's interface). `x`, `base`: its left and its foot, `s` the unit.
void crown(gfx::Canvas& canvas, float x, float base, float s, uint32_t ink) {
    canvas.rect({x, base - 3.0f * s, 12.0f * s, 3.0f * s}, ink);
    canvas.rect({x, base - 9.0f * s, 2.5f * s, 6.0f * s}, ink);
    canvas.rect({x + 4.75f * s, base - 10.0f * s, 2.5f * s, 7.0f * s}, ink);
    canvas.rect({x + 9.5f * s, base - 9.0f * s, 2.5f * s, 6.0f * s}, ink);
}

// What one capsule says.
struct Capsule {
    std::string name;
    int kin = -1;        // sim::Kin, -1 for none
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
    const gfx::Box whole{x, y, wide, tall};
    // A soft shadow under it, a hairline rim, and the empty well inside.
    // Two dark rounded layers under it, dropped and grown a little: tip::shadowUnder's falloffs
    // are a card's, and under a see-through capsule they read as a grey box round it.
    for (const float grow : {3.0f, 1.5f}) {
        const float g = grow * u, rr[4] = {r + g, r + g, r + g, r + g};
        tip::rounded(canvas, {x - g, y - g + 1.5f * u, wide + 2.0f * g, tall + 2.0f * g}, rr,
                     faded(kShadow, a * (grow > 2.0f ? 0.5f : 1.0f)));
    }
    const float rim[4] = {r + u, r + u, r + u, r + u};
    tip::rounded(canvas, {x - u, y - u, wide + 2.0f * u, tall + 2.0f * u}, rim, faded(kRim, a));
    const float corners[4] = {r, r, r, r};
    tip::rounded(canvas, whole, corners, faded(kWellTop, a), faded(kWellFoot, a));
    // SD along the top, health the body, mana the foot.
    if (c.shield >= 0.0f) fill(canvas, {x, y, wide, sh}, c.shield, kShieldTop, kShieldFoot, a, r, 0.0f);
    const gfx::Box body{x, y + sh, wide, lh};
    const bool low = !c.dead && !c.away && c.life * 100.0f < kLow;
    fill(canvas, body, c.life, c.away ? kAwayTop : low ? kLowTop : kLifeTop,
         c.away ? kAwayFoot : low ? kLowFoot : kLifeFoot, a, 0.0f, 0.0f);
    if (c.trail > c.life + 0.002f) {
        canvas.rect({x + wide * c.life, body.y, wide * (std::min(c.trail, 1.0f) - c.life), lh}, faded(kTrail, a));
    }
    if (low && pulse > 0.0f) canvas.rect({x, body.y, wide * c.life, lh}, faded(kPulse, 0.42f * pulse));
    // The sheen over the health's upper half: light, fading down.
    if (c.life > 0.0f) {
        const float lit = wide * std::min(c.life, 1.0f);
        canvas.shade({x, body.y, lit, lh * 0.45f}, faded(kSheen, a), faded(kSheen, a), 0u, 0u);
    }
    if (c.mana >= 0.0f) fill(canvas, {x, y + sh + lh, wide, mh}, c.mana, kManaTop, kManaFoot, a, 0.0f, r);
    canvas.rect({x, body.y, wide, 0.75f * u}, kSeam);
    canvas.rect({x, body.y + lh - 0.75f * u, wide, 0.75f * u}, kSeam);
    canvas.rect({x + r, y, wide - 2.0f * r, 0.75f * u}, faded(kGleam, a));
    // The dark band behind the name, fading into the fill.
    const float band = wide * make.band;
    canvas.shade({x, body.y, band * 0.4f, lh}, faded(kBand, a), faded(kBand, a * 0.85f), faded(kBand, a * 0.85f),
                 faded(kBand, a));
    canvas.shade({x + band * 0.4f, body.y, band * 0.6f, lh}, faded(kBand, a * 0.85f), 0u, 0u, faded(kBand, a * 0.85f));

    // The class's cell at the left end, a square of the health's height.
    float at = x + make.pad * u;
    if (c.kin >= 0 && c.kin < 4) {
        canvas.rect({x, body.y, lh, lh}, faded(kCell, a));
        const float size = make.cell * u;
        const std::string& letters = kKinShort[c.kin];
        const float wideLetters = controls::capsWidth(size, letters, 0.06f);
        controls::caps(canvas, x + (lh - wideLetters) * 0.5f, controls::middle(body.y, lh, size), size,
                       faded(c.dead ? kGhost : kKinInk[c.kin], a), letters, 0.06f);
        at = x + lh + 6.0f * u;
    }
    // The leader's badge (the user, 2026-10-10: "we need clear icon who is the party leader"):
    // a crown a size up, standing on the capsule's top left corner over a dark drop.
    if (c.leader) {
        // Never dimmed: whoever leads is said in full, even from another map.
        const float s = make.name / 14.5f * 1.5f * u, bx = x - 4.0f * u, foot = y + 4.0f * u;
        crown(canvas, bx + 1.2f * u, foot + 1.5f * u, s, kCrownDrop);
        crown(canvas, bx, foot, s, kGold);
    }
    // The words, their capitals centred on the health.
    const float base = controls::middle(body.y, lh, make.name * u);
    const uint32_t ink = c.dead ? kGhost : c.leader ? kGold : style::kBoneHi;
    const float named = controls::label(canvas, at, base, make.name * u, c.leader ? ink : faded(ink, a), c.name);
    if (c.dead) canvas.rect({at, base - make.name * u * 0.3f, named, 1.0f * u}, faded(kGhost, a));
    if (!c.tag.empty() && make.tag > 0.0f) {
        controls::caps(canvas, at + named + 6.0f * u, base, make.tag * u, faded(kTag, a), c.tag, 0.1f);
    }
    const float right = x + wide - make.pad * u;
    const float numberBase = controls::middle(body.y, lh, make.number * u);
    if (c.dead) {
        controls::caps(canvas, right - controls::capsWidth(make.number * u, "Dead"), numberBase, make.number * u,
                       kDead, "Dead");
    } else if (!c.number.empty()) {
        controls::ranged(canvas, right, numberBase, make.number * u, faded(c.away ? style::kBone2 : kNumber, a), c.number);
    }
    return tall + make.gap * u;
}

}  // namespace

void PartyBar::open(const gfx::Interface& interface) { interface.adopt(canvas_); }

bool PartyBar::covers(float x, float y) const {
    if (!showing_) return false;
    if (menuOpen_ && menu_.box.has(x, y)) return true;
    for (const Hit& hit : hits_) {
        if (hit.box.has(x, y)) return true;
    }
    return false;
}

void PartyBar::update(float seconds, Play& play, const Pointer& pointer) {
    if (!play.isOpen()) {
        showing_ = false;
        menuOpen_ = false;
        return;
    }
    std::string me;
    play.playerName(play.realm().hero().id, &me, nullptr);
    const net::PartyState& party = play.party();
    std::vector<Raider> raid = raidOf(play);
    const float u = tip::unit();
    const bool inParty = party.members.size() > 1;
    const bool leading = inParty && party.members.front().name == me;

    // The menu: a row taken, or shut by a click anywhere else; a right click on a capsule opens
    // its own. Only on a server: the --raid harness's raiders are no one to whisper or invite.
    bool menuChanged = false;
    if (menuOpen_) {
        const int before = menu_.hovered;
        menu_.hovered = -1;
        const float top = menu_.box.y + kMenuHead * u;
        if (menu_.box.has(pointer.x, pointer.y) && pointer.y >= top) {
            menu_.hovered = std::min(int((pointer.y - top) / (kMenuRow * u)), int(menu_.rows.size()) - 1);
        }
        menuChanged = menu_.hovered != before;
        if (pointer.pressed || pointer.rightPressed) {
            if (pointer.pressed && menu_.hovered >= 0) {
                const std::string& row = menu_.rows[size_t(menu_.hovered)];
                if (row == "Whisper") whisper_ = menu_.name;
                else if (row == "Make leader") play.askParty({net::PartyWhat::Promote, menu_.name});
                else if (row == "Kick") play.askParty({net::PartyWhat::Kick, menu_.name});
                else if (row == "Invite to party") play.askParty({net::PartyWhat::Invite, menu_.name});
                else if (row == "Leave party") play.askParty({net::PartyWhat::Leave, std::string()});
            }
            if (!menu_.box.has(pointer.x, pointer.y) || pointer.pressed) {
                menuOpen_ = false;
                menuChanged = true;
                // A row taken or the menu shut clicks, as every window's button does (the user,
                // 2026-10-10: "we need also some UI sounds for right click menu").
                play.ui(Play::Ui::Click);
            }
        }
    }
    if (!menuOpen_ && pointer.rightPressed && play.remote()) {
        for (const Hit& hit : hits_) {
            if (!hit.box.has(pointer.x, pointer.y)) continue;
            menu_ = Menu{};
            menu_.name = hit.name;
            menu_.rows.push_back("Whisper");
            if (hit.member && leading) {
                menu_.rows.push_back("Make leader");
                menu_.rows.push_back("Kick");
            }
            if (!hit.member && (!inParty || leading)) menu_.rows.push_back("Invite to party");
            if (inParty) menu_.rows.push_back("Leave party");
            const float w = kMenuWide * u, h = (kMenuHead + kMenuRow * float(menu_.rows.size()) + 6.0f) * u;
            menu_.box = {hit.box.right() + 8.0f * u, std::max(4.0f * u, pointer.y - kMenuHead * u * 0.5f), w, h};
            menuOpen_ = true;
            menuChanged = true;
            play.ui(Play::Ui::Opened);  // as a window opens
            break;
        }
    }

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

    if (same(party, drawn_) && raid == raid_ && me == me_ && u == unit_ && !moving && !menuChanged) {
        return;  // redrawn on a change only, or while something moves
    }
    drawn_ = party;
    raid_ = std::move(raid);
    me_ = me;
    unit_ = u;
    canvas_.clear();
    hits_.clear();
    std::vector<const net::PartyState::Member*> others;
    for (const auto& member : party.members) {
        if (member.name != me) others.push_back(&member);
    }
    showing_ = !others.empty() || !raid_.empty();
    if (!showing_) {
        trail_.clear();
        menuOpen_ = false;
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
        crown(canvas_, x + 1.0f * u, base + 1.0f * u, s, kCrownDrop);
        crown(canvas_, x, base, s, kGold);
        const float named = controls::label(canvas_, x + 18.0f * u, base, 13.0f * u, kGold, "You");
        controls::label(canvas_, x + 18.0f * u + named + 4.0f * u, base, 13.0f * u, style::kBone2, "lead the party");
    }
    for (const auto* member : others) {
        Capsule c;
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
        const float tall = capsule(canvas_, x, y, u, kPlate, c, pulse);
        hits_.push_back({{x, y, kWide * u, tall - kPlate.gap * u}, member->name, true});
        y += tall;
    }

    // The rest of the raid, a line each, a little apart from the party; no heading (the user,
    // 2026-10-10: "get rid of party header": the leader's crown is in his capsule).
    if (!raid_.empty()) {
        if (!others.empty()) y += (kBetween - kPlate.gap) * u;
        const size_t shown = raid_.size() > kRaidMost ? kRaidMost - 1 : raid_.size();
        for (size_t i = 0; i < shown; ++i) {
            const Raider& one = raid_[i];
            Capsule c;
            c.name = one.name;
            c.kin = one.kin;
            c.dead = one.health == 0;
            c.number = std::to_string(one.health) + "%";
            c.shield = one.shield >= 0 ? float(one.shield) / 100.0f : -1.0f;
            c.life = float(one.health) / 100.0f;
            c.mana = one.mana >= 0 ? float(one.mana) / 100.0f : -1.0f;
            c.trail = trailOf(one.name);
            const float tall = capsule(canvas_, x, y, u, kLine, c, pulse);
            hits_.push_back({{x, y, kWide * u, tall - kLine.gap * u}, one.name, false});
            y += tall;
        }
        if (shown < raid_.size()) {
            controls::caps(canvas_, x + 8.0f * u, y + 10.0f * u, 11.0f * u, style::kAshInk,
                           "+" + std::to_string(raid_.size() - shown) + " more", 0.2f);
        }
    }

    // The menu over it all: the name as its head, then a row each, the danger's in its ink.
    if (menuOpen_) {
        bool still = false;
        for (const Hit& hit : hits_) still = still || hit.name == menu_.name;
        if (!still) {
            menuOpen_ = false;  // he left, or the raid ended
            return;
        }
        const gfx::Box& b = menu_.box;
        const float r[4] = {6.0f * u, 6.0f * u, 6.0f * u, 6.0f * u};
        for (const float grow : {4.0f, 2.0f}) {
            const float g = grow * u, rr[4] = {r[0] + g, r[0] + g, r[0] + g, r[0] + g};
            tip::rounded(canvas_, {b.x - g, b.y - g + 2.0f * u, b.w + 2.0f * g, b.h + 2.0f * g}, rr, kShadow);
        }
        tip::rounded(canvas_, b, r, kMenuSheet);
        controls::caps(canvas_, b.x + kMenuPad * u, b.y + 16.0f * u, 10.5f * u, style::kAshInk, menu_.name, 0.14f);
        for (size_t i = 0; i < menu_.rows.size(); ++i) {
            const float top = b.y + (kMenuHead + kMenuRow * float(i)) * u;
            if (int(i) == menu_.hovered) canvas_.rect({b.x + 3.0f * u, top, b.w - 6.0f * u, kMenuRow * u}, kMenuLit);
            const std::string& row = menu_.rows[i];
            const bool danger = row == "Kick" || row == "Leave party";
            controls::label(canvas_, b.x + kMenuPad * u, controls::middle(top, kMenuRow * u, 13.5f * u), 13.5f * u,
                            danger ? style::kDanger : style::kBoneHi, row);
        }
    }
}

}  // namespace mu::game
