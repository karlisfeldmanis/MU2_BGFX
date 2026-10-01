#include "game/ui/go_back.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "game/ui/controls.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"

namespace mu::game {
namespace {

using gfx::Box;

// The proposal's measures, in tip::unit() at 1080 lines.
// The whole plate at 85% of the page's measures: drawn at 1:1 it was too large over the HUD
// (the user, 2026-10-01: 'scale Go Back button little bit its to large').
constexpr float kScale = 0.85f;
constexpr float kTall = 64.0f;
constexpr float kPadLeft = 22.0f;
constexpr float kPadRight = 20.0f;
constexpr float kRuleGap = 18.0f;    // either side of the upright rule
constexpr float kRuleInset = 14.0f;  // the rule's ends, in from the frame's top and foot
constexpr float kRadius = 8.0f;
constexpr float kHairline = 1.5f;
constexpr float kOverHud = 0.0f;     // the frame's foot above the HUD plate's top edge; the gems rise past it
constexpr float kTitleSize = 21.0f;
constexpr float kWhereSize = 15.5f;
constexpr float kClockSize = 24.0f;
constexpr float kTitleBase = 28.0f;  // the two lines' baselines, down from the frame's top
constexpr float kWhereBase = 48.0f;
constexpr float kScrimAlpha = 0.72f;
constexpr float kInsideAlpha = 0.42f;
constexpr float kFadeSeconds = 0.4f;
constexpr int kLateSeconds = 30;

// The travel list's gold (game/ui/travel.cpp), the quest marker's: what the user chose for
// "yours" on 2026-09-30, kept here for the way back to the field.
constexpr uint32_t kGold = gfx::rgba(0.722f, 0.600f, 0.353f);
constexpr uint32_t kGoldHi = gfx::rgba(0.886f, 0.776f, 0.541f);

uint32_t faded(uint32_t abgr, float a) {
    return (abgr & 0x00FFFFFFu) | (gfx::rgbaByte(a * float(abgr >> 24) / 255.0f) << 24);
}

std::string clock(int seconds) {
    char text[16];
    std::snprintf(text, sizeof(text), "%d:%02d", seconds / 60, seconds % 60);
    return text;
}

// The outline of a box with round corners, `count` points clockwise from the top-left corner's
// start; each corner a quarter turn in six steps.
constexpr int kCornerSteps = 6;
constexpr int kOutline = 4 * (kCornerSteps + 1);
void outline(const Box& b, float r, float* xy) {
    const float cx[4] = {b.right() - r, b.right() - r, b.x + r, b.x + r};
    const float cy[4] = {b.y + r, b.bottom() - r, b.bottom() - r, b.y + r};
    const float start[4] = {-1.5707963f, 0.0f, 1.5707963f, 3.1415927f};
    int n = 0;
    for (int c = 0; c < 4; ++c) {
        for (int s = 0; s <= kCornerSteps; ++s) {
            const float a = start[c] + 1.5707963f * float(s) / float(kCornerSteps);
            xy[n++] = cx[c] + std::cos(a) * r;
            xy[n++] = cy[c] + std::sin(a) * r;
        }
    }
}

// A hairline round the box and nothing inside it: the canvas fills shapes, so the ring is laid
// down as the strip of quads between the outline and the same outline `thick` further in.
void ring(gfx::Canvas& canvas, const Box& box, float r, float thick, uint32_t ink) {
    float outer[kOutline * 2], inner[kOutline * 2];
    outline(box, r, outer);
    outline(box.grown(-thick), std::max(0.0f, r - thick), inner);
    const uint32_t inks[4] = {ink, ink, ink, ink};
    for (int i = 0; i < kOutline; ++i) {
        const int j = (i + 1) % kOutline;
        const float quad[8] = {outer[i * 2], outer[i * 2 + 1], outer[j * 2], outer[j * 2 + 1],
                               inner[j * 2], inner[j * 2 + 1], inner[i * 2], inner[i * 2 + 1]};
        canvas.polygon(quad, inks, 4);
    }
}

// The tracker's soft cloud (game/ui/tracker.cpp): a Gaussian each way, so it has no edge.
void scrim(gfx::Canvas& canvas, const Box& box, float u, float alpha) {
    const float cx = box.midX(), cy = box.midY();
    // Spread so the frame's own edge still sits in about half the dark: thinner, and the words
    // were lost over Noria's bright stones.
    const float sx = (box.w * 0.5f + 50.0f * u) / 1.6f, sy = (box.h * 0.5f + 18.0f * u) / 1.3f;
    constexpr int kColumns = 12, kRows = 8;
    const auto at = [&](int i, int j) {
        const float gx = -3.0f + 6.0f * float(i) / kColumns;
        const float gy = -3.0f + 6.0f * float(j) / kRows;
        return faded(style::kVoid, kScrimAlpha * alpha * std::exp(-0.5f * (gx * gx + gy * gy)));
    };
    for (int j = 0; j < kRows; ++j) {
        for (int i = 0; i < kColumns; ++i) {
            const float x0 = cx + sx * (-3.0f + 6.0f * float(i) / kColumns);
            const float y0 = cy + sy * (-3.0f + 6.0f * float(j) / kRows);
            canvas.shade({x0, y0, sx * 6.0f / kColumns, sy * 6.0f / kRows}, at(i, j), at(i + 1, j),
                         at(i + 1, j + 1), at(i, j + 1));
        }
    }
}

const char* const kTitle = "Go Back!";
const char* const kClosed = "Go Back! has closed";

// Where the frame stands for what it says.
Box layout(int width, float plateTop, bool closed, const std::string& where, float u) {
    float inside = 0.0f;
    if (closed) {
        inside = controls::capsWidth(kTitleSize * u, kClosed, 0.0f);
    } else {
        const float words = std::max(controls::capsWidth(kTitleSize * u, kTitle, 0.0f),
                                     controls::labelWidth(kWhereSize * u, where));
        inside = words + kRuleGap * 2.0f * u + std::round(u) +
                 controls::capsWidth(kClockSize * u, "8:88", 0.0f);
    }
    const float w = std::round(inside + (kPadLeft + kPadRight) * u);
    const float h = std::round(kTall * u);
    const float bottom = std::round(plateTop - kOverHud * u);
    return {std::round((float(width) - w) * 0.5f), bottom - h, w, h};
}

}  // namespace

bool GoBackPlate::update(float seconds, bool shown, int secondsLeft, bool closed,
                         const std::string& where, const Pointer& pointer, int width, int height,
                         float plateTop) {
    const float target = shown ? 1.0f : 0.0f;
    const float step = seconds / kFadeSeconds;
    alpha_ = alpha_ < target ? std::min(target, alpha_ + step) : std::max(target, alpha_ - step);
    if (alpha_ <= 0.0f) {
        if (built_) canvas_.clear();
        built_ = false;
        box_ = {};
        return false;
    }
    const float u = tip::unit() * kScale;
    // The closed line is a reading, not a button.
    box_ = layout(width, plateTop, closed, where, u);
    const bool hover = shown && !closed && box_.has(pointer.x, pointer.y);
    const bool clicked = hover && pointer.pressed;

    Drawn now;
    now.width = width;
    now.height = height;
    now.plateTop = int(plateTop);
    now.secondsLeft = secondsLeft;
    now.closed = closed;
    now.hover = hover;
    now.alpha = int(std::round(alpha_ * 64.0f));
    now.where = where;
    if (!built_ || !(now == drawn_)) {
        drawn_ = now;
        built_ = true;
        rebuild(now);
    }
    return clicked;
}

void GoBackPlate::rebuild(const Drawn& now) {
    canvas_.clear();
    const float u = tip::unit() * kScale;
    const float a = float(now.alpha) / 64.0f;
    const Box b = box_;
    const bool late = !now.closed && now.secondsLeft <= kLateSeconds;

    scrim(canvas_, b, u, a);
    const uint32_t rim = now.closed ? style::kIronLo
                         : late     ? (now.hover ? style::kBloodHi : style::kBlood)
                         : now.hover ? kGoldHi
                                     : kGold;
    // A breath of dark inside the frame as well: the cloud alone left the words thin over
    // Noria's bright stones.
    tip::panel(canvas_, b, kRadius * u, faded(style::kVoid, kInsideAlpha * a),
               faded(style::kVoid, kInsideAlpha * a));
    ring(canvas_, b, kRadius * u, std::max(1.0f, kHairline * u), faded(rim, a));

    const float left = b.x + kPadLeft * u;
    if (now.closed) {
        controls::caps(canvas_, left, controls::middle(b.y, b.h, kTitleSize * u), kTitleSize * u,
                       faded(style::kAshInk, a), kClosed, 0.0f);
        return;
    }
    const uint32_t word = late ? style::kBloodHi : now.hover ? style::kBoneHi : kGoldHi;
    controls::caps(canvas_, left, b.y + kTitleBase * u, kTitleSize * u, faded(word, a), kTitle,
                   0.0f);
    controls::label(canvas_, left, b.y + kWhereBase * u, kWhereSize * u,
                    faded(style::kBone2, a * 0.85f), now.where);

    // The clock ranged against the right, behind its rule.
    const float right = b.right() - kPadRight * u;
    const float clockWide = controls::capsWidth(kClockSize * u, "8:88", 0.0f);
    const float ruleX = std::round(right - clockWide - kRuleGap * u);
    canvas_.rect({ruleX, b.y + kRuleInset * u, std::max(1.0f, std::round(u)), b.h - 2.0f * kRuleInset * u},
                 faded(style::kIron, a * 0.55f));
    const std::string text = clock(now.secondsLeft);
    const float wide = controls::capsWidth(kClockSize * u, text, 0.0f);
    controls::caps(canvas_, right - wide, controls::middle(b.y, b.h, kClockSize * u),
                   kClockSize * u, faded(late ? style::kBloodHi : kGoldHi, a), text, 0.0f);
}

}  // namespace mu::game
