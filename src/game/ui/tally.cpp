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

// ---- the ramp (design page 2026-09-23, concept B2, in the map name's face) -------------------
// Sizes in interface units, which panel::scale() turns into pixels -- 2 at 1080 lines, so a
// swing is 42 px there and the same fraction of the screen at any other height.
//
// A point smaller than the page drew them, because Cinzel is a Roman capital and not a
// condensed grotesque: at the page's own 23 a three-figure blow was half a tile wide. The
// stack rule below is what now does the work the narrow face was chosen for.
constexpr float kSwingSize = 21.0f;
constexpr float kSkillSize = 27.0f;
constexpr float kCriticalSize = 32.0f;
// The shield's share is the quiet end of the ramp: it is read, not felt.
constexpr float kSmallSize = 15.5f;
// And a miss is quieter still -- a word, not a figure, and the one thing on the ramp that says
// nothing happened. Smaller again at the user's word, 2026-09-23.
constexpr float kMissSize = 12.0f;
// None on a figure. Roman capitals want air between LETTERS, and Cinzel's own drawing already
// carries it; a number is one object and tracking it made 27 read as 2 7 (the user, on sight,
// 2026-09-23). The word MISS keeps its own, below, because a word is not a number.
constexpr float kTracking = 0.0f;

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
// And a hard drop under the figures alone, asked for on 2026-09-23: the same glyph in black,
// offset down and right. Minimal on purpose -- enough that a figure sits ON the world instead
// of in it, not enough to become a second reading beside the first. A Roman capital wants it
// more than the grotesque did: Cinzel's stems are thin where they meet the serif, and the soft
// halo alone let the grass through them.
constexpr float kDropAway = 1.6f;   // interface units, down and right
constexpr float kDropAlpha = 0.55f;

