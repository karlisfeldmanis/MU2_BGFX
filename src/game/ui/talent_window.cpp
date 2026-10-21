#include "game/ui/talent_window.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

#include "core/log.h"
#include "game/play.h"
#include "game/ui/controls.h"
#include "game/ui/panel.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"
#include "sim/quests.h"
#include "sim/realm.h"
#include "sim/skills.h"

namespace mu::game {
namespace {

using gfx::Box;

// ---- the measures, in tip::unit(): the canvas's pixels at 1080 lines, one for one --------------

constexpr float kTall = 882.0f;
// A column is 200 wide with its share of the gaps, and the window's sides take 60.
constexpr float kColumnPitch = 200.0f, kSides = 60.0f;
constexpr float kPadX = 14.0f, kColumnGap = 10.0f;
constexpr float kColumnTop = style::kHead + 12.0f;
// The strip at the foot, and the air above and below it.
constexpr float kStripTall = 66.0f, kStripAir = 12.0f;
constexpr float kColumnTall = kTall - kColumnTop - (kStripAir + kStripTall + kStripAir);
// A column's head: its name and spent count, the skills it feeds under them.
constexpr float kColumnHead = 62.0f;
constexpr float kSkillW = 16.0f, kSkillH = 22.0f;

// **The tree.** A small is MU's 20x28 cell at twice (40 by 56), a major at 2.4 times (48 by 67):
// the canvas's two sizes, which read the major as the tier's goal without a second shape. Both
// stay inside the 3x the upscale rules allow. A tier is 214 deep and its major 100 under its
// smalls; the smalls stand 42 either side of the column's middle.
constexpr float kSmallW = 40.0f, kSmallH = 56.0f, kMajorW = 48.0f, kMajorH = 67.0f;
constexpr float kStrip = 20.0f;  // the − | + strip inside the frame
constexpr float kBand = 17.0f;   // the x/10 band over the icon's foot, tall enough for its 13
constexpr float kTier = 214.0f, kMajorDrop = 100.0f, kSmallOff = 42.0f;
constexpr float kTreeTall = kTier * 2.0f + kMajorDrop + kMajorH + kStrip + 1.0f + 4.0f;
// MU's sheet: twenty-five 20x28 cells a row, skill N in cell N - 300 (pipeline/talent_icons.py).
constexpr int kSheetRow = 25;
constexpr float kCellW = 20.0f, kCellH = 28.0f;

// The type, in units.
constexpr float kClassSize = 16.0f, kFreeSize = 22.0f, kFreeWord = 16.0f;
constexpr float kNameSize = 18.0f, kSpentSize = 14.0f, kSubSize = 12.0f, kCountSize = 13.0f;
// The least a small figure is set at, in pixels, whatever the unit: at 1280x720 the window's unit
// is two thirds and the canvas's 11 for a count came out at 7 pixels, which could not be read
// (the user, 2026-10-21). The window keeps its size; only these few words are held up.
constexpr float kLeastCount = 10.0f, kLeastWord = 11.0f;
constexpr float kSelName = 19.0f, kSelSmall = 14.0f, kSelText = 15.0f;

// ---- the motion, in seconds: the plan's "The unlock" -------------------------------------------

// The lines' light rides the unlock's whoosh (window_talent_whoosh), which swells from nothing to
// its peak kWhooshPeak on: a line out to a talent it opens takes kOpenRun and lights that talent
// on the peak, where the sword cut's hit is laid (the user, 2026-10-21: "sync with audio"). The
// lines into a talent just taken, and a reset's, are quick.
constexpr float kWhooshPeak = 0.6f;     // the whoosh's loudest, from its start
constexpr float kCutHit = 0.1f;         // the sword cut's hit, from its start (window_talent)
constexpr float kOpenRun = kWhooshPeak / 0.85f;  // lights its talent at 0.85 of the run: the peak
constexpr float kFillSeconds = 0.22f;   // a line into a taken talent, or a reset's
constexpr float kWakeSeconds = 0.2f;    // grey to colour, and the frame's pop
constexpr float kWakePop = 0.12f;       // 1.0 -> 1.12 -> 1.0
constexpr float kPointSeconds = 0.25f;  // a plain point's flash
constexpr float kPointPop = 0.05f;
// The ignition's lengths: a talent opened, a major taken, a plain point. No rings and no sparks
// (the user, 2026-10-21: "remove those unlock rings and sparkles").
constexpr float kOpenSeconds = 0.95f, kMajorSeconds = 1.4f, kGlintSeconds = 0.45f;
// **The smoke** (the user, 2026-10-21: "can we use better some smoke effect on unlocking talent
// block?"): when a talent lights, soft puffs breathe out of its frame -- "from all sides evenly"
// (the user, 2026-10-21), a rim of smoke -- and grow and fade. MU's own smoke -- smoke01,
// BITMAP_SMOKE, a grey wisp on black, laid on as light so it glows warm grey and lit from below
// by the ember -- over smoke02, the fire's brown puff, a trace, for body. Toned down once (the
// user: "smoke effect is little bit too much"): faint, a short way out, gone by these, at most:
constexpr float kOpenSmoke = 1.0f, kMajorSmoke = 1.2f;
constexpr int kOpenPuffs = 14, kMajorPuffs = 22;  // along the rim, and a puff at each corner
// A reset runs up the tree a tier at a time, bottom first: three tiers in about 0.3 s.
constexpr float kRippleStep = 0.1f, kFadeSeconds = 0.12f;
// The head's figure, as the points in hand change: a white-gold flash settling, and a small pop.
constexpr float kFreeFlash = 0.25f;
// The breath round a node that can take a point: one in two seconds, at most a quarter lit at the
// ring nearest the frame -- seen at the edge of the eye, never a blink (the user, 2026-10-21).
constexpr float kBreathSeconds = 2.0f, kBreathMost = 0.26f;

// The lines of a column: each tier's two smalls into its major, and each major out to the next
// tier's two smalls. Upper node first.
constexpr int kEdge[10][2] = {{0, 2}, {1, 2}, {2, 3}, {2, 4}, {3, 5}, {4, 5}, {5, 6}, {5, 7}, {6, 8}, {7, 8}};

// The rim's flash as a node wakes: white gold, settling to blood.
constexpr uint32_t kFlash = gfx::rgba(1.0f, 0.894f, 0.659f);
constexpr uint32_t kEmber = gfx::rgba(1.0f, 0.776f, 0.420f);
// A column's floor and the band under a count: the canvas's void at 45% and 78%.
constexpr uint32_t kColumnFloor = gfx::rgba(0.020f, 0.012f, 0.012f, 0.45f);
constexpr uint32_t kBandInk = gfx::rgba(0.020f, 0.012f, 0.012f, 0.78f);
constexpr uint32_t kMinusFoot = gfx::rgba(0.082f, 0.063f, 0.051f);  // #15100d

// The hit ids: the close, Reset, then a node's icon, its −, its + (base + column x 9 + node).
constexpr int kCloseId = 0, kResetId = 1, kIconIds = 100, kMinusIds = 200, kPlusIds = 300;

Box placed(float x, float y, const Box& b, float u) {
    return {std::round(x + b.x * u), std::round(y + b.y * u), std::round(b.w * u), std::round(b.h * u)};
}

uint32_t alphaOf(uint32_t abgr, float times) {
    const float a = float(abgr >> 24) / 255.0f * std::clamp(times, 0.0f, 1.0f);
    return (abgr & 0x00FFFFFFu) | (uint32_t(std::lround(a * 255.0f)) << 24);
}

uint32_t mixed(uint32_t a, uint32_t b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const float x = float((a >> shift) & 0xFFu), y = float((b >> shift) & 0xFFu);
        out |= uint32_t(std::lround(x + (y - x) * t)) << shift;
    }
    return out;
}

uint32_t grey(float level, float alpha = 1.0f) { return gfx::rgba(level, level, level, alpha); }

float smooth(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// A line from (x1, y1) to (x2, y2), `thick` across, its ink graded from one end to the other.
void stroke(gfx::Canvas& canvas, float x1, float y1, float x2, float y2, float thick, uint32_t from, uint32_t to) {
    const float dx = x2 - x1, dy = y2 - y1, length = std::sqrt(dx * dx + dy * dy);
    if (length < 0.01f) return;
    const float nx = -dy / length * thick * 0.5f, ny = dx / length * thick * 0.5f;
    const float xy[8] = {x1 + nx, y1 + ny, x2 + nx, y2 + ny, x2 - nx, y2 - ny, x1 - nx, y1 - ny};
    const uint32_t ink[4] = {from, to, to, from};
    canvas.polygon(xy, ink, 4);
}

// A word centred in a box, in the buttons' bold small capitals.
void centred(gfx::Canvas& canvas, const Box& box, float size, uint32_t ink, const std::string& text) {
    const float base = controls::middle(box.y, box.h, size);
    const gfx::Face* face = controls::wordFace();
    if (face == nullptr) {
        controls::label(canvas, box.x + 2.0f, base, size, ink, text);
        return;
    }
    const float wide = face->measure(size, text);
    canvas.lettered(*face, controls::wordTexture(), std::round(box.midX() - wide * 0.5f) + 1.0f, base + 1.0f, size, 0.0f,
                    style::kDrop, text);
    canvas.lettered(*face, controls::wordTexture(), std::round(box.midX() - wide * 0.5f), base, size, 0.0f, ink, text);
}

// A heading in the bold small capitals, untracked; returns its width.
float heading(gfx::Canvas& canvas, float x, float baseline, float size, uint32_t ink, const std::string& text) {
    const gfx::Face* face = controls::wordFace();
    if (face == nullptr) return controls::label(canvas, x, baseline, size, ink, text);
    canvas.lettered(*face, controls::wordTexture(), std::round(x) + 1.0f, std::round(baseline) + 1.0f, size, 0.0f,
                    style::kDrop, text);
    return canvas.lettered(*face, controls::wordTexture(), std::round(x), std::round(baseline), size, 0.0f, ink, text);
}

// A figure in the titles' Cinzel; returns its width.
float cinzel(gfx::Canvas& canvas, float x, float baseline, float size, uint32_t ink, const std::string& text) {
    const gfx::Face* face = panel::titleFace();
    if (face == nullptr) return controls::label(canvas, x, baseline, size, ink, text);
    canvas.lettered(*face, panel::titleTexture(), std::round(x) + 1.0f, std::round(baseline) + 1.0f, size, 0.0f,
                    style::kDrop, text);
    return canvas.lettered(*face, panel::titleTexture(), std::round(x), std::round(baseline), size, 0.0f, ink, text);
}
float cinzelWidth(float size, const std::string& text) {
    const gfx::Face* face = panel::titleFace();
    return face ? face->measure(size, text) : controls::labelWidth(size, text);
}

// "1.5", "2", "-2": a talent's figure, a tenth at most, its ".0" dropped.
std::string figure(float value) {
    char text[24];
    std::snprintf(text, sizeof text, "%.1f", double(value));
    std::string out = text;
    if (out.size() > 2 && out.compare(out.size() - 2, 2, ".0") == 0) out.resize(out.size() - 2);
    if (out == "-0") out = "0";
    return out;
}

// The stats that are a count rather than a percent (sim/talents.h, TalentStat).
bool counted(sim::TalentStat stat) {
    return stat == sim::TalentStat::AttackSpeed || stat == sim::TalentStat::CastSpeed ||
           stat == sim::TalentStat::IcePoisonResistance || stat == sim::TalentStat::AidReach;
}

// What a small gives at `rank` points (at least one): "+3% two-handed damage". A major's whole line.
std::string effect(const sim::TalentNode& node, int index, int rank) {
    if (sim::isMajor(index)) return node.text;
    const float value = node.effect.amount * float(std::max(rank, 1));
    return (value > 0.0f ? "+" : "") + figure(value) + (counted(node.effect.stat) ? "" : "%") + " " + node.text;
}

// Where a node stands in its column, in units from the column's left and the tree's top.
struct Geo {
    Box frame, icon, band, minus, plus;
    float border = 1.0f;
};
Geo geoOf(float left, float wide, float top, int i) {
    const bool major = sim::isMajor(i);
    const float iw = major ? kMajorW : kSmallW, ih = major ? kMajorH : kSmallH;
    const float border = major ? 2.0f : 1.0f;
    const float w = iw + border * 2.0f, h = ih + kStrip + 1.0f + border * 2.0f;
    const float mid = left + wide * 0.5f;
    const int p = i % 3, t = i / 3;
    const float cx = p == 2 ? mid : (p == 0 ? mid - kSmallOff : mid + kSmallOff);
    const float y = top + float(t) * kTier + (major ? kMajorDrop : 0.0f);
    Geo g;
    g.border = border;
    g.frame = {cx - w * 0.5f, y, w, h};
    g.icon = {g.frame.x + border, y + border, iw, ih};
    g.band = {g.icon.x, g.icon.bottom() - kBand, iw, kBand};
    const float stripTop = g.icon.bottom() + 1.0f;
    g.minus = {g.icon.x, stripTop, iw * 0.5f, kStrip};
    g.plus = {g.icon.x + iw * 0.5f, stripTop, iw * 0.5f, kStrip};
    return g;
}

// A node's look: 0 closed (MU's grey twin), 1 open and empty (the colour, dimmed), 2 taken.
int levelOf(const std::array<uint8_t, sim::kTalentNodes>& column, int i) {
    return column[size_t(i)] > 0 ? 2 : sim::talentOpen(column, i) ? 1 : 0;
}
// A line's: 0 iron, 1 ready (the lower node open), 2 lit (both taken).
int stateOf(const std::array<uint8_t, sim::kTalentNodes>& column, int e) {
    const int a = kEdge[e][0], b = kEdge[e][1];
    if (column[size_t(a)] > 0 && column[size_t(b)] > 0) return 2;
    return sim::talentOpen(column, b) ? 1 : 0;
}
uint32_t lineInk(int state) {
    return state == 2 ? style::kBlood : state == 1 ? style::kBloodMd : style::kIronDk;
}

// The skills a column feeds, by MU's number: the canvas's own list for its head (TalentColumn::
// feeds, 0 ends), which the head's icons and the strip's "where" both read.
int fedSkills(const sim::TalentColumn& column, int32_t* out, int most) {
    int n = 0;
    for (const int16_t skill : column.feeds) {
        if (skill == 0 || n >= most) break;
        out[n++] = skill;
    }
    return n;
}

const char* titleOf(sim::Kin kin, bool second) { return sim::className(int(kin), second); }

// ---- light: the ignition's (the user, 2026-10-21: "nows its very vierd and basic") -------------
// Everything below is laid on as light (gfx::Canvas::light): a colour adds to what is under it
// and hides nothing, so the burst glows over the window like fire, not like paint.

struct Hue {
    float r, g, b;
};
constexpr Hue kWhiteGold{1.0f, 0.93f, 0.76f}, kGold{1.0f, 0.74f, 0.34f}, kFire{1.0f, 0.46f, 0.16f};

uint32_t lit(const Hue& h, float s) {
    s = std::clamp(s, 0.0f, 1.0f);
    return gfx::rgba(h.r * s, h.g * s, h.b * s, 0.0f);
}
// A number in [0, 1), the same every frame for the same spark.
float noise01(uint32_t key, uint32_t channel) {
    uint32_t h = key * 0x9E3779B1u ^ (channel + 0x7F4A7C15u) * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return float(h & 0xFFFFFFu) / float(0x1000000);
}
// A soft round glow: `s` at its middle, nothing at its rim.
void glowDisc(gfx::Canvas& canvas, float cx, float cy, float r, const Hue& h, float s) {
    if (s <= 0.003f || r <= 0.5f) return;
    constexpr int kSides = 20;
    float xy[(kSides + 2) * 2];
    uint32_t ink[kSides + 2];
    xy[0] = cx;
    xy[1] = cy;
    ink[0] = lit(h, s);
    for (int i = 0; i <= kSides; ++i) {
        const float a = float(i) * 6.2831853f / float(kSides);
        xy[(i + 1) * 2] = cx + std::cos(a) * r;
        xy[(i + 1) * 2 + 1] = cy + std::sin(a) * r;
        ink[i + 1] = lit(h, 0.0f);
    }
    canvas.light(xy, ink, kSides + 2);
}
// One puff of smoke: MU's wisp `r` from its middle each way, turned `turn`, laid on as light at
// `s` in warm grey, its lower side lit `ember` by the fire under it; and the brown body under it
// at `body` alpha, for density.
void puff(gfx::Canvas& canvas, const gfx::Art& wisp, const gfx::Art& thick, float cx, float cy, float r, float turn,
          float s, float ember, float body) {
    if (r <= 0.5f) return;
    const float c = std::cos(turn), n = std::sin(turn);
    const float off[4][2] = {{-r, -r}, {r, -r}, {r, r}, {-r, r}};
    float xy[8];
    const float uv[8] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    for (int k = 0; k < 4; ++k) {
        xy[k * 2] = cx + off[k][0] * c - off[k][1] * n;
        xy[k * 2 + 1] = cy + off[k][0] * n + off[k][1] * c;
    }
    if (body > 0.003f && thick.valid()) {
        const uint32_t ink = gfx::rgba(0.80f, 0.79f, 0.78f, std::clamp(body, 0.0f, 1.0f));
        const uint32_t inks[4] = {ink, ink, ink, ink};
        canvas.polygon(thick, xy, uv, inks, 4);
    }
    if (s > 0.003f && wisp.valid()) {
        // The wisp is dim (its brightest texel is 71 of 255), so it is laid three times, each a
        // little turned and grown, which also gives the puff its folds.
        for (int layer = 0; layer < 3; ++layer) {
            const float t = turn + 1.9f * float(layer), g = r * (1.0f - 0.16f * float(layer));
            const float lc = std::cos(t), ln = std::sin(t);
            float lxy[8];
            uint32_t inks[4];
            for (int k = 0; k < 4; ++k) {
                lxy[k * 2] = cx + (off[k][0] * lc - off[k][1] * ln) * g / r;
                lxy[k * 2 + 1] = cy + (off[k][0] * ln + off[k][1] * lc) * g / r;
                // 0 at the puff's top, 1 at its foot, as it stands on the screen.
                const float down = std::clamp((lxy[k * 2 + 1] - cy) / (g * 1.42f) * 0.5f + 0.5f, 0.0f, 1.0f);
                const float e = ember * down;
                const float gr = s * (0.78f * (1.0f - e) + 1.00f * e), gg = s * (0.75f * (1.0f - e) + 0.52f * e),
                            gb = s * (0.72f * (1.0f - e) + 0.24f * e);
                inks[k] = gfx::rgba(std::min(1.0f, gr), std::min(1.0f, gg), std::min(1.0f, gb), 0.0f);
            }
            canvas.lightImage(wisp, lxy, uv, inks, 4);
        }
    }
}
// Light round a frame's edge: `s` on the edge, nothing `spread` out from it.
void glowFrame(gfx::Canvas& canvas, const Box& b, float spread, const Hue& h, float s) {
    if (s <= 0.003f) return;
    const float x0 = b.x, y0 = b.y, x1 = b.right(), y1 = b.bottom();
    const float X0 = x0 - spread, Y0 = y0 - spread, X1 = x1 + spread, Y1 = y1 + spread;
    const float sides[4][8] = {{x0, y0, x1, y0, X1, Y0, X0, Y0},
                               {x1, y0, x1, y1, X1, Y1, X1, Y0},
                               {x1, y1, x0, y1, X0, Y1, X1, Y1},
                               {x0, y1, x0, y0, X0, Y0, X0, Y1}};
    const uint32_t ink[4] = {lit(h, s), lit(h, s), lit(h, 0.0f), lit(h, 0.0f)};
    for (const auto& side : sides) canvas.light(side, ink, 4);
}

}  // namespace

void TalentWindow::open(const gfx::Interface& interface, panel::Arts* arts) {
    interface.adopt(canvas_);
    interface.adopt(card_);
    interface.adopt(breath_);
    arts_ = arts;
    for (auto& column : burst_) {
        for (Spark& one : column) one = Spark{};
    }
}

void TalentWindow::close() {
    canvas_.clear();
    card_.clear();
    breath_.clear();
    up_ = false;
    built_ = false;
    carded_ = -1;
}

void TalentWindow::show() {
    up_ = true;
    over_ = pressing_ = -1;
    armed_ = false;
    built_ = false;
    carded_ = -1;
}

void TalentWindow::hide() {
    if (up_) close();
}

bool TalentWindow::covers(float x, float y) const {
    if (!up_) return false;
    return x >= x_ && y >= y_ && x < x_ + wide_ * u_ && y < y_ + kTall * u_;
}

bool TalentWindow::unlocks(const sim::TalentRanks& now, int column, int node, int took) {
    if (column < 0 || column >= sim::kTalentColumnsMost || node < 0 || node >= sim::kTalentNodes || took <= 0) {
        return false;
    }
    const auto& after = now[size_t(column)];
    auto before = after;
    before[size_t(node)] = uint8_t(std::max(0, int(before[size_t(node)]) - took));
    if (before[size_t(node)] == 0) return true;
    for (int j = 0; j < sim::kTalentNodes; ++j) {
        if (!sim::talentOpen(before, j) && sim::talentOpen(after, j)) return true;
    }
    return false;
}

void TalentWindow::unlocked(bool ownFirstPoint) {
    cueCount_ = 0;
    cues_[cueCount_++] = {clock_, Cue::Whoosh};
    cues_[cueCount_++] = {ownFirstPoint ? clock_ : clock_ + double(kWhooshPeak - kCutHit), Cue::Cut};
}

bool TalentWindow::takeSound(Cue* out) {
    for (int i = 0; i < cueCount_; ++i) {
        if (cues_[i].at > clock_) continue;
        *out = cues_[i].cue;
        cues_[i] = cues_[--cueCount_];
        return true;
    }
    return false;
}

void TalentWindow::watch(float seconds, const sim::Body* hero) {
    clock_ += double(std::max(0.0f, seconds));
    if (hero == nullptr) return;
    if (hero->id != seenId_ || int(hero->kin) != seenKin_) {
        seen_ = hero->talents;
        seenId_ = hero->id;
        seenKin_ = int(hero->kin);
        return;
    }
    if (hero->talents == seen_) return;
    if (up_) animate(seen_, hero->talents);
    seen_ = hero->talents;
}

void TalentWindow::animate(const sim::TalentRanks& before, const sim::TalentRanks& after) {
    const double now = clock_;
    const auto busy = [&](double until) { busyUntil_ = std::max(busyUntil_, until); };

    // **A reset**: every talent back to grey, a tier at a time from the bottom up.
    if (sim::talentsSpent(after) == 0 && sim::talentsSpent(before) > 0) {
        for (int c = 0; c < sim::kTalentColumnsMost; ++c) {
            for (int i = 0; i < sim::kTalentNodes; ++i) {
                const int was = levelOf(before[size_t(c)], i), is = levelOf(after[size_t(c)], i);
                if (was <= is) continue;
                const double at = now + double(kRippleStep) * double(sim::kTalentTiers - 1 - i / 3);
                glow_[c][i] = {at, kFadeSeconds, float(was), float(is), Motion::Fade};
                busy(at + kFadeSeconds);
            }
            for (int e = 0; e < kEdges; ++e) {
                const int was = stateOf(before[size_t(c)], e), is = stateOf(after[size_t(c)], e);
                if (was == is) continue;
                const double at = now + double(kRippleStep) * double(sim::kTalentTiers - 1 - kEdge[e][1] / 3);
                fill_[c][e] = {at, uint8_t(was), uint8_t(is), kFillSeconds};
                busy(at + kFillSeconds);
            }
        }
        return;
    }

    for (int c = 0; c < sim::kTalentColumnsMost; ++c) {
        const auto& was = before[size_t(c)];
        const auto& is = after[size_t(c)];
        if (was == is) continue;
        int raised = -1;
        for (int i = 0; i < sim::kTalentNodes; ++i) {
            if (is[size_t(i)] > was[size_t(i)]) raised = i;
        }
        if (raised < 0) {
            // A point given back: its rank flashes, and the lines and looks it changes snap.
            for (int i = 0; i < sim::kTalentNodes; ++i) {
                if (is[size_t(i)] < was[size_t(i)]) {
                    glow_[c][i] = {now, kPointSeconds, 2.0f, float(levelOf(is, i)), Motion::Point};
                    busy(now + kPointSeconds);
                }
            }
            continue;
        }
        // The point itself: a first point wakes the icon (a major's grows its ring too); any
        // other flashes its rank.
        if (was[size_t(raised)] == 0) {
            glow_[c][raised] = {now, kWakeSeconds, float(levelOf(was, raised)), 2.0f, Motion::Wake};
            busy(now + kWakeSeconds);
            burst_[c][raised] = {now, sim::isMajor(raised) ? Burst::Major : Burst::Open};
        } else {
            glow_[c][raised] = {now, kPointSeconds, 2.0f, 2.0f, Motion::Point};
            busy(now + kPointSeconds);
            burst_[c][raised] = {now, Burst::Point};
        }
        // The lines it lights: those into it first, then those out of it, one after another,
        // each waking the talent it opens as the blood reaches it.
        for (int e = 0; e < kEdges; ++e) {
            const int from = stateOf(was, e), to = stateOf(is, e);
            if (to <= from || kEdge[e][1] != raised) continue;
            fill_[c][e] = {now, uint8_t(from), uint8_t(to), kFillSeconds};
            busy(now + kFillSeconds);
        }
        for (int e = 0; e < kEdges; ++e) {
            const int from = stateOf(was, e), to = stateOf(is, e);
            if (to <= from || kEdge[e][1] == raised) continue;
            // Every line out at once, on the whoosh: all reach their talents on its peak.
            const double at = now;
            fill_[c][e] = {at, uint8_t(from), uint8_t(to), kOpenRun};
            busy(at + kOpenRun);
            const int b = kEdge[e][1];
            const int lb = levelOf(was, b), la = levelOf(is, b);
            if (la > lb) {
                // The icon wakes as the line's head comes into it.
                const double wake = at + double(kOpenRun) * 0.8;
                glow_[c][b] = {wake, kWakeSeconds, float(lb), float(la), Motion::Wake};
                busy(wake + kWakeSeconds);
                // And ignites as the energy reaches it.
                burst_[c][b] = {at + double(kOpenRun) * 0.85, Burst::Open};
            }
        }
    }
}

int TalentWindow::hitAt(float ux, float uy) const {
    const float side = style::kSmallSquare;
    if (Box{wide_ - style::kPad - side, (style::kHead - side) * 0.5f, side, side}.has(ux, uy)) return kCloseId;
    const float stripTop = kTall - kStripAir - kStripTall;
    const Box strip{kPadX, stripTop, wide_ - kPadX * 2.0f, kStripTall};
    const float resetW = armed_ ? 170.0f : 90.0f;
    if (Box{strip.right() - 10.0f - resetW, strip.y + (kStripTall - style::kButtonS) * 0.5f, resetW, style::kButtonS}
            .has(ux, uy)) {
        return kResetId;
    }
    const float colW = (wide_ - kPadX * 2.0f - kColumnGap * float(columns_ - 1)) / float(columns_);
    const float treeTop = kColumnTop + kColumnHead + 2.0f + (kColumnTall - kColumnHead - 2.0f - kTreeTall) * 0.5f;
    for (int c = 0; c < columns_; ++c) {
        const float left = kPadX + float(c) * (colW + kColumnGap);
        if (ux < left || ux >= left + colW) continue;
        for (int i = 0; i < sim::kTalentNodes; ++i) {
            const Geo g = geoOf(left, colW, treeTop, i);
            if (g.minus.has(ux, uy)) return kMinusIds + c * sim::kTalentNodes + i;
            if (g.plus.has(ux, uy)) return kPlusIds + c * sim::kTalentNodes + i;
            if (g.frame.has(ux, uy)) return kIconIds + c * sim::kTalentNodes + i;
        }
    }
    return -1;
}

void TalentWindow::update(const Play& play, float width, float height, float floor, const Pointer& pointer,
                          Result* out) {
    if (!up_ || !play.isOpen() || play.realm().tables() == nullptr) return;
    const sim::Realm& realm = play.realm();
    const sim::Body& hero = realm.hero();
    const sim::TalentTree tree = sim::talentTree(hero.kin);
    if (tree.count <= 0) return;
    columns_ = std::min(tree.count, sim::kTalentColumnsMost);
    wide_ = kColumnPitch * float(columns_) + kSides;

    // In the middle, between the screen's top and the HUD's plate; a unit smaller where 1260 (or
    // the Gladiator's 1460) and 882 do not fit, so the window is never cut by an edge.
    const float unit = tip::unit();
    const float bottom = floor > 0.0f ? floor : height;
    const float side = 16.0f * unit;
    float u = unit;
    u = std::min(u, (width - side * 2.0f) / wide_);
    u = std::min(u, (bottom - side * 2.0f) / kTall);
    u_ = std::max(u, 0.25f);
    x_ = std::round((width - wide_ * u_) * 0.5f);
    y_ = std::round(std::max(side, (bottom - kTall * u_) * 0.5f));
    width_ = width;
    height_ = height;
    column_ = std::clamp(column_, 0, columns_ - 1);

    const int free = realm.talentsInHand();
    const auto& ranks = hero.talents;
    const auto canAdd = [&](int c, int i) {
        const auto& col = ranks[size_t(c)];
        return sim::talentOpen(col, i) && int(col[size_t(i)]) < sim::mostOf(i) && free > 0;
    };

    const float ux = (pointer.x - x_) / u_, uy = (pointer.y - y_) / u_;
    over_ = covers(pointer.x, pointer.y) ? hitAt(ux, uy) : -1;
    if (pointer.pressed) pressing_ = over_;
    if (pointer.released) {
        if (pressing_ >= 0 && pressing_ == over_ && out) {
            const int at = pressing_;
            if (at == kCloseId) {
                out->close = true;
            } else if (at == kResetId) {
                if (armed_) {
                    out->reset = true;  // silent here: the answer is heard (Desk)
                    armed_ = false;
                } else {
                    out->clicked = true;
                    armed_ = sim::talentsSpent(ranks) > 0;
                }
            } else if (at >= kIconIds) {
                const int id = at >= kPlusIds ? at - kPlusIds : at >= kMinusIds ? at - kMinusIds : at - kIconIds;
                const int c = id / sim::kTalentNodes, i = id % sim::kTalentNodes;
                column_ = c;
                node_ = i;
                armed_ = false;
                // A + or a − that can land is the realm's to answer, and its sound is the
                // answer's (Desk, Then::Talent); one that cannot only chooses the node.
                if (at >= kPlusIds && canAdd(c, i)) {
                    out->column = c;
                    out->node = i;
                    out->take = true;
                } else if (at >= kMinusIds && at < kPlusIds && sim::talentTakeable(ranks[size_t(c)], i)) {
                    out->column = c;
                    out->node = i;
                    out->take = false;
                } else {
                    out->clicked = true;
                }
            }
        }
        pressing_ = -1;
    }

    // The head's figure flashes as it changes (the user, 2026-10-21: "ui effects").
    if (lastFree_ >= 0 && free != lastFree_) {
        freeAt_ = clock_;
        busyUntil_ = std::max(busyUntil_, clock_ + double(kFreeFlash));
    }
    lastFree_ = free;

    // Drawn again only when something it shows has changed, or while something moves.
    Drawn now;
    now.ranks = ranks;
    now.kin = int(hero.kin);
    now.second = hero.second;
    now.free = free;
    now.x = x_;
    now.y = y_;
    now.u = u_;
    now.width = width;
    now.height = height;
    now.over = over_;
    now.pressing = pressing_;
    now.column = column_;
    now.node = node_;
    now.armed = armed_;
    const bool moving = clock_ <= busyUntil_ + 0.05;
    if (!built_ || moving || !(now == drawn_)) {
        drawn_ = now;
        built_ = true;
        rebuild(play);
    }

    // The card over the node under the pointer.
    const int hovered = over_ >= kIconIds ? (over_ - kIconIds) % (kMinusIds - kIconIds) : -1;
    if (hovered != carded_ || (hovered >= 0 && cardRanks_ != ranks) || (hovered >= 0 && moving)) {
        carded_ = hovered;
        cardRanks_ = ranks;
        rebuildCard(play);
    }

    // **Where a point can go** breathes: every open node not full, while he has points in hand,
    // a soft ember round its frame, slow and low (kBreathSeconds, kBreathMost), so the eye finds
    // them without the tree flickering. None at no points. Every frame, on its own canvas.
    breath_.clear();
    if (free > 0) {
        const float breath = 0.5f - 0.5f * std::cos(6.2831853f * float(std::fmod(clock_, double(kBreathSeconds))) /
                                                     kBreathSeconds);
        const float colW = (wide_ - kPadX * 2.0f - kColumnGap * float(columns_ - 1)) / float(columns_);
        const float treeTop = kColumnTop + kColumnHead + 2.0f + (kColumnTall - kColumnHead - 2.0f - kTreeTall) * 0.5f;
        const float ring = std::max(1.0f, std::round(2.0f * u_));
        for (int c = 0; c < columns_; ++c) {
            for (int i = 0; i < sim::kTalentNodes; ++i) {
                if (!canAdd(c, i)) continue;
                const Geo g = geoOf(kPadX + float(c) * (colW + kColumnGap), colW, treeTop, i);
                const Box frame = placed(x_, y_, g.frame, u_);
                for (int k = 1; k <= 3; ++k) {
                    const float alpha = kBreathMost * (0.35f + 0.65f * breath) / float(k);
                    breath_.outline(frame.grown(ring * float(k)), ring, alphaOf(kEmber, alpha));
                }
            }
        }
    }
    // Over the breath, the light of whatever is igniting (effects).
    effects();
}

void TalentWindow::effects() {
    const double t = clock_;
    const float u = u_;
    const float colW = (wide_ - kPadX * 2.0f - kColumnGap * float(columns_ - 1)) / float(columns_);
    const float treeTop = kColumnTop + kColumnHead + 2.0f + (kColumnTall - kColumnHead - 2.0f - kTreeTall) * 0.5f;
    const auto easeOut = [](float k) { k = std::clamp(k, 0.0f, 1.0f); return 1.0f - (1.0f - k) * (1.0f - k) * (1.0f - k); };
    gfx::Canvas& fx = breath_;

    // A beam of light along a line: `wide` across, brightest down its middle, from s0 to s1.
    const auto beam = [&](float ax, float ay, float bx, float by, float wide, const Hue& h, float s0, float s1) {
        const float dx = bx - ax, dy = by - ay, len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.5f) return;
        const float nx = -dy / len * wide * 0.5f, ny = dx / len * wide * 0.5f;
        for (int side = -1; side <= 1; side += 2) {
            const float xy[8] = {ax, ay, bx, by, bx + nx * float(side), by + ny * float(side), ax + nx * float(side),
                                 ay + ny * float(side)};
            const uint32_t ink[4] = {lit(h, s0), lit(h, s1), lit(h, 0.0f), lit(h, 0.0f)};
            fx.light(xy, ink, 4);
        }
    };

