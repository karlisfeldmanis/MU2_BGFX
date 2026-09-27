// The game menu, on Escape: Options, Switch Character, Exit Game.
//
// MU's CNewUISystemMenu is a column of worded buttons over the world -- Exit Game, Server
// Select, Character Select, Option, Close -- and this is that column in the windows' skin,
// chosen by the user on 2026-09-27 from a page drawn over Lorencia: *"same style as other ui,
// clean, fat but with diablo 4 vibe"*, then *"cleaner with rounder radius"*, then *"a little
// bit simpler, without icons"*. A centred sheet, three fat rounded slabs with an engraved
// inner line, a warm light rising under the one the pointer is on, and Exit set apart below a
// rule and lit red.
//
// Three pages in one sheet: the menu; Exit's one question; and Options, which offers only what
// can change while the game runs -- the volume and the frame-rate counter. Switch Character is
// drawn and does nothing: there is no character select until sprint 9, and the user asked for
// the button to stand there inactive until there is.
//
// Modal, as the number box is: while it is up the desk gives it the pointer and every key, and
// the realm is held (PlayMode steps it by nothing).
#pragma once

#include <cstdint>
#include <string>

#include "game/ui/hud.h"
#include "gfx/interface.h"

namespace mu::game {

class Menu {
public:
    enum class Page : uint8_t { Main, Confirm, Options };
    struct Result {
        bool closed = false;    // Resume: the menu went down this frame
        bool quit = false;      // Exit to Desktop
        bool clicked = false;   // a button answered, for the interface's click
        bool settings = false;  // the volume or the counter changed
    };

    void open(const gfx::Interface& interface);

    void show();
    void hide() { up_ = false; }
    bool up() const { return up_; }

    // The settings Options edits. Set once from the run; read back when `settings` says so.
    void setVolume(int percent) { volume_ = percent; }
    int volume() const { return volume_; }
    void setFps(bool on) { fps_ = on; }
    bool fps() const { return fps_; }

    // `escape` backs out a page, and off the menu shuts it. `place` is the foot's right-hand
    // line: where he is standing.
    void update(float seconds, float width, float height, const Pointer& pointer, bool escape,
                const std::string& place, Result* out);

    const gfx::Canvas& canvas() const { return canvas_; }
    uint64_t rebuilds() const { return rebuilds_; }

private:
    int hitAt(float x, float y) const;
    void rebuild();

    gfx::Canvas canvas_;
    bool up_ = false;
    Page page_ = Page::Main;
    int volume_ = 100;
    bool fps_ = true;
    std::string place_;
    float width_ = 0.0f, height_ = 0.0f;
    float x_ = 0.0f, y_ = 0.0f;  // the sheet's corner, in pixels
    int over_ = -1, pressing_ = -1;
    static constexpr int kTargets = 10;
    float lift_[kTargets] = {};
    struct Drawn {
        bool up = false;
        Page page = Page::Main;
        int volume = 0;
        bool fps = false;
        std::string place;
        float width = 0, height = 0;
        int over = -1, pressing = -1;
        float lift[kTargets] = {};
        bool operator==(const Drawn& o) const {
            if (up != o.up || page != o.page || volume != o.volume || fps != o.fps ||
                place != o.place || width != o.width || height != o.height || over != o.over ||
                pressing != o.pressing) {
                return false;
            }
            for (int i = 0; i < kTargets; ++i) {
                if (lift[i] != o.lift[i]) return false;
            }
            return true;
        }
    };
    Drawn drawn_, now_;
    bool built_ = false;
    uint64_t rebuilds_ = 0;
};

}  // namespace mu::game
