#include "game/ui/speech.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "game/play.h"
#include "game/ui/controls.h"
#include "game/ui/tip.h"

namespace mu::game {

namespace {

// In the card's own units: one pixel of a 1080-line screen (tip::unit).
constexpr float kWide = 170.0f;     // the most a line runs before it wraps
constexpr float kSize = 15.0f;      // the words, in the label face
constexpr float kLeading = 18.0f;   // one line to the next
constexpr float kPadX = 12.0f, kPadY = 7.0f;
constexpr float kRound = 14.0f;     // the corners, or half the height when that is less
constexpr float kEdge = 1.5f;       // the outline
// The tail: its root on the bubble's foot a little right of the speaker, its point a little
// left of him, which is the lean a hand-drawn tail has.
constexpr float kTailFrom = 2.0f, kTailTo = 17.0f;  // the root, right of the speaker
constexpr float kTailPoint = -4.0f;                  // the point, from the speaker
constexpr float kTailTall = 15.0f;                   // below the foot
// Where the tail's point stands over the crown: clear of the name a guard under the pointer
// shows there (the vitals' gap and name, 24 of these units), so it points at him past it.
constexpr float kLift = 28.0f;

// Seconds. A bubble pops rather than fades: it is opaque, and an outline under a fill at part
// alpha shows through it.
constexpr float kPopIn = 0.2f;
constexpr float kPopOut = 0.14f;

// The comic's own colours: paper, ink round it, ink in it.
constexpr uint32_t kPaper = gfx::rgba(0.965f, 0.945f, 0.898f);
constexpr uint32_t kOutline = gfx::rgba(0.086f, 0.067f, 0.055f);
constexpr uint32_t kWords = gfx::rgba(0.110f, 0.086f, 0.071f);

// How big it is at `age`: up past whole and settling as it arrives (an ease-out-back), down to
// nothing as it goes.
float popped(float age) {
    if (age < kPopIn) {
        const float t = age / kPopIn - 1.0f;
        constexpr float kBack = 1.70158f;
        return 1.0f + t * t * ((kBack + 1.0f) * t + kBack);
    }
    const float left = Play::kSaidSeconds - age;
    return left < kPopOut ? std::max(0.0f, left / kPopOut) : 1.0f;
}

// The bubble as one shape: a rounded box and the tail under it, grown by `grow` on every side.
// Laid down a pixel row at a time as the vitals' bar is, the two ends of each row covered by how
// much of their pixel they take, so the curves and the tail's edges are smooth: the canvas does
// not antialias a polygon, and a tail drawn as one read as stairs.
struct Bubble {
    gfx::Box box;
    float radius = 0.0f;
    float rootL = 0.0f, rootR = 0.0f, point = 0.0f, tip = 0.0f;  // x, x, x and the point's y

