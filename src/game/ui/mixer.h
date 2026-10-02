// The Chaos Machine's window: the Goblin's eight by four, what the box makes, and Combine.
//
// MuMain's `CNewUIMixInventory` (NewUIMixInventory.cpp) on the bag's frame and at its scale, in
// the column to its left as the vault is -- MU opens INTERFACE_MIXINVENTORY with the inventory.
// From the top, as its RenderFrame lays it out:
//
//   * the recipe the box is, yellow when it is ready and red when it is not ("Improper items
//     for combination"), the success rate and the Zen asked, in MU's pale blue;
//   * the grid, its CNewUIInventoryCtrl of 8 x 4;
//   * "Assembly prediction:" and the likest recipe's source lines, each coloured as
//     GetSourceName colours it -- red missing, pale blue optional or short, yellow met -- or,
//     with nothing in the box, "Please put the items to combine";
//   * Combine at the foot, and MU's CMixCheckMsgBoxLayout ("Do you want to combine your
//     items?") as a second step on the same foot, Cancel and Combine.
//
// After the mix: the answer in place of the recipe, MU's ChaosCombinationHasSucceeded or
// HasFailed -- which MU prints in the system log this tree does not have -- and the box's
// sparks for MU's fifty frames (RenderMixEffect), over whatever came back. Ours: the sparks are
// drawn in the window's own ink, as MU's BITMAP_SHINY is not in the interface's art.
//
// Phase one of the machine (docs/chaos-machine.md): MU's window as it was. The window of our own
// is the next step.
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

struct MixerRequests {
    int moveFrom = -1, moveTo = -1;  // a drag let go over another cell of the box
    int outside = -1;                // a drag let go outside the window, from this cell
    float outsideX = 0.0f, outsideY = 0.0f;
    int back = -1;                   // a right-click on a thing in the box: back to the bag
    bool mix = false;                // Combine, confirmed
    bool click = false;              // a button pressed that asks nothing of the realm
    bool close = false;
};

class Mixer {
public:
    void open(const gfx::Interface& interface, panel::Arts* arts);

    // `answer` is Play::mixAnswer; the rest is read off the realm and its judge.
    void update(float seconds, float width, float height, int column, const sim::Realm& realm,
                int answer, const Pointer& pointer, Stage* stage, MixerRequests* out);

    void useTipStage(Stage* stage) { tipStage_ = stage; }
    // Starts the sparks: the realm has just answered.
    void spark() { sparks_ = kSparkSeconds; }

    bool covers(float x, float y) const;
    bool dragging() const { return dragging_ >= 0; }
    int dragged() const { return dragging_; }
    // A thing dragged out of the bag and over this window, and whether the box takes it.
    void carrying(const sim::Held* what, bool takes) {
        incoming_ = what ? *what : sim::Held{};
        incomingTakes_ = takes;
    }
    int cellUnder(float x, float y) const;
    const gfx::Canvas& canvas() const { return canvas_; }
    const gfx::Canvas& tipCanvas() const { return tip_; }

private:
    // MU's m_iMixEffectTimer of 50, at its 25 frames a second.
    static constexpr float kSparkSeconds = 2.0f;

    struct Contents {
        uint32_t version = 0, bagVersion = 0;
        long long money = -1;
        int answer = -1;
        int dragging = -1;
        int32_t incoming = -1;
        bool incomingTakes = false;
        float dragX = 0, dragY = 0;
        int hovered = -1;
        float pointerX = 0, pointerY = 0;
        int button = -1, pressing = -1;
        bool confirming = false;
        bool closing = false, overClose = false;
        int spark = -1;
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
    sim::Held incoming_;
    bool incomingTakes_ = false;
    int hovered_ = -1;
    int button_ = -1;    // 0 Combine (or the confirm's Combine), 1 the confirm's Cancel
    int pressing_ = -1;
    bool confirming_ = false;
    bool closing_ = false;
    bool overClose_ = false;
    float sparks_ = 0.0f;
    uint32_t sparkFrame_ = 0;
    int answer_ = -1;
    std::vector<Standing> standing_;
    uint64_t rebuilds_ = 0;
};

}  // namespace mu::game
