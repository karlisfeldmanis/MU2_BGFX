// The bug report: a window a player writes what went wrong in, sent home to the website
// (launcher/bug_report.h) and read on the admins' /bugs page.
//
// The user, 2026-10-16: "we need to add buttons report bug and find perfect place where to put
// in on game", then: an in-game form sent home, with where and who; a small HUD icon; a website
// admin page. It is opened from the small bug button on the minimap's rim (game/ui/minimap.h),
// WoW's place for the small buttons that are always there and never in the fight's way, beside
// the map name and the tile the report sends.
//
// Sanctuary's controls, as the Zen box (game/ui/amount.h) is: a window headed Report a Bug with
// its close, a line asking, a well the words wrap in, what goes with it (where he stands, who he
// is), and Cancel and Send. Enter starts a new line; Send is the only way out that sends. Sent,
// it thanks him and goes down by itself; refused, it says why and keeps his words.
//
// Modal while it is up, as the Zen box is: the desk gives it the pointer and every key, so a
// letter typed into it is not a window's key.
#pragma once

#include <cstdint>
#include <string>

#include "game/ui/hud.h"
#include "gfx/interface.h"

namespace mu::game {

class Report {
public:
    struct Result {
        bool cancel = false;  // Cancel, the close or Escape: down, the words dropped
        bool send = false;    // Send: words() is what to send, and answered() follows
        bool clicked = false;
    };

    void open(const gfx::Interface& interface);

    // Up, empty, with `about` as what goes with it: "Lorencia  ·  135, 128  ·  Aldric, Dark
    // Knight, level 42".
    void show(const std::string& about);
    void hide() { up_ = false; }
    bool up() const { return up_; }
    const std::string& words() const { return text_; }

    // The site's answer to the last Send.
    void answered(bool sent, const std::string& why);

    void update(float seconds, float width, float height, const Pointer& pointer,
                const std::string& typed, int backspaces, bool enter, bool escape, Result* out);

    const gfx::Canvas& canvas() const { return canvas_; }

private:
    enum class State : uint8_t { Writing, Sending, Sent, Refused };
    int buttonAt(float ux, float uy) const;
    void rebuild();

    gfx::Canvas canvas_;
    bool up_ = false;
    State state_ = State::Writing;
    std::string text_, about_, why_;
    float clock_ = 0.0f;     // the caret's blink
    float sentFor_ = 0.0f;   // how long the thanks has been up
    float x_ = 0.0f, y_ = 0.0f;
    int over_ = -1, pressing_ = -1;       // 0 Send, 1 Cancel, 2 the head's close
    float lift_[3] = {0.0f, 0.0f, 0.0f};
    struct Drawn {
        bool up = false;
        State state = State::Writing;
        std::string text, about, why;
        bool caret = false;
        float x = 0, y = 0, scale = 0;
        int over = -1, pressing = -1;
        float lift[3] = {0.0f, 0.0f, 0.0f};
        bool operator==(const Drawn& o) const {
            return up == o.up && state == o.state && text == o.text && about == o.about && why == o.why &&
                   caret == o.caret && x == o.x && y == o.y && scale == o.scale && over == o.over &&
                   pressing == o.pressing && lift[0] == o.lift[0] && lift[1] == o.lift[1] &&
                   lift[2] == o.lift[2];
        }
    };
    Drawn drawn_, now_;
    bool built_ = false;
};

}  // namespace mu::game
