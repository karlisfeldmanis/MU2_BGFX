#include "game/ui/menu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "game/ui/panel.h"
#include "game/ui/sheet.h"
#include "game/ui/tip.h"

namespace mu::game {
namespace {

using gfx::Box;

// ---- the layout ----------------------------------------------------------------------------
//
// In the design page's own pixels at 1080 lines (tip::unit), which is the interface's pixel and
// not the windows' scale: the menu is a thing in front of the game, sized like the pointer and
// the tips, and not a right-hand panel. Every number is the page the user chose.

constexpr float kWide = 420.0f;
constexpr float kPadX = 36.0f;
constexpr float kInner = kWide - kPadX * 2.0f;  // 348, a button's width
constexpr float kRadius = 20.0f;
constexpr float kTitleTop = 30.0f, kTitleTall = 28.0f;
constexpr float kFirstRule = 70.0f;
constexpr float kBig = 66.0f, kBigRadius = 14.0f;
constexpr float kQuiet = 48.0f, kQuietRadius = 12.0f;
constexpr float kRow = 50.0f, kRowRadius = 10.0f;
constexpr float kGap = 10.0f;
constexpr float kInset = 4.0f;  // the engraved line inside a slab
constexpr float kLiftSeconds = 0.12f;

// The whole menu at 85% of the page it was drawn from: the user, 2026-09-27, *"scale down menu
// little bit"*. One number, so every measure below keeps its proportion.
constexpr float kScale = 0.85f;
float unit() { return tip::unit() * kScale; }

// Where the sheet's centre stands: a little above the middle, where a thing to be answered
// is looked for.
constexpr float kCentreShare = 0.46f;

enum Target : int {
    kOptions = 0, kSwitch = 1, kExit = 2, kBack = 4, kClose = 9,
    // Each Options row's two arrows: row r steps down at kRowArrows + 2r and up one past it.
    kRowArrows = 10,
};

// The Options rows, top to bottom.
enum Row : int { kDisplay = 0, kSize = 1, kVsync = 2, kVolume = 3, kCounter = 4, kRows = 5 };
constexpr const char* kRowNames[kRows] = {"Display", "Resolution", "V-sync", "Volume",
                                          "Frame rate counter"};

// Each page's own rules and foot, top to bottom.
constexpr float kMainSecondRule = 240.0f, kMainFoot = 338.0f, kMainTall = 374.0f;
constexpr float kRowsTop = 84.0f;
constexpr float kOptionsRule = kRowsTop + kRows * (kRow + kGap) + 4.0f;
constexpr float kOptionsTall = kOptionsRule + 14.0f + kQuiet + 26.0f;

float rowTop(int row) { return kRowsTop + float(row) * (kRow + kGap); }

float tallOf(Menu::Page page) {
    switch (page) {
        case Menu::Page::Main: return kMainTall;
        case Menu::Page::Options: return kOptionsTall;
    }
    return kMainTall;
}

// A target's box on its page, relative to the sheet's corner, or an empty box when the page
// has no such target.
Box boxOf(Menu::Page page, int target) {
    const float x = kPadX;
    const Box close{kWide - 18.0f - 30.0f, 18.0f, 30.0f, 30.0f};
    switch (page) {
        case Menu::Page::Main:
            if (target == kOptions) return {x, 84.0f, kInner, kBig};
            if (target == kSwitch) return {x, 84.0f + kBig + kGap, kInner, kBig};
            if (target == kExit) return {x, kMainSecondRule + 14.0f, kInner, kBig};
            if (target == kClose) return close;
            break;
        case Menu::Page::Options: {
            // The two arrows sit at either end of a row's value, which is its right 150.
            const float right = kWide - kPadX - 12.0f;
            const float arrow = 28.0f;
            if (target >= kRowArrows && target < kRowArrows + kRows * 2) {
                const int row = (target - kRowArrows) / 2;
                const bool up = (target - kRowArrows) % 2 == 1;
                return {up ? right - arrow : right - 150.0f, rowTop(row) + 11.0f, arrow, arrow};
            }
            if (target == kBack) return {x, kOptionsRule + 14.0f, kInner, kQuiet};
            if (target == kClose) return close;
            break;
        }
    }
    return {};
}

Box at(float x, float y, const Box& design) {
    const float u = unit();
    return {std::round(x + design.x * u), std::round(y + design.y * u), std::round(design.w * u),
            std::round(design.h * u)};
}

// ---- the slab ------------------------------------------------------------------------------

struct Tone {
    float r, g, b, a;
    Tone mix(const Tone& o, float t) const {
        return {r + (o.r - r) * t, g + (o.g - g) * t, b + (o.b - b) * t, a + (o.a - a) * t};
    }
    Tone times(float k) const { return {r, g, b, a * k}; }
    uint32_t packed() const { return gfx::rgba(r, g, b, a); }
};

constexpr Tone kBronze{0.643f, 0.573f, 0.404f, 1.0f};
constexpr Tone kPale{0.941f, 0.847f, 0.604f, 1.0f};  // sheet::ink::kTitle
constexpr Tone kRed{1.0f, 0.451f, 0.373f, 1.0f};     // sheet.cpp's kBlockedEdge
constexpr Tone kEmber{0.886f, 0.290f, 0.220f, 1.0f}; // and its kBlockedBack
constexpr Tone kWarm{1.0f, 0.745f, 0.314f, 1.0f};
constexpr Tone kInkRest{0.808f, 0.792f, 0.737f, 0.82f};
constexpr Tone kSlabTop{0.110f, 0.102f, 0.090f, 0.98f};
constexpr Tone kSlabFoot{0.059f, 0.055f, 0.051f, 0.98f};

enum class Kind : uint8_t { Plain, Danger, Inactive };

// One button: a rounded slab under a rim lit from above, an engraved line a few pixels inside
// it, a breath of light over its top half, and its word in Cinzel. Under the pointer the rim and
// the word go gold -- red for Exit -- and a warm light rises from its foot and spills round it.
// Held, it sinks a pixel. `lift` is the eased hover, 0 to 1.
void slab(gfx::Canvas& canvas, const Box& box, float radius, const std::string& word,
          float size, Kind kind, float lift, bool pressed) {
    const float u = unit();
    const bool live = kind != Kind::Inactive;
    const float t = live ? lift : 0.0f;
    const Tone accent = kind == Kind::Danger ? kRed : kPale;
    const Tone glow = kind == Kind::Danger ? kEmber : kWarm;
    const float line = std::max(1.0f, std::round(u));
    const Box body = pressed && live ? Box{box.x, box.y + line, box.w, box.h} : box;

    const Tone rimTop = live ? kBronze.times(0.55f).mix(accent.times(0.90f), t)
                             : kBronze.times(0.22f);
    const Tone rimFoot = live ? kBronze.times(0.18f).mix((kind == Kind::Danger ? kEmber : kBronze)
                                                              .times(0.45f), t)
                              : kBronze.times(0.08f);
    const Tone ink = !live ? kInkRest.times(0.36f)
                   : pressed ? Tone{1.0f, 1.0f, 1.0f, 0.95f}
                             : kInkRest.mix(accent.times(0.97f), t);

    // The drop under it, and the light spilling round it while lifted.
    if (!pressed) tip::shadowUnder(canvas, body, 0.6f);
    if (t > 0.0f) {
        for (const float spread : {9.0f, 5.0f, 2.5f}) {
            const float g = spread * u;
            tip::panel(canvas, body.grown(line + g), radius + line + g,
                       glow.times(0.07f * t).packed(), glow.times(0.12f * t).packed());
        }
    }
    tip::panel(canvas, body.grown(line), radius + line, rimTop.packed(), rimFoot.packed());
    tip::panel(canvas, body, radius, kSlabTop.packed(), kSlabFoot.packed());

    // The engraved line: a ring of bronze a few pixels in, cut back to the slab inside it.
    const float in = kInset * u;
    const Box inner{body.x + in, body.y + in, body.w - in * 2.0f, body.h - in * 2.0f};
    const float innerRadius = std::max(2.0f, radius - in);
    const Tone engraved = live ? kBronze.times(0.14f).mix(accent.times(0.24f), t)
                               : kBronze.times(0.07f);
    tip::panel(canvas, inner, innerRadius, engraved.packed(), engraved.packed());
    tip::panel(canvas, inner.grown(-line), innerRadius - line,
               kSlabTop.mix(kSlabFoot, in / body.h).packed(),
               kSlabFoot.mix(kSlabTop, in / body.h).packed());

    // A breath of light over the top half, and the warm pool rising from the foot.
    const Box upper{inner.x + line, inner.y + line, inner.w - line * 2.0f,
                    (inner.h - line * 2.0f) * 0.5f};
    const float top[4] = {innerRadius - line, innerRadius - line, 0.0f, 0.0f};
    tip::rounded(canvas, upper, top, gfx::rgba(1.0f, 0.98f, 0.92f, pressed ? 0.02f : 0.06f),
                 gfx::rgba(1.0f, 0.98f, 0.92f, 0.0f));
    if (t > 0.0f) {
        const float lowTall = (inner.h - line * 2.0f) * 0.7f;
        const Box lower{inner.x + line, inner.bottom() - line - lowTall, inner.w - line * 2.0f,
                        lowTall};
        const float foot[4] = {0.0f, 0.0f, innerRadius - line, innerRadius - line};
        tip::rounded(canvas, lower, foot, glow.times(0.0f).packed(), glow.times(0.20f * t).packed());
    }

    // The word, in the windows' title face where it baked, centred on its capitals' height.
    const std::string caps = sheet::shouted(word);
    const float px = size * u;
    const gfx::Face* title = panel::titleFace();
    const gfx::Face& face = title ? *title : canvas.face();
    const float tracking = px * 0.14f;
    const float wide = face.measure(px, caps) + tracking * float(caps.size() - 1);
    const float x = std::round(body.midX() - wide * 0.5f);
    const float baseline = std::round(body.y + (body.h + face.ascent(px) * 0.72f) * 0.5f);
    if (title) {
        canvas.lettered(face, panel::titleTexture(), x + 1.0f, baseline + 1.0f, px, tracking,
                        tip::ink::kDrop, caps);
        canvas.lettered(face, panel::titleTexture(), x, baseline, px, tracking, ink.packed(),
                        caps);
    } else {
        tip::tracked(canvas, x, baseline, px, 0.14f, ink.packed(), caps, 1.0f);
    }
}

// A small arrow for a setting, pointing left or right, bronze and gold under the pointer.
void arrow(gfx::Canvas& canvas, const Box& box, bool right, float lift) {
    const float h = box.h * 0.26f, w = box.h * 0.16f;
    const float cx = box.midX(), cy = box.midY();
    const float s = right ? 1.0f : -1.0f;
    const float xy[6] = {cx - w * s, cy - h, cx + w * s, cy, cx - w * s, cy + h};
    canvas.polygon(nullptr, xy, nullptr, 3, kBronze.times(0.7f).mix(kPale, lift).packed());
}

}  // namespace

void Menu::open(const gfx::Interface& interface) { interface.adopt(canvas_); }

void Menu::show() {
    up_ = true;
    page_ = Page::Main;
    over_ = pressing_ = -1;
    std::fill(std::begin(lift_), std::end(lift_), 0.0f);
}

int Menu::hitAt(float x, float y) const {
    for (int target = 0; target < kTargets; ++target) {
        const Box design = boxOf(page_, target);
        if (design.w <= 0.0f || target == kSwitch) continue;
        // The window's size does not step while the display is the whole screen.
        if (settings_.fullscreen && (target - kRowArrows) / 2 == kSize && target >= kRowArrows) {
            continue;
        }
        if (at(x_, y_, design).has(x, y)) return target;
    }
    return -1;
}

void Menu::update(float seconds, float width, float height, const Pointer& pointer, bool escape,
                  const std::string& place, Result* out) {
    Result result;
    if (up_) {
        const float u = unit();
        width_ = width;
        height_ = height;
        place_ = place;
        x_ = std::round((width - kWide * u) * 0.5f);
        y_ = std::round(height * kCentreShare - tallOf(page_) * u * 0.5f);

        over_ = hitAt(pointer.x, pointer.y);
        const float step = seconds / kLiftSeconds;
        for (int i = 0; i < kTargets; ++i) {
            lift_[i] = over_ == i ? std::min(1.0f, lift_[i] + step) : std::max(0.0f, lift_[i] - step);
        }
        if (pointer.pressed) pressing_ = over_;
        int fired = -1;
        if (pointer.released) {
            if (pressing_ >= 0 && pressing_ == over_) fired = pressing_;
            pressing_ = -1;
        }

        const auto turn = [&](Page to) {
            page_ = to;
            over_ = pressing_ = -1;
            std::fill(std::begin(lift_), std::end(lift_), 0.0f);
            result.clicked = true;
        };
        if (escape) {
            if (page_ == Page::Main) {
                up_ = false;
                result.closed = result.clicked = true;
            } else {
                turn(Page::Main);
            }
        } else if (fired >= 0) {
            switch (fired) {
                case kOptions: turn(Page::Options); break;
                case kBack: turn(Page::Main); break;
                case kExit:
                    result.quit = result.clicked = true;
                    break;
                case kClose:
                    if (page_ == Page::Main) {
                        up_ = false;
                        result.closed = result.clicked = true;
                    } else {
                        turn(Page::Main);
                    }
                    break;
                default:
                    if (fired >= kRowArrows && fired < kRowArrows + kRows * 2) {
                        const int row = (fired - kRowArrows) / 2;
                        const int by = (fired - kRowArrows) % 2 == 1 ? 1 : -1;
                        Settings& set = settings_;
                        switch (row) {
                            case kDisplay: set.fullscreen = !set.fullscreen; break;
                            case kSize:
                                if (!set.sizes.empty()) {
                                    set.size = std::clamp(set.size + by, 0, int(set.sizes.size()) - 1);
                                }
                                break;
                            case kVsync: set.vsync = !set.vsync; break;
                            case kVolume: set.volume = std::clamp(set.volume + by * 10, 0, 100); break;
                            case kCounter: set.fps = !set.fps; break;
                            default: break;
                        }
                        result.settings = result.clicked = true;
                    }
                    break;
            }
        }
        // A page turned this frame is laid out from its own height on the next.
        y_ = std::round(height * kCentreShare - tallOf(page_) * u * 0.5f);
    }
    if (out) *out = result;

    now_ = Drawn{};
    now_.up = up_;
    if (up_) {
        now_.page = page_;
        now_.settings = settings_;
        now_.place = place_;
        now_.width = width_;
        now_.height = height_;
        now_.over = over_;
        now_.pressing = pressing_;
        std::copy(std::begin(lift_), std::end(lift_), std::begin(now_.lift));
    }
    if (built_ && now_ == drawn_) return;
    drawn_ = now_;
    built_ = true;
    rebuild();
}

void Menu::rebuild() {
    canvas_.clear();
    if (!up_) return;
    ++rebuilds_;
    const float u = unit();
    const float line = std::max(1.0f, std::round(u));
    const gfx::Face& face = canvas_.face();
    const auto eased = [&](int i) { return lift_[i] * lift_[i] * (3.0f - 2.0f * lift_[i]); };

    // The world held behind it: dimmed, and darker again towards every edge, so the sheet is
    // the lit thing in the middle of the screen.
    const float w = width_, h = height_;
    canvas_.rect({0.0f, 0.0f, w, h}, gfx::rgba(0.0f, 0.0f, 0.0f, 0.34f));
    const uint32_t edge = gfx::rgba(0.0f, 0.0f, 0.0f, 0.42f), none = gfx::rgba(0.0f, 0.0f, 0.0f, 0.0f);
    canvas_.shade({0.0f, 0.0f, w, h * 0.32f}, edge, edge, none, none);
    canvas_.shade({0.0f, h * 0.68f, w, h * 0.32f}, none, none, edge, edge);
    canvas_.shade({0.0f, 0.0f, w * 0.26f, h}, edge, none, none, edge);
    canvas_.shade({w * 0.74f, 0.0f, w * 0.26f, h}, none, edge, edge, none);

    // The sheet: the windows' own glass and head band, rounder.
    const float tall = tallOf(page_);
    const Box sheetBox = at(x_, y_, {0.0f, 0.0f, kWide, tall});
    // The shadow at a window's scale, not the corner's: the user, 2026-09-27, *"menu shadow is
    // to strong"* -- scaled off the 20-unit corner it reached 2.9 times as far.
    sheet::glass(canvas_, sheetBox, kRadius * u, u);
    sheet::band(canvas_, at(x_, y_, {0.0f, 0.0f, kWide, 110.0f}), true, kRadius * u);

    // The title, in the windows' face and ink.
    const char* titles[3] = {"MENU", "EXIT GAME", "OPTIONS"};
    const std::string title = titles[int(page_)];
    {
        const gfx::Face* gothic = panel::titleFace();
        const gfx::Face& tf = gothic ? *gothic : face;
        const float px = 22.0f * u, tracking = px * 0.34f;
        const float wide = tf.measure(px, title) + tracking * float(title.size() - 1);
        const Box band = at(x_, y_, {0.0f, kTitleTop, kWide, kTitleTall});
        const float x = std::round(band.midX() - wide * 0.5f);
        const float baseline = std::round(band.y + (band.h + tf.ascent(px) * 0.72f) * 0.5f);
        if (gothic) {
            canvas_.lettered(tf, panel::titleTexture(), x + 1.0f, baseline + 1.0f, px, tracking,
                             tip::ink::kDrop, title);
            canvas_.lettered(tf, panel::titleTexture(), x, baseline, px, tracking,
                             sheet::ink::kTitle, title);
        } else {
            tip::tracked(canvas_, x, baseline, px, 0.34f, sheet::ink::kTitle, title, 1.0f);
        }
    }

    const auto rule = [&](float y) {
        sheet::rule(canvas_, x_ + kPadX * u, std::round(y_ + y * u), kInner * u, line);
    };
    const auto big = [&](int target, const char* word, Kind kind) {
        slab(canvas_, at(x_, y_, boxOf(page_, target)), kBigRadius * u, word, 19.0f, kind,
             eased(target), pressing_ == target && over_ == target);
    };
    const auto quiet = [&](int target, const char* word) {
        slab(canvas_, at(x_, y_, boxOf(page_, target)), kQuietRadius * u, word, 15.0f,
             Kind::Plain, eased(target), pressing_ == target && over_ == target);
    };

    {
        const Box close = at(x_, y_, boxOf(page_, kClose));
        sheet::close(canvas_, close, over_ == kClose, pressing_ == kClose && over_ == kClose);
    }

    if (page_ == Page::Main) {
        rule(kFirstRule);
        big(kOptions, "Options", Kind::Plain);
        big(kSwitch, "Switch Character", Kind::Inactive);
        rule(kMainSecondRule);
        big(kExit, "Exit Game", Kind::Danger);

        // The foot: Escape resumes, on the left; where he stands, on the right.
        const float px = 12.0f * u;
        const Box foot = at(x_, y_, {kPadX, kMainFoot, kInner, 16.0f});
        const float baseline = tip::middle(face, foot.y, foot.h, px);
        const std::string key = "ESC";
        const float keyWide = tip::trackedWidth(face, px, 0.12f, key) + 10.0f * u;
        const Box cap{foot.x, foot.y - 2.0f * u, std::round(keyWide), foot.h + 4.0f * u};
        tip::panel(canvas_, cap, 5.0f * u, kBronze.times(0.38f).packed(), kBronze.times(0.38f).packed());
        tip::panel(canvas_, cap.grown(-line), 5.0f * u - line,
                   gfx::rgba(0.047f, 0.044f, 0.040f, 1.0f), gfx::rgba(0.040f, 0.037f, 0.034f, 1.0f));
        tip::tracked(canvas_, cap.x + 5.0f * u, baseline, px, 0.12f, sheet::ink::kTitle, key, 1.0f);
        tip::tracked(canvas_, cap.right() + 8.0f * u, baseline, px, 0.12f, sheet::ink::kQuiet,
                     "RESUME", 1.0f);
        const std::string where = sheet::shouted(place_);
        tip::tracked(canvas_, foot.right() - tip::trackedWidth(face, px, 0.12f, where), baseline,
                     px, 0.12f, sheet::ink::kQuiet, where, 1.0f);
    } else {
        rule(kFirstRule);
        // One rounded well a setting: its name, and its value between two arrows. The window's
        // size is drawn quiet, with no arrows, while the display is the whole screen: fullscreen
        // is the display's own mode and names it.
        const Settings& set = settings_;
        const auto size = [](std::pair<int, int> wh) {
            return std::to_string(wh.first) + " x " + std::to_string(wh.second);
        };
        std::string values[kRows];
        values[kDisplay] = set.fullscreen ? "Fullscreen" : "Windowed";
        values[kSize] = set.fullscreen ? size(set.display)
                        : set.sizes.empty() ? std::string("-")
                                            : size(set.sizes[size_t(std::clamp(
                                                  set.size, 0, int(set.sizes.size()) - 1))]);
        values[kVsync] = set.vsync ? "On" : "Off";
        values[kVolume] = std::to_string(set.volume) + "%";
        values[kCounter] = set.fps ? "On" : "Off";
        for (int i = 0; i < kRows; ++i) {
            const Box row = at(x_, y_, {kPadX, rowTop(i), kInner, kRow});
            tip::panel(canvas_, row, kRowRadius * u, kBronze.times(0.20f).packed(),
                       kBronze.times(0.12f).packed());
            tip::panel(canvas_, row.grown(-line), kRowRadius * u - line,
                       gfx::rgba(0.070f, 0.066f, 0.061f, 1.0f),
                       gfx::rgba(0.058f, 0.054f, 0.050f, 1.0f));
            const float px = 15.0f * u;
            const float baseline = tip::middle(face, row.y, row.h, px);
            sheet::printed(canvas_, row.x + 16.0f * u, baseline, px, sheet::ink::kFigure,
                           kRowNames[i]);
            const int downTarget = kRowArrows + i * 2, upTarget = downTarget + 1;
            const Box down = at(x_, y_, boxOf(page_, downTarget));
            const Box up = at(x_, y_, boxOf(page_, upTarget));
            const bool steps = !(i == kSize && set.fullscreen);
            if (steps) {
                arrow(canvas_, down, false, eased(downTarget));
                arrow(canvas_, up, true, eased(upTarget));
            }
            const float mid = (down.right() + up.x) * 0.5f;
            sheet::printed(canvas_, std::round(mid - face.measure(px, values[i]) * 0.5f),
                           baseline, px, steps ? sheet::ink::kTitle : sheet::ink::kQuiet,
                           values[i]);
        }
        rule(kOptionsRule);
        quiet(kBack, "Back");
    }
}

}  // namespace mu::game
