#include "game/ui/amount.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "game/ui/controls.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"

namespace mu::game {
namespace {

using gfx::Box;

// Sanctuary's Zen box, in tip::unit() at 1080 lines, laid out as the design page's "Deposit":
// the window's head with its title and close, the question, the field, a line kept for the
// refusal so the box does not jump when one comes, and the answers ranged right.
constexpr float kWide = 400.0f;
constexpr float kInset = 22.0f;  // the body's sides
constexpr float kLineTop = style::kHead + 16.0f, kLineTall = 22.0f;
constexpr float kFieldTop = kLineTop + kLineTall + 10.0f;
constexpr float kNoticeTop = kFieldTop + style::kRow + 6.0f, kNoticeTall = 22.0f;
constexpr float kButtonTop = kNoticeTop + kNoticeTall + 8.0f;
constexpr float kButtonW = 110.0f;
constexpr float kTall = kButtonTop + style::kButtonM + 20.0f;
constexpr int kLimit = 8;  // INPUTBOX_TEXTLIMIT
constexpr float kBlink = 0.5f;
constexpr const char* kButtonWord[2] = {"OK", "Cancel"};

// OK at the right, where the eye ends a box and where Return answers it; Cancel before it; and
// the head's close, which is Cancel again.
Box buttonBox(int which) {
    if (which == 2) {
        const float side = style::kSmallSquare;
        return {kWide - style::kPad - side, (style::kHead - side) * 0.5f, side, side};
    }
    const float x = which == 0 ? kWide - kInset - kButtonW
                               : kWide - kInset - kButtonW * 2.0f - style::kGap;
    return {x, kButtonTop, kButtonW, style::kButtonM};
}

// A box of the layout above, placed at (x, y) in pixels of `u`.
Box placed(float x, float y, const Box& b, float u) {
    return {std::round(x + b.x * u), std::round(y + b.y * u), std::round(b.w * u), std::round(b.h * u)};
}

const char* askingFor(Amount::Purpose purpose) {
    return purpose == Amount::Purpose::Deposit ? "Enter the amount of Zen you would like to deposit."
                                               : "Enter the amount of Zen you would like to withdraw.";
}

}  // namespace

void Amount::open(const gfx::Interface& interface, panel::Arts* arts) {
    interface.adopt(canvas_);
    arts_ = arts;
}

void Amount::show(Purpose purpose) {
    up_ = true;
    purpose_ = purpose;
    digits_.clear();
    short_ = false;
    clock_ = 0.0f;
    over_ = pressing_ = -1;
    lift_[0] = lift_[1] = lift_[2] = 0.0f;
}

int Amount::buttonAt(float ux, float uy) const {
    for (int which = 0; which < 3; ++which) {
        if (buttonBox(which).has(ux, uy)) return which;
    }
    return -1;
}

void Amount::update(float seconds, float width, float height, const Pointer& pointer,
                    const std::string& typed, int backspaces, bool enter, bool escape,
                    Result* out) {
    if (up_) {
        const float k = tip::unit();
        x_ = std::round((width - kWide * k) * 0.5f);
        // A little above the middle, where MU stands its message boxes.
        y_ = std::round(height * 0.4f - kTall * k * 0.5f);
        clock_ += seconds;

        // Digits only, eight at most, and no leading nought: UIOPTION_NUMBERONLY.
        const size_t before = digits_.size();
        for (char c : typed) {
            if (c < '0' || c > '9' || int(digits_.size()) >= kLimit) continue;
            if (c == '0' && digits_.empty()) continue;
            digits_ += c;
        }
        for (int i = 0; i < backspaces && !digits_.empty(); ++i) digits_.pop_back();
        // An edit is a new ask, so the last refusal is taken down with it, and the caret shows at
        // once rather than half way through its blink.
        if (digits_.size() != before || backspaces > 0) {
            short_ = false;
            clock_ = 0.0f;
        }

        const float ux = (pointer.x - x_) / k, uy = (pointer.y - y_) / k;
        over_ = buttonAt(ux, uy);
        for (int which = 0; which < 3; ++which) {
            const float step = seconds / style::kHoverSeconds;
            lift_[which] = over_ == which ? std::min(1.0f, lift_[which] + step)
                                          : std::max(0.0f, lift_[which] - step);
        }
        if (pointer.pressed) pressing_ = over_;
        bool ok = enter, cancel = escape;
        if (pointer.released) {
            if (pressing_ >= 0 && pressing_ == over_) (pressing_ == 0 ? ok : cancel) = true;
            pressing_ = -1;
        }
        if (out) {
            if (cancel) {
                out->cancel = true;
            } else if (ok && !digits_.empty()) {
                out->amount = std::atoll(digits_.c_str());
            }
        }
    }

    now_ = Drawn{};
    now_.up = up_;
    if (up_) {
        now_.purpose = purpose_;
        now_.digits = digits_;
        now_.shortOf = short_;
        now_.caret = std::fmod(clock_, kBlink * 2.0f) < kBlink;
        now_.x = x_;
        now_.y = y_;
        now_.scale = tip::unit();
        now_.over = over_;
        now_.pressing = pressing_;
        for (int which = 0; which < 3; ++which) now_.lift[which] = lift_[which];
    }
    if (built_ && now_ == drawn_) return;
    drawn_ = now_;
    built_ = true;
    rebuild();
}

void Amount::rebuild() {
    canvas_.clear();
    if (!up_ || !arts_) return;
    const float x = x_, y = y_, u = tip::unit();
    const float inset = kInset * u;

    // Dims what is behind it a little, as a box that has to be answered does.
    canvas_.rect({0.0f, 0.0f, 1e5f, 1e5f}, gfx::rgba(0.0f, 0.0f, 0.0f, 0.25f));
    controls::frame(canvas_, placed(x, y, {0.0f, 0.0f, kWide, kTall}, u), u,
                    purpose_ == Purpose::Deposit ? "Deposit" : "Withdraw");

    // Each control's hover, smoothstepped, so the lift eases in and settles.
    const auto state = [&](int which, bool off) {
        const float t = lift_[which] * lift_[which] * (3.0f - 2.0f * lift_[which]);
        return controls::State{t, pressing_ == which && over_ == which, off};
    };
    controls::square(canvas_, placed(x, y, buttonBox(2), u), controls::Glyph::Close,
                     state(2, false), u);

    const Box line = placed(x, y, {0.0f, kLineTop, kWide, kLineTall}, u);
    controls::label(canvas_, line.x + inset, controls::middle(line.y, line.h, style::kBodySize * u),
                    style::kBodySize * u, style::kBone2, askingFor(purpose_));

    // The field, the sum grouped as the vault prints Zen, and rimmed red while it asks for more
    // than there is.
    const std::string shown =
        digits_.empty() ? std::string() : panel::commas(std::atoll(digits_.c_str()));
    controls::field(canvas_, placed(x, y, {kInset, kFieldTop, kWide - kInset * 2.0f, style::kRow}, u),
                    shown, "", true, short_, now_.caret, false, u);
    if (short_) {
        const Box notice = placed(x, y, {0.0f, kNoticeTop, kWide, kNoticeTall}, u);
        controls::message(canvas_, notice.x + inset, controls::middle(notice.y, notice.h, 14.0f * u),
                          "You are short of Zen.", u);
    }

    // OK is the primary, Return's answer, and stands inactive until there is a sum to give it:
    // an empty field does nothing on OK, as in MU, and now says so before it is pressed.
    controls::button(canvas_, placed(x, y, buttonBox(1), u), kButtonWord[1],
                     controls::Kind::Secondary, state(1, false), u);
    controls::button(canvas_, placed(x, y, buttonBox(0), u), kButtonWord[0],
                     controls::Kind::Primary, state(0, digits_.empty()), u);
}

}  // namespace mu::game
