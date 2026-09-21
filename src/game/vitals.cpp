#include "game/vitals.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "game/panel.h"
#include "game/play.h"

namespace mu::game {

namespace {

// ---- the palette, Vitals.cs's -----------------------------------------------------------
// A warm off-white for the type, an outline rather than a drop shadow round it (a drop only
// guarantees contrast on two of a glyph's four sides), a dark rim rather than a pale frame, a
// shadow that holds the bar off the world instead of a third edge, a dead grey track, a deep
// unsaturated crimson, and a pale trail that reads as an absence being uncovered.
constexpr float kInk[4] = {240 / 255.0f, 238 / 255.0f, 232 / 255.0f, 1.0f};
constexpr float kHalo[4] = {0.0f, 0.0f, 0.0f, 0.85f};
constexpr float kFrame[4] = {0.0f, 0.0f, 0.0f, 0.78f};
constexpr float kCast = 0.38f;  // MU2's 0.55, lightened: a lift, not a halo
constexpr float kTrack[4] = {66 / 255.0f, 63 / 255.0f, 60 / 255.0f, 1.0f};
constexpr float kFill[4] = {150 / 255.0f, 34 / 255.0f, 32 / 255.0f, 1.0f};
constexpr float kChip[4] = {212 / 255.0f, 158 / 255.0f, 146 / 255.0f, 165 / 255.0f};

// ---- geometry, in interface units (panel::scale() pixels each) --------------------------
constexpr float kBarWide = 78.0f;
constexpr float kBarTall = 7.5f;  // set by the figures printed inside it
constexpr float kEdge = 1.0f;
constexpr float kRadius = 2.5f;   // a third of the height: not a pill, not square
// MU2's 1.5 and 2.5, tightened: the user wanted the shadow elegant and minimal (2026-09-22),
// and at 2.5 units it read as a dark smudge round the bar rather than as the bar lying over
// the ground.
constexpr float kCastDrop = 1.0f;
constexpr float kCastSpread = 1.75f;
constexpr float kNameTall = 8.0f;
constexpr float kReadingTall = 5.0f;
constexpr float kGap = 4.0f;      // clears the shadow before it starts being a gap

// ---- time, in seconds --------------------------------------------------------------------
constexpr float kChipHold = 0.28f;
constexpr float kChipDrain = 1.1f;  // of the whole bar, a second
constexpr float kLinger = 0.35f;
constexpr float kFadeOut = 0.1f;
constexpr float kFadeIn = 0.05f;
// A kill is taken down on a longer, eased fade: there is no answer left to read, and a bar
// that vanishes on the frame the body drops reads as a fault. The hold lets it be seen
// reaching nought under the blow that emptied it.
constexpr float kSlain = 0.42f;
constexpr float kSlainFade = 0.32f;

uint32_t colour(const float c[4], float alpha) {
    return gfx::rgba(c[0], c[1], c[2], c[3] * alpha);
}

// A type size in whole pixels, never small enough to become a smear. Vitals.Points.
float points(float pixels) { return std::max(8.0f, std::round(pixels)); }

// A rounded box, one pixel row a quad. Each row is cut to the outline at its middle, scaled
// by how much of its pixel it covers top to bottom, and its two ends are antialiased by how
// much of their pixel they cover side to side -- which is what StyleBoxFlat's AntiAliasing
// did, and without it the corners are a staircase and the point of having them is lost.
//
// `cut` ends the box at an x from the left, square: that is how the health and the trail are
// filled. Because they are cut from the TRACK's own outline, a full bar's right end is the
// track's rounded one, and the red nub Vitals.cs eased away over the last tenth never exists.
// `shade` gives each row its colour from how far down the box it is, 0 to 1.
template <typename Shade>
void rows(gfx::Canvas& canvas, const gfx::Box& box, float radius, float cut, Shade shade) {
    if (box.w <= 0.0f || box.h <= 0.0f || cut <= box.x) return;
    const float r = std::min(radius, std::min(box.w, box.h) * 0.5f);
    const int first = int(std::floor(box.y)), last = int(std::ceil(box.bottom()));
    for (int py = first; py < last; ++py) {
        const float top = std::max(float(py), box.y);
        const float bottom = std::min(float(py + 1), box.bottom());
        const float cover = bottom - top;
        if (cover <= 0.0f) continue;
        const float mid = (top + bottom) * 0.5f;
        float dy = 0.0f;
        if (mid < box.y + r) dy = box.y + r - mid;
        if (mid > box.bottom() - r) dy = mid - (box.bottom() - r);
        const float in = r - std::sqrt(std::max(0.0f, r * r - dy * dy));
        const float left = box.x + in;
        const float right = std::min(box.right() - in, cut);
        if (right <= left) continue;
        const uint32_t abgr = shade((mid - box.y) / box.h);
        const float alpha = float(abgr >> 24) / 255.0f;
        const auto faded = [&](float part) {
            return (abgr & 0x00FFFFFFu) | (gfx::rgbaByte(alpha * cover * part) << 24);
        };
        const float solidLeft = std::ceil(left), solidRight = std::floor(right);
        if (solidLeft > solidRight) {
            canvas.rect({std::floor(left), float(py), 1.0f, 1.0f}, faded(right - left));
            continue;
        }
        if (solidRight > solidLeft) {
            canvas.rect({solidLeft, float(py), solidRight - solidLeft, 1.0f}, faded(1.0f));
        }
        if (solidLeft > left) {
            canvas.rect({solidLeft - 1.0f, float(py), 1.0f, 1.0f}, faded(solidLeft - left));
        }
        if (right > solidRight) {
            canvas.rect({solidRight, float(py), 1.0f, 1.0f}, faded(right - solidRight));
        }
    }
}

// StyleBoxFlat's shadow, which is what the bar casts in Vitals.cs: the box's own rounded shape
// at full `alpha`, falling off to nothing `size` pixels out, round a corner
// the same way as along a side -- Godot draws it as a ring from the box to the box grown by
// the shadow's size, full colour on the inside edge and transparent on the outside.
//
// Laid down a row at a time like everything else: the straight middle of a row is one alpha
// and one quad, and only the rounded ends are walked a pixel at a time, each pixel taking its
// alpha from its centre's distance to the box.
void cast(gfx::Canvas& canvas, const gfx::Box& box, float radius, float size, float alpha) {
    if (box.w <= 0.0f || box.h <= 0.0f || size <= 0.0f) return;
    const float r = std::min(radius, std::min(box.w, box.h) * 0.5f);
    const float cx = box.midX(), cy = box.midY();
    const float hx = box.w * 0.5f - r, hy = box.h * 0.5f - r;
    // The signed distance from a rounded box, negative inside it.
    const auto away = [&](float x, float y) {
        const float qx = std::fabs(x - cx) - hx, qy = std::fabs(y - cy) - hy;
        const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
        return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;
    };
    // Eased rather than Godot's straight line: a linear ramp ends in a visible edge at the
    // outside, and squaring it lets the tail go to nothing without one.
    const auto level = [&](float d) {
        const float t = std::clamp(1.0f - d / size, 0.0f, 1.0f);
        return alpha * t * t;
    };
    const int first = int(std::floor(box.y - size)), last = int(std::ceil(box.bottom() + size));
    const int left = int(std::floor(box.x - size)), right = int(std::ceil(box.right() + size));
    // The straight middle, where only the row decides the distance.
    const int midL = int(std::ceil(cx - hx)), midR = int(std::floor(cx + hx));
    for (int py = first; py < last; ++py) {
        const float y = float(py) + 0.5f;
        if (midR > midL) {
            const float a = level(away(cx, y));
            if (a > 0.0f) {
                canvas.rect({float(midL), float(py), float(midR - midL), 1.0f},
                            gfx::rgba(0, 0, 0, a));
            }
        }
        for (int px = left; px < std::min(midL, right); ++px) {
            const float a = level(away(float(px) + 0.5f, y));
            if (a > 0.0f) canvas.rect({float(px), float(py), 1.0f, 1.0f}, gfx::rgba(0, 0, 0, a));
        }
        for (int px = std::max(midR, left); px < right; ++px) {
            const float a = level(away(float(px) + 0.5f, y));
            if (a > 0.0f) canvas.rect({float(px), float(py), 1.0f, 1.0f}, gfx::rgba(0, 0, 0, a));
        }
    }
}

void flat(gfx::Canvas& canvas, const gfx::Box& box, float radius, float cut, uint32_t abgr) {
    rows(canvas, box, radius, cut, [abgr](float) { return abgr; });
}

// A string in its outline, which is how anything here is legible over the world: eight
// copies in the halo a pixel or two out, and the ink over them.
void type(gfx::Canvas& canvas, float x, float baseline, float size, float alpha,
          const std::string& text) {
    const float round = std::max(1.0f, std::round(size / 12.0f));
    const uint32_t halo = colour(kHalo, alpha);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            canvas.text(x + float(dx) * round, baseline + float(dy) * round, size, halo, text);
        }
    }
    canvas.text(x, baseline, size, colour(kInk, alpha), text);
}

}  // namespace

