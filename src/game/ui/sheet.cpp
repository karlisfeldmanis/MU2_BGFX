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
// **Lifted on 2026-09-23** -- the user, on the first build of it: *"a little bit too dark"*.
// Measured on the shot, the body was (8, 7, 6) over Lorencia's paving and its cells (16, 14, 13),
// which is near enough to black that the whole window read as a hole cut in the screen rather
// than as a panel lying on it. It is a dark warm GREY now, which is what the reference panels
// this skin was drawn from actually are, and the warmth is real: a touch more red than blue, so
// it sits in MU's own light instead of going cold against it.
// **And brought down a step on the same day**, once the grids were flat -- the user: *"make
// windows little bit darker, we are getting there"*. About a fifth off each end; still warm,
// still a grey and not a hole.
constexpr uint32_t kBodyTop = gfx::rgba(0.056f, 0.052f, 0.048f, 0.988f);
constexpr uint32_t kBodyFoot = gfx::rgba(0.030f, 0.028f, 0.026f, 0.982f);

// The edge, and the head's band of light over it.
constexpr uint32_t kEdgeTop = gfx::rgba(0.957f, 0.886f, 0.690f, 0.60f);
constexpr uint32_t kEdgeFoot = gfx::rgba(0.588f, 0.510f, 0.353f, 0.07f);
constexpr uint32_t kBandLit = gfx::rgba(1.0f, 0.941f, 0.804f, 0.115f);

// One hairline, the colour of old bronze, at three strengths: the window's edge, a rule inside
// it, and a cell's own border.
constexpr uint32_t kRule = gfx::rgba(0.643f, 0.573f, 0.404f, 0.38f);
constexpr uint32_t kRuleOut = gfx::rgba(0.643f, 0.573f, 0.404f, 0.0f);

constexpr uint32_t kCellBack = gfx::rgba(1.0f, 0.976f, 0.929f, 0.046f);
// Quieter than the page that was chosen: at 0.26 a grid of sixty-four reads as a
// lattice of bright lines from across the room, which is the one thing a flat skin
// must not do. The recess carries the cell and the border only closes it.
constexpr uint32_t kCellEdge = gfx::rgba(0.643f, 0.573f, 0.404f, 0.24f);
constexpr uint32_t kCellOver = gfx::rgba(1.0f, 0.976f, 0.929f, 0.125f);
constexpr uint32_t kCellOverEdge = gfx::rgba(0.886f, 0.816f, 0.600f, 0.55f);
constexpr uint32_t kCellHeld = gfx::rgba(0.0f, 0.0f, 0.0f, 0.30f);
constexpr uint32_t kFitsBack = gfx::rgba(0.267f, 0.545f, 0.937f, 0.24f);
constexpr uint32_t kFitsEdge = gfx::rgba(0.478f, 0.698f, 1.0f, 0.65f);
constexpr uint32_t kBlockedBack = gfx::rgba(0.886f, 0.290f, 0.220f, 0.24f);
constexpr uint32_t kBlockedEdge = gfx::rgba(1.0f, 0.451f, 0.373f, 0.65f);

constexpr uint32_t kBarBack = gfx::rgba(1.0f, 1.0f, 1.0f, 0.08f);
constexpr uint32_t kDrop = tip::ink::kDrop;

}  // namespace

void glass(gfx::Canvas& canvas, const Box& window, float radius) {
    tip::shadowUnder(canvas, window, std::max(1.0f, radius / tip::ink::kRadius));
    // The edge as the card draws its own: a graded fan a hairline wider than the body, and the
    // body over it, so the line follows the corner round. It was four straight hairlines held
    // back by the radius, which at a corner of two and a half units left nothing to see and at
    // one of six -- the user, 2026-09-23: *"round on border-radiuses for windows"* -- left a
    // gap at every corner.
    const float line = std::max(1.0f, radius * 0.08f);
    tip::panel(canvas, window.grown(line), radius + line, kEdgeTop, kEdgeFoot);
    tip::panel(canvas, window, radius, kBodyTop, kBodyFoot);
}

