#include "game/ui/herald.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "game/play.h"
#include "game/ui/controls.h"
#include "game/ui/panel.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"
#include "sim/event.h"

namespace mu::game {
namespace {

using gfx::Box;

// The proposal's measures, in tip::unit() at 1080 lines (the page drew a 1080p screen at half
// size, so each of its pixels is two here), all drawn at kScale: at 1:1 it was too large on the
// screen (the user, 2026-10-05), then 0.65 ('scale little bit more down').
constexpr float kScale = 0.65f;
constexpr float kWide = 880.0f, kTall = 52.0f;
constexpr float kTop = 12.0f;
constexpr float kGap = 18.0f;        // between the line's items
constexpr float kNameSize = 20.0f, kNameTrack = 0.14f;
constexpr float kClockSize = 22.0f;
constexpr float kStateSize = 22.0f;
constexpr float kPlaceSize = 20.0f;
constexpr float kDot = 12.0f;        // the live dot's width
constexpr float kRuleTall = 18.0f;   // the upright rule between the clock and the place
constexpr float kCloseRight = 92.0f; // the cross's box, in from the band's right end
constexpr float kCloseBox = 28.0f, kCloseArm = 18.0f;
constexpr float kDropFrom = 80.0f;   // how far above its place the band comes from
constexpr float kRise = 20.0f;       // and how far it rises as it goes
constexpr float kSlideFrom = 52.0f;  // a second event's line, from the right

// The moments, in seconds.
constexpr float kDwell = 4.0f;       // each event up
constexpr float kLeast = 6.0f;       // the whole showing, at the least
constexpr float kDropSeconds = 0.6f, kDropFade = 0.36f;
constexpr float kLeaveSeconds = 0.45f;
constexpr float kSlideSeconds = 0.55f, kSlideFade = 0.3f;
constexpr float kPulseSeconds = style::kLiveSeconds;
// When it speaks: half an hour before, briefly; then the last few seconds counted down to the
// gate, the gate opening under the green dot, held a moment and gone (the user, 2026-10-05:
// 'show last 6 seconds of BC,DS gates open then show green indicator and then close
// notification'). The minute's call it had before is gone.
constexpr int kAnnounce = 1800, kCountdown = 6;
constexpr float kOpenHold = 4.0f;  // the green dot's seconds before the line goes

// The gates it counts to, each on its own timetable and level bands, 0 for no ceiling.
struct Gate {
    const char* name;
    const char* place;
    int period, opensAt, entry;  // seconds: between openings, into the day, held open
    const int (*bands)[2];
    int bandCount;
    int built;  // the highest numbered one there is
};
// Devil Square: WebZen 0.97d's four squares (DevilSquare.h), Charon in Noria; on the travel
// list's timetable, every four hours from midnight for 25 minutes (game/ui/travel.cpp kEvents,
// OpenMU's DevilSquareStartConfiguration). Not playable yet: the herald counts to it as the
// travel card does.
constexpr int kSquareBands[4][2] = {{15, 130}, {131, 180}, {181, 230}, {231, 0}};
const Gate kGates[] = {
    {"Blood Castle", "Devias \xB7 Messenger", sim::kCastlePeriod, sim::kCastleOpensAt,
     sim::kCastleEntry, sim::kCastleBands, sim::kCastles, sim::kCastlesBuilt},
    {"Devil Square", "Noria \xB7 Charon", 14400, 0, 1500, kSquareBands, 4, 4},
};

// Black under the line, across its width: clear at the ends, 78% in the middle (60% was tried
// and was too light: 'darkens was better before', 2026-10-05).
constexpr float kBandAt[5] = {0.0f, 0.22f, 0.5f, 0.78f, 1.0f};
constexpr float kBandAlpha[5] = {0.0f, 0.55f, 0.78f, 0.55f, 0.0f};
// The travel list's gold (game/ui/travel.cpp): the hairline's middle, the clock that is near.
constexpr uint32_t kGoldHi = gfx::rgba(0.886f, 0.776f, 0.541f);
// The live dot (style.h).
constexpr uint32_t kGreen = style::kLive;
constexpr uint32_t kGreenHi = style::kLiveHi;

uint32_t faded(uint32_t abgr, float a) {
    a = std::clamp(a, 0.0f, 1.0f);
    return (abgr & 0x00FFFFFFu) | (gfx::rgbaByte(a * float(abgr >> 24) / 255.0f) << 24);
}

uint32_t mixed(uint32_t a, uint32_t b, float t) {
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const float x = float((a >> shift) & 0xFFu), y = float((b >> shift) & 0xFFu);
        out |= uint32_t(std::lround(x + (y - x) * t)) << shift;
    }
    return out;
}

// cubic-bezier(.2,.8,.2,1), near enough: quick out of the start, long settle.
float settle(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

std::string clock(int seconds) {
    char text[16];
    if (seconds >= 3600) {
        std::snprintf(text, sizeof(text), "%d:%02d:%02d", seconds / 3600, seconds / 60 % 60,
                      seconds % 60);
    } else {
        std::snprintf(text, sizeof(text), "%d:%02d", seconds / 60, seconds % 60);
    }
    return text;
}

// A face, its texture and how a line in it is set: its size, tracking and the glyph whose ink
// stands for the line's height when it is centred.
struct Type {
    const gfx::Face* face = nullptr;
    bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
    float size = 0.0f;
    float track = 0.0f;  // pixels after each letter
    char ink = 'H';

    float width(const std::string& text) const {
        if (!face || text.empty()) return 0.0f;
        return face->measure(size, text) + track * float(text.size() - 1);
    }
    // The baseline that puts the ink's middle on `mid`.
    float baseline(float mid) const {
        const gfx::FaceGlyph* g = face ? face->glyph(ink) : nullptr;
        const float tall = g ? -g->y0 * face->emScale(size) : size * 0.7f;
        return std::round(mid + tall * 0.5f);
    }
    // Over its drop, both faded together so nothing dark is left behind as it goes.
    void draw(gfx::Canvas& canvas, float x, float mid, uint32_t ink, float alpha,
              const std::string& text) const {
        if (!face || text.empty()) return;
        const float base = baseline(mid);
        x = std::round(x);
        canvas.lettered(*face, texture, x + 1.0f, base + 1.0f, size, track,
                        faded(style::kDrop, alpha), text);
        canvas.lettered(*face, texture, x, base, size, track, faded(ink, alpha), text);
    }
};

// A filled circle, as a fan: the canvas has none.
void disc(gfx::Canvas& canvas, float cx, float cy, float r, uint32_t ink) {
    constexpr int kSteps = 20;
    float xy[kSteps * 2];
    for (int i = 0; i < kSteps; ++i) {
        const float a = 6.28318531f * float(i) / float(kSteps);
        xy[i * 2] = cx + std::cos(a) * r;
        xy[i * 2 + 1] = cy + std::sin(a) * r;
    }
    canvas.polygon(nullptr, xy, nullptr, kSteps, ink);
}

// A ring `thick` wide inside radius `r`, as the strip of quads between two circles.
void hoop(gfx::Canvas& canvas, float cx, float cy, float r, float thick, uint32_t ink) {
    constexpr int kSteps = 28;
    const uint32_t inks[4] = {ink, ink, ink, ink};
    const float in = std::max(0.0f, r - thick);
    for (int i = 0; i < kSteps; ++i) {
        const float a0 = 6.28318531f * float(i) / float(kSteps);
        const float a1 = 6.28318531f * float(i + 1) / float(kSteps);
        const float quad[8] = {cx + std::cos(a0) * r,  cy + std::sin(a0) * r,
                               cx + std::cos(a1) * r,  cy + std::sin(a1) * r,
                               cx + std::cos(a1) * in, cy + std::sin(a1) * in,
                               cx + std::cos(a0) * in, cy + std::sin(a0) * in};
        canvas.polygon(quad, inks, 4);
    }
}

// A straight stroke from (x0, y0) to (x1, y1), `thick` across.
void stroke(gfx::Canvas& canvas, float x0, float y0, float x1, float y1, float thick,
            uint32_t ink) {
    const float dx = x1 - x0, dy = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.0f) return;
    const float nx = -dy / len * thick * 0.5f, ny = dx / len * thick * 0.5f;
    const float quad[8] = {x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny};
    const uint32_t inks[4] = {ink, ink, ink, ink};
    canvas.polygon(quad, inks, 4);
}

// A hairline across the band, faded in from both ends with gold at its middle. No glow: one was
// tried either side and turned down ('dont use that glow', 2026-10-05).
void hairline(gfx::Canvas& canvas, const Box& band, float y, float thick, float alpha) {
    constexpr int kStops = 7;
    const float at[kStops] = {0.0f, 0.06f, 0.30f, 0.50f, 0.70f, 0.94f, 1.0f};
    const uint32_t clear = faded(kGoldHi, 0.0f);
    const uint32_t ink[kStops] = {clear,
                                  clear,
                                  faded(style::kIron, 0.7f * alpha),
                                  faded(kGoldHi, 0.85f * alpha),
                                  faded(style::kIron, 0.7f * alpha),
                                  clear,
                                  clear};
    for (int i = 0; i + 1 < kStops; ++i) {
        const float x0 = band.x + band.w * at[i], x1 = band.x + band.w * at[i + 1];
        canvas.shade({x0, y, x1 - x0, thick}, ink[i], ink[i + 1], ink[i + 1], ink[i]);
    }
}

// The local day's second, off the wall clock the desk hands the realm; -1 when there is none.
int dayOf(int64_t wall) {
    if (wall <= 0) return -1;
    const time_t at = time_t(wall);
    struct tm local {};
    localtime_r(&at, &local);
    return local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
}

}  // namespace

