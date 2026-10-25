// The Broker's window (docs/game/broker.md): the market as a shop (the user, 2026-10-24: "new
// market ... in ecommerce style with our design system"; canvas
// https://claude.ai/artifact/5KWQMUb13wk4QgQ12dYQLL). Shop is a rail of filters, the jewels'
// prices and a grid of cards that open an item's own page; Sell is a list beside one form; My
// listings is what waits, what is on sale and what sold. The panels' height, as wide as the room
// left of the bag (game/ui/broker_layout.h), the bag open beside it.
//
// A mirror as every window is, of the server's book this time rather than the realm: it opens
// while the realm says he is at a Broker (Realm::brokering), asks the server (Play::askBroker)
// and redraws from its answers (net::BrokerView). It decides nothing; the server does.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/ui/broker_layout.h"
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
    // Whether the market fits left of the bag's column on a screen this wide; when not, it stands
    // in the middle and the desk keeps the bag shut under it.
    static bool besideBag(float screenWidth);
    bool covers(float x, float y) const;
    // A box has the keyboard: the search, a price, the rail's price range.
    bool typing() const { return up_ && focus_ != Focus::None; }
    // A bag slot dragged onto the window, or right-clicked in the bag while it is open: chosen
    // to sell, on the Sell tab.
    void offer(int bagSlot);
    // No seller is whispered from the market: he may play on another realm (the user,
    // 2026-10-24: "we cant talk with other realm chars"). Kept for the desk; never true.
    bool takeWhisper(std::string*) { return false; }

    const gfx::Canvas& canvas() const { return canvas_; }
    const gfx::Canvas& tipCanvas() const { return tip_; }

    enum class Tab : uint8_t { Shop, Sell, Mine };
    enum class Focus : uint8_t { None, Search, MinPrice, MaxPrice, Price, Lower };

    // One thing the pointer can press, laid out each frame in the window's units: its kind and
    // which one of that kind. Drawn from the same list, so what is hit is what is drawn.
    enum class Hit : uint8_t {
        None, Close, Tab, Search, Collect,
        // Shop
        Kind, Option, MinPrice, MaxPrice, Clear, Unfilter, SortOpen, Sort, Ticker, Card, CardBuy, CardBack,
        Prev, Next,
        // An item's page
        Back, Path, Qty, MostQty, Buy, BuyBack, More, Offer,
        // Sell
        Source, Thing, ThingPrev, ThingNext, Price, Help, Hours, Pieces, AllPieces, List,
        // My listings
        CollectAll, Listing, Lower, Cancel, LowerField, Save, Keep, MinePrev, MineNext,
    };
    struct Spot {
        gfx::Box box;
        Hit hit = Hit::None;
        int index = 0;
    };

private:
    const gfx::Art* classFaces_[sim::kKinCount * 2] = {};
    uint32_t ask(Play& play, net::BrokerAsk asked);
    uint32_t askQuiet(Play& play, net::BrokerAsk asked);
    void heard(Play& play, const net::BrokerView& view);
    void browse(Play& play);
    void openItem(Play& play, const net::BrokerRow& row);
    void layout(Play& play);
    void press(Play& play, const Spot& spot);
    void type(Play& play, const Keys& keys);
    void rebuild(Play& play, Stage* stage);
    void buy(Play& play, const net::BrokerRow& row, int count);
    // The sellable things in the chosen source, in slot order, and whether each may be listed.
    struct Thing {
        int slot = -1;
        sim::Held what;
        sim::BrokerNo no = sim::BrokerNo::Yes;
    };
    void gather(Play& play);
    const net::BrokerRow* cardRow(int i) const;
    int64_t priceTyped() const;
    // The Sell tab's stack: how many of it go up (1 for what never stacks), and that part of it.
    bool sellStack(const Play& play) const;
    int sellPieces(const Play& play) const;
    sim::Held sellPart(const Play& play) const;
    void requote(const Play& play);
    // The query's filters, each one a chip in the bar that takes it off.
    struct Filter {
        std::string word;
        int which = 0;
    };
    std::vector<Filter> filters() const;

    gfx::Canvas canvas_, tip_;
    Stage* tipStage_ = nullptr;
    bool up_ = false;
    float x_ = 0.0f, y_ = 0.0f, screenW_ = 0.0f, screenH_ = 0.0f;
    broker_layout::Geometry g_;
    std::vector<Spot> spots_;
    int hovered_ = -1, pressed_ = -1;  // in spots_
    double clock_ = 0.0;

    Tab tab_ = Tab::Shop;
    Focus focus_ = Focus::None;
    uint32_t nextAsk_ = 1, waiting_ = 0;  // the ask whose answer moves things, 0 for none
    net::BrokerWhat waitingWhat_ = net::BrokerWhat::Look;
    bool browseOwed_ = false, tickerOwed_ = false;
    float browseIn_ = 0.0f;  // a search typed waits this long before it is asked

    // Shop.
    sim::BrokerQuery query_;
    std::string minText_, maxText_;  // the rail's price range, in millions
    bool sorting_ = false;           // the sort list open under its button
    net::BrokerView page_, mine_, quote_;
    std::vector<net::BrokerRow> ticker_;
    // Each answer is taken only by the ask that is still its place's newest: a late answer to an
    // older ask (an item opened and left within one round trip) is dropped, not laid over the grid.
    uint32_t pageAsk_ = 0, tickerAsk_ = 0, quoteAsk_ = 0;
    uint32_t confirm_ = 0;  // the listing whose Buy asks a second press, on its card or page

    // An item's page: the row as it was opened, others like it, and its last sales.
    bool item_ = false;
    net::BrokerRow open_;
    std::vector<net::BrokerRow> offers_;
    uint32_t offersAsk_ = 0, salesAsk_ = 0;
    // After alike things are bought, their row asked again to refresh the page (itemAsk_); the
    // listing a Buy was asked of, so its answer closes only that one's page.
    uint32_t itemAsk_ = 0, bought_ = 0;
    std::vector<sim::Sale> sales_;
    int qty_ = 1;

    std::string said_;
    bool saidBad_ = false;
    float saidFor_ = 0.0f;

    // Sell.
    bool fromVault_ = false;
    std::vector<Thing> things_;
    int thingsPage_ = 0;
    int sellSlot_ = -1;  // in the chosen source
    sim::Held sellSeen_;
    int sellQty_ = 0;            // of a stack, how many go up
    net::BrokerView quoteEach_;  // the server's quote, a piece's price; quote_ is it for sellQty_
    std::string priceText_;
    int hours_ = 1;  // sim::kBrokerHours[1], 24

    // My listings: 20 an account, kMineRows a page.
    int minePage_ = 0;
    uint32_t editing_ = 0;
    std::string lowerText_;

    // What the stage was last asked to stand: the shop's pictures (1) or one turning square of
    // this side (2 + side), so a picture from the other is never drawn for a frame.
    uint32_t stood_ = 0;
};

}  // namespace mu::game
