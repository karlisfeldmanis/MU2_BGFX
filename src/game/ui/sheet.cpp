#include "game/ui/sheet.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace mu::game::sheet {
namespace {

using gfx::Box;

// **Skin B, "obsidian, deeper wells"**, chosen by the user on 2026-09-23 from a page of four
// drawn on MU's own window. Flat and near-black -- *"very clean, flat, modern, Diablo 4 style"*
// -- with the two things B does that A did not: the cells are cut DEEPER (a stronger seat of
// shadow inside the top edge and a brighter hairline over it), and the head and its mark are
// gold rather than bone. Every number here is that page's, converted.
constexpr uint32_t kBodyTop = gfx::rgba(0.027f, 0.027f, 0.035f, 0.95f);
constexpr uint32_t kBodyFoot = gfx::rgba(0.016f, 0.016f, 0.020f, 0.92f);

// The edge, and the head's band of light over it.
constexpr uint32_t kEdgeTop = gfx::rgba(0.957f, 0.886f, 0.690f, 0.60f);
constexpr uint32_t kEdgeFoot = gfx::rgba(0.588f, 0.510f, 0.353f, 0.07f);
constexpr uint32_t kBandLit = gfx::rgba(1.0f, 0.941f, 0.804f, 0.075f);

// One hairline, the colour of old bronze, at three strengths: the window's edge, a rule inside
// it, and a cell's own border.
constexpr uint32_t kRule = gfx::rgba(0.643f, 0.573f, 0.404f, 0.38f);
constexpr uint32_t kRuleOut = gfx::rgba(0.643f, 0.573f, 0.404f, 0.0f);

constexpr uint32_t kCellBack = gfx::rgba(1.0f, 1.0f, 1.0f, 0.020f);
constexpr uint32_t kCellEdge = gfx::rgba(0.643f, 0.573f, 0.404f, 0.26f);
constexpr uint32_t kCellOver = gfx::rgba(1.0f, 1.0f, 1.0f, 0.085f);
constexpr uint32_t kCellOverEdge = gfx::rgba(0.886f, 0.816f, 0.600f, 0.55f);
constexpr uint32_t kCellHeld = gfx::rgba(0.0f, 0.0f, 0.0f, 0.30f);
constexpr uint32_t kFitsBack = gfx::rgba(0.267f, 0.545f, 0.937f, 0.24f);
constexpr uint32_t kFitsEdge = gfx::rgba(0.478f, 0.698f, 1.0f, 0.65f);
constexpr uint32_t kBlockedBack = gfx::rgba(0.886f, 0.290f, 0.220f, 0.24f);
constexpr uint32_t kBlockedEdge = gfx::rgba(1.0f, 0.451f, 0.373f, 0.65f);

constexpr uint32_t kBarBack = gfx::rgba(1.0f, 1.0f, 1.0f, 0.08f);
constexpr uint32_t kDrop = tip::ink::kDrop;

}  // namespace

namespace {

// The edge, as a gradient rather than a ring: four hairlines whose alpha runs from `top` at the
// window's head to `foot` at its bottom. Drawn INSIDE the body so the corners' own fan keeps the
// shape, and each horizontal run is held back by the radius so a square line never crosses a
// rounded corner.
void stroke(gfx::Canvas& canvas, const Box& box, float radius, float thick, uint32_t top,
            uint32_t foot) {
    const float r = std::min(radius, std::min(box.w, box.h) * 0.5f);
    const uint32_t middle = gfx::rgba(0.886f, 0.816f, 0.600f,
                                      float((top >> 24) & 0xFFu) / 255.0f * 0.55f);
    canvas.shade({box.x + r, box.y, box.w - r * 2.0f, thick}, top, top, top, top);
    canvas.shade({box.x + r, box.bottom() - thick, box.w - r * 2.0f, thick}, foot, foot, foot,
                 foot);
    canvas.shade({box.x, box.y + r, thick, box.h - r * 2.0f}, middle, middle, foot, foot);
    canvas.shade({box.right() - thick, box.y + r, thick, box.h - r * 2.0f}, middle, middle, foot,
                 foot);
}

}  // namespace

