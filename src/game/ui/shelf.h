// A merchant's shelf: what it sells, what it charges, and the click that buys.
//
// MU2's `client/core/Shelf.cs`, which is `CNewUINPCShop` on the bag's frame and at its scale,
// in the column to its left -- `SetPos(PanelColumnX(2), 0)` while the inventory stays where it
// is. The grid is `CNewUIInventoryCtrl`, the bag's own class, from (15, 50), eight wide and
// fifteen tall.
//
// Nothing here moves an item, as in the bag. A click raises a purchase and the shelf is
// unchanged -- a merchant's stock is endless in MU -- and the bag redraws off its own version.
// Selling is a drag: a bag item let go over this window. The desk routes that; the realm
// refuses a worn slot again, because a window is not the gate.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/tables.h"
#include "game/ui/hud.h"
#include "game/ui/panel.h"
#include "game/ui/stage.h"
#include "gfx/interface.h"
#include "sim/market.h"
#include "sim/realm.h"

namespace mu::game {

class Shelf {
public:
    void open(const gfx::Interface& interface, panel::Arts* arts);

    // `buy` comes back as the shelf slot a click was released on, or -1; `close` as the X.
    void update(float width, float height, int column, const sim::Realm& realm,
                const Pointer& pointer, Stage* stage, int* buy, bool* close);

    bool covers(float x, float y) const;
    const gfx::Canvas& canvas() const { return canvas_; }
    // The tooltip, on a canvas of its own so the desk can lay it over every window: a tip is
    // drawn at the pointer and runs past its own window's edge, and in the window's canvas the
    // one beside it covered it -- the shelf's tip went under the bag. MU2's tooltip layer.
    const gfx::Canvas& tipCanvas() const { return tip_; }
    uint64_t rebuilds() const { return rebuilds_; }

private:
    struct Line {
        sim::Offer offer;
        int32_t item = -1;
        int64_t price = 0;
    };
    void restock(const sim::Realm& realm, int folk);
    int lineAt(const content::Tables& tables, float ux, float uy) const;
    void rebuild(const sim::Realm& realm, Stage* stage);

    gfx::Canvas canvas_;
    gfx::Canvas tip_;
    panel::Arts* arts_ = nullptr;
    std::vector<Line> lines_;
    std::vector<Standing> standing_;
    std::string merchant_;
    int keeper_ = -1;
    float x_ = 0.0f, y_ = 0.0f, screenW_ = 0.0f, screenH_ = 0.0f;
    bool up_ = false;
    int hovered_ = -1;
    bool closing_ = false;
    bool pressing_ = false;
    // What the last rebuild drew for, compared whole.
    struct Drawn {
        int keeper = -2, hovered = -2;
        float pointerX = 0, pointerY = 0, x = 0, y = 0, scale = 0;
        bool closing = false;
        int level = 0, strength = 0, agility = 0, vitality = 0, energy = 0;
        uint32_t version = 0;
        uint16_t picture = 0xFFFF;
        bool operator==(const Drawn& o) const;
    };
    Drawn drawn_, now_;
    uint64_t rebuilds_ = 0;
};

}  // namespace mu::game
