// What's New: the slides a player sees the first time he enters the world after an update, and
// again whenever he asks for them from the game menu.
//
// The user, 2026-10-16: "we need to design ... in game first tutorial with title whats new where
// we in slides introduce rune system, raids, market, scaling updates", then quests and a closing
// "& more" of only what MU never had, picked from the design canvas
// https://claude.ai/artifact/S8hXGeHHD44weomsh9V6Dz (variant B, the wide split): a screenshot on
// the left with its kicker and title over it, on the right the topics as tabs, a line of prose,
// the facts in a well, and Back / Next under a progress line. Skip, Escape or Start playing close
// it and nothing else comes up; "What's New" stays on the game menu's first page.
//
// Modal while it is up, as the bug report is: the desk gives it the pointer and Escape.
#pragma once

#include <array>
#include <cstdint>

#include "game/ui/hud.h"
#include "gfx/interface.h"

namespace mu::game {

namespace panel {
class Arts;
}

// Raised whenever the slides change, so a player who saw the last edition sees the new one once.
// options.txt keeps the edition seen (`whatsnew N`, core/args.cpp).
inline constexpr int kWhatsNewEdition = 2;  // 2: Talents, Elites and the Jeweler

class WhatsNew {
public:
    struct Result {
        bool closed = false;   // Skip, Escape or Start playing: down, and seen
        bool clicked = false;  // a button answered, for the interface's click
    };

    void open(const gfx::Interface& interface, panel::Arts* arts);

    // Up on its first slide, the welcome.
    void show();
    void hide() { up_ = false; }
    bool up() const { return up_; }

    void update(float seconds, float width, float height, const Pointer& pointer, bool escape,
                Result* out);

    const gfx::Canvas& canvas() const { return canvas_; }

    // The slides' count, the welcome included.
    static int slides();

private:
    // What the pointer can press: the eight tabs, Back, Next, Skip, and the welcome's eight topics.
    static constexpr int kTargets = 19;
    int targetAt(float ux, float uy) const;
    void go(int slide);
    void rebuild();

    gfx::Canvas canvas_;
    panel::Arts* arts_ = nullptr;
    bool up_ = false;
    int slide_ = 0;
    float x_ = 0.0f, y_ = 0.0f;
    int over_ = -1, pressing_ = -1;
    std::array<float, kTargets> lift_{};
    struct Drawn {
        bool up = false;
        int slide = 0;
        float x = 0, y = 0, scale = 0;
        int over = -1, pressing = -1;
        std::array<float, kTargets> lift{};
        bool operator==(const Drawn& o) const {
            return up == o.up && slide == o.slide && x == o.x && y == o.y && scale == o.scale &&
                   over == o.over && pressing == o.pressing && lift == o.lift;
        }
    };
    Drawn drawn_, now_;
    bool built_ = false;
};

}  // namespace mu::game
