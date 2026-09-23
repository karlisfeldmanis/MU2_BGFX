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

#include "game/ui/hud.h"
#include "game/ui/panel.h"
#include "gfx/interface.h"
#include "sim/realm.h"

namespace mu::game {

class Card {
public:
    void open(const gfx::Interface& interface, panel::Arts* arts);

    // A frame. `spend` comes back as the stat a plus was released on (0 strength to 3 energy),
    // or -1; `close` as whether the X was. Both are requests; the caller answers them.
    void update(float width, float height, const sim::Body* hero, const Pointer& pointer,
                int* spend, bool* close);

    bool covers(float x, float y) const;
    const gfx::Canvas& canvas() const { return canvas_; }
    uint64_t rebuilds() const { return rebuilds_; }

private:
    struct Sheet {
        const sim::Body* who = nullptr;
        float width = 0, height = 0;
        int level = 0, points = 0;
        unsigned long long experience = 0;
        int strength = 0, agility = 0, vitality = 0, energy = 0;
        int minimum = 0, maximum = 0, attackRate = 0, defense = 0, defenseRate = 0;
        int health = 0, maxHealth = 0, mana = 0, maxMana = 0;
        int pushed = -1, over = -1;
        bool closing = false, overClose = false;
        bool operator==(const Sheet& o) const;
    };
    void rebuild();

    gfx::Canvas canvas_;
    panel::Arts* arts_ = nullptr;
    Sheet drawn_, now_;
    float x_ = 0.0f, y_ = 0.0f;
    int pushed_ = -1;
    int over_ = -1;      // the diamond under the pointer
    bool closing_ = false;
    bool overClose_ = false;
    uint64_t rebuilds_ = 0;
};

}  // namespace mu::game
