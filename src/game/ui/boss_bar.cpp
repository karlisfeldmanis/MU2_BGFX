#include "game/ui/boss_bar.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "game/play.h"
#include "game/ui/controls.h"
#include "game/ui/panel.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"

namespace mu::game {
namespace {

using gfx::Box;

// The herald's grammar (game/ui/herald.cpp), the user, 2026-10-06: 'finetune it to match our
// style'. Its scale, its soft black band clear at both ends, its hairlines, Cinzel capitals for
// the name and the label face for figures; Sanctuary's inks (game/ui/style.h): bone for words,
// the life red for the bar, iron for the notches and the rule, a danger word for a move told --
// and no gold, which is loot's. In tip::unit() at the herald's kScale.
constexpr float kScale = 0.65f;
constexpr float kTop = 70.0f;       // under the herald's band (12 + 52) and a breath of air
constexpr float kWide = 880.0f, kTall = 96.0f;
constexpr float kBarWide = 560.0f, kBarTall = 16.0f;
constexpr float kNameSize = 20.0f, kNameTrack = 0.14f;
constexpr float kFigureSize = 17.0f;
constexpr float kLineSize = 16.0f;
constexpr float kGap = 18.0f;
constexpr float kRuleTall = 14.0f;
constexpr float kToldTall = 2.0f;
// The band, as the herald's: black across, clear at the ends.
constexpr float kBandAt[5] = {0.0f, 0.22f, 0.5f, 0.78f, 1.0f};
constexpr float kBandAlpha[5] = {0.0f, 0.55f, 0.78f, 0.55f, 0.0f};
// The raiders' small bars, in tip::unit().
constexpr float kRaiderWide = 48.0f, kRaiderTall = 5.0f, kRaiderName = 16.0f;
constexpr float kFadeSeconds = 0.4f;
constexpr float kDrain = 0.6f;  // the trail, of the whole bar a second

uint32_t faded(uint32_t abgr, float a) {
    a = std::clamp(a, 0.0f, 1.0f);
    return (abgr & 0x00FFFFFFu) | (gfx::rgbaByte(a * float(abgr >> 24) / 255.0f) << 24);
}

// A face and how a line in it is set, as the herald's Type.
struct Type {
    const gfx::Face* face = nullptr;
    bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
    float size = 0.0f;
    float track = 0.0f;
    char ink = 'H';
    float width(const std::string& text) const {
        if (!face || text.empty()) return 0.0f;
        return face->measure(size, text) + track * float(text.size() - 1);
    }
    float baseline(float mid) const {
        const gfx::FaceGlyph* g = face ? face->glyph(ink) : nullptr;
        const float tall = g ? -g->y0 * face->emScale(size) : size * 0.7f;
        return std::round(mid + tall * 0.5f);
    }
    void draw(gfx::Canvas& canvas, float x, float mid, uint32_t colour, float alpha,
              const std::string& text) const {
        if (!face || text.empty()) return;
        const float base = baseline(mid);
        x = std::round(x);
        canvas.lettered(*face, texture, x + 1.0f, base + 1.0f, size, track, faded(style::kDrop, alpha), text);
        canvas.lettered(*face, texture, x, base, size, track, faded(colour, alpha), text);
    }
    void centred(gfx::Canvas& canvas, float x, float mid, uint32_t colour, float alpha,
                 const std::string& text) const {
        draw(canvas, x - width(text) * 0.5f, mid, colour, alpha, text);
    }
};

// The herald's hairline: iron at its ends of light, gone at both ends of the band.
void hairline(gfx::Canvas& canvas, const Box& band, float y, float thick, float alpha) {
    constexpr int kStops = 5;
    const float at[kStops] = {0.0f, 0.12f, 0.5f, 0.88f, 1.0f};
    const uint32_t clear = faded(style::kIron, 0.0f);
    const uint32_t ink[kStops] = {clear, faded(style::kIronLo, 0.7f * alpha), faded(style::kIron, 0.9f * alpha),
                                  faded(style::kIronLo, 0.7f * alpha), clear};
    for (int i = 0; i + 1 < kStops; ++i) {
        const float x0 = band.x + band.w * at[i], x1 = band.x + band.w * at[i + 1];
        canvas.shade({x0, y, x1 - x0, thick}, ink[i], ink[i + 1], ink[i + 1], ink[i]);
    }
}

const char* stageName(sim::RaidStage stage) {
    switch (stage) {
        case sim::RaidStage::Ground: return "stage one";
        case sim::RaidStage::Flight: return "stage two \xB7 flight";
        case sim::RaidStage::Enraged: return "stage three \xB7 enraged";
        case sim::RaidStage::LastStand: return "stage four \xB7 last stand";
        default: return "";
    }
}

const char* moveName(sim::HazardKind kind) {
    switch (kind) {
        case sim::HazardKind::Breath: return "fire breath";
        case sim::HazardKind::Shock: return "roar";
        case sim::HazardKind::Inferno: return "golden inferno";
        default: return nullptr;
    }
}

}  // namespace

void BossBar::update(float seconds, const Play& play, const float* viewProj, int width, int height) {
    canvas_.clear();
    const sim::Realm& realm = play.realm();
    const sim::Body* dragon = realm.invader();
    const bool up = dragon != nullptr && play.raidFighting();
    shown_ = up ? std::min(1.0f, shown_ + seconds / kFadeSeconds) : std::max(0.0f, shown_ - seconds / kFadeSeconds);
    const float u = tip::unit() * kScale;
    const float hair = std::max(1.0f, std::round(u));

    // The raiders' bars, over each one standing: the green of a thing that fits (style::kFits),
    // its name in the label face over it.
    const Type raiderName{controls::labelFace(), controls::labelTexture(), kRaiderName * u, 0.0f, 'H'};
    for (int i = 0; i < realm.raiderCount(); ++i) {
        const sim::Body* raider = realm.raiderAt(i);
        float x = 0.0f, y = 0.0f;
        if (!raider || !raider->alive() || raider->maxHealth <= 0 ||
            !play.crownOf(raider->id, viewProj, width, height, &x, &y)) {
            continue;
        }
        const float w = std::round(kRaiderWide * u), h = std::max(2.0f, std::round(kRaiderTall * u));
        const Box bar{std::round(x - w * 0.5f), std::round(y - h - 6.0f * u), w, h};
        canvas_.rect(bar.grown(hair), faded(style::kSeam, 0.85f));
        canvas_.rect(bar, style::kVoid);
        const float share = std::clamp(float(play.shownHealth(raider->id)) / float(raider->maxHealth), 0.0f, 1.0f);
        canvas_.rect({bar.x, bar.y, std::round(bar.w * share), bar.h}, style::kFits);
        if (i + 1 < int(play.raidParty().size())) {
            raiderName.centred(canvas_, x, bar.y - 9.0f * u, style::kBone2, 0.9f, play.raidParty()[size_t(i + 1)].name);
        }
    }

    if (shown_ <= 0.0f || dragon == nullptr) return;
    const float a = shown_ * shown_ * (3.0f - 2.0f * shown_);
    const float share = dragon->maxHealth > 0
                            ? std::clamp(float(play.shownHealth(dragon->id)) / float(dragon->maxHealth), 0.0f, 1.0f)
                            : 0.0f;
    lag_ = share > lag_ ? share : std::max(share, lag_ - kDrain * seconds);

    // The band and its hairlines.
    const Box band{std::round((float(width) - kWide * u) * 0.5f), std::round(kTop * u), std::round(kWide * u),
                   std::round(kTall * u)};
    for (int i = 0; i + 1 < 5; ++i) {
        const float x0 = band.x + band.w * kBandAt[i], x1 = band.x + band.w * kBandAt[i + 1];
        const uint32_t l = faded(style::kSeam, kBandAlpha[i] * a), r = faded(style::kSeam, kBandAlpha[i + 1] * a);
        canvas_.shade({x0, band.y, x1 - x0, band.h}, l, r, r, l);
    }
    hairline(canvas_, band, band.y, hair, a);
    hairline(canvas_, band, band.bottom() - hair, hair, a * 0.55f);

    // The name, in Cinzel capitals.
    Type name{panel::titleFace(), panel::titleTexture(), kNameSize * u, 0.0f, 'H'};
    name.track = name.size * kNameTrack;
    const float nameMid = band.y + 20.0f * u;
    name.centred(canvas_, band.midX(), nameMid, style::kBoneHi, a, "GOLDEN DRAGON");

    // The bar: a seam, the void, the trail in bone, the life, the notches in iron, a gloss.
    const float w = std::round(kBarWide * u), h = std::round(kBarTall * u);
    const Box bar{std::round(band.midX() - w * 0.5f), std::round(band.y + 38.0f * u), w, h};
    canvas_.rect(bar.grown(hair), faded(style::kSeam, a));
    canvas_.rect(bar, faded(style::kVoid, a));
    canvas_.rect({bar.x, bar.y, std::round(bar.w * lag_), bar.h}, faded(style::kBone, 0.45f * a));
    canvas_.rect({bar.x, bar.y, std::round(bar.w * share), bar.h}, faded(style::kLife, a));
    canvas_.shade({bar.x, bar.y, bar.w, std::round(bar.h * 0.4f)}, gfx::rgba(1, 1, 1, 0.10f * a),
                  gfx::rgba(1, 1, 1, 0.10f * a), gfx::rgba(1, 1, 1, 0.0f), gfx::rgba(1, 1, 1, 0.0f));
    for (const int at : {sim::kFlightAt, sim::kEnragedAt, sim::kLastStandAt}) {
        canvas_.rect({std::round(bar.x + bar.w * float(at) / 100.0f), bar.y, hair, bar.h}, faded(style::kIronDk, a));
    }
    {
        char reading[48];
        std::snprintf(reading, sizeof(reading), "%d / %d", play.shownHealth(dragon->id), dragon->maxHealth);
        const Type figures{controls::labelFace(), controls::labelTexture(), kFigureSize * u, 0.0f, '0'};
        figures.centred(canvas_, bar.midX(), bar.midY(), style::kBoneHi, 0.92f * a, reading);
    }

    // Under it: the stage in small capitals, an upright iron rule, the enrage's clock -- or, while
    // a move is told, its name as a danger word over a hairline filling to the blow.
    const Type word{controls::wordFace(), controls::wordTexture(), kLineSize * u, 0.0f, 'x'};
    const Type figure{controls::labelFace(), controls::labelTexture(), kLineSize * u, 0.0f, '0'};
    const float lineMid = bar.bottom() + 16.0f * u;
    const sim::Hazard* told = nullptr;
    for (int k = 0; k < sim::kHazards; ++k) {
        const sim::Hazard& one = realm.hazards()[k];
        if (one.kind == sim::HazardKind::None || moveName(one.kind) == nullptr || realm.tick() >= one.landsAt) continue;
        if (told == nullptr || one.landsAt < told->landsAt) told = &one;
    }
    if (told != nullptr) {
        const std::string move = moveName(told->kind);
        word.centred(canvas_, bar.midX(), lineMid, style::kDanger, a, move);
        const float whole = told->kind == sim::HazardKind::Breath  ? float(sim::kBreathTell)
                            : told->kind == sim::HazardKind::Shock ? float(sim::kShockTell)
                                                                   : float(sim::kInfernoTell);
        const float done = std::clamp(1.0f - float(told->landsAt - realm.tick()) / whole, 0.0f, 1.0f);
        const float tw = std::round(word.width(move) + 24.0f * u), th = std::max(1.0f, std::round(kToldTall * u));
        const Box cast{std::round(bar.midX() - tw * 0.5f), std::round(lineMid + 10.0f * u), tw, th};
        canvas_.rect(cast, faded(style::kIronDk, a));
        canvas_.rect({cast.x, cast.y, std::round(cast.w * done), cast.h}, faded(style::kBlood, a));
    } else {
        const int64_t left = std::max<int64_t>(0, sim::kHardEnrage - (realm.tick() - realm.raidLandedAt()));
        char clock[24];
        std::snprintf(clock, sizeof(clock), "%lld:%02lld", (long long)(left / 20 / 60), (long long)(left / 20 % 60));
        const std::string stage = stageName(realm.raidStage());
        const float gap = kGap * u;
        const float wide = word.width(stage) + gap + hair + gap + figure.width(clock);
        float x = bar.midX() - wide * 0.5f;
        word.draw(canvas_, x, lineMid, style::kBone2, a, stage);
        x += word.width(stage) + gap;
        const float top = std::round(lineMid - kRuleTall * u * 0.5f), half = kRuleTall * u * 0.5f;
        const uint32_t clear = faded(style::kIron, 0.0f), iron = faded(style::kIron, a);
        canvas_.shade({std::round(x), top, hair, half}, clear, clear, iron, iron);
        canvas_.shade({std::round(x), top + half, hair, half}, iron, iron, clear, clear);
        x += hair + gap;
        // The last minute in the danger word's ink.
        figure.draw(canvas_, x, lineMid, left > 60 * 20 ? style::kAshInk : style::kDanger, a, clock);
    }
}

}  // namespace mu::game
