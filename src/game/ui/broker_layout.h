// The Broker's window's rectangles, in MU's panel units (game/ui/broker.h): what broker.cpp lays
// out for the pointer and broker_draw.cpp draws, one set for both. The ecommerce redesign (the
// user, 2026-10-24, canvas https://claude.ai/artifact/5KWQMUb13wk4QgQ12dYQLL): a head with the
// title, the tabs, the search, Collect and his Zen; Shop as a rail of filters beside a strip of
// jewel prices and a grid of cards; an item's own page; Sell as a list beside one form; My
// listings as three tiles over two lists. The panels' fixed height, and as wide as the room left
// of the bag (geometry's `wide`), so every box here follows that one width.
#pragma once

#include <algorithm>

#include "gfx/interface.h"

namespace mu::game::broker_layout {

using gfx::Box;

constexpr float kTall = 429.0f;  // panel::kHeight: the windows' one height
// How wide it may be: six cards a row stay 80 units wide down to kLeast (tests/market_test.cpp).
constexpr float kLeast = 680.0f, kMost = 900.0f;
constexpr float kPad = 12.0f;
constexpr float kGap = 4.0f;
constexpr float kHead = 34.0f;
constexpr float kBodyY = 42.0f;
constexpr float kBottom = kTall - 10.0f;
// Type, in pixels at 1080 lines (tip::unit()): names and values, the rest, captions.
constexpr float kName = 15.0f, kText = 13.25f, kCaption = 11.5f;
constexpr float kButtonH = 18.0f;   // every secondary button and chip
constexpr float kPrimaryH = 24.0f;  // Buy, List, Collect all

// Shop.
constexpr int kColumns = 6, kCardRows = 2;  // twelve: the server's page, sim::kBrokerPage
constexpr int kCategories = 13;             // All, then sim::BrokerKind's twelve
constexpr int kOptions = 5;                 // Usable by me, Excellent, +7 and up, Luck, Socket
constexpr int kSorts = 5;                   // Newest, Lowest, Highest, Ending soonest, Name
constexpr int kTicker = 5;                  // jewels in the strip
constexpr float kRailW = 120.0f, kRailNarrowW = 104.0f;  // the rail, and under kRailNarrowFrom wide
constexpr float kRailNarrowFrom = 760.0f;
constexpr float kCatTop = 58.0f, kCatStep = 17.0f, kCatH = 15.0f;
constexpr float kOptTop = 297.0f, kOptStep = 19.0f;
constexpr float kPriceTop = 371.0f;
constexpr float kCardGap = 6.0f;
constexpr float kCardPicture = 62.0f;  // the picture's band at a card's top
constexpr float kCardFoot = 22.0f;     // the seller and the Buy button at its foot
// Sell.
constexpr float kThingRowH = 20.0f, kThingStep = 21.0f;
constexpr int kThingRows = 15;
// My listings.
constexpr int kMineRows = 12;
constexpr float kMineRowH = 20.0f;
constexpr float kSoldW = 250.0f;

struct Geometry {
    float wide = 0.0f;
    // The head.
    Box close, zen, collect, search;
    Box tabs[3];
    // Shop.
    Box rail, main, strip, bar, grid, pager, sort;
    Box cats[kCategories], opts[kOptions], minPrice, maxPrice, clear;
    Box sorts[kSorts];
    Box ticker[kTicker];
    Box prev, next;
    Box card(int i) const {
        const float w = (grid.w - kCardGap * float(kColumns - 1)) / float(kColumns);
        const float h = (grid.h - kCardGap * float(kCardRows - 1)) / float(kCardRows);
        return {grid.x + float(i % kColumns) * (w + kCardGap), grid.y + float(i / kColumns) * (h + kCardGap), w, h};
    }
    // A card's foot: its Buy at the right; asked to confirm, the back square at its left and
    // Confirm across the rest of it.
    static Box buy(const Box& card) { return {card.right() - 5.0f - 42.0f, card.bottom() - kCardFoot + 2.0f, 42.0f, kButtonH}; }
    static Box confirmBack(const Box& card) { return {card.x + 6.0f, card.bottom() - kCardFoot + 2.0f, kButtonH, kButtonH}; }
    static Box confirm(const Box& card) {
        const Box back = confirmBack(card);
        return {back.right() + kGap, back.y, card.right() - 6.0f - (back.right() + kGap), kButtonH};
    }
    // An item's page.
    Box back, info, buyBox, offers, history;
    // Sell.
    Box things, form, source[2];
    Box thing(int i) const { return {things.x, things.y + 26.0f + float(i) * kThingStep, things.w, kThingRowH}; }
    Box thingsPrev, thingsNext;
    // My listings.
    Box tiles[3], collectAll, mine, sold, minePrev, mineNext;
    Box mineRow(int r) const { return {mine.x, mine.y + 20.0f + float(r) * kMineRowH, mine.w, kMineRowH}; }
};

// Every box for a window `wide` units across, with a Collect button `collectW` wide in the head
// (0 for none: nothing waits).
inline Geometry geometry(float wide, float collectW) {
    Geometry g;
    wide = std::clamp(wide, kLeast, kMost);
    g.wide = wide;
    // The head: MARKET, the tabs, the search; at the right the close, his Zen, and Collect.
    g.close = {wide - 8.0f - kButtonH, (kHead - kButtonH) * 0.5f, kButtonH, kButtonH};
    g.zen = {g.close.x - 10.0f - 96.0f, 6.0f, 96.0f, 22.0f};
    for (int t = 0; t < 3; ++t) g.tabs[t] = {90.0f + float(t) * 62.0f, kHead - 25.0f, 62.0f, 25.0f};
    const float searchX = g.tabs[2].right() + kPad;
    float rightmost = g.zen.x - 10.0f;
    if (collectW > 0.0f) {
        collectW = std::min(collectW, rightmost - (searchX + 60.0f + 10.0f));
        g.collect = {rightmost - collectW, (kHead - 20.0f) * 0.5f, collectW, 20.0f};
        rightmost = g.collect.x - 10.0f;
    }
    g.search = {searchX, (kHead - kButtonH) * 0.5f, std::clamp(rightmost - searchX, 0.0f, 220.0f), kButtonH};

    // Shop: the rail, then the strip, the bar, the grid and the pager.
    const float railW = wide < kRailNarrowFrom ? kRailNarrowW : kRailW;
    g.rail = {kPad, kBodyY, railW, kBottom - kBodyY};
    g.main = {g.rail.right() + kPad, kBodyY, wide - (g.rail.right() + kPad) - kPad, kBottom - kBodyY};
    for (int i = 0; i < kCategories; ++i) g.cats[i] = {g.rail.x, kCatTop + float(i) * kCatStep, railW, kCatH};
    for (int i = 0; i < kOptions; ++i) {
        g.opts[i] = {g.rail.x + float(i % 2) * (railW * 0.5f + 2.0f), kOptTop + float(i / 2) * kOptStep, railW * 0.5f - 2.0f, 16.0f};
    }
    g.minPrice = {g.rail.x, kPriceTop, railW * 0.5f - 6.0f, kButtonH};
    g.maxPrice = {g.rail.x + railW * 0.5f + 6.0f, kPriceTop, railW * 0.5f - 6.0f, kButtonH};
    g.clear = {g.rail.x, kBottom - 16.0f, railW, 14.0f};
    g.strip = {g.main.x, kBodyY, g.main.w, 28.0f};
    {
        const float label = 60.0f;
        const float w = (g.strip.w - label - kGap * float(kTicker - 1)) / float(kTicker);
        for (int i = 0; i < kTicker; ++i) g.ticker[i] = {g.strip.x + label + float(i) * (w + kGap), g.strip.y, w, g.strip.h};
    }
    g.bar = {g.main.x, g.strip.bottom() + 6.0f, g.main.w, kButtonH};
    g.sort = {g.bar.right() - 118.0f, g.bar.y, 118.0f, kButtonH};
    for (int i = 0; i < kSorts; ++i) g.sorts[i] = {g.sort.x, g.sort.bottom() + 2.0f + float(i) * 16.0f, g.sort.w, 15.0f};
    g.pager = {g.main.x, kBottom - kButtonH, g.main.w, kButtonH};
    g.grid = {g.main.x, g.bar.bottom() + 6.0f, g.main.w, g.pager.y - 6.0f - (g.bar.bottom() + 6.0f)};
    g.next = {g.pager.right() - kButtonH, g.pager.y, kButtonH, kButtonH};
    g.prev = {g.next.x - kGap - kButtonH, g.pager.y, kButtonH, kButtonH};

    // An item's page: back, the thing in full and its buy box, then other offers and its sales.
    g.back = {kPad, kBodyY, 96.0f, kButtonH};
    g.buyBox = {wide - kPad - 180.0f, kBodyY + 24.0f, 180.0f, 236.0f};
    g.info = {kPad, g.buyBox.y, g.buyBox.x - kPad - kPad, g.buyBox.h};
    g.history = {wide - kPad - 230.0f, g.buyBox.bottom() + 8.0f, 230.0f, kBottom - (g.buyBox.bottom() + 8.0f)};
    g.offers = {kPad, g.history.y, g.history.x - kPad - kPad, g.history.h};

    // Sell: what he has beside the one form.
    g.things = {kPad, kBodyY, std::min(300.0f, std::round(wide * 0.36f)), kBottom - kBodyY};
    g.form = {g.things.right() + kPad, kBodyY, wide - (g.things.right() + kPad) - kPad, kBottom - kBodyY};
    g.source[0] = {g.things.right() - 2.0f * 56.0f - kGap, g.things.y, 56.0f, kButtonH};
    g.source[1] = {g.things.right() - 56.0f, g.things.y, 56.0f, kButtonH};
    g.thingsNext = {g.things.right() - kButtonH, kBottom - kButtonH, kButtonH, kButtonH};
    g.thingsPrev = {g.thingsNext.x - kGap - kButtonH, g.thingsNext.y, kButtonH, kButtonH};

    // My listings: three tiles, then his listings and what waits and sold.
    {
        // What waits is the wider: its words and Collect all side by side.
        const float run = wide - 2.0f * kPad - 2.0f * 8.0f;
        const float w0 = std::round(run * 0.4f), w = (run - w0) * 0.5f;
        g.tiles[0] = {kPad, kBodyY, w0, 46.0f};
        g.tiles[1] = {g.tiles[0].right() + 8.0f, kBodyY, w, 46.0f};
        g.tiles[2] = {g.tiles[1].right() + 8.0f, kBodyY, w, 46.0f};
        g.collectAll = {g.tiles[0].right() - 10.0f - 100.0f, g.tiles[0].y + (46.0f - kPrimaryH) * 0.5f, 100.0f, kPrimaryH};
    }
    g.sold = {wide - kPad - kSoldW, g.tiles[0].bottom() + 8.0f, kSoldW, kBottom - (g.tiles[0].bottom() + 8.0f)};
    g.mine = {kPad, g.sold.y, g.sold.x - kPad - kPad, g.sold.h};
    g.mineNext = {g.mine.right() - kButtonH, kBottom - kButtonH - 4.0f, kButtonH, kButtonH};
    g.minePrev = {g.mineNext.x - kGap - kButtonH, g.mineNext.y, kButtonH, kButtonH};
    return g;
}

}  // namespace mu::game::broker_layout
