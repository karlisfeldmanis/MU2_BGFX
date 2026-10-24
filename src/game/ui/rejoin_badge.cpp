#include "game/ui/rejoin_badge.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "game/ui/controls.h"
#include "game/ui/tip.h"

namespace mu::game {
namespace {

// The line chip's measures (line_chip.cpp), at 1080 lines and three quarters.
constexpr float kScale = 0.75f;
constexpr float kTall = 36.0f;
constexpr float kPad = 13.0f;
constexpr float kText = 16.5f;
constexpr float kGap = 10.0f;
constexpr float kRadius = 3.0f;
// The arc: a ring this wide across and this thick, its tail fading behind its head over this
// much of the turn, round once in this long.
constexpr float kRing = 15.0f;
constexpr float kThick = 2.2f;
constexpr float kTail = 0.72f;
constexpr int kSegments = 28;
constexpr float kTurnSeconds = 1.1f;
constexpr float kFadeSeconds = 0.35f;

constexpr uint32_t kBack = gfx::rgba(0.047f, 0.043f, 0.035f, 0.78f);
constexpr uint32_t kTrack = gfx::rgba(0.290f, 0.267f, 0.220f, 0.55f);
constexpr uint32_t kGold = gfx::rgba(0.890f, 0.761f, 0.478f);
constexpr uint32_t kInk = gfx::rgba(0.722f, 0.694f, 0.639f);
constexpr uint32_t kQuiet = gfx::rgba(0.502f, 0.478f, 0.427f);

uint32_t faded(uint32_t abgr, float a) {
    return (abgr & 0x00FFFFFFu) | (gfx::rgbaByte(a * float(abgr >> 24) / 255.0f) << 24);
}

// One piece of the ring from `from` to `to` turns, clockwise from twelve o'clock.
void piece(gfx::Canvas& canvas, float cx, float cy, float r, float thick, float from, float to, uint32_t ink) {
    constexpr float kTau = 6.28318531f;
    const float a0 = from * kTau, a1 = to * kTau;
    const float in = r - thick * 0.5f, out = r + thick * 0.5f;
    const float xy[8] = {cx + std::sin(a0) * out, cy - std::cos(a0) * out, cx + std::sin(a1) * out,
                         cy - std::cos(a1) * out, cx + std::sin(a1) * in,  cy - std::cos(a1) * in,
                         cx + std::sin(a0) * in,  cy - std::cos(a0) * in};
    const uint32_t inks[4] = {ink, ink, ink, ink};
    canvas.polygon(xy, inks, 4);
}

}  // namespace

void RejoinBadge::update(float seconds, bool up, int count, float left, float top) {
    if (!up && alpha_ <= 0.0f) {
        if (up_) canvas_.clear();
        up_ = false;
        return;
    }
    up_ = true;
    const float step = seconds / kFadeSeconds;
    alpha_ = up ? std::min(1.0f, alpha_ + step) : std::max(0.0f, alpha_ - step);
    turn_ = std::fmod(turn_ + seconds / kTurnSeconds, 1.0f);

    // It turns, so it is laid again every frame it shows: a few dozen quads.
    canvas_.clear();
    if (alpha_ <= 0.0f) return;
    const float u = tip::unit() * kScale;
    const float a = alpha_ * alpha_ * (3.0f - 2.0f * alpha_);
    const std::string word = "Reconnecting";
    char after[16] = "";
    if (count > 0) std::snprintf(after, sizeof after, "%d s", count);
    const float wordWide = controls::labelWidth(kText * u, word);
    const float afterWide = after[0] ? kGap * u + controls::labelWidth(kText * u, after) : 0.0f;
    const float ring = kRing * u;
    const gfx::Box b{std::round(left), std::round(top),
                     std::round(2.0f * kPad * u + ring + kGap * u + wordWide + afterWide), std::round(kTall * u)};
    tip::panel(canvas_, b, kRadius * u, faded(kBack, a), faded(kBack, a));

    const float cx = b.x + kPad * u + ring * 0.5f, cy = b.midY();
    const float r = ring * 0.5f - kThick * u * 0.5f, thick = std::max(1.5f, kThick * u);
    piece(canvas_, cx, cy, r, thick, 0.0f, 0.5f, faded(kTrack, a));
    piece(canvas_, cx, cy, r, thick, 0.5f, 1.0f, faded(kTrack, a));
    for (int i = 0; i < kSegments; ++i) {
        const float t0 = float(i) / float(kSegments), t1 = float(i + 1) / float(kSegments);
        const float shade = t1 * t1;  // the head bright, the tail dying away behind it
        piece(canvas_, cx, cy, r, thick, turn_ - kTail * (1.0f - t0), turn_ - kTail * (1.0f - t1),
              faded(kGold, a * shade));
    }

    const float base = controls::middle(b.y, b.h, kText * u);
    float x = cx + ring * 0.5f + kGap * u;
    x += controls::label(canvas_, x, base, kText * u, faded(kInk, a), word);
    if (after[0]) controls::label(canvas_, x + kGap * u, base, kText * u, faded(kQuiet, a), after);
}

}  // namespace mu::game
