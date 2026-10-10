// The party, down the left side at middle height (docs/party.md, sprint 2): each other member's
// name and level, his health and his mana, as the server last said them (net::PartyState, a
// second apart). In the Golden Dragon's fight, the rest of the raid under them, a line each.
//
// MU's CNewUIPartyInfoWindow stood its members down the left in framed plates. Ours is a soft
// dark plate a member, no stroke: a name over two thin bars. The leader's name in the tracker's
// gold; a member on another map dimmed, with the map's name where his level was. Not himself:
// he has his own gems.
//
// Moved off the top left on 2026-10-10 (the user: "party memeber blocks is blocking experience
// gains and top left corner"): the exp lane keeps that corner. The raid's rows from a proposal
// the user chose the same day ("R2 ... but with some minimal container for each player").
#pragma once

#include <string>
#include <vector>

#include "gfx/interface.h"
#include "net/wire.h"

namespace mu::game {

class Play;

class PartyBar {
public:
    void open(const gfx::Interface& interface);
    void update(const Play& play);
    bool showing() const { return showing_; }
    const gfx::Canvas& canvas() const { return canvas_; }

    // One of the rest of the raid: not in his party, fighting the dragon with him.
    struct Raider {
        std::string name;
        int health = 0;  // percent
        bool operator==(const Raider& o) const { return name == o.name && health == o.health; }
    };

private:
    gfx::Canvas canvas_;
    bool showing_ = false;
    net::PartyState drawn_;
    std::vector<Raider> raid_;
    std::string me_;
    float unit_ = 0.0f;
};

}  // namespace mu::game