void Vitals::dismiss() {
    if (on_ == 0) return;
    on_ = 0;
    shown_ = 0.0f;
}

void Vitals::trail(float now, float seconds) {
    if (now < was_) holding_ = kChipHold;
    was_ = now;
    // Healed, or risen: nothing to uncover, so the trail keeps up rather than being overtaken
    // and drawn on the wrong side of the fill.
    if (now >= lag_) {
        lag_ = now;
        holding_ = 0.0f;
        return;
    }
    if (holding_ > 0.0f) {
        holding_ -= seconds;
        return;
    }
    lag_ = std::max(now, lag_ - kChipDrain * seconds);
}

void Vitals::update(float seconds, const Play& play, uint32_t pointed, bool paneled,
                    const float* viewProj, int width, int height) {
    const sim::Realm& realm = play.realm();
    const auto fraction = [&](uint32_t id) {
        const sim::Body* body = realm.find(id);
        if (!body || body->maxHealth <= 0) return 1.0f;
        return std::clamp(float(play.shownHealth(id)) / float(body->maxHealth), 0.0f, 1.0f);
    };

    // Show: a live monster under the pointer and no window over it. A different monster
    // starts its bar full rather than inheriting the last one's trail.
    const sim::Body* hovered = pointed != 0 ? realm.find(pointed) : nullptr;
    if (hovered && hovered->alive() && !hovered->player && !paneled) {
        if (on_ != pointed) {
            lag_ = was_ = fraction(pointed);
            holding_ = 0.0f;
            alive_ = true;
        }
        on_ = pointed;
        left_ = kLinger;
    }

    // Process.
    const sim::Body* beast = on_ != 0 ? realm.find(on_) : nullptr;
    if (on_ != 0 && !beast) dismiss();
    if (on_ != 0) {
        const bool alive = beast->alive();
        // The frame it dies on: the readout stops being read and starts being taken away, and
        // gets the window that job gets.
        if (alive_ && !alive) left_ = kSlain;
        alive_ = alive;
        left_ -= seconds;
        if (left_ <= 0.0f) {
            dismiss();
        } else {
            const float window = alive ? kLinger : kSlain;
            const float going = alive ? kFadeOut : kSlainFade;
            const float part = std::min(1.0f, left_ / going);
            // Straight on a dismissal, which nobody can tell from a curve over a tenth of a
            // second; smoothstep on a death, whose ends are visible over a third.
            shown_ = left_ > window - kFadeIn ? std::min(1.0f, shown_ + seconds / kFadeIn)
                     : alive                  ? part
                                              : part * part * (3.0f - 2.0f * part);
            trail(fraction(on_), seconds);
        }
    }

    Readout now;
    now.unit = panel::scale();
    float x = 0.0f, y = 0.0f;
    if (on_ != 0 && play.crownOf(on_, viewProj, width, height, &x, &y)) {
        now.on = on_;
        // Whole pixels: the rows are laid on the pixel grid, and a bar that slid by fractions
        // would shimmer its antialiased ends and rebuild on every sub-pixel of a walk.
        now.x = std::round(x);
        now.y = std::round(y);
        now.shown = shown_;
        now.lag = lag_;
        now.health = was_;
        now.reading = play.shownHealth(on_);
        now.maximum = beast->maxHealth;
    }
    if (now == drawn_ && rebuilds_ > 0) return;
    drawn_ = now;
    ++rebuilds_;
    rebuild(play, now);
}