void glass(gfx::Canvas& canvas, const Box& window, float radius) {
    tip::shadowUnder(canvas, window, std::max(1.0f, radius / tip::ink::kRadius));
    tip::panel(canvas, window, radius, kBodyTop, kBodyFoot);
    stroke(canvas, window, radius, std::max(1.0f, radius * 0.20f), kEdgeTop, kEdgeFoot);
}

void band(gfx::Canvas& canvas, const Box& box, bool downward) {
    const uint32_t lit = kBandLit;
    const uint32_t out = gfx::rgba(1.0f, 0.941f, 0.804f, 0.0f);
    if (downward) {
        canvas.shade(box, lit, lit, out, out);
    } else {
        canvas.shade(box, out, out, lit, lit);
    }
}

void rule(gfx::Canvas& canvas, float x, float y, float wide, float thick) {
    const float half = wide * 0.5f;
    // Two halves, each grading from nothing at its far end to the ink at the middle: a rule that
    // stops dead at the padding reads as a line somebody drew; one that fades reads as an edge.
    canvas.shade({x, y, half, thick}, kRuleOut, kRule, kRule, kRuleOut);
    canvas.shade({x + half, y, half, thick}, kRule, kRuleOut, kRuleOut, kRule);
}

void well(gfx::Canvas& canvas, const Box& box, float thick) {
    canvas.rect(box, tip::ink::kFramed);
    canvas.outline(box, thick, tip::ink::kFrame);
}

void cell(gfx::Canvas& canvas, const Box& box, Cell state, float thick) {
    uint32_t back = kCellBack, edge = kCellEdge;
    bool quiet = true;
    switch (state) {
        case Cell::Over: back = kCellOver; edge = kCellOverEdge; quiet = false; break;
        case Cell::Held: back = kCellHeld; break;
        case Cell::Fits: back = kFitsBack; edge = kFitsEdge; quiet = false; break;
        case Cell::Blocked: back = kBlockedBack; edge = kBlockedEdge; quiet = false; break;
        case Cell::Rest: break;
    }
    // The well: a flat fill, a seat of shadow inside its top edge, a hairline of light on that
    // edge, and the border over both. The seat is what B is: deep enough to read as a recess cut
    // into the panel rather than a square drawn on it, and still nothing anyone would name.
    canvas.rect(box, back);
    if (quiet) {
        const uint32_t dark = gfx::rgba(0.0f, 0.0f, 0.0f, 0.55f);
        const uint32_t none = gfx::rgba(0.0f, 0.0f, 0.0f, 0.0f);
        const float deep = std::max(thick * 3.0f, box.h * 0.22f);
        canvas.shade({box.x, box.y, box.w, deep}, dark, dark, none, none);
        // And the same the other way at the foot, a third as strong: the light that fell into
        // the well has to come out of it somewhere.
        const uint32_t lift = gfx::rgba(1.0f, 0.976f, 0.910f, 0.055f);
        canvas.shade({box.x, box.bottom() - thick * 2.0f, box.w, thick * 2.0f},
                     gfx::rgba(1.0f, 0.976f, 0.910f, 0.0f), gfx::rgba(1.0f, 0.976f, 0.910f, 0.0f),
                     lift, lift);
    }
    // And the border as a gradient of its own: lit along the top, quiet at the foot.
    const uint32_t lit = edge;
    const uint32_t dim = (edge & 0x00FFFFFFu) |
                         (uint32_t(float((edge >> 24) & 0xFFu) * (quiet ? 0.45f : 0.7f)) << 24);
    canvas.shade({box.x, box.y, box.w, thick}, lit, lit, lit, lit);
    canvas.shade({box.x, box.bottom() - thick, box.w, thick}, dim, dim, dim, dim);
    canvas.shade({box.x, box.y, thick, box.h}, lit, lit, dim, dim);
    canvas.shade({box.right() - thick, box.y, thick, box.h}, lit, lit, dim, dim);
}