// A source that reached a moment: a fresh showing if none is up (or one is going), else onto
// the one up, which runs long enough to give it its turn.
void Herald::raise(int source, float hold, bool pin) {
    if (queued_ == 0 || leaving_ >= 0.0f) {
        queued_ = 0;
        at_ = 0;
        age_ = 0.0f;
        slide_ = kSlideSeconds;  // the first line comes in with the band, not after it
        dwell_ = 0.0f;
        leaving_ = -1.0f;
        life_ = 0.0f;
    }
    bool queued = false;
    for (int i = 0; i < queued_; ++i) queued |= queue_[i] == source;
    if (!queued && queued_ < kSources) queue_[queued_++] = source;
    life_ = std::max(life_, std::max(hold, kDwell * float(queued_)));
    // A gate counting down or opening holds the line on itself until it is done.
    if (pin) {
        for (int i = 0; i < queued_; ++i) {
            if (queue_[i] != source) continue;
            if (at_ != i) slide_ = 0.0f;
            at_ = i;
        }
        pinned_ = source;
        dwell_ = 0.0f;
    }
}

void Herald::close() {
    canvas_.clear();
    for (Source& one : sources_) one = Source{};
    queued_ = 0;
    pinned_ = -1;
    leaving_ = -1.0f;
    alpha_ = 0.0f;
    close_ = {};
}

