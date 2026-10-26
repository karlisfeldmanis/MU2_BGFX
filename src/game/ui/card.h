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

// What a figure on the character sheet is the sum of, for its card (card.cpp, explain): his
// points alone, then his gear and bonuses. `x y w h` the figure's row in window units, the card's
// anchor; `up` while there is one.
struct LineParts {
    bool up = false;
    float x = 0, y = 0, w = 0, h = 0;
    std::string name, note;
    std::vector<std::pair<std::string, std::string>> rows;
    bool operator==(const LineParts&) const = default;
};

class Card {
public:
    void open(const gfx::Interface& interface, panel::Arts* arts);

    // A frame. `spend` comes back as the stat a plus was released on (0 strength to 3 energy),
    // or -1, and `count` as how many points it asks (the 1 | 10 | All under the attributes);
    // `reset` as whether the reset was confirmed; `close` as whether the X was. All are
    // requests; the caller answers them. `realm` is asked for what only it reckons (his attack
    // speed, what a reset would take off); the window keeps nothing of it. `name` is his, as the
    // server said it, and heads the window; empty off a server. `wheel` is the mouse wheel this
    // frame, which scrolls a page too tall for the window.
    void update(float width, float height, const sim::Body* hero, const sim::Realm* realm,
                const std::string& name, const Pointer& pointer, float wheel, int* spend, int* count,
                bool* reset, bool* close);

    int tab() const { return tab_; }  // the open page: 0 Attributes, 1 Combat, 2 Bonuses
    bool covers(float x, float y) const;
    // His guild's name, in brackets after his own (docs/game/guild.md); empty for none.
    void setGuild(const std::string& guild) { guild_ = guild; }
    const gfx::Canvas& canvas() const { return canvas_; }
    // The Combat or Bonuses page, clipped to its room between the tabs and the foot: over the
    // window, under the tip.
    const gfx::Canvas& pageCanvas() const { return page_; }
    // Whether a Bonuses heading opened or shut its group this frame, for the desk's click.
    bool folded() const { return folded_; }
    // The card a stat's name raises under the pointer (what a point of it buys his class), over
    // every window as the other tips are.
    const gfx::Canvas& tipCanvas() const { return tip_; }
    uint64_t rebuilds() const { return rebuilds_; }

private:
    struct Line;
    void pipsAt(gfx::Canvas& canvas, const Line& line, float x, float cy, float k);
    // One printed line: a name and its figure, `dim` for a nought shown so its row is known.
    struct Line {
        std::string label, value;
        bool dim = false;
        // The value's ink on the Bonuses page (its element's, its stat's); 0 is the plain bone.
        uint32_t ink = 0;
        // Under a plus the pointer is on: what the press would add, and the value lit in gold.
        std::string gain;
        bool lit = false;
        // A rune's row on the Bonuses page: its name's ink (its rarity's), and of a limited one
        // `pips` rings with the worn set in them, strongest first, in their rarities' inks.
        uint32_t labelInk = 0;
        int pips = 0;
        std::vector<uint32_t> stones;
        // What it does, quiet under its name: "+10% critical chance each".
        std::string note;
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
        // The wells shown: MU's four, five for the Dark Lord, whose Command is the fifth.
        int wells = 4;
        int stats[5] = {};
        Line keys[3];
        std::vector<Line> lines[5];  // what each attribute buys, under its well
        std::vector<Group> groups;   // the page open, when it is not Attributes
        int tab = 0, overTab = -1, pressTab = -1;
        // pushed and over: 0-4 a stat's plus, kStepIds + i a part of the switch, then the reset,
        // its No and its Yes (card.cpp's ids).
        int pushed = -1, over = -1;
        // The stat whose well the pointer is on, off its plus (0 strength to 4 command), or -1:
        // its card is up. `totals` is what it reads, his points with the runes in.
        int overStat = -1;
        int totals[5] = {};
        // The figure the pointer is on, any page or the three at the top: its card.
        LineParts parts;
        // The page's scroll, in units; the Bonuses groups shut by their headings, and the
        // heading under the pointer (-1 none).
        float scroll = 0.0f;
        std::vector<std::string> shut;
        int overHead = -1;
        bool dragging = false;
        bool closing = false, overClose = false;
        int step = 1;          // 1, 10 or 0 for all, as a press now spends (10 is All under ten)
        int returnable = 0;    // the points a reset would take back
        int strips = 0;        // the worn pieces it would take off, -1 no room for them
        bool asking = false;   // the reset's question is up
        bool operator==(const Sheet&) const = default;
    };
    void rebuild();

    gfx::Canvas canvas_;
    gfx::Canvas page_;
    gfx::Canvas tip_;
    panel::Arts* arts_ = nullptr;
    Sheet drawn_, now_;
    float x_ = 0.0f, y_ = 0.0f;
    int tab_ = 0;         // 0 Attributes, 1 Combat, 2 Bonuses
    int pressTab_ = -1;   // the tab a press went down on
    int pushed_ = -1;
    int over_ = -1;       // the control under the pointer, as Sheet::over
    // What a press spends, as he left it: 1, 10 or 0 for all (the user, 2026-10-20). Kept while
    // the game runs.
    int step_ = 1;
    bool asking_ = false;  // the reset's question is up
    bool closing_ = false;
    bool overClose_ = false;
    uint64_t rebuilds_ = 0;
    // The Combat page's rows as last drawn, in window units, for the pointer's next frame: their
    // height follows the face, so only the drawing knows them. {group, line} with each.
    struct Drawn {
        gfx::Box box;
        int group = 0, line = 0;
    };
    std::vector<Drawn> rows_;
    // **The Bonuses page is an accordion, and a page too tall scrolls** (the user, 2026-10-26:
    // "lets implement accordion, with inline scroller if there is a lot of stats"). Each group's
    // heading as last drawn (in page units, unscrolled), with its name; the groups shut, by name,
    // kept while the game runs; the page's scroll and its height as last drawn; the thumb's drag.
    struct Head {
        gfx::Box box;
        std::string name;
    };
    std::vector<Head> heads_;
    std::vector<std::string> shut_;
    int pressHead_ = -1;
    bool folded_ = false;
    float scroll_ = 0.0f, content_ = 0.0f;
    bool dragging_ = false;
    float dragFrom_ = 0.0f, dragScroll_ = 0.0f;
    std::string guild_;
};

}  // namespace mu::game
