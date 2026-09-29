#include "game/ui/controls.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <vector>

#include "core/log.h"
#include "game/ui/panel.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"

namespace mu::game::controls {
namespace {

using gfx::Box;

// ---- the faces -------------------------------------------------------------------------------

constexpr const char* kWordFacePath = MU2_ROOT_DIR "/extern/AlegreyaSansSC-Bold.ttf";
constexpr const char* kLabelFacePath = MU2_ROOT_DIR "/extern/AlegreyaSans-Medium.ttf";
// Baked once, larger than any control draws them, and minified through the mips: a word is 13
// to 18 units, which is 22 to 30 pixels on this Mac's 1800-line backbuffer.
constexpr float kBake = 48.0f;

gfx::Face s_word, s_label;
bgfx::TextureHandle s_wordTexture = BGFX_INVALID_HANDLE;
bgfx::TextureHandle s_labelTexture = BGFX_INVALID_HANDLE;

bool bakeFace(gfx::Face& face, bgfx::TextureHandle& texture, const char* path, const char* name) {
    if (!face.bake(path, kBake, 512, 4, 1, 0)) return false;
    texture = gfx::uploadFace(face, name);
    face.dropPixels();
    return bgfx::isValid(texture);
}

bool wordReady() { return s_word.ready() && bgfx::isValid(s_wordTexture); }
bool labelReady() { return s_label.ready() && bgfx::isValid(s_labelTexture); }

// ---- the stone -------------------------------------------------------------------------------
//
// Three tiles of 128 texels in one 384-wide texture: stone, iron, well. Each is three noises
// summed -- a one-texel grain, small flecks, and for the stone a barely-there unevenness -- all
// periodic in 128 so a tile meets its neighbour without a seam. A texel is black where the sum
// is below nought and warm white where it is above, and its alpha is how far: so the texture
// darkens and lifts whatever fill is under it and has no colour of its own.

constexpr int kTile = 128;
enum Stone : int { kStone = 0, kIron = 1, kWell = 2 };

bgfx::TextureHandle s_stone = BGFX_INVALID_HANDLE;
gfx::Art s_stoneArt;

uint32_t hash(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = x * 0x8da6b343u ^ y * 0xd8163841u ^ seed * 0xcb1ab31fu;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}
float white(int x, int y, uint32_t seed) {
    const uint32_t m = uint32_t(kTile - 1);
    return float(hash(uint32_t(x) & m, uint32_t(y) & m, seed) & 0xFFFFu) / 32767.5f - 1.0f;
}
// Value noise on a lattice of `cell` texels, smoothly interpolated, wrapping at the tile.
float value(float x, float y, int cell, uint32_t seed) {
    const int n = kTile / cell;
    const float fx = x / float(cell), fy = y / float(cell);
    const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
    const float tx = fx - float(x0), ty = fy - float(y0);
    const auto at = [&](int i, int j) {
        return float(hash(uint32_t(((i % n) + n) % n), uint32_t(((j % n) + n) % n), seed) & 0xFFFFu) /
                   32767.5f - 1.0f;
    };
    const float sx = tx * tx * (3.0f - 2.0f * tx), sy = ty * ty * (3.0f - 2.0f * ty);
    const float a = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * sx;
    const float b = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * sx;
    return a + (b - a) * sy;
}

struct Recipe {
    float grain, fleck, mottle;  // how much of each noise
    float dark, light;           // the alpha of a full step down and a full step up
    uint32_t seed;
};
// Lowered a quarter from the design page's first cut: the user asked for minimal.
constexpr Recipe kRecipes[3] = {
    {0.55f, 0.35f, 0.25f, 0.24f, 0.048f, 11u},  // stone
    {0.75f, 0.25f, 0.00f, 0.18f, 0.050f, 23u},  // iron
    {0.70f, 0.30f, 0.00f, 0.30f, 0.024f, 37u},  // well
};

bool makeStone() {
    const int width = kTile * 3;
    std::vector<uint8_t> rgba(size_t(width) * kTile * 4);
    std::vector<float> sum(size_t(kTile) * kTile);
    for (int t = 0; t < 3; ++t) {
        const Recipe& r = kRecipes[t];
        double square = 0.0;
        for (int y = 0; y < kTile; ++y) {
            for (int x = 0; x < kTile; ++x) {
                // The grain is white noise softened by its four neighbours, so it is a texel and
                // not a sparkle.
                const float g = 0.6f * white(x, y, r.seed) +
                                0.1f * (white(x - 1, y, r.seed) + white(x + 1, y, r.seed) +
                                        white(x, y - 1, r.seed) + white(x, y + 1, r.seed));
                const float f = value(float(x), float(y), 4, r.seed + 1u);
                const float m = value(float(x), float(y), 32, r.seed + 2u);
                const float v = r.grain * g + r.fleck * f + r.mottle * m;
                sum[size_t(y) * kTile + x] = v;
                square += double(v) * v;
            }
        }
        const float sd = float(std::sqrt(square / double(kTile * kTile)));
        for (int y = 0; y < kTile; ++y) {
            for (int x = 0; x < kTile; ++x) {
                const float n = std::clamp(sum[size_t(y) * kTile + x] / (sd * 3.0f), -1.0f, 1.0f);
                uint8_t* p = &rgba[(size_t(y) * width + size_t(t * kTile + x)) * 4];
                const bool lift = n > 0.0f;
                p[0] = lift ? 255 : 0;
                p[1] = lift ? 240 : 0;
                p[2] = lift ? 222 : 0;
                p[3] = uint8_t(std::clamp((lift ? n * r.light : -n * r.dark) * 255.0f, 0.0f, 255.0f));
            }
        }
    }
    s_stone = bgfx::createTexture2D(uint16_t(width), uint16_t(kTile), false, 1,
                                    bgfx::TextureFormat::RGBA8,
                                    BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP,
                                    bgfx::copy(rgba.data(), uint32_t(rgba.size())));
    if (!bgfx::isValid(s_stone)) return false;
    bgfx::setName(s_stone, "interface stone");
    s_stoneArt = {s_stone, float(width), float(kTile)};
    return true;
}

// Lays one of the three tiles over `box`, a texel to a pixel, cut at the box's far edges.
void stone(gfx::Canvas& canvas, const Box& box, Stone which) {
    if (!s_stoneArt.valid()) return;
    const float x0 = std::round(box.x), y0 = std::round(box.y);
    const float x1 = std::round(box.right()), y1 = std::round(box.bottom());
    for (float y = y0; y < y1; y += kTile) {
        const float h = std::min(float(kTile), y1 - y);
        for (float x = x0; x < x1; x += kTile) {
            const float w = std::min(float(kTile), x1 - x);
            canvas.region(s_stoneArt, {x, y, w, h}, {float(which * kTile), 0.0f, w, h});
        }
    }
}

// ---- colour as four floats, so two looks mix by the hover --------------------------------------

struct Tone {
    float r, g, b, a;
    Tone mix(const Tone& o, float t) const {
        return {r + (o.r - r) * t, g + (o.g - g) * t, b + (o.b - b) * t, a + (o.a - a) * t};
    }
    Tone alpha(float k) const { return {r, g, b, a * k}; }
    uint32_t packed() const { return gfx::rgba(r, g, b, a); }
};
constexpr Tone kBloodTone{0.761f, 0.212f, 0.169f, 1.0f};

// A button's look: its rim top to foot, its body top to foot, its word, the ember rising from
// its foot, the glow round it, and the breath of light along its top inside the rim.
struct Look {
    Tone rimTop, rimFoot, top, foot, ink;
    float ember, glow, highlight;
    Tone lightTone;
};
Look mixed(const Look& a, const Look& b, float t) {
    return {a.rimTop.mix(b.rimTop, t), a.rimFoot.mix(b.rimFoot, t), a.top.mix(b.top, t),
            a.foot.mix(b.foot, t),     a.ink.mix(b.ink, t),         a.ember + (b.ember - a.ember) * t,
            a.glow + (b.glow - a.glow) * t, a.highlight + (b.highlight - a.highlight) * t,
            a.lightTone.mix(b.lightTone, t)};
}

constexpr Tone kWarmLight{1.0f, 0.922f, 0.824f, 1.0f};
constexpr Tone kRedLight{1.0f, 0.706f, 0.627f, 1.0f};

// The design page's numbers, one look a state.
constexpr Look kSecondaryRest{{0.420f, 0.337f, 0.271f, 1}, {0.169f, 0.133f, 0.106f, 1},
                              {0.141f, 0.106f, 0.086f, 1}, {0.067f, 0.047f, 0.039f, 1},
                              {0.788f, 0.749f, 0.682f, 1}, 0.0f, 0.0f, 0.06f, kWarmLight};
constexpr Look kSecondaryOver{{0.659f, 0.545f, 0.424f, 1}, {0.290f, 0.227f, 0.180f, 1},
                              {0.180f, 0.133f, 0.110f, 1}, {0.082f, 0.063f, 0.051f, 1},
                              {1.000f, 0.957f, 0.886f, 1}, 0.30f, 0.18f, 0.06f, kWarmLight};
constexpr Look kSecondaryHeld{{0.239f, 0.192f, 0.157f, 1}, {0.420f, 0.337f, 0.271f, 1},
                              {0.059f, 0.043f, 0.035f, 1}, {0.102f, 0.075f, 0.063f, 1},
                              {0.749f, 0.702f, 0.627f, 1}, 0.0f, 0.0f, 0.0f, kWarmLight};
constexpr Look kPrimaryRest{{0.831f, 0.286f, 0.231f, 1}, {0.369f, 0.082f, 0.055f, 1},
                            {0.478f, 0.102f, 0.075f, 1}, {0.227f, 0.043f, 0.027f, 1},
                            {0.965f, 0.906f, 0.816f, 1}, 0.0f, 0.25f, 0.25f, kRedLight};
constexpr Look kPrimaryOver{{0.941f, 0.380f, 0.314f, 1}, {0.478f, 0.110f, 0.075f, 1},
                            {0.580f, 0.137f, 0.102f, 1}, {0.290f, 0.059f, 0.039f, 1},
                            {1.000f, 1.000f, 1.000f, 1}, 0.0f, 0.55f, 0.30f, kRedLight};
constexpr Look kPrimaryHeld{{0.369f, 0.082f, 0.055f, 1}, {0.831f, 0.286f, 0.231f, 1},
                            {0.165f, 0.031f, 0.020f, 1}, {0.353f, 0.071f, 0.047f, 1},
                            {0.965f, 0.906f, 0.816f, 1}, 0.0f, 0.0f, 0.0f, kRedLight};
constexpr Look kDangerOver{{0.761f, 0.212f, 0.169f, 1}, {0.290f, 0.082f, 0.063f, 1},
                           {0.165f, 0.082f, 0.071f, 1}, {0.078f, 0.035f, 0.031f, 1},
                           {1.000f, 0.604f, 0.549f, 1}, 0.45f, 0.30f, 0.06f, kWarmLight};
constexpr Look kOff{{0.180f, 0.145f, 0.118f, 1}, {0.102f, 0.078f, 0.067f, 1},
                    {0.094f, 0.071f, 0.059f, 1}, {0.051f, 0.035f, 0.031f, 1},
                    {0.361f, 0.322f, 0.286f, 1}, 0.0f, 0.0f, 0.0f, kWarmLight};
constexpr Tone kDangerInk{0.878f, 0.478f, 0.424f, 1.0f};

Look lookOf(Kind kind, const State& s) {
    if (s.off) return kOff;
    switch (kind) {
        case Kind::Primary:
            return s.held ? kPrimaryHeld : mixed(kPrimaryRest, kPrimaryOver, s.lift);
        case Kind::Danger: {
            if (s.held) {
                Look l = kSecondaryHeld;
                l.ink = kDangerInk;
                return l;
            }
            Look rest = kSecondaryRest;
            rest.ink = kDangerInk;
            return mixed(rest, kDangerOver, s.lift);
        }
        default:
            return s.held ? kSecondaryHeld : mixed(kSecondaryRest, kSecondaryOver, s.lift);
    }
}

float px(float u) { return std::max(1.0f, std::round(u)); }

// The slab every button and square is: glow, drop, rim, body, iron grain, highlight, ember.
// Returns the body it drew, which a held control has sunk by a pixel.
Box slabOf(gfx::Canvas& canvas, const Box& box, const Look& look, bool held, bool off, float u) {
    const float line = px(u);
    const float r = style::kRadiusControl * u;
    const Box body = held ? Box{box.x, box.y + line, box.w, box.h} : box;
    if (look.glow > 0.0f) {
        for (const float spread : {8.0f, 4.0f}) {
            const float g = spread * u;
            tip::panel(canvas, body.grown(line + g), r + line + g,
                       kBloodTone.alpha(look.glow * 0.10f).packed(),
                       kBloodTone.alpha(look.glow * 0.16f).packed());
        }
    }
    if (!held && !off) tip::shadowUnder(canvas, body, u * 0.45f, r + line);
    tip::panel(canvas, body.grown(line), r + line, look.rimTop.packed(), look.rimFoot.packed());
    tip::panel(canvas, body, r, look.top.packed(), look.foot.packed());
    stone(canvas, body.grown(-line), kIron);
    if (held) {
        // Pressed into its bed: a shadow cast down from the top edge inside the rim.
        const float deep = std::round(body.h * 0.3f);
        canvas.shade({body.x + line, body.y, body.w - line * 2.0f, deep}, gfx::rgba(0, 0, 0, 0.55f),
                     gfx::rgba(0, 0, 0, 0.55f), gfx::rgba(0, 0, 0, 0), gfx::rgba(0, 0, 0, 0));
    }
    if (look.highlight > 0.0f) {
        canvas.rect({body.x + r, body.y, body.w - r * 2.0f, line},
                    look.lightTone.alpha(look.highlight).packed());
    }
    if (look.ember > 0.0f) {
        const float tall = std::round(body.h * 0.62f);
        const float corners[4] = {0.0f, 0.0f, r, r};
        tip::rounded(canvas, {body.x, body.bottom() - tall, body.w, tall}, corners,
                     kBloodTone.alpha(0.0f).packed(), kBloodTone.alpha(look.ember).packed());
    }
    return body;
}

// A word in the button face, centred in a box on its capitals.
void word(gfx::Canvas& canvas, const Box& box, float size, const Tone& ink,
          const std::string& text) {
    if (wordReady()) {
        const float wide = s_word.measure(size, text);
        const float x = std::round(box.midX() - wide * 0.5f);
        const float baseline = std::round(box.y + (box.h + s_word.ascent(size) * 0.66f) * 0.5f);
        canvas.lettered(s_word, s_wordTexture, x + 1.0f, baseline + 1.0f, size, 0.0f,
                        gfx::rgba(0, 0, 0, 0.6f), text);
        canvas.lettered(s_word, s_wordTexture, x, baseline, size, 0.0f, ink.packed(), text);
        return;
    }
    const gfx::Face& face = canvas.face();
    const float baseline = std::round(box.y + (box.h + face.ascent(size) * 0.72f) * 0.5f);
    canvas.text(box.x, baseline, size, ink.packed(), text, gfx::Align::Centre, box.w);
}

// A disc's ring, as quads: the canvas has no circle.
void ring(gfx::Canvas& canvas, float cx, float cy, float r, float thick, uint32_t ink) {
    constexpr int kSteps = 28;
    const float in = r - thick;
    for (int i = 0; i < kSteps; ++i) {
        const float a = 6.28318531f * float(i) / float(kSteps);
        const float b = 6.28318531f * float(i + 1) / float(kSteps);
        const float xy[8] = {cx + std::cos(a) * r,  cy + std::sin(a) * r,
                             cx + std::cos(b) * r,  cy + std::sin(b) * r,
                             cx + std::cos(b) * in, cy + std::sin(b) * in,
                             cx + std::cos(a) * in, cy + std::sin(a) * in};
        canvas.polygon(nullptr, xy, nullptr, 4, ink);
    }
}

// A bar from (x0, y0) to (x1, y1), `t` either side of the line.
void bar(gfx::Canvas& canvas, float x0, float y0, float x1, float y1, float t, uint32_t ink) {
    const float dx = x1 - x0, dy = y1 - y0;
    const float len = std::max(0.001f, std::sqrt(dx * dx + dy * dy));
    const float nx = -dy / len * t, ny = dx / len * t;
    const float xy[8] = {x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny};
    canvas.polygon(nullptr, xy, nullptr, 4, ink);
}

// The canvas has no antialiasing, so an edge that must read smooth at a glyph's size is given a
// one-pixel fringe fading to nothing: a band of per-vertex colour outside the solid shape.
uint32_t clear(uint32_t ink) { return ink & 0x00FFFFFFu; }

void fringe(gfx::Canvas& canvas, float ax, float ay, float bx, float by, float ox, float oy,
            uint32_t ink) {
    const float xy[8] = {ax, ay, bx, by, bx + ox, by + oy, ax + ox, ay + oy};
    const uint32_t c[4] = {ink, ink, clear(ink), clear(ink)};
    canvas.polygon(xy, c, 4);
}

// A convex polygon, solid, with its fringe. Points in either winding.
void featheredPolygon(gfx::Canvas& canvas, const float* xy, int count, uint32_t ink) {
    canvas.polygon(nullptr, xy, nullptr, count, ink);
    float mx = 0.0f, my = 0.0f;
    for (int i = 0; i < count; ++i) mx += xy[i * 2], my += xy[i * 2 + 1];
    mx /= float(count), my /= float(count);
    for (int i = 0; i < count; ++i) {
        const int n = (i + 1) % count;
        const float ax = xy[i * 2], ay = xy[i * 2 + 1], bx = xy[n * 2], by = xy[n * 2 + 1];
        const float dx = bx - ax, dy = by - ay;
        const float len = std::max(0.001f, std::sqrt(dx * dx + dy * dy));
        float ox = -dy / len, oy = dx / len;
        // Outward: away from the middle.
        if ((ax - mx) * ox + (ay - my) * oy < 0.0f) ox = -ox, oy = -oy;
        fringe(canvas, ax, ay, bx, by, ox, oy, ink);
    }
}

// An arc `w` either side of radius `r` from angle `a0` to `a1`, fringed on both rims and at
// both ends.
void featheredArc(gfx::Canvas& canvas, float cx, float cy, float r, float w, float a0, float a1,
                  uint32_t ink) {
    const int steps = std::max(12, int(std::abs(a1 - a0) * r * 0.5f));
    const float in = r - w, out = r + w;
    for (int i = 0; i < steps; ++i) {
        const float a = a0 + (a1 - a0) * float(i) / float(steps);
        const float b = a0 + (a1 - a0) * float(i + 1) / float(steps);
        const float ca = std::cos(a), sa = std::sin(a), cb = std::cos(b), sb = std::sin(b);
        const float core[8] = {cx + ca * out, cy + sa * out, cx + cb * out, cy + sb * out,
                               cx + cb * in,  cy + sb * in,  cx + ca * in,  cy + sa * in};
        canvas.polygon(nullptr, core, nullptr, 4, ink);
        fringe(canvas, cx + ca * out, cy + sa * out, cx + cb * out, cy + sb * out, ca, sa, ink);
        fringe(canvas, cx + ca * in, cy + sa * in, cx + cb * in, cy + sb * in, -ca, -sa, ink);
    }
    // The two ends, fringed along the way the arc would have gone on.
    const float ends[2] = {a0, a1};
    for (int e = 0; e < 2; ++e) {
        const float c = std::cos(ends[e]), sn = std::sin(ends[e]);
        const float d = e == 0 ? -1.0f : 1.0f;
        fringe(canvas, cx + c * out, cy + sn * out, cx + c * in, cy + sn * in, -sn * d, c * d, ink);
    }
}

void glyph(gfx::Canvas& canvas, const Box& box, Glyph which, uint32_t ink, float u) {
    const float cx = box.midX(), cy = box.midY();
    const float s = std::min(box.w, box.h);
    const float t = std::max(0.75f, 0.8f * u);
    switch (which) {
        case Glyph::Close: {
            const float a = s * 0.17f;
            bar(canvas, cx - a, cy - a, cx + a, cy + a, t, ink);
            bar(canvas, cx + a, cy - a, cx - a, cy + a, t, ink);
            break;
        }
        case Glyph::Plus: {
            const float a = s * 0.2f, w = std::max(1.0f, 1.0f * u);
            canvas.rect({cx - a, cy - w, a * 2.0f, w * 2.0f}, ink);
            canvas.rect({cx - w, cy - a, w * 2.0f, a * 2.0f}, ink);
            break;
        }
        case Glyph::Left:
        case Glyph::Right: {
            const float k = which == Glyph::Right ? 1.0f : -1.0f;
            const float a = s * 0.13f, h = s * 0.22f;
            bar(canvas, cx - a * k, cy - h, cx + a * k, cy, t, ink);
            bar(canvas, cx + a * k, cy, cx - a * k, cy + h, t, ink);
            break;
        }
        case Glyph::CoinIn:
        case Glyph::CoinOut: {
            const float r = s * 0.2f, oy = s * 0.07f;
            ring(canvas, cx, cy + oy, r, std::max(1.0f, 1.3f * u), ink);
            canvas.rect({cx - r * 0.35f, cy + oy - r * 0.35f, r * 0.7f, r * 0.7f}, ink);
            const float top = cy + oy - r - s * 0.2f, low = cy + oy - r - s * 0.03f;
            bar(canvas, cx, top, cx, low, t, ink);
            const float tip = which == Glyph::CoinIn ? low : top;
            const float d = which == Glyph::CoinIn ? -1.0f : 1.0f;
            bar(canvas, cx, tip, cx - s * 0.07f, tip + d * s * 0.07f, t, ink);
            bar(canvas, cx, tip, cx + s * 0.07f, tip + d * s * 0.07f, t, ink);
            break;
        }
        case Glyph::Hammer:
        case Glyph::Hammers: {
            // A head across the top left and a haft down to the right; Repair All is two, the
            // second a step behind the first.
            const auto hammer = [&](float ox, float oy, uint32_t c) {
                bar(canvas, ox - s * 0.2f, oy - s * 0.02f, ox - s * 0.02f, oy - s * 0.2f, s * 0.07f, c);
                bar(canvas, ox - s * 0.08f, oy - s * 0.08f, ox + s * 0.2f, oy + s * 0.2f, t * 1.2f, c);
            };
            if (which == Glyph::Hammers) {
                hammer(cx + s * 0.1f, cy - s * 0.02f, (ink & 0x00FFFFFFu) | (uint32_t((ink >> 24) * 0.55f) << 24));
                hammer(cx - s * 0.06f, cy + s * 0.04f, ink);
            } else {
                hammer(cx, cy, ink);
            }
            break;
        }
        case Glyph::Undo: {
            // An arrow turning back on itself: most of a ring open at the left, and a solid head
            // at its upper end pointing down into the gap. Feathered, both of it, because the
            // canvas does not antialias and a thin arc of bars read as stairs.
            const float r = s * 0.16f, w = std::max(1.0f, 1.1f * u);
            const float from = -2.0f, to = 2.55f;  // radians, clockwise from the head
            featheredArc(canvas, cx, cy, r, w, from, to, ink);
            const float hx = cx + std::cos(from) * r, hy = cy + std::sin(from) * r;
            // Along the travel at the head (down the ring, anticlockwise) and across it.
            const float tx = std::sin(from), ty = -std::cos(from);
            const float nx = std::cos(from), ny = std::sin(from);
            const float len = s * 0.13f, half = s * 0.1f;
            const float xy[6] = {hx + tx * len, hy + ty * len,
                                 hx - tx * len * 0.2f + nx * half, hy - ty * len * 0.2f + ny * half,
                                 hx - tx * len * 0.2f - nx * half, hy - ty * len * 0.2f - ny * half};
            featheredPolygon(canvas, xy, 3, ink);
            break;
        }
    }
}

}  // namespace

// ---- lifetime ------------------------------------------------------------------------------------

// Opened by each screen that draws controls -- the desk in the world, the character screen
// before it -- and the two can overlap while one hands over to the other, so the bake is
// counted: the first open makes it and the last close takes it down.
int s_opens = 0;
bool s_opened = false;

bool open() {
    if (s_opens++ > 0) return s_opened;
    bool ok = true;
    if (!bakeFace(s_word, s_wordTexture, kWordFacePath, "button words")) {
        core::logError("controls: the button face did not bake (%s); buttons keep the body face",
                       kWordFacePath);
        ok = false;
    }
    if (!bakeFace(s_label, s_labelTexture, kLabelFacePath, "control labels")) {
        core::logError("controls: the label face did not bake (%s)", kLabelFacePath);
        ok = false;
    }
    if (!makeStone()) {
        core::logError("controls: the stone texture did not make; surfaces are flat");
        ok = false;
    }
    s_opened = ok;
    return ok;
}

void close() {
    if (s_opens == 0 || --s_opens > 0) return;
    for (bgfx::TextureHandle* t : {&s_wordTexture, &s_labelTexture, &s_stone}) {
        if (bgfx::isValid(*t)) bgfx::destroy(*t);
        *t = BGFX_INVALID_HANDLE;
    }
    s_word = gfx::Face{};
    s_label = gfx::Face{};
    s_stoneArt = {};
}

// ---- type --------------------------------------------------------------------------------------

float label(gfx::Canvas& canvas, float x, float baseline, float size, uint32_t ink,
            const std::string& text) {
    x = std::round(x);
    baseline = std::round(baseline);
    if (labelReady()) {
        canvas.lettered(s_label, s_labelTexture, x + 1.0f, baseline + 1.0f, size, 0.0f,
                        gfx::rgba(0, 0, 0, 0.6f), text);
        return canvas.lettered(s_label, s_labelTexture, x, baseline, size, 0.0f, ink, text);
    }
    return canvas.shadowed(x, baseline, size, ink, style::kDrop, 1.0f, text);
}

float labelWidth(float size, const std::string& text) {
    return labelReady() ? s_label.measure(size, text) : 0.0f;
}

const gfx::Face* labelFace() { return labelReady() ? &s_label : nullptr; }
bgfx::TextureHandle labelTexture() {
    return labelReady() ? s_labelTexture : bgfx::TextureHandle BGFX_INVALID_HANDLE;
}

float ranged(gfx::Canvas& canvas, float right, float baseline, float size, uint32_t ink,
             const std::string& text) {
    const float wide = labelReady() ? s_label.measure(size, text) : canvas.face().measure(size, text);
    return label(canvas, right - wide, baseline, size, ink, text);
}

void caps(gfx::Canvas& canvas, float x, float baseline, float size, uint32_t ink,
          const std::string& text, float track) {
    if (!wordReady()) {
        canvas.text(x, baseline, size, ink, text);
        return;
    }
    canvas.lettered(s_word, s_wordTexture, std::round(x) + 1.0f, std::round(baseline) + 1.0f, size,
                    size * track, gfx::rgba(0, 0, 0, 0.6f), text);
    canvas.lettered(s_word, s_wordTexture, std::round(x), std::round(baseline), size, size * track,
                    ink, text);
}

float capsWidth(float size, const std::string& text, float track) {
    return wordReady() ? s_word.measure(size, text) + size * track * float(text.size()) : 0.0f;
}

float middle(float top, float tall, float size) {
    const float ascent = labelReady() ? s_label.ascent(size) : size * 0.8f;
    return std::round(top + (tall + ascent * 0.62f) * 0.5f);
}

void kicker(gfx::Canvas& canvas, float x, float baseline, const std::string& text, float u) {
    const float size = style::kKickerSize * u;
    if (!wordReady()) {
        canvas.text(x, baseline, size, style::kAshInk, text);
        return;
    }
    canvas.lettered(s_word, s_wordTexture, std::round(x), std::round(baseline), size,
                    size * style::kKickerTrack, style::kAshInk, text);
}

// ---- surfaces ----------------------------------------------------------------------------------

void grain(gfx::Canvas& canvas, const Box& box) { stone(canvas, box, kStone); }

void frame(gfx::Canvas& canvas, const Box& window, float u, const std::string& title,
           Box* closeBox, float headPixels) {
    const Box b{std::round(window.x), std::round(window.y), std::round(window.w),
                std::round(window.h)};
    const float line = px(u);
    const float gap = std::round(style::kFrameGap * u);
    const float r = style::kRadiusSheet * u;
    // Outside in: seam, the outer iron ring, the dark gap, a seam, the lit edge, the body.
    const float seam = line, ringOut = line;
    const float out = seam + ringOut + gap + seam + line;
    // Lighter than a card's: a window is big, and the card's full shadow round two or three of
    // them blacked out the world between -- the user, 2026-09-29, "window shadows is to dark".
    constexpr float kWindowShadow = 0.45f;
    tip::shadowUnder(canvas, b.grown(out), u, r + out, kWindowShadow);
    tip::panel(canvas, b.grown(out), r + out, style::kSeam, style::kSeam);
    tip::panel(canvas, b.grown(out - seam), r + out - seam, style::kIronLo, style::kIronLo);
    const uint32_t gapInk = gfx::rgba(0.067f, 0.051f, 0.043f);
    tip::panel(canvas, b.grown(line + seam + gap), r + line + seam + gap, gapInk, gapInk);
    tip::panel(canvas, b.grown(line + seam), r + line + seam, style::kSeam, style::kSeam);
    tip::panel(canvas, b.grown(line), r + line, gfx::rgba(0.478f, 0.388f, 0.314f),
               gfx::rgba(0.165f, 0.125f, 0.098f));
    const auto sheet = [](uint32_t c) { return (c & 0x00FFFFFFu) | (uint32_t(style::kSheetAlpha * 255.0f + 0.5f) << 24); };
    tip::panel(canvas, b, r, sheet(style::kAsh2), sheet(style::kAsh0));
    stone(canvas, b.grown(-line), kStone);

    // The head: a red wash fading down it, and a lit iron line across the middle of its top.
    const float head = std::round(headPixels > 0.0f ? headPixels : style::kHead * u);
    const float closeSize = std::round(std::min(style::kSmallSquare * u, head * 0.66f));
    const float top[4] = {r, r, 0.0f, 0.0f};
    tip::rounded(canvas, {b.x, b.y, b.w, head}, top, kBloodTone.alpha(0.09f).packed(),
                 kBloodTone.alpha(0.0f).packed());
    {
        const float from = b.x + b.w * 0.18f, wide = b.w * 0.64f, half = wide * 0.5f;
        const uint32_t hi = style::kIronHi, clear = style::kIronHi & 0x00FFFFFFu;
        canvas.shade({from, b.y, half, line * 2.0f}, clear, hi, hi, clear);
        canvas.shade({from + half, b.y, half, line * 2.0f}, hi, clear, clear, hi);
    }
    // The title, in the windows' Cinzel, centred on its capitals.
    const std::string caps = [&] {
        std::string s = title;
        for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
        return s;
    }();
    const float size = style::kTitleSize * u;
    if (const gfx::Face* tf = panel::titleFace()) {
        // Centred, in the room the close button leaves at each end. A merchant's own name is
        // the title of his window and "Lumen the Barmaid" is seventeen tracked capitals: it is
        // shrunk by up to a quarter and only then trimmed, in one pass that never grows. Trimmed
        // only when the whole of it does not fit: the trim measures with its "..", and a title
        // that just fit -- Devin's "The White Silence" -- lost its last letter to it.
        const float room = b.w - (12.0f * u + closeSize + 8.0f * u) * 2.0f;
        const auto wideAt = [&](float s, const std::string& t) {
            return tf->measure(s, t) + s * style::kTitleTrack * float(t.size() > 0 ? t.size() - 1 : 0);
        };
        float fitted = size;
        while (fitted > size * 0.62f && wideAt(fitted, caps) > room) fitted -= 0.5f;
        std::string text = caps;
        if (wideAt(fitted, caps) > room) {
            while (text.size() > 1 && wideAt(fitted, text + "..") > room) text.pop_back();
        }
        if (text.size() != caps.size()) text += "..";
        const float tracking = fitted * style::kTitleTrack;
        const float x = std::round(b.midX() - wideAt(fitted, text) * 0.5f);
        const float baseline = std::round(b.y + (head + tf->ascent(fitted) * 0.72f) * 0.5f);
        canvas.lettered(*tf, panel::titleTexture(), x + 1.0f, baseline + 1.0f, fitted, tracking,
                        style::kDrop, text);
        canvas.lettered(*tf, panel::titleTexture(), x, baseline, fitted, tracking, style::kBoneHi,
                        text);
    }
    rule(canvas, b.x + 14.0f * u, b.y + head, b.w - 28.0f * u, u);
    if (closeBox) {
        *closeBox = {std::round(b.right() - 12.0f * u - closeSize),
                     std::round(b.y + (head - closeSize) * 0.5f), closeSize, closeSize};
    }
}

void foot(gfx::Canvas& canvas, const Box& window, float top, float u) {
    const float r = style::kRadiusSheet * u;
    const Box band{std::round(window.x), std::round(top), std::round(window.w),
                   std::round(window.bottom() - top)};
    const float corners[4] = {0.0f, 0.0f, r, r};
    tip::rounded(canvas, band, corners, gfx::rgba(0, 0, 0, 0.0f), gfx::rgba(0, 0, 0, 0.38f));
    rule(canvas, window.x + 14.0f * u, top, window.w - 28.0f * u, u);
}

float keycap(gfx::Canvas& canvas, float x, float midY, const std::string& key, float u) {
    const float size = 12.0f * u;
    const float wide = labelReady() ? s_label.measure(size, key) : canvas.face().measure(size, key);
    const Box b{std::round(x), std::round(midY - 10.5f * u), std::round(std::max(24.0f * u, wide + 12.0f * u)),
                std::round(21.0f * u)};
    keycap(canvas, b, key, u);
    return b.w;
}

void keycap(gfx::Canvas& canvas, const Box& box, const std::string& key, float u) {
    const Box b{std::round(box.x), std::round(box.y), std::round(box.w), std::round(box.h)};
    const float line = px(u), r = std::min(style::kRadiusSmall * u, b.h * 0.25f);
    // A seam of black round it, the iron rim, and the key's face a pixel in -- two at its foot,
    // so it reads as a key standing up off the sheet.
    tip::panel(canvas, b.grown(line), r + line, style::kSeam, style::kSeam);
    tip::panel(canvas, b, r, style::kIron, style::kIronLo);
    const Box face{b.x + line, b.y + line, b.w - line * 2.0f, b.h - line * 3.0f};
    tip::panel(canvas, face, std::max(0.0f, r - line), style::kAsh3, style::kAsh0);
    if (key.empty()) return;
    // The letter's capitals fill most of the face: Alegreya's capital is about six tenths of its
    // size, so a size of the face's height and a third stands the capital two thirds up it.
    const float size = std::round(std::min(12.0f * u, face.h * 1.35f) * 2.0f) * 0.5f;
    const float wide = labelReady() ? s_label.measure(size, key) : canvas.face().measure(size, key);
    label(canvas, b.x + (b.w - wide) * 0.5f, middle(face.y, face.h, size), size, style::kBoneHi, key);
}

void well(gfx::Canvas& canvas, const Box& box, float u) {
    const Box b{std::round(box.x), std::round(box.y), std::round(box.w), std::round(box.h)};
    const float line = px(u), r = style::kRadiusSmall * u;
    tip::panel(canvas, b, r, style::kIronLo, style::kIronLo);
    const Box in = b.grown(-line);
    tip::panel(canvas, in, std::max(0.0f, r - line), style::kVoid, style::kVoid);
    stone(canvas, in, kWell);
    canvas.shade({in.x, in.y, in.w, std::round(5.0f * u)}, gfx::rgba(0, 0, 0, 0.5f),
                 gfx::rgba(0, 0, 0, 0.5f), gfx::rgba(0, 0, 0, 0), gfx::rgba(0, 0, 0, 0));
}

void cell(gfx::Canvas& canvas, const Box& box, Cell state, float u) {
    const Box b{std::round(box.x), std::round(box.y), std::round(box.w), std::round(box.h)};
    const float line = px(u);
    uint32_t back = 0u, edge = style::kIronDk;
    switch (state) {
        case Cell::Rest: back = style::kVoid; break;
        case Cell::Over: back = gfx::rgba(1.0f, 0.922f, 0.824f, 0.08f); edge = gfx::rgba(0.788f, 0.749f, 0.682f, 0.75f); break;
        case Cell::Held: back = gfx::rgba(0, 0, 0, 0.35f); break;
        case Cell::Fits: back = gfx::rgba(0.431f, 0.620f, 0.345f, 0.24f); edge = gfx::rgba(0.549f, 0.784f, 0.431f, 0.75f); break;
        case Cell::Blocked: back = gfx::rgba(0.702f, 0.149f, 0.118f, 0.26f); edge = gfx::rgba(0.941f, 0.380f, 0.314f, 0.8f); break;
    }
    canvas.rect(b, back);
    if (state == Cell::Rest) stone(canvas, b, kWell);
    canvas.outline(b, line, edge);
}

void grid(gfx::Canvas& canvas, const Box& box, int columns, int rows, float u) {
    const Box b{std::round(box.x), std::round(box.y), std::round(box.w), std::round(box.h)};
    const float line = px(u);
    // A seam of black round the block and the iron edge inside it, as a well is framed.
    canvas.outline(b.grown(line * 2.0f), line, style::kSeam);
    canvas.outline(b.grown(line), line, style::kIronLo);
    canvas.rect(b, style::kVoid);
    stone(canvas, b, kWell);
    const float pw = b.w / float(columns), ph = b.h / float(rows);
    for (int c = 1; c < columns; ++c) {
        canvas.rect({std::floor(b.x + pw * float(c) - line * 0.5f + 0.5f), b.y, line, b.h}, style::kIronDk);
    }
    for (int r = 1; r < rows; ++r) {
        canvas.rect({b.x, std::floor(b.y + ph * float(r) - line * 0.5f + 0.5f), b.w, line}, style::kIronDk);
    }
}

void rule(gfx::Canvas& canvas, float x, float y, float wide, float u) {
    const float half = wide * 0.5f, line = px(u);
    const uint32_t mid = style::kIron, clear = style::kIronLo & 0x00FFFFFFu;
    canvas.shade({std::round(x), std::round(y), half, line}, clear, mid, mid, clear);
    canvas.shade({std::round(x + half), std::round(y), half, line}, mid, clear, clear, mid);
}

// ---- buttons -----------------------------------------------------------------------------------

void button(gfx::Canvas& canvas, const Box& box, const std::string& text, Kind kind,
            const State& state, float u, float wordSize) {
    const Box b{std::round(box.x), std::round(box.y), std::round(box.w), std::round(box.h)};
    const float size = wordSize > 0.0f ? wordSize
                       : (b.h >= style::kButtonL * u - 0.5f   ? style::kButtonWordL
                          : b.h >= style::kButtonM * u - 0.5f ? style::kButtonWordM
                                                              : style::kButtonWordS) *
                             u;
    if (kind == Kind::Quiet) {
        const Tone rest{0.561f, 0.522f, 0.459f, 1}, lit{0.953f, 0.910f, 0.831f, 1};
        word(canvas, b, size, state.off ? Tone{0.361f, 0.322f, 0.286f, 1} : rest.mix(lit, state.lift),
             text);
        return;
    }
    const Look look = lookOf(kind, state);
    const Box body = slabOf(canvas, b, look, state.held && !state.off, state.off, u);
    word(canvas, body, size, look.ink, text);
}

void square(gfx::Canvas& canvas, const Box& box, Glyph which, const State& state, float u,
            bool red, bool on) {
    const Box b{std::round(box.x), std::round(box.y), std::round(box.w), std::round(box.h)};
    Look look = lookOf(Kind::Secondary, state);
    if (red && !state.off) {
        const Look rest{{0.761f, 0.212f, 0.169f, 1}, {0.369f, 0.082f, 0.055f, 1},
                        {0.165f, 0.051f, 0.035f, 1}, {0.165f, 0.051f, 0.035f, 1},
                        {1.000f, 0.541f, 0.471f, 1}, 0.0f, 0.0f, 0.0f, kRedLight};
        const Look over{{0.941f, 0.380f, 0.314f, 1}, {0.478f, 0.110f, 0.075f, 1},
                        {0.761f, 0.212f, 0.169f, 1}, {0.580f, 0.137f, 0.102f, 1},
                        {1.000f, 1.000f, 1.000f, 1}, 0.0f, 0.45f, 0.2f, kRedLight};
        look = state.held ? kPrimaryHeld : mixed(rest, over, state.lift);
    }
    if (on && !state.off) {
        look = kSecondaryHeld;
        look.rimTop = kBloodTone;
        look.rimFoot = {0.369f, 0.082f, 0.055f, 1};
        look.ink = {1.0f, 0.604f, 0.549f, 1};
        look.glow = 0.35f;
    }
    const Box body = slabOf(canvas, b, look, (state.held || on) && !state.off, state.off, u);
    glyph(canvas, body, which, look.ink.packed(), u);
}

void chevron(gfx::Canvas& canvas, const Box& box, bool right, float lift, float u) {
    const Tone rest{0.420f, 0.337f, 0.271f, 1}, lit{0.941f, 0.380f, 0.314f, 1};
    glyph(canvas, box, right ? Glyph::Right : Glyph::Left, rest.mix(lit, lift).packed(), u * 1.3f);
}

// ---- settings and input ------------------------------------------------------------------------

void row(gfx::Canvas& canvas, const Box& box, const std::string& text, float lift, float u) {
    const Box b{std::round(box.x), std::round(box.y), std::round(box.w), std::round(box.h)};
    const float line = px(u), r = style::kRadiusControl * u;
    tip::panel(canvas, b, r, style::kIronDk, style::kIronDk);
    const Box in = b.grown(-line);
    tip::panel(canvas, in, r - line, gfx::rgba(0.031f, 0.024f, 0.020f, 0.96f),
               gfx::rgba(0.043f, 0.031f, 0.027f, 0.96f));
    stone(canvas, in, kStone);
    // The mark down its left edge: iron at rest, red under the pointer.
    const Tone rest{0.239f, 0.192f, 0.157f, 1};
    canvas.rect({in.x, in.y + r, std::round(2.0f * u), in.h - r * 2.0f},
                rest.mix(kBloodTone, lift).packed());
    const float size = style::kBodySize * u;
    label(canvas, b.x + 14.0f * u, middle(b.y, b.h, size), size, style::kBone2, text);
}

void toggle(gfx::Canvas& canvas, const Box& box, const char* left, const char* right,
            bool rightOn, float u) {
    const Box b{std::round(box.x), std::round(box.y), std::round(box.w), std::round(box.h)};
    const float line = px(u), r = style::kRadiusControl * u;
    tip::panel(canvas, b, r, style::kIronLo, style::kIronLo);
    const Box in = b.grown(-line);
    tip::panel(canvas, in, r - line, style::kVoid, style::kVoid);
    stone(canvas, in, kWell);
    const float half = std::round(in.w * 0.5f);
    const Box halves[2] = {{in.x, in.y, half, in.h}, {in.x + half, in.y, in.w - half, in.h}};
    const int on = rightOn ? 1 : 0;
    const float corners[2][4] = {{r - line, 0.0f, 0.0f, r - line}, {0.0f, r - line, r - line, 0.0f}};
    tip::rounded(canvas, halves[on], corners[on], style::kBloodMd, style::kBloodDk);
    canvas.rect({halves[on].x, halves[on].y, halves[on].w, line}, gfx::rgba(1.0f, 0.706f, 0.627f, 0.2f));
    canvas.rect({in.x + half, in.y, line, in.h}, style::kIronDk);
    const float size = style::kButtonWordS * u;
    word(canvas, halves[0], size, on == 0 ? Tone{1, 1, 1, 1} : Tone{0.561f, 0.522f, 0.459f, 1}, left);
    word(canvas, halves[1], size, on == 1 ? Tone{1, 1, 1, 1} : Tone{0.561f, 0.522f, 0.459f, 1}, right);
}

void slider(gfx::Canvas& canvas, const Box& box, float value, const std::string& figure, float u) {
    const float line = px(u);
    const float size = 14.0f * u;
    const float figureWide = 44.0f * u;
    const float trackW = std::round(box.w - figureWide - 12.0f * u);
    const float trackH = std::round(4.0f * u);
    const Box track{std::round(box.x), std::round(box.midY() - trackH * 0.5f), trackW, trackH};
    tip::panel(canvas, track.grown(line), line * 2.0f, style::kIronLo, style::kIronLo);
    canvas.rect(track, style::kVoid);
    const float filled = std::round(track.w * std::clamp(value, 0.0f, 1.0f));
    if (filled > 0.0f) {
        canvas.shade({track.x, track.y, filled, track.h}, style::kBloodDk, style::kBlood,
                     style::kBlood, style::kBloodDk);
    }
    const Box handle{std::round(track.x + filled - 3.0f * u), std::round(box.midY() - 7.0f * u),
                     std::round(6.0f * u), std::round(14.0f * u)};
    tip::panel(canvas, handle.grown(line), style::kRadiusSmall * u, style::kSeam, style::kSeam);
    tip::panel(canvas, handle, style::kRadiusSmall * u * 0.5f, style::kBoneHi, style::kBone2);
    const float wide = labelWidth(size, figure);
    label(canvas, box.right() - wide, middle(box.y, box.h, size), size, style::kBoneHi, figure);
}

void field(gfx::Canvas& canvas, const Box& box, const std::string& text,
           const std::string& placeholder, bool focus, bool error, bool caret, bool off, float u) {
    const Box b{std::round(box.x), std::round(box.y), std::round(box.w), std::round(box.h)};
    const float line = px(u), r = style::kRadiusControl * u;
    const uint32_t rim = off ? style::kIronDk : error ? style::kBlood : focus ? style::kIronHi : style::kIronLo;
    if (focus && !error && !off) {
        tip::panel(canvas, b.grown(line * 4.0f), r + line * 4.0f,
                   kBloodTone.alpha(0.05f).packed(), kBloodTone.alpha(0.08f).packed());
    }
    tip::panel(canvas, b, r, rim, rim);
    const Box in = b.grown(-line);
    tip::panel(canvas, in, r - line, style::kVoid, style::kVoid);
    stone(canvas, in, kWell);
    const float deep = std::round(6.0f * u);
    canvas.shade({in.x, in.y, in.w, deep}, gfx::rgba(0, 0, 0, 0.6f), gfx::rgba(0, 0, 0, 0.6f),
                 gfx::rgba(0, 0, 0, 0), gfx::rgba(0, 0, 0, 0));
    const float size = 17.0f * u;
    const float baseline = middle(b.y, b.h, size);
    const float x = b.x + 14.0f * u;
    const bool empty = text.empty();
    const float wide = label(canvas, x, baseline, size,
                             off ? style::kAshInk2 : empty ? style::kAshInk2 : style::kBoneHi,
                             empty ? placeholder : text);
    if (caret && !off) {
        const float at = empty ? x : x + wide + 2.0f * u;
        const float tall = std::round(b.h * 0.5f);
        canvas.rect({std::round(at), std::round(b.midY() - tall * 0.5f), std::max(1.0f, std::round(2.0f * u)), tall},
                    style::kBloodHi);
    }
}

void message(gfx::Canvas& canvas, float x, float baseline, const std::string& text, float u) {
    const float size = 14.0f * u;
    canvas.rect({std::round(x), std::round(baseline - size * 0.72f), std::max(1.0f, std::round(3.0f * u)),
                 std::round(size * 0.82f)},
                style::kBlood);
    label(canvas, x + 10.0f * u, baseline, size, gfx::rgba(1.0f, 0.541f, 0.471f), text);
}

void hint(gfx::Canvas& canvas, float x, float bottom, const std::string& text, float u) {
    const float size = 13.0f * u, line = px(u);
    const float wide = std::round(labelWidth(size, text) + 16.0f * u), tall = std::round(24.0f * u);
    const Box box{std::round(x - wide * 0.5f), std::round(bottom - tall), wide, tall};
    tip::panel(canvas, box.grown(line), style::kRadiusSmall * u + line, style::kIron, style::kIronLo);
    tip::panel(canvas, box, style::kRadiusSmall * u, style::kAsh2, style::kAsh1);
    stone(canvas, box, kStone);
    label(canvas, box.x + 8.0f * u, middle(box.y, box.h, size), size, style::kBoneHi, text);
}

void meter(gfx::Canvas& canvas, const Box& box, float share, uint32_t top, uint32_t foot, float u) {
    const Box b{std::round(box.x), std::round(box.y), std::round(box.w), std::round(box.h)};
    const float line = px(u);
    tip::panel(canvas, b.grown(line), style::kRadiusSmall * u * 0.5f + line, style::kIronLo,
               style::kIronLo);
    canvas.rect(b, style::kVoid);
    const float filled = std::round(b.w * std::clamp(share, 0.0f, 1.0f));
    if (filled > 0.0f) canvas.shade({b.x, b.y, filled, b.h}, top, top, foot, foot);
}

}  // namespace mu::game::controls