void band(gfx::Canvas& canvas, const Box& box, bool downward, float radius) {
    // A foot is half a head: the light in this window falls from above, so a band coming up out
    // of the bottom edge is a reflection and not a source. At the head's own strength it read as
    // a second header at the wrong end -- sampled, it lifted the foot to (30, 28, 25) against a
    // body of (15, 13, 12), which is a brighter step than the head makes.
    // Cut round at the window's own two corners, so the band's corners do not show square past
    // the body's round ones.
    const uint32_t lit = downward ? kBandLit : gfx::rgba(1.0f, 0.941f, 0.804f, 0.055f);
    const uint32_t out = gfx::rgba(1.0f, 0.941f, 0.804f, 0.0f);
    const float corners[4] = {downward ? radius : 0.0f, downward ? radius : 0.0f,
                              downward ? 0.0f : radius, downward ? 0.0f : radius};
    tip::rounded(canvas, box, corners, downward ? lit : out, downward ? out : lit);
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
    switch (state) {
        case Cell::Over: back = kCellOver; edge = kCellOverEdge; break;
        case Cell::Held: back = kCellHeld; break;
        case Cell::Fits: back = kFitsBack; edge = kFitsEdge; break;
        case Cell::Blocked: back = kBlockedBack; edge = kBlockedEdge; break;
        case Cell::Rest: break;
    }
    // **Flat.** A fill and a hairline, nothing else. The first cut of this skin gave every cell a
    // graded fill, a seat of shadow inside its top edge and a border lit along the top -- a well
    // -- and sixty-four wells with air between them read as sixty-four things. The user,
    // 2026-09-23: *"we need flat, organized look"*. So a cell is a square of one tone with one
    // line round it, and a grid of them (`grid`) is one block ruled into squares.
    canvas.rect(box, back);
    canvas.outline(box, thick, edge);
}

void grid(gfx::Canvas& canvas, const Box& box, int columns, int rows, float thick) {
    // One block, ruled: the fill once across the whole grid, a hairline between every two
    // columns and every two rows, and the border round it all -- so the lines between cells are
    // single lines and not two neighbours' borders side by side. Each rule is placed on a whole
    // pixel so a 1-pixel line does not land across two and go grey.
    canvas.rect(box, kCellBack);
    const float pw = box.w / float(columns), ph = box.h / float(rows);
    for (int c = 1; c < columns; ++c) {
        const float at = std::floor(box.x + pw * float(c) - thick * 0.5f + 0.5f);
        canvas.rect({at, box.y, thick, box.h}, kCellEdge);
    }
    for (int r = 1; r < rows; ++r) {
        const float at = std::floor(box.y + ph * float(r) - thick * 0.5f + 0.5f);
        canvas.rect({box.x, at, box.w, thick}, kCellEdge);
    }
    canvas.outline(box, thick, kCellEdge);
}

namespace {

// A disc and a ring, as fans of quads: the canvas has neither.
constexpr int kRoundSteps = 40;

void disc(gfx::Canvas& canvas, float cx, float cy, float r, uint32_t ink) {
    float xy[kRoundSteps * 2];
    for (int i = 0; i < kRoundSteps; ++i) {
        const float a = 6.28318531f * float(i) / float(kRoundSteps);
        xy[i * 2] = cx + std::cos(a) * r;
        xy[i * 2 + 1] = cy + std::sin(a) * r;
    }
    canvas.polygon(nullptr, xy, nullptr, kRoundSteps, ink);
}

void ring(gfx::Canvas& canvas, float cx, float cy, float r, float thick, uint32_t ink) {
    const float in = r - thick;
    for (int i = 0; i < kRoundSteps; ++i) {
        const float a = 6.28318531f * float(i) / float(kRoundSteps);
        const float b = 6.28318531f * float(i + 1) / float(kRoundSteps);
        const float xy[8] = {cx + std::cos(a) * r,  cy + std::sin(a) * r,
                             cx + std::cos(b) * r,  cy + std::sin(b) * r,
                             cx + std::cos(b) * in, cy + std::sin(b) * in,
                             cx + std::cos(a) * in, cy + std::sin(a) * in};
        canvas.polygon(nullptr, xy, nullptr, 4, ink);
    }
}

}  // namespace

