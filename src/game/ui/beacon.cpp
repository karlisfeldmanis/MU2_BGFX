#include "game/ui/beacon.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "game/play.h"
#include "game/ui/tip.h"

namespace mu::game {

namespace {

// In the interface's own units: one pixel of a 1080-line screen (tip::unit).
constexpr float kCellW = 30.0f, kCellH = 56.0f;
constexpr float kMid = kCellW * 0.5f;
// The bar: a round top of radius kTopR tapering to a round foot of kFootR, kBarLong below it.
constexpr float kTopY = 10.0f, kTopR = 6.0f;
constexpr float kBarLong = 24.0f, kFootR = 3.4f;
// The dot, round.
constexpr float kDotY = 46.0f, kDotR = 5.0f;
constexpr float kRim = 2.0f;                       // the dark edge round it
constexpr float kShadowX = 1.2f, kShadowY = 2.0f;  // a hard shadow, down and right
// The engraved line: a thin darker line this far inside the edge, this wide.
constexpr float kInlay = 1.9f, kInlayWide = 0.8f;
// The flat highlight: a short stroke down the bar's left, inside the inlay.
constexpr float kGlintX = -2.3f, kGlintTop = 7.0f, kGlintFoot = 17.0f, kGlintR = 0.95f;

// Where it hangs: close over the crown, and lifted clear of the name while he is under the
// pointer and the name shows there (24 units). Then the bob.
constexpr float kLift = 2.0f;
constexpr float kLiftNamed = 24.0f;
constexpr float kBob = 2.0f;  // units, either way
constexpr float kBobHz = 0.5f;

// The whole mark against the interface unit.
constexpr float kSize = 1.05f;

// Flat colours, straight: one gold, a shade deeper for the right-hand facet, the engraving,
// the highlight and the rim.
struct Rgb {
    float r, g, b;
};
constexpr Rgb kGold = {0.98f, 0.76f, 0.26f};
constexpr Rgb kGoldDeep = {0.88f, 0.60f, 0.16f};
constexpr Rgb kInlayInk = {0.62f, 0.38f, 0.08f};
constexpr Rgb kGlint = {1.00f, 0.95f, 0.76f};
constexpr Rgb kRimInk = {0.14f, 0.08f, 0.03f};

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

// Inigo Quilez's uneven capsule: a circle of r1 at the origin, one of r2 at (0, h), and the
// tangent lines between them. `y` grows toward the second circle.
float unevenCapsule(float x, float y, float r1, float r2, float h) {
    x = std::fabs(x);
    const float b = (r1 - r2) / h;
    const float a = std::sqrt(1.0f - b * b);
    const float k = -b * x + a * y;
    if (k < 0.0f) return std::sqrt(x * x + y * y) - r1;
    if (k > a * h) return std::sqrt(x * x + (y - h) * (y - h)) - r2;
    return a * x + b * y - r1;
}

// A plain capsule: the segment from (x, y0) to (x, y1), rounded by r.
float capsule(float px, float py, float x, float y0, float y1, float r) {
    const float cy = std::clamp(py, y0, y1);
    const float dx = px - x, dy = py - cy;
    return std::sqrt(dx * dx + dy * dy) - r;
}

// The mark's distance, in units, from a point in the cell: negative inside.
float mark(float x, float y) {
    const float bar = unevenCapsule(x - kMid, y - kTopY, kTopR, kFootR, kBarLong);
    const float dx = x - kMid, dy = y - kDotY;
    const float dot = std::sqrt(dx * dx + dy * dy) - kDotR;
    return std::min(bar, dot);
}

// A premultiplied colour, so samples average and layers stack.
struct Pre {
    float r = 0, g = 0, b = 0, a = 0;
    void over(const Rgb& c, float alpha) {
        const float keep = 1.0f - alpha;
        r = c.r * alpha + r * keep;
        g = c.g * alpha + g * keep;
        b = c.b * alpha + b * keep;
        a = alpha + a * keep;
    }
};

}  // namespace

bool Beacon::bake(float unit) {
    if (bgfx::isValid(texture_)) bgfx::destroy(texture_);
    texture_ = BGFX_INVALID_HANDLE;
    art_ = {};
    cellW_ = int(std::ceil(kCellW * unit));
    cellH_ = int(std::ceil(kCellH * unit));
    std::vector<uint8_t> rgba(size_t(cellW_) * size_t(cellH_) * 4, 0);
    const float texel = 1.0f / unit;  // one texel, in units
    constexpr int kSide = 4;          // samples a side
    // How much of a sample a shape at distance `d` covers: a texel's worth of edge, so the
    // flat shapes stay crisp and only their edges are soft.
    const auto cover = [&](float d) { return clamp01(0.5f - d / texel); };
    for (int py = 0; py < cellH_; ++py) {
        for (int px = 0; px < cellW_; ++px) {
            Pre sum;
            for (int sy = 0; sy < kSide; ++sy) {
                for (int sx = 0; sx < kSide; ++sx) {
                    const float x = (float(px) + (float(sx) + 0.5f) / kSide) * texel;
                    const float y = (float(py) + (float(sy) + 0.5f) / kSide) * texel;
                    const float d = mark(x, y);
                    Pre one;
                    // The shadow, then the rim, both hard: a flat mark casts a flat shadow.
                    one.over(kRimInk, 0.45f * cover(mark(x - kShadowX, y - kShadowY) - kRim));
                    one.over(kRimInk, 0.96f * cover(d - kRim));
                    const float inside = cover(d);
                    if (inside > 0.0f) {
                        // Two flat facets split down the middle, the right a shade deeper.
                        one.over(x < kMid ? kGold : kGoldDeep, inside);
                        // The engraving: a thin line following the edge, inside it.
                        const float line = std::fabs(d + kInlay) - kInlayWide * 0.5f;
                        one.over(kInlayInk, 0.85f * cover(line) * inside);
                        // The highlight, a short flat stroke down the bar's left.
                        const float glint = capsule(x, y, kMid + kGlintX, kGlintTop,
                                                    kGlintFoot, kGlintR);
                        one.over(kGlint, 0.95f * cover(glint) * inside);
                    }
                    sum.r += one.r;
                    sum.g += one.g;
                    sum.b += one.b;
                    sum.a += one.a;
                }
            }
            constexpr float kShare = 1.0f / float(kSide * kSide);
            uint8_t* out = &rgba[(size_t(py) * size_t(cellW_) + size_t(px)) * 4];
            const float a = clamp01(sum.a * kShare);
            const float inv = a > 0.0f ? kShare / a : 0.0f;
            out[0] = uint8_t(clamp01(sum.r * inv) * 255.0f + 0.5f);
            out[1] = uint8_t(clamp01(sum.g * inv) * 255.0f + 0.5f);
            out[2] = uint8_t(clamp01(sum.b * inv) * 255.0f + 0.5f);
            out[3] = uint8_t(a * 255.0f + 0.5f);
        }
    }
    texture_ = bgfx::createTexture2D(uint16_t(cellW_), uint16_t(cellH_), false, 1,
                                     bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_UVW_CLAMP,
                                     bgfx::copy(rgba.data(), uint32_t(rgba.size())));
    if (!bgfx::isValid(texture_)) return false;
    bgfx::setName(texture_, "quest marker");
    art_ = {texture_, float(cellW_), float(cellH_)};
    bakedUnit_ = unit;
    return true;
}

void Beacon::close() {
    if (bgfx::isValid(texture_)) bgfx::destroy(texture_);
    texture_ = BGFX_INVALID_HANDLE;
    art_ = {};
    bakedUnit_ = 0.0f;
    dismiss();
}

void Beacon::update(float seconds, const Play& play, const float* viewProj, int width,
                    int height) {
    if (play.questGivers().empty()) {
        if (showing_) dismiss();
        return;
    }
    clock_ = std::fmod(clock_ + seconds, 1000.0f);
    const float u = tip::unit();
    if (u * kSize != bakedUnit_ && !bake(u * kSize)) return;
    canvas_.clear();
    showing_ = false;
    constexpr float kTau = 6.2831853f;
    const float bob = std::sin(clock_ * kTau * kBobHz) * kBob * u;
    for (int folk : play.questGivers()) {
        float x = 0.0f, y = 0.0f;
        if (!play.folkCrownOf(folk, viewProj, width, height, &x, &y)) continue;
        const float w = float(cellW_), h = float(cellH_);
        const float lift = play.pointedFolk() == folk ? kLiftNamed : kLift;
        canvas_.region(art_, {std::round(x - w * 0.5f), y - lift * u - h + bob, w, h},
                       {0.0f, 0.0f, w, h});
        showing_ = true;
    }
}

}  // namespace mu::game
