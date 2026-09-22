// The inventory window: what he is wearing, what he is carrying, and the dragging.
//
// MU2's `client/core/Bag.cs`, which is `CNewUIMyInventory` at MU's own layout: every worn
// slot's rectangle from `SetEquipmentSlotInfo` (closed up onto a shared-border grid, as MU2
// did, so no two gold rims cross), the grid from `Create(x + 15, y + 200, 8, 8)` at MU's
// twenty-unit cell, and the Zen strip at the foot.
//
// Nothing here moves an item. A drag ends in a request, and the window goes on drawing what it
// drew until the satchel it mirrors changes -- the realm's own `version`. The drop-target
// colour is asked of the same gate the realm refuses by (`sim::movable`), so a red cell and a
// refusal cannot disagree.
//
// Not here, and why: MU2's repair button (nothing in this sim wears out, so it would be a
// button that does nothing), and applying a jewel (refining is later).
#pragma once

#include <cstdint>
#include <vector>

#include "content/tables.h"
#include "game/ui/hud.h"
#include "game/ui/panel.h"
#include "game/ui/stage.h"
#include "gfx/interface.h"
#include "sim/items.h"
#include "sim/realm.h"

namespace mu::game {

// What a frame of the bag asks the realm for. All requests; the desk answers them.
struct BagRequests {
    int moveFrom = -1, moveTo = -1;  // a drag let go over a slot
    int use = -1;                    // a right-click on a carried thing
    int outside = -1;                // a drag let go outside the window: the desk decides where
    float outsideX = 0.0f, outsideY = 0.0f;
    bool close = false;
};

class Bag {
public:
    void open(const gfx::Interface& interface, panel::Arts* arts);

    // `column` is 1 normally and 2 when the character window is up, which is MU's
    // arrangement: the character window keeps the right-hand column and the bag moves left.
    void update(float width, float height, int column, const sim::Realm& realm,
                const Pointer& pointer, Stage* stage, BagRequests* out);

    bool covers(float x, float y) const;
    bool dragging() const { return dragging_ >= 0; }
    // The slot whose thing is under the pointer, or -1: what a quick key binds.
    int hovered() const { return up_ ? hovered_ : -1; }
    const gfx::Canvas& canvas() const { return canvas_; }
    // The tooltip, on a canvas of its own so the desk can lay it over every window: a tip is
    // drawn at the pointer and runs past its own window's edge, and in the window's canvas the
    // one beside it covered it -- the shelf's tip went under the bag. MU2's tooltip layer.
    const gfx::Canvas& tipCanvas() const { return tip_; }
    uint64_t rebuilds() const { return rebuilds_; }

    // Where a slot sits in the window, in MU's units: the worn slots at their own sizes, a bag
    // cell at twenty. Public because the stage and the tests ask it too.
    static gfx::Box slotBox(int slot);
    // Which slot a point in the window's MU units falls in, or -1.
    static int slotAt(float ux, float uy);

private:
    struct Contents {
        uint32_t version = 0;
        long long money = -1;
        int dragging = -1;
        float dragX = 0, dragY = 0;
        int hovered = -1;
        float pointerX = 0, pointerY = 0;
        bool closing = false;
        int level = 0, strength = 0, agility = 0, vitality = 0, energy = 0;
        float x = 0, y = 0, scale = 0;
        uint16_t picture = 0xFFFF;
        bool operator==(const Contents& o) const;
    };
    void rebuild(const sim::Realm& realm, Stage* stage);
    gfx::Box itemBox(const content::Tables& tables, int slot, const sim::Held& what) const;

    gfx::Canvas canvas_;
    gfx::Canvas tip_;
    panel::Arts* arts_ = nullptr;
    Contents drawn_, now_;
    float x_ = 0.0f, y_ = 0.0f;
    float screenW_ = 0.0f, screenH_ = 0.0f;
    bool up_ = false;
    int dragging_ = -1;
    int hovered_ = -1;
    float pointerX_ = 0.0f, pointerY_ = 0.0f;
    bool closing_ = false;
    std::vector<Standing> standing_;
    uint64_t rebuilds_ = 0;
};

}  // namespace mu::game