    for (int c = 0; c < columns_; ++c) {
        const float left = kPadX + float(c) * (colW + kColumnGap);

        // **Energy down a line**: a comet of light from the upper talent to the one it opens,
        // a bright head and a tail that cools behind it, and the line glowing once it is there.
        for (int e = 0; e < kEdges; ++e) {
            const Fill& f = fill_[c][e];
            if (f.at < -1.0e8 || f.to <= f.from) continue;
            const float k = float((t - f.at) / double(f.seconds));
            if (k < 0.0f || k > 1.6f) continue;
            const Geo ga = geoOf(left, colW, treeTop, kEdge[e][0]), gb = geoOf(left, colW, treeTop, kEdge[e][1]);
            const float x1 = x_ + ga.frame.midX() * u, y1 = y_ + ga.frame.bottom() * u;
            const float x2 = x_ + gb.frame.midX() * u, y2 = y_ + gb.frame.y * u;
            if (k <= 1.0f) {
                const float head = smooth(k), tail = std::max(0.0f, head - 0.45f);
                const float hx = x1 + (x2 - x1) * head, hy = y1 + (y2 - y1) * head;
                const float tx = x1 + (x2 - x1) * tail, ty = y1 + (y2 - y1) * tail;
                beam(tx, ty, hx, hy, 9.0f * u, kFire, 0.0f, 0.75f);
                beam(tx, ty, hx, hy, 3.0f * u, kWhiteGold, 0.0f, 0.9f);
                glowDisc(fx, hx, hy, 14.0f * u, kGold, 0.75f);
                glowDisc(fx, hx, hy, 5.0f * u, kWhiteGold, 1.0f);
            } else {
                // Arrived: the whole line glows and settles.
                const float after = 1.0f - (k - 1.0f) / 0.6f;
                beam(x1, y1, x2, y2, 8.0f * u, kFire, 0.45f * after * after, 0.45f * after * after);
            }
        }

        // **The ignitions.**
        for (int i = 0; i < sim::kTalentNodes; ++i) {
            Spark& b = burst_[c][i];
            if (b.kind == Burst::None) continue;
            const float age = float(t - b.at);
            const float length = b.kind == Burst::Major ? kMajorSeconds : b.kind == Burst::Open ? kOpenSeconds : kGlintSeconds;
            const bool smoky = b.kind == Burst::Open || b.kind == Burst::Major;
            const float smokeFor = b.kind == Burst::Major ? kMajorSmoke : kOpenSmoke;
            if (age < 0.0f) continue;
            if (age > std::max(length, smoky ? smokeFor : 0.0f)) {
                b = Spark{};
                continue;
            }
            const Geo g = geoOf(left, colW, treeTop, i);
            const Box frame = placed(x_, y_, g.frame, u), icon = placed(x_, y_, g.icon, u);
            const float cx = icon.midX(), cy = icon.midY(), size = frame.h;
            const uint32_t seed = uint32_t(c * 31 + i * 7) ^ uint32_t(int64_t(b.at * 1000.0));

            // **The smoke**, under the light: out of the frame's rim on every side at once, each
            // puff growing and fading as it goes; the ember under it dies first.
            if (smoky && arts_ != nullptr) {
                const gfx::Art& wisp = arts_->get("smoke01");
                const gfx::Art& thick = arts_->get("smoke");
                const bool major = b.kind == Burst::Major;
                // Evenly along the frame's whole rim, the four corners among them: each puff
                // starts on the edge and breathes straight out along its normal (the corners'
                // out on the diagonal), all within a short ripple, so the block exhales a rim of
                // smoke the same on every side. A major has more of them, not bigger ones.
                const int puffs = major ? kMajorPuffs : kOpenPuffs;
                const float rim = 2.0f * (frame.w + frame.h);
                for (int k = 0; k < puffs + 4; ++k) {
                    const auto n = [&](uint32_t ch) { return noise01(seed, uint32_t(300 + k * 8) + ch); };
                    float sx, sy, nx, ny;
                    if (k >= puffs) {
                        const int corner = k - puffs;  // top left, top right, bottom right, bottom left
                        const bool right = corner == 1 || corner == 2, low = corner >= 2;
                        sx = right ? frame.right() : frame.x;
                        sy = low ? frame.bottom() : frame.y;
                        nx = (right ? 1.0f : -1.0f) * 0.70710678f;
                        ny = (low ? 1.0f : -1.0f) * 0.70710678f;
                    } else {
                        float d = rim * (float(k) + 0.5f) / float(puffs);
                        if (d < frame.w) {
                            sx = frame.x + d, sy = frame.y, nx = 0.0f, ny = -1.0f;
                        } else if ((d -= frame.w) < frame.h) {
                            sx = frame.right(), sy = frame.y + d, nx = 1.0f, ny = 0.0f;
                        } else if ((d -= frame.h) < frame.w) {
                            sx = frame.right() - d, sy = frame.bottom(), nx = 0.0f, ny = 1.0f;
                        } else {
                            d -= frame.w;
                            sx = frame.x, sy = frame.bottom() - d, nx = -1.0f, ny = 0.0f;
                        }
                    }
                    const float delay = 0.03f * n(0);
                    const float life = smokeFor * (0.85f + 0.15f * n(1)) - delay;
                    const float a = age - delay;
                    if (a <= 0.0f || a >= life) continue;
                    const float q = a / life;
                    const float out = easeOut(q);
                    // Out along the normal, a slight lift as it cools, a small sway along the edge.
                    const float push = size * (0.16f + 0.06f * n(3)) * out;
                    const float lift = size * 0.05f * q;
                    const float sway = std::sin(q * 3.6f + 6.2831853f * n(5)) * size * 0.03f * q;
                    const float px = sx + nx * push - ny * sway, py = sy + ny * push + nx * sway - lift;
                    const float r = size * (0.13f + 0.15f * out) * (0.9f + 0.2f * n(6));
                    const float turn = 6.2831853f * n(7) + (n(1) - 0.5f) * 2.0f * a;
                    const float in = std::min(1.0f, a / 0.1f);
                    const float fade = in * std::pow(1.0f - q, 1.4f);
                    const float ember = std::pow(std::max(0.0f, 1.0f - a / 0.5f), 1.5f);
                    puff(fx, wisp, thick, px, py, r, turn, (major ? 0.62f : 0.58f) * fade, ember,
                         (major ? 0.06f : 0.05f) * fade);
                }
            }
            if (age > length) continue;
            const float p = age / length;

            if (b.kind == Burst::Point) {
                // A plain point: a soft glow over its count.
                const float fade = (1.0f - p) * (1.0f - p);
                const float gy = g.band.h > 0.0f ? y_ + g.band.midY() * u : cy;
                glowDisc(fx, cx, gy, size * 0.35f, kGold, 0.55f * fade);
                continue;
            }

            const bool major = b.kind == Burst::Major;
            const float big = major ? 1.45f : 1.0f;
            // The flash: the icon itself lit white-gold for an instant.
            const float flash = std::max(0.0f, 1.0f - age / 0.16f);
            if (flash > 0.0f) {
                const float xy[8] = {icon.x, icon.y, icon.right(), icon.y, icon.right(), icon.bottom(), icon.x, icon.bottom()};
                const uint32_t ink = lit(kWhiteGold, 0.42f * flash * flash);
                const uint32_t inks[4] = {ink, ink, ink, ink};
                fx.light(xy, inks, 4);
            }
            // The bloom behind it, warm, growing as it fades.
            // Rising to its peak over the first tenth, so the icon is never washed out; a fifth
            // lower since the smoke, which it lights, carries the warmth outward.
            const float rise = std::min(1.0f, age / 0.09f);
            glowDisc(fx, cx, cy, size * (0.65f + 0.85f * easeOut(p)) * big, kFire, 0.34f * rise * std::pow(1.0f - p, 1.6f));
            glowDisc(fx, cx, cy, size * (0.4f + 0.3f * easeOut(p)) * big, kGold, 0.30f * rise * std::pow(1.0f - p, 2.2f));
            // No rays (the user, 2026-10-21: "there is still that beam shine which I don't like").
            // The frame's edge burning, then settling.
            glowFrame(fx, frame, size * 0.2f * big, kGold, 0.75f * std::pow(1.0f - p, 1.3f));
            glowFrame(fx, frame, size * 0.06f, kWhiteGold, 0.6f * std::pow(1.0f - p, 2.5f));
            // No shine across the icon (the user, 2026-10-21: "remove that shine effect").
        }
    }
}

