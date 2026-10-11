#include "game/ui/target_frame.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "game/play.h"
#include "game/ui/capsule.h"
#include "game/ui/controls.h"
#include "game/ui/panel.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"
#include "sim/items.h"
#include "sim/raid.h"
#include "sim/realm_tuning.h"
#include "sim/skills.h"

namespace mu::game {
namespace {

// Measures in tip::unit(), a pixel at 1080 lines (the proposal's, at its own scale).
constexpr float kFace = 72.0f;   // the portrait's width
constexpr float kRing = 3.0f;    // the dark ring round it
constexpr float kWide = 286.0f;  // the capsule
constexpr float kTuck = 14.0f;   // how far the capsule runs in under the face
constexpr float kCell = 22.0f;   // an aura's cell (the user: "little bit smaller", from 28)
constexpr float kCellGap = 3.0f;
constexpr float kCellsOver = 6.0f;  // between the row and the capsule
constexpr float kSubDown = 17.0f;   // the line under it, its baseline under the capsule's foot
constexpr float kSub = 13.0f;       // its words
constexpr int kMostCells = 10;
constexpr float kMore = 20.0f, kMoreGap = 6.0f;  // the menu's button, and its gap from the capsule
// The capsule, a size over a party plate's: SD and mana the same thin line (the user, 2026-10-10:
// "SD has to be just line same as mana"), the name clear of the face's tuck.
constexpr capsule::Make kMake{5.0f, 26.0f, 5.0f, 6.0f, 0.0f, 15.5f, 0.0f, 13.0f, kTuck + 8.0f, 0.0f, 0.62f};

constexpr uint32_t kTagBack = gfx::rgba(0.043f, 0.039f, 0.035f, 0.94f);
constexpr uint32_t kCellBack = gfx::rgba(0.0f, 0.0f, 0.0f, 0.45f);
constexpr uint32_t kCellEdge = gfx::rgba(0.627f, 0.549f, 0.373f, 0.55f);  // the HUD strip's
constexpr uint32_t kBaneEdge = gfx::rgba(0.784f, 0.188f, 0.173f, 0.95f);  // a debuff's red
constexpr uint32_t kGone = gfx::rgba(0.0f, 0.0f, 0.0f, 0.58f);           // the clock's dark
constexpr uint32_t kGuild = style::kBone;
constexpr uint32_t kBracket = style::kAshInk;
constexpr uint32_t kHitBy = gfx::rgba(0.784f, 0.188f, 0.173f);
constexpr uint32_t kMiniWell = gfx::rgba(0.353f, 0.086f, 0.078f, 0.9f);
constexpr uint32_t kMiniFill = gfx::rgba(0.722f, 0.196f, 0.184f);
constexpr float kPulseRate = 5.7f;
constexpr float kReach = 20.0f;  // tiles: farther than this and the target is let go
// The whole frame a little under the proposal's size (the user, 2026-10-10: "sclae crop label
// little bit and add some shadow background behind").
constexpr float kScale = 0.86f;
// The soft dark behind it, at its middle; it fades to nothing at its rim.
constexpr uint32_t kBackdrop = gfx::rgba(0.0f, 0.0f, 0.0f, 0.55f);

std::string grouped(int32_t n) { return panel::grouped(n); }

// What stands on another body, off its own fields: the guard a skill raised, the elf's might,
// the Ale, a Frenzy, and the three debuffs. Debuffs first, as the frame's row has them. The pet
// and the potions are the hero's satchel's and not a body's, so a target shows neither.
int aurasOf(const sim::Realm& realm, const sim::Body& body, Hud::Boon* out) {
    const int64_t now = realm.tick();
    int n = 0;
    const auto add = [&](int64_t until, int64_t whole) -> Hud::Boon* {
        if (until <= now || n >= kMostCells) return nullptr;
        Hud::Boon& one = out[n++];
        const float left = float(until - now);
        one.seconds = left * 0.05f;
        one.share = whole > 0 ? std::min(1.0f, left / float(whole)) : 0.0f;
        return &one;
    };
    if (Hud::Boon* one = add(body.poisonUntil, sim::kHeroPoisonTicks)) one->poison = true;
    if (body.dragonfire > 0) {
        if (Hud::Boon* one = add(body.dragonfireUntil, sim::kDragonfireTicks)) {
            one->dragonfire = true;
            one->stacks = body.dragonfire;
        }
    }
    if (Hud::Boon* one = add(body.chilledUntil, sim::kHeroChillTicks)) one->chill = true;
    if (body.boonSkill != 0) {
        const sim::SkillRow* row = sim::skillNumbered(body.boonSkill);
        if (Hud::Boon* one = add(body.boonUntil, row ? row->boonTicks : 0)) one->skill = body.boonSkill;
    }
    {
        const sim::SkillRow* row = sim::skillNumbered(sim::skill::kGreaterDamage);
        if (Hud::Boon* one = add(body.mightUntil, row ? row->mightTicks : 0)) one->skill = sim::skill::kGreaterDamage;
    }
    if (Hud::Boon* one = add(body.aleUntil, sim::kAleTicks)) one->ale = true;
    if (Hud::Boon* one = add(body.frenzyUntil, sim::kFrenzyTicks)) {
        one->frenzy = true;
        one->stacks = body.frenzyStacks;
    }
    return n;
}

// The monster hitting him: of those fighting him, the nearest.
const sim::Body* hitterOf(const sim::Realm& realm, const sim::Body& body) {
    const sim::Body* best = nullptr;
    float nearest = 1e9f;
    for (const sim::Body& one : realm.bodies()) {
        if (!one.monster() || !one.alive() || one.quarry != body.id) continue;
        if (one.temper != sim::Temper::Fighting && one.temper != sim::Temper::Chasing) continue;
        const float away = std::hypot(one.x - body.x, one.y - body.y);
        if (away < nearest) {
            nearest = away;
            best = &one;
        }
    }
    return best;
}

}  // namespace

void TargetFrame::update(float seconds, Play& play, const Pointer& pointer, panel::Arts& arts, float width,
                         float bottom, const gfx::Art& face, bool faceTaken) {
    took_ = false;
    canvas_.clear();
    showing_ = false;
    if (!play.isOpen() || play.targetName().empty()) {
        killer_.clear();
        return;
    }
    const sim::Realm& realm = play.realm();
    const std::string name = play.targetName();
    if (name != name_) killer_.clear();
    name_ = name;

    // Who he is: a member of the party wherever he is, else a body here that is still him.
    const net::PartyState& party = play.party();
    const net::PartyState::Member* member = nullptr;
    for (const auto& one : party.members) {
        if (one.name == name) member = &one;
    }
    // The party's word for where he is comes once a second; the body a click found is good
    // while it is still him.
    uint32_t id = member && member->here != 0 ? member->here : play.targetId();
    std::string named;
    bool bot = false;
    const sim::Body* body = id != 0 ? realm.find(id) : nullptr;
    if (body && (!body->player || body->gone || !play.playerName(id, &named, &bot) || named != name)) body = nullptr;
    // Too far, or on another map: let go of him, and the frame goes (the user, 2026-10-10: "if
    // distance is longer than need char label has to disepear"). About the screen's reach.
    const sim::Body& hero = realm.hero();
    if (!body || std::hypot(body->x - hero.x, body->y - hero.y) > kReach) {
        play.untarget();
        return;
    }
    if (id != play.targetId()) play.target(name, id);
    if (member) bot = bot || member->bot;
    const bool dead = !play.shownAlive(id);
    const bool leader = member && member == &party.members.front() && party.members.size() > 1;

    const float u = tip::unit() * kScale;
    // His numbers: off the body here, off the party's state from another map.
    capsule::Look look;
    look.name = name;
    look.crown = false;
    look.leader = leader;
    look.dead = dead;
    const int kin = int(body->kin);
    const int level = body->level;
    {
        const int32_t health = play.shownHealth(id);
        look.life = body->maxHealth > 0 ? std::clamp(float(health) / float(body->maxHealth), 0.0f, 1.0f) : 0.0f;
        look.shield = body->maxSd > 0 ? std::clamp(float(body->sd) / float(body->maxSd), 0.0f, 1.0f) : -1.0f;
        look.mana = body->maxMana > 0 ? std::clamp(float(body->mana) / float(body->maxMana), 0.0f, 1.0f) : -1.0f;
        look.number = grouped(std::max(0, health)) + " / " + grouped(body->maxHealth);
    }
    if (dead) look.life = look.mana = look.shield = 0.0f;
    const bool low = !dead && look.life * 100.0f < capsule::kLow;
    clock_ = low ? clock_ + seconds : 0.0f;
    const float pulse = 0.5f - 0.5f * std::cos(clock_ * kPulseRate);

    // What hits him, remembered while he stands for the "Killed by" he falls to.
    const sim::Body* hitter = !dead ? hitterOf(realm, *body) : nullptr;
    if (hitter && realm.tables()) killer_ = realm.tables()->kinds[size_t(hitter->kind)].label;

    // ---- laid out from the foot up, in the screen's middle ----
    const float total = (kFace - kTuck + kWide) * u;
    const float left = std::round((width - total) * 0.5f);
    const float tall = (kMake.shield + kMake.life + kMake.mana) * u;
    const float capTop = std::round(bottom - kSubDown * u - 4.0f * u - tall);
    const float capX = left + (kFace - kTuck) * u;
    const float cellsTop = capTop - (kCellsOver + kCell) * u;
    const float r = kFace * 0.5f * u;
    const float cx = left + r, cy = capTop + tall * 0.5f;
    // The button for his menu at the capsule's right end, on a server only: off one there is
    // no one to whisper or invite (the user, 2026-10-11: "lets not use right click but add some
    // simple button which triggers that right click menu").
    const bool menuable = play.remote();
    const gfx::Box more{capX + (kWide + kMoreGap) * u, capTop, kMore * u, tall};
    const float boxRight = menuable ? more.right() : left + total;
    box_ = {left - kRing * u, std::min(cellsTop, cy - r - kRing * u), boxRight - left + kRing * u,
            bottom - std::min(cellsTop, cy - r - kRing * u)};

    // The pointer: a press on the button asks for his menu beside it, any press on it is the frame's.
    const bool onMore = menuable && more.has(pointer.x, pointer.y);
    if (onMore && pointer.pressed) {
        menuAsk_ = true;
        menuX_ = more.right() + 6.0f * u;
        menuY_ = more.y;
        took_ = true;
    } else if (box_.has(pointer.x, pointer.y) && pointer.pressed) {
        took_ = true;
    }
    showing_ = true;

    // A soft dark behind it, so it reads over a bright floor: an ellipse darkest at its middle
    // and gone at its rim, with no edge anywhere (the user: "shadow has to be more elegant, not
    // like container"). Wider than the frame, as the tracker's cloud runs past its words.
    {
        const float top = std::min(cellsTop, cy - r);
        const float ex = left + total * 0.5f, ey = (top + bottom) * 0.5f;
        const float rx = total * 0.5f + 70.0f * u, ry = (bottom - top) * 0.5f + 34.0f * u;
        constexpr int kSides = 48;
        float xy[2 * (kSides + 2)];
        uint32_t ink[kSides + 2];
        xy[0] = ex;
        xy[1] = ey;
        ink[0] = kBackdrop;
        for (int i = 0; i <= kSides; ++i) {
            const float a = float(i) / float(kSides) * 6.2831853f;
            xy[2 * (i + 1)] = ex + std::cos(a) * rx;
            xy[2 * (i + 1) + 1] = ey + std::sin(a) * ry;
            ink[i + 1] = 0u;
        }
        canvas_.polygon(xy, ink, kSides + 2);
    }

    capsule::draw(canvas_, capX, capTop, u, kWide * u, kMake, look, pulse);
    if (menuable) capsule::more(canvas_, more, u, onMore);

    // The face over the capsule's end: a dark ring, the well, and him -- greyed when he lies
    // dead, his class's letters in the well before his first picture.
    capsule::face(canvas_, cx, cy, r, kRing * u, faceTaken ? &face : nullptr, kin, dead, false);
    const float tagFade = dead ? 0.55f : 1.0f;
    // His class on the face's shoulder, in the party bar's tints.
    if (kin >= 0 && kin < 4) {
        const float size = 10.5f * u, w = 24.0f * u, h = 16.0f * u;
        const gfx::Box tag{cx + r * 0.62f - w * 0.5f, cy - r * 0.86f - h * 0.5f, w, h};
        const float rr[4] = {3.0f * u, 3.0f * u, 3.0f * u, 3.0f * u};
        tip::rounded(canvas_, tag, rr, kTagBack);
        const std::string letters = capsule::kKinShort[kin];
        controls::caps(canvas_, tag.midX() - controls::capsWidth(size, letters, 0.06f) * 0.5f,
                       controls::middle(tag.y, tag.h, size), size,
                       capsule::faded(dead ? style::kAshInk : capsule::kKinInk[kin], tagFade), letters, 0.06f);
    }
    // His level on a tag under the face.
    {
        const std::string figure = std::to_string(level);
        const float size = 12.5f * u;
        const float w = std::max(30.0f * u, controls::labelWidth(size, figure) + 12.0f * u), h = 17.0f * u;
        const gfx::Box tag{cx - w * 0.5f, cy + r - h * 0.5f, w, h};
        const float rr[4] = {3.0f * u, 3.0f * u, 3.0f * u, 3.0f * u};
        tip::rounded(canvas_, tag, rr, kTagBack);
        controls::label(canvas_, tag.midX() - controls::labelWidth(size, figure) * 0.5f,
                        controls::middle(tag.y, tag.h, size), size,
                        capsule::faded(dead ? style::kAshInk : style::kBoneHi, tagFade), figure);
    }
    // The leader's crown over the face.
    if (leader) {
        const float s = 1.5f * u, cw = 12.0f * s, base = cy - r - 1.0f * u;
        capsule::crown(canvas_, cx - cw * 0.5f + 1.2f * u, base + 1.5f * u, s, capsule::kCrownDrop);
        capsule::crown(canvas_, cx - cw * 0.5f, base, s, dead ? capsule::faded(capsule::kGold, 0.5f) : capsule::kGold);
    }

    // One row of what stands on him, debuffs first, each cell's time its clock.
    if (!dead) {
        Hud::Boon cells[kMostCells];
        const int count = aurasOf(realm, *body, cells);
        float x = capX + (kTuck + 6.0f) * u;
        for (int i = 0; i < count; ++i) {
            const Hud::Boon& one = cells[i];
            const gfx::Box cell{x, cellsTop, kCell * u, kCell * u};
            canvas_.rect(cell, kCellBack);
            const gfx::Art& icon = arts.get(Hud::artOf(one));
            if (icon.valid()) canvas_.image(icon, cell);
            controls::sweep(canvas_, cell, 1.0f - one.share, kGone);
            if ((one.frenzy || one.dragonfire) && one.stacks > 1) {
                const std::string figure = std::to_string(one.stacks);
                const float size = 11.0f * u;
                canvas_.shadowed(cell.right() - 2.0f * u, cell.bottom() - 2.0f * u, size, style::kBoneHi,
                                 gfx::rgba(0.0f, 0.0f, 0.0f, 0.9f), std::max(1.0f, u), figure, gfx::Align::Right, 0.0f);
            }
            canvas_.outline(cell, one.debuff() ? std::max(1.0f, 1.5f * u) : std::max(1.0f, u),
                            one.debuff() ? kBaneEdge : kCellEdge);
            x += (kCell + kCellGap) * u;
        }
    }

    // Under it: his guild at the left, the monster on him at the right.
    const float base = capTop + tall + kSubDown * u;
    if (bot) {
        float x = capX + (kTuck + 8.0f) * u;
        x += controls::label(canvas_, x, base, kSub * u, kBracket, "<");
        x += controls::label(canvas_, x, base, kSub * u, dead ? style::kAshInk : kGuild, "Bot");
        controls::label(canvas_, x, base, kSub * u, kBracket, ">");
    }
    const float right = capX + kWide * u - 4.0f * u;
    if (hitter && realm.tables()) {
        const std::string& what = realm.tables()->kinds[size_t(hitter->kind)].label;
        const float barW = 64.0f * u, barH = 6.0f * u;
        const gfx::Box bar{right - barW, base - kSub * u * 0.36f - barH * 0.5f, barW, barH};
        canvas_.rect(bar, kMiniWell);
        const float share = hitter->maxHealth > 0
                                ? std::clamp(float(play.shownHealth(hitter->id)) / float(hitter->maxHealth), 0.0f, 1.0f)
                                : 0.0f;
        canvas_.rect({bar.x, bar.y, bar.w * share, bar.h}, kMiniFill);
        const float w = controls::labelWidth(kSub * u, what);
        const float wx = bar.x - 8.0f * u - w;
        controls::label(canvas_, wx, base, kSub * u, style::kBoneHi, what);
        // A small red pointer before the name: something on him.
        const float tx = wx - 12.0f * u, ty = base - kSub * u * 0.36f;
        const float tri[6] = {tx, ty - 5.0f * u, tx + 7.0f * u, ty, tx, ty + 5.0f * u};
        const float uv[6] = {};
        canvas_.polygon(nullptr, tri, uv, 3, kHitBy);
    } else if (dead && !killer_.empty()) {
        const std::string what = "Killed by " + killer_;
        const float w = controls::labelWidth(kSub * u, what);
        controls::label(canvas_, right - w, base, kSub * u, style::kAshInk, what);
        const float tx = right - w - 12.0f * u, ty = base - kSub * u * 0.36f;
        const float tri[6] = {tx, ty - 5.0f * u, tx + 7.0f * u, ty, tx, ty + 5.0f * u};
        const float uv[6] = {};
        canvas_.polygon(nullptr, tri, uv, 3, style::kAshInk2);
    }
}

}  // namespace mu::game
