#include "game/ui/tip.h"

#include <algorithm>
#include <cmath>

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
constexpr int kShadowColumns = 40, kShadowRows = 34;

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

float unit() { return panel::scale() * 0.5f; }

namespace {

// The words of `text` broken to `wide`, at least one word a line.
std::vector<std::string> wrapped(const gfx::Face& face, float size, const std::string& text,
                                 float wide) {
    std::vector<std::string> out;
    std::string line;
    size_t at = 0;
    while (at <= text.size()) {
        const size_t space = text.find(' ', at);
        const std::string word = text.substr(at, space - at);
        const std::string tried = line.empty() ? word : line + " " + word;
        if (!line.empty() && face.measure(size, tried) > wide) {
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
            const float xy[8] = {cx, cy - h, cx + h, cy, cx, cy + h, cx - h, cy};
            canvas.polygon(nullptr, xy, nullptr, 4, colour);
            if (which == Mark::Socket) {  // hollow: a socket is a hole
                const float i = h * 0.45f;
                const float in[8] = {cx, cy - i, cx + i, cy, cx, cy + i, cx - i, cy};
                canvas.polygon(nullptr, in, nullptr, 4, kBody);
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
void shadowUnder(gfx::Canvas& canvas, const gfx::Box& box, float u) {
    {
        const float reach = kFalls[2].sigma * 3.0f * u + kFalls[2].drop * u;
        const auto edge = [](float at, float low, float high, float sigma) {
            const float k = 1.0f / (sigma * 1.41421356f);
            return 0.5f * (std::erf((at - low) * k) - std::erf((at - high) * k));
        };
        // Cut out from under the card. The card is glass, so a shadow laid down across its whole
        // footprint is what shows THROUGH it -- the world behind never gets a look in, and the
        // card reads as solid however thin its background is. `covered` is the card's own shape
        // with a pixel of softness, and the shadow is what is left outside it.
        const auto alphaAt = [&](float px, float py) {
            float sum = 0.0f;
            for (const Fall& f : kFalls) {
                sum += f.alpha * edge(px, box.x, box.right(), f.sigma * u) *
                       edge(py, box.y + f.drop * u, box.bottom() + f.drop * u, f.sigma * u);
            }
            const float covered = edge(px, box.x, box.right(), u) *
                                  edge(py, box.y, box.bottom(), u);
            return std::min(0.85f, sum) * (1.0f - covered);
        };
        const float x0 = box.x - reach, x1 = box.right() + reach;
        const float y0 = box.y - reach, y1 = box.bottom() + reach * 1.4f;
        const float stepX = (x1 - x0) / kShadowColumns, stepY = (y1 - y0) / kShadowRows;
        for (int j = 0; j < kShadowRows; ++j) {
            for (int i = 0; i < kShadowColumns; ++i) {
                const float px = x0 + stepX * float(i), py = y0 + stepY * float(j);
                const uint32_t tl = gfx::rgba(0, 0, 0, alphaAt(px, py));
                const uint32_t tr = gfx::rgba(0, 0, 0, alphaAt(px + stepX, py));
                const uint32_t br = gfx::rgba(0, 0, 0, alphaAt(px + stepX, py + stepY));
                const uint32_t bl = gfx::rgba(0, 0, 0, alphaAt(px, py + stepY));
                canvas.shade({px, py, stepX, stepY}, tl, tr, br, bl);
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

void glass(gfx::Canvas& canvas, const gfx::Box& box, float u, float radius, uint32_t top,
           uint32_t foot) {
    shadowUnder(canvas, box, u);
    // The ring first and a hair wider, then the body over it: two fans, and the ring is left
    // showing as the edge. Drawn rounded, and see-through enough that the world moves behind it.
    const float r = radius * u;
    const float line = std::max(1.0f, u);
    const float all[4] = {r, r, r, r};
    const float wider[4] = {r + line, r + line, r + line, r + line};
    roundedFan(canvas, box.grown(line), wider, fade(kRing));
    roundedFan(canvas, box, all, top != 0u ? top : kBodyTop, foot != 0u ? foot : kBodyFoot);
}

void draw(gfx::Canvas& canvas, const Sheet& sheet, float x, float y, float screenWidth,
          float screenHeight) {
    if (sheet.empty()) return;
    const gfx::Face& face = canvas.face();
    const float u = unit();
    const float wide = (sheet.wide > 0.0f ? sheet.wide : kWide) * u, pad = kPad * u;
    const float nameSize = kNameSize * u, baseSize = kBaseSize * u, rowSize = kRowSize * u;
    const float kickerSize = kKickerSize * u, footSize = kFootSize * u, chipSize = kChipSize * u;
    const float rowTall = std::round(rowSize * kRowTall);
    const float railPad = kRailPad * u;
    // The mark column is reserved only when something is standing in it. An item card always has
    // a blade or a shield beside its first section, so the gutter is the alignment; a card with no
    // marks at all -- a skill's -- was indenting every row past an empty 25 units while its name
    // sat at the pad, which is the ragged left edge the user saw.
    bool marked = false;
    for (const Section& section : sheet.sections) marked |= section.mark != Mark::None;
    const float textX = marked ? pad + kMarkColumn * u + kMarkGap * u : pad;
    const float drop = std::max(1.0f, u);
    const float textWide = wide - textX - pad;

    // ---- measure ----------------------------------------------------------------------------
    const float plate = sheet.picture.valid() ? kPlate * u : 0.0f;
    const float headTextX = pad + (plate > 0.0f ? plate + kPad * u : 0.0f);
    const std::vector<std::string> title =
        wrapped(face, nameSize, sheet.name, wide - headTextX - pad);
    const float titleTall = float(title.size()) * std::round(nameSize * 1.25f);
    const float baseTall = sheet.base.empty() ? 0.0f : std::round(baseSize * 1.5f);
    const float headTall = std::max(plate, titleTall + baseTall) + pad * 2.0f;

    // Free rows wrap, so each section is measured by its rows rather than counted.
    std::vector<std::vector<std::vector<std::string>>> prose(sheet.sections.size());
    std::vector<float> sectionTall(sheet.sections.size(), 0.0f);
    for (size_t s = 0; s < sheet.sections.size(); ++s) {
        const Section& section = sheet.sections[s];
        float tall = railPad * 2.0f;
        if (!section.kicker.empty()) tall += std::round(kickerSize * 1.75f);
        prose[s].resize(section.rows.size());
        for (size_t r = 0; r < section.rows.size(); ++r) {
            const Row& row = section.rows[r];
            if (!row.free.empty()) {
                prose[s][r] = wrapped(face, rowSize, row.free, textWide - (section.framed ? pad : 0.0f));
                tall += float(prose[s][r].size()) * rowTall;
            } else {
                tall += std::max<size_t>(1, row.values.size()) * rowTall;
            }
        }
        if (section.framed) tall += railPad;
        sectionTall[s] = tall;
    }
    const bool hasFoot = !sheet.wear.empty() || !sheet.price.empty() || !sheet.note.empty();
    const float footTall = hasFoot ? std::round(footSize * 1.4f) + railPad * 2.0f : 0.0f;
    // A card with no foot ends on its last row with one rail's padding under it, which is less
    // air than the head carries over its name and reads as the text falling out of the bottom.
    // The difference is made up here rather than in the section, so an item card -- which always
    // has a foot -- is untouched.
    float tall = headTall + footTall + (hasFoot ? 0.0f : pad - railPad);
    for (float t : sectionTall) tall += t;

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
    }

    float pen = box.y + pad;
    if (plate > 0.0f) {
        const gfx::Box at{box.x + pad, pen, plate, plate};
        canvas.rect(at, fade(kPlateBack));
        canvas.region(sheet.picture, at, sheet.from, fade(0xFFFFFFFFu));
        canvas.outline(at, std::max(1.0f, u), fade(kPlateEdge));
    }
    float headPen = pen + (std::max(plate, titleTall + baseTall) - titleTall - baseTall) * 0.5f;
    const float titleLine = std::round(nameSize * 1.25f);
    for (const std::string& line : title) {
        printed(canvas, box.x + headTextX, middle(face, headPen, titleLine, nameSize), nameSize,
                fade(nameColour), line, drop);
        headPen += titleLine;
    }
    if (!sheet.base.empty()) {
        tracked(canvas, box.x + headTextX, middle(face, headPen, baseTall, baseSize), baseSize,
                kBaseTrack, fade(kQuiet), sheet.base, drop);
    }
    pen = box.y + headTall;

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
        if (section.mark != Mark::None) {
            const float markTall = section.kicker.empty() ? rowTall
                                                          : std::round(kickerSize * 1.75f);
            glyphAt(canvas, section.mark, box.x + pad + kMarkColumn * u * 0.5f,
                 rowPen + markTall * 0.5f, 11.0f * u, fade(kQuiet));
        }
        if (!section.kicker.empty()) {
            const float kickerTall = std::round(kickerSize * 1.75f);
            tracked(canvas, left, middle(face, rowPen, kickerTall, kickerSize), kickerSize,
                    kKickerTrack, fade(kQuiet), section.kicker, drop);
            rowPen += kickerTall;
        }
        for (size_t r = 0; r < section.rows.size(); ++r) {
            const Row& row = section.rows[r];
            if (!row.free.empty()) {
                for (const std::string& line : prose[s][r]) {
                    printed(canvas, left, middle(face, rowPen, rowTall, rowSize), rowSize,
                            fade(colourOf(row.freeTone)), line, drop);
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
        const float baseline = middle(face, pen, footTall, footSize);
        // The left, walked with a pen: the wear and its bar first where there is one, and the
        // note after whatever went before it. Nothing in the game carries both today -- only
        // ammunition wears and only an orb is noted -- but a foot that laid one over the other
        // the day something did would be a bug nobody went looking for.
        float left = box.x + pad;
        if (!sheet.wear.empty()) {
            const float w = printed(canvas, left, baseline, footSize, fade(kFoot), sheet.wear, drop);
            const float barWide = 46.0f * u, barTall = std::max(2.0f, 3.0f * u);
            const gfx::Box bar{left + w + pad * 0.5f, baseline - footSize * 0.35f, barWide, barTall};
            canvas.rect(bar, fade(kBarBack));
            canvas.rect({bar.x, bar.y, barWide * std::clamp(sheet.worn, 0.0f, 1.0f), barTall},
                        fade(panel::kLettering));
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
