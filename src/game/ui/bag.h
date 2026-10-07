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
// A jewel let go over a thing it goes on is a refinement and not a move, asked of the same
// `sim::refinable` the realm refuses by -- MuMain's HandlePickedItemPlacement tries ApplyJewels
// before the move.
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
// The art key of a worn slot's silhouette -- `bag_ghost_helm` and its fellows -- as the bag draws
// it in an empty slot. Shared so anything else that names a slot draws the same shape (the
// worn-gear warning, game/ui/endurance.h).
const char* ghostArt(int slot);

struct BagRequests {
    int moveFrom = -1, moveTo = -1;  // a drag let go over a slot
    // A jewel let go over a thing it goes on: asked instead of the move, never as well.
    int refineJewel = -1, refineTarget = -1;
    int use = -1;                   // a right-click on a carried thing
    int repair = -1;                 // a click on one while the counter's repair is on
    bool toggleMending = false;      // the foot's hammer: repair mode on or off
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
    // The repair mode a mending counter turns on (CNewUIMyInventory::SetRepairMode): a click
    // mends the thing under it instead of lifting it, and its card leads with the cost.
    void setMending(bool on) { mending_ = on; }

    // The stage the tooltip's own picture is taken on: one item, at rest, its own size.
    // Shared with the other windows -- only one tip is up at a time.
    void useTipStage(Stage* stage) { tipStage_ = stage; }

    bool covers(float x, float y) const;
    bool dragging() const { return dragging_ >= 0; }
    // The slot being dragged, or -1.
    int dragged() const { return dragging_; }
    // A thing dragged out of the vault and over this window, or null: the bag lights the cells
    // it would land on as it lights its own drag, by Realm::withdraw's gate. Set before update.
    void carrying(const sim::Held* what) { incoming_ = what ? *what : sim::Held{}; }
    // The slot under a point on screen, or -1: where a thing dragged out of the vault lands.
    int slotUnder(float x, float y) const {
        if (!covers(x, y)) return -1;
        return cellAt((x - x_) / panel::scale(), (y - y_) / panel::scale());
    }
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
        int32_t incoming = -1;
        int16_t incomingCount = 0;
        float dragX = 0, dragY = 0;
        int hovered = -1;
        float pointerX = 0, pointerY = 0;
        bool closing = false, overClose = false;
        int level = 0, strength = 0, agility = 0, vitality = 0, energy = 0;
        float x = 0, y = 0, scale = 0;
        uint16_t picture = 0xFFFF;
        bool mending = false;
        bool canMend = false;
        bool overHammer = false, pressingHammer = false;
        bool bare = false;
        bool operator==(const Contents& o) const;
    };
    void rebuild(const sim::Realm& realm, Stage* stage);
    // slotAt, less the helm's slot for a wearer who has none (bare_).
    int cellAt(float ux, float uy) const {
        const int slot = slotAt(ux, uy);
        return bare_ && slot == sim::kHelm ? -1 : slot;
    }
    gfx::Box itemBox(const content::Tables& tables, int slot, const sim::Held& what) const;

    gfx::Canvas canvas_;
    gfx::Canvas tip_;
    Stage* tipStage_ = nullptr;
    panel::Arts* arts_ = nullptr;
    Contents drawn_, now_;
    float x_ = 0.0f, y_ = 0.0f;
    float screenW_ = 0.0f, screenH_ = 0.0f;
    bool up_ = false;
    int dragging_ = -1;
    sim::Held incoming_;
    int hovered_ = -1;
    float pointerX_ = 0.0f, pointerY_ = 0.0f;
    bool closing_ = false;
    bool overClose_ = false;  // the pointer is on the cross, which lights it
    bool mending_ = false;
    // The foot's hammer: under the pointer, and held down on.
    bool overHammer_ = false;
    bool pressingHammer_ = false;
    // **No helm slot for the Magic Gladiator** (the user, 2026-10-07: 'remove helm equpment slot
    // from MG then'): no helm is his (MuMain's item.bmd, RequireClass[3]), so its well is not
    // drawn and takes no click or drop. Ours: MU draws the slot for him empty.
    bool bare_ = false;
    std::vector<Standing> standing_;
    uint64_t rebuilds_ = 0;
};

}  // namespace mu::game
