#include "game/ui/tally.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include <bx/math.h>

#include "core/log.h"
#include "game/play.h"
#include "game/ui/panel.h"

namespace mu::game {
namespace {

// ---- the ramp (design page 2026-09-23, concept B2) ------------------------------------------
// Sizes in interface units, which panel::scale() turns into pixels -- 2 at 1080 lines, so a
// swing is 46 px there and the same fraction of the screen at any other height. The page was
// drawn at 1080 and its numbers came straight across.
constexpr float kSwingSize = 23.0f;
constexpr float kSkillSize = 30.0f;
constexpr float kCriticalSize = 35.0f;
// A miss and the shield's share are the quiet end of the ramp: they are read, not felt.
constexpr float kSmallSize = 17.0f;
// Barlow at 700 wants a little air between figures; the page's letter-spacing, in ems.
constexpr float kTracking = 0.02f;

// The motion, all of it in screen pixels on a point projected from the world. A figure hangs
// where the blow landed and rises from there, so it never slides with the camera and never
// grows as the camera comes in -- both of which the sprite digits did, and both of which read
// as the number belonging to the screen rather than to the body.
constexpr float kRise = 62.0f;    // interface units over the whole life
constexpr float kLean = 13.0f;    // sideways, so two blows in a row are not one figure twice
constexpr float kHold = 0.66f;    // of the life at full opacity, then out
// A row for each figure already standing over that body when this one went up -- Showing::land
// counts them. One line of the swing size, which is what keeps two readings apart without
// either of them leaving the body they belong to.
constexpr float kStackStep = 21.0f;
// The pop: up to 1.08 and settled by a quarter of the way through. Small on purpose -- the
// page's own, and what makes it read as a blow landing rather than as a thing being announced.
constexpr float kPopTo = 1.08f;
constexpr float kPopFrom = 0.82f;
constexpr float kPopAt = 0.12f;
constexpr float kSettledAt = 0.24f;

// The halo: the face blurred and laid under the figure in black, because a figure has to hold
// over Lorencia's paving and over its night grass without an outline to thicken it. The page's
// `0 1px 2px rgba(0,0,0,.9), 0 0 7px rgba(0,0,0,.55)`, as one Gaussian and one drop.
constexpr float kHaloSigmaEm = 0.15f;
constexpr float kHaloAlpha = 0.82f;
constexpr float kHaloDrop = 1.0f;  // interface units, the page's 1 px at 1080

// ---- the lane over the HUD ------------------------------------------------------------------
// Small, and smaller than the page drew it: the lane is income and not a blow, and the user
// asked for it quieter on 2026-09-23. A figure here is half a swing's size.
constexpr float kLaneSize = 11.0f;
constexpr float kLaneUnitSize = 8.0f;
constexpr float kLaneUnitTracking = 0.2f;  // uppercase wants it, at the page's 0.2 em
constexpr float kLaneGapAboveHud = 14.0f;  // clear of the plate's top edge
constexpr float kLaneRowGap = 3.0f;
constexpr float kLaneWordGap = 7.0f;
constexpr float kRowLife = 2.1f;
constexpr float kRowIn = 0.10f, kRowOut = 0.72f;  // of the life
constexpr float kRowLift = 8.0f;                  // it comes up into place, in units
// Zen is summed for this long and posted once. A hunt pays a pile a body and a good one is a
// pile a second; a figure for each is a slot machine, and one figure a second is income.
constexpr float kZenRollUp = 1.0f;

// ---- the palette ----------------------------------------------------------------------------
// Bone, amber, gold: the ramp. Red for what lands on HIM, whatever threw it -- his own pain is
// one reading and not four. Blue for the shield's share. MU's own orange is gone from the
// fight, and this is the one place the page departs from the client on purpose.
constexpr uint32_t byteColour(int r, int g, int b, float a = 1.0f) {
    return gfx::rgba(float(r) / 255.0f, float(g) / 255.0f, float(b) / 255.0f, a);
}
uint32_t withAlpha(uint32_t abgr, float alpha) {
    return (abgr & 0x00FFFFFFu) | (gfx::rgbaByte(float(abgr >> 24) / 255.0f * alpha) << 24);
}

const uint32_t kSwingInk = byteColour(240, 231, 214);
const uint32_t kSkillInk = byteColour(255, 195, 77);
const uint32_t kCriticalInk = byteColour(255, 224, 138);
const uint32_t kTakenInk = byteColour(239, 74, 60);
const uint32_t kAbsorbedInk = byteColour(111, 182, 255);
const uint32_t kMissInk = byteColour(207, 199, 184);
const uint32_t kMissOnHeroInk = byteColour(236, 228, 214);
// The lane's own inks, and they are deliberately not the fight's: experience is the ramp's
// bone dimmed, because a colour of its own made the quietest thing on screen the brightest.
// Zen keeps gold, since that is what Zen is; the potions keep enough hue to be told apart.
const uint32_t kExperienceInk = byteColour(222, 213, 196);
const uint32_t kZenInk = byteColour(231, 201, 130);
const uint32_t kHealthInk = byteColour(134, 201, 143);
const uint32_t kManaInk = byteColour(138, 166, 224);
const uint32_t kHalo = gfx::rgba(0.0f, 0.0f, 0.0f, kHaloAlpha);

float sizeOf(Mark mark) {
    switch (mark) {
        case Mark::Skill: return kSkillSize;
        case Mark::Critical: return kCriticalSize;
        case Mark::Absorbed:
        case Mark::Miss: return kSmallSize;
        case Mark::Swing:
        case Mark::Taken: break;
    }
    return kSwingSize;
}

uint32_t inkOf(const Showing::Figure& figure) {
    switch (figure.mark) {
        case Mark::Skill: return kSkillInk;
        case Mark::Critical: return kCriticalInk;
        case Mark::Taken: return kTakenInk;
        case Mark::Absorbed: return kAbsorbedInk;
        case Mark::Miss: return figure.onHero ? kMissOnHeroInk : kMissInk;
        case Mark::Swing: break;
    }
    return kSwingInk;
}

// CSS's cubic-bezier(x1, y1, x2, y2) at x = u, by bisection. Arrival solves the same equation
// for the map name's curves; a dozen lines twice is better than a header shared between two
// windows that have nothing else in common.
float bezier(float x1, float y1, float x2, float y2, float u) {
    u = std::clamp(u, 0.0f, 1.0f);
    const auto at = [](float a, float b, float t) {
        const float s = 1.0f - t;
        return 3.0f * s * s * t * a + 3.0f * s * t * t * b + t * t * t;
    };
    float lo = 0.0f, hi = 1.0f, t = u;
    for (int i = 0; i < 32; ++i) {
        t = (lo + hi) * 0.5f;
        if (at(x1, x2, t) < u) lo = t;
        else hi = t;
    }
    return at(y1, y2, t);
}
// The page's own easing on the rise: cubic-bezier(.2,.8,.3,1) -- most of the travel early, and
// it is still moving when it goes, which is what stops a figure looking parked.
float ridden(float u) { return bezier(0.2f, 0.8f, 0.3f, 1.0f, u); }

// The pop, as a multiplier on the size: up and settled inside a quarter of the life.
float popped(float u) {
    if (u <= 0.0f) return kPopFrom;
    if (u < kPopAt) return kPopFrom + (kPopTo - kPopFrom) * (u / kPopAt);
    if (u < kSettledAt) return kPopTo + (1.0f - kPopTo) * ((u - kPopAt) / (kSettledAt - kPopAt));
    return 1.0f;
}

// Full until the hold is up, then out. The page's opacity track exactly.
float faded(float u) {
    if (u < kHold) return 1.0f;
    return std::clamp(1.0f - (u - kHold) / (1.0f - kHold), 0.0f, 1.0f);
}

// A bake and its texture, the arrival's own helper: the room a halo needs is only known once
// the bake has said what its em is, so it is reserved for the largest the em can be.
bool bakeOne(gfx::Face& face, bgfx::TextureHandle& texture, const char* path, float pixels,
             int size, float sigmaEm, const char* name) {
    const int spread = sigmaEm > 0.0f ? int(std::ceil(sigmaEm * pixels * 3.0f)) : 0;
    if (!face.bake(path, pixels, size, 4 + spread * 2, 1, spread)) return false;
    if (sigmaEm > 0.0f) face.blur(sigmaEm / face.emScale(1.0f));
    texture = gfx::uploadFace(face, name);
    face.dropPixels();
    return bgfx::isValid(texture);
}

// The sharp face is baked well above the size it is drawn at 1080, because this machine's
// backbuffer is 1894 lines and a critical there is 122 px; the halo is baked small, since a
// blur has no detail to lose.
constexpr float kSharpBake = 128.0f;
constexpr float kHaloBake = 28.0f;

}  // namespace

void Tally::open(const gfx::Interface& interface) {
    interface.adopt(canvas_);
    const char* path = gfx::figureFacePath();
    const bool ok = bakeOne(face_, faceTexture_, path, kSharpBake, 1024, 0.0f, "tally figures") &&
                    bakeOne(halo_, haloTexture_, path, kHaloBake, 512, kHaloSigmaEm, "tally halo");
    if (!ok) {
        core::logError("tally: the figures' face did not bake; a blow will show no number");
    }
}

void Tally::shutdown() {
    for (bgfx::TextureHandle* t : {&faceTexture_, &haloTexture_}) {
        if (bgfx::isValid(*t)) bgfx::destroy(*t);
        *t = BGFX_INVALID_HANDLE;
    }
    dismiss();
}

void Tally::dismiss() {
    lane_.clear();
    heldZen_ = 0;
    zenAge_ = 0.0f;
    canvas_.clear();
    drawn_ = false;
}

void Tally::collect(const Play& play, float seconds) {
    for (const Play::Gain& gain : play.gains()) {
        switch (gain.kind) {
            case Play::Gain::Kind::Zen:
                // Held, not posted: see kZenRollUp.
                if (heldZen_ == 0) zenAge_ = 0.0f;
                heldZen_ += gain.value;
                break;
            case Play::Gain::Kind::Experience:
                lane_.push_back({Row::Kind::Experience, gain.value, 0.0f});
                break;
            case Play::Gain::Kind::Health:
                lane_.push_back({Row::Kind::Health, gain.value, 0.0f});
                break;
            case Play::Gain::Kind::Mana:
                lane_.push_back({Row::Kind::Mana, gain.value, 0.0f});
                break;
        }
    }
    if (heldZen_ > 0) {
        zenAge_ += seconds;
        if (zenAge_ >= kZenRollUp) {
            lane_.push_back({Row::Kind::Zen, heldZen_, 0.0f});
            heldZen_ = 0;
            zenAge_ = 0.0f;
        }
    }
    for (size_t i = 0; i < lane_.size();) {
        lane_[i].age += seconds;
        if (lane_[i].age >= kRowLife) {
            // In order, unlike the figures: the lane is a stack and swapping with the back
            // would have a row jump down the screen as an older one above it went.
            lane_.erase(lane_.begin() + long(i));
            continue;
        }
        ++i;
    }
}

void Tally::update(float seconds, const Play& play, const float* viewProj, int width, int height,
                   float hudTop) {
    if (!play.isOpen()) {
        if (drawn_) dismiss();
        return;
    }
    collect(play, seconds);
    const bool anything = !play.showing().figures().empty() || !lane_.empty();
    if (!anything) {
        // Cleared once, then left alone: a town with nothing happening in it rebuilds nothing.
        if (drawn_) {
            canvas_.clear();
            drawn_ = false;
        }
        return;
    }
    // Everything here moves every frame -- a figure rises and fades, a row comes up and goes --
    // so this is one of the two things in the interface that really is redrawn each frame. It
    // is a dozen figures of four glyphs; the windows' own rule (redraw on change) still holds
    // for everything that HAS an unchanged state to sit in.
    rebuild(play, viewProj, width, height, hudTop);
    drawn_ = true;
    ++rebuilds_;
}

void Tally::rebuild(const Play& play, const float* viewProj, int width, int height,
                    float hudTop) {
    canvas_.clear();
    if (!bgfx::isValid(faceTexture_)) return;
    const float unit = panel::scale();

    // A line of figures: the halo first, dropped a pixel, then the face over it.
    const auto write = [&](float centreX, float baseline, float size, uint32_t ink,
                           const std::string& text, float trackingEm) {
        const float tracking = trackingEm * size;
        const float wide = face_.measure(size, text) + tracking * float(text.size() - 1);
        const float x = centreX - wide * 0.5f;
        if (bgfx::isValid(haloTexture_)) {
            canvas_.lettered(halo_, haloTexture_, x, baseline + kHaloDrop * unit, size, tracking,
                             withAlpha(kHalo, float(ink >> 24) / 255.0f), text);
        }
        canvas_.lettered(face_, faceTexture_, x, baseline, size, tracking, ink, text);
    };

    // ---- the blows ---------------------------------------------------------------------
    for (const Showing::Figure& figure : play.showing().figures()) {
        const float u = figure.life > 0.0f ? std::clamp(figure.age / figure.life, 0.0f, 1.0f) : 1.0f;
        const float alpha = faded(u);
        if (alpha <= 0.0f) continue;
        const float world[4] = {figure.world[0], figure.world[1], figure.world[2], 1.0f};
        float clip[4];
        bx::vec4MulMtx(clip, world, viewProj);
        if (clip[3] <= 0.0f) continue;  // behind the camera
        const float nx = clip[0] / clip[3], ny = clip[1] / clip[3];
        if (nx < -1.4f || nx > 1.4f || ny < -1.4f || ny > 1.4f) continue;
        const float travelled = ridden(u);
        const float x = (nx * 0.5f + 0.5f) * float(width) + figure.lean * kLean * travelled * unit;
        const float y = (0.5f - ny * 0.5f) * float(height) - kRise * travelled * unit -
                        float(figure.slot) * kStackStep * unit;

        const float size = sizeOf(figure.mark) * unit * popped(u);
        const std::string text = figure.mark == Mark::Miss
                                     ? std::string("MISS")
                                     : std::to_string(figure.value < 0 ? 0 : figure.value);
        // A miss is a word, so it takes the lane's own tracking rather than the ramp's: set
        // solid at this size it reads as one long glyph.
        const float tracking = figure.mark == Mark::Miss ? 0.08f : kTracking;
        write(std::round(x), std::round(y), size, withAlpha(inkOf(figure), alpha), text, tracking);
    }

    // ---- the lane ----------------------------------------------------------------------
    // Newest at the bottom, stacking upward off the HUD's own top edge, centred on the screen
    // as the plate is. A row that is fading still holds its place until it is gone, so nothing
    // below it slides up under the eye.
    float baseline = hudTop - kLaneGapAboveHud * unit;
    const float rowSize = kLaneSize * unit;
    const float unitSize = kLaneUnitSize * unit;
    const float centre = float(width) * 0.5f;
    for (size_t i = lane_.size(); i-- > 0;) {
        const Row& row = lane_[i];
        const float u = std::clamp(row.age / kRowLife, 0.0f, 1.0f);
        float alpha = 1.0f;
        if (u < kRowIn) alpha = u / kRowIn;
        else if (u > kRowOut) alpha = std::clamp(1.0f - (u - kRowOut) / (1.0f - kRowOut), 0.0f, 1.0f);
        const float lift = u < kRowIn ? (1.0f - u / kRowIn) * kRowLift * unit : 0.0f;

        uint32_t ink = kExperienceInk;
        const char* word = "experience";
        switch (row.kind) {
            case Row::Kind::Zen: ink = kZenInk; word = "zen"; break;
            case Row::Kind::Health: ink = kHealthInk; word = "life"; break;
            case Row::Kind::Mana: ink = kManaInk; word = "mana"; break;
            case Row::Kind::Experience: break;
        }
        const std::string figure = "+" + panel::commas((long long)row.value);
        const std::string unitWord = word;
        const float figureWide = face_.measure(rowSize, figure) + kTracking * rowSize *
                                                                      float(figure.size() - 1);
        const float wordWide = face_.measure(unitSize, unitWord) +
                               kLaneUnitTracking * unitSize * float(unitWord.size() - 1);
        const float whole = figureWide + kLaneWordGap * unit + wordWide;
        const float left = centre - whole * 0.5f;
        const float at = std::round(baseline + lift);
        const uint32_t tinted = withAlpha(ink, alpha);
        write(std::round(left + figureWide * 0.5f), at, rowSize, tinted, figure, kTracking);
        // The unit is the same ink at two thirds, uppercase and widely tracked: a label under
        // the figure's own colour, not a second reading.
        std::string shouted = unitWord;
        for (char& c : shouted) c = char(std::toupper(static_cast<unsigned char>(c)));
        write(std::round(left + figureWide + kLaneWordGap * unit + wordWide * 0.5f), at, unitSize,
              withAlpha(ink, alpha * 0.6f), shouted, kLaneUnitTracking);
        baseline -= face_.height(rowSize) + kLaneRowGap * unit;
    }
}

}  // namespace mu::game