// ---- the lane over the HUD ------------------------------------------------------------------
// Small, in Cinzel Medium, and the SAME size and weight for every row -- experience, Zen and a
// potion are one kind of thing and are said one way (the user, 2026-09-23). The fight's Bold is
// for the blows and the death.
constexpr float kLaneSize = 9.0f;
constexpr float kLaneUnitSize = 6.5f;
constexpr float kLaneUnitTracking = 0.24f;  // the arrival's caption tracking, near enough
constexpr float kLaneGapAboveHud = 14.0f;  // clear of the plate's top edge
constexpr float kLaneRowGap = 3.0f;
constexpr float kLaneWordGap = 7.0f;
constexpr float kRowLife = 2.1f;
// The death holds longer and is set larger, in the fight's own weight rather than the lane's:
// it is the one line there that is not income. Still no banner across the middle of the screen
// -- the user asked for it where the experience and the Zen are said.
constexpr float kDiedLife = 4.5f;
// Bigger than the lane ever was, because it is no longer in the lane: at the middle of the
// screen it is the same size the map name is (35.5 px at 1080, which is 17.75 units) and a
// third again, since a death is the one thing the game says that stops the hunt.
constexpr float kDiedSize = 24.0f;
// The death is dressed as the map name is (the user's call, 2026-09-23), so these are the
// arrival's own numbers divided by two: its design page is in pixels at 1080 lines and an
// interface unit is two of them. Its name is 35.5 px where this is 18 units, so the two are
// drawn at the same size and a shadow measured for one is right for the other.
//   soft shadow   0 1.9 px blur 17.3 px black 75%   -> sigma 8.64 px = 0.243 em
//   tight shadow  0 0   px blur  3.8 px black 60%   -> the halo every figure already has
//   rule          two 1 px hairlines 201.6 px long, 17.3 px either side of an 11.1 px diamond
constexpr float kSoftSigmaEm = 0.243f;
constexpr float kSoftAlpha = 0.75f;
constexpr float kTightAlpha = 0.60f;
constexpr float kSoftDrop = 0.95f;      // units: the page's 1.9 px
constexpr float kRuleLength = 100.8f;   // units, each side
constexpr float kRuleGap = 8.64f;
constexpr float kRuleSigma = 1.92f;
constexpr float kMarkBox = 5.57f;
constexpr float kMarkSigma = 1.44f;
constexpr float kDiedRuleDrop = 11.0f;  // units under the death's baseline
// And it is NOT in the lane: the death is said in the middle of the screen (the user, 2026-09-23)
// and only the income is said over the HUD. A share of the height, so it sits where the eye
// already is rather than where the plate happens to end.
constexpr float kDiedDownScreen = 0.44f;
// The scrim, the arrival's own (0.52 black, a Gaussian 250 by 50 px at 1080) in the death's red
// and drawn a little tighter, because it stands behind two lines and not four.
constexpr float kScrimAlpha = 0.5f;
constexpr float kScrimSigmaX = 118.0f;  // units
constexpr float kScrimSigmaY = 26.0f;
// Little tracking and NOT shouted: Cinzel has no lowercase, so "You Died" comes out as a
// capital and small capitals, which is the inscription's own way of saying a thing quietly.
// The user's call on sight of the all-capital version, 2026-09-23.
constexpr float kDiedTracking = 0.10f;
// It fades in and out and does NOTHING else -- the user's call, 2026-09-23, on seeing the map
// name's own entrance on it. The arrival introduces a place you have arrived at and can take a
// moment over it; a death is already true when it is said, and letters closing into place read
// as the message arranging itself while he lies there.
// The arrival's own silver, for the rule and the diamond: #ECE8DD at 85%.
constexpr uint32_t silver(float a) {
    return gfx::rgba(236.0f / 255.0f, 232.0f / 255.0f, 221.0f / 255.0f, a);
}
constexpr uint32_t black(float a) { return gfx::rgba(0.0f, 0.0f, 0.0f, a); }
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
// The death: the arrival's own off-white over the arrival's own black halos, and the red is
// the SCRIM behind the whole block rather than a glow round each letter -- the user's
// correction, 2026-09-23. A per-glyph red read as the word being lit; a cloud behind the block
// reads as the screen itself darkening, which is what it is.
const uint32_t kDiedInk = byteColour(240, 236, 228);
const uint32_t kScrimInk = byteColour(86, 8, 6);
const uint32_t kExperienceInk = byteColour(222, 213, 196);
const uint32_t kZenInk = byteColour(231, 201, 130);
const uint32_t kHealthInk = byteColour(134, 201, 143);
const uint32_t kManaInk = byteColour(138, 166, 224);
const uint32_t kHalo = gfx::rgba(0.0f, 0.0f, 0.0f, kHaloAlpha);

