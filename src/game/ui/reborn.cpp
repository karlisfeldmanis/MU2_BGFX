#include "game/ui/reborn.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>

#include "core/log.h"
#include "net/wire.h"

namespace mu::game {
namespace {

// The page's numbers at 1080 lines; everything is multiplied by height / 1080.
constexpr float kReference = 1080.0f;
constexpr float kFoot = 0.785f;          // the name's foot, down the screen (Arrival's)
constexpr float kNameEm = 17.0f, kNameLine = 20.0f, kNameTracking = 0.12f;
constexpr float kWordEm = 26.0f, kWordLine = 30.0f, kWordTracking = 0.46f;
constexpr float kWordTrackingFrom = 0.72f, kWordTrackingOut = 0.50f;
constexpr float kGap = 6.0f;             // the name's line under REBIRTH's
constexpr float kHaloBox = 174.0f;       // the halo's box, its bottom this far above REBIRTH's top:
constexpr float kHaloGap = 7.0f;
constexpr float kNumeralEm = 96.0f;
constexpr float kNumeralTop = 42.0f;     // the numeral's line box in the halo's, flex-centred 6 px low
constexpr float kPi = 3.14159265f;

// The shadows as Gaussian sigmas in pixels at 1080: a CSS blur radius over two.
constexpr float kNumeralSigma = 1.5f;    // drop-shadow 0 1px 3px, 50%
constexpr float kWordTightSigma = 1.5f;  // text-shadow 0 1px 3px, 90%
constexpr float kWordSoftSigma = 4.0f;   // text-shadow 0 0 8px, 50%
constexpr float kNameSigma = 1.0f;       // text-shadow 0 1px 2px, 50%

// Bakes. The sharp Cinzel is baked larger than the 96 px numeral at 1080, for a larger screen,
// and REBIRTH is drawn out of it minified; the halos are baked where their sigma is a texel or two.
constexpr float kTitleBake = 128.0f;     // line height, pixels
constexpr int kTitleAtlas = 2048;
constexpr float kNumeralShadowBake = 40.0f;
constexpr float kWordTightBake = 24.0f;
constexpr float kWordSoftBake = 16.0f;
constexpr float kNameBake = 48.0f;       // the launcher's own bake of the face, so the memo's
constexpr float kNameShadowBake = 24.0f;

constexpr uint32_t rgb(int r, int g, int b, float a) {
    return gfx::rgba(float(r) / 255.0f, float(g) / 255.0f, float(b) / 255.0f, a);
}

// CSS's cubic-bezier(x1, y1, x2, y2) at x = `u`: solve x(t) = u, return y(t) (as Arrival's).
float bezier(float x1, float y1, float x2, float y2, float u) {
    u = std::clamp(u, 0.0f, 1.0f);
    const auto at = [](float a, float b, float t) {
        const float s = 1.0f - t;
        return 3.0f * s * s * t * a + 3.0f * s * t * t * b + t * t * t;
    };
    float lo = 0.0f, hi = 1.0f, t = u;
    for (int i = 0; i < 40; ++i) {
        t = (lo + hi) * 0.5f;
        if (at(x1, x2, t) < u) lo = t;
        else hi = t;
    }
    return at(y1, y2, t);
}

struct Curve {
    float x1, y1, x2, y2;
    float operator()(float u) const { return bezier(x1, y1, x2, y2, u); }
};
constexpr Curve kLinear{0.0f, 0.0f, 1.0f, 1.0f};
constexpr Curve kEaseOut{0.0f, 0.0f, 0.58f, 1.0f};
constexpr Curve kEaseInOut{0.42f, 0.0f, 0.58f, 1.0f};
constexpr Curve kRays{0.55f, 0.0f, 0.9f, 0.4f};    // the rays' fall: slow, then fast
constexpr Curve kDraw{0.2f, 0.7f, 0.3f, 1.0f};     // the rings drawing round
constexpr Curve kLand{0.3f, 0.0f, 0.2f, 1.0f};     // the numeral landing
constexpr Curve kSettle{0.2f, 0.7f, 0.2f, 1.0f};   // REBIRTH closing up

// The page's keyframes in seconds of the 4.6 s (a percentage of it times 0.046), its timing
// function applied to each segment between two of them, as CSS does.
struct Key {
    float at, value;
};
float track(float t, std::initializer_list<Key> keys, Curve curve) {
    const Key* k = keys.begin();
    const size_t n = keys.size();
    if (t <= k[0].at) return k[0].value;
    for (size_t i = 1; i < n; ++i) {
        if (t < k[i].at) {
            const float u = (t - k[i - 1].at) / std::max(1e-6f, k[i].at - k[i - 1].at);
            return k[i - 1].value + (k[i].value - k[i - 1].value) * curve(u);
        }
    }
    return k[n - 1].value;
}
constexpr float pc(float percent) { return percent * Reborn::kSeconds / 100.0f; }

// The hit, the second thump and the fade, on the page's percentages.
constexpr float kBefore = pc(5.2f);   // 0.24 s: the halo and numeral still waiting
constexpr float kPrimed = pc(5.6f);   // the flash and glow still waiting
constexpr float kHit = pc(5.9f);      // 0.27 s
constexpr float kThump = pc(8.2f);    // 0.375 s
constexpr float kHeld = pc(78.3f);    // 3.6 s: the fade begins

bool bakeOne(gfx::Face& face, bgfx::TextureHandle& texture, const char* path, float pixels,
             int size, float sigmaAt1080, float emAt1080, const char* name) {
    // As Arrival's: room for the halo reserved at the largest the em can be, then the blur is
    // exactly the page's fraction of an em.
    const int spread = sigmaAt1080 > 0.0f ? int(std::ceil(sigmaAt1080 * pixels / emAt1080 * 3.0f)) : 0;
    if (!face.bake(path, pixels, size, 4 + spread * 2, 1, spread)) return false;
    if (sigmaAt1080 > 0.0f) face.blur(sigmaAt1080 / emAt1080 / face.emScale(1.0f));
    texture = gfx::uploadFace(face, name);
    face.dropPixels();
    return bgfx::isValid(texture);
}

// A radial gradient: a disc at the centre and rings out to the last stop, which is clear.
// CSS's `radial-gradient(circle, ...)` measures its stops against the farthest corner of its
// box, so a stop at 45% of a 340 px box is 108 px out, not 76.
struct Stop {
    float radius;
    uint32_t colour;
};
void radial(gfx::Canvas& canvas, float cx, float cy, std::initializer_list<Stop> stops) {
    constexpr int kSides = 64;
    const Stop* s = stops.begin();
    const size_t n = stops.size();
    // The centre out to the first stop past it.
    float xy[(kSides + 2) * 2];
    uint32_t colours[kSides + 2];
    xy[0] = cx, xy[1] = cy, colours[0] = s[0].colour;
    for (int i = 0; i <= kSides; ++i) {
        const float a = 2.0f * kPi * float(i) / kSides;
        xy[(i + 1) * 2] = cx + std::sin(a) * s[1].radius;
        xy[(i + 1) * 2 + 1] = cy - std::cos(a) * s[1].radius;
        colours[i + 1] = s[1].colour;
    }
    canvas.polygon(xy, colours, kSides + 2);
    for (size_t j = 2; j < n; ++j) {
        for (int i = 0; i < kSides; ++i) {
            const float a0 = 2.0f * kPi * float(i) / kSides, a1 = 2.0f * kPi * float(i + 1) / kSides;
            const float r0 = s[j - 1].radius, r1 = s[j].radius;
            const float quad[8] = {cx + std::sin(a0) * r0, cy - std::cos(a0) * r0,
                                   cx + std::sin(a1) * r0, cy - std::cos(a1) * r0,
                                   cx + std::sin(a1) * r1, cy - std::cos(a1) * r1,
                                   cx + std::sin(a0) * r1, cy - std::cos(a0) * r1};
            const uint32_t c[4] = {s[j - 1].colour, s[j - 1].colour, s[j].colour, s[j].colour};
            canvas.polygon(quad, c, 4);
        }
    }
}

// A ring's arc from `from` degrees round to `from + sweep`, clockwise from the top as CSS's
// conic gradients go, its alpha a function of the angle on screen. The page masks it with a
// radial gradient: clear at `inner`, solid from `solid0` to `solid1`, clear again at `outer`.
template <typename Alpha>
void arc(gfx::Canvas& canvas, float cx, float cy, float inner, float solid0, float solid1,
         float outer, float from, float sweep, int r, int g, int b, Alpha alphaAt) {
    if (sweep <= 0.0f) return;
    const int steps = std::max(1, int(std::ceil(sweep / 2.0f)));
    const float radii[4] = {inner, solid0, solid1, outer};
    for (int i = 0; i < steps; ++i) {
        const float d0 = from + sweep * float(i) / steps, d1 = from + sweep * float(i + 1) / steps;
        const float a0 = d0 * kPi / 180.0f, a1 = d1 * kPi / 180.0f;
        const float alpha0 = alphaAt(d0), alpha1 = alphaAt(d1);
        if (alpha0 <= 0.0f && alpha1 <= 0.0f) continue;
        for (int band = 0; band < 3; ++band) {
            const float q0 = radii[band], q1 = radii[band + 1];
            const float in = band == 0 ? 0.0f : 1.0f, out = band == 2 ? 0.0f : 1.0f;
            const float quad[8] = {cx + std::sin(a0) * q0, cy - std::cos(a0) * q0,
                                   cx + std::sin(a1) * q0, cy - std::cos(a1) * q0,
                                   cx + std::sin(a1) * q1, cy - std::cos(a1) * q1,
                                   cx + std::sin(a0) * q1, cy - std::cos(a0) * q1};
            const uint32_t c[4] = {rgb(r, g, b, alpha0 * in), rgb(r, g, b, alpha1 * in),
                                   rgb(r, g, b, alpha1 * out), rgb(r, g, b, alpha0 * out)};
            canvas.polygon(quad, c, 4);
        }
    }
}

float wrap360(float d) {
    d = std::fmod(d, 360.0f);
    return d < 0.0f ? d + 360.0f : d;
}

}  // namespace

void Reborn::open(const gfx::Interface& interface) {
    interface_ = &interface;
    interface.adopt(canvas_);
    const char* cinzel = gfx::deathFacePath();  // Cinzel Bold
    const char* alegreya = gfx::fontPath("AlegreyaSansSC-Bold.ttf");
    const bool ok =
        bakeOne(title_, titleTexture_, cinzel, kTitleBake, kTitleAtlas, 0.0f, kNumeralEm,
                "reborn title") &&
        bakeOne(numeralShadow_, numeralShadowTexture_, cinzel, kNumeralShadowBake, 1024,
                kNumeralSigma, kNumeralEm, "reborn numeral shadow") &&
        bakeOne(wordTight_, wordTightTexture_, cinzel, kWordTightBake, 512, kWordTightSigma, kWordEm,
                "reborn word shadow") &&
        bakeOne(wordSoft_, wordSoftTexture_, cinzel, kWordSoftBake, 512, kWordSoftSigma, kWordEm,
                "reborn word halo") &&
        bakeOne(name_, nameTexture_, alegreya, kNameBake, 512, 0.0f, kNameEm, "reborn name") &&
        bakeOne(nameShadow_, nameShadowTexture_, alegreya, kNameShadowBake, 512, kNameSigma, kNameEm,
                "reborn name shadow");
    if (!ok) core::logError("reborn: the faces did not bake; no Rebirth message will show");
}

void Reborn::shutdown() {
    for (bgfx::TextureHandle* t : {&titleTexture_, &numeralShadowTexture_, &wordTightTexture_,
                                   &wordSoftTexture_, &nameTexture_, &nameShadowTexture_}) {
        if (bgfx::isValid(*t)) bgfx::destroy(*t);
        *t = BGFX_INVALID_HANDLE;
    }
}

void Reborn::announce(int count, const std::string& name) {
    numeral_ = net::romanNumeral(count);
    who_ = name;
    clock_ = kHeard;
    fresh_ = true;
    core::logf("reborn: %s, rebirth %s", name.c_str(), numeral_.c_str());
}

void Reborn::update(float seconds, float width, float height) {
    if (numeral_.empty()) return;
    if (!fresh_) clock_ += seconds;
    fresh_ = false;
    if (clock_ >= kSeconds) {
        numeral_.clear();
        canvas_.clear();
        return;
    }
    if (!bgfx::isValid(titleTexture_)) return;
    rebuild(width, height);
}

void Reborn::rebuild(float width, float height) {
    canvas_.clear();
    const float t = clock_;
    const float k = height / kReference;
    const float cx = width * 0.5f;

    // The stack, from the name's foot up.
    const float foot = height * kFoot;
    const float nameTop = foot - kNameLine * k;
    const float wordTop = nameTop - kGap * k - kWordLine * k;
    const float cy = wordTop - kHaloGap * k - kHaloBox * 0.5f * k;  // the halo's centre

    // ---- the darkening behind it all, in by the hit and out with the rest. It and the rays
    // start from nothing where the sound is first heard, not at the page's 0, which the clock
    // never shows: begun there, the darkening came in at half its depth on the first frame.
    {
        const float bed = track(t, {{Reborn::kHeard, 0.0f}, {kHit, 1.0f}, {kHeld, 1.0f}, {kSeconds, 0.0f}}, kEaseOut);
        const float corner = 170.0f * 1.41421356f * k;
        if (bed > 0.0f) {
            radial(canvas_, cx, cy,
                   {{0.0f, rgb(5, 3, 3, 0.30f * bed)},
                    {corner * 0.45f, rgb(5, 3, 3, 0.16f * bed)},
                    {corner * 0.70f, rgb(5, 3, 3, 0.0f)}});
        }
    }

    // ---- the rays: five lines falling onto the ring from above, grown down from their tops
    // over the swoosh (scaleY from the top), then breathing.
    {
        const float grown = track(t, {{Reborn::kHeard, 0.0f}, {kHit, 1.0f}}, kRays);
        const float alpha = track(t,
                                  {{Reborn::kHeard, 0.0f}, {kHit, 1.0f}, {pc(20.0f), 0.7f}, {pc(40.0f), 1.0f},
                                   {pc(60.0f), 0.75f}, {kHeld, 0.9f}, {kSeconds, 0.0f}},
                                  kRays);
        const float top = cy - 226.0f * k;  // the rays' box: 150 px tall, its foot at the ring
        const float foot = top + 150.0f * k;
        struct Ray {
            float x, degrees, length, wide, alpha;
        };
        const Ray rays[5] = {{0.0f, 0.0f, 150.0f, 2.0f, 0.55f},  {-27.5f, -9.0f, 120.0f, 1.0f, 0.35f},
                             {27.5f, 9.0f, 120.0f, 1.0f, 0.35f}, {-55.5f, -18.0f, 90.0f, 1.0f, 0.22f},
                             {55.5f, 18.0f, 90.0f, 1.0f, 0.22f}};
        for (const Ray& ray : rays) {
            if (grown <= 0.0f || alpha <= 0.0f) break;
            // Turned about its foot, then the whole box squeezed toward its top.
            const float a = ray.degrees * kPi / 180.0f;
            const float fx = cx + ray.x * k, fy = foot;
            const float tx = fx + std::sin(a) * ray.length * k, ty = fy - std::cos(a) * ray.length * k;
            const auto squeeze = [&](float y) { return top + (y - top) * grown; };
            const float px = std::cos(a) * ray.wide * 0.5f * k, py = std::sin(a) * ray.wide * 0.5f * k;
            const float quad[8] = {tx - px, squeeze(ty - py), tx + px, squeeze(ty + py),
                                   fx + px, squeeze(fy + py), fx - px, squeeze(fy - py)};
            const uint32_t clear = rgb(255, 232, 180, 0.0f), lit = rgb(255, 232, 180, ray.alpha * alpha);
            const uint32_t c[4] = {clear, clear, lit, lit};
            canvas_.polygon(quad, c, 4);
        }
    }

    // ---- the halo: everything in it scaled about its centre and faded together.
    const float haloAlpha = track(t, {{0.0f, 0.0f}, {kBefore, 0.0f}, {kHit, 1.0f}, {kHeld, 1.0f},
                                      {kSeconds, 0.0f}}, kEaseOut);
    if (haloAlpha <= 0.0f) {
        // Nothing of it yet; the words come later still.
        return;
    }
    const float haloScale = track(t, {{0.0f, 0.92f}, {kBefore, 0.92f}, {kHit, 1.02f}, {pc(10.0f), 1.0f},
                                      {kHeld, 1.0f}, {kSeconds, 1.07f}}, kEaseOut);
    const float hk = k * haloScale;  // a halo pixel, on screen

    // The flash on the hit: a warm disc thrown open and gone by 0.64 s.
    {
        const float a = track(t, {{0.0f, 0.0f}, {kPrimed, 0.0f}, {kHit, 1.0f}, {pc(14.0f), 0.0f}}, kEaseOut);
        const float s = track(t, {{0.0f, 0.7f}, {kPrimed, 0.7f}, {kHit, 0.85f}, {pc(14.0f), 1.35f}}, kEaseOut);
        if (a > 0.0f) {
            const float corner = 100.0f * 1.41421356f * hk * s;
            const float f = a * haloAlpha;
            radial(canvas_, cx, cy,
                   {{0.0f, rgb(255, 246, 220, 0.55f * f)},
                    {corner * 0.40f, rgb(255, 226, 160, 0.18f * f)},
                    {corner * 0.70f, rgb(255, 226, 160, 0.0f)}});
        }
    }
    // The glow inside the ring.
    {
        const float a = track(t,
                              {{0.0f, 0.0f}, {kPrimed, 0.0f}, {kHit, 1.0f}, {pc(14.0f), 0.7f},
                               {pc(40.0f), 0.95f}, {pc(60.0f), 0.8f}, {kHeld, 0.85f}, {kSeconds, 0.0f}},
                              kEaseInOut) * haloAlpha;
        if (a > 0.0f) {
            const float corner = 75.0f * 1.41421356f * hk;
            radial(canvas_, cx, cy,
                   {{0.0f, rgb(240, 200, 120, 0.26f * a)},
                    {corner * 0.55f, rgb(240, 200, 120, 0.06f * a)},
                    {corner * 0.70f, rgb(240, 200, 120, 0.0f)}});
        }
    }
    // The motes: five gold dots rising inside the ring, each on its own 2.6 s loop.
    {
        const float all = track(t, {{0.0f, 0.0f}, {pc(12.0f), 0.0f}, {pc(20.0f), 1.0f}, {kHeld, 1.0f},
                                    {pc(95.0f), 0.0f}}, kLinear) * haloAlpha;
        struct Mote {
            float x, y, size, delay;
        };
        const Mote motes[5] = {{70, 110, 3, 0.0f}, {102, 118, 2, 0.7f}, {58, 96, 2, 1.3f},
                               {114, 100, 3, 1.9f}, {86, 124, 2, 0.4f}};
        for (const Mote& m : motes) {
            if (all <= 0.0f || t < m.delay) continue;
            const float u = std::fmod(t - m.delay, 2.6f) / 2.6f;
            const float rise = 26.0f + (-38.0f - 26.0f) * kEaseInOut(u);
            const float a = track(u, {{0.0f, 0.0f}, {0.25f, 0.9f}, {0.75f, 0.6f}, {1.0f, 0.0f}}, kEaseInOut) * all;
            if (a <= 0.0f) continue;
            const float mx = cx + (m.x + m.size * 0.5f - 87.0f) * hk;
            const float my = cy + (m.y + m.size * 0.5f + rise - 87.0f) * hk;
            constexpr int kSides = 10;
            float xy[kSides * 2];
            uint32_t c[kSides];
            for (int i = 0; i < kSides; ++i) {
                const float b = 2.0f * kPi * float(i) / kSides;
                xy[i * 2] = mx + std::sin(b) * m.size * 0.5f * hk;
                xy[i * 2 + 1] = my - std::cos(b) * m.size * 0.5f * hk;
                c[i] = rgb(255, 230, 176, a);
            }
            canvas_.polygon(xy, c, kSides);
        }
    }
    // The inner ring: gold, full at the top of its turn and gone at the bottom, turning once in
    // 9 s; drawn round from the top over the half second after the hit.
    {
        const float sweep = track(t, {{0.0f, 0.0f}, {kHit, 0.0f}, {pc(17.4f), 360.0f}}, kDraw);
        const float spin = 360.0f * t / 9.0f;
        arc(canvas_, cx, cy, 70.0f * hk, 71.0f * hk, 72.5f * hk, 73.5f * hk, 0.0f, sweep, 255, 226, 160,
            [&](float d) {
                const float g = wrap360(d - spin - 180.0f);  // conic-gradient(from 180deg)
                const float a = g < 150.0f ? g / 150.0f : g < 210.0f ? 1.0f : (360.0f - g) / 150.0f;
                return 0.95f * a * haloAlpha;
            });
    }
    // The outer ring: a hairline 240-degree arc turning the other way, once in 14 s; drawn round
    // from the bottom on the second thump.
    {
        const float sweep = track(t, {{0.0f, 0.0f}, {kThump, 0.0f}, {pc(21.7f), 360.0f}}, kDraw);
        const float spin = -360.0f * t / 14.0f;
        arc(canvas_, cx, cy, 84.6f * hk, 85.2f * hk, 85.8f * hk, 86.4f * hk, 180.0f, sweep, 201, 149, 63,
            [&](float d) {
                const float g = wrap360(d - spin);
                const float a = g < 120.0f ? g / 120.0f : g < 240.0f ? (240.0f - g) / 120.0f : 0.0f;
                return 0.42f * a * haloAlpha;
            });
    }
    // The numeral: landing from 1.3 on the hit, in a vertical gold gradient down its line box,
    // a glint crossing it once, over its soft shadow.
    {
        const float a = track(t, {{0.0f, 0.0f}, {kBefore, 0.0f}, {kHit, 1.0f}, {kHeld, 1.0f},
                                  {kSeconds, 0.0f}}, kLand) * haloAlpha;
        const float s = track(t, {{0.0f, 1.3f}, {kBefore, 1.3f}, {kHit, 1.0f}, {kHeld, 1.0f},
                                  {kSeconds, 0.96f}}, kLand);
        const float glint = track(t, {{0.0f, 1.6f}, {pc(6.5f), 1.6f}, {pc(12.0f), -0.6f}}, kEaseOut);
        if (a > 0.0f) {
            // In the numeral's own pixels at 1080, its line box's top-left the origin; then
            // landed about the box's centre and scaled with the halo about the halo's.
            const float em = kNumeralEm;
            const float wide = title_.measure(em, numeral_);
            const float boxLeft = -wide * 0.5f, boxTop = kNumeralTop - kHaloBox * 0.5f;  // from the centre
            const float baseline =
                boxTop + (em - title_.ascent(em) - title_.descent(em)) * 0.5f + title_.ascent(em);
            const float boxMidY = boxTop + em * 0.5f;
            const auto place = [&](float x, float y, float* out) {
                out[0] = cx + x * s * hk;
                out[1] = cy + (boxMidY + (y - boxMidY) * s) * hk;
            };
            // The shadow: the blurred bake, a pixel down, under the whole of it.
            {
                float at[2];
                place(boxLeft, baseline + 1.0f, at);
                canvas_.lettered(numeralShadow_, numeralShadowTexture_, at[0], at[1], em * s * hk, 0.0f,
                                 rgb(0, 0, 0, 0.5f * a), numeral_);
            }
            // The gradient's stops down the line box, and the glint: a band 105 degrees across a
            // background 2.6 of the box wide, its middle at `glint` of the way along.
            const auto gold = [&](float y) {
                struct G {
                    float at;
                    int r, g, b;
                };
                static const G kStops[4] = {{0.0f, 255, 243, 214}, {0.40f, 240, 207, 134},
                                            {0.75f, 201, 149, 63}, {1.0f, 138, 95, 34}};
                const float u = std::clamp((y - boxTop) / em, 0.0f, 1.0f);
                int i = 0;
                while (i < 2 && u > kStops[i + 1].at) ++i;
                const float f = (u - kStops[i].at) / (kStops[i + 1].at - kStops[i].at);
                float out[3] = {float(kStops[i].r) + (float(kStops[i + 1].r) - float(kStops[i].r)) * f,
                                float(kStops[i].g) + (float(kStops[i + 1].g) - float(kStops[i].g)) * f,
                                float(kStops[i].b) + (float(kStops[i + 1].b) - float(kStops[i].b)) * f};
                return std::array<float, 3>{out[0], out[1], out[2]};
            };
            const float dirX = std::sin(105.0f * kPi / 180.0f), dirY = -std::cos(105.0f * kPi / 180.0f);
            const float lineLength = wide * 2.6f * std::fabs(dirX) + em * std::fabs(dirY);
            const float bandX = boxLeft - 1.6f * wide * glint + 1.3f * wide;
            const auto colourAt = [&](float x, float y) {
                const std::array<float, 3> g = gold(y);
                const float d = (x - bandX) * dirX + (y - boxMidY) * dirY;
                const float shine = 0.95f * std::max(0.0f, 1.0f - std::fabs(d) / (0.08f * lineLength));
                return rgb(int(g[0] + (255.0f - g[0]) * shine), int(g[1] + (252.0f - g[1]) * shine),
                           int(g[2] + (240.0f - g[2]) * shine), a);
            };
            const gfx::Art art{titleTexture_, float(title_.size()), float(title_.size())};
            const float e = title_.emScale(em);
            float pen = boxLeft;
            for (char c : numeral_) {
                const gfx::FaceGlyph* g = title_.glyph(c);
                if (!g) continue;
                if (g->x1 > g->x0 && g->y1 > g->y0) {
                    // The glyph's quad cut into cells about 3 px across, each corner coloured.
                    const float x0 = pen + g->x0 * e, y0 = baseline + g->y0 * e;
                    const float w = (g->x1 - g->x0) * e, h = (g->y1 - g->y0) * e;
                    const int nx = std::max(1, int(std::ceil(w / 3.0f))), ny = std::max(1, int(std::ceil(h / 3.0f)));
                    for (int j = 0; j < ny; ++j) {
                        for (int i = 0; i < nx; ++i) {
                            const float fx[2] = {float(i) / nx, float(i + 1) / nx};
                            const float fy[2] = {float(j) / ny, float(j + 1) / ny};
                            const int corner[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
                            float xy[8], uv[8];
                            uint32_t colours[4];
                            for (int v = 0; v < 4; ++v) {
                                const float px = x0 + w * fx[corner[v][0]], py = y0 + h * fy[corner[v][1]];
                                place(px, py, xy + v * 2);
                                uv[v * 2] = g->u0 + (g->u1 - g->u0) * fx[corner[v][0]];
                                uv[v * 2 + 1] = g->v0 + (g->v1 - g->v0) * fy[corner[v][1]];
                                colours[v] = colourAt(px, py);
                            }
                            canvas_.polygon(art, xy, uv, colours, 4);
                        }
                    }
                }
                pen += g->advance * e;
            }
        }
    }

    // ---- REBIRTH: in from 0.41 s while its letters close from 0.72 em to 0.46.
    {
        const float a = track(t, {{0.0f, 0.0f}, {pc(9.0f), 0.0f}, {pc(21.7f), 1.0f}, {kHeld, 1.0f},
                                  {kSeconds, 0.0f}}, kSettle);
        const float spacing = track(t, {{0.0f, kWordTrackingFrom}, {pc(9.0f), kWordTrackingFrom},
                                        {pc(21.7f), kWordTracking}, {kHeld, kWordTracking},
                                        {kSeconds, kWordTrackingOut}}, kSettle);
        if (a > 0.0f) {
            static const std::string kWord = "REBIRTH";
            const float em = kWordEm * k;
            const float tracking = spacing * em, pad = kWordTracking * em;
            const float total = pad + title_.measure(em, kWord) + tracking * float(kWord.size());
            const float x = cx - total * 0.5f + pad;
            const float baseline = wordTop + (kWordLine * k - title_.ascent(em) - title_.descent(em)) * 0.5f +
                                   title_.ascent(em);
            canvas_.lettered(wordSoft_, wordSoftTexture_, x, baseline, em, tracking, rgb(0, 0, 0, 0.5f * a), kWord);
            canvas_.lettered(wordTight_, wordTightTexture_, x, baseline + k, em, tracking, rgb(0, 0, 0, 0.9f * a),
                             kWord);
            canvas_.lettered(title_, titleTexture_, x, baseline, em, tracking, rgb(243, 232, 212, a), kWord);
        }
    }

    // ---- the name: in from 0.87 s, rising 4 px.
    {
        const float a = track(t, {{0.0f, 0.0f}, {pc(19.0f), 0.0f}, {pc(28.0f), 1.0f}, {kHeld, 1.0f},
                                  {kSeconds, 0.0f}}, kEaseOut);
        const float drop = track(t, {{0.0f, 4.0f}, {pc(19.0f), 4.0f}, {pc(28.0f), 0.0f}}, kEaseOut);
        if (a > 0.0f && !who_.empty()) {
            const float em = kNameEm * k;
            const float tracking = kNameTracking * em;
            const float total = tracking + name_.measure(em, who_) + tracking * float(who_.size());
            const float x = cx - total * 0.5f + tracking;
            const float baseline = nameTop + drop * k +
                                   (kNameLine * k - name_.ascent(em) - name_.descent(em)) * 0.5f +
                                   name_.ascent(em);
            canvas_.lettered(nameShadow_, nameShadowTexture_, x, baseline + k, em, tracking,
                             rgb(0, 0, 0, 0.5f * a), who_);
            canvas_.lettered(name_, nameTexture_, x, baseline, em, tracking, rgb(217, 207, 189, a), who_);
        }
    }
}

}  // namespace mu::game
