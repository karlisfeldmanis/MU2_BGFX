// The frame along the bottom of the screen: life and mana in two diamond gems, a row of
// eleven boxes between them, the level as a hairline beneath, and the buttons that open the
// windows.
//
// MU2's `client/core/Hud.cs`, which is `CNewUIMainFrameWindow` in MuDream's clothes. Kept of it
// is exactly what it kept of MuMain: the order the pieces go down in, a gauge filled by CROPPING
// its texture from the waterline rather than by scaling it (`RenderLifeMana`), and a level cut
// into ten (`buildExpSegment`). Every position is MuDream's base-plate pixel, and `kUnit` is the
// one number that turns those into MU's 640x480.
//
// What is not drawn, and why, in the order Hud.cs draws it:
//   * the shield bar: 0.75 has no shield, and MU2's bar was Season 3's carried in on purpose;
//     this sim is smaller and has no such pool, so the bar is not there to hide.
//   * the ability bar: off in MU2 too (`AbilityShown`), because nothing in 0.75 spends it.
//   * the buff strip and the skill fan: there are no skills yet (PLAN.md, their own sprint).
//
// The keys under the boxes are this sprint's: potions on 1 to 4 and skills on Q W E R, decided
// by the user on 2026-09-21. MuDream's plate paints the other arrangement, so its labels are
// covered with the rail's own dark and the real key printed over them.
#pragma once

#include <cstdint>
#include <string>

#include "game/panel.h"
#include "gfx/interface.h"
#include "sim/realm.h"

namespace mu::game {

// The pointer as the windows read it, in backbuffer pixels, with its two edges.
struct Pointer {
    float x = -1.0f, y = -1.0f;
    bool pressed = false;   // the left button went down this frame
    bool released = false;  // and came up
    bool held = false;
    bool rightPressed = false;
};

class Hud {
public:
    enum class Button { Menu, Chat, Inventory, Character };

    // What one of the four potion boxes shows: the row bound to it (or -1), what it is called
    // and how many of it and what may stand in for it he carries. Given by the desk, which owns
    // the binding; the frame draws what it is handed.
    struct Quick {
        int32_t item = -1;
        std::string label;
        int count = 0;
        bool operator==(const Quick& o) const {
            return item == o.item && count == o.count;
        }
    };
    void setQuick(int key, const Quick& quick) {
        if (key >= 0 && key < 4) quick_[key] = quick;
    }
    // Which potion box a point is over, 0 to 3, or -1: where a drag from the bag binds.
    int quickAt(float x, float y) const;

    void open(const gfx::Interface& interface, panel::Arts* arts);
    void follow(const sim::Body* hero);

    // A frame: slides the hairline, answers the pointer, and rebuilds the canvas only if what
    // it draws moved. Returns the button pressed this frame, if any, through `pressed`.
    void update(float seconds, float width, float height, const Pointer& pointer,
                bool inventoryOpen, bool characterOpen, bool* toggleInventory,
                bool* toggleCharacter);

    // Whether a point is over the frame -- plate, rail or a side button -- so a click there
    // is the frame's and not the ground's. The plate's transparent corners count: a click in
    // the gap beside a gem's wing that walked the character is a click nobody meant.
    bool covers(float x, float y) const;

    const gfx::Canvas& canvas() const { return canvas_; }
    // How many times the canvas was rebuilt, for the log's "redraws on change" line.
    uint64_t rebuilds() const { return rebuilds_; }

private:
    // Everything the plate draws that is a number, compared whole to decide a rebuild.
    struct Face {
        float width = 0, height = 0;
        int health = -1, maxHealth = 0, mana = 0, maxMana = 0, level = 0;
        int gem = 0;
        float slid = 0;
        bool inventory = false, character = false;
        int hovered = -1;  // which button or slot is lit
        bool tip = false;  // a tip is up, so the pointer's place is part of the picture
        float pointerX = 0, pointerY = 0;
        Quick quick[4];
        bool operator==(const Face& o) const;
    };

    void rebuild();
    void slide(float seconds);
    float progress() const;  // into the level, 0 to 1
    int segment() const;     // which tenth
    int hoveredAt(float x, float y) const;
    bool tipAt(float x, float y) const;

    gfx::Canvas canvas_;
    panel::Arts* arts_ = nullptr;
    const sim::Body* hero_ = nullptr;
    panel::Screen screen_;
    Face drawn_;
    Face now_;
    double clock_ = 0.0;
    float slid_ = 0.0f;
    int drawnLevel_ = 0;
    uint64_t rebuilds_ = 0;
    Quick quick_[4];
};

}  // namespace mu::game