bool Herald::update(float seconds, const Play& play, const Pointer& pointer, int width,
                    int height) {
    (void)height;
    const sim::Realm& realm = play.realm();
    const int64_t wall = realm.wallClock();
    const int day = dayOf(wall);
    const sim::Body& hero = realm.hero();
    // Inside the castle the tracker has the run; with no clock there is nothing to count to.
    const bool inside = realm.tables() && realm.tables()->map == sim::kBloodCastleMap;

    // ---- the gates: Blood Castle's Messenger (sim/event.h), Devil Square's Charon ------------
    for (int g = 0; g < kSources; ++g) {
        const Gate& gate = kGates[g];
        Source& one = sources_[g];
        Call& call = calls_[g];
        int moment = 0;
        int toStart = 0;
        if (day >= 0 && play.isOpen() && hero.level >= gate.bands[0][0]) {
            const int phase = ((day - gate.opensAt) % gate.period + gate.period) % gate.period;
            const bool open = phase < gate.entry;
            toStart = open ? 0 : gate.period - phase;
            const int64_t startAt = open ? wall - phase : wall + toStart;
            // The same opening for the second or two the clock wobbles across a frame.
            if (std::llabs(startAt - one.startAt) > 2) one = Source{startAt, 0};
            moment = open ? 3 : toStart <= kCountdown ? 2 : toStart <= kAnnounce ? 1 : 0;

            // His band's number, no higher than is built.
            int tier = gate.bandCount;
            for (int b = 0; b < gate.bandCount; ++b) {
                if (hero.level <= gate.bands[b][1] || gate.bands[b][1] == 0) {
                    tier = b + 1;
                    break;
                }
            }
            tier = std::min(tier, gate.built);
            call.name = std::string(gate.name) + " " + std::to_string(tier);
            call.place = gate.place;
            call.live = open;
            call.state = open ? "gate open" : "";
            call.seconds = open ? gate.entry - phase : toStart;
        }
        if (moment > one.spoken) {
            const int was = one.spoken;
            one.spoken = moment;
            // Reached a moment it speaks at: into the showing, unless he is where it would not.
            if (!inside) {
                if (moment == 1) raise(g, kLeast, false);
                // The countdown runs into the gate and past it by the hold.
                else if (moment == 2) raise(g, float(toStart) + kOpenHold, true);
                // Opened under a countdown already up: the line just turns. Opened unseen (he
                // came in during it): it shows the open gate alone.
                else if (was != 2 || queued_ == 0 || leaving_ >= 0.0f) raise(g, kLeast, true);
                else life_ = std::max(life_, kOpenHold);
            }
        }
    }
    if (inside && queued_ > 0 && leaving_ < 0.0f) leaving_ = 0.0f;

    // ---- the showing --------------------------------------------------------------------------
    bool clicked = false;
    if (queued_ > 0) {
        pulse_ += seconds;
        if (leaving_ < 0.0f) {
            age_ += seconds;
            slide_ += seconds;
            dwell_ += seconds;
            life_ -= seconds;
            if (dwell_ >= kDwell && queued_ > 1 && pinned_ < 0) {
                at_ = (at_ + 1) % queued_;
                dwell_ = 0.0f;
                slide_ = 0.0f;
            }
            if (life_ <= 0.0f) leaving_ = 0.0f;
            hover_ = close_.has(pointer.x, pointer.y);
            if (hover_ && pointer.pressed) {
                leaving_ = 0.0f;
                clicked = true;
            }
        } else {
            hover_ = false;
            leaving_ += seconds;
            if (leaving_ >= kLeaveSeconds) {
                queued_ = 0;
                pinned_ = -1;
                leaving_ = -1.0f;
            }
        }
    }
    if (queued_ == 0) {
        if (!canvas_.empty()) canvas_.clear();
        alpha_ = 0.0f;
        close_ = {};
        return clicked;
    }
    alpha_ = leaving_ >= 0.0f ? 1.0f - leaving_ / kLeaveSeconds : std::min(1.0f, age_ / kDropFade);
    // Moving every frame it is up -- the drop, the slide, the dot's breath -- so it is drawn
    // every frame it is up, and only then.
    rebuild(width);
    return clicked;
}