float sizeOf(Mark mark) {
    switch (mark) {
        case Mark::Skill: return kSkillSize;
        case Mark::Critical: return kCriticalSize;
        case Mark::Miss: return kMissSize;
        case Mark::Absorbed: return kSmallSize;
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
//
// **Both atlases are 2048, and the halo's is not the 512 a blur would seem to want.** A halo
// bake reserves three sigmas of padding round every glyph, and at 28 px that is 28 texels of
// air on a 28-texel letter -- Barlow fitted, Cinzel did not, and stbtt_PackFontRanges then
// failed the whole bake. The figures drew on with no halo and, because the four bakes were
// chained with &&, the gain lane silently got no face at all and stopped drawing. The bakes
// are independent now, and each says which one failed.
constexpr float kSharpBake = 128.0f;
constexpr float kHaloBake = 28.0f;
// The death's soft halo is a wider blur, so it is baked smaller again: nothing in a Gaussian
// that broad survives being drawn at eighteen units anyway.
constexpr float kSoftBake = 20.0f;
// The lane is never drawn above 35 px, so its own bake is small.
constexpr float kQuietBake = 48.0f;

}  // namespace

void Tally::open(const gfx::Interface& interface) {
    interface.adopt(canvas_);
    const char* path = gfx::figureFacePath();
    // Two bakes, each judged on its own: a halo that will not pack must not take the face it
    // shades down with it. One face for everything the tally says -- the blow, the lane and the
    // death are one voice at four sizes.
    const bool figures = bakeOne(face_, faceTexture_, path, kSharpBake, 2048, 0.0f,
                                 "tally figures");
    const bool halo = bakeOne(halo_, haloTexture_, path, kHaloBake, 2048, kHaloSigmaEm,
                              "tally halo");
    const bool soft = bakeOne(soft_, softTexture_, path, kSoftBake, 2048, kSoftSigmaEm,
                              "tally soft halo");
    const bool lane = bakeOne(quiet_, quietTexture_, gfx::quietFacePath(), kQuietBake, 1024,
                              0.0f, "tally lane");
    if (!lane) core::logError("tally: no lane face; nothing will be said over the HUD");
    if (!figures) core::logError("tally: no face baked; a blow will show no number");
    if (!halo) core::logError("tally: no halo baked; the text will read thin over grass");
    if (!soft) core::logError("tally: no soft halo; the death will read flat");
}

void Tally::shutdown() {
    for (bgfx::TextureHandle* t :
         {&faceTexture_, &haloTexture_, &softTexture_, &quietTexture_}) {
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
            case Play::Gain::Kind::Died:
                // Alone: whatever he was earning a second ago is not what the lane is for now.
                lane_.clear();
                heldZen_ = 0;
                lane_.push_back({Row::Kind::Died, 0, 0.0f});
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
        if (lane_[i].age >= (lane_[i].kind == Row::Kind::Died ? kDiedLife : kRowLife)) {
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
    const auto writeIn = [&](const gfx::Face& face, bgfx::TextureHandle texture,
                             const gfx::Face& shade, bgfx::TextureHandle shadeTexture,
                             float centreX, float baseline, float size, uint32_t ink,
                             const std::string& text, float trackingEm, bool drop,
                             float shade_ = 1.0f) {
        const float tracking = trackingEm * size;
        const float wide = face.measure(size, text) + tracking * float(text.size() - 1);
        const float x = centreX - wide * 0.5f;
        const float alpha = float(ink >> 24) / 255.0f;
        if (bgfx::isValid(shadeTexture)) {
            canvas_.lettered(shade, shadeTexture, x, baseline + kHaloDrop * unit, size, tracking,
                             withAlpha(kHalo, alpha * shade_), text);
        }
        // The hard drop goes over the halo and under the figure, so the halo is the air round
        // it and this is the shadow it casts.
        if (drop) {
            canvas_.lettered(face, texture, x + kDropAway * unit, baseline + kDropAway * unit,
                             size, tracking, gfx::rgba(0.0f, 0.0f, 0.0f, kDropAlpha * alpha),
                             text);
        }
        canvas_.lettered(face, texture, x, baseline, size, tracking, ink, text);
    };
    // A blow, in the fight's own Bold, with its drop.
    const auto write = [&](float centreX, float baseline, float size, uint32_t ink,
                           const std::string& text, float trackingEm) {
        writeIn(face_, faceTexture_, halo_, haloTexture_, centreX, baseline, size, ink, text,
                trackingEm, true);
    };
    // A gain: the Medium, and no hard drop -- a shadow under nine-unit text is a smudge, and
    // the fight's own halo (baked from the Bold) is close enough at this size to shade it.
    const auto writeQuiet = [&](float centreX, float baseline, float size, uint32_t ink,
                                const std::string& text, float trackingEm) {
        // Half the halo the fight takes: a blur baked for a 42-unit figure, laid under a
        // 9-unit one at full strength, is a dark band behind the words rather than air round
        // them -- seen in the first shot of it.
        writeIn(quiet_, quietTexture_, halo_, haloTexture_, centreX, baseline, size, ink, text,
                trackingEm, false, 0.5f);
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
        const float tracking = figure.mark == Mark::Miss ? 0.14f : kTracking;
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
        const float life = row.kind == Row::Kind::Died ? kDiedLife : kRowLife;
        const float u = std::clamp(row.age / life, 0.0f, 1.0f);
        float alpha = 1.0f;
        if (u < kRowIn) alpha = u / kRowIn;
        else if (u > kRowOut) alpha = std::clamp(1.0f - (u - kRowOut) / (1.0f - kRowOut), 0.0f, 1.0f);
        const float lift = u < kRowIn ? (1.0f - u / kRowIn) * kRowLift * unit : 0.0f;

        if (row.kind == Row::Kind::Died) {
            // In the middle of the screen and not in the lane, and dressed as the map name is:
            // the scrim, the tight halo, the soft one under its own drop, the word, and a
            // hairline with a diamond beneath it. See kSoftSigmaEm for why the arrival's pixels
            // come across as units without conversion.
            const float size = kDiedSize * unit;
            const float tracking = kDiedTracking * size;
            const std::string said = "You Died";
            const float wide = face_.measure(size, said) + tracking * float(said.size() - 1);
            const float x = std::round(centre - wide * 0.5f);
            // No lift: it fades in where it will stand, and stands there.
            const float at = std::round(float(height) * kDiedDownScreen);
            // The cloud first, behind the whole block -- the word, the rule and the air round
            // them -- and then the arrival's own two black halos under the word.
            scrim(centre, at - size * 0.25f, unit, alpha);
            if (bgfx::isValid(haloTexture_)) {
                canvas_.lettered(halo_, haloTexture_, x, at, size, tracking,
                                 black(kTightAlpha * alpha), said);
            }
            if (bgfx::isValid(softTexture_)) {
                canvas_.lettered(soft_, softTexture_, x, at + kSoftDrop * unit, size, tracking,
                                 black(kSoftAlpha * alpha), said);
            }
            canvas_.lettered(face_, faceTexture_, x, at, size, tracking,
                             withAlpha(kDiedInk, alpha), said);
            rule(centre, at + kDiedRuleDrop * unit, unit, alpha, 1.0f, 1.0f);
            // The lane's own stack is left where it was: nothing else is up while he is down,
            // and a row that arrives after him starts at the foot of the screen as always.
            continue;
        }
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
        const float figureWide = quiet_.measure(rowSize, figure);
        const float wordWide = quiet_.measure(unitSize, unitWord) +
                               kLaneUnitTracking * unitSize * float(unitWord.size() - 1);
        const float whole = figureWide + kLaneWordGap * unit + wordWide;
        const float left = centre - whole * 0.5f;
        const float at = std::round(baseline + lift);
        const uint32_t tinted = withAlpha(ink, alpha);
        writeQuiet(std::round(left + figureWide * 0.5f), at, rowSize, tinted, figure, 0.0f);
        // The unit is the same ink at two thirds, uppercase and widely tracked: a label under
        // the figure's own colour, not a second reading.
        std::string shouted = unitWord;
        for (char& c : shouted) c = char(std::toupper(static_cast<unsigned char>(c)));
        writeQuiet(std::round(left + figureWide + kLaneWordGap * unit + wordWide * 0.5f), at,
                   unitSize, withAlpha(ink, alpha * 0.6f), shouted, kLaneUnitTracking);
        baseline -= quiet_.height(rowSize) + kLaneRowGap * unit;
    }
}

// The arrival's scrim: a Gaussian each way laid down as a grid of shaded quads, so it has no
// edge anywhere and fades with what it stands behind.
void Tally::scrim(float centre, float middle, float unit, float opacity) {
    const float sx = kScrimSigmaX * unit, sy = kScrimSigmaY * unit;
    constexpr int kColumns = 16, kRows = 8;
    const auto at = [&](int i, int j) {
        const float x = -3.0f + 6.0f * float(i) / kColumns;
        const float y = -3.0f + 6.0f * float(j) / kRows;
        return withAlpha(kScrimInk, kScrimAlpha * opacity * std::exp(-0.5f * (x * x + y * y)));
    };
    for (int j = 0; j < kRows; ++j) {
        for (int i = 0; i < kColumns; ++i) {
            const float x0 = centre + sx * (-3.0f + 6.0f * float(i) / kColumns);
            const float y0 = middle + sy * (-3.0f + 6.0f * float(j) / kRows);
            canvas_.shade({x0, y0, sx * 6.0f / kColumns, sy * 6.0f / kRows}, at(i, j),
                          at(i + 1, j), at(i + 1, j + 1), at(i, j + 1));
        }
    }
}

// The arrival's rule, at the arrival's own arithmetic: two hairlines drawing outward from a
// turned square, each over the box-shadow a browser would cast, and the square over the ring
// shadow of its own. Nothing here animates -- the map name draws its rule out as it comes in,
// and a death has nothing to introduce.
void Tally::rule(float centre, float middle, float unit, float opacity, float drawn,
                 float mark) {
    const float line = std::max(1.0f, unit * 0.5f);
    const float inner = kMarkBox * unit * 0.5f + kRuleGap * unit;
    const float length = kRuleLength * unit * drawn;
    if (length <= 0.0f && mark <= 0.0f) return;
    const float sigma = kRuleSigma * unit;
    for (int side = -1; side <= 1; side += 2) {
        if (length <= 0.0f) break;
        const float near = centre + float(side) * inner;
        const float far = centre + float(side) * (inner + length);
        const float left = std::min(near, far), w = std::fabs(far - near);
        // The box-shadow in strips down a Gaussian profile: a 1 px line under this sigma peaks
        // at about 8% black, which is the whole of what a browser draws there.
        constexpr int kStrips = 8;
        const float reach = sigma * 3.0f;
        const auto profile = [&](float y) {
            const float d = std::fabs(y) / sigma;
            return 0.80f * opacity * std::exp(-0.5f * d * d) * line / (sigma * 2.5066283f);
        };
        for (int i = 0; i < kStrips; ++i) {
            const float y0 = -reach + reach * 2.0f * float(i) / kStrips;
            const float y1 = -reach + reach * 2.0f * float(i + 1) / kStrips;
            const uint32_t a = black(profile(y0)), b = black(profile(y1));
            canvas_.shade({left, middle + y0, w, y1 - y0}, a, a, b, b);
        }
        const uint32_t clear = silver(0.0f), full = silver(0.85f * opacity);
        const gfx::Box box{left, middle - line * 0.5f, w, line};
        if (side < 0) canvas_.shade(box, clear, full, full, clear);
        else canvas_.shade(box, full, clear, clear, full);
    }

    if (mark <= 0.0f) return;
    // The diamond turns in from -20 degrees at 0.7 of its size, as the arrival's does.
    const float grown = 0.7f + 0.3f * mark;
    const float turn = -20.0f * (1.0f - mark) * 3.14159265f / 180.0f;
    const float cosTurn = std::cos(turn), sinTurn = std::sin(turn);
    const auto diamond = [&](float half, uint32_t colour) {
        const float corners[4][2] = {{0.0f, -half}, {half, 0.0f}, {0.0f, half}, {-half, 0.0f}};
        float xy[8];
        for (int i = 0; i < 4; ++i) {
            xy[i * 2] = centre + (corners[i][0] * cosTurn - corners[i][1] * sinTurn) * grown;
            xy[i * 2 + 1] = middle + (corners[i][0] * sinTurn + corners[i][1] * cosTurn) * grown;
        }
        canvas_.polygon(nullptr, xy, nullptr, 4, colour);
    };
    const float half = kMarkBox * unit * 0.45f;
    // The shadow as rings of a blurred edge, outermost first, each layer's alpha chosen so that
    // what has piled up at its distance is the Gaussian edge's: 0.9 * erfc(d / sigma root 2) / 2.
    const float markSigma = kMarkSigma * unit;
    constexpr int kRings = 7;
    float below = 0.0f;
    for (int i = kRings; i >= 0; --i) {
        const float d = markSigma * 3.0f * float(i) / kRings;
        const float want =
            0.90f * opacity * mark * 0.5f * std::erfc(d / (markSigma * 1.41421356f));
        const float layer = below >= 1.0f ? 0.0f : 1.0f - (1.0f - want) / (1.0f - below);
        if (layer > 0.0f) diamond(half + d * 1.41421356f, black(layer));
        below = want;
    }
    diamond(half, silver(opacity * mark));
}

}  // namespace mu::game
