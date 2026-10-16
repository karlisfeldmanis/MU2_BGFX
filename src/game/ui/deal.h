// The trade window: a trade between two players (sim/realm_deal.cpp), MU's CNewUITrade.
//
// The user picked "A · MuMain, tidied" on 2026-10-10 from three
// (claude.ai/artifact/FVFs4zXhXtKXMweGzoNrA5). MU's own column -- the other player's eight by four
// over his Zen, and his own under it -- with what MuMain left to a blinking warning said in plain
// words instead:
//   * **One Accept each**, which any change to either offer drops; Accept is greyed with its
//     seconds while the realm holds it (kDealHoldTicks), as MuMain greys its OK for 150 frames.
//   * **New and Changed** on the other player's things: against his offer as it stood when this
//     player last pressed Accept, a thing that was not there is New, and one that came back
//     different -- a +9 swapped for a +7 -- is Changed; what went is named under his Zen. MuMain
//     backs up only jewels, wings and +5 and over and flags a worse copy with an arrow
//     (AlertYourTradeInven).
//   * **The plus on the cell** in the trade alone, gold in the corner, so a +7 for a +9 shows.
//   * **Whether it fits**, the realm's own test (Realm::dealMisfit): Accept stays off while what he
//     would get has no room in his bag.
// His Zen is the coin's: the box to type the sum in (game/ui/amount.h, Purpose::Offer), which
// replaces the last; nought takes it back.
//
// Nothing here moves a thing, as in the vault: a click on his own thing asks for it back, a bag
// thing let go over the window asks to put it up (the desk routes it), and the window redraws off
// the realm's dealVersion.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "content/tables.h"
#include "game/ui/hud.h"
#include "game/ui/panel.h"
#include "game/ui/stage.h"
#include "gfx/interface.h"
#include "sim/realm.h"

namespace mu::game {

struct DealRequests {
    int take = -1;       // his own box cell clicked: back to the bag
    int outside = -1;    // his own box cell dragged out and let go outside the window
    float outsideX = 0.0f, outsideY = 0.0f;
    bool zen = false;    // the coin: the box to type his sum in
    int accept = -1;     // 1 Accept, 0 take it back
    bool close = false;  // Cancel or the head's close
};

class Deal {
public:
    void open(const gfx::Interface& interface, panel::Arts* arts);

    // A frame. `name` is the other player's, as the server said it.
    void update(float seconds, float width, float height, int column, const sim::Realm& realm,
                const std::string& name, const Pointer& pointer, Stage* stage, DealRequests* out);
    void useTipStage(Stage* stage) { tipStage_ = stage; }

    bool covers(float x, float y) const;
    bool dragging() const { return dragging_ >= 0; }
    // A thing dragged out of the bag and over this window, or null: the cell it would land on in
    // his own box is lit by the realm's room. Set before update.
    void carrying(const sim::Held* what) { incoming_ = what ? *what : sim::Held{}; }
    // His own box cell under a point on screen, or -1: where a bag thing let go lands.
    // A thing `width` by `height`: its top-left, half its footprint back of the pointer
    // (panel::landing).
    int cellUnder(float x, float y, int width = 1, int height = 1) const;
    const gfx::Canvas& canvas() const { return canvas_; }
    const gfx::Canvas& tipCanvas() const { return tip_; }

private:
    void rebuild(const sim::Realm& realm, Stage* stage);
    // What the other side's offer is against what he saw when he last accepted.
    struct Marks {
        std::vector<int> fresh, changed;  // their box cells
        std::vector<int32_t> wasRow;      // for each changed, the row it was
        std::vector<int> wasPlus;
        std::vector<sim::Held> gone;      // things no longer there
        bool zenMoved = false;
        int64_t zenWas = 0;
    };
    Marks marks(const sim::Realm& realm) const;

    gfx::Canvas canvas_;
    gfx::Canvas tip_;
    Stage* tipStage_ = nullptr;
    panel::Arts* arts_ = nullptr;
    float x_ = 0.0f, y_ = 0.0f, screenW_ = 0.0f, screenH_ = 0.0f;
    bool up_ = false;
    std::string name_;
    // Which box a cell under the pointer is in: 0 his own, 1 the other's.
    int hoveredBox_ = -1, hovered_ = -1;
    int dragging_ = -1;
    sim::Held incoming_;
    float pointerX_ = 0.0f, pointerY_ = 0.0f;
    int button_ = -1, pressing_ = -1;  // 0 the coin, 1 Accept, 2 Cancel, 3 the close
    // The other's offer as it stood the last time his own Accept went on.
    std::vector<std::pair<int, sim::Held>> seen_;
    int64_t seenZen_ = 0;
    bool seenAny_ = false;
    bool acceptedBefore_ = false;
    uint32_t partner_ = 0;
    std::vector<Standing> standing_;
    uint64_t drawnKey_ = 0;
    bool drawnOnce_ = false;
};

}  // namespace mu::game