void Herald::rebuild(int width) {
    canvas_.clear();
    const float u = tip::unit() * kScale;
    const float a = alpha_;
    const float dy = leaving_ >= 0.0f
                         ? -kRise * u * (leaving_ / kLeaveSeconds) * (leaving_ / kLeaveSeconds)
                         : -kDropFrom * u * (1.0f - settle(age_ / kDropSeconds));
    const Box band{std::round((float(width) - kWide * u) * 0.5f), std::round(kTop * u + dy),
                   std::round(kWide * u), std::round(kTall * u)};

    // The band: black across, clear at both ends; then the two hairlines.
    for (int i = 0; i + 1 < 5; ++i) {
        const float x0 = band.x + band.w * kBandAt[i], x1 = band.x + band.w * kBandAt[i + 1];
        const uint32_t l = faded(style::kSeam, kBandAlpha[i] * a);
        const uint32_t r = faded(style::kSeam, kBandAlpha[i + 1] * a);
        canvas_.shade({x0, band.y, x1 - x0, band.h}, l, r, r, l);
    }
    const float hair = std::max(1.0f, std::round(u));
    hairline(canvas_, band, band.y, hair, a);
    hairline(canvas_, band, band.bottom() - hair, hair, a * 0.55f);

    // The line up, sliding in from the right when it took over from another.
    const Call& call = calls_[queue_[std::clamp(at_, 0, queued_ - 1)]];
    const float slid = settle(slide_ / kSlideSeconds);
    const float ca = a * std::min(1.0f, slide_ / kSlideFade);
    const float dx = kSlideFrom * u * (1.0f - slid);

    Type name{panel::titleFace(), panel::titleTexture(), kNameSize * u, 0.0f, 'H'};
    name.track = name.size * kNameTrack;
    const Type figure{controls::labelFace(), controls::labelTexture(), kClockSize * u, 0.0f, '0'};
    const Type place{controls::labelFace(), controls::labelTexture(), kPlaceSize * u, 0.0f, 'H'};
    // Small capitals all: the face's lower case is its small capitals, so the state is centred
    // on their height and not on one tall first letter.
    const Type state{controls::wordFace(), controls::wordTexture(), kStateSize * u, 0.0f, 'x'};

    std::string upper = call.name;
    for (char& c : upper) c = char(std::toupper(static_cast<unsigned char>(c)));
    const std::string time = call.seconds >= 0 ? clock(call.seconds) : std::string();
    const float gap = kGap * u;
    const float rule = std::max(1.0f, std::round(u));
    float wide = name.width(upper);
    if (call.live) wide += kDot * u + gap;
    if (!call.state.empty()) wide += gap + state.width(call.state);
    if (!time.empty()) wide += gap + figure.width(time);
    wide += gap + rule + gap + place.width(call.place);

    const float mid = band.y + band.h * 0.5f;
    float x = band.midX() - wide * 0.5f + dx;
    if (call.live) {
        // The green dot, its glow brightening and dimming, and a ring that widens and fades.
        const float cx = x + kDot * u * 0.5f, r = kDot * u * 0.5f;
        const float breath = 0.5f - 0.5f * std::cos(6.28318531f * pulse_ / kPulseSeconds);
        const float wave = settle(std::fmod(pulse_, kPulseSeconds) / kPulseSeconds);
        hoop(canvas_, cx, mid, r * 2.0f * (0.6f + 1.2f * wave), std::max(1.0f, 1.5f * u),
             faded(kGreen, 0.75f * (1.0f - wave) * ca));
        disc(canvas_, cx, mid, r * 2.2f, faded(kGreen, (0.10f + 0.12f * breath) * ca));
        disc(canvas_, cx, mid, r * 1.5f, faded(kGreen, (0.18f + 0.18f * breath) * ca));
        disc(canvas_, cx, mid, r, faded(mixed(kGreen, kGreenHi, breath), ca));
        x += kDot * u + gap;
    }
    name.draw(canvas_, x, mid, style::kBoneHi, ca, upper);
    x += name.width(upper);
    const bool near = call.live || (call.seconds >= 0 && call.seconds <= 300);
    if (!call.state.empty()) {
        x += gap;
        state.draw(canvas_, x, mid, kGoldHi, ca, call.state);
        x += state.width(call.state);
    }
    if (!time.empty()) {
        x += gap;
        figure.draw(canvas_, x, mid, near ? kGoldHi : style::kAshInk, ca, time);
        x += figure.width(time);
    }
    // The upright rule, fading at both ends.
    x += gap;
    {
        const float top = std::round(mid - kRuleTall * u * 0.5f), half = kRuleTall * u * 0.5f;
        const uint32_t clear = faded(style::kIron, 0.0f), iron = faded(style::kIron, ca);
        canvas_.shade({std::round(x), top, rule, half}, clear, clear, iron, iron);
        canvas_.shade({std::round(x), top + half, rule, half}, iron, iron, clear, clear);
    }
    x += rule + gap;
    place.draw(canvas_, x, mid, style::kBone2, ca, call.place);
    x += place.width(call.place);

    // The cross: two thin strokes, dim until the pointer is on it.
    close_ = {std::round(band.right() - kCloseRight * u - kCloseBox * u),
              std::round(mid - kCloseBox * u * 0.5f), std::round(kCloseBox * u),
              std::round(kCloseBox * u)};
    const float arm = kCloseArm * u * 0.5f * 0.7071f;
    const float ccx = close_.midX(), ccy = close_.midY();
    const uint32_t ink = faded(hover_ ? style::kBoneHi : style::kBone2, a * (hover_ ? 1.0f : 0.6f));
    const float thick = std::max(1.0f, 1.5f * u);
    stroke(canvas_, ccx - arm, ccy - arm, ccx + arm, ccy + arm, thick, ink);
    stroke(canvas_, ccx - arm, ccy + arm, ccx + arm, ccy - arm, thick, ink);
}

}  // namespace mu::game
