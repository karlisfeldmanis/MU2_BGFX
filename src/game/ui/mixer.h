// The Chaos Machine's window: the Goblin's eight by four, the service it is read as, and what that
// service needs, risks and costs.
//
// MuMain's `CNewUIMixInventory` (NewUIMixInventory.cpp) on the bag's frame and at its scale, in the
// column to its left as the vault is -- MU opens INTERFACE_MIXINVENTORY with the inventory. Phase
// two (docs/chaos-machine.md, the user's "proposed version is perfect", 2026-10-02) keeps that
// window and its box and lays the rest out in the parts the other windows already draw:
//
//   * a **service row** under the title, Options' setting row with its two chevrons, the service
//     in gold: Combine, Remove Rune, Add Socket, Fuse Runes. A step turns the
//     page as the quest journal turns one -- the old page fades and slides out, the new one in --
//     with the journal's own page sound;
//   * **Box**, the grid, drag or right-click either way;
//   * **Recipe** (or **Sockets** for Remove Rune, a row a socket in its rune's rarity, the picked
//     one in the travel list's gold), **Needs** with in-box / wanted counts in MuMain's
//     GetSourceName colours, and **Chance**: the character card's meter, the share luck gives
//     drawn faint where the thing is not lucky, and success and failure in words;
//   * the **foot**: the vault's coin and the cost at the left, red when he is short, the button
//     at the right, and MU's CMixCheckMsgBoxLayout as a second step on the same foot.
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
    bool mix = false;                // the service, confirmed
    bool turned = false;             // the service row stepped: the page's sound
    bool click = false;              // a button pressed that asks nothing of the realm
    bool close = false;
};

class Mixer {
public:
    void open(const gfx::Interface& interface, panel::Arts* arts);

    // `answer` and `words` are Play::mixAnswer and mixWords.
    void update(float seconds, float width, float height, int column, const sim::Realm& realm,
                int answer, const std::string& words, const Pointer& pointer, Stage* stage,
                MixerRequests* out);

    void useTipStage(Stage* stage) { tipStage_ = stage; }
    // Starts the sparks: the realm has just answered.
    void spark() { sparks_ = kSparkSeconds; }

    // What a run asks of the realm: the service on the row and Remove Rune's socket.
    // `service_` is a place on the row (sim::kRowServices), not the enum.
    sim::Service service() const { return sim::kRowServices[service_]; }
    int socket() const { return socket_; }

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
    // The buttons and rows the pointer can be on.
    enum Hit : int {
        kNone = -1,
        kRun = 0,      // the foot's button: the service, Take out, or the confirm's yes
        kCancel = 1,   // the confirm's no
        kPrev = 2,     // the service row's chevrons
        kNext = 3,
        kSocket0 = 4,  // Remove Rune's socket rows, 4 to 6
    };

    struct Contents {
        uint32_t version = 0, bagVersion = 0;
        long long money = -1;
        int answer = -1;
        int service = 0, socket = -1, turn = 0;
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
    void rebuild(const sim::Realm& realm, Stage* stage);
    int hitAt(float ux, float uy, const sim::Realm& realm) const;
    // The page turn, as the quest journal's: -1 to 0 the old page going out, 0 to 1 the new
    // one coming in, 1 at rest.
    float turnAlpha() const;
    float turnShift() const;  // in window units

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
    int over_ = kNone;
    int pressing_ = kNone;
    bool confirming_ = false;
    bool closing_ = false;
    bool overClose_ = false;
    int service_ = 0;
    int pending_ = -1;   // the service the page is turning to
    int socket_ = -1;
    float turn_ = 1.0f;
    int turnDir_ = 1;
    float sparks_ = 0.0f;
    uint32_t sparkFrame_ = 0;
    int answer_ = -1;
    std::string words_;
    std::vector<Standing> standing_;
    uint64_t rebuilds_ = 0;
};

}  // namespace mu::game
