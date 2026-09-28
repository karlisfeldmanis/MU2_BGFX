// The box a sum of Zen is typed into: the vault's deposit and withdrawal.
//
// MU's CZenReceiptMsgBoxLayout and CZenPaymentMsgBoxLayout (NewUICustomMessageBox.cpp): a line
// asking how much, a field that takes digits only and at most eight of them
// (INPUTBOX_TEXTLIMIT, UIOPTION_NUMBERONLY), and OK and Cancel. Return is OK and Escape is
// Cancel. An empty field or a nought does nothing on OK, and the box stays up -- MU's
// CALLBACK_CONTINUE.
//
// Drawn in Sanctuary's controls since 2026-09-28 (game/ui/controls.h): a window headed Deposit or
// Withdraw with its close, a text field, and Cancel and OK with OK the primary. MU's leather
// plates read crude, and the skin's own buttons were the last of `sheet::button` in a window.
//
// **One departure**: asked for more than there is, MU shuts the box and opens a second one that
// says "You are short of Zen." Here the box stays up with that line in red under the field and
// the figure still in it, so the next try is an edit rather than a retype.
//
// Modal while it is up: the desk gives it the pointer and the keyboard and nothing else hears
// either, so a 1 typed into it is not the first potion drunk.
#pragma once

#include <cstdint>
#include <string>

#include "game/ui/hud.h"
#include "game/ui/panel.h"
#include "gfx/interface.h"

namespace mu::game {

class Amount {
public:
    enum class Purpose : uint8_t { Deposit, Withdraw };
    struct Result {
        bool cancel = false;
        int64_t amount = 0;  // a sum confirmed, or 0 for nothing this frame
    };

    void open(const gfx::Interface& interface, panel::Arts* arts);

    void show(Purpose purpose);
    void hide() { up_ = false; }
    bool up() const { return up_; }
    Purpose purpose() const { return purpose_; }
    // The last OK was more than there is: says so, and keeps the box up.
    void refuse() { short_ = true; }

    void update(float seconds, float width, float height, const Pointer& pointer,
                const std::string& typed, int backspaces, bool enter, bool escape, Result* out);

    const gfx::Canvas& canvas() const { return canvas_; }

private:
    int buttonAt(float ux, float uy) const;
    void rebuild();

    gfx::Canvas canvas_;
    panel::Arts* arts_ = nullptr;
    bool up_ = false;
    Purpose purpose_ = Purpose::Deposit;
    std::string digits_;
    bool short_ = false;
    float clock_ = 0.0f;
    float x_ = 0.0f, y_ = 0.0f;
    int over_ = -1, pressing_ = -1;       // 0 OK, 1 Cancel, 2 the head's close
    float lift_[3] = {0.0f, 0.0f, 0.0f};  // each button's hover, eased
    struct Drawn {
        bool up = false;
        Purpose purpose = Purpose::Deposit;
        std::string digits;
        bool shortOf = false, caret = false;
        float x = 0, y = 0, scale = 0;
        int over = -1, pressing = -1;
        float lift[3] = {0.0f, 0.0f, 0.0f};
        bool operator==(const Drawn& o) const {
            return up == o.up && purpose == o.purpose && digits == o.digits &&
                   shortOf == o.shortOf && caret == o.caret && x == o.x && y == o.y &&
                   scale == o.scale && over == o.over && pressing == o.pressing &&
                   lift[0] == o.lift[0] && lift[1] == o.lift[1] && lift[2] == o.lift[2];
        }
    };
    Drawn drawn_, now_;
    bool built_ = false;
};

}  // namespace mu::game
