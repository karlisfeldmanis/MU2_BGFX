// The Broker's window (docs/game/broker.md): the market, Buy, Sell and My listings, three of the
// panels' columns wide (618 x 429 units) at the vault's place, the bag in its own column beside
// it. Designed on the canvas https://claude.ai/artifact/1f7nUNsQRELX6BMbnf84Bk and picked by the
// user on 2026-10-11; redrawn as the canvas's variant B on 2026-10-12 (game/ui/broker_layout.h).
//
// A mirror as every window is, of the server's book this time rather than the realm: it opens
// while the realm says he is at a Broker (Realm::brokering), asks the server (Play::askBroker)
// and redraws from its answers (net::BrokerView). It decides nothing; the server does.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/ui/hud.h"
#include "game/ui/stage.h"
#include "gfx/interface.h"
#include "net/broker_wire.h"
#include "sim/rules.h"

namespace mu::game {

class Play;

class Broker {
public:
    // What the keyboard did this frame, as the desk reads it off the window.
    struct Keys {
        std::string typed;
        int backspaces = 0;
        bool enter = false;
        bool escape = false;
    };

    void open(const gfx::Interface& interface);
    void useTipStage(Stage* stage) { tipStage_ = stage; }
    // The classes' faces for the card's class line: kin * 2 + second, null until taken (the desk's
    // classFaces_). Set before update.
    void classFaces(const gfx::Art* const faces[sim::kKinCount * 2]) {
        for (int i = 0; i < sim::kKinCount * 2; ++i) classFaces_[i] = faces[i];
    }

    void update(float seconds, float width, float height, Play& play, const Pointer& pointer,
                const Keys& keys, Stage* stage);

    bool up() const { return up_; }
    bool covers(float x, float y) const;
    // A box has the keyboard: the search or a price.
    bool typing() const { return up_ && focus_ != Focus::None; }
    // A bag slot dragged onto the window, or right-clicked in the bag while it is open: chosen
    // to sell, on the Sell tab.
    void offer(int bagSlot);
    // A seller to whisper, asked from the item card: the desk opens the chat on him.
    bool takeWhisper(std::string* name);

    const gfx::Canvas& canvas() const { return canvas_; }
    const gfx::Canvas& tipCanvas() const { return tip_; }

    // The window in MU's panel units: three columns and the two gaps between them.
    static constexpr float kWide = 618.0f;
    static constexpr float kTall = 429.0f;

    enum class Tab : uint8_t { Buy, Sell, Mine };
    enum class Focus : uint8_t { None, Search, Price, Lower };

    // One thing the pointer can press, laid out each frame in the window's units: its kind and
    // which one of that kind. Drawn from the same list, so what is hit is what is drawn.
    enum class Hit : uint8_t {
        None, Close, Tab, Kind, Flag, Clear, Head, Row, Prev, Next, Search, Qty, MostQty, Buy,
        Whisper, Seller, Source, Thing, ThingPrev, ThingNext, Price, Quick, Hours, List,
        Collect, Lower, Cancel, LowerField, Save, Back, Listing, Category,
    };
    struct Spot {
        gfx::Box box;
        Hit hit = Hit::None;
        int index = 0;
    };

private:
    const gfx::Art* classFaces_[sim::kKinCount * 2] = {};
    void ask(Play& play, net::BrokerAsk asked);
    void heard(Play& play, const net::BrokerView& view);
    void browse(Play& play);
    void layout(Play& play);
    void press(Play& play, const Spot& spot);
    void type(Play& play, const Keys& keys);
    void rebuild(Play& play, Stage* stage);
    // The sellable things in the chosen source, in slot order, and whether each may be listed.
    struct Thing {
        int slot = -1;
        sim::Held what;
        sim::BrokerNo no = sim::BrokerNo::Yes;
    };
    void gather(Play& play);
    const net::BrokerRow* chosenRow() const;
    int64_t priceTyped() const;

    gfx::Canvas canvas_, tip_;
    Stage* tipStage_ = nullptr;
    bool up_ = false;
    float x_ = 0.0f, y_ = 0.0f, screenW_ = 0.0f, screenH_ = 0.0f;
    std::vector<Spot> spots_;
    int hovered_ = -1, pressed_ = -1;  // in spots_
    double clock_ = 0.0;

    Tab tab_ = Tab::Buy;
    Focus focus_ = Focus::None;
    uint32_t nextAsk_ = 1, waiting_ = 0;  // the ask whose answer moves things, 0 for none
    net::BrokerWhat waitingWhat_ = net::BrokerWhat::Look;
    bool browseOwed_ = false;
    float browseIn_ = 0.0f;  // a search typed waits this long before it is asked

    sim::BrokerQuery query_;
    bool categories_ = false;  // the Category list open under its button
    net::BrokerView page_, mine_, quote_;
    uint32_t chosen_ = 0;  // the listing chosen on Buy
    int qty_ = 1;
    bool confirm_ = false;
    std::string said_;
    bool saidBad_ = false;
    float saidFor_ = 0.0f;
    std::string whisper_;

    bool fromVault_ = false;
    std::vector<Thing> things_;
    int thingsPage_ = 0;
    int sellSlot_ = -1;  // in the chosen source
    sim::Held sellSeen_;
    std::string priceText_;
    int hours_ = 1;  // sim::kBrokerHours[1], 24
    uint32_t editing_ = 0;
    std::string lowerText_;

    // What was drawn, and the signature that says whether to draw again.
    uint64_t drawn_ = 0;
};

}  // namespace mu::game