void TalentWindow::rebuild(const Play& play) {
    canvas_.clear();
    if (arts_ == nullptr) return;
    const sim::Realm& realm = play.realm();
    const sim::Body& hero = realm.hero();
    const sim::TalentTree tree = sim::talentTree(hero.kin);
    const auto& ranks = hero.talents;
    const int free = realm.talentsInHand();
    const float u = u_, x = x_, y = y_;
    const double t = clock_;
    const float line = std::max(1.0f, std::round(u));
    const auto sx = [&](float ux) { return x + ux * u; };
    const auto sy = [&](float uy) { return y + uy * u; };
    const auto at = [&](const Box& b) { return placed(x, y, b, u); };
    const auto state = [&](int which) {
        return controls::State{over_ == which ? 1.0f : 0.0f, pressing_ == which && over_ == which, false};
    };
    const gfx::Art& colour = arts_->get("talent_icons");
    const gfx::Art& greyed = arts_->get("talent_icons_grey");

    // MU's cell for a node, half a texel in from each side so the filter never reaches the
    // neighbour's paint at 2x.
    const auto cellOf = [](int number) {
        const int k = std::max(0, number - 300);
        return Box{float(k % kSheetRow) * kCellW + 0.5f, float(k / kSheetRow) * kCellH + 0.5f, kCellW - 1.0f,
                   kCellH - 1.0f};
    };
    // The icon at a look between 0 (grey) and 2 (taken): grey under a fading-in colour up to 1,
    // the colour brightening from the canvas's 0.62 to whole from 1 to 2.
    const auto icon = [&](const Box& box, int number, float level) {
        const Box from = cellOf(number);
        // Open and empty is the canvas's brightness(0.62) saturate(0.75): the colour at 0.62 with a
        // quarter of MU's grey twin laid over it, which is the same painting without its colour.
        if (level < 1.0f) {
            if (greyed.valid()) canvas_.region(greyed, box, from, grey(0.8f));
            else canvas_.rect(box, style::kAsh2);
            if (level > 0.0f && colour.valid()) canvas_.region(colour, box, from, grey(0.62f, level));
            if (level > 0.0f && greyed.valid()) canvas_.region(greyed, box, from, grey(0.62f, 0.25f * level));
        } else if (colour.valid()) {
            const float lit = std::min(1.0f, level - 1.0f);
            canvas_.region(colour, box, from, grey(0.62f + 0.38f * lit));
            if (lit < 1.0f && greyed.valid()) canvas_.region(greyed, box, from, grey(0.62f + 0.38f * lit, 0.25f * (1.0f - lit)));
        }
    };

    controls::frame(canvas_, at({0.0f, 0.0f, wide_, kTall}), u, "Talents");
    const float side = style::kSmallSquare;
    controls::square(canvas_, at({wide_ - style::kPad - side, (style::kHead - side) * 0.5f, side, side}),
                     controls::Glyph::Close, state(kCloseId), u);

    // ---- the head: his class at the left, the points in hand at the right ---------------------------
    {
        const float base = controls::middle(y, style::kHead * u, kClassSize * u);
        heading(canvas_, sx(16.0f), base, kClassSize * u, style::kAshInk, titleOf(hero.kin, hero.second));
        const std::string figureText = std::to_string(free);
        const float right = sx(wide_ - 52.0f);
        const float word = std::max(kFreeWord * u, kLeastWord);
        const float wordW = controls::labelWidth(word, "talent points");
        controls::label(canvas_, right - wordW, base, word, free > 0 ? style::kBone2 : style::kAshInk, "talent points");
        uint32_t figInk = free > 0 ? style::kBoneHi : style::kAshInk;
        float figSize = kFreeSize * u;
        const float flash = float((t - freeAt_) / double(kFreeFlash));
        if (flash >= 0.0f && flash < 1.0f) {
            figInk = mixed(kFlash, figInk, smooth(flash));
            figSize *= 1.0f + 0.1f * std::sin(3.14159265f * flash);
        }
        const float figW = cinzelWidth(figSize, figureText);
        cinzel(canvas_, right - wordW - 6.0f * u - figW, base + 1.0f * u, figSize, figInk, figureText);
    }

    // ---- the columns ---------------------------------------------------------------------------------
    const float colW = (wide_ - kPadX * 2.0f - kColumnGap * float(columns_ - 1)) / float(columns_);
    const float treeTop = kColumnTop + kColumnHead + 2.0f + (kColumnTall - kColumnHead - 2.0f - kTreeTall) * 0.5f;
    for (int c = 0; c < columns_ && c < tree.count; ++c) {
        const sim::TalentColumn& column = tree.columns[c];
        const auto& col = ranks[size_t(c)];
        const float left = kPadX + float(c) * (colW + kColumnGap);
        int spent = 0;
        for (const uint8_t r : col) spent += r;

        const Box outer = at({left, kColumnTop, colW, kColumnTall});
        canvas_.rect(outer, spent > 0 ? style::kIronLo : style::kIronDk);
        canvas_.rect(outer.grown(-line), kColumnFloor);

        // Its head: the name and what is spent in it, the skills it feeds under them.
        heading(canvas_, sx(left + 10.0f), sy(kColumnTop + 27.0f), kNameSize * u, style::kBoneHi, column.name);
        if (spent > 0) {
            controls::ranged(canvas_, sx(left + colW - 10.0f), sy(kColumnTop + 27.0f), kSpentSize * u, style::kBone2,
                             std::to_string(spent));
        }
        int32_t fed[6];
        const int feeds = fedSkills(column, fed, 6);
        float ix = left + 10.0f;
        const float iy = kColumnTop + 34.0f;
        for (int s = 0; s < feeds; ++s) {
            const gfx::Art& art = arts_->get("skill_" + std::to_string(fed[s]));
            const Box box = at({ix, iy, kSkillW, kSkillH});
            canvas_.rect(box.grown(line), style::kIronLo);
            if (art.valid()) canvas_.image(art, box);
            else canvas_.rect(box, style::kVoid);
            ix += kSkillW + 3.0f;
        }
        // What the column is for, in words, beside its skills -- only where it feeds two or fewer
        // (the canvas: `skills.length > 2 ? '' : sub`), whole, cut only where it truly cannot fit.
        if (feeds <= 2) {
            float size = std::max(kSubSize * u, kLeastWord);
            const float from = feeds > 0 ? ix + 3.0f : left + 10.0f;
            const float room = (left + colW - 8.0f - from) * u;
            std::string sub = column.sub;
            // Whole before legible-sized: at 720 the least size outgrows a column with two icons, so
            // it steps down to 9 pixels before any of it is cut.
            while (size > 9.0f && controls::labelWidth(size, sub) > room) size -= 0.5f;
            if (controls::labelWidth(size, sub) > room) {
                while (sub.size() > 1 && controls::labelWidth(size, sub + "...") > room) sub.pop_back();
                while (!sub.empty() && sub.back() == ' ') sub.pop_back();
                sub += "...";
            }
            controls::label(canvas_, sx(from), sy(iy + kSkillH * 0.5f) + size * 0.35f, size, style::kAshInk, sub);
        }
        canvas_.rect(at({left + 1.0f, kColumnTop + kColumnHead, colW - 2.0f, 1.0f}), style::kIronDk);

        // The lines, under the nodes. A line filling runs its new ink from the upper node down,
        // with a bright head.
        for (int e = 0; e < kEdges; ++e) {
            const Geo ga = geoOf(left, colW, treeTop, kEdge[e][0]), gb = geoOf(left, colW, treeTop, kEdge[e][1]);
            const float x1 = sx(ga.frame.midX()), y1 = sy(ga.frame.bottom());
            const float x2 = sx(gb.frame.midX()), y2 = sy(gb.frame.y);
            const float thick = 2.0f * u;
            const Fill& f = fill_[c][e];
            const float p = float((t - f.at) / double(f.seconds));
            if (p >= 1.0f || f.at < -1.0e8) {
                const uint32_t ink = lineInk(stateOf(col, e));
                stroke(canvas_, x1, y1, x2, y2, thick, ink, ink);
                continue;
            }
            const uint32_t was = lineInk(f.from), is = lineInk(f.to);
            stroke(canvas_, x1, y1, x2, y2, thick, was, was);
            if (p <= 0.0f) continue;
            const float k = smooth(p);
            const float hx = x1 + (x2 - x1) * k, hy = y1 + (y2 - y1) * k;
            stroke(canvas_, x1, y1, hx, hy, thick, is, is);
            // Its head is light, drawn every frame over the window (effects).
        }

        // The nodes.
        for (int i = 0; i < sim::kTalentNodes; ++i) {
            const sim::TalentNode& node = column.nodes[i];
            const Geo g = geoOf(left, colW, treeTop, i);
            const int r = col[size_t(i)], most = sim::mostOf(i);
            const bool open = sim::talentOpen(col, i);
            const bool chosen = c == column_ && i == node_;
            const bool add = open && r < most && free > 0;
            const bool take = sim::talentTakeable(col, i);
            uint32_t rim = r > 0 ? (r >= most ? style::kBloodHi : style::kBlood) : (open ? style::kIron : style::kIronDk);
            uint32_t countInk = r >= most ? style::kBloodHi : r > 0 ? style::kBoneHi : open ? style::kBone2 : style::kAshInk2;
            float level = float(levelOf(col, i));
            float pop = 1.0f;
            // Under the pointer, anywhere on it, its frame a step brighter.
            const int id = c * sim::kTalentNodes + i;
            if (over_ == kIconIds + id || over_ == kMinusIds + id || over_ == kPlusIds + id) {
                rim = r > 0 ? (r >= most ? mixed(style::kBloodHi, kFlash, 0.4f) : style::kBloodHi)
                            : (open ? style::kIronHi : style::kIron);
            }

            const Glow& glow = glow_[c][i];
            if (glow.motion != Motion::None && glow.at > -1.0e8) {
                const float p = float((t - glow.at) / double(std::max(0.01f, glow.seconds)));
                if (p < 0.0f) {
                    level = glow.from;
                } else if (p < 1.0f) {
                    level = glow.from + (glow.to - glow.from) * smooth(p);
                    const float bump = std::sin(3.14159265f * p);
                    if (glow.motion == Motion::Wake) {
                        pop = 1.0f + kWakePop * bump;
                        rim = mixed(kFlash, rim, smooth(p));
                    } else if (glow.motion == Motion::Point) {
                        pop = 1.0f + kPointPop * bump;
                        countInk = mixed(kFlash, countInk, smooth(p));
                    }
                }
            }

            // The frame scaled about its middle by the pop; everything in it with it.
            const float cx = g.frame.midX(), cy = g.frame.midY();
            const auto scaled = [&](const Box& b) {
                return Box{std::round(sx(cx + (b.x - cx) * pop)), std::round(sy(cy + (b.y - cy) * pop)),
                           std::round(b.w * pop * u), std::round(b.h * pop * u)};
            };
            const Box frame = scaled(g.frame);
            const float border = std::max(1.0f, std::round(g.border * u));
            if (chosen) {
                canvas_.rect(frame.grown(std::round(4.0f * u)), style::kBone2);
                canvas_.rect(frame.grown(std::round(2.0f * u)), style::kVoid);
            } else {
                canvas_.rect(frame.grown(line), style::kSeam);
            }
            canvas_.rect(frame, rim);
            canvas_.rect(frame.grown(-border), style::kVoid);
            const Box iconBox = scaled(g.icon);
            icon(iconBox, node.icon, level);
            if (!sim::isMajor(i)) {
                const Box band = scaled(g.band);
                canvas_.rect(band, kBandInk);
                centred(canvas_, band, std::max(kCountSize * u, kLeastCount) * pop, countInk, std::to_string(r) + "/" + std::to_string(most));
            }
            // The − | + strip, under a rule of the frame's own ink.
            const Box minus = scaled(g.minus), plus = scaled(g.plus);
            canvas_.rect({iconBox.x, iconBox.bottom(), iconBox.w, std::max(1.0f, minus.y - iconBox.bottom())}, rim);
            const int minusId = kMinusIds + c * sim::kTalentNodes + i, plusId = kPlusIds + c * sim::kTalentNodes + i;
            const bool overMinus = over_ == minusId, overPlus = over_ == plusId;
            const bool downMinus = overMinus && pressing_ == minusId, downPlus = overPlus && pressing_ == plusId;
            if (take) {
                const uint32_t top = overMinus ? style::kIronLo : style::kAsh4;
                canvas_.shade(minus, downMinus ? kMinusFoot : top, downMinus ? kMinusFoot : top, kMinusFoot, kMinusFoot);
            } else {
                canvas_.rect(minus, style::kAsh1);
            }
            if (add) {
                const uint32_t top = overPlus ? style::kBloodHi : style::kBlood;
                canvas_.shade(plus, downPlus ? style::kBloodMd : top, downPlus ? style::kBloodMd : top, style::kBloodMd,
                              style::kBloodMd);
            } else {
                canvas_.rect(plus, style::kAsh1);
            }
            canvas_.rect({minus.right() - line, minus.y, line, minus.h}, style::kIronDk);
            // The two glyphs, drawn as bars: 9 across, 1.7 thick, the canvas's.
            const float bar = std::max(1.0f, std::round(1.7f * u * pop)), arm = std::round(9.0f * u * pop);
            const uint32_t minusInk = take ? (overMinus ? style::kBoneHi : style::kBone2) : style::kAshInk2;
            const uint32_t plusInk = add ? style::kBoneHi : style::kAshInk2;
            canvas_.rect({std::round(minus.midX() - arm * 0.5f), std::round(minus.midY() - bar * 0.5f), arm, bar}, minusInk);
            canvas_.rect({std::round(plus.midX() - arm * 0.5f), std::round(plus.midY() - bar * 0.5f), arm, bar}, plusInk);
            canvas_.rect({std::round(plus.midX() - bar * 0.5f), std::round(plus.midY() - arm * 0.5f), bar, arm}, plusInk);

        }
    }

    // ---- the strip: the chosen node, its rank, what it gives; Reset ----------------------------------
    {
        const float top = kTall - kStripAir - kStripTall;
        const Box strip{kPadX, top, wide_ - kPadX * 2.0f, kStripTall};
        canvas_.rect(at(strip), style::kIronLo);
        canvas_.rect(at(strip).grown(-line), style::kVoid);
        const int c = std::clamp(column_, 0, std::max(0, tree.count - 1));
        const int i = std::clamp(node_, 0, sim::kTalentNodes - 1);
        if (c < tree.count) {
            const sim::TalentColumn& column = tree.columns[c];
            const sim::TalentNode& node = column.nodes[i];
            const auto& col = ranks[size_t(c)];
            const int r = col[size_t(i)], most = sim::mostOf(i);
            const bool open = sim::talentOpen(col, i);
            const Box pic{strip.x + 8.0f, strip.y + (kStripTall - 48.0f) * 0.5f, 34.0f, 48.0f};
            canvas_.rect(at(pic), r > 0 ? style::kBlood : open ? style::kIron : style::kIronDk);
            icon(at(pic).grown(-line), node.icon, float(levelOf(col, i)));

            float tx = pic.right() + 14.0f;
            const float nameBase = strip.y + 28.0f, textBase = strip.y + 51.0f;
            tx += heading(canvas_, sx(tx), sy(nameBase), kSelName * u, style::kBoneHi, node.name) / u + 10.0f;
            const std::string count = sim::isMajor(i) ? (r > 0 ? "Taken" : "Major")
                                                      : std::to_string(r) + " / " + std::to_string(most);
            tx += controls::label(canvas_, sx(tx), sy(nameBase), kSelSmall * u, style::kBone2, count) / u + 10.0f;
            std::string where = column.name;
            int32_t fed[6];
            const int feeds = fedSkills(column, fed, 6);
            for (int s = 0; s < feeds; ++s) {
                const sim::SkillRow* skill = sim::skillNumbered(fed[s]);
                if (skill == nullptr) continue;
                where += (s == 0 ? "  \xC2\xB7  " : ", ") + std::string(skill->name);
            }
            tx += controls::label(canvas_, sx(tx), sy(nameBase), kSelSmall * u, style::kAshInk, where) / u + 10.0f;
            std::string why;
            if (!open) why = sim::isMajor(i) ? "opens at 10 points in a talent above" : "opens when the major above is taken";
            else if (r > 0 && !sim::talentTakeable(col, i)) why = "holds the talents below it";
            else if (r < most && free <= 0) why = "no talent points left";
            if (!why.empty()) controls::label(canvas_, sx(tx), sy(nameBase), kSelSmall * u, style::kAshInk, "\xC2\xB7  " + why);

            std::string text = effect(node, i, r);
            if (!sim::isMajor(i) && r > 0 && r < most) text += "   \xC2\xB7   next " + effect(node, i, r + 1);
            controls::label(canvas_, sx(pic.right() + 14.0f), sy(textBase), kSelText * u, style::kBone, text);
        }
        const float resetW = armed_ ? 170.0f : 90.0f;
        const Box reset{strip.right() - 10.0f - resetW, strip.y + (kStripTall - style::kButtonS) * 0.5f, resetW,
                        style::kButtonS};
        // The canvas's own button: an iron rim (blood once armed, lit under the pointer) round the
        // ash gradient, its word in the bold small capitals, bone2 or, armed, bloodHi.
        const bool none = sim::talentsSpent(ranks) == 0;
        const bool hot = over_ == kResetId && !none, down = hot && pressing_ == kResetId;
        const Box box = at(reset);
        canvas_.rect(box, armed_ ? (hot ? style::kBloodHi : style::kBlood) : hot ? style::kIronHi : style::kIron);
        const Box in = box.grown(-line);
        canvas_.shade(in, down ? kMinusFoot : style::kAsh4, down ? kMinusFoot : style::kAsh4, kMinusFoot, kMinusFoot);
        centred(canvas_, box, std::max(13.0f * u, kLeastWord),
                none ? style::kAshInk2 : armed_ ? style::kBloodHi : style::kBone2,
                armed_ ? "Click again to reset" : "Reset");
    }
}

