#include "game/ui/amount.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "game/ui/sheet.h"

namespace mu::game {
namespace {

using gfx::Box;

// In the panels' units, so the box is drawn at the windows' own scale. MU's message box is
// 352 by 113 at twice; this is the same shape, a little narrower for a single line.
constexpr float kWide = 230.0f, kTall = 100.0f;
constexpr Box kLine{0.0f, 10.0f, kWide, 18.0f};
constexpr Box kField{35.0f, 32.0f, 160.0f, 20.0f};
constexpr Box kNotice{0.0f, 54.0f, kWide, 12.0f};
constexpr float kButtonW = 58.0f, kButtonH = 19.0f, kButtonTop = 70.0f, kButtonGap = 16.0f;
constexpr int kLimit = 8;  // INPUTBOX_TEXTLIMIT
constexpr float kBlink = 0.5f;
// How long a button takes to lift under the pointer and settle when it leaves: quick enough to
// read as an answer to the hand, slow enough not to flicker when the pointer crosses it.
constexpr float kLiftSeconds = 0.12f;
constexpr const char* kButtonWord[2] = {"OK", "Cancel"};

Box buttonBox(int which) {
    const float x = which == 0 ? kWide * 0.5f - kButtonGap * 0.5f - kButtonW
                               : kWide * 0.5f + kButtonGap * 0.5f;
    return {x, kButtonTop, kButtonW, kButtonH};
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
    lift_[0] = lift_[1] = 0.0f;
}

int Amount::buttonAt(float ux, float uy) const {
    for (int which = 0; which < 2; ++which) {
        if (buttonBox(which).has(ux, uy)) return which;
    }
    return -1;
}

void Amount::update(float seconds, float width, float height, const Pointer& pointer,
                    const std::string& typed, int backspaces, bool enter, bool escape,
                    Result* out) {
    if (up_) {
        const float k = panel::scale();
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
        for (int which = 0; which < 2; ++which) {
            const float step = seconds / kLiftSeconds;
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
        now_.scale = panel::scale();
        now_.over = over_;
        now_.pressing = pressing_;
        now_.lift[0] = lift_[0];
        now_.lift[1] = lift_[1];
    }
    if (built_ && now_ == drawn_) return;
    drawn_ = now_;
    built_ = true;
    rebuild();
}

void Amount::rebuild() {
    canvas_.clear();
    if (!up_ || !arts_) return;
    const float x = x_, y = y_, k = panel::scale();
    const gfx::Face& face = canvas_.face();

    // Dims what is behind it a little, as a box that has to be answered does.
    canvas_.rect({0.0f, 0.0f, 1e5f, 1e5f}, gfx::rgba(0.0f, 0.0f, 0.0f, 0.25f));
    sheet::glass(canvas_, panel::scaled(x, y, {0.0f, 0.0f, kWide, kTall}), panel::kRadius * k);

    const float lineSize = 8.5f * k;
    const std::string line = askingFor(purpose_);
    const Box lineBox = panel::scaled(x, y, kLine);
    sheet::printed(canvas_, std::round(lineBox.midX() - face.measure(lineSize, line) * 0.5f),
                   panel::centredBaseline(face, lineBox, lineSize), lineSize, panel::kLettering,
                   line);

    // The field: a well with the sum ranged right in it, grouped as the vault prints Zen, and
    // the caret after it.
    const Box field = panel::scaled(x, y, kField);
    sheet::well(canvas_, field, std::max(1.0f, k * 0.5f));
    const float size = 9.5f * k;
    const std::string shown = digits_.empty() ? std::string() : panel::commas(std::atoll(digits_.c_str()));
    const float right = field.x + field.w - 8.0f * k;
    const float baseline = panel::centredBaseline(face, field, size);
    if (!shown.empty()) sheet::ranged(canvas_, right, baseline, size, panel::kOrdinary, shown);
    if (now_.caret) {
        canvas_.rect({right + 1.5f * k, baseline - face.ascent(size), std::max(1.0f, k),
                      face.ascent(size) * 1.1f},
                     panel::kOrdinary);
    }

    if (short_) {
        const float noticeSize = 7.5f * k;
        const std::string notice = "You are short of Zen.";
        const Box noticeBox = panel::scaled(x, y, kNotice);
        sheet::printed(canvas_,
                       std::round(noticeBox.midX() - face.measure(noticeSize, notice) * 0.5f),
                       panel::centredBaseline(face, noticeBox, noticeSize), noticeSize,
                       panel::kUnmet, notice);
    }

    // In the skin rather than MU's leather plates, which the user found crude on the glass:
    // OK the answer the box expects, in gold, and Cancel quiet.
    for (int which = 0; which < 2; ++which) {
        // Smoothstepped, so the lift eases in and settles rather than running at a rate.
        const float t = lift_[which] * lift_[which] * (3.0f - 2.0f * lift_[which]);
        sheet::button(canvas_, panel::scaled(x, y, buttonBox(which)), kButtonWord[which],
                      which == 0, t, pressing_ == which && over_ == which, which == 0);
    }
}

}  // namespace mu::game
