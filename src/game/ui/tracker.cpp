#include "game/ui/tracker.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

#include "game/play.h"
#include "game/ui/controls.h"
#include "game/ui/panel.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"

namespace mu::game {
namespace {

using gfx::Box;
using quest_marks::faded;
using quest_marks::StepMark;

// The proposal's measures, in tip::unit() at 1080 lines.
constexpr float kWide = 300.0f;
constexpr float kRight = 44.0f;   // the right edge, in from the screen's
constexpr float kTop = 196.0f;
constexpr float kTitle = 18.0f, kTitleTrack = 0.08f;
constexpr float kStep = 16.0f;    // a step's words
constexpr float kRowGap = 10.0f;  // between rows
constexpr float kBarGap = 6.0f;   // a count's bar under its row
constexpr float kMarkRoom = 24.0f;
constexpr float kScrimWide = 260.0f;
constexpr float kScrimAlpha = 0.55f;
// The moments, in seconds.
constexpr float kFadeSeconds = style::kOpenSeconds;  // in and out of a window's way
constexpr float kBarSeconds = 0.25f;
constexpr float kEmberSeconds = 0.6f;
constexpr float kBannerIn = style::kOpenSeconds, kBannerOut = 0.4f;
// The edge pointer: how far in from the frame's edge it stands, and when the giver counts as
// on the frame (the blade over his head takes over).
constexpr float kEdgeInset = 64.0f;
// The pointer's old gold, the quest marker's own two faces (game/ui/beacon.cpp).
constexpr uint32_t kGoldLit = gfx::rgba(0.784f, 0.635f, 0.345f);
constexpr uint32_t kGoldShade = gfx::rgba(0.612f, 0.478f, 0.235f);
// MU's Zen, the loot tone: gold is loot (style.h).
constexpr uint32_t kZenGold = gfx::rgba(1.0f, 0.8f, 0.102f);

// A line in the label face over its drop, both at `alpha`: the controls' own label with the drop
// faded too, so a banner going out leaves no dark ghost of itself.
float line(gfx::Canvas& canvas, float x, float baseline, float size, uint32_t ink, float alpha,
           const std::string& text) {
    const gfx::Face* face = controls::labelFace();
    if (!face) return 0.0f;
    const bgfx::TextureHandle texture = controls::labelTexture();
    canvas.lettered(*face, texture, x + 1.0f, baseline + 1.0f, size, 0.0f,
                    faded(style::kDrop, alpha), text);
    return canvas.lettered(*face, texture, x, baseline, size, 0.0f, faded(ink, alpha), text);
}

float lineWidth(float size, const std::string& text) {
    const gfx::Face* face = controls::labelFace();
    return face ? face->measure(size, text) : 0.0f;
}

// A title in the windows' Cinzel, tracked, over its drop.
float title(gfx::Canvas& canvas, float x, float baseline, float size, float track, uint32_t ink,
            float alpha, const std::string& text) {
    const gfx::Face* face = panel::titleFace();
    if (!face) return 0.0f;
    const float tracking = size * track;
    canvas.lettered(*face, panel::titleTexture(), x + 1.0f, baseline + 1.0f, size, tracking,
                    faded(style::kDrop, alpha), text);
    return canvas.lettered(*face, panel::titleTexture(), x, baseline, size, tracking,
                           faded(ink, alpha), text);
}

float titleWidth(float size, float track, const std::string& text) {
    const gfx::Face* face = panel::titleFace();
    if (!face || text.empty()) return 0.0f;
    return face->measure(size, text) + size * track * float(text.size() - 1);
}

std::string grouped(int64_t n) { return panel::commas(n); }

}  // namespace

namespace quest_marks {

uint32_t faded(uint32_t abgr, float alpha) {
    const float a = float((abgr >> 24) & 0xFFu) * std::clamp(alpha, 0.0f, 1.0f);
    return (abgr & 0x00FFFFFFu) | (uint32_t(std::lround(a)) << 24);
}

void mark(gfx::Canvas& canvas, StepMark kind, float cx, float cy, float u, float alpha) {
    constexpr int kSides = 20;
    constexpr float kTau = 6.2831853f;
    if (kind == StepMark::Done) {
        // The check: two strokes, each a thin quad, in ash ink.
        const uint32_t ink = faded(style::kAshInk, alpha);
        const float pts[3][2] = {{cx - 4.0f * u, cy + 0.4f * u}, {cx - 1.2f * u, cy + 3.0f * u},
                                 {cx + 4.0f * u, cy - 3.0f * u}};
        const float half = 0.9f * u;
        for (int s = 0; s < 2; ++s) {
            const float dx = pts[s + 1][0] - pts[s][0], dy = pts[s + 1][1] - pts[s][1];
            const float len = std::max(1e-3f, std::sqrt(dx * dx + dy * dy));
            const float nx = -dy / len * half, ny = dx / len * half;
            const float ex = dx / len * half * 0.6f, ey = dy / len * half * 0.6f;
            const float xy[8] = {pts[s][0] + nx - ex,     pts[s][1] + ny - ey,
                                 pts[s + 1][0] + nx + ex, pts[s + 1][1] + ny + ey,
                                 pts[s + 1][0] - nx + ex, pts[s + 1][1] - ny + ey,
                                 pts[s][0] - nx - ex,     pts[s][1] - ny - ey};
            const uint32_t c[4] = {ink, ink, ink, ink};
            canvas.polygon(xy, c, 4);
        }
        return;
    }
    if (kind == StepMark::Ready) {
        float xy[kSides * 2];
        uint32_t c[kSides];
        for (int i = 0; i < kSides; ++i) {
            const float a = kTau * float(i) / float(kSides);
            xy[i * 2] = cx + std::cos(a) * 4.0f * u;
            xy[i * 2 + 1] = cy + std::sin(a) * 4.0f * u;
            c[i] = faded(style::kBlood, alpha);
        }
        canvas.polygon(xy, c, kSides);
        return;
    }
    // A ring, 1.6u wide at 4.6u, as quads round its circumference.
    const uint32_t ink = faded(kind == StepMark::Live ? style::kIronHi : style::kIronLo, alpha);
    const float outer = 5.4f * u, inner = 3.8f * u;
    for (int i = 0; i < kSides; ++i) {
        const float a0 = kTau * float(i) / float(kSides), a1 = kTau * float(i + 1) / float(kSides);
        const float xy[8] = {cx + std::cos(a0) * outer, cy + std::sin(a0) * outer,
                             cx + std::cos(a1) * outer, cy + std::sin(a1) * outer,
                             cx + std::cos(a1) * inner, cy + std::sin(a1) * inner,
                             cx + std::cos(a0) * inner, cy + std::sin(a0) * inner};
        const uint32_t c[4] = {ink, ink, ink, ink};
        canvas.polygon(xy, c, 4);
    }
}

std::string wait(int64_t seconds) {
    if (seconds < 60) return "under a minute";
    const int64_t minutes = seconds / 60;
    char text[48];
    if (minutes < 60) std::snprintf(text, sizeof(text), "%lldm", (long long)minutes);
    else std::snprintf(text, sizeof(text), "%lldh %02lldm", (long long)(minutes / 60),
                       (long long)(minutes % 60));
    return text;
}

}  // namespace quest_marks

bool Tracker::Drawn::operator==(const Drawn& o) const {
    if (quest != o.quest || shown != o.shown || width != o.width || height != o.height ||
        minutesLeft != o.minutesLeft || pointing != o.pointing || pointX != o.pointX ||
        pointY != o.pointY || pointMetres != o.pointMetres ||
        progress.state != o.progress.state) {
        return false;
    }
    for (int i = 0; i < sim::kQuestSteps; ++i) {
        if (counts[i] != o.counts[i] || embers[i] != o.embers[i] ||
            progress.counts[i] != o.progress.counts[i]) {
            return false;
        }
    }
    return true;
}

void Tracker::open(const gfx::Interface& interface) {
    interface.adopt(canvas_);
    interface.adopt(banner_);
    opened_ = true;
}

void Tracker::close() {
    canvas_.clear();
    banner_.clear();
    seen_ = false;
    built_ = false;
    bannerAge_ = -1.0f;
    bannerDrawn_ = -1;
}

void Tracker::update(float seconds, const Play& play, bool hidden, const float* viewProj,
                     int width, int height) {
    if (!opened_ || !play.isOpen()) return;
    const sim::Realm& realm = play.realm();
    const float u = tip::unit();

    // ---- what moved, since last frame: the banners and the embers ------------------------------
    for (int q = 0; q < sim::kQuests; ++q) {
        const sim::QuestProgress& now = realm.quest(q);
        if (seen_) {
            const sim::QuestProgress& was = last_[q];
            const sim::QuestRow& row = sim::questAt(q);
            if (now.state == sim::QuestState::Active && was.state != sim::QuestState::Active) {
                bannerKicker_ = "Quest accepted";
                bannerTitle_ = row.title;
                bannerLine_.clear();
                bannerZen_.clear();
                bannerHold_ = 2.2f;
                bannerAge_ = 0.0f;
                for (int s = 0; s < sim::kQuestSteps; ++s) counts_[s] = float(now.counts[s]);
            } else if (now.state == sim::QuestState::Ready && was.state == sim::QuestState::Active) {
                bannerKicker_ = row.title;
                bannerTitle_ = std::string("Return to ") + row.giverName;
                bannerLine_.clear();
                bannerZen_.clear();
                bannerHold_ = 2.2f;
                bannerAge_ = 0.0f;
            } else if (now.state == sim::QuestState::Resting &&
                       was.state == sim::QuestState::Ready) {
                bannerKicker_ = "Quest complete";
                bannerTitle_ = row.title;
                bannerLine_ = grouped(row.experience) + " experience      ";
                bannerZen_ = grouped(row.zen) + " Zen";
                bannerHold_ = 3.0f;
                bannerAge_ = 0.0f;
            }
            if (q == quest_) {
                for (int s = 0; s < sim::kQuestSteps; ++s) {
                    if (now.counts[s] > was.counts[s]) ember_[s] = 1.0f;
                }
            }
        }
        last_[q] = now;
    }
    seen_ = true;

    // The quest followed: the first that is live, then the first resting one (for its wait).
    quest_ = -1;
    for (int q = 0; q < sim::kQuests && quest_ < 0; ++q) {
        const sim::QuestState state = realm.quest(q).state;
        if (state == sim::QuestState::Active || state == sim::QuestState::Ready) quest_ = q;
    }
    for (int q = 0; q < sim::kQuests && quest_ < 0; ++q) {
        if (realm.quest(q).state == sim::QuestState::Resting && !realm.questOffered(q)) quest_ = q;
    }

    // ---- the eased pieces -----------------------------------------------------------------------
    const float step = seconds / kFadeSeconds;
    const bool wanted = quest_ >= 0 && !hidden;
    shown_ = wanted ? std::min(1.0f, shown_ + step) : std::max(0.0f, shown_ - step);
    if (quest_ >= 0) {
        const sim::QuestProgress& now = realm.quest(quest_);
        for (int s = 0; s < sim::kQuestSteps; ++s) {
            const float target = float(now.counts[s]);
            const float goal = float(std::max(1, realm.questGoal(quest_, s)));
            const float speed = goal * seconds / kBarSeconds;
            counts_[s] = counts_[s] < target ? std::min(target, counts_[s] + std::max(speed, seconds))
                                             : target;
            ember_[s] = std::max(0.0f, ember_[s] - seconds / kEmberSeconds);
        }
    }

    // ---- the edge pointer: the giver, when he is the next step and off the frame ---------------
    pointing_ = false;
    if (quest_ >= 0 && realm.quest(quest_).state == sim::QuestState::Ready && viewProj) {
        const sim::QuestRow& row = sim::questAt(quest_);
        const content::Tables* tables = realm.tables();
        for (size_t f = 0; tables && f < tables->folk.size(); ++f) {
            if (tables->folk[f].number != row.giver) continue;
            float x = 0.0f, y = 0.0f;
            if (!play.folkCrownOf(int(f), viewProj, width, height, &x, &y)) break;
            const float inset = kEdgeInset * u;
            if (x >= 0.0f && x <= float(width) && y >= 0.0f && y <= float(height)) break;
            const float cx = float(width) * 0.5f, cy = float(height) * 0.5f;
            const float dx = x - cx, dy = y - cy;
            const float sx = dx != 0.0f ? (cx - inset) / std::fabs(dx) : 1e9f;
            const float sy = dy != 0.0f ? (cy - inset) / std::fabs(dy) : 1e9f;
            const float s = std::min(sx, sy);
            pointX_ = cx + dx * s;
            pointY_ = cy + dy * s;
            pointAngle_ = std::atan2(dy, dx);
            const sim::Body& hero = realm.hero();
            const float mx = float(tables->folk[f].x) - hero.x, my = float(tables->folk[f].y) - hero.y;
            pointMetres_ = int(std::lround(std::sqrt(mx * mx + my * my)));
            pointing_ = true;
            break;
        }
    }

    // ---- the banner's life ------------------------------------------------------------------
    int bannerStep = -1;
    if (bannerAge_ >= 0.0f) {
        bannerAge_ += seconds;
        float alpha = 1.0f;
        if (bannerAge_ < kBannerIn) alpha = bannerAge_ / kBannerIn;
        else if (bannerAge_ > kBannerIn + bannerHold_) {
            alpha = 1.0f - (bannerAge_ - kBannerIn - bannerHold_) / kBannerOut;
        }
        if (alpha <= 0.0f) bannerAge_ = -1.0f;
        else bannerStep = int(std::lround(alpha * 64.0f));
    }
    if (bannerStep != bannerDrawn_) {
        bannerDrawn_ = bannerStep;
        rebuildBanner(width, height);
    }

    Drawn now;
    now.quest = shown_ > 0.0f ? quest_ : -1;
    now.shown = int(std::lround(shown_ * 64.0f));
    now.width = width;
    now.height = height;
    now.pointing = pointing_;
    if (pointing_) {
        now.pointX = int(pointX_);
        now.pointY = int(pointY_);
        now.pointMetres = pointMetres_;
    }
    if (now.quest >= 0) {
        now.progress = realm.quest(quest_);
        for (int s = 0; s < sim::kQuestSteps; ++s) {
            now.counts[s] = int(counts_[s] * 8.0f);
            now.embers[s] = int(ember_[s] * 32.0f);
        }
        if (now.progress.state == sim::QuestState::Resting) {
            now.minutesLeft = std::max<int64_t>(0, now.progress.availableAt - realm.wallClock()) / 60;
        }
    }
    if (built_ && now == drawn_) return;
    drawn_ = now;
    built_ = true;
    rebuild(play, width, height);
}

void Tracker::rebuild(const Play& play, int width, int height) {
    canvas_.clear();
    const float u = tip::unit();
    const sim::Realm& realm = play.realm();

    // The pointer is the giver's, and shows whether the tracker is in a window's way or not.
    if (drawn_.pointing) {
        const float s = 11.0f * u;
        const float c = std::cos(pointAngle_), sn = std::sin(pointAngle_);
        // A slim blade pointing along the angle: tip, the two back corners, the notch.
        const auto at = [&](float fx, float fy, float* out) {
            out[0] = pointX_ + (fx * c - fy * sn) * s;
            out[1] = pointY_ + (fx * sn + fy * c) * s;
        };
        float lit[6], shade[6];
        at(1.0f, 0.0f, lit + 0);
        at(-0.9f, -0.75f, lit + 2);
        at(-0.45f, 0.0f, lit + 4);
        at(1.0f, 0.0f, shade + 0);
        at(-0.45f, 0.0f, shade + 2);
        at(-0.9f, 0.75f, shade + 4);
        const uint32_t cl[3] = {kGoldLit, kGoldLit, kGoldLit};
        const uint32_t cs[3] = {kGoldShade, kGoldShade, kGoldShade};
        canvas_.polygon(lit, cl, 3);
        canvas_.polygon(shade, cs, 3);
        // His name and the distance, on the side of the pointer away from the edge.
        const bool left = pointX_ > float(width) * 0.5f;
        const std::string name = sim::questAt(std::max(0, drawn_.quest)).giverName;
        const std::string metres = std::to_string(drawn_.pointMetres) + " m";
        const float nameSize = 14.0f * u, metreSize = 13.0f * u;
        const float gap = 16.0f * u;
        const float nameW = titleWidth(nameSize, 0.06f, name), metreW = lineWidth(metreSize, metres);
        const float wide = std::max(nameW, metreW);
        const float x = left ? pointX_ - gap - wide : pointX_ + gap;
        const float top = std::clamp(pointY_ - 14.0f * u, 8.0f * u, float(height) - 40.0f * u);
        title(canvas_, left ? x + wide - nameW : x, top + 12.0f * u, nameSize, 0.06f,
              style::kBoneHi, 1.0f, name);
        line(canvas_, left ? x + wide - metreW : x, top + 28.0f * u, metreSize, style::kBone2, 1.0f,
             metres);
    }

    if (drawn_.quest < 0) return;
    const float alpha = float(drawn_.shown) / 64.0f;
    const int q = drawn_.quest;
    const sim::QuestRow& row = sim::questAt(q);
    const sim::QuestProgress& now = drawn_.progress;
    const float right = float(width) - kRight * u;
    const float left = right - kWide * u;
    float y = kTop * u;

    // The scrim: the banners' own soft cloud (a Gaussian each way, as the map name's), its
    // middle at the screen's right edge and the tracker's middle, so it has no edge anywhere. A
    // rectangle fading one way only showed its top and bottom over bright paving.
    {
        const float tall = now.state == sim::QuestState::Resting
                               ? 70.0f
                               : 60.0f + float(row.stepCount) * (kStep + kRowGap + kBarGap);
        const float cx = float(width), cy = y + tall * 0.5f * u;
        const float sx = kScrimWide * u * 0.55f, sy = tall * u * 0.42f;
        constexpr int kColumns = 12, kRows = 12;
        const auto at = [&](int i, int j) {
            const float gx = -3.0f + 3.0f * float(i) / kColumns;  // the left half only
            const float gy = -3.0f + 6.0f * float(j) / kRows;
            return faded(style::kVoid, kScrimAlpha * alpha * std::exp(-0.5f * (gx * gx + gy * gy)));
        };
        for (int j = 0; j < kRows; ++j) {
            for (int i = 0; i < kColumns; ++i) {
                const float x0 = cx + sx * (-3.0f + 3.0f * float(i) / kColumns);
                const float y0 = cy + sy * (-3.0f + 6.0f * float(j) / kRows);
                canvas_.shade({x0, y0, sx * 3.0f / kColumns, sy * 6.0f / kRows}, at(i, j),
                              at(i + 1, j), at(i + 1, j + 1), at(i, j + 1));
            }
        }
    }

    if (now.state == sim::QuestState::Resting) {
        // Handed in, and not his to give again yet: one quiet line and the wait.
        controls::caps(canvas_, left, y + 12.0f * u, style::kKickerSize * u,
                       faded(style::kAshInk, alpha), std::string(row.giverName));
        title(canvas_, left, y + 36.0f * u, 15.0f * u, 0.06f, style::kBone2, alpha, row.title);
        line(canvas_, left, y + 58.0f * u, 15.0f * u, style::kAshInk, alpha,
             "Offered again in " + quest_marks::wait(drawn_.minutesLeft * 60 + 59));
        return;
    }

    controls::caps(canvas_, left, y + 12.0f * u, style::kKickerSize * u,
                   faded(style::kAshInk, alpha), "Tracked quest");
    y += 24.0f * u;
    title(canvas_, left, y + kTitle * u, kTitle * u, kTitleTrack, style::kBoneHi, alpha, row.title);
    y += kTitle * u + 10.0f * u;
    {
        // The only line: iron, fading in over its first third.
        const float third = kWide * u / 3.0f;
        const uint32_t iron = faded(style::kIron, alpha), clear = faded(style::kIron, 0.0f);
        canvas_.shade({left, y, third, std::max(1.0f, u)}, clear, iron, iron, clear);
        canvas_.rect({left + third, y, kWide * u - third, std::max(1.0f, u)}, iron);
    }
    y += 12.0f * u;

    const bool ready = now.state == sim::QuestState::Ready;
    for (int s = 0; s < row.stepCount; ++s) {
        const sim::QuestStepRow& want = row.steps[s];
        const int goal = realm.questGoal(q, s);
        const bool counted = want.kind == sim::QuestStepKind::Clear;
        const bool done = counted ? now.counts[s] >= goal : false;
        StepMark kind = StepMark::Live;
        uint32_t ink = style::kBoneHi;
        if (counted && done) {
            kind = StepMark::Done;
            ink = style::kAshInk;
        } else if (!counted) {
            kind = ready ? StepMark::Ready : StepMark::Waiting;
            ink = ready ? style::kBloodHi : style::kAshInk2;
        }
        const float rowTall = kStep * u;
        // The ember under a count that just moved: the hover's own, rising from the row's foot.
        const float ember = float(drawn_.embers[s]) / 32.0f;
        if (ember > 0.0f) {
            const uint32_t hot = faded(style::kBlood, style::kEmberAlpha * ember * alpha);
            const uint32_t clear = faded(style::kBlood, 0.0f);
            canvas_.shade({left - 8.0f * u, y - 4.0f * u, kWide * u + 16.0f * u, rowTall + 12.0f * u},
                          clear, clear, hot, hot);
        }
        quest_marks::mark(canvas_, kind, left + 7.0f * u, y + rowTall * 0.5f, u, alpha);
        const float baseline = controls::middle(y, rowTall, kStep * u);
        line(canvas_, left + kMarkRoom * u, baseline, kStep * u, ink, alpha, want.line);
        if (counted) {
            const float shownCount = float(drawn_.counts[s]) / 8.0f;
            const std::string figure = std::to_string(int(std::floor(shownCount))) + " / " +
                                       std::to_string(goal);
            const float fw = lineWidth(kStep * u, figure);
            line(canvas_, right - fw, baseline, kStep * u, done ? style::kAshInk : style::kBoneHi,
                 alpha, figure);
            if (!done) {
                const float barY = y + rowTall + kBarGap * u * 0.5f;
                const float barLeft = left + kMarkRoom * u, barWide = right - barLeft;
                const float h = std::max(1.0f, 2.0f * u);
                canvas_.rect({barLeft, barY, barWide, h}, faded(style::kIronDk, alpha));
                const float share = std::clamp(shownCount / float(std::max(1, goal)), 0.0f, 1.0f);
                if (share > 0.0f) {
                    canvas_.rect({barLeft, barY, std::round(barWide * share), h},
                                 faded(style::kBone, alpha));
                }
                y += kBarGap * u;
            }
        }
        y += rowTall + kRowGap * u;
    }
}

void Tracker::rebuildBanner(int width, int height) {
    banner_.clear();
    if (bannerDrawn_ <= 0) return;
    (void)height;
    const float alpha = float(bannerDrawn_) / 64.0f;
    const float u = tip::unit();
    const float cx = float(width) * 0.5f;
    // In the arrival banner's grammar (game/ui/arrival.h) but in the upper band, over the town
    // and clear of the hero's head: the map's name holds the lower third, and at 300u the first
    // try sat across the character and the tile over him.
    const float top = 130.0f * u;
    const float rise = (1.0f - std::min(1.0f, alpha * 2.0f)) * style::kOpenRise * u;

    // The map name's own shadow behind the whole stack (game/ui/arrival.cpp): a Gaussian each
    // way at 52% in its middle, laid as a grid of shaded quads so it has no edge and fades with
    // the words. The user, 2026-09-28: "use similar shadow behind quest messages as map names".
    // Wider and a little taller than the map name's, for the longer title and the reward line.
    {
        const float cy = top + (bannerLine_.empty() ? 40.0f : 52.0f) * u;
        const float sx = 290.0f * u, sy = (bannerLine_.empty() ? 50.0f : 60.0f) * u;
        constexpr int kColumns = 16, kRows = 8;
        const auto at = [&](int i, int j) {
            const float gx = -3.0f + 6.0f * float(i) / kColumns;
            const float gy = -3.0f + 6.0f * float(j) / kRows;
            return quest_marks::faded(gfx::rgba(0.0f, 0.0f, 0.0f, 1.0f),
                                      0.52f * alpha * std::exp(-0.5f * (gx * gx + gy * gy)));
        };
        for (int j = 0; j < kRows; ++j) {
            for (int i = 0; i < kColumns; ++i) {
                const float x0 = cx + sx * (-3.0f + 6.0f * float(i) / kColumns);
                const float y0 = cy + sy * (-3.0f + 6.0f * float(j) / kRows);
                banner_.shade({x0, y0, sx * 6.0f / kColumns, sy * 6.0f / kRows}, at(i, j),
                              at(i + 1, j), at(i + 1, j + 1), at(i, j + 1));
            }
        }
    }

    const float kickerSize = 13.0f * u;
    const float kw = controls::capsWidth(kickerSize, bannerKicker_);
    const float kickerBase = top + 14.0f * u + rise;
    controls::caps(banner_, cx - kw * 0.5f, kickerBase, kickerSize, faded(style::kAshInk, alpha),
                   bannerKicker_);
    const float ruleWide = 70.0f * u, ruleGap = 14.0f * u;
    const float ruleY = kickerBase - kickerSize * 0.35f;
    const uint32_t iron = faded(style::kIron, alpha), clear = faded(style::kIron, 0.0f);
    const float h = std::max(1.0f, u);
    banner_.shade({cx - kw * 0.5f - ruleGap - ruleWide, ruleY, ruleWide, h}, clear, iron, iron, clear);
    banner_.shade({cx + kw * 0.5f + ruleGap, ruleY, ruleWide, h}, iron, clear, clear, iron);

    const float titleSize = 30.0f * u;
    const float tw = titleWidth(titleSize, 0.1f, bannerTitle_);
    title(banner_, cx - tw * 0.5f, top + 56.0f * u + rise, titleSize, 0.1f, style::kBoneHi, alpha,
          bannerTitle_);

    if (!bannerLine_.empty()) {
        const float size = 16.0f * u;
        const float lw = lineWidth(size, bannerLine_), zw = lineWidth(size, bannerZen_);
        const float x = cx - (lw + zw) * 0.5f;
        const float base = top + 88.0f * u + rise;
        line(banner_, x, base, size, style::kBone, alpha, bannerLine_);
        line(banner_, x + lw, base, size, kZenGold, alpha, bannerZen_);
    }
}

}  // namespace mu::game
