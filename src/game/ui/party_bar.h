// The party, at the top left (docs/party.md, sprint 2): each other member's name and level, his
// health and his mana, as the server last said them (net::PartyState, a second apart).
//
// MU's CNewUIPartyInfoWindow stood its members down the left in framed plates. Ours is drawn as
// the quest tracker is, with no box: the tracker's soft dark cloud from the screen's edge, a name
// over two thin bars. The leader's name in the tracker's gold; a member on another map dimmed,
// with the map's name where his level was. Not himself: he has his own gems.
#pragma once

#include <string>

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

private:
    gfx::Canvas canvas_;
    bool showing_ = false;
    net::PartyState drawn_;
    std::string me_;
    float unit_ = 0.0f;
};

}  // namespace mu::game
