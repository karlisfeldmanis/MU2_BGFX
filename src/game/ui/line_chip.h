// The line's chip, top right beside the minimap: the ping and the frame rate in one small pill, and
// under it, when
// the line is slow and another region would answer him much sooner, a small orange note saying
// which (the user, 2026-10-20: "we need somekind of notification in game where we inform that you
// are getting much ping, maybe you should check other realms closer to your location").
//
// The proposal of 2026-10-20 (claude.ai/artifact/1xusGyH3djsC6c5R1e1iVS), B1 as the user tuned it:
// the chip moved from under the minimap to the bottom left "with some opacity", see-through at
// rest and whole under the pointer; the note "orangish ... above the block", "smaller", and with a
// cross that hides it for good ("alow user to close notification and dont show it anymore").
// Then, in the game, "overlay is to large" (all of it at three quarters) and "move container to top
// right": beside the minimap, where the old counter stood, its note and list hanging under it.
// A click on the chip opens the regions under it, each with its round trip from here, his own
// marked and a much nearer one too. It takes the old counter's place in a played world.
//
// A mirror: told what to show each frame, and rebuilt only when that moved.
#pragma once

#include <string>
#include <vector>

#include "game/ui/hud.h"
#include "gfx/interface.h"

namespace mu::game {

class LineChip {
public:
    // What it shows, from the mode (app/modes/play_mode.cpp), every frame.
    struct Line {
        int fps = -1;       // the frame rate, -1 with the counter off (Options)
        float ping = -1.0f;  // his round trip to the server, -1 off a server or not yet timed
        int here = -1;      // his region, by net::regions() index; -1 a server that is none of them
        // Each region's connect timed from here (net::pingAll), empty until it is done; -1 for one
        // that did not answer.
        std::vector<double> pings;
        int nearer = -1;  // the region the note names, -1 for no note
    };
    void open(const gfx::Interface& interface) { interface.adopt(canvas_); }
    void close() {
        canvas_.clear();
        built_ = false;
    }
    // One frame. The chip's right edge stands at `right` and its top at `top`, in pixels; its note
    // and its list hang under it.
    void update(float seconds, const Line& line, const Pointer& pointer, int width, int height,
                float right, float top);
    bool covers(float x, float y) const {
        return showing() && (chip_.has(x, y) || (noteUp_ && note_.has(x, y)) || (listUp_ && list_.has(x, y)));
    }
    bool showing() const { return built_ && chip_.w > 0.0f; }
    const gfx::Canvas& canvas() const { return canvas_; }
    // The list opened since this was last asked: the mode times the regions to fill it.
    bool takeOpened() {
        const bool was = opened_;
        opened_ = false;
        return was;
    }
    // A press it took this frame (the chip, the cross), for the desk's click.
    bool takeClicked() {
        const bool was = clicked_;
        clicked_ = false;
        return was;
    }
    // The note's cross was pressed since this was last asked: never again (options.txt).
    bool takeDismissed() {
        const bool was = dismissed_;
        dismissed_ = false;
        return was;
    }

private:
    struct Drawn {
        int width = 0, height = 0, plateTop = 0;
        int fps = -1, ping = -1, here = -1, nearer = -1;
        std::vector<int> pings;
        bool listUp = false, noteUp = false, hover = false, crossHover = false;
        int alpha = 0;  // in 64ths
        bool operator==(const Drawn& o) const {
            return width == o.width && height == o.height && plateTop == o.plateTop && fps == o.fps &&
                   ping == o.ping && here == o.here && nearer == o.nearer && pings == o.pings &&
                   listUp == o.listUp && noteUp == o.noteUp && hover == o.hover &&
                   crossHover == o.crossHover && alpha == o.alpha;
        }
    };
    void rebuild(const Drawn& now);

    gfx::Canvas canvas_;
    Drawn drawn_, now_;
    bool built_ = false;
    bool listUp_ = false, noteUp_ = false, opened_ = false, dismissed_ = false, clicked_ = false;
    float alpha_ = 0.0f;  // the chip's see-through, eased toward its rest, slow or lit
    gfx::Box chip_, note_, cross_, list_;
    // The list's foot in lines, wrapped for the width it was wrapped at.
    std::vector<std::string> foot_;
    float footFor_ = -1.0f;
};

}  // namespace mu::game