    void draw(gfx::Canvas& canvas, float grow, uint32_t abgr) const {
        const gfx::Box b = box.grown(grow);
        const float r = std::min(radius + grow, std::min(b.w, b.h) * 0.5f);
        // The tail's root sits inside the foot, so the two shapes meet with no seam.
        const float rootY = box.bottom() - radius * 0.5f;
        const float tipY = tip + grow * 2.0f;
        const float left0 = rootL - grow * 1.3f, right0 = rootR + grow * 1.3f;
        const int first = int(std::floor(b.y)), last = int(std::ceil(tipY));
        for (int py = first; py < last; ++py) {
            const float top = std::max(float(py), b.y);
            const float bottom = std::min(float(py + 1), tipY);
            const float cover = bottom - top;
            if (cover <= 0.0f) continue;
            const float mid = (top + bottom) * 0.5f;
            float left = 1e9f, right = -1e9f;
            if (mid >= b.y && mid <= b.bottom()) {
                float dy = 0.0f;
                if (mid < b.y + r) dy = b.y + r - mid;
                if (mid > b.bottom() - r) dy = mid - (b.bottom() - r);
                const float in = r - std::sqrt(std::max(0.0f, r * r - dy * dy));
                left = b.x + in;
                right = b.right() - in;
            }
            if (mid >= rootY && mid <= tipY) {
                const float t = (mid - rootY) / std::max(1.0f, tipY - rootY);
                const float tailL = left0 + (point - left0) * t;
                const float tailR = right0 + (point - right0) * t;
                // Joined with the box's own span where they touch, and on its own below it.
                if (right < left || tailR < left || tailL > right) {
                    if (right < left) {
                        left = tailL;
                        right = tailR;
                    }
                } else {
                    left = std::min(left, tailL);
                    right = std::max(right, tailR);
                }
            }
            if (right <= left) continue;
            const float alpha = float(abgr >> 24) / 255.0f;
            const auto part = [&](float share) {
                return (abgr & 0x00FFFFFFu) | (gfx::rgbaByte(alpha * cover * share) << 24);
            };
            const float solidL = std::ceil(left), solidR = std::floor(right);
            if (solidL > solidR) {
                canvas.rect({std::floor(left), float(py), 1.0f, 1.0f}, part(right - left));
                continue;
            }
            if (solidR > solidL) canvas.rect({solidL, float(py), solidR - solidL, 1.0f}, part(1.0f));
            if (solidL > left) canvas.rect({solidL - 1.0f, float(py), 1.0f, 1.0f}, part(solidL - left));
            if (right > solidR) canvas.rect({solidR, float(py), 1.0f, 1.0f}, part(right - solidR));
        }
    }
};

// The words broken to `wide`, a word at least a line; a '\n' in the words is a break of its own.
std::vector<std::string> wrapped(const std::string& text, float size, float wide) {
    std::vector<std::string> lines;
    std::string line, word;
    const auto put = [&](bool hard) {
        if (!word.empty()) {
            const std::string tried = line.empty() ? word : line + " " + word;
            if (!line.empty() && controls::labelWidth(size, tried) > wide) {
                lines.push_back(line);
                line = word;
            } else {
                line = tried;
            }
            word.clear();
        }
        if (hard && !line.empty()) {
            lines.push_back(line);
            line.clear();
        }
    };
    for (char c : text) {
        if (c == ' ') put(false);
        else if (c == '\n') put(true);
        else word += c;
    }
    put(true);
    return lines;
}

}  // namespace

void Speech::update(const Play& play, const float* viewProj, int width, int height) {
    if (play.said().empty()) {
        if (showing_) dismiss();
        return;
    }
    canvas_.clear();
    showing_ = true;
    const float u = tip::unit();
    const float size = std::round(kSize * u);
    for (const Play::Said& said : play.said()) {
        float x = 0.0f, y = 0.0f;
        const bool placed = said.folk >= 0
                                ? play.folkCrownOf(said.folk, viewProj, width, height, &x, &y)
                                : play.crownOf(said.who, viewProj, width, height, &x, &y);
        // Only over a speaker on the screen: one off it -- Lumen at her bar while the hero is at
        // the gate -- has no bubble at all, rather than one pulled in from the edge far from him.
        if (!placed || x < 0.0f || x > float(width) || y < 0.0f || y > float(height)) continue;
        const float scale = popped(said.age);
        if (scale <= 0.02f) continue;
        // Scaled about the tail's point, which stays on the speaker: the bubble grows out of him.
        const float k = u * scale;
        // Broken at its whole size, so a line does not re-wrap as the bubble grows.
        const std::vector<std::string> lines = wrapped(said.line, size, kWide * u);
        if (lines.empty()) continue;
        const float drawn = size * scale;
        float wide = 0.0f;
        for (const std::string& line : lines) wide = std::max(wide, controls::labelWidth(size, line));
        wide *= scale;
        const float tall = kLeading * k * float(lines.size() - 1) + drawn;
        const float boxW = wide + kPadX * k * 2.0f;
        const float boxH = tall + kPadY * k * 2.0f;
        // Never off the top of the screen: a speaker whose head is on it but near its top edge
        // -- Lumen behind her bar, with the hero at the door -- has his bubble brought down to it.
        const float room = std::round(8.0f * u);
        const float tip = std::max(std::round(y - kLift * u), room + boxH + kTailTall * k);
        const float foot = tip - kTailTall * k;
        Bubble bubble;
        bubble.box = {x - boxW * 0.5f, foot - boxH, boxW, boxH};
        bubble.radius = std::min(kRound * k, boxH * 0.5f);
        bubble.rootL = x + kTailFrom * k;
        bubble.rootR = x + kTailTo * k;
        bubble.point = x + kTailPoint * k;
        bubble.tip = tip;
        bubble.draw(canvas_, kEdge * k, kOutline);
        bubble.draw(canvas_, 0.0f, kPaper);

        // The words, centred, in ink and with no shadow: they are on paper, not on the world.
        // Not until it is most of the way up -- type grown from a point is a smear.
        if (scale < 0.7f) continue;
        const gfx::Face* face = controls::labelFace();
        const float ascent = face ? face->ascent(drawn) : drawn * 0.8f;
        float baseline = bubble.box.y + kPadY * k + ascent;
        for (const std::string& words : lines) {
            const float left = std::round(x - controls::labelWidth(drawn, words) * 0.5f);
            if (face) {
                canvas_.lettered(*face, controls::labelTexture(), left, std::round(baseline), drawn,
                                 0.0f, kWords, words);
            } else {
                canvas_.text(left, std::round(baseline), drawn, kWords, words);
            }
            baseline += kLeading * k;
        }
    }
}

}  // namespace mu::game
