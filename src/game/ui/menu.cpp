#include "game/ui/menu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "game/ui/controls.h"
#include "game/ui/panel.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"

namespace mu::game {
namespace {

using controls::Kind;
using gfx::Box;

// ---- the layout ----------------------------------------------------------------------------
//
// Sanctuary's "Game menu and Options" since 2026-09-28, in its own pixels at 1080 lines
// (tip::unit): two windows of the controls' own frame, the menu's three large buttons with Exit
// set apart below an iron rule, and Options as the bench draws it -- a row a setting, stepped by
// chevrons, switched Off and On, or slid. The fat bronze slabs this replaced are
// game/ui/slab.h, which the character screen still draws until it comes across too.

constexpr float kMainWide = 380.0f, kOptionsWide = 430.0f;
constexpr float kMainInset = 26.0f, kOptionsInset = 22.0f;
constexpr float kFootTall = 44.0f;

// The menu: Options and Switch Character, a rule, Exit Game, and the foot.
constexpr float kMainFirst = style::kHead + 22.0f;
constexpr float kMainStep = style::kButtonL + 12.0f;
constexpr float kMainRule = kMainFirst + kMainStep * 2.0f + 2.0f;
constexpr float kMainExit = kMainRule + 14.0f;
constexpr float kMainFoot = kMainExit + style::kButtonL + 16.0f;
constexpr float kMainTall = kMainFoot + kFootTall;

// Options: the Display group's four rows, the Sound group's one, a rule, Back, and the foot.
constexpr float kRowStep = style::kRow + 8.0f;
constexpr float kDisplayKicker = style::kHead + 20.0f;
constexpr float kDisplayRows = kDisplayKicker + 24.0f;
constexpr float kSoundKicker = kDisplayRows + kRowStep * 4.0f + 8.0f;
constexpr float kSoundRows = kSoundKicker + 24.0f;
constexpr float kOptionsRule = kSoundRows + style::kRow + 14.0f;
constexpr float kOptionsBack = kOptionsRule + 12.0f;
constexpr float kOptionsFoot = kOptionsBack + style::kButtonM + 14.0f;
constexpr float kOptionsTall = kOptionsFoot + kFootTall;

// The whole menu at 85% of the page it was drawn from: the user, 2026-09-27, *"scale down menu
// little bit"*. One number, so every measure below keeps its proportion.
constexpr float kScale = 0.85f;
float unit() { return tip::unit() * kScale; }

// Where the sheet's centre stands: a little above the middle, where a thing to be answered
// is looked for.
constexpr float kCentreShare = 0.46f;

enum Target : int {
    kOptions = 0, kSwitch = 1, kExit = 2, kBack = 4, kClose = 9,
    // The Options page's own: two chevrons a stepped row, two halves a switch, and the slider.
    kDisplayDown = 10, kDisplayUp, kSizeDown, kSizeUp, kVsyncOff, kVsyncOn, kCounterOff,
    kCounterOn, kVolume,
};

// The Options rows, top to bottom.
enum Row : int { kDisplay = 0, kSize = 1, kVsync = 2, kCounter = 3, kVolumeRow = 4, kRows = 5 };
constexpr const char* kRowNames[kRows] = {"Display", "Resolution", "V-sync", "Frame rate counter",
                                          "Volume"};

float wideOf(Menu::Page page) { return page == Menu::Page::Main ? kMainWide : kOptionsWide; }
float tallOf(Menu::Page page) { return page == Menu::Page::Main ? kMainTall : kOptionsTall; }

// A row's box on the Options page.
Box rowBox(int row) {
    const float top = row == kVolumeRow ? kSoundRows : kDisplayRows + float(row) * kRowStep;
    return {kOptionsInset, top, kOptionsWide - kOptionsInset * 2.0f, style::kRow};
}

// A target's box on its page, relative to the sheet's corner, or an empty box when the page
// has no such target. The close is where controls::frame puts it.
Box boxOf(Menu::Page page, int target) {
    const float wide = wideOf(page);
    if (target == kClose) {
        const float side = style::kSmallSquare;
        return {wide - 12.0f - side, (style::kHead - side) * 0.5f, side, side};
    }
    if (page == Menu::Page::Main) {
        const float inner = wide - kMainInset * 2.0f;
        if (target == kOptions) return {kMainInset, kMainFirst, inner, style::kButtonL};
        if (target == kSwitch) return {kMainInset, kMainFirst + kMainStep, inner, style::kButtonL};
        if (target == kExit) return {kMainInset, kMainExit, inner, style::kButtonL};
        return {};
    }
    const float inner = wide - kOptionsInset * 2.0f;
    if (target == kBack) return {kOptionsInset, kOptionsBack, inner, style::kButtonM};
    if (target >= kDisplayDown && target <= kSizeUp) {
        const Box row = rowBox((target - kDisplayDown) / 2);
        const bool up = (target - kDisplayDown) % 2 == 1;
        return {row.x + row.w - (up ? 36.0f : 190.0f), row.y + 8.0f, 28.0f, 28.0f};
    }
    if (target >= kVsyncOff && target <= kCounterOn) {
        const Box row = rowBox(kVsync + (target - kVsyncOff) / 2);
        const bool on = (target - kVsyncOff) % 2 == 1;
        return {row.x + row.w - 124.0f + (on ? 58.0f : 0.0f), row.y + 8.0f, 58.0f, 28.0f};
    }
    if (target == kVolume) {
        const Box row = rowBox(kVolumeRow);
        return {row.x + row.w - 220.0f, row.y, 208.0f, row.h};
    }
    return {};
}

Box at(float x, float y, const Box& design) {
    const float u = unit();
    return {std::round(x + design.x * u), std::round(y + design.y * u), std::round(design.w * u),
            std::round(design.h * u)};
}

// The slider's track inside its box, as controls::slider lays it: the figure takes the right 44
// and 12 of air.
Box trackOf(const Box& slider) {
    const float u = unit();
    return {slider.x, slider.y, std::round(slider.w - 44.0f * u - 12.0f * u), slider.h};
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
        if (design.w <= 0.0f || (target == kSwitch && !switchable_)) continue;
        // The window's size does not step while the display is the whole screen.
        if (settings_.fullscreen && (target == kSizeDown || target == kSizeUp)) continue;
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
        x_ = std::round((width - wideOf(page_) * u) * 0.5f);
        y_ = std::round(height * kCentreShare - tallOf(page_) * u * 0.5f);

        over_ = hitAt(pointer.x, pointer.y);
        const float step = seconds / style::kHoverSeconds;
        for (int i = 0; i < kTargets; ++i) {
            lift_[i] = over_ == i ? std::min(1.0f, lift_[i] + step) : std::max(0.0f, lift_[i] - step);
        }
        if (pointer.pressed) pressing_ = over_;
        // The volume follows the hand from the press on the slider to the release, in steps of
        // five, and is applied as it goes: a level is judged by ear while it moves. A click is a
        // press and a release, and sets it where it lands.
        if (page_ == Page::Options && pressing_ == kVolume) {
            const Box track = trackOf(at(x_, y_, boxOf(page_, kVolume)));
            const float share = std::clamp((pointer.x - track.x) / std::max(1.0f, track.w), 0.0f, 1.0f);
            const int volume = int(std::round(share * 20.0f)) * 5;
            if (volume != settings_.volume) {
                settings_.volume = volume;
                result.settings = true;
            }
        }
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
                case kSwitch:
                    up_ = false;
                    result.switched = result.clicked = true;
                    break;
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
                    if (fired >= kDisplayDown && fired <= kCounterOn) {
                        Settings& set = settings_;
                        const int by = fired == kSizeUp ? 1 : -1;
                        switch (fired) {
                            case kDisplayDown:
                            case kDisplayUp: set.fullscreen = !set.fullscreen; break;
                            case kSizeDown:
                            case kSizeUp:
                                if (!set.sizes.empty()) {
                                    set.size = std::clamp(set.size + by, 0, int(set.sizes.size()) - 1);
                                }
                                break;
                            case kVsyncOff: set.vsync = false; break;
                            case kVsyncOn: set.vsync = true; break;
                            case kCounterOff: set.fps = false; break;
                            case kCounterOn: set.fps = true; break;
                            default: break;
                        }
                        result.settings = result.clicked = true;
                    }
                    break;
            }
        }
        // A page turned this frame is laid out from its own size on the next.
        x_ = std::round((width - wideOf(page_) * u) * 0.5f);
        y_ = std::round(height * kCentreShare - tallOf(page_) * u * 0.5f);
    }
    if (out) *out = result;

    now_ = Drawn{};
    now_.up = up_;
    now_.switchable = switchable_;
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
    const auto eased = [&](int i) { return lift_[i] * lift_[i] * (3.0f - 2.0f * lift_[i]); };
    const auto state = [&](int target, bool off = false) {
        return controls::State{off ? 0.0f : eased(target), pressing_ == target && over_ == target, off};
    };

    // The world held behind it: dimmed, and darker again towards every edge, so the sheet is
    // the lit thing in the middle of the screen.
    const float w = width_, h = height_;
    canvas_.rect({0.0f, 0.0f, w, h}, gfx::rgba(0.0f, 0.0f, 0.0f, 0.34f));
    const uint32_t edge = gfx::rgba(0.0f, 0.0f, 0.0f, 0.42f), none = gfx::rgba(0.0f, 0.0f, 0.0f, 0.0f);
    canvas_.shade({0.0f, 0.0f, w, h * 0.32f}, edge, edge, none, none);
    canvas_.shade({0.0f, h * 0.68f, w, h * 0.32f}, none, none, edge, edge);
    canvas_.shade({0.0f, 0.0f, w * 0.26f, h}, edge, none, none, edge);
    canvas_.shade({w * 0.74f, 0.0f, w * 0.26f, h}, none, edge, edge, none);

    const bool main = page_ == Page::Main;
    const Box sheet = at(x_, y_, {0.0f, 0.0f, wideOf(page_), tallOf(page_)});
    controls::frame(canvas_, sheet, u, main ? "Menu" : "Options");
    controls::square(canvas_, at(x_, y_, boxOf(page_, kClose)), controls::Glyph::Close,
                     state(kClose), u);

    // The foot: a key cap and what it does on the left, and on the right where he stands.
    const float footTop = y_ + (main ? kMainFoot : kOptionsFoot) * u;
    controls::foot(canvas_, sheet, footTop, u);
    {
        const float mid = footTop + kFootTall * u * 0.5f;
        const float size = 13.0f * u;
        const float baseline = controls::middle(footTop, kFootTall * u, size);
        const float left = sheet.x + (main ? kMainInset : kOptionsInset) * u;
        const float cap = controls::keycap(canvas_, left, mid, "ESC", u);
        controls::caps(canvas_, left + cap + 8.0f * u, baseline, size, style::kAshInk,
                       main ? "Resume" : "Back", 0.1f);
        if (main && !place_.empty()) {
            const float wide = controls::capsWidth(size, place_, 0.1f);
            controls::caps(canvas_, sheet.right() - kMainInset * u - wide, baseline, size,
                           style::kAshInk, place_, 0.1f);
        }
    }

    if (main) {
        const auto big = [&](int target, const char* word, Kind kind, bool off = false) {
            controls::button(canvas_, at(x_, y_, boxOf(page_, target)), word, kind,
                             state(target, off), u);
        };
        big(kOptions, "Options", Kind::Secondary);
        big(kSwitch, "Switch Character", Kind::Secondary, !switchable_);
        controls::rule(canvas_, sheet.x + kMainInset * u, y_ + kMainRule * u,
                       (kMainWide - kMainInset * 2.0f) * u, u);
        big(kExit, "Exit Game", Kind::Danger);
        return;
    }

    // Options: one row a setting, its value stepped, switched or slid.
    const Settings& set = settings_;
    const float inset = kOptionsInset * u;
    controls::kicker(canvas_, sheet.x + inset, y_ + (kDisplayKicker + 12.0f) * u, "DISPLAY", u);
    controls::kicker(canvas_, sheet.x + inset, y_ + (kSoundKicker + 12.0f) * u, "SOUND", u);
    const auto size = [](std::pair<int, int> wh) {
        return std::to_string(wh.first) + " x " + std::to_string(wh.second);
    };
    const int rowTargets[kRows][2] = {{kDisplayDown, kDisplayUp}, {kSizeDown, kSizeUp},
                                      {kVsyncOff, kVsyncOn}, {kCounterOff, kCounterOn},
                                      {kVolume, kVolume}};
    for (int i = 0; i < kRows; ++i) {
        const Box row = at(x_, y_, rowBox(i));
        const int a = rowTargets[i][0], b = rowTargets[i][1];
        controls::row(canvas_, row, kRowNames[i], std::max(eased(a), eased(b)), u);
        if (i == kDisplay || i == kSize) {
            // The window's size is said quiet, with no chevrons, while the display is the whole
            // screen: fullscreen is the display's own mode and names it.
            const bool steps = !(i == kSize && set.fullscreen);
            const std::string value =
                i == kDisplay ? std::string(set.fullscreen ? "Fullscreen" : "Windowed")
                : set.fullscreen ? size(set.display)
                : set.sizes.empty()
                    ? std::string("-")
                    : size(set.sizes[size_t(std::clamp(set.size, 0, int(set.sizes.size()) - 1))]);
            const Box down = at(x_, y_, boxOf(page_, a)), up = at(x_, y_, boxOf(page_, b));
            if (steps) {
                controls::chevron(canvas_, down, false, eased(a), u);
                controls::chevron(canvas_, up, true, eased(b), u);
            }
            const float px = style::kBodySize * u;
            const float wide = controls::labelWidth(px, value);
            controls::label(canvas_, std::round((down.right() + up.x) * 0.5f - wide * 0.5f),
                            controls::middle(row.y, row.h, px), px,
                            steps ? style::kBoneHi : style::kAshInk, value);
        } else if (i == kVsync || i == kCounter) {
            const Box off = at(x_, y_, boxOf(page_, a)), on = at(x_, y_, boxOf(page_, b));
            controls::toggle(canvas_, {off.x, off.y, on.right() - off.x, off.h}, "Off", "On",
                             i == kVsync ? set.vsync : set.fps, u);
        } else {
            controls::slider(canvas_, at(x_, y_, boxOf(page_, kVolume)), float(set.volume) / 100.0f,
                             std::to_string(set.volume) + "%", u);
        }
    }
    controls::rule(canvas_, sheet.x + inset, y_ + kOptionsRule * u, (kOptionsWide - kOptionsInset * 2.0f) * u, u);
    controls::button(canvas_, at(x_, y_, boxOf(page_, kBack)), "Back", Kind::Secondary,
                     state(kBack), u);
}

}  // namespace mu::game
