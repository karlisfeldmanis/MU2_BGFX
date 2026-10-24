// The Broker's window without a window (game/ui/broker_logic.h, game/ui/broker_layout.h): its sums
// -- what a count costs, how many his Zen pays for, what a typed price says, the pages, Collect's
// word -- and its rectangles at every width it may be, none outside the window or over another.
// The server's half (the filters, the wire) is sim_test's (tests/sim_test_broker.cpp).
#include <cstdio>
#include <string>
#include <vector>

#include "game/ui/broker_layout.h"
#include "game/ui/broker_logic.h"

using namespace mu;
using namespace mu::game;
using gfx::Box;

namespace {

int g_checks = 0, g_failures = 0;

void check(bool held, const std::string& what) {
    ++g_checks;
    if (held) return;
    ++g_failures;
    std::printf("FAIL: %s\n", what.c_str());
}

void checkEqual(long long got, long long wanted, const std::string& what) {
    ++g_checks;
    if (got == wanted) return;
    ++g_failures;
    std::printf("FAIL: %s (got %lld, wanted %lld)\n", what.c_str(), got, wanted);
}

bool inside(const Box& inner, const Box& outer) {
    constexpr float e = 0.01f;
    return inner.x >= outer.x - e && inner.y >= outer.y - e && inner.right() <= outer.right() + e &&
           inner.bottom() <= outer.bottom() + e;
}

bool overlap(const Box& a, const Box& b) {
    constexpr float e = 0.01f;
    return a.x < b.right() - e && b.x < a.right() - e && a.y < b.bottom() - e && b.y < a.bottom() - e;
}

bool real(const Box& b) { return b.w > 0.0f && b.h > 0.0f; }

net::BrokerRow alikeRow(std::vector<net::BrokerRow::Tier> tiers, int count) {
    net::BrokerRow row;
    row.flags = net::BrokerRow::Alike;
    row.tiers = std::move(tiers);
    row.count = count;
    row.price = row.tiers.empty() ? 0 : row.tiers.front().price;
    return row;
}

void testSums() {
    // Typing.
    checkEqual(market::parsed("1,850,000"), 1850000, "a price with its commas");
    checkEqual(market::parsed("abc"), 0, "no digits, no price");
    check(market::parsed("99999999999999") <= sim::kBrokerMostPrice * 10 + 9, "a price typed long stops past the most");
    checkEqual(market::parsedMillions("1.5"), 1500000, "1.5 millions");
    checkEqual(market::parsedMillions("12"), 12000000, "12 millions");
    checkEqual(market::parsedMillions("0.25"), 250000, "a quarter of a million");
    checkEqual(market::parsedMillions(""), 0, "nothing typed is no bound");
    checkEqual(market::parsedMillions("."), 0, "a lone point is no bound");
    checkEqual(market::parsedMillions("2,5"), 2500000, "a comma reads as the point");
    checkEqual(market::parsedMillions("1.2.3"), 1200000, "a second point ends it");
    check(market::commas(0) == "0" && market::commas(999) == "999" && market::commas(1000) == "1,000" &&
              market::commas(48500000) == "48,500,000" && market::commas(-1234) == "-1,234",
          "commas every three");

    // One listing.
    net::BrokerRow one;
    one.price = 48500000;
    checkEqual(market::costOf(one, 1), 48500000, "one listing costs its price");
    checkEqual(market::costOf(one, 2), -1, "and two of it are not on sale");
    checkEqual(market::costOf(one, 0), 0, "none costs nothing");
    checkEqual(market::mostAffordable(one, 0), 1, "a single listing's count is always one");
    checkEqual(market::clampQty(9, one), 1, "and a single listing's box holds one");

    // Alike things down their tiers.
    const net::BrokerRow bless = alikeRow({{1850000, 3, "Kesh"}, {1870000, 6, "Morrow"}, {1900000, 2, "Odo"}}, 11);
    checkEqual(market::costOf(bless, 1), 1850000, "the first from the cheapest");
    checkEqual(market::costOf(bless, 3), 3 * 1850000LL, "three all from the cheapest tier");
    checkEqual(market::costOf(bless, 5), 3 * 1850000LL + 2 * 1870000LL, "five run into the second tier");
    checkEqual(market::costOf(bless, 11), 3 * 1850000LL + 6 * 1870000LL + 2 * 1900000LL, "all eleven");
    checkEqual(market::costOf(bless, 12), -1, "a twelfth is not on sale");
    checkEqual(market::mostAffordable(bless, 3 * 1850000LL + 1869999), 3, "his Zen buys three, a Zen short of a fourth");
    checkEqual(market::mostAffordable(bless, 3 * 1850000LL + 1870000), 4, "and the fourth when it is there");
    checkEqual(market::mostAffordable(bless, 0), 1, "no Zen still reads one, never zero");
    checkEqual(market::mostAffordable(bless, 1LL << 50), 11, "a fortune buys what is on sale and no more");
    checkEqual(market::clampQty(0, bless), 1, "the box never goes under one");
    checkEqual(market::clampQty(40, bless), 11, "nor over what is on sale");
    // The server sends eight tiers at most: what is past them is not priced here.
    const net::BrokerRow cut = alikeRow({{100, 2, "a"}, {110, 1, "b"}}, 40);
    checkEqual(market::onOffer(cut), 3, "forty on sale, three in the tiers sent: three may be bought at once");
    checkEqual(market::clampQty(10, cut), 3, "so the box stops at three");
    checkEqual(market::costOf(cut, market::clampQty(10, cut)), 310, "and its cost is known");
    const net::BrokerRow empty = alikeRow({}, 0);
    checkEqual(market::onOffer(empty), 1, "an empty row still reads one");
    checkEqual(market::costOf(empty, 1), -1, "and costs what is not there");

    // Pages.
    checkEqual(market::pagesOf(0, 12), 1, "no listings are one page");
    checkEqual(market::pagesOf(12, 12), 1, "twelve are one");
    checkEqual(market::pagesOf(13, 12), 2, "thirteen are two");
    checkEqual(market::pagesOf(20, 12), 2, "his twenty listings are two pages");

    // The price against the market.
    using Says = market::Verdict::Says;
    check(market::verdict(0, 100, 0, 0).says == Says::None && !market::verdict(0, 100, 0, 0).listable, "no price, no listing");
    check(market::verdict(99, 100, 0, 0).says == Says::Low && !market::verdict(99, 100, 0, 0).listable, "under the floor: refused");
    check(market::verdict(100, 100, 0, 0).says == Says::Only && market::verdict(100, 100, 0, 0).listable, "on the floor with none on sale: the only one");
    check(market::verdict(sim::kBrokerMostPrice + 1, 100, 0, 0).says == Says::High, "over the most: refused");
    check(market::verdict(sim::kBrokerMostPrice, 100, 0, 0).listable, "the most itself may be listed");
    check(market::verdict(5000, 100, 5000, 3).says == Says::Cheapest, "the cheapest's own price: shown with it, first");
    check(market::verdict(4000, 100, 5000, 3).says == Says::Cheapest, "under it: first");
    check(market::verdict(5001, 100, 5000, 3).says == Says::Behind && market::verdict(5001, 100, 5000, 3).listable,
          "over it: behind, and still listable");
    check(market::verdict(5001, 100, 0, 3).says == Says::Only, "no cheapest known: the only one");

    // Collect's word in the head.
    std::vector<net::BrokerParcel> box;
    check(market::collectWord(box).empty(), "nothing waits: no Collect");
    box.push_back({sim::Held{}, 41500000, 3600, 0});
    box.push_back({sim::Held{}, 28600000, 3600, 0});
    check(market::collectWord(box) == "Collect 70,100,000", "two sales: their Zen together");
    sim::Held armour;
    armour.item = 5;
    box.push_back({armour, 0, 3600, uint8_t(sim::Parcel::Why::Unsold)});
    check(market::collectWord(box) == "Collect 70,100,000 + 1 item", "and a thing come back");
    box.erase(box.begin(), box.begin() + 2);
    box.push_back({armour, 0, 3600, uint8_t(sim::Parcel::Why::Cancelled)});
    check(market::collectWord(box) == "Collect 2 items", "things alone");

    // Lines.
    check(market::againstLast(48500000, 51600000) == "6% under the last sale", "under the last sale");
    check(market::againstLast(62000000, 60000000) == "3% over the last sale", "over it");
    check(market::againstLast(7300000, 7300000) == "Same as the last sale", "the same");
    check(market::againstLast(100, 0).empty(), "never sold: nothing to say");
    check(market::hoursLeft(23 * 3600 + 59) == "23 h" && market::hoursLeft(35 * 60) == "35 min" && market::hoursLeft(5) == "1 min",
          "time left in hours, then minutes");

    // The sort list says what it asks, and asks back what it says.
    for (int i = 0; i < int(std::size(market::kSortsOf)); ++i) {
        sim::BrokerQuery q;
        q.sort = market::kSortsOf[i].sort;
        q.descending = market::kSortsOf[i].descending;
        checkEqual(market::sortIndex(q), i, std::string("the sort list round-trips ") + market::kSortsOf[i].word);
    }
    checkEqual(int(std::size(market::kKinds)), int(sim::BrokerKind::Count), "a word for every category");
    checkEqual(int(std::size(market::kOptionWords)), broker_layout::kOptions, "a word for every option");
    checkEqual(int(std::size(market::kSortsOf)), broker_layout::kSorts, "a word for every sort");
}

void testLayout() {
    using namespace broker_layout;
    checkEqual(kColumns * kCardRows, sim::kBrokerPage, "the grid holds the server's page");
    const float widths[] = {kLeast - 100.0f, kLeast, 690.0f, 700.0f, 741.0f, 780.0f, 827.0f, 857.0f, 900.0f, kMost + 200.0f};
    const float collects[] = {0.0f, 90.0f, 160.0f, 400.0f};
    int bad = 0;
    for (const float asked : widths) {
        for (const float cw : collects) {
            const Geometry g = geometry(asked, cw);
            const Box window{0.0f, 0.0f, g.wide, kTall};
            const std::string at = " (wide " + std::to_string(int(asked)) + ", collect " + std::to_string(int(cw)) + ")";
            const auto must = [&](bool held, const std::string& what) {
                if (!held) ++bad;
                check(held, what + at);
            };
            must(g.wide >= kLeast && g.wide <= kMost, "the width held to its bounds");
            // The head.
            std::vector<Box> head{g.close, g.zen, g.search, g.tabs[0], g.tabs[1], g.tabs[2]};
            if (cw > 0.0f) head.push_back(g.collect);
            for (size_t i = 0; i < head.size(); ++i) {
                must(real(head[i]) && inside(head[i], {0.0f, 0.0f, g.wide, kHead}), "a head box " + std::to_string(i) + " in the head");
                for (size_t j = i + 1; j < head.size(); ++j) must(!overlap(head[i], head[j]), "head boxes apart");
            }
            must(g.search.w >= 60.0f, "the search keeps room to type in");
            must(cw == 0.0f || real(g.collect), "a Collect when something waits");
            // Shop: the rail.
            std::vector<Box> rail;
            for (const Box& b : g.cats) rail.push_back(b);
            for (const Box& b : g.opts) rail.push_back(b);
            rail.push_back(g.minPrice);
            rail.push_back(g.maxPrice);
            rail.push_back(g.clear);
            for (size_t i = 0; i < rail.size(); ++i) {
                must(real(rail[i]) && inside(rail[i], g.rail), "a rail box " + std::to_string(i) + " in the rail");
                for (size_t j = i + 1; j < rail.size(); ++j) must(!overlap(rail[i], rail[j]), "rail boxes apart");
            }
            must(g.cats[kCategories - 1].bottom() + 14.0f <= kOptTop, "the categories end before Options' heading");
            must(g.opts[kOptions - 1].bottom() + 14.0f <= kPriceTop, "the options end before Price's heading");
            must(g.maxPrice.bottom() <= g.clear.y, "the price boxes end before Clear");
            // Shop: the main column.
            must(!overlap(g.rail, g.main) && inside(g.main, window) && inside(g.rail, window), "the rail beside the main column");
            const Box stack[] = {g.strip, g.bar, g.grid, g.pager};
            for (int i = 0; i < 4; ++i) {
                must(real(stack[i]) && inside(stack[i], g.main), "the main column's band " + std::to_string(i) + " inside it");
                if (i > 0) must(stack[i - 1].bottom() <= stack[i].y, "its bands one under another");
            }
            for (int i = 0; i < kTicker; ++i) {
                must(real(g.ticker[i]) && inside(g.ticker[i], g.strip), "a jewel tile in the strip");
                must(g.ticker[i].w >= 70.0f, "a jewel tile wide enough for its price");
                if (i > 0) must(!overlap(g.ticker[i - 1], g.ticker[i]), "jewel tiles apart");
            }
            must(inside(g.sort, g.bar) && g.sort.x > g.bar.x + 150.0f, "the sort at the bar's right, its title room kept");
            for (int i = 0; i < kSorts; ++i) must(inside(g.sorts[i], window), "the sort list inside the window");
            for (int i = 0; i < kColumns * kCardRows; ++i) {
                const Box c = g.card(i);
                must(real(c) && inside(c, g.grid), "card " + std::to_string(i) + " in the grid");
                must(c.w >= 80.0f && c.h >= kCardPicture + kCardFoot + 50.0f, "a card big enough for its lines");
                const Box b = Geometry::buy(c), back = Geometry::confirmBack(c), yes = Geometry::confirm(c);
                must(inside(b, c) && b.y >= c.bottom() - kCardFoot, "a card's Buy in its foot");
                must(b.x - c.x >= 36.0f, "the seller's name keeps room beside Buy");
                must(inside(back, c) && inside(yes, c) && !overlap(back, yes) && back.y >= c.bottom() - kCardFoot,
                     "asked to confirm: the back square and Confirm in its foot, apart");
                must(yes.w >= 50.0f, "Confirm wide enough for its word");
                for (int j = i + 1; j < kColumns * kCardRows; ++j) must(!overlap(c, g.card(j)), "cards apart");
            }
            must(inside(g.prev, g.pager) && inside(g.next, g.pager) && !overlap(g.prev, g.next), "the pager's squares");
            // An item's page.
            const Box item[] = {g.info, g.buyBox, g.offers, g.history};
            must(real(g.back) && inside(g.back, window) && g.back.bottom() <= g.info.y, "back over the page");
            for (int i = 0; i < 4; ++i) {
                must(real(item[i]) && inside(item[i], window) && item[i].bottom() <= kBottom + 0.01f, "the page's box " + std::to_string(i));
                for (int j = i + 1; j < 4; ++j) must(!overlap(item[i], item[j]), "the page's boxes apart");
            }
            must(g.info.w >= 150.0f + 8.0f + 160.0f, "the thing's lines beside its 150-unit picture");
            must(g.buyBox.h >= 230.0f, "the buy box tall enough for the ladder and Buy");
            must(g.offers.w >= 330.0f, "the offers wide enough for a name, seller, time, price and View");
            // Sell.
            must(!overlap(g.things, g.form) && inside(g.things, window) && inside(g.form, window), "the list beside the form");
            must(g.form.w >= 340.0f && g.form.h >= 300.0f, "the form fits its price, help, how many, hours and List");
            for (int s = 0; s < 2; ++s) must(inside(g.source[s], g.things), "Bag and Vault in the list's head");
            must(!overlap(g.source[0], g.source[1]), "Bag and Vault apart");
            must(g.thing(kThingRows - 1).bottom() <= g.thingsPrev.y, "the last row over the list's pager");
            must(inside(g.thing(kThingRows - 1), g.things) && g.thing(0).y >= g.source[0].bottom(), "the rows in the list, under its head");
            // My listings.
            for (int t = 0; t < 3; ++t) {
                must(real(g.tiles[t]) && inside(g.tiles[t], window), "a tile in the window");
                if (t > 0) must(!overlap(g.tiles[t - 1], g.tiles[t]), "tiles apart");
            }
            must(inside(g.collectAll, g.tiles[0]) && g.collectAll.x - g.tiles[0].x >= 120.0f, "Collect all in its tile, words beside it");
            must(!overlap(g.mine, g.sold) && inside(g.mine, window) && inside(g.sold, window), "his listings beside what sold");
            must(g.mine.w >= 360.0f, "a listing's row keeps room for its name beside Ends, price, Lower and Cancel");
            must(g.mineRow(kMineRows - 1).bottom() <= g.minePrev.y, "the last listing over the pager");
            must(inside(g.minePrev, g.mine) && inside(g.mineNext, g.mine) && !overlap(g.minePrev, g.mineNext), "the listings' pager");
        }
    }
    checkEqual(bad, 0, "every box at every width");
}

}  // namespace

int main() {
    testSums();
    testLayout();
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