void close(gfx::Canvas& canvas, const Box& box, bool over, bool pressed) {
    // **A ring with a cross in it.** The user, 2026-09-23: *"more polished circle type close
    // buttons"*. The disc is the button: a faint fill of the window's lettering at rest, lifted
    // under the pointer and lit when pressed; a hairline ring closes it; the cross stands inside
    // at a third of the radius. Two thirds of the socket across, so the ring has air on every
    // side of MU's 24-unit button box and does not touch the head's rule.
    const uint32_t ink = pressed  ? gfx::rgba(1.0f, 1.0f, 1.0f, 0.95f)
                         : over   ? gfx::rgba(0.941f, 0.847f, 0.604f, 0.95f)
                                  : gfx::rgba(0.769f, 0.757f, 0.706f, 0.70f);
    const uint32_t fill = pressed ? gfx::rgba(1.0f, 0.941f, 0.804f, 0.16f)
                          : over  ? gfx::rgba(1.0f, 0.941f, 0.804f, 0.09f)
                                  : gfx::rgba(1.0f, 0.941f, 0.804f, 0.04f);
    const uint32_t rim = pressed  ? gfx::rgba(1.0f, 1.0f, 1.0f, 0.70f)
                         : over   ? gfx::rgba(0.941f, 0.847f, 0.604f, 0.75f)
                                  : gfx::rgba(0.643f, 0.573f, 0.404f, 0.45f);
    const float cx = box.midX(), cy = box.midY();
    const float r = std::min(box.w, box.h) * 0.34f;
    const float line = std::max(1.0f, r * 0.09f);
    disc(canvas, cx, cy, r, fill);
    ring(canvas, cx, cy, r, line, rim);
    // Two bars on the diagonal, drawn as quads because the canvas has no line.
    const float arm = r * 0.36f;
    const float t = std::max(1.0f, arm * 0.22f);
    const float one[8] = {cx - arm - t, cy - arm + t, cx - arm + t, cy - arm - t,
                          cx + arm + t, cy + arm - t, cx + arm - t, cy + arm + t};
    const float two[8] = {cx + arm - t, cy - arm - t, cx + arm + t, cy - arm + t,
                          cx - arm + t, cy + arm + t, cx - arm - t, cy + arm - t};
    canvas.polygon(nullptr, one, nullptr, 4, ink);
    canvas.polygon(nullptr, two, nullptr, 4, ink);
}

void diamond(gfx::Canvas& canvas, const Box& box, bool over, bool pressed) {
    // **A ring with a plus in it**, the close button's own shape in gold: the two buttons a
    // window has are then one kind of thing. The disc fills gold under the pointer and lights
    // when pressed; at rest it is a gold hairline ring with a faint fill, and the plus is gold.
    const float cx = box.midX(), cy = box.midY();
    const float r = std::min(box.w, box.h) * 0.42f;
    const float line = std::max(1.0f, r * 0.11f);
    const uint32_t gold = gfx::rgba(0.878f, 0.741f, 0.365f, 0.90f);
    const uint32_t bright = gfx::rgba(1.0f, 0.855f, 0.302f, 1.0f);
    const uint32_t lit = gfx::rgba(1.0f, 0.902f, 0.451f, 1.0f);
    const uint32_t fill = pressed ? lit : over ? bright : gfx::rgba(0.878f, 0.741f, 0.365f, 0.10f);
    const uint32_t rim = pressed ? lit : over ? bright : gold;
    const uint32_t ink = (pressed || over) ? kBodyTop : gold;
    disc(canvas, cx, cy, r, fill);
    ring(canvas, cx, cy, r, line, rim);
    const float t = std::max(1.0f, r * 0.13f), arm = r * 0.48f;
    canvas.rect({cx - arm, cy - t, arm * 2.0f, t * 2.0f}, ink);
    canvas.rect({cx - t, cy - arm, t * 2.0f, arm * 2.0f}, ink);
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