void close(gfx::Canvas& canvas, const Box& box, bool over, bool pressed) {
    const uint32_t ink = pressed  ? gfx::rgba(1.0f, 1.0f, 1.0f, 0.95f)
                         : over   ? gfx::rgba(0.886f, 0.816f, 0.600f, 0.95f)
                                  : gfx::rgba(0.588f, 0.600f, 0.557f, 0.55f);
    if (over || pressed) canvas.rect(box, gfx::rgba(1.0f, 1.0f, 1.0f, pressed ? 0.10f : 0.06f));
    // Two bars on the diagonal, drawn as quads because the canvas has no line.
    // Small and quiet: a cross that shouts is the loudest thing in a dark window.
    const float arm = std::min(box.w, box.h) * 0.19f;
    const float t = std::max(1.0f, arm * 0.19f);
    const float cx = box.midX(), cy = box.midY();
    const float one[8] = {cx - arm - t, cy - arm + t, cx - arm + t, cy - arm - t,
                          cx + arm + t, cy + arm - t, cx + arm - t, cy + arm + t};
    const float two[8] = {cx + arm - t, cy - arm - t, cx + arm + t, cy - arm + t,
                          cx - arm + t, cy + arm + t, cx - arm - t, cy + arm - t};
    canvas.polygon(nullptr, one, nullptr, 4, ink);
    canvas.polygon(nullptr, two, nullptr, 4, ink);
}

void diamond(gfx::Canvas& canvas, const Box& box, bool over, bool pressed) {
    const float cx = box.midX(), cy = box.midY();
    const float h = std::min(box.w, box.h) * (pressed ? 0.40f : 0.46f);
    const uint32_t ink = pressed ? gfx::rgba(1.0f, 0.902f, 0.451f, 1.0f)
                         : over  ? gfx::rgba(1.0f, 0.855f, 0.302f, 1.0f)
                                 : gfx::rgba(0.878f, 0.741f, 0.365f, 0.80f);
    const float outer[8] = {cx, cy - h, cx + h, cy, cx, cy + h, cx - h, cy};
    canvas.polygon(nullptr, outer, nullptr, 4, ink);
    // The plus inside it, cut in the window's own body, so the diamond reads as a button.
    const float t = std::max(1.0f, h * 0.16f), arm = h * 0.44f;
    canvas.rect({cx - arm, cy - t, arm * 2.0f, t * 2.0f}, kBodyTop);
    canvas.rect({cx - t, cy - arm, t * 2.0f, arm * 2.0f}, kBodyTop);
}

void bar(gfx::Canvas& canvas, const Box& box, float share, uint32_t ink, float thick) {
    canvas.rect(box, kBarBack);
    const float filled = std::clamp(share, 0.0f, 1.0f) * box.w;
    if (filled > 0.0f) canvas.rect({box.x, box.y, filled, box.h}, ink);
    canvas.outline(box, thick, gfx::rgba(1.0f, 1.0f, 1.0f, 0.08f));
}

float kicker(gfx::Canvas& canvas, float x, float baseline, float size, const std::string& text,
             uint32_t inkColour, float track) {
    tip::tracked(canvas, x, baseline, size, track, inkColour, text, 1.0f);
    return x + tip::trackedWidth(canvas.face(), size, track, text);
}

void printed(gfx::Canvas& canvas, float x, float baseline, float size, uint32_t inkColour,
             const std::string& text) {
    canvas.text(x + 1.0f, baseline + 1.0f, size, kDrop, text);
    canvas.text(x, baseline, size, inkColour, text);
}

void ranged(gfx::Canvas& canvas, float right, float baseline, float size, uint32_t inkColour,
            const std::string& text) {
    printed(canvas, right - canvas.face().measure(size, text), baseline, size, inkColour, text);
}

std::string compactZen(long long zen) {
    char out[32];
    if (zen < 10000) {
        std::snprintf(out, sizeof out, "%lld", zen);
    } else if (zen < 1000000) {
        const double k = double(zen) / 1000.0;
        std::snprintf(out, sizeof out, k < 100.0 ? "%.1fK" : "%.0fK", k);
    } else {
        const double m = double(zen) / 1000000.0;
        std::snprintf(out, sizeof out, m < 100.0 ? "%.1fM" : "%.0fM", m);
    }
    return out;
}

std::string shouted(const std::string& text) {
    std::string out = text;
    for (char& c : out) c = char(std::toupper((unsigned char)c));
    return out;
}

}  // namespace mu::game::sheet
