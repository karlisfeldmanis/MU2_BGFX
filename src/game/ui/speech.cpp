#include "game/ui/speech.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "game/play.h"
#include "game/ui/controls.h"
#include "game/ui/describe.h"
#include "game/ui/tip.h"
#include "sim/item_link.h"

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
float popped(float age, float life) {
    if (age < kPopIn) {
        const float t = age / kPopIn - 1.0f;
        constexpr float kBack = 1.70158f;
        return 1.0f + t * t * ((kBack + 1.0f) * t + kBack);
    }
    const float left = life - age;
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

// ---- a player's line: the chat's dark glass (the user picked "A · Dark glass", 2026-10-11) -----
// claude.ai/artifact/9PMRqvHrnXfT9DMv5BEKpg. No outline and no comic tail: a soft shadow, a short
// tail straight down, the chat's bone words, item links in their quality's colour, a Party line
// tagged in the chat's blue. Three lines at most; his last line stands over it, dimmed. It fades
// and rises in rather than popping: glass at part alpha shows nothing it should not.
constexpr float kGlassWide = 212.0f;   // the words' most, so the glass is 236 across at most
constexpr float kGlassSize = 15.0f, kGlassLeading = 19.0f;
constexpr float kGlassPadX = 12.0f, kGlassPadTop = 7.0f, kGlassPadFoot = 8.0f;
constexpr float kGlassRound = 7.0f;
constexpr float kGlassTail = 11.0f, kGlassTailHalf = 7.0f;
constexpr float kGlassLift = 4.0f;     // over the paper's kLift: clear of his name
constexpr float kGlassGap = 4.0f;      // between his last line and this one
constexpr int kGlassLines = 3;
constexpr float kGlassIn = 0.15f, kGlassOut = 0.3f, kGlassRise = 6.0f;
constexpr float kGlassBody = 0.82f;
constexpr uint32_t kGlass = gfx::rgba(0.051f, 0.039f, 0.031f);
constexpr uint32_t kBone = gfx::rgba(0.925f, 0.886f, 0.812f);
constexpr uint32_t kPartyBlue = gfx::rgba(0.498f, 0.714f, 0.949f);

uint32_t alphaOf(uint32_t abgr, float a) {
    const float was = float(abgr >> 24) / 255.0f;
    return (abgr & 0x00FFFFFFu) | (gfx::rgbaByte(std::clamp(was * a, 0.0f, 1.0f)) << 24);
}

// The line as it reads -- links "[Name]", a Party tag before it -- and each byte's ink.
struct Inked {
    std::string text;
    std::vector<uint32_t> ink;
};
Inked inked(const Play::Said& said, const content::Tables* tables) {
    Inked out;
    const auto add = [&](const std::string& part, uint32_t ink) {
        out.text += part;
        out.ink.insert(out.ink.end(), part.size(), ink);
    };
    if (said.party) add("Party ", kPartyBlue);
    size_t at = 0;
    for (const sim::ItemLink& link : sim::findLinks(said.line)) {
        add(said.line.substr(at, link.from - at), kBone);
        uint32_t ink = kBone;
        const int32_t row = tables ? sim::linkRow(*tables, link.group, link.number) : -1;
        if (row >= 0) {
            sim::Held held = link.held;
            held.item = row;
            ink = tip::colourOf(qualityOf(tables->items[size_t(row)], held));
        }
        add("[" + link.label + "]", ink);
        at = link.to;
    }
    add(said.line.substr(at), kBone);
    return out;
}

// The rows it breaks into at `wide`, as (first byte, length), by words; a word wider than a row
// is a row of its own. Past kGlassLines the last is cut and ends in "...".
std::vector<std::pair<size_t, size_t>> rows(const std::string& text, float size, float wide, bool* cut) {
    std::vector<std::pair<size_t, size_t>> out;
    size_t start = 0;
    *cut = false;
    while (start < text.size()) {
        while (start < text.size() && text[start] == ' ') ++start;
        if (start >= text.size()) break;
        size_t end = start, fit = start;
        while (end < text.size()) {
            size_t next = text.find(' ', end + 1);
            if (next == std::string::npos) next = text.size();
            if (controls::labelWidth(size, text.substr(start, next - start)) > wide && fit > start) break;
            fit = next;
            end = next;
        }
        out.push_back({start, fit - start});
        start = fit;
        if (int(out.size()) == kGlassLines && start < text.size()) {
            *cut = text.find_first_not_of(' ', start) != std::string::npos;
            break;
        }
    }
    return out;
}

// One glass line drawn with its foot at `foot` (the tail's point, or the box's foot without one),
// faded by `dim`. The top of its box, for the line over it.
float glassLine(gfx::Canvas& canvas, const Play::Said& said, const content::Tables* tables, float x, float foot,
                bool tail, float dim, float u) {
    const float fadeIn = std::min(1.0f, said.age / kGlassIn);
    const float fadeOut = std::clamp((said.life - said.age) / kGlassOut, 0.0f, 1.0f);
    const float a = fadeIn * fadeOut * dim;
    if (a <= 0.01f) return foot;
    foot = std::round(foot + (1.0f - fadeIn) * kGlassRise * u);
    const float size = std::round(kGlassSize * u);
    const Inked line = inked(said, tables);
    bool cut = false;
    std::vector<std::pair<size_t, size_t>> parts = rows(line.text, size, kGlassWide * u, &cut);
    if (parts.empty()) return foot;
    std::vector<std::string> shown;
    for (const auto& [from, length] : parts) shown.push_back(line.text.substr(from, length));
    if (cut) {
        std::string& last = shown.back();
        while (!last.empty() && controls::labelWidth(size, last + "...") > kGlassWide * u) last.pop_back();
        while (!last.empty() && (uint8_t(last.back()) & 0xC0) == 0x80) last.pop_back();
        if (!last.empty()) last.pop_back();
        // On a whole word where the row has one to end on.
        if (const size_t space = last.rfind(' '); space != std::string::npos && space > 0) last.resize(space);
        parts.back().second = last.size();
        last += "...";
    }
    float wide = 0.0f;
    for (const std::string& one : shown) wide = std::max(wide, controls::labelWidth(size, one));
    const float boxW = std::round(wide + kGlassPadX * u * 2.0f);
    const float boxH = std::round(kGlassLeading * u * float(shown.size() - 1) + size + (kGlassPadTop + kGlassPadFoot) * u);
    const float tailTall = tail ? std::round(kGlassTail * u) : 0.0f;
    Bubble glass;
    glass.box = {std::round(x - boxW * 0.5f), foot - tailTall - boxH, boxW, boxH};
    glass.radius = std::min(kGlassRound * u, boxH * 0.5f);
    glass.rootL = tail ? x - kGlassTailHalf * u : x;
    glass.rootR = tail ? x + kGlassTailHalf * u : x;
    glass.point = x;
    glass.tip = tail ? foot : glass.box.bottom();
    // The shadow, a little down and soft: two wider passes under the glass.
    Bubble shadow = glass;
    shadow.box.y += 2.0f * u;
    shadow.tip += 2.0f * u;
    shadow.draw(canvas, 3.0f * u, gfx::rgba(0.0f, 0.0f, 0.0f, 0.16f * a));
    shadow.draw(canvas, 1.5f * u, gfx::rgba(0.0f, 0.0f, 0.0f, 0.22f * a));
    glass.draw(canvas, 0.0f, alphaOf(kGlass, kGlassBody * a));

    const gfx::Face* face = controls::labelFace();
    const float ascent = face ? face->ascent(size) : size * 0.8f;
    float baseline = std::round(glass.box.y + kGlassPadTop * u + ascent);
    for (size_t r = 0; r < shown.size(); ++r) {
        const std::string& words = shown[r];
        float pen = std::round(x - controls::labelWidth(size, words) * 0.5f);
        // Each byte's ink, the "..." of a cut row in bone; then runs of one ink, each over its drop.
        std::vector<uint32_t> inks(line.ink.begin() + long(parts[r].first),
                                   line.ink.begin() + long(parts[r].first + parts[r].second));
        inks.resize(words.size(), kBone);
        size_t at = 0;
        while (at < words.size()) {
            size_t end = at + 1;
            while (end < words.size() && inks[end] == inks[at]) ++end;
            const std::string run = words.substr(at, end - at);
            if (face) {
                canvas.lettered(*face, controls::labelTexture(), pen, baseline + std::max(1.0f, u), size, 0.0f,
                                gfx::rgba(0.0f, 0.0f, 0.0f, 0.8f * a), run);
                pen += canvas.lettered(*face, controls::labelTexture(), pen, baseline, size, 0.0f,
                                       alphaOf(inks[at], a), run);
            } else {
                canvas.text(pen, baseline, size, alphaOf(inks[at], a), run);
                pen += controls::labelWidth(size, run);
            }
            at = end;
        }
        baseline += std::round(kGlassLeading * u);
    }
    return glass.box.y;
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
    const content::Tables* tables = play.isOpen() ? play.realm().tables() : nullptr;
    const std::vector<Play::Said>& all = play.said();
    for (size_t i = 0; i < all.size(); ++i) {
        const Play::Said& said = all[i];
        if (!said.chat) continue;
        // Each speaker once, from his newest line, with the one before it over it.
        bool newest = true;
        const Play::Said* before = nullptr;
        for (size_t j = 0; j < all.size(); ++j) {
            if (j == i || !all[j].chat || all[j].who != said.who) continue;
            if (j > i) newest = false;
            else before = &all[j];
        }
        if (!newest) continue;
        float x = 0.0f, y = 0.0f;
        if (!play.crownOf(said.who, viewProj, width, height, &x, &y)) continue;
        if (x < 0.0f || x > float(width) || y < 0.0f || y > float(height)) continue;
        x = std::round(x);
        const float top = glassLine(canvas_, said, tables, x, std::round(y - (kLift + kGlassLift) * u), true, 1.0f, u);
        if (before) glassLine(canvas_, *before, tables, x, top - kGlassGap * u, false, 0.5f, u);
    }
    for (const Play::Said& said : all) {
        if (said.chat) continue;
        float x = 0.0f, y = 0.0f;
        const bool placed = said.folk >= 0
                                ? play.folkCrownOf(said.folk, viewProj, width, height, &x, &y)
                                : play.crownOf(said.who, viewProj, width, height, &x, &y);
        // Only over a speaker on the screen: one off it -- Lumen at her bar while the hero is at
        // the gate -- has no bubble at all, rather than one pulled in from the edge far from him.
        if (!placed || x < 0.0f || x > float(width) || y < 0.0f || y > float(height)) continue;
        // On whole pixels, the box and the words together. The crown follows the speaker's head
        // through his idle, a fraction of a pixel a frame; the box went with it smoothly while the
        // words, set on whole pixels, jumped a pixel at a time, and swam inside it (the user,
        // 2026-10-06: 'speech buble text is moving when buble is active').
        x = std::round(x);
        y = std::round(y);
        const float scale = popped(said.age, said.life);
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
        // Only at their whole size, once the bubble has settled: drawn through the pop, past its
        // overshoot and back, they grew and shrank in place.
        if (scale != 1.0f) continue;
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
