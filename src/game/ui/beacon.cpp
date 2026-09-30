#include "game/ui/beacon.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "game/play.h"
#include "game/ui/tip.h"

namespace mu::game {

namespace {

// In the interface's own units: one pixel of a 1080-line screen (tip::unit).
constexpr float kCellW = 22.0f, kCellH = 50.0f;
constexpr float kMid = kCellW * 0.5f;
// The blade: a flat top with its corners cut, tapering to a point. Offsets from kMid, in units.
constexpr float kTop = 6.0f, kTopHalf = 2.4f;        // the flat of the top
constexpr float kShoulder = 7.6f, kShoulderHalf = 3.9f;  // where the cut corners meet the sides
constexpr float kPoint = 36.0f;                          // the tip
// The dot, round and small.
constexpr float kDotY = 42.5f, kDotR = 2.7f;
constexpr float kRim = 0.9f;        // the hairline edge
constexpr float kHalo = 3.2f;       // the dark halo round it, soft, for bright ground
constexpr float kEdgeLight = 0.7f;  // the pale edge on the lit side, inside the rim
constexpr float kGrain = 0.06f;     // the wear in the metal, either way
// And a drop shadow, the same user the same day ("some minimal drop shadow"): the glyph again,
// down and to the right, dark and a little soft, under the halo.
constexpr float kShadowX = 1.3f, kShadowY = 1.6f, kShadowSoft = 1.2f, kShadowAlpha = 0.55f;

// Where it hangs: close over the crown, and lifted clear of the name while the name shows there
// (24 units), by the name's own fade. Then the bob.
constexpr float kLift = 4.0f;
constexpr float kLiftNamed = 24.0f;
constexpr float kBob = 1.5f;  // units, either way
constexpr float kBobHz = 0.45f;

// The whole mark against the interface unit.
constexpr float kSize = 1.05f;

// Old gold rather than yellow: the lit face, the shadowed face, the pale edge and the ink.
struct Rgb {
    float r, g, b;
};
// Brighter since the user's "very calm, need to make little brighter" and "brighter gold
// color" (2026-09-29): a bright gold on the lit face, the shaded one lifted to a warm amber, the
// edge nearly white.
constexpr Rgb kLit = {1.00f, 0.88f, 0.42f};
constexpr Rgb kShade = {0.90f, 0.66f, 0.22f};
constexpr Rgb kEdge = {1.00f, 0.98f, 0.86f};
constexpr Rgb kInk = {0.07f, 0.045f, 0.025f};
// A quest still to come -- Sevina's, before its words are written (the user, 2026-09-30): the
// same "!" gone to cold iron, lit and shaded as the gold one is, so it reads as "not yet".
constexpr Rgb kLitLater = {0.70f, 0.70f, 0.68f};
constexpr Rgb kShadeLater = {0.50f, 0.50f, 0.49f};
constexpr Rgb kEdgeLater = {0.86f, 0.86f, 0.84f};

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

// Inigo Quilez's polygon distance: negative inside, any simple polygon.
template <int N>
float polygon(float px, float py, const float (&v)[N][2]) {
    float dx = px - v[0][0], dy = py - v[0][1];
    float d = dx * dx + dy * dy;
    float s = 1.0f;
    for (int i = 0, j = N - 1; i < N; j = i, ++i) {
        const float ex = v[j][0] - v[i][0], ey = v[j][1] - v[i][1];
        const float wx = px - v[i][0], wy = py - v[i][1];
        const float t = clamp01((wx * ex + wy * ey) / (ex * ex + ey * ey));
        const float bx = wx - ex * t, by = wy - ey * t;
        d = std::min(d, bx * bx + by * by);
        const bool c0 = py >= v[i][1], c1 = py < v[j][1], c2 = ex * wy > ey * wx;
        if ((c0 && c1 && c2) || (!c0 && !c1 && !c2)) s = -s;
    }
    return s * std::sqrt(d);
}

constexpr float kBlade[5][2] = {
    {kMid - kTopHalf, kTop},           {kMid + kTopHalf, kTop},
    {kMid + kShoulderHalf, kShoulder}, {kMid, kPoint},
    {kMid - kShoulderHalf, kShoulder},
};

// The hand-in's "?": a hook of a ring open at its lower left, a stem down from the hook's foot,
// and the same dot. Its stroke is the blade's width at the shoulder.
constexpr float kHookR = 6.4f, kHookHalf = 1.9f;
constexpr float kHookY = kTop + kHookR + kHookHalf;  // the ring's centre: its top on the blade's
constexpr float kStemEnd = 33.0f;
constexpr float kGapFrom = 1.5708f, kGapTo = 3.7525f;  // the opening, 90 to 215 degrees, y down

float hook(float x, float y) {
    const float dx = x - kMid, dy = y - kHookY;
    float a = std::atan2(dy, dx);
    if (a < 0.0f) a += 6.2831853f;
    float d;
    if (a > kGapFrom && a < kGapTo) {
        // In the opening: the nearer of its two round ends.
        const auto end = [&](float at) {
            const float ex = x - (kMid + kHookR * std::cos(at)), ey = y - (kHookY + kHookR * std::sin(at));
            return std::sqrt(ex * ex + ey * ey);
        };
        d = std::min(end(kGapFrom), end(kGapTo)) - kHookHalf;
    } else {
        d = std::fabs(std::sqrt(dx * dx + dy * dy) - kHookR) - kHookHalf;
    }
    // The stem, a capsule from the hook's foot down.
    const float sy = std::clamp(y, kHookY + kHookR, kStemEnd);
    const float stem = std::sqrt(dx * dx + (y - sy) * (y - sy)) - kHookHalf;
    return std::min(d, stem);
}

// The mark's distance, in units, from a point in the cell: negative inside. `ask` is the "?",
// else the "!".
float mark(float x, float y, bool ask) {
    const float dx = x - kMid, dy = y - kDotY;
    return std::min(ask ? hook(x, y) : polygon(x, y, kBlade), std::sqrt(dx * dx + dy * dy) - kDotR);
}

// Fine white noise, one value a texel, for the wear.
float grain(int x, int y) {
    uint32_t h = uint32_t(x) * 0x8da6b343u ^ uint32_t(y) * 0xd8163841u ^ 0x5eedu;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return float(h & 0xFFFFu) / 65535.0f * 2.0f - 1.0f;
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
    // Four cells side by side: the offer's "!", the hand-in's "?", and the same two in grey --
    // a quest still to come, and one under way.
    const int wide = cellW_ * 4;
    std::vector<uint8_t> rgba(size_t(wide) * size_t(cellH_) * 4, 0);
    const float texel = 1.0f / unit;  // one texel, in units
    constexpr int kSide = 4;          // samples a side
    // How much of a sample a shape at distance `d` covers: a texel's worth of edge, so the
    // shapes stay crisp and only their edges are soft.
    const auto cover = [&](float d) { return clamp01(0.5f - d / texel); };
    for (int py = 0; py < cellH_; ++py) {
        for (int column = 0; column < wide; ++column) {
            const int cell = column / cellW_;
            const bool ask = cell == 1 || cell == 3, later = cell >= 2;
            const int px = column - cell * cellW_;
            Pre sum;
            for (int sy = 0; sy < kSide; ++sy) {
                for (int sx = 0; sx < kSide; ++sx) {
                    const float x = (float(px) + (float(sx) + 0.5f) / kSide) * texel;
                    const float y = (float(py) + (float(sy) + 0.5f) / kSide) * texel;
                    const float d = mark(x, y, ask);
                    Pre one;
                    const float cast = mark(x - kShadowX, y - kShadowY, ask);
                    one.over(kInk, kShadowAlpha * clamp01(0.5f - cast / kShadowSoft));
                    // The halo, soft, and the hairline, crisp.
                    const float halo = clamp01(1.0f - (d - kRim) / kHalo);
                    one.over(kInk, 0.32f * halo * halo);
                    one.over(kInk, 0.85f * cover(d - kRim));
                    const float inside = cover(d);
                    if (inside > 0.0f) {
                        // Forged: a ridge down the middle, the left face lit and the right in
                        // shade, each a little deeper toward the point, and worn.
                        const bool lit = x < kMid;
                        const float fall = 1.0f - 0.18f * clamp01((y - kTop) / (kDotY - kTop));
                        const float wear = 1.0f + kGrain * grain(px, py);
                        const Rgb& face = later ? (lit ? kLitLater : kShadeLater)
                                                : (lit ? kLit : kShade);
                        one.over({clamp01(face.r * fall * wear), clamp01(face.g * fall * wear),
                                  clamp01(face.b * fall * wear)},
                                 inside);
                        // The pale edge along the lit side and the flat of the top.
                        if (lit || y < kShoulder) {
                            one.over(later ? kEdgeLater : kEdge,
                                     0.8f * inside * clamp01(1.0f + d / kEdgeLight));
                        }
                    }
                    sum.r += one.r;
                    sum.g += one.g;
                    sum.b += one.b;
                    sum.a += one.a;
                }
            }
            constexpr float kShare = 1.0f / float(kSide * kSide);
            uint8_t* out = &rgba[(size_t(py) * size_t(wide) + size_t(column)) * 4];
            const float a = clamp01(sum.a * kShare);
            const float inv = a > 0.0f ? kShare / a : 0.0f;
            out[0] = uint8_t(clamp01(sum.r * inv) * 255.0f + 0.5f);
            out[1] = uint8_t(clamp01(sum.g * inv) * 255.0f + 0.5f);
            out[2] = uint8_t(clamp01(sum.b * inv) * 255.0f + 0.5f);
            out[3] = uint8_t(a * 255.0f + 0.5f);
        }
    }
    texture_ = bgfx::createTexture2D(uint16_t(wide), uint16_t(cellH_), false, 1,
                                     bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_UVW_CLAMP,
                                     bgfx::copy(rgba.data(), uint32_t(rgba.size())));
    if (!bgfx::isValid(texture_)) return false;
    bgfx::setName(texture_, "quest marker");
    art_ = {texture_, float(wide), float(cellH_)};
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

void Beacon::update(float seconds, const Play& play, int named, float shown,
                    const float* viewProj, int width, int height) {
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
    const sim::Realm& realm = play.realm();
    for (int folk : play.questGivers()) {
        // While he has something for the hero or is waiting on him: the quest on offer, under
        // way, or its hand-in. Resting until it is his to give again, he is a townsperson.
        const int quest = realm.questHere(realm.tables()->folk[size_t(folk)].number);
        const bool ready = quest >= 0 && realm.quest(quest).state == sim::QuestState::Ready;
        // No quest of his in the table, or his waiting on another -- Devin's, until Lorencia or
        // Noria is cleared: one still to come (Play::questGivers), a grey "!". And
        // his quest taken and under way, a grey "?", as WoW marks one not yet done (the user,
        // 2026-09-30: Devin's mark was gone once his quest was taken).
        const bool later = quest < 0 || realm.questLocked(quest);
        const bool underway = quest >= 0 && realm.quest(quest).state == sim::QuestState::Active;
        if (quest >= 0 && !realm.questOffered(quest) && !ready && !underway && !later) continue;
        float x = 0.0f, y = 0.0f;
        if (!play.folkCrownOf(folk, viewProj, width, height, &x, &y)) continue;
        const float w = float(cellW_), h = float(cellH_);
        // Risen by the name's own fade, eased on the same clock: up as the name comes, held
        // while it lingers, down as it goes.
        const float t = folk == named ? std::clamp(shown, 0.0f, 1.0f) : 0.0f;
        const float lift = kLift + (kLiftNamed - kLift) * t * t * (3.0f - 2.0f * t);
        canvas_.region(art_, {std::round(x - w * 0.5f), y - lift * u - h + bob, w, h},
                       {ready ? w : later ? w * 2.0f : underway ? w * 3.0f : 0.0f, 0.0f, w, h});
        showing_ = true;
    }
}

}  // namespace mu::game
