#include "game/ui/line_chip.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "game/ui/controls.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"
#include "net/regions.h"

namespace mu::game {
namespace {

using gfx::Box;

// The proposal's measures, at 1080 lines: its 720-line board's times one and a half, and the
// whole at three quarters of that (the user, 2026-10-20, seeing it in the game: "overlay is to
// large").
constexpr float kScale = 0.75f;
constexpr float kChipTall = 36.0f;
constexpr float kChipPad = 15.0f;
constexpr float kChipText = 16.5f;
constexpr float kChipGap = 12.0f;
constexpr float kBarWide = 4.5f, kBarGap = 3.0f;
constexpr float kBarTall[3] = {6.0f, 10.5f, 16.5f};
constexpr float kNoteGap = 9.0f;   // between the chip and the note beside it, or the list under it
constexpr float kNoteTall = 27.0f;
constexpr float kNotePad = 10.5f;
constexpr float kNoteText = 15.0f;
constexpr float kNoteIcon = 12.0f;
constexpr float kCross = 21.0f;    // the cross's box; its mark is half that
constexpr float kListWide = 420.0f;
constexpr float kListPad = 18.0f;
constexpr float kHeadText = 18.0f;
constexpr float kHeadTall = 34.0f;
constexpr float kRowTall = 40.0f;
constexpr float kRowGap = 3.0f;
constexpr float kRowText = 18.0f;
constexpr float kTagText = 15.0f;
constexpr float kMsWide = 78.0f;   // the ms column, ranged right
constexpr float kFootText = 15.5f;
constexpr float kFootLine = 22.0f;
constexpr float kRadius = 3.0f;  // every corner, the chip's too (the user, 2026-10-20: "decrease border radius")

// See-through at rest, a little less so on a slow line, whole under the pointer or open (the
// user, 2026-10-20: "with some opacity").
constexpr float kRest = 0.55f;
constexpr float kSlowAlpha = 0.75f;
constexpr float kFadeSeconds = 0.2f;
constexpr float kSlowMs = 150.0f;

constexpr uint32_t kChipBack = gfx::rgba(0.047f, 0.043f, 0.035f, 0.85f);
constexpr uint32_t kOff = gfx::rgba(0.290f, 0.267f, 0.220f);
constexpr uint32_t kFpsInk = gfx::rgba(0.722f, 0.694f, 0.639f);
constexpr uint32_t kNoteBack = gfx::rgba(0.227f, 0.133f, 0.047f, 0.68f);
constexpr uint32_t kNoteInk = gfx::rgba(0.941f, 0.753f, 0.541f);
constexpr uint32_t kNoteName = gfx::rgba(0.961f, 0.827f, 0.659f);
constexpr uint32_t kNoteCross = gfx::rgba(0.788f, 0.604f, 0.400f);
constexpr uint32_t kNoteCrossHi = gfx::rgba(0.961f, 0.827f, 0.659f);
constexpr uint32_t kAmber = gfx::rgba(0.910f, 0.627f, 0.290f);
constexpr uint32_t kListBack = gfx::rgba(0.055f, 0.051f, 0.043f, 0.98f);  // over the minimap: solid
constexpr uint32_t kGold = gfx::rgba(0.890f, 0.761f, 0.478f);
constexpr uint32_t kHereBack = gfx::rgba(0.165f, 0.145f, 0.114f, 0.9f);
constexpr uint32_t kNearBack = gfx::rgba(0.106f, 0.149f, 0.098f, 0.9f);
constexpr uint32_t kGreen = gfx::rgba(0.561f, 0.820f, 0.541f);
constexpr uint32_t kQuiet = gfx::rgba(0.639f, 0.616f, 0.565f);

const char* const kFoot =
    "Characters stay in the realm they were made in. Switch realm in the launcher, "
    "or move a character from your Account page.";

uint32_t faded(uint32_t abgr, float a) {
    return (abgr & 0x00FFFFFFu) | (gfx::rgbaByte(a * float(abgr >> 24) / 255.0f) << 24);
}

// Green under 80 ms, yellow to 150, amber to 250, red over: the proposal's.
uint32_t tone(float ms) {
    return ms < 80.0f ? kGreen : ms < 150.0f ? gfx::rgba(0.910f, 0.816f, 0.416f) : ms < 250.0f ? kAmber
                                                                                              : gfx::rgba(0.878f, 0.353f, 0.290f);
}
int litBars(float ms) { return ms < 80.0f ? 3 : ms < 150.0f ? 2 : 1; }

std::string msText(int ms) {
    char text[16];
    std::snprintf(text, sizeof text, "%d ms", std::min(ms, 9999));
    return text;
}
std::string fpsText(int fps) {
    char text[16];
    std::snprintf(text, sizeof text, "%d fps", fps);
    return text;
}
std::string clockText(int minutes) {
    char text[16];
    std::snprintf(text, sizeof text, "%02d:%02d", minutes / 60, minutes % 60);
    return text;
}

// The three bars, their feet on `foot`, from `x`.
void bars(gfx::Canvas& canvas, float x, float foot, int lit, uint32_t ink, float u, float a) {
    for (int i = 0; i < 3; ++i) {
        const float tall = std::round(kBarTall[i] * u);
        const float wide = std::max(2.0f, std::round(kBarWide * u));
        canvas.rect({std::round(x + float(i) * (kBarWide + kBarGap) * u), foot - tall, wide, tall},
                    faded(i < lit ? ink : kOff, a));
    }
}
float barsWide(float u) { return (3.0f * kBarWide + 2.0f * kBarGap) * u; }

// A stroke from (x0, y0) to (x1, y1), `thick` across: the cross's two.
void stroke(gfx::Canvas& canvas, float x0, float y0, float x1, float y1, float thick, uint32_t ink) {
    const float dx = x1 - x0, dy = y1 - y0;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.0f) return;
    const float nx = -dy / length * thick * 0.5f, ny = dx / length * thick * 0.5f;
    const float xy[8] = {x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny};
    const uint32_t inks[4] = {ink, ink, ink, ink};
    canvas.polygon(xy, inks, 4);
}

// The foot's words in lines no wider than `wide`.
std::vector<std::string> wrap(const std::string& text, float size, float wide) {
    std::vector<std::string> lines;
    std::string line;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find(' ', at);
        if (end == std::string::npos) end = text.size();
        const std::string word = text.substr(at, end - at);
        const std::string tried = line.empty() ? word : line + " " + word;
        if (!line.empty() && controls::labelWidth(size, tried) > wide) {
            lines.push_back(line);
            line = word;
        } else {
            line = tried;
        }
        at = end + 1;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

// What the note says after the region's name, before its time.
const char* const kNoteAfter = " is nearer";

}  // namespace

void LineChip::update(float seconds, const Line& line, const Pointer& pointer, int width, int height,
                      float right, float top) {
    const bool shown = line.fps >= 0 || line.ping >= 0.0f || line.clock >= 0;
    if (!shown) {
        if (built_) canvas_.clear();
        built_ = false;
        chip_ = note_ = cross_ = list_ = {};
        listUp_ = noteUp_ = false;
        return;
    }
    const float u = tip::unit() * kScale;
    const int ping = line.ping >= 0.0f ? int(line.ping + 0.5f) : -1;

    // The chip, its right edge at `right` and its top at `top`.
    float inside = 0.0f;
    if (ping >= 0) inside += barsWide(u) + kChipGap * u + controls::labelWidth(kChipText * u, msText(ping));
    if (line.fps >= 0) inside += controls::labelWidth(kChipText * u, fpsText(line.fps));
    if (line.clock >= 0) inside += controls::labelWidth(kChipText * u, clockText(line.clock));
    const int parts = (ping >= 0 ? 1 : 0) + (line.fps >= 0 ? 1 : 0) + (line.clock >= 0 ? 1 : 0);
    if (parts > 1) inside += float(parts - 1) * (kChipGap * 2.0f * u + std::max(1.0f, std::round(1.5f * u)));
    const float chipTall = std::round(kChipTall * u);
    const float chipWide = std::round(inside + 2.0f * kChipPad * u);
    chip_ = {std::round(right) - chipWide, std::round(top), chipWide, chipTall};

    // The note on its row, to its left, while there is a nearer region and the list is down.
    noteUp_ = line.nearer >= 0 && size_t(line.nearer) < net::regions().size() && !listUp_;
    if (noteUp_) {
        const std::string& name = net::regions()[size_t(line.nearer)].name;
        const float text = controls::labelWidth(kNoteText * u, name) +
                           controls::labelWidth(kNoteText * u, std::string(kNoteAfter) + " \xC2\xB7 " +
                                                                    msText(int(line.pings[size_t(line.nearer)] + 0.5)));
        const float wide = std::round(kNotePad * u * 2.0f + kNoteIcon * u + 7.0f * u + text + kCross * u);
        const float tall = std::round(kNoteTall * u);
        note_ = {chip_.x - std::round(kNoteGap * u) - wide, std::round(chip_.midY() - tall * 0.5f), wide, tall};
        cross_ = {note_.right() - std::round(kCross * u) - std::round(3.0f * u),
                  note_.y + std::round((tall - kCross * u) * 0.5f), std::round(kCross * u), std::round(kCross * u)};
    } else {
        note_ = cross_ = {};
    }

    // The list straight under the chip, their right edges one, over the minimap while it is open
    // (the user, 2026-10-20: "when click wrong position for overlay window", hung beside the disc).
    {
        const float wide = std::round(kListWide * u);
        if (footFor_ != wide) {
            footFor_ = wide;
            foot_ = wrap(kFoot, kFootText * u, wide - 2.0f * kListPad * u);
        }
        const float rows = float(net::regions().size());
        const float tall = std::round((kListPad + kHeadTall) * u + rows * (kRowTall + kRowGap) * u + 6.0f * u +
                                      float(foot_.size()) * kFootLine * u + kListPad * 0.6f * u);
        list_ = {chip_.right() - wide, chip_.bottom() + std::round(kNoteGap * u), wide, tall};
    }

    const bool onChip = chip_.has(pointer.x, pointer.y);
    const bool onCross = noteUp_ && cross_.has(pointer.x, pointer.y);
    if (pointer.pressed) {
        if (onChip) {
            clicked_ = true;
            listUp_ = !listUp_;
            if (listUp_) opened_ = true;
        } else if (onCross) {
            clicked_ = dismissed_ = true;
            noteUp_ = false;
        } else if (listUp_ && !list_.has(pointer.x, pointer.y)) {
            listUp_ = false;
        }
    }
    if (listUp_) noteUp_ = false;

    const float target = onChip || listUp_ ? 1.0f : ping >= 0 && line.ping >= kSlowMs ? kSlowAlpha : kRest;
    const float step = seconds / kFadeSeconds;
    alpha_ = alpha_ <= 0.0f ? target
             : alpha_ < target ? std::min(target, alpha_ + step) : std::max(target, alpha_ - step);

    // Filled in place, so a frame that changes nothing allocates nothing.
    Drawn& now = now_;
    now.width = width;
    now.height = height;
    now.fps = line.fps;
    now.clock = line.clock;
    now.ping = ping;
    now.here = line.here;
    now.nearer = noteUp_ ? line.nearer : -1;
    now.pings.clear();
    if (listUp_) {
        for (double one : line.pings) now.pings.push_back(int(std::lround(one)));
    } else if (noteUp_) {
        now.pings.push_back(int(std::lround(line.pings[size_t(line.nearer)])));
    }
    now.listUp = listUp_;
    now.noteUp = noteUp_;
    now.hover = onChip;
    now.crossHover = onCross;
    now.alpha = int(std::round(alpha_ * 64.0f));
    if (!built_ || !(now == drawn_)) {
        drawn_ = now;
        built_ = true;
        rebuild(drawn_);
    }
}

void LineChip::rebuild(const Drawn& now) {
    canvas_.clear();
    const float u = tip::unit() * kScale;
    const float a = float(now.alpha) / 64.0f;

    // The chip.
    {
        const Box& b = chip_;
        tip::panel(canvas_, b, kRadius * u, faded(kChipBack, a), faded(kChipBack, a));
        const float base = controls::middle(b.y, b.h, kChipText * u);
        float x = b.x + kChipPad * u;
        if (now.ping >= 0) {
            const uint32_t ink = tone(float(now.ping));
            bars(canvas_, x, std::round(b.midY() + kBarTall[2] * 0.5f * u), litBars(float(now.ping)), ink, u, a);
            x += barsWide(u) + kChipGap * u;
            x += controls::label(canvas_, x, base, kChipText * u, faded(ink, a), msText(now.ping));
        }
        // A thin upright rule between each two parts shown.
        bool before = now.ping >= 0;
        const auto rule = [&]() {
            if (!before) {
                before = true;
                return;
            }
            x += kChipGap * u;
            const float wide = std::max(1.0f, std::round(1.5f * u));
            const float tall = std::round(kChipText * 0.7f * u);
            canvas_.rect({std::round(x), std::round(b.midY() - tall * 0.5f), wide, tall}, faded(kOff, a));
            x += wide + kChipGap * u;
        };
        if (now.fps >= 0) {
            rule();
            x += controls::label(canvas_, x, base, kChipText * u, faded(kFpsInk, a), fpsText(now.fps));
        }
        if (now.clock >= 0) {
            rule();
            controls::label(canvas_, x, base, kChipText * u, faded(kFpsInk, a), clockText(now.clock));
        }
    }

    // The note: "<region> is nearer · 34 ms", and its cross.
    if (now.noteUp && now.nearer >= 0 && !now.pings.empty()) {
        const Box& b = note_;
        tip::panel(canvas_, b, kRadius * u, kNoteBack, kNoteBack);
        float x = b.x + kNotePad * u;
        bars(canvas_, x, std::round(b.midY() + kNoteIcon * 0.5f * u), 3, kAmber, u * kNoteIcon / kBarTall[2], 1.0f);
        x += barsWide(u * kNoteIcon / kBarTall[2]) + 7.0f * u;
        const float base = controls::middle(b.y, b.h, kNoteText * u);
        x += controls::label(canvas_, x, base, kNoteText * u, kNoteName, net::regions()[size_t(now.nearer)].name);
        controls::label(canvas_, x, base, kNoteText * u, kNoteInk,
                        std::string(kNoteAfter) + " \xC2\xB7 " + msText(now.pings.front()));
        const float half = kCross * 0.25f * u;
        const float cx = cross_.midX(), cy = cross_.midY();
        const float thick = std::max(1.0f, 1.6f * u);
        const uint32_t ink = now.crossHover ? kNoteCrossHi : kNoteCross;
        stroke(canvas_, cx - half, cy - half, cx + half, cy + half, thick, ink);
        stroke(canvas_, cx + half, cy - half, cx - half, cy + half, thick, ink);
    }

    // The regions, under the chip.
    if (now.listUp) {
        const Box& b = list_;
        tip::panel(canvas_, b, kRadius * u, kListBack, kListBack);
        const float left = b.x + kListPad * u, right = b.right() - kListPad * u;
        controls::caps(canvas_, left, b.y + kListPad * u + kHeadText * u * 0.8f, kHeadText * u, kGold, "Realms");
        const int near = net::nearer(std::vector<double>(now.pings.begin(), now.pings.end()), now.here);
        float y = b.y + (kListPad + kHeadTall) * u;
        const auto& all = net::regions();
        for (size_t i = 0; i < all.size(); ++i) {
            const bool here = int(i) == now.here, nearest = int(i) == near;
            const Box row{b.x + 8.0f * u, std::round(y), b.w - 16.0f * u, std::round(kRowTall * u)};
            if (here || nearest) tip::panel(canvas_, row, kRadius * u, here ? kHereBack : kNearBack, here ? kHereBack : kNearBack);
            const float base = controls::middle(row.y, row.h, kRowText * u);
            controls::label(canvas_, left, base, kRowText * u, here || nearest ? style::kBoneHi : style::kBone, all[i].name);
            // His own by the game's ping, live; the others by their connect, once.
            int ms = -1;
            std::string figure;
            if (here && now.ping >= 0) ms = now.ping;
            else if (i < now.pings.size() && now.pings[i] >= 0) ms = now.pings[i];
            if (ms >= 0) figure = msText(ms);
            else figure = now.pings.empty() ? "..." : "-";
            controls::ranged(canvas_, right, base, kRowText * u, ms >= 0 ? tone(float(ms)) : kQuiet, figure);
            if (here || nearest) {
                controls::ranged(canvas_, right - kMsWide * u, controls::middle(row.y, row.h, kTagText * u), kTagText * u,
                                 here ? kQuiet : kGreen, here ? "you are here" : "nearest");
            }
            y += (kRowTall + kRowGap) * u;
        }
        y += 6.0f * u + kFootLine * u * 0.75f;
        for (const std::string& one : foot_) {
            controls::label(canvas_, left, std::round(y), kFootText * u, kQuiet, one);
            y += kFootLine * u;
        }
    }
}

}  // namespace mu::game
