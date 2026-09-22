#include "game/arrival.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "core/log.h"

namespace mu::game {
namespace {

// The page's numbers at 1080 lines; everything is multiplied by height / 1080.
constexpr float kReference = 1080.0f;
constexpr float kNameEm = 35.52f;
constexpr float kNameTracking = 0.34f;      // em, at rest
constexpr float kNameTrackingFrom = 0.60f;  // em, as it comes in
constexpr float kCaptionEm = 11.14f;
constexpr float kCaptionTracking = 0.42f;
constexpr float kGap = 12.48f;
constexpr float kMarkBox = 11.14f;        // the diamond's box; the diamond is 0.9 of it
constexpr float kRuleLength = 201.6f;
constexpr float kRuleGap = 17.28f;
constexpr float kFoot = 0.785f;           // the caption's foot, down the screen

// The halos, as Gaussian sigmas in pixels at 1080: a CSS blur radius over two.
constexpr float kSoftSigma = 8.64f;       // text-shadow 0 .1cqw .9cqw
constexpr float kSoftDrop = 1.92f;
constexpr float kTightSigma = 1.92f;      // text-shadow 0 0 .2cqw
constexpr float kRuleSigma = 3.84f;       // box-shadow 0 0 .4cqw
constexpr float kMarkSigma = 2.88f;       // drop-shadow 0 0 .3cqw
constexpr float kCaptionSigma = 4.80f;    // text-shadow 0 0 .5cqw
// The scrim behind it all: black, peaking at this alpha, a Gaussian this wide and this tall.
constexpr float kScrimAlpha = 0.75f;
constexpr float kScrimSigmaX = 250.0f;
constexpr float kScrimSigmaY = 50.0f;

// Each halo is baked at the size where its sigma is a few texels, which keeps its atlas small;
// the sharp title is baked at twice the size it is drawn at 1080, for a retina screen.
constexpr float kTitleBake = 96.0f;    // line height, pixels
constexpr float kTightBake = 40.0f;
constexpr float kSoftBake = 20.0f;
constexpr float kCaptionBake = 12.0f;

// The page's colours: #ECE8DD, #C2B48F, and the hairline's silver at 85%.
constexpr uint32_t silver(float a) { return gfx::rgba(236.0f / 255.0f, 232.0f / 255.0f, 221.0f / 255.0f, a); }
constexpr uint32_t gilt(float a) { return gfx::rgba(194.0f / 255.0f, 180.0f / 255.0f, 143.0f / 255.0f, a); }
constexpr uint32_t black(float a) { return gfx::rgba(0.0f, 0.0f, 0.0f, a); }

struct Place {
    const char* world;
    const char* name;
    const char* caption;
};
// MU 0.75's towns. "\xB7" is the middle dot the faces bake past ASCII.
constexpr Place kPlaces[] = {
    {"lorencia", "Lorencia", "Town \xB7 Safe zone"},
    {"noria", "Noria", "Town \xB7 Safe zone"},
    {"devias", "Devias", "Town \xB7 Safe zone"},
};

std::string upper(std::string s) {
    for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// CSS's cubic-bezier(x1, y1, x2, y2) at x = `u`: solve x(t) = u, return y(t).
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
float settle(float u) { return bezier(0.2f, 0.7f, 0.2f, 1.0f, u); }  // the page's own curve
float easeOut(float u) { return bezier(0.0f, 0.0f, 0.58f, 1.0f, u); }

// Where a keyframe segment [from, to] of the 5.4 s stands at `t`, 0 to 1.
float span(float t, float from, float to) {
    return std::clamp((t - from * Arrival::kSeconds) / ((to - from) * Arrival::kSeconds), 0.0f, 1.0f);
}

bool bakeOne(gfx::Face& face, bgfx::TextureHandle& texture, const char* path, float pixels,
             int size, float sigmaAt1080, float emAt1080, const char* name) {
    // The room a halo needs is only known once the bake has said what its em is, so it is
    // reserved for the largest the em can be -- a line is never shorter than its em -- and the
    // blur itself is then exactly the page's fraction of an em.
    const int spread = sigmaAt1080 > 0.0f ? int(std::ceil(sigmaAt1080 * pixels / emAt1080 * 3.0f)) : 0;
    if (!face.bake(path, pixels, size, 4 + spread * 2, 1, spread)) return false;
    if (sigmaAt1080 > 0.0f) face.blur(sigmaAt1080 / emAt1080 / face.emScale(1.0f));
    texture = gfx::uploadFace(face, name);
    face.dropPixels();
    return bgfx::isValid(texture);
}

}  // namespace

void Arrival::open(const gfx::Interface& interface) {
    interface_ = &interface;
    interface.adopt(canvas_);
    const char* title = gfx::titleFacePath();
    const bool ok =
        bakeOne(title_, titleTexture_, title, kTitleBake, 1024, 0.0f, kNameEm, "arrival title") &&
        bakeOne(titleTight_, tightTexture_, title, kTightBake, 1024, kTightSigma, kNameEm,
                "arrival tight halo") &&
        bakeOne(titleSoft_, softTexture_, title, kSoftBake, 1024, kSoftSigma, kNameEm,
                "arrival soft halo") &&
        bakeOne(captionSoft_, captionTexture_, gfx::facePath(), kCaptionBake, 1024, kCaptionSigma,
                kCaptionEm, "arrival caption halo");
    if (!ok) core::logError("arrival: the title faces did not bake; no map name will show");
}

void Arrival::shutdown() {
    for (bgfx::TextureHandle* t : {&titleTexture_, &tightTexture_, &softTexture_, &captionTexture_}) {
        if (bgfx::isValid(*t)) bgfx::destroy(*t);
        *t = BGFX_INVALID_HANDLE;
    }
}

void Arrival::announce(const std::string& world, float delay) {
    name_.clear();
    caption_.clear();
    for (const Place& p : kPlaces) {
        if (world == p.world) {
            name_ = p.name;
            caption_ = p.caption;
        }
    }
    if (name_.empty() && !world.empty()) {
        name_ = world;
        name_[0] = char(std::toupper(static_cast<unsigned char>(name_[0])));
    }
    name_ = upper(name_);
    caption_ = upper(caption_);
    clock_ = -delay;
    core::logf("arrival: %s in %.1f s", name_.c_str(), double(delay));
}

void Arrival::update(float seconds, float width, float height) {
    if (name_.empty()) return;
    clock_ += seconds;
    if (clock_ >= kSeconds) {
        name_.clear();
        canvas_.clear();
        shown_ = false;
        return;
    }
    if (clock_ < 0.0f || !bgfx::isValid(titleTexture_)) return;
    rebuild(width, height);
}

void Arrival::rebuild(float width, float height) {
    canvas_.clear();
    shown_ = true;
    const float t = clock_;
    const float k = height / kReference;
    const float mid = width * 0.5f;

    // The whole title's opacity: 0 to 1 over 13%, held to 80%, out by 100%, linear.
    const float opacity = t < 0.13f * kSeconds ? span(t, 0.0f, 0.13f)
                                               : 1.0f - span(t, 0.80f, 1.0f);
    const float closing = settle(span(t, 0.0f, 0.22f));
    const float drawn = settle(span(t, 0.06f, 0.24f));
    const float mark = easeOut(span(t, 0.12f, 0.26f));
    const float caption = easeOut(span(t, 0.18f, 0.32f));

    // The stack, from the caption's foot up.
    const float nameEm = kNameEm * k, captionEm = kCaptionEm * k, gap = kGap * k;
    const float markBox = kMarkBox * k;
    const float foot = height * kFoot;
    const float captionTop = foot - captionEm;
    const float rowTop = captionTop - gap - markBox;
    const float nameTop = rowTop - gap - nameEm;
    const float rowMid = rowTop + markBox * 0.5f;

    // ---- the scrim: a soft dark cloud behind the whole stack, so the caption still reads on
    // Lorencia's bright paving. The user's addition on 2026-09-22, after seeing it in the game;
    // not on the design page. A Gaussian each way, laid down as a grid of shaded quads, so it
    // has no edge anywhere and fades with the title.
    {
        const float cy = (nameTop + foot) * 0.5f;
        const float sx = kScrimSigmaX * k, sy = kScrimSigmaY * k;
        constexpr int kColumns = 16, kRows = 8;
        const auto at = [&](int i, int j) {
            const float x = -3.0f + 6.0f * float(i) / kColumns;
            const float y = -3.0f + 6.0f * float(j) / kRows;
            return black(kScrimAlpha * opacity * std::exp(-0.5f * (x * x + y * y)));
        };
        for (int j = 0; j < kRows; ++j) {
            for (int i = 0; i < kColumns; ++i) {
                const float x0 = mid + sx * (-3.0f + 6.0f * float(i) / kColumns);
                const float y0 = cy + sy * (-3.0f + 6.0f * float(j) / kRows);
                canvas_.shade({x0, y0, sx * 6.0f / kColumns, sy * 6.0f / kRows}, at(i, j),
                              at(i + 1, j), at(i + 1, j + 1), at(i, j + 1));
            }
        }
    }

    // ---- the name: centred with its padding-left of one rest tracking, as the page set it.
    {
        const float tracking = (kNameTrackingFrom + (kNameTracking - kNameTrackingFrom) * closing) * nameEm;
        const float pad = kNameTracking * nameEm;
        const float glyphs = title_.measure(nameEm, name_);
        const float total = pad + glyphs + tracking * float(name_.size());
        const float x = mid - total * 0.5f + pad;
        const float baseline =
            nameTop + (nameEm - title_.ascent(nameEm) - title_.descent(nameEm)) * 0.5f +
            title_.ascent(nameEm);
        canvas_.lettered(titleTight_, tightTexture_, x, baseline, nameEm, tracking,
                         black(0.60f * opacity), name_);
        canvas_.lettered(titleSoft_, softTexture_, x, baseline + kSoftDrop * k, nameEm, tracking,
                         black(0.75f * opacity), name_);
        canvas_.lettered(title_, titleTexture_, x, baseline, nameEm, tracking, silver(opacity),
                         name_);
    }

    // ---- the rule: two hairlines drawing outward from the diamond, and their shadows.
    {
        const float line = std::max(1.0f, k);
        const float inner = markBox * 0.5f + kRuleGap * k;
        const float length = kRuleLength * k * drawn;
        const float sigma = kRuleSigma * k;
        for (int side = -1; side <= 1; side += 2) {
            if (length <= 0.0f) break;
            const float near = mid + float(side) * inner;
            const float far = mid + float(side) * (inner + length);
            const float left = std::min(near, far), w = std::fabs(far - near);
            // The box-shadow: the line's box blurred, uniform along it whatever the gradient
            // does, in strips down a Gaussian profile. A 1 px line under a 3.8 px sigma peaks
            // at about 8% black.
            constexpr int kStrips = 8;
            const float reach = sigma * 3.0f;
            const auto profile = [&](float y) {
                const float d = std::fabs(y) / sigma;
                return 0.80f * opacity * std::exp(-0.5f * d * d) * line /
                       (sigma * 2.5066283f);
            };
            for (int i = 0; i < kStrips; ++i) {
                const float y0 = -reach + reach * 2.0f * float(i) / kStrips;
                const float y1 = -reach + reach * 2.0f * float(i + 1) / kStrips;
                const uint32_t a = black(profile(y0)), b = black(profile(y1));
                canvas_.shade({left, rowMid + y0, w, y1 - y0}, a, a, b, b);
            }
            // The line: clear at its far end, 85% silver at the diamond.
            const uint32_t clear = silver(0.0f), full = silver(0.85f * opacity);
            const gfx::Box box{left, rowMid - line * 0.5f, w, line};
            if (side < 0) canvas_.shade(box, clear, full, full, clear);
            else canvas_.shade(box, full, clear, clear, full);
        }
    }

    // ---- the diamond, turning in from -20 degrees at 0.7 of its size, over its drop-shadow.
    if (mark > 0.0f) {
        const float scale = 0.7f + 0.3f * mark;
        const float turn = -20.0f * (1.0f - mark) * 3.14159265f / 180.0f;
        const float c = std::cos(turn), s = std::sin(turn);
        const auto diamond = [&](float half, uint32_t colour) {
            const float corners[4][2] = {{0.0f, -half}, {half, 0.0f}, {0.0f, half}, {-half, 0.0f}};
            float xy[8];
            for (int i = 0; i < 4; ++i) {
                xy[i * 2] = mid + (corners[i][0] * c - corners[i][1] * s) * scale;
                xy[i * 2 + 1] = rowMid + (corners[i][0] * s + corners[i][1] * c) * scale;
            }
            canvas_.polygon(nullptr, xy, nullptr, 4, colour);
        };
        const float half = markBox * 0.45f;
        // The shadow as rings of a blurred edge, outermost first, each layer's alpha chosen so
        // that what has piled up at its distance is the Gaussian edge's: 0.9 * erfc(d / σ√2) / 2.
        const float sigma = kMarkSigma * k;
        const float alpha = 0.90f * opacity * mark;
        constexpr int kRings = 7;
        float below = 0.0f;
        for (int i = kRings; i >= 0; --i) {
            const float d = sigma * 3.0f * float(i) / kRings;
            const float want = alpha * 0.5f * std::erfc(d / (sigma * 1.41421356f));
            const float layer = below >= 1.0f ? 0.0f : 1.0f - (1.0f - want) / (1.0f - below);
            if (layer > 0.0f) diamond(half + d * 1.41421356f, black(layer));
            below = want;
        }
        diamond(half, silver(opacity * mark));
    }

    // ---- the caption, in the windows' own face over its halo.
    if (caption > 0.0f && !caption_.empty()) {
        const gfx::Face& face = canvas_.face();
        const float tracking = kCaptionTracking * captionEm;
        const float total = tracking + face.measure(captionEm, caption_) +
                            tracking * float(caption_.size());
        const float x = mid - total * 0.5f + tracking;
        const float baseline =
            captionTop + (captionEm - face.ascent(captionEm) - face.descent(captionEm)) * 0.5f +
            face.ascent(captionEm);
        const float a = opacity * caption;
        canvas_.lettered(captionSoft_, captionTexture_, x, baseline, captionEm, tracking,
                         black(0.90f * a), caption_);
        canvas_.lettered(face, interface_->faceTexture(), x, baseline, captionEm, tracking,
                         gilt(a), caption_);
    }
}

}  // namespace mu::game