void Vitals::rebuild(const Play& play, const Readout& r) {
    canvas_.clear();
    if (r.on == 0 || r.shown <= 0.0f) return;
    const sim::Body* beast = play.realm().find(r.on);
    if (!beast || beast->kind < 0) return;
    const gfx::Face& face = canvas_.face();
    const float unit = r.unit, alpha = r.shown;

    // Sized against the interface's scale, not the distance: a reading, not a thing in the
    // world, so the same size on every body. Only its place comes from the projection.
    const float barW = std::round(kBarWide * unit), barH = std::round(kBarTall * unit);
    const gfx::Box bar{r.x - std::round(barW * 0.5f), r.y - barH, barW, barH};
    const float round = kRadius * unit;
    const float edge = std::max(1.0f, std::round(kEdge * unit));

    // The name, centred over the bar. Just the name: the level went, twice (Vitals.Title).
    const std::string& name = play.realm().tables()->kinds[size_t(beast->kind)].label;
    const float nameSize = points(kNameTall * unit);
    const float nameTop = bar.y - (kGap + kNameTall) * unit;
    type(canvas_, std::round(r.x - face.measure(nameSize, name) * 0.5f),
         std::round(nameTop + face.ascent(nameSize)), nameSize, alpha, name);

    // The shadow, as the track's StyleBoxFlat cast it: the bar's shape dropped by ShadowOffset,
    // fading out over ShadowSize. Both whole pixels, as Vitals.Bar rounded them, and the
    // corner radius too -- SetCornerRadiusAll took an int.
    {
        gfx::Box shade = bar;
        shade.y += kCastDrop * unit;
        cast(canvas_, shade, std::round(round), std::round(kCastSpread * unit), kCast * alpha);
    }

    // The rim as the outer shape, and everything else inside it -- the same picture as
    // Godot's border drawn over the lot, in one fewer ring.
    flat(canvas_, bar, round, 1e9f, colour(kFrame, alpha));
    const gfx::Box inside = bar.grown(-edge);
    const float in = std::max(0.0f, round - edge);
    flat(canvas_, inside, in, 1e9f, colour(kTrack, alpha));
    // The trail under the fill, so the two cannot disagree about where the health ends.
    if (r.lag > 0.0f) flat(canvas_, inside, in, inside.x + inside.w * r.lag, colour(kChip, alpha));
    if (r.health > 0.0f) {
        flat(canvas_, inside, in, inside.x + inside.w * r.health, colour(kFill, alpha));
    }
    // The gloss over the whole bar, not over the red, so the lighting belongs to the object
    // and does not slide as the monster dies. Vitals.Lighting's ramp: a squared highlight that
    // falls off fast under the crown, a linear shade toward the foot, both shallow -- a strong
    // ramp on a bar this small is a white stripe and a black stripe, not a cylinder.
    rows(canvas_, inside, in, 1e9f, [alpha](float down) {
        const float lit = std::pow(std::max(0.0f, 1.0f - down / 0.5f), 2.0f) * 0.18f;
        const float shade = std::max(0.0f, (down - 0.55f) / 0.45f) * 0.2f;
        return lit > shade ? gfx::rgba(1, 1, 1, lit * alpha) : gfx::rgba(0, 0, 0, shade * alpha);
    });

    // The two figures, small, inside the bar, centred on the ink rather than on the line.
    // MU could not print the maximum -- HealthStatus reached its client as a byte's fraction.
    const std::string reading = std::to_string(r.reading) + " / " + std::to_string(r.maximum);
    const float readSize = points(kReadingTall * unit);
    const float baseline =
        bar.midY() + (face.ascent(readSize) - face.descent(readSize)) * 0.5f;
    type(canvas_, std::round(bar.midX() - face.measure(readSize, reading) * 0.5f),
         std::round(baseline), readSize, alpha, reading);
}

}  // namespace mu::game
