// A player's right-click menu: the one menu whether he is right-clicked in the world, on his party
// capsule or on the target frame (the user, 2026-10-10: "we will ne right click UI which we use
// for party where we can whiper/invite/inspect/trade etc..."). Moved out of the party bar, which
// had it first.
//
// Headed by his name, then a row each: Whisper; while you lead, Make leader and Kick on a member;
// Invite to party on anyone of no party of yours while you lead or are in none; Inspect and Trade.
// Leave party is never on another player's menu: it is "Leave" on your own line over the party
// bar (the user, 2026-10-11). Inspect and Trade live while he is on this map, the mirror's own
// (game/ui/inspect.h; sim/realm_deal.cpp), and greyed when he is not. The asks go to the server
// (Play::askParty); a whisper is the chat's, taken by the desk from whisper(). On a server only:
// off one there is nobody to ask.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "game/ui/hud.h"
#include "gfx/interface.h"

namespace mu::game {

class Play;

class PlayerMenu {
public:
    void open(const gfx::Interface& interface) { interface.adopt(canvas_); }
    // Opens on `name` with its top left near `x`, `y` (pixels), kept on a screen `width` by
    // `height`, its rows by where he stands with you. A press that opened it is the opener's;
    // the menu answers the next.
    void show(Play& play, const std::string& name, float x, float y, float width, float height);
    void hide();
    // A frame, before anything else that answers the pointer: a row taken or the menu shut by a
    // press anywhere. Returns whether the menu took this frame's press, which nothing under it
    // may then take too.
    bool update(Play& play, const Pointer& pointer);
    bool up() const { return up_; }
    bool covers(float x, float y) const { return up_ && box_.has(x, y); }
    std::string whisper() { return std::exchange(whisper_, std::string()); }
    // And the one whose Inspect row was taken, once: the desk opens his window.
    std::string inspect() { return std::exchange(inspect_, std::string()); }
    const gfx::Canvas& canvas() const { return canvas_; }

private:
    void place(Play& play, float x, float y, float width, float height);
    void draw();

    struct Row {
        std::string word;
        bool live = true;    // greyed and deaf when false: a thing not built yet
        bool danger = false; // Kick, Leave: in the danger ink
    };
    gfx::Canvas canvas_;
    bool up_ = false;
    bool fresh_ = false;  // opened this frame: this frame's press is not its own
    std::string name_;
    std::vector<Row> rows_;
    gfx::Box box_;
    int hovered_ = -1;
    float unit_ = 0.0f;
    std::string whisper_;
    std::string inspect_;
};

}  // namespace mu::game
