#include "game/ui/tip.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "game/ui/controls.h"
#include "game/ui/panel.h"

namespace mu::game::tip {
namespace {

// The design page's pixels at 1080 lines; `u` below is height / 1080.
constexpr float kWide = 346.0f;
constexpr float kPad = 12.0f;
constexpr float kPlate = 56.0f;
constexpr float kNameSize = 15.5f;
constexpr float kBaseSize = 10.5f;
constexpr float kKickerSize = 9.5f;
constexpr float kRowSize = 12.5f;
constexpr float kChipSize = 10.0f;
constexpr float kFootSize = 11.5f;
constexpr float kBaseTrack = 0.14f;    // em
constexpr float kKickerTrack = 0.16f;
constexpr float kChipTrack = 0.08f;
constexpr float kRowTall = 1.5f;       // of its own size
constexpr float kRailPad = 8.0f;
constexpr float kMarkColumn = 16.0f;
constexpr float kMarkGap = 9.0f;
// The corners, and how many segments each quarter turn is cut into. Six is smooth at this
// radius and keeps the whole card inside one fan of 28 points.
constexpr float kRadius = ink::kRadius;
// How far above the item the card floats, so a raised cell edge and the card's own shadow do
// not touch.
constexpr float kStandOff = 10.0f;
constexpr int kCorner = 6;

// The shadow: three falloffs of the page's, summed into one field -- a contact hairline under
// the edge, then wider and fainter. Offsets and sigmas in the same 1080-line pixels.
struct Fall { float drop, sigma, alpha; };
constexpr Fall kFalls[3] = {{1.0f, 1.5f, 0.70f}, {8.0f, 7.0f, 0.55f}, {30.0f, 30.0f, 0.55f}};
// The shadow's grid a side: cells out to its reach, round each corner, and across the card.
constexpr int kShadowOutside = 10, kShadowCorner = 5, kShadowAcross = 6;

// Two opacities, because they are two different jobs. `kOpacity` is the card's own glass and
// goes through every colour it draws -- ring, marks, labels, numbers, chips, foot -- so the
// whole container is one sheet. The BACKGROUND is separately much thinner: the world is meant
// to move behind it, and at one shared alpha a plate dark enough to read on is a plate nothing
// shows through. It is a gradient, lighter at the head and settling toward the foot, which is
// what keeps it from reading as a flat grey rectangle.
constexpr float kOpacity = 0.94f;
// The inks are `tip::ink` now, so the rail above the plate and this card cannot drift apart;
// these are the names the rest of this file already used.
constexpr uint32_t kBodyTop = ink::kBodyTop;
constexpr uint32_t kBodyFoot = ink::kBodyFoot;
constexpr uint32_t kBody = kBodyTop;  // the corners' own fill; the gradient is drawn over it
constexpr uint32_t kRing = ink::kRing;
constexpr uint32_t kHair = ink::kHair;
constexpr uint32_t kLabel = ink::kLabel;
constexpr uint32_t kQuiet = ink::kQuiet;
constexpr uint32_t kFoot = gfx::rgba(0.490f, 0.502f, 0.467f);
constexpr uint32_t kFootBack = gfx::rgba(0.0f, 0.0f, 0.0f, 0.40f);
constexpr uint32_t kFramed = ink::kFramed;
constexpr uint32_t kFrame = ink::kFrame;
constexpr uint32_t kGood = gfx::rgba(0.498f, 0.831f, 0.545f);
constexpr uint32_t kBad = gfx::rgba(0.886f, 0.408f, 0.373f);
constexpr uint32_t kPlateEdge = ink::kPlateEdge;
constexpr uint32_t kPlateBack = ink::kPlateBack;
constexpr uint32_t kBarBack = gfx::rgba(1.0f, 1.0f, 1.0f, 0.12f);

// Everything the card draws goes through this: the colour with its alpha taken down by the
// card's own opacity.
constexpr uint32_t fade(uint32_t abgr) {
    const uint32_t a = (abgr >> 24) & 0xFFu;
    return (abgr & 0x00FFFFFFu) | (uint32_t(float(a) * kOpacity + 0.5f) << 24);
}

// A rounded rectangle as one convex fan, with a radius a corner: the canvas draws polygons and
// quads and has no rounded primitive, and a rounded rectangle is convex, so one polygon does it.
// Corners run top-left, top-right, bottom-right, bottom-left.
void roundedFan(gfx::Canvas& canvas, const gfx::Box& box, const float radius[4], uint32_t colour,
                uint32_t foot = 0u, float fadeFrom = 0.0f, float fadeTo = 1.0f) {
    float xy[(kCorner + 1) * 4 * 2];
    int at = 0;
    const float cx[4] = {box.x, box.right(), box.right(), box.x};
    const float cy[4] = {box.y, box.y, box.bottom(), box.bottom()};
    const float sx[4] = {1.0f, -1.0f, -1.0f, 1.0f};
    const float sy[4] = {1.0f, 1.0f, -1.0f, -1.0f};
    // Each corner is a quarter turn from the edge before it to the edge after it.
    const float from[4] = {3.14159265f, 4.71238898f, 0.0f, 1.57079633f};
    for (int c = 0; c < 4; ++c) {
        const float rad = std::min({radius[c], box.w * 0.5f, box.h * 0.5f});
        const float ox = cx[c] + sx[c] * rad, oy = cy[c] + sy[c] * rad;
        for (int i = 0; i <= kCorner; ++i) {
            const float a = from[c] + 1.57079633f * float(i) / float(kCorner);
            xy[at++] = ox + std::cos(a) * rad;
            xy[at++] = oy + std::sin(a) * rad;
        }
    }
    if (foot == 0u) {
        canvas.polygon(nullptr, xy, nullptr, at / 2, colour);
        return;
    }
    // A colour a vertex, mixed by where the point stands between `fadeFrom` and `fadeTo` of the
    // box's own height: the corners grade with everything else, which a fan of one colour under
    // a gradient quad cannot do.
    uint32_t colours[(kCorner + 1) * 4];
    const auto channel = [](uint32_t c, int shift) { return float((c >> shift) & 0xFFu); };
    for (int i = 0; i < at / 2; ++i) {
        const float down = box.h > 0.0f ? (xy[i * 2 + 1] - box.y) / box.h : 0.0f;
        const float t = std::clamp((down - fadeFrom) / std::max(0.001f, fadeTo - fadeFrom), 0.0f,
                                   1.0f);
        uint32_t mixed = 0;
        for (int shift = 0; shift < 32; shift += 8) {
            const float v = channel(colour, shift) + (channel(foot, shift) - channel(colour, shift)) * t;
            mixed |= uint32_t(v + 0.5f) << shift;
        }
        colours[i] = mixed;
    }
    canvas.polygon(xy, colours, at / 2);
}

// Every word on the card is printed over its own shadow: a pixel down and right in black, which
// is MU's own way of putting text over art (`RenderTextByScript`'s drop) and what makes a label
// readable on glass this thin. The colour is passed through `fade` by the caller; the shadow is
// its own alpha.
constexpr uint32_t kDrop = ink::kDrop;

}  // namespace

// The four below are declared in the header: the rail above the plate sets type with them, and a
// second copy of "a line with a drop shadow" is a second thing to keep in step.
float printed(gfx::Canvas& canvas, float x, float baseline, float size, uint32_t colour,
              const std::string& s, float drop) {
    canvas.text(x + drop, baseline + drop, size, kDrop, s);
    return canvas.text(x, baseline, size, colour, s);
}

// A line of text with CSS's letter-spacing: `track` ems after every letter. Drawn a glyph at a
// time, because the canvas's own text() advances by the face alone.
float trackedWidth(const gfx::Face& face, float size, float track, const std::string& s) {
    return face.measure(size, s) + size * track * float(s.size());
}
void tracked(gfx::Canvas& canvas, float x, float baseline, float size, float track,
             uint32_t colour, const std::string& s, float drop) {
    float pen = x;
    for (char c : s) {
        const std::string one(1, c);
        canvas.text(pen + drop, baseline + drop, size, kDrop, one);
        canvas.text(pen, baseline, size, colour, one);
        pen += canvas.face().measure(size, one) + size * track;
    }
}

// Where a line of `size` sits to be centred in a box `tall` high from `top`: the half-leading
// above it and the same below, which is what `line-height` means on the page the card came from
// and what panel::centredBaseline does for the windows. Text hung off the top of its own box
// instead reads a pixel or two high in every row, which is what this was doing.
float middle(const gfx::Face& face, float top, float tall, float size) {
    return top + (tall - face.ascent(size) - face.descent(size)) * 0.5f + face.ascent(size);
}

// The interface's own pixel, not the windows' scale: a tip stands over the world as often
// as over a window, and it kept its size when the windows were brought in on 2026-09-23.
float unit() { return panel::unit() * 0.5f; }

namespace {

// The words of `text` broken to `wide` by `measure`, at least one word a line.
template <typename Measure>
std::vector<std::string> wrappedBy(const std::string& text, float wide, Measure measure) {
    std::vector<std::string> out;
    std::string line;
    size_t at = 0;
    while (at <= text.size()) {
        const size_t space = text.find(' ', at);
        const std::string word = text.substr(at, space - at);
        const std::string tried = line.empty() ? word : line + " " + word;
        if (!line.empty() && measure(tried) > wide) {
            out.push_back(line);
            line = word;
        } else {
            line = tried;
        }
        if (space == std::string::npos) break;
        at = space + 1;
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

}  // namespace

// One section's mark, drawn as a small figure rather than a letter: the face bakes ASCII only,
// and a "+" standing in for a blade would read as a plus. Declared in the header since
// 2026-09-23: the windows' heads carry the same marks, and a second set of them drawn somewhere
// else is a second set to keep in step.
void glyphAt(gfx::Canvas& canvas, Mark which, float cx, float cy, float size, uint32_t colour) {
    const auto quad = [&](float x0, float y0, float x1, float y1, float x2, float y2, float x3,
                          float y3) {
        const float xy[8] = {x0, y0, x1, y1, x2, y2, x3, y3};
        canvas.polygon(nullptr, xy, nullptr, 4, colour);
    };
    const float h = size * 0.5f;
    switch (which) {
        case Mark::Blade: {
            // A blade on the diagonal with its guard across it.
            const float t = size * 0.11f;
            quad(cx - h + t, cy + h, cx - h, cy + h - t, cx + h - t, cy - h, cx + h, cy - h + t);
            quad(cx - h * 0.1f, cy + h * 0.55f, cx + h * 0.55f, cy - h * 0.1f,
                 cx + h * 0.55f - t * 1.4f, cy - h * 0.1f - t * 1.4f,
                 cx - h * 0.1f - t * 1.4f, cy + h * 0.55f - t * 1.4f);
            break;
        }
        case Mark::Shield: {
            // A shield: square shoulders down to a point.
            const float xy[10] = {cx - h, cy - h * 0.85f, cx + h,        cy - h * 0.85f,
                                  cx + h, cy - h * 0.1f,  cx,            cy + h,
                                  cx - h, cy - h * 0.1f};
            canvas.polygon(nullptr, xy, nullptr, 5, colour);
            break;
        }
        case Mark::Star: {
            // Four spikes: a diamond pulled out at the points.
            const float in = size * 0.16f;
            const float xy[16] = {cx, cy - h,  cx + in, cy - in, cx + h, cy,      cx + in, cy + in,
                                  cx, cy + h,  cx - in, cy + in, cx - h, cy,      cx - in, cy - in};
            canvas.polygon(nullptr, xy, nullptr, 8, colour);
            break;
        }
        case Mark::Triangle: {
            const float xy[6] = {cx, cy - h, cx + h, cy + h * 0.8f, cx - h, cy + h * 0.8f};
            canvas.polygon(nullptr, xy, nullptr, 3, colour);
            break;
        }
        case Mark::Diamond:
        case Mark::Socket: {
            // An upright square, a little smaller than the turned one it replaces: the user
            // refused every diamond on the Sanctuary page (2026-09-27).
            const float s = h * 0.62f;
            canvas.rect({cx - s, cy - s, s * 2.0f, s * 2.0f}, colour);
            if (which == Mark::Socket) {  // hollow: a socket is a hole
                const float i = s * 0.5f;
                canvas.rect({cx - i, cy - i, i * 2.0f, i * 2.0f}, kBody);
            }
            break;
        }
        case Mark::Note: {
            const float t = size * 0.13f;
            quad(cx - t, cy - h, cx + t, cy - h, cx + t * 0.6f, cy + h * 0.2f, cx - t * 0.6f,
                 cy + h * 0.2f);
            canvas.rect({cx - t, cy + h * 0.5f, t * 2.0f, t * 2.0f}, colour);
            break;
        }
        case Mark::None:
            break;
    }
}

uint32_t colourOf(Tone tone) {
    switch (tone) {
        case Tone::Blue: return gfx::rgba(0.5f, 0.7f, 1.0f);
        case Tone::Red: return gfx::rgba(1.0f, 0.2f, 0.1f);
        case Tone::Yellow: return gfx::rgba(1.0f, 0.8f, 0.1f);
        case Tone::Green: return gfx::rgba(0.1f, 1.0f, 0.5f);
        case Tone::Gray: return gfx::rgba(0.4f, 0.4f, 0.4f);
        case Tone::Violet: return gfx::rgba(0.7f, 0.4f, 1.0f);
        case Tone::RedPurple: return gfx::rgba(0.8f, 0.5f, 0.8f);
        case Tone::Orange: return gfx::rgba(0.9f, 0.42f, 0.04f);
        case Tone::White:
        default: return gfx::rgba(1.0f, 1.0f, 1.0f);
    }
}

void stand(Stage& stage, int32_t item, int refinement, Sheet& sheet) {
    const std::vector<Standing> one = {
        Standing{item, {0.0f, 0.0f, kPlateUnits, kPlateUnits}, refinement, false}};
    stage.stand(one, kPlateUnits, kPlateUnits);
    const gfx::Art picture = stage.picture();
    if (!picture.valid()) return;
    sheet.picture = picture;
    sheet.from = {0.0f, 0.0f, picture.width, picture.height};
}

// The container: the shadow, the ring and the graded body, and nothing printed on it.
//
// Lifted out of `draw` on 2026-09-23, when the skill rail above the plate was asked for in this
// card's style. It is one function rather than two copies for the reason the inks are in the
// header: a container drawn twice drifts, and the user asked for the same style and not a
// similar one.
// The shadow alone, so a window can lay it down and then draw its own edge over its own body:
// the three falloffs summed into one field and laid down as a grid of shaded quads, so it has no
// edge anywhere. A rectangle blurred by a Gaussian is the product of two error functions, one
// each way, which is what `edge` is.
void shadowUnder(gfx::Canvas& canvas, const gfx::Box& box, float u, float radius) {
    {
        const float reach = kFalls[2].sigma * 3.0f * u + kFalls[2].drop * u;
        const auto edge = [](float at, float low, float high, float sigma) {
            const float k = 1.0f / (sigma * 1.41421356f);
            return 0.5f * (std::erf((at - low) * k) - std::erf((at - high) * k));
        };
        // Cut out from under the card. The card is glass, so a shadow laid down across its whole
        // footprint is what shows THROUGH it -- the world behind never gets a look in, and the
        // card reads as solid however thin its background is. `covered` is the card's own
        // rounded shape, and the shadow is what is left outside it: full strength right up to
        // the edge, which the card's own ring then draws over.
        const float r = std::clamp(radius, 0.0f, std::min(box.w, box.h) * 0.5f);
        const auto covered = [&](float px, float py) {
            const float qx = std::abs(px - box.midX()) - (box.w * 0.5f - r);
            const float qy = std::abs(py - (box.y + box.h * 0.5f)) - (box.h * 0.5f - r);
            const float out = std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f));
            const float inside = std::min(std::max(qx, qy), 0.0f);
            return std::clamp(-(out + inside - r), 0.0f, 1.0f);
        };
        const auto alphaAt = [&](float px, float py) {
            float sum = 0.0f;
            for (const Fall& f : kFalls) {
                sum += f.alpha * edge(px, box.x, box.right(), f.sigma * u) *
                       edge(py, box.y + f.drop * u, box.bottom() + f.drop * u, f.sigma * u);
            }
            return std::min(0.85f, sum) * (1.0f - covered(px, py));
        };
        // The grid's lines fall on the card's edges and round its corners, finer there. On an
        // even grid the cut landed inside a cell and was smeared across it: at the menu's size
        // a cell was 30 to 40 pixels, so the shadow faded in that far below the foot, and the
        // square cut left the round corners pale.
        const auto lines = [&](float lo, float from, float to, float hi, float tail) {
            std::vector<float> at;
            const auto run = [&](float a, float b, int n) {
                for (int i = 0; i < n; ++i) at.push_back(a + (b - a) * float(i) / float(n));
            };
            const float round = std::min(r, (to - from) * 0.5f);
            run(lo, from, kShadowOutside);
            run(from, from + round, round > 0.0f ? kShadowCorner : 0);
            run(from + round, to - round, kShadowAcross);
            run(to - round, to, round > 0.0f ? kShadowCorner : 0);
            run(to, hi, int(std::round(float(kShadowOutside) * tail)));
            at.push_back(hi);
            return at;
        };
        const std::vector<float> xs = lines(box.x - reach, box.x, box.right(), box.right() + reach, 1.0f);
        const std::vector<float> ys =
            lines(box.y - reach, box.y, box.bottom(), box.bottom() + reach * 1.4f, 1.4f);
        for (size_t j = 0; j + 1 < ys.size(); ++j) {
            for (size_t i = 0; i + 1 < xs.size(); ++i) {
                const float ax = xs[i], bx = xs[i + 1], ay = ys[j], by = ys[j + 1];
                // Wholly under the card, clear of its corners: nothing to lay down.
                if (ax >= box.x + r && bx <= box.right() - r && ay >= box.y && by <= box.bottom()) continue;
                if (ay >= box.y + r && by <= box.bottom() - r && ax >= box.x && bx <= box.right()) continue;
                canvas.shade({ax, ay, bx - ax, by - ay}, gfx::rgba(0, 0, 0, alphaAt(ax, ay)),
                             gfx::rgba(0, 0, 0, alphaAt(bx, ay)), gfx::rgba(0, 0, 0, alphaAt(bx, by)),
                             gfx::rgba(0, 0, 0, alphaAt(ax, by)));
            }
        }
    }
}

// The rounded body on its own, graded from its head to its foot. `radius` is in pixels here, not
// in the card's units: a window sets its own corner.
void panel(gfx::Canvas& canvas, const gfx::Box& box, float radius, uint32_t top, uint32_t foot) {
    const float all[4] = {radius, radius, radius, radius};
    roundedFan(canvas, box, all, top, foot);
}

void rounded(gfx::Canvas& canvas, const gfx::Box& box, const float radius[4], uint32_t top,
             uint32_t foot) {
    roundedFan(canvas, box, radius, top, foot);
}

void glass(gfx::Canvas& canvas, const gfx::Box& box, float u, float radius, uint32_t top,
           uint32_t foot) {
    shadowUnder(canvas, box, u, radius * u);
    // The ring first and a hair wider, then the body over it: two fans, and the ring is left
    // showing as the edge. Drawn rounded, and see-through enough that the world moves behind it.
    const float r = radius * u;
    const float line = std::max(1.0f, u);
    const float all[4] = {r, r, r, r};
    const float wider[4] = {r + line, r + line, r + line, r + line};
    const float widest[4] = {r + line * 2.0f, r + line * 2.0f, r + line * 2.0f, r + line * 2.0f};
    // Sanctuary's edge: a seam of black outside a lit iron ring, then the body on stone.
    roundedFan(canvas, box.grown(line * 2.0f), widest, gfx::rgba(0.0f, 0.0f, 0.0f, 1.0f));
    roundedFan(canvas, box.grown(line), wider, fade(kRing), fade(gfx::rgba(0.165f, 0.125f, 0.098f)));
    roundedFan(canvas, box, all, top != 0u ? top : kBodyTop, foot != 0u ? foot : kBodyFoot);
    controls::grain(canvas, box.grown(-line));
}

namespace {

// The item card's own sizes (`Sheet::item`), in the same 1080-line pixels.
constexpr float kArt = 96.0f;            // the picture, square, at the head's right
constexpr float kItemNameSize = 17.0f;
constexpr float kItemNameTrack = 0.06f;  // em
constexpr float kTypeSize = 13.0f;
constexpr float kHeroSize = 21.0f;
constexpr float kHeroWordSize = 13.5f;
constexpr float kRailSize = 12.0f;
constexpr float kRailIndent = 13.0f;     // the rail's text, past its line
constexpr float kMarkSize = 8.0f;        // a row's mark, smaller than a section's
constexpr float kFootLine = 1.5f;        // a foot line, of the foot's size
constexpr uint32_t kRailInk = gfx::rgba(1.0f, 1.0f, 1.0f, 0.18f);

std::string capitals(std::string s) {
    for (char& c : s) {
        if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    }
    return s;
}

// A value's ink: its tone taken a third of the way to white, so "+5%" stands out of the blue
// line it ends without leaving the blue.
uint32_t brighter(uint32_t abgr) {
    uint32_t out = abgr & 0xFF000000u;
    for (int shift = 0; shift < 24; shift += 8) {
        const float v = float((abgr >> shift) & 0xFFu);
        out |= uint32_t(v + (255.0f - v) * 0.35f + 0.5f) << shift;
    }
    return out;
}

// A heavier weight than the face has: the line struck twice, half a pixel apart. The one body
// face is Open Sans SemiBold, and a Bold is another bake and another texture for the handful of
// characters a value is; at twelve pixels the doubled stroke reads as bold. Returns the width.
float heavy(gfx::Canvas& canvas, float x, float baseline, float size, uint32_t colour,
            const std::string& s, float drop, float u) {
    const float thicken = std::max(0.5f, 0.45f * u);
    canvas.text(x + drop, baseline + drop, size, kDrop, s);
    canvas.text(x + thicken + drop, baseline + drop, size, kDrop, s);
    canvas.text(x, baseline, size, colour, s);
    return canvas.text(x + thicken, baseline, size, colour, s) + thicken;
}

// A free row as one string: the keyword, the prose and the value, which wrap together.
std::string composed(const Row& row) {
    std::string s;
    if (!row.keyword.empty()) s = row.keyword + " \xB7 ";
    s += row.free;
    if (!row.tail.empty()) s += (s.empty() ? "" : " ") + row.tail;
    return s;
}
bool prosaic(const Row& row) { return !row.free.empty() || !row.tail.empty(); }

// Where each wrapped line of `text` starts in it and how long it is, so a line can be coloured by
// what part of the row it holds.
struct Span {
    size_t at = 0, length = 0;
};
std::vector<Span> spansOf(const gfx::Face& face, float size, const std::string& text, float wide) {
    std::vector<Span> out;
    size_t start = 0, end = 0, at = 0;
    while (at <= text.size()) {
        const size_t space = text.find(' ', at);
        const size_t wordEnd = space == std::string::npos ? text.size() : space;
        if (end > start && face.measure(size, text.substr(start, wordEnd - start)) > wide) {
            out.push_back({start, end - start});
            start = at;
        }
        end = wordEnd;
        if (space == std::string::npos) break;
        at = space + 1;
    }
    if (end > start) out.push_back({start, end - start});
    return out;
}

}  // namespace

void draw(gfx::Canvas& canvas, const Sheet& sheet, float x, float y, float screenWidth,
          float screenHeight) {
    if (sheet.empty()) return;
    const gfx::Face& face = canvas.face();
    const float u = unit();
    const bool item = sheet.item;
    const float wide = (sheet.wide > 0.0f ? sheet.wide : kWide) * u, pad = kPad * u;
    const float rowSize = kRowSize * u;
    const float kickerSize = kKickerSize * u, footSize = kFootSize * u, chipSize = kChipSize * u;
    const float rowTall = std::round(rowSize * kRowTall);
    const float railPad = kRailPad * u;
    // The mark column is reserved only when something is standing in it. An item card always has
    // a mark beside its options, so the gutter is the alignment; a card with no marks at all --
    // a skill's -- was indenting every row past an empty 25 units while its name sat at the pad,
    // which is the ragged left edge the user saw. The item card marks rows, not sections.
    bool marked = false;
    for (const Section& section : sheet.sections) {
        marked |= !item && section.mark != Mark::None;
        for (const Row& row : section.rows) marked |= row.mark != Mark::None;
    }
    const float textX = marked ? pad + kMarkColumn * u + kMarkGap * u : pad;
    const float drop = std::max(1.0f, u);
    const float textWide = wide - textX - pad;

    // ---- measure ----------------------------------------------------------------------------
    // The head. The item card's name is set in capitals in the window titles' Cinzel, where it is
    // baked; the picture stands large at its right rather than in a plate at its left.
    const gfx::Face* gothic = item ? panel::titleFace() : nullptr;
    const gfx::Face& nameFace = gothic ? *gothic : face;
    const float nameSize = (item ? kItemNameSize : kNameSize) * u;
    const float nameTrack = gothic ? kItemNameTrack * nameSize : 0.0f;
    const std::string name = item ? capitals(sheet.name) : sheet.name;
    const float art =
        item && sheet.picture.valid() ? kArt * u * std::clamp(sheet.artScale, 0.2f, 1.0f) : 0.0f;
    const float plate = !item && sheet.picture.valid() ? kPlate * u : 0.0f;
    const float headTextX = pad + (plate > 0.0f ? plate + kPad * u : 0.0f);
    // The name keeps out of the picture's middle; the model stands in the centre of its square,
    // and its sides are mostly air.
    const float nameWide = wide - headTextX - pad - art * 0.6f;
    const std::vector<std::string> title = wrappedBy(name, nameWide, [&](const std::string& s) {
        return nameFace.measure(nameSize, s) + nameTrack * float(s.size());
    });
    const float titleLine = std::round(nameSize * 1.25f);
    const float titleTall = float(title.size()) * titleLine;
    const float baseSize = (item ? kTypeSize : kBaseSize) * u;
    const float baseTall =
        sheet.base.empty() ? 0.0f : std::round(baseSize * (item ? 1.6f : 1.5f));
    const float headTall = std::max(plate, titleTall + baseTall) + pad * 2.0f;

    // The headline and its rail, under the name with no rule between: the number is the second
    // thing read, after what the thing is called.
    const Hero& hero = sheet.hero;
    const float heroSize = kHeroSize * u, heroWordSize = kHeroWordSize * u,
                railSize = kRailSize * u;
    const float heroLine = std::round(heroSize * 1.3f);
    const float railLine = std::round(railSize * 1.55f);
    const float heroTall = item && !hero.empty()
                               ? heroLine + float(hero.rail.size()) * railLine + railPad * 1.5f
                               : 0.0f;
    // The sections start under the hero, and under the picture where the picture reaches
    // further down: it may stand beside the hero's short lines but not behind an option's long one.
    const float artTop = pad * 0.5f;
    const float bodyTop =
        std::max(headTall + heroTall, art > 0.0f ? artTop + art + railPad * 0.5f : 0.0f);

    // Free rows wrap, so each section is measured by its rows rather than counted.
    std::vector<std::vector<std::vector<Span>>> prose(sheet.sections.size());
    std::vector<float> sectionTall(sheet.sections.size(), 0.0f);
    for (size_t s = 0; s < sheet.sections.size(); ++s) {
        const Section& section = sheet.sections[s];
        float tall = railPad * 2.0f;
        if (!item && !section.kicker.empty()) tall += std::round(kickerSize * 1.75f);
        prose[s].resize(section.rows.size());
        for (size_t r = 0; r < section.rows.size(); ++r) {
            const Row& row = section.rows[r];
            if (prosaic(row)) {
                prose[s][r] = spansOf(face, rowSize, composed(row),
                                      textWide - (section.framed ? pad : 0.0f));
                tall += float(prose[s][r].size()) * rowTall;
            } else {
                tall += std::max<size_t>(1, row.values.size()) * rowTall;
            }
        }
        if (section.framed) tall += railPad;
        sectionTall[s] = tall;
    }

    // The foot. The item card's is two columns, one line each: who it is for on the left, and on
    // the right the wear, the note, what it keeps, and the price.
    std::vector<FootLine> right;
    if (item) {
        if (!sheet.wear.empty()) right.push_back({sheet.wear, sheet.wearTone, true});
        if (!sheet.note.empty()) right.push_back({sheet.note, sheet.noteTone, false});
        for (const FootLine& line : sheet.keep) right.push_back(line);
        if (!sheet.price.empty()) right.push_back({sheet.price, sheet.priceTone, false});
    }
    const float footLine = std::round(footSize * kFootLine);
    const size_t footLines = std::max(sheet.who.size(), right.size());
    const bool hasFoot = item ? footLines > 0
                              : !sheet.wear.empty() || !sheet.price.empty() || !sheet.note.empty();
    const float footTall = !hasFoot ? 0.0f
                           : item   ? float(footLines) * footLine + railPad * 2.0f
                                    : std::round(footSize * 1.4f) + railPad * 2.0f;
    // A card with no foot ends on its last row with one rail's padding under it, which is less
    // air than the head carries over its name and reads as the text falling out of the bottom.
    // The difference is made up here rather than in the section, so an item card -- which always
    // has a foot -- is untouched.
    float tall = bodyTop + footTall + (hasFoot ? 0.0f : pad - railPad);
    for (float t : sectionTall) tall += t;
    // The sell strip, under everything, one foot line and its padding tall.
    const float sellTall = sheet.sell.empty() ? 0.0f : std::round(footSize * 1.5f) + railPad * 2.0f;
    tall += sellTall;

    // ---- place ------------------------------------------------------------------------------
    const float margin = 4.0f * u;
    const float ox = std::clamp(x - wide * 0.5f, margin, std::max(margin, screenWidth - margin - wide));
    const float oy = std::clamp(y - tall - kStandOff * u, margin,
                                std::max(margin, screenHeight - margin - tall));
    const gfx::Box box{ox, oy, wide, tall};

    glass(canvas, box, u);
    const float radius = kRadius * u;
    const float line = std::max(1.0f, u);

    // The head, tinted by the name's own colour, fading out downward. Its own top corners are
    // rounded to the card's; it fades before it reaches the bottom two, so those stay square.
    const uint32_t nameColour = colourOf(sheet.nameTone);
    const uint32_t tintTop = fade((nameColour & 0x00FFFFFFu) | (uint32_t(0.13f * 255.0f) << 24));
    const uint32_t tintOut = nameColour & 0x00FFFFFFu;  // clear at its foot, fade or no fade
    {
        const float tops[4] = {radius, radius, 0.0f, 0.0f};
        roundedFan(canvas, {box.x, box.y, box.w, headTall}, tops, tintTop, tintOut);
        // And the name's colour as a lit line along the top edge, fading at both ends: the one
        // place the card says at a glance what the thing is -- yellow +7, green excellent.
        const float half = (box.w - radius * 2.0f) * 0.5f, edge = line * 2.0f;
        const uint32_t lit = fade((nameColour & 0x00FFFFFFu) | (uint32_t(0.85f * 255.0f) << 24));
        canvas.shade({box.x + radius, box.y - line, half, edge}, tintOut, lit, lit, tintOut);
        canvas.shade({box.x + radius + half, box.y - line, half, edge}, lit, tintOut, tintOut, lit);
    }
    // **The sell strip**: laid first so the foot above it draws as it always has. A darker band
    // across the card's bottom under an iron rule; "Sells for" in the foot's quiet capitals on
    // the left; MU's coin and the figure in Zen yellow ranged right, which is where the eye ends
    // a card and where Diablo IV prints what a thing is worth.
    if (sellTall > 0.0f) {
        const gfx::Box strip{box.x, box.bottom() - sellTall, box.w, sellTall};
        const float bottoms[4] = {0.0f, 0.0f, radius, radius};
        roundedFan(canvas, strip, bottoms, fade(gfx::rgba(0.0f, 0.0f, 0.0f, 0.55f)));
        canvas.rect({strip.x, strip.y, strip.w, line}, fade(gfx::rgba(0.420f, 0.337f, 0.271f, 0.9f)));
        const float baseline = middle(face, strip.y, strip.h, footSize);
        tracked(canvas, strip.x + pad, baseline, footSize * 0.92f, 0.12f, fade(kFoot), "SELLS FOR",
                drop);
        const float figure = footSize * 1.12f;
        const float w = face.measure(figure, sheet.sell);
        const float right = strip.right() - pad;
        heavy(canvas, right - w, baseline, figure, fade(colourOf(Tone::Yellow)), sheet.sell, drop, u);
        if (sheet.coin.valid()) {
            const float side = std::round(figure * 1.25f);
            const float cw = side * sheet.coin.width / std::max(1.0f, sheet.coin.height);
            canvas.image(sheet.coin, {std::round(right - w - pad * 0.45f - cw),
                                      std::round(strip.midY() - side * 0.5f), cw, side},
                         fade(0xFFFFFFFFu));
        }
    }

    float pen = box.y + pad;
    if (plate > 0.0f) {
        const gfx::Box at{box.x + pad, pen, plate, plate};
        canvas.rect(at, fade(kPlateBack));
        canvas.region(sheet.picture, at, sheet.from, fade(0xFFFFFFFFu));
        canvas.outline(at, std::max(1.0f, u), fade(kPlateEdge));
    }
    if (art > 0.0f) {
        canvas.region(sheet.picture, {box.right() - pad * 0.5f - art, box.y + artTop, art, art},
                      sheet.from, fade(0xFFFFFFFFu));
    }
    float headPen = pen + (std::max(plate, titleTall + baseTall) - titleTall - baseTall) * 0.5f;
    for (const std::string& words : title) {
        const float baseline = middle(nameFace, headPen, titleLine, nameSize);
        if (gothic) {
            const bgfx::TextureHandle texture = panel::titleTexture();
            canvas.lettered(*gothic, texture, box.x + headTextX + drop, baseline + drop, nameSize,
                            nameTrack, kDrop, words);
            canvas.lettered(*gothic, texture, box.x + headTextX, baseline, nameSize, nameTrack,
                            fade(nameColour), words);
        } else {
            printed(canvas, box.x + headTextX, baseline, nameSize, fade(nameColour), words, drop);
        }
        headPen += titleLine;
    }
    if (!sheet.base.empty()) {
        // The item card's type line is the name's own tone, as Diablo's "Legendary Bow" is the
        // legendary's: it is where "Excellent" stands.
        if (item) {
            printed(canvas, box.x + headTextX, middle(face, headPen, baseTall, baseSize), baseSize,
                    fade(nameColour), sheet.base, drop);
        } else {
            tracked(canvas, box.x + headTextX, middle(face, headPen, baseTall, baseSize), baseSize,
                    kBaseTrack, fade(kQuiet), sheet.base, drop);
        }
    }
    pen = box.y + headTall;

    // ---- the headline -----------------------------------------------------------------------
    if (heroTall > 0.0f) {
        const float left = box.x + pad;
        float at = left;
        const float baseline = middle(face, pen, heroLine, heroSize);
        at += heavy(canvas, at, baseline, heroSize, fade(colourOf(hero.tone)), hero.value, drop, u);
        at += heroSize * 0.35f;
        at += printed(canvas, at, baseline, heroWordSize, fade(kLabel), hero.word, drop);
        if (!hero.delta.empty()) {
            at += heroSize * 0.45f;
            printed(canvas, at, baseline, footSize,
                    fade(hero.deltaWay > 0 ? kGood : hero.deltaWay < 0 ? kBad : kQuiet), hero.delta,
                    drop);
        }
        float railPen = pen + heroLine;
        if (!hero.rail.empty()) {
            canvas.rect({left + 3.0f * u, railPen + railLine * 0.15f, line,
                         railLine * float(hero.rail.size()) - railLine * 0.3f},
                        fade(kRailInk));
        }
        for (const Row& row : hero.rail) {
            float rx = left + kRailIndent * u;
            const float rb = middle(face, railPen, railLine, railSize);
            if (!row.values.empty()) {
                const Value& v = row.values[0];
                rx += heavy(canvas, rx, rb, railSize,
                            fade(v.tone == Tone::White ? kLabel : colourOf(v.tone)), v.text, drop, u);
                rx += railSize * 0.35f;
            }
            printed(canvas, rx, rb, railSize, fade(kQuiet), row.label, drop);
            railPen += railLine;
        }
    }
    pen = box.y + bodyTop;

    // ---- the sections -----------------------------------------------------------------------
    for (size_t s = 0; s < sheet.sections.size(); ++s) {
        const Section& section = sheet.sections[s];
        canvas.rect({box.x, pen, box.w, std::max(1.0f, u)}, fade(kHair));
        float rowPen = pen + railPad;
        const float left = box.x + textX + (section.framed ? pad * 0.5f : 0.0f);
        const float right = box.right() - pad - (section.framed ? pad * 0.5f : 0.0f);
        if (section.framed) {
            canvas.rect({box.x + textX - pad * 0.5f, pen + railPad * 0.5f,
                         textWide + pad, sectionTall[s] - railPad}, fade(kFramed));
            canvas.outline({box.x + textX - pad * 0.5f, pen + railPad * 0.5f, textWide + pad,
                            sectionTall[s] - railPad}, std::max(1.0f, u), fade(kFrame));
            rowPen += railPad * 0.5f;
        }
        const bool kicked = !item && !section.kicker.empty();
        if (!item && section.mark != Mark::None) {
            const float markTall = kicked ? std::round(kickerSize * 1.75f) : rowTall;
            glyphAt(canvas, section.mark, box.x + pad + kMarkColumn * u * 0.5f,
                 rowPen + markTall * 0.5f, 11.0f * u, fade(kQuiet));
        }
        if (kicked) {
            const float kickerTall = std::round(kickerSize * 1.75f);
            tracked(canvas, left, middle(face, rowPen, kickerTall, kickerSize), kickerSize,
                    kKickerTrack, fade(kQuiet), section.kicker, drop);
            rowPen += kickerTall;
        }
        for (size_t r = 0; r < section.rows.size(); ++r) {
            const Row& row = section.rows[r];
            if (row.mark != Mark::None) {
                glyphAt(canvas, row.mark, box.x + pad + kMarkColumn * u * 0.5f,
                        rowPen + rowTall * 0.5f, kMarkSize * u, fade(colourOf(row.markTone)));
            }
            if (prosaic(row)) {
                // Coloured by what part of the row each piece is: the keyword white, the prose in
                // its tone, the value brighter and heavier. A line break may fall in any of them.
                const std::string full = composed(row);
                const size_t keyEnd = row.keyword.size();
                const size_t tailAt = full.size() - row.tail.size();
                const uint32_t tone = colourOf(row.freeTone);
                for (const Span& span : prose[s][r]) {
                    const float baseline = middle(face, rowPen, rowTall, rowSize);
                    float px = left;
                    size_t at = span.at;
                    const size_t end = span.at + span.length;
                    while (at < end) {
                        const bool key = at < keyEnd;
                        const bool value = !key && at >= tailAt;
                        const size_t stop = key ? std::min(end, keyEnd)
                                            : value ? end
                                                    : std::min(end, tailAt);
                        const std::string piece = full.substr(at, stop - at);
                        px += value ? heavy(canvas, px, baseline, rowSize, fade(brighter(tone)),
                                            piece, drop, u)
                                    : printed(canvas, px, baseline, rowSize,
                                              fade(key ? colourOf(Tone::White) : tone), piece,
                                              drop);
                        at = stop;
                    }
                    rowPen += rowTall;
                }
                continue;
            }
            // The label once, at the top of its values: MU repeats it, and that is the stutter
            // this layout is here to fix.
            if (!row.label.empty()) {
                printed(canvas, left, middle(face, rowPen, rowTall, rowSize), rowSize, fade(kLabel),
                        row.label, drop);
            }
            float chipPen = right;
            for (size_t v = row.values.size(); v-- > 0;) {
                const Value& value = row.values[v];
                const float baseline = middle(face, rowPen + rowTall * float(value.chip ? 0 : v),
                                             rowTall, rowSize);
                if (value.chip) {
                    // Chips sit side by side on one row, filled from the right.
                    const float w = trackedWidth(face, chipSize, kChipTrack, value.text) +
                                    pad * 0.8f;
                    const float h = std::round(chipSize * 1.9f);
                    const gfx::Box at{chipPen - w, rowPen + (rowTall - h) * 0.5f, w, h};
                    canvas.outline(at, std::max(1.0f, u), fade(colourOf(value.tone)));
                    tracked(canvas, at.x + pad * 0.4f, middle(face, at.y, h, chipSize), chipSize,
                            kChipTrack, fade(colourOf(value.tone)), value.text, drop);
                    chipPen -= w + pad * 0.4f;
                    continue;
                }
                float end = right;
                if (!value.delta.empty()) {
                    const float dw = face.measure(footSize, value.delta);
                    printed(canvas, end - dw, baseline, footSize,
                                fade(value.deltaWay > 0   ? kGood
                                 : value.deltaWay < 0 ? kBad
                                                      : kQuiet),
                            value.delta, drop);
                    end -= dw + pad * 0.4f;
                }
                const float w = face.measure(rowSize, value.text);
                printed(canvas, end - w, baseline, rowSize, fade(colourOf(value.tone)), value.text, drop);
            }
            rowPen += rowTall * float(std::max<size_t>(1, row.values.size()));
        }
        pen += sectionTall[s];
    }

    // ---- the foot ---------------------------------------------------------------------------
    if (hasFoot) {
        // The foot carries the card's own bottom corners.
        const float bottoms[4] = {0.0f, 0.0f, radius, radius};
        roundedFan(canvas, {box.x, pen, box.w, footTall}, bottoms, fade(kFootBack));
        canvas.rect({box.x, pen, box.w, std::max(1.0f, u)}, fade(kHair));
        const float barWide = 46.0f * u, barTall = std::max(2.0f, 3.0f * u);
        if (item) {
            const auto ink = [&](const FootLine& l) {
                return fade(l.quiet ? kFoot : colourOf(l.tone));
            };
            float at = pen + railPad;
            for (const FootLine& l : sheet.who) {
                printed(canvas, box.x + pad, middle(face, at, footLine, footSize), footSize, ink(l),
                        l.text, drop);
                at += footLine;
            }
            at = pen + railPad;
            const float edge = box.right() - pad;
            for (size_t i = 0; i < right.size(); ++i) {
                const FootLine& l = right[i];
                const float baseline = middle(face, at, footLine, footSize);
                const float w = face.measure(footSize, l.text);
                // The wear's bar stands to the left of its words, in its band's colour.
                if (i == 0 && !sheet.wear.empty()) {
                    printed(canvas, edge - w, baseline, footSize, fade(kFoot), l.text, drop);
                    const gfx::Box bar{edge - w - pad * 0.5f - barWide,
                                       baseline - footSize * 0.35f, barWide, barTall};
                    canvas.rect(bar, fade(kBarBack));
                    canvas.rect({bar.x, bar.y, barWide * std::clamp(sheet.worn, 0.0f, 1.0f), barTall},
                                fade(sheet.wearTone == Tone::White ? panel::kLettering
                                                                   : colourOf(sheet.wearTone)));
                } else {
                    printed(canvas, edge - w, baseline, footSize, ink(l), l.text, drop);
                }
                at += footLine;
            }
            return;
        }
        const float baseline = middle(face, pen, footTall, footSize);
        // The left, walked with a pen: the wear and its bar first where there is one, and the
        // note after whatever went before it. Nothing in the game carries both today -- only
        // ammunition wears and only an orb is noted -- but a foot that laid one over the other
        // the day something did would be a bug nobody went looking for.
        float left = box.x + pad;
        if (!sheet.wear.empty()) {
            const float w = printed(canvas, left, baseline, footSize, fade(kFoot), sheet.wear, drop);
            const gfx::Box bar{left + w + pad * 0.5f, baseline - footSize * 0.35f, barWide, barTall};
            canvas.rect(bar, fade(kBarBack));
            canvas.rect({bar.x, bar.y, barWide * std::clamp(sheet.worn, 0.0f, 1.0f), barTall},
                        fade(sheet.wearTone == Tone::White ? panel::kLettering
                                                           : colourOf(sheet.wearTone)));
            left = bar.right() + pad;
        }
        if (!sheet.note.empty()) {
            printed(canvas, left, baseline, footSize, fade(colourOf(sheet.noteTone)), sheet.note,
                    drop);
        }
        if (!sheet.price.empty()) {
            const float w = face.measure(footSize, sheet.price);
            printed(canvas, box.right() - pad - w, baseline, footSize,
                    fade(colourOf(sheet.priceTone)), sheet.price, drop);
        }
    }
}

}  // namespace mu::game::tip
