// The Broker's window's rectangles, in MU's panel units (game/ui/broker.h): what broker.cpp lays
// out for the pointer and broker_draw.cpp draws, one set for both.
#pragma once

#include "game/ui/broker.h"
#include "gfx/interface.h"

namespace mu::game::broker_layout {

using gfx::Box;

constexpr float kPad = 12.0f;
constexpr float kBarY = 40.0f, kBarH = 22.0f;
constexpr float kBodyY = 68.0f, kBodyBottom = 396.0f;
constexpr float kRowH = 24.0f;
constexpr int kRows = 12;       // a Buy page, sim::kBrokerPage, and My listings shown
constexpr int kThingRows = 12;  // the Sell tab's bag or vault, a page
// Buy: the rail, the list, the card.
constexpr Box kRail{kPad, kBodyY, 100.0f, kBodyBottom - kBodyY};
constexpr Box kList{118.0f, kBodyY, 326.0f, kBodyBottom - kBodyY};
constexpr Box kCard{452.0f, kBodyY, Broker::kWide - 452.0f - kPad, kBodyBottom - kBodyY};
// Sell: the things, the thing, the price.
constexpr Box kThings{kPad, kBodyY, 168.0f, kBodyBottom - kBodyY};
constexpr Box kThing{188.0f, kBodyY, 254.0f, kBodyBottom - kBodyY};
constexpr Box kPrice{450.0f, kBodyY, Broker::kWide - 450.0f - kPad, kBodyBottom - kBodyY};
// My listings: the Collect and the listings.
constexpr Box kBox{kPad, kBodyY, 190.0f, kBodyBottom - kBodyY};
constexpr Box kMine{210.0f, kBodyY, Broker::kWide - 210.0f - kPad, kBodyBottom - kBodyY};

}  // namespace mu::game::broker_layout
