// The character window: what he is, and where his points go.
//
// MU2's `client/core/Card.cs`, which is `CNewUICharacterInfoWindow` -- the inventory's twin,
// the same 190x429 and the same frame. It keeps the right-hand column always: MU moves the
// INVENTORY out of its way, not this.
//
// Nothing here spends a point. The plus raises a request and the window goes on showing what
// it showed until the character's own numbers move, because a point is permanent and a number
// that moves before the realm agrees is a number that can move back.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/ui/hud.h"
#include "game/ui/panel.h"
#include "gfx/interface.h"
#include "sim/realm.h"

namespace mu::game {

class Card {
public:
    void open(const gfx::Interface& interface, panel::Arts* arts);

    // A frame. `spend` comes back as the stat a plus was released on (0 strength to 3 energy),
    // or -1; `close` as whether the X was. Both are requests; the caller answers them. `realm`
    // is asked for what only it reckons (his attack speed); the window keeps nothing of it.
    // `name` is his, as the server said it, and heads the window; empty off a server.
    void update(float width, float height, const sim::Body* hero, const sim::Realm* realm,
                const std::string& name, const Pointer& pointer, int* spend, bool* close);

    int tab() const { return tab_; }  // the open page: 0 Attributes, 1 Combat, 2 Bonuses
    bool covers(float x, float y) const;
    // His guild's name, in brackets after his own (docs/game/guild.md); empty for none.
    void setGuild(const std::string& guild) { guild_ = guild; }
    const gfx::Canvas& canvas() const { return canvas_; }
    uint64_t rebuilds() const { return rebuilds_; }

private:
    // One printed line: a name and its figure, `dim` for a nought shown so its row is known.
    struct Line {
        std::string label, value;
        bool dim = false;
        bool operator==(const Line&) const = default;
    };
    // A headed block of lines on the Combat and Bonuses pages.
    struct Group {
        std::string name;
        std::vector<Line> lines;
        bool operator==(const Group&) const = default;
    };
    // Everything the drawing reads, so a frame that changes none of it draws nothing.
    struct Sheet {
        const sim::Body* who = nullptr;
        bool second = false;  // his class's second: the heading's name changes with it
        float width = 0, height = 0;
        float top = 0;  // y_: the window comes down once the HUD's plate is laid out
        int level = 0, points = 0, resets = 0;
        unsigned long long experience = 0;
        std::string name, guild;
        int stats[4] = {};
        // Which of the four the stat runes raise, a bit each (Body::runeShare).
        int raised = 0;
        Line keys[3];
        std::vector<Line> lines[4];  // what each attribute buys, under its well
        std::vector<Group> groups;   // the page open, when it is not Attributes
        int tab = 0, overTab = -1, pressTab = -1;
        int pushed = -1, over = -1;
        bool closing = false, overClose = false;
        bool operator==(const Sheet&) const = default;
    };
    void rebuild();

    gfx::Canvas canvas_;
    panel::Arts* arts_ = nullptr;
    Sheet drawn_, now_;
    float x_ = 0.0f, y_ = 0.0f;
    int tab_ = 0;         // 0 Attributes, 1 Combat, 2 Bonuses
    int pressTab_ = -1;   // the tab a press went down on
    int pushed_ = -1;
    int over_ = -1;       // the plus under the pointer
    bool closing_ = false;
    bool overClose_ = false;
    uint64_t rebuilds_ = 0;
    std::string guild_;
};

}  // namespace mu::game
