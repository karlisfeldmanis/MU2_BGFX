// The vault window: Baz's eight by fifteen, the Zen he keeps, and the two coin buttons.
//
// `CNewUIStorageInventory` on the bag's frame and at its scale, in the column to its left, as the
// shelf is: MU opens the storage beside the inventory (`INTERFACE_STORAGE` with
// `INTERFACE_INVENTORY`). The grid is its `CNewUIInventoryCtrl` of 8 x 15 on the skin's shared
// pitch; the foot is its money strip, `newui_item_money3`, and the first two of its three buttons,
// `newui_Bt_money01` (put Zen in) and `newui_Bt_money02` (take Zen out), in MU's own art.
//
// A coin button opens MU's box to type the sum into (CZenReceiptMsgBoxLayout,
// CZenPaymentMsgBoxLayout; game/ui/amount.h), which the desk runs. The third button, the lock,
// is not drawn: see sim/vault.h.
//
// Nothing here moves an item, as in the bag. A drag ends in a request -- inside the window a
// rearrangement, let go outside it a withdrawal the desk routes to the bag cell under the
// pointer -- and the window redraws off the vault's own version.
#pragma once

#include <cstdint>
#include <vector>

#include "content/tables.h"
#include "game/ui/hud.h"
#include "game/ui/panel.h"
#include "game/ui/stage.h"
#include "gfx/interface.h"
#include "sim/realm.h"

namespace mu::game {

struct ChestRequests {
    int moveFrom = -1, moveTo = -1;  // a drag let go over another cell of the vault
    int outside = -1;                // a drag let go outside the window, from this cell
    float outsideX = 0.0f, outsideY = 0.0f;
    bool depositZen = false, withdrawZen = false;
    bool close = false;
};

class Chest {
public:
    void open(const gfx::Interface& interface, panel::Arts* arts);

    void update(float width, float height, int column, const sim::Realm& realm,
                const Pointer& pointer, Stage* stage, ChestRequests* out);

    void useTipStage(Stage* stage) { tipStage_ = stage; }

    bool covers(float x, float y) const;
    bool dragging() const { return dragging_ >= 0; }
    // The vault cell under a point on screen, or -1: where a thing dragged out of the bag lands.
    int cellUnder(float x, float y) const;
    const gfx::Canvas& canvas() const { return canvas_; }
    const gfx::Canvas& tipCanvas() const { return tip_; }
    uint64_t rebuilds() const { return rebuilds_; }

private:
    struct Contents {
        uint32_t version = 0, bagVersion = 0;
        long long money = -1;
        int dragging = -1;
        float dragX = 0, dragY = 0;
        int hovered = -1;
        float pointerX = 0, pointerY = 0;
        int button = -1, pressing = -1;
        bool closing = false, overClose = false;
        int level = 0, strength = 0, agility = 0, vitality = 0, energy = 0;
        float x = 0, y = 0, scale = 0;
        uint16_t picture = 0xFFFF;
        bool operator==(const Contents& o) const;
    };
    void rebuild(const sim::Realm& realm, Stage* stage);
    int buttonAt(float ux, float uy) const;

    gfx::Canvas canvas_;
    gfx::Canvas tip_;
    Stage* tipStage_ = nullptr;
    panel::Arts* arts_ = nullptr;
    Contents drawn_, now_;
    float x_ = 0.0f, y_ = 0.0f, screenW_ = 0.0f, screenH_ = 0.0f;
    bool up_ = false;
    int dragging_ = -1;
    int hovered_ = -1;
    int button_ = -1;    // the coin button under the pointer, 0 in and 1 out
    int pressing_ = -1;  // the one pressed and not yet let go
    bool closing_ = false;
    bool overClose_ = false;
    std::vector<Standing> standing_;
    uint64_t rebuilds_ = 0;
};

}  // namespace mu::game
