// The Chaos Machine's window: every service it offers in a list, the Goblin's eight by four, and
// what the picked service needs, risks and costs.
//
// MuMain's `CNewUIMixInventory` (NewUIMixInventory.cpp) opened with the inventory, as MU opens
// INTERFACE_MIXINVENTORY. Phase two (docs/game/chaos-machine.md, 2026-10-02) put the services on
// one row stepped with chevrons; since 2026-10-20 the window is the Broker's width, three columns
// at the vault's place, so every service is on it at once (the user: "craft options all visible
// with buttons and user press it"; canvas B, "keep finetuning UI based on our design system"):
//
//   * the **list** on the left, the services under MIXES, TICKETS and RUNES, a bordered iron row
//     each, the picked one in the travel list's gold; at its right "Ready" in the bag's green
//     when the bag and the box hold all it needs (sim::plan), else how many needs are short;
//   * on the right the service's name and what the box is to it, the **Box** (the grid, drag or
//     right-click either way), **Needs** with in-box / wanted counts and how many more the bag
//     holds, **Socket** rows for Remove Rune in their runes' rarities, and **Chance**: the
//     character card's meter, the share luck gives drawn faint where the thing is not lucky, and
//     success and failure in words;
//   * the **foot**: the coin and the cost, red when he is short, then Take out, **Fill from bag**
//     (what sim::plan picks, put in as many drags would) and the service's button, and MU's
//     CMixCheckMsgBoxLayout as a second step on the same foot.
//
// After a run the answer stands where the chance was, gold or red, with two seconds of sparks
// over the box (RenderMixEffect); the button becomes Take out.
#pragma once

#include <cstdint>
#include <string>
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
    bool takeAll = false;            // Take out: everything in the box back to the bag
    bool fill = false;               // Fill from bag: Realm::planned's slots into the box
    bool mix = false;                // the service, confirmed
    bool turned = false;             // another service picked: the page's sound
    bool click = false;              // a button pressed that asks nothing of the realm
    bool close = false;
};

class Mixer {
public:
    // The window's width in units, the Broker's: three columns and the two gaps between them.
    static constexpr float kWide = panel::kWidth * 3.0f + panel::kColumnGap * 2.0f;  // 618

    void open(const gfx::Interface& interface, panel::Arts* arts);

    // `answer` and `words` are Play::mixAnswer and mixWords. `column` is the rightmost of the
    // three it covers, as panel::columnX counts.
    void update(float seconds, float width, float height, int column, const sim::Realm& realm,
                int answer, const std::string& words, const Pointer& pointer, Stage* stage,
                MixerRequests* out);

    void useTipStage(Stage* stage) { tipStage_ = stage; }
    // Starts the sparks: the realm has just answered.
    void spark() { sparks_ = kSparkSeconds; }

    // What a run asks of the realm: the picked service and Remove Rune's socket.
    // `service_` is a place in the list (sim::kRowServices), not the enum.
    sim::Service service() const { return sim::kRowServices[service_]; }
    int socket() const { return socket_; }

    bool covers(float x, float y) const;
    bool dragging() const { return dragging_ >= 0; }
    // A frame the window is not updated on ends any drag and press (Bag::cancel).
    void cancel() { dragging_ = lifting_ = -1; pressing_ = kNone; closing_ = false; }
    int dragged() const { return dragging_; }
    // Back on the pointer: a click outside that nothing took (the bag's hold).
    void hold(int cell) { dragging_ = cell; }
    // A thing dragged out of the bag and over this window, and whether the box takes it.
    void carrying(const sim::Held* what, bool takes) {
        incoming_ = what ? *what : sim::Held{};
        incomingTakes_ = takes;
    }
    // A thing `width` by `height`: its top-left, half its footprint back of the pointer
    // (panel::landing at the box's own pitch).
    int cellUnder(float x, float y, int width = 1, int height = 1) const;
    const gfx::Canvas& canvas() const { return canvas_; }
    const gfx::Canvas& tipCanvas() const { return tip_; }

private:
    // MU's m_iMixEffectTimer of 50, at its 25 frames a second.
    static constexpr float kSparkSeconds = 2.0f;
    // The buttons and rows the pointer can be on.
    enum Hit : int {
        kNone = -1,
        kRun = 0,      // the foot's button: the service, Take out, or the confirm's yes
        kCancel = 1,   // the confirm's no
        kTake = 2,     // the foot's Take out
        kFill = 3,     // the foot's Fill from bag
        kSocket0 = 4,  // Remove Rune's socket rows, 4 to 6
        kRow0 = 8,     // the list's rows, one a service on it
    };

    struct Contents {
        uint32_t version = 0, bagVersion = 0;
        long long money = -1;
        int answer = -1;
        int service = 0, socket = -1;
        int dragging = -1;
        int32_t incoming = -1;
        bool incomingTakes = false;
        float dragX = 0, dragY = 0;
        int hovered = -1;
        float pointerX = 0, pointerY = 0;
        int over = -1, pressing = -1;
        bool confirming = false;
        bool closing = false, overClose = false;
        int spark = -1;
        float x = 0, y = 0, scale = 0;
        uint16_t picture = 0xFFFF;
        bool operator==(const Contents& o) const;
    };
    // What the bag makes of each service on the list, and of the picked one for Fill: worked out
    // again only when the box, the bag, the pick or the socket changes.
    void plans(const sim::Realm& realm);
    void rebuild(const sim::Realm& realm, Stage* stage);
    int hitAt(float ux, float uy, const sim::Realm& realm) const;

    gfx::Canvas canvas_;
    gfx::Canvas tip_;
    Stage* tipStage_ = nullptr;
    panel::Arts* arts_ = nullptr;
    Contents drawn_, now_;
    float x_ = 0.0f, y_ = 0.0f, screenW_ = 0.0f, screenH_ = 0.0f;
    bool up_ = false;
    int dragging_ = -1;
    int lifting_ = -1;  // the thing pressed on, picked when the button comes up on it
    sim::Held incoming_;
    bool incomingTakes_ = false;
    int hovered_ = -1;
    int over_ = kNone;
    int pressing_ = kNone;
    bool confirming_ = false;
    bool closing_ = false;
    bool overClose_ = false;
    int service_ = 0;
    int socket_ = -1;
    // The list's place of the service last run: its answer is shown on its own page only.
    int ranService_ = -1;
    long long ranZen_ = 0;  // and what it cost, which the foot says while the answer stands
    float sparks_ = 0.0f;
    uint32_t sparkFrame_ = 0;
    int answer_ = -1;
    std::string words_;
    std::vector<Standing> standing_;
    uint64_t rebuilds_ = 0;
    // The plans and what they were worked out for.
    sim::Planned rowPlans_[sim::kRowCount];
    sim::Planned fillPlan_, readyPlan_;
    uint32_t plannedBox_ = ~0u, plannedBag_ = ~0u;
    int plannedService_ = -1, plannedSocket_ = -2;
};

}  // namespace mu::game