void TalentWindow::rebuildCard(const Play& play) {
    card_.clear();
    if (carded_ < 0) return;
    const sim::Realm& realm = play.realm();
    const sim::Body& hero = realm.hero();
    const sim::TalentTree tree = sim::talentTree(hero.kin);
    const int c = carded_ / sim::kTalentNodes, i = carded_ % sim::kTalentNodes;
    if (c >= tree.count) return;
    const sim::TalentColumn& column = tree.columns[c];
    const sim::TalentNode& node = column.nodes[i];
    const auto& col = hero.talents[size_t(c)];
    const int r = col[size_t(i)], most = sim::mostOf(i);
    const bool open = sim::talentOpen(col, i);

    // **The card**, the plan's "Cards": the table's own words, nothing new. The name, the rank,
    // what it gives now and at the next point; a major's whole line; why a closed one is closed,
    // and why a taken one cannot give a point back.
    tip::Sheet sheet;
    sheet.name = node.name;
    sheet.nameTone = r > 0 ? tip::Tone::White : tip::Tone::Gray;
    sheet.base = std::string(column.name) + "  \xC2\xB7  " +
                 (sim::isMajor(i) ? (r > 0 ? std::string("Major, taken") : std::string("Major"))
                                  : std::to_string(r) + " / " + std::to_string(most));
    sheet.wide = 260.0f;
    tip::Section what;
    const auto prose = [&](const std::string& keyword, const std::string& text, tip::Tone tone) {
        tip::Row row;
        row.keyword = keyword;
        row.free = text;
        row.freeTone = tone;
        what.rows.push_back(row);
    };
    if (sim::isMajor(i)) {
        prose("", node.text, tip::Tone::White);
    } else {
        if (r > 0) prose("Now ", effect(node, i, r), tip::Tone::White);
        if (r < most) prose(r > 0 ? "Next " : "First point ", effect(node, i, r + 1), r > 0 ? tip::Tone::Gray : tip::Tone::White);
    }
    if (!open) {
        prose("", sim::isMajor(i) ? "Opens at 10 points in a talent above" : "Opens when the major above is taken",
              tip::Tone::Gray);
    } else if (r > 0 && !sim::talentTakeable(col, i)) {
        prose("", "Holds the talents below it", tip::Tone::Gray);
    }
    sheet.sections.push_back(what);

    const float colW = (wide_ - kPadX * 2.0f - kColumnGap * float(columns_ - 1)) / float(columns_);
    const float treeTop = kColumnTop + kColumnHead + 2.0f + (kColumnTall - kColumnHead - 2.0f - kTreeTall) * 0.5f;
    const Geo g = geoOf(kPadX + float(c) * (colW + kColumnGap), colW, treeTop, i);
    tip::draw(card_, sheet, placed(x_, y_, g.frame, u_), width_, height_);
}

}  // namespace mu::game
