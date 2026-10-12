// The Broker's window's rectangles, in MU's panel units (game/ui/broker.h): what broker.cpp lays
// out for the pointer and broker_draw.cpp draws, one set for both. Variant B of the canvas's
// redesign (the user, 2026-10-12): no title (the tabs sit in the head band), the categories in a
// bar over a wider one-line list, and the body ending on the bag's foot rule with a foot under it.
#pragma once

#include "game/ui/broker.h"
#include "game/ui/panel.h"
#include "gfx/interface.h"

namespace mu::game::broker_layout {

using gfx::Box;

constexpr float kPad = 12.0f;
// The head: the tabs and the close button, the rule under them (the user: "we dont need MARKET
// header it take to much space").
constexpr float kHead = 32.0f;
constexpr float kTabY = kHead - 25.0f, kTabH = 25.0f;  // the tabs' floor on the head's rule, one line
constexpr float kBodyY = 40.0f;
// The body stops short of the bag's foot rule, and the foot under it holds the pager, the line
// the Broker said and his Zen, level with the bag's.
constexpr float kBodyBottom = panel::kFootRule - 8.0f;
constexpr float kFootBaseline = 404.5f;
constexpr float kFootButtonY = 393.5f;

constexpr int kRows = 12;       // a Buy page, sim::kBrokerPage, and My listings shown
constexpr int kThingRows = 12;  // the Sell tab's bag or vault, a page
// Buy: the filter bar across the window, the list and the card under it.
constexpr float kBarH = 20.0f;
constexpr float kRowH = 24.0f;
constexpr float kListHead = 18.0f;
// Detail A, "Even" (the user, 2026-10-12): one 4-unit grid, three sizes of type, and every
// column started from the right so a row's seller, time, price and comparison line up with
// their heads whatever the row is.
constexpr float kRowPad = 8.0f;
constexpr float kButtonH = 18.0f;  // every secondary button: Whisper, presets, Lower, the pager
constexpr float kPrimaryH = 24.0f;  // Buy, List, Collect all
constexpr float kGap = 4.0f;
constexpr float kVsRight = 8.0f, kPriceRight = 58.0f, kLeftRight = 110.0f, kSellerFromRight = 223.0f;
// Type, in pixels at 1080 lines (tip::unit()): names and values, the rest, captions.
constexpr float kName = 15.0f, kText = 13.25f, kCaption = 11.5f;
constexpr Box kList{kPad, kBodyY + kBarH + 6.0f, 406.0f, kBodyBottom - (kBodyY + kBarH + 6.0f)};
constexpr Box kCard{424.0f, kList.y, Broker::kWide - 424.0f - kPad, kList.h};
constexpr float kSearchW = 120.0f, kCategoryW = 118.0f;
// Sell: the things, the thing, the price.
constexpr Box kThings{kPad, kBodyY, 168.0f, kBodyBottom - kBodyY};
constexpr Box kThing{188.0f, kBodyY, 254.0f, kBodyBottom - kBodyY};
constexpr Box kPrice{450.0f, kBodyY, Broker::kWide - 450.0f - kPad, kBodyBottom - kBodyY};
// My listings: what waits as a strip across the top, then the listings and what sold.
constexpr Box kStrip{kPad, kBodyY, Broker::kWide - 2.0f * kPad, 40.0f};
constexpr float kMineRowH = 22.0f;
constexpr Box kMine{kPad, kBodyY + 46.0f, 394.0f, kBodyBottom - (kBodyY + 46.0f)};
constexpr Box kSold{412.0f, kBodyY + 46.0f, Broker::kWide - 412.0f - kPad, kBodyBottom - (kBodyY + 46.0f)};

}  // namespace mu::game::broker_layout
