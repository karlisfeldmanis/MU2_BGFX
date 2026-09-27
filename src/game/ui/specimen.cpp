#include "game/ui/specimen.h"

#include <algorithm>
#include <cmath>

#include "game/ui/controls.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"

namespace mu::game {
namespace {

using controls::Glyph;
using controls::Kind;
using controls::State;
using gfx::Box;

// The two sheets, in tip::unit() at 1080 lines, and the air between them.
constexpr float kLeftWide = 640.0f, kLeftTall = 520.0f;
constexpr float kRightWide = 430.0f, kRightTall = 580.0f;
constexpr float kBetween = 40.0f;
constexpr float kTop = 70.0f;

// The live controls, by what the pointer can be on.
enum Target : int {
    kSizeL = 0, kSizeM, kSizeS,
    kPrimary, kSecondary, kDanger, kQuiet,
    kCloseLeft, kSpend, kCoinIn, kCoinOut, kHammer,
    kCloseRight, kDisplayDown, kDisplayUp, kSizeDown, kSizeUp,
    kRowDisplay, kRowSize, kRowVsync, kRowCounter, kRowVolume,
    kVsyncOff, kVsyncOn, kCounterOff, kCounterOn, kBack,
};

float eased(float t) { return t * t * (3.0f - 2.0f * t); }

}  // namespace

bool Specimen::Drawn::operator==(const Drawn& o) const {
    if (width != o.width || height != o.height || over != o.over || pressing != o.pressing ||
        caret != o.caret || vsync != o.vsync || counter != o.counter) {
        return false;
    }
    for (int i = 0; i < kTargets; ++i) {
        if (lift[i] != o.lift[i]) return false;
    }
    return true;
}

void Specimen::open(const gfx::Interface& interface) { interface.adopt(canvas_); }

void Specimen::close() {}

bool Specimen::covers(float x, float y) const {
    return sheets_[0].has(x, y) || sheets_[1].has(x, y);
}

int Specimen::hitAt(float x, float y) const {
    for (const auto& [id, box] : targets_) {
        if (box.has(x, y)) return id;
    }
    return -1;
}

void Specimen::update(float seconds, float width, float height, const Pointer& pointer) {
    width_ = width;
    height_ = height;
    clock_ += seconds;
    over_ = hitAt(pointer.x, pointer.y);
    if (pointer.pressed) pressing_ = over_;
    if (pointer.released) {
        if (pressing_ >= 0 && pressing_ == over_) {
            if (over_ == kVsyncOff) vsync_ = false;
            if (over_ == kVsyncOn) vsync_ = true;
            if (over_ == kCounterOff) counter_ = false;
            if (over_ == kCounterOn) counter_ = true;
        }
        pressing_ = -1;
    }
    if (!pointer.held) pressing_ = -1;
    const float step = seconds / style::kHoverSeconds;
    for (int i = 0; i < kTargets; ++i) {
        lift_[i] = std::clamp(lift_[i] + (i == over_ ? step : -step), 0.0f, 1.0f);
    }

    Drawn now;
    now.width = width_;
    now.height = height_;
    now.over = over_;
    now.pressing = pressing_;
    std::copy(lift_, lift_ + kTargets, now.lift);
    now.caret = std::fmod(clock_, style::kCaretSeconds) < style::kCaretSeconds * style::kCaretLit;
    now.vsync = vsync_;
    now.counter = counter_;
    if (now == drawn_) return;
    drawn_ = now;
    rebuild();
}

void Specimen::rebuild() {
    canvas_.clear();
    targets_.clear();
    const float u = tip::unit();
    const float total = (kLeftWide + kBetween + kRightWide) * u;
    const float x0 = std::round((width_ - total) * 0.5f);
    const float y0 = std::round(kTop * u);
    const Box left{x0, y0, kLeftWide * u, kLeftTall * u};
    const Box right{x0 + (kLeftWide + kBetween) * u, y0, kRightWide * u, kRightTall * u};
    sheets_[0] = left;
    sheets_[1] = right;

    // A live control's state off the pointer; a frozen one says its own.
    const auto live = [&](int id, bool off = false) {
        State s;
        s.lift = eased(drawn_.lift[id]);
        s.held = pressing_ == id && over_ == id;
        s.off = off;
        return s;
    };
    const auto place = [&](int id, const Box& box) {
        targets_.push_back({id, box});
        return box;
    };
    // A rectangle in a sheet's own units from its corner.
    const auto in = [&](const Box& sheet, float x, float y, float w, float h) {
        return Box{std::round(sheet.x + x * u), std::round(sheet.y + y * u), std::round(w * u),
                   std::round(h * u)};
    };

    // ---- the left sheet: every button, the icons, the meters ---------------------------------
    Box close;
    controls::frame(canvas_, left, u, "Sanctuary", &close);
    controls::square(canvas_, place(kCloseLeft, close), Glyph::Close, live(kCloseLeft), u);
    const float pad = style::kPad;
    const float mid = style::kButtonM, big = style::kButtonL, small = style::kButtonS;

    controls::kicker(canvas_, left.x + pad * u, left.y + 76.0f * u, "SIZES", u);
    {
        const float y = 88.0f;
        controls::button(canvas_, place(kSizeL, in(left, pad, y, 230, big)), "Create Character",
                         Kind::Secondary, live(kSizeL), u);
        controls::button(canvas_, place(kSizeM, in(left, pad + 242, y + (big - mid) * 0.5f, 140, mid)),
                         "Options", Kind::Secondary, live(kSizeM), u);
        controls::button(canvas_, place(kSizeS, in(left, pad + 394, y + (big - small) * 0.5f, 120, small)),
                         "Dark Knight", Kind::Secondary, live(kSizeS), u);
    }
    controls::kicker(canvas_, left.x + pad * u, left.y + 172.0f * u, "KINDS", u);
    {
        const float y = 184.0f;
        controls::button(canvas_, place(kPrimary, in(left, pad, y, 160, mid)), "Enter World",
                         Kind::Primary, live(kPrimary), u);
        controls::button(canvas_, place(kSecondary, in(left, pad + 172, y, 116, mid)), "Cancel",
                         Kind::Secondary, live(kSecondary), u);
        controls::button(canvas_, place(kDanger, in(left, pad + 300, y, 116, mid)), "Delete",
                         Kind::Danger, live(kDanger), u);
        State off;
        off.off = true;
        controls::button(canvas_, in(left, pad + 428, y, 100, mid), "Switch", Kind::Secondary, off, u);
        controls::button(canvas_, place(kQuiet, in(left, pad + 536, y, 72, mid)), "Back", Kind::Quiet,
                         live(kQuiet), u);
    }
    controls::kicker(canvas_, left.x + pad * u, left.y + 256.0f * u, "STATES  REST  OVER  HELD  OFF", u);
    {
        const State states[4] = {{0.0f, false, false}, {1.0f, false, false}, {0.0f, true, false},
                                 {0.0f, false, true}};
        for (int i = 0; i < 4; ++i) {
            controls::button(canvas_, in(left, pad + float(i) * 132.0f, 268, 120, small), "Cancel",
                             Kind::Secondary, states[i], u);
            controls::button(canvas_, in(left, pad + float(i) * 132.0f, 310, 120, small), "Enter World",
                             Kind::Primary, states[i], u);
        }
    }
    controls::kicker(canvas_, left.x + pad * u, left.y + 376.0f * u, "ICONS", u);
    {
        const float y = 388.0f, sq = style::kSmallSquare, ic = style::kIconSquare;
        const float dy = (ic - sq) * 0.5f;
        State over;
        over.lift = 1.0f;
        State none;
        none.off = true;
        controls::square(canvas_, in(left, pad, y + dy, sq, sq), Glyph::Close, State{}, u);
        controls::square(canvas_, in(left, pad + 40, y + dy, sq, sq), Glyph::Close, over, u);
        controls::square(canvas_, place(kSpend, in(left, pad + 92, y + dy, sq, sq)), Glyph::Plus,
                         live(kSpend), u, true);
        controls::square(canvas_, in(left, pad + 132, y + dy, sq, sq), Glyph::Plus, over, u, true);
        controls::square(canvas_, in(left, pad + 172, y + dy, sq, sq), Glyph::Plus, none, u, true);
        controls::square(canvas_, place(kCoinIn, in(left, pad + 224, y, ic, ic)), Glyph::CoinIn,
                         live(kCoinIn), u);
        controls::square(canvas_, place(kCoinOut, in(left, pad + 268, y, ic, ic)), Glyph::CoinOut,
                         live(kCoinOut), u);
        controls::square(canvas_, place(kHammer, in(left, pad + 312, y, ic, ic)), Glyph::Hammer,
                         live(kHammer), u);
        controls::square(canvas_, in(left, pad + 356, y, ic, ic), Glyph::Hammer, State{}, u, false, true);
        const float size = 13.0f * u;
        controls::label(canvas_, left.x + (pad + 404) * u, controls::middle(left.y + y * u, ic * u, size),
                        size, style::kAshInk, "close, spend, coins, repair, repair on");
    }
    controls::kicker(canvas_, left.x + pad * u, left.y + 450.0f * u, "METERS", u);
    {
        const float size = 14.0f * u;
        const char* names[3] = {"Experience", "Life", "Mana"};
        const float share[3] = {0.62f, 0.38f, 0.8f};
        const uint32_t top[3] = {gfx::rgba(0.910f, 0.863f, 0.773f), gfx::rgba(0.878f, 0.278f, 0.227f),
                                 gfx::rgba(0.353f, 0.549f, 0.910f)};
        const uint32_t foot[3] = {style::kIron, gfx::rgba(0.557f, 0.114f, 0.082f),
                                  gfx::rgba(0.114f, 0.247f, 0.541f)};
        for (int i = 0; i < 3; ++i) {
            const float y = 462.0f + float(i) * 18.0f;
            controls::label(canvas_, left.x + pad * u, controls::middle(left.y + y * u, 8.0f * u, size),
                            size, style::kBone2, names[i]);
            controls::meter(canvas_, in(left, pad + 100, y + 1, 300, 6), share[i], top[i], foot[i], u);
        }
    }

    // ---- the right sheet: Options, as the menu will draw it ---------------------------------
    controls::frame(canvas_, right, u, "Options", &close);
    controls::square(canvas_, place(kCloseRight, close), Glyph::Close, live(kCloseRight), u);
    const float rowW = kRightWide - pad * 2.0f;
    const auto rowAt = [&](int i) { return 70.0f + float(i) * 52.0f; };
    const auto valueRow = [&](int rowId, int down, int up, int i, const char* name, const char* value) {
        const Box r = place(rowId, in(right, pad, rowAt(i), rowW, style::kRow));
        controls::row(canvas_, r, name, eased(drawn_.lift[rowId]), u);
        const Box dn = place(down, in(right, pad + rowW - 190, rowAt(i) + 8, 28, 28));
        const Box upBox = place(up, in(right, pad + rowW - 36, rowAt(i) + 8, 28, 28));
        controls::chevron(canvas_, dn, false, eased(drawn_.lift[down]), u);
        controls::chevron(canvas_, upBox, true, eased(drawn_.lift[up]), u);
        const float size = style::kBodySize * u;
        const float wide = controls::labelWidth(size, value);
        controls::label(canvas_, std::round((dn.right() + upBox.x) * 0.5f - wide * 0.5f),
                        controls::middle(r.y, r.h, size), size, style::kBoneHi, value);
    };
    valueRow(kRowDisplay, kDisplayDown, kDisplayUp, 0, "Display", "Windowed");
    valueRow(kRowSize, kSizeDown, kSizeUp, 1, "Resolution", "1600 x 900");
    const auto switchRow = [&](int rowId, int offId, int onId, int i, const char* name, bool on) {
        const Box r = place(rowId, in(right, pad, rowAt(i), rowW, style::kRow));
        controls::row(canvas_, r, name, eased(drawn_.lift[rowId]), u);
        const Box sw = in(right, pad + rowW - 124, rowAt(i) + 8, 116, 28);
        targets_.push_back({offId, {sw.x, sw.y, std::round(sw.w * 0.5f), sw.h}});
        targets_.push_back({onId, {sw.x + std::round(sw.w * 0.5f), sw.y, sw.w - std::round(sw.w * 0.5f), sw.h}});
        controls::toggle(canvas_, sw, "Off", "On", on, u);
    };
    switchRow(kRowVsync, kVsyncOff, kVsyncOn, 2, "V-sync", drawn_.vsync);
    switchRow(kRowCounter, kCounterOff, kCounterOn, 3, "Frame rate counter", drawn_.counter);
    {
        const Box r = place(kRowVolume, in(right, pad, rowAt(4), rowW, style::kRow));
        controls::row(canvas_, r, "Volume", eased(drawn_.lift[kRowVolume]), u);
        controls::slider(canvas_, in(right, pad + rowW - 220, rowAt(4), 208, style::kRow), 0.72f, "72%", u);
    }
    controls::kicker(canvas_, right.x + pad * u, right.y + 348.0f * u, "FIELDS", u);
    controls::field(canvas_, in(right, pad, 360, rowW, style::kRow), "Gorm", "", true, false,
                    drawn_.caret, false, u);
    controls::field(canvas_, in(right, pad, 414, rowW, style::kRow), "60 000", "", false, true,
                    false, false, u);
    controls::message(canvas_, right.x + pad * u, right.y + 478.0f * u,
                      "You are short of Zen. You carry 48 920.", u);
    controls::rule(canvas_, right.x + pad * u, right.y + 500.0f * u, rowW * u, u);
    controls::button(canvas_, place(kBack, in(right, pad, 518, rowW, mid)), "Back", Kind::Secondary,
                     live(kBack), u);
}

}  // namespace mu::game
