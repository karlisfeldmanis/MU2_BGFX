#include "game/ui/broker.h"
#include "game/ui/broker_logic.h"

#include <algorithm>
#include <cctype>

#include "game/play.h"
#include "game/ui/controls.h"
#include "game/ui/panel.h"
#include "game/ui/tip.h"
#include "sim/broker.h"
#include "sim/vault.h"

namespace mu::game {

namespace {

using gfx::Box;
using namespace broker_layout;

bool numeric(char c) { return c >= '0' && c <= '9'; }

}  // namespace

void Broker::open(const gfx::Interface& interface) {
    interface.adopt(canvas_);
    interface.adopt(tip_);
}

bool Broker::besideBag(float screenWidth) {
    const float k = panel::scale();
    return (panel::columnX(screenWidth, 1) - panel::kColumnGap * k - panel::sideMargin()) / k >= broker_layout::kLeast;
}

bool Broker::covers(float x, float y) const {
    const float k = panel::scale();
    return up_ && Box{x_, y_, g_.wide * k, kTall * k}.has(x, y);
}

void Broker::offer(int bagSlot) {
    if (!up_ || !sim::baggable(bagSlot)) return;
    tab_ = Tab::Sell;
    fromVault_ = false;
    sellSlot_ = bagSlot;
    sellSeen_ = sim::Held{};  // taken from the bag by the next gather
    priceText_.clear();
    quote_ = net::BrokerView{};
}

const net::BrokerRow* Broker::cardRow(int i) const {
    return i >= 0 && size_t(i) < page_.rows.size() ? &page_.rows[size_t(i)] : nullptr;
}

int64_t Broker::priceTyped() const { return market::parsed(priceText_); }

// **Part of a stack goes up** (the user, 2026-10-23: "i cant sell stacked unirias as single
// item"): the Sell tab asks how many, the whole stack at first, and the server lifts only those.
bool Broker::sellStack(const Play& play) const {
    const content::Tables* tables = play.realm().tables();
    if (!tables || sellSeen_.empty() || size_t(sellSeen_.item) >= tables->items.size()) return false;
    const content::ItemRow& row = tables->items[size_t(sellSeen_.item)];
    return sim::stacks(row) && !sim::ammunition(row);
}

int Broker::sellPieces(const Play& play) const {
    if (!sellStack(play)) return 1;
    const int whole = std::max<int>(1, sellSeen_.durability);
    return sellQty_ > 0 ? std::min(sellQty_, whole) : whole;
}

sim::Held Broker::sellPart(const Play& play) const {
    sim::Held part = sellSeen_;
    if (sellStack(play)) part.durability = int16_t(sellPieces(play));
    return part;
}

// The server quotes a piece's price; a part of a stack is priced whole, so its figures are that
// many times it.
void Broker::requote(const Play& play) {
    quote_ = quoteEach_;
    if (quoteEach_.item != sellSeen_.item || !sellStack(play)) return;
    const int64_t pieces = sellPieces(play);
    quote_.cheapest *= pieces;
    for (sim::Sale& sale : quote_.sales) sale.price *= pieces;
}

uint32_t Broker::ask(Play& play, net::BrokerAsk asked) {
    asked.ask = nextAsk_++;
    const bool moves = asked.what == net::BrokerWhat::List || asked.what == net::BrokerWhat::Buy ||
                       asked.what == net::BrokerWhat::Collect || asked.what == net::BrokerWhat::Cancel ||
                       asked.what == net::BrokerWhat::Lower;
    if (moves) waiting_ = asked.ask, waitingWhat_ = asked.what;
    if (asked.what == net::BrokerWhat::Buy) bought_ = asked.listing;
    play.askBroker(asked);
    return asked.ask;
}

// An ask whose answer goes somewhere of its own (the strip, an item's offers and sales): its
// number, for heard to know it by.
uint32_t Broker::askQuiet(Play& play, net::BrokerAsk asked) {
    asked.ask = nextAsk_++;
    play.askBroker(asked);
    return asked.ask;
}

void Broker::browse(Play& play) {
    if (browseOwed_) {
        net::BrokerAsk asked;
        asked.what = net::BrokerWhat::Browse;
        asked.query = query_;
        pageAsk_ = ask(play, asked);
        browseOwed_ = false;
    }
    if (tickerOwed_) {
        // The strip: the cheapest of each jewel, whatever the rail says.
        net::BrokerAsk asked;
        asked.what = net::BrokerWhat::Browse;
        asked.query.kind = int(sim::BrokerKind::Jewels);
        asked.query.sort = sim::BrokerQuery::Sort::Price;
        tickerAsk_ = askQuiet(play, asked);
        tickerOwed_ = false;
    }
}

// An item's own page: the row as it stands, the same thing from other sellers, its last sales.
void Broker::openItem(Play& play, const net::BrokerRow& row) {
    item_ = true;
    open_ = row;
    qty_ = 1;
    confirm_ = 0;
    sorting_ = false;
    offers_.clear();
    sales_.clear();
    const content::Tables* tables = play.realm().tables();
    if (!tables || size_t(row.what.item) >= tables->items.size()) return;
    const content::ItemRow& shape = tables->items[size_t(row.what.item)];
    if (!(row.flags & net::BrokerRow::Alike)) {
        net::BrokerAsk asked;
        asked.what = net::BrokerWhat::Browse;
        asked.query.kind = int(sim::brokerKind(shape));
        asked.query.text = shape.label;
        asked.query.sort = sim::BrokerQuery::Sort::Price;
        offersAsk_ = askQuiet(play, asked);
    }
    net::BrokerAsk quote;
    quote.what = net::BrokerWhat::Quote;
    quote.item = row.what.item;
    salesAsk_ = askQuiet(play, quote);
}

// The server's answer: its parts laid over what the window holds, and the sound of it.
void Broker::heard(Play& play, const net::BrokerView& view) {
    const bool mine = view.answered != 0 && view.answered == waiting_;
    if (mine) waiting_ = 0;
    if (view.parts & net::BrokerView::Page) {
        if (view.answered != 0 && view.answered == tickerAsk_) {
            ticker_ = view.rows;
        } else if (view.answered != 0 && view.answered == itemAsk_) {
            // The alike things just bought, asked again: the page follows their row, or closes
            // when none are left.
            bool found = false;
            for (const net::BrokerRow& r : view.rows) {
                if ((r.flags & net::BrokerRow::Alike) && r.what.item == open_.what.item) open_ = r, found = true;
            }
            if (item_ && (open_.flags & net::BrokerRow::Alike)) {
                if (found) {
                    qty_ = market::clampQty(qty_, open_);
                } else {
                    item_ = false;
                    said_ = "None left on sale.";
                    saidBad_ = false;
                    saidFor_ = 4.0f;
                }
            }
        } else if (view.answered != 0 && view.answered == offersAsk_) {
            offers_.clear();
            for (const net::BrokerRow& r : view.rows) {
                if (r.id != open_.id && r.what.item == open_.what.item && !(r.flags & net::BrokerRow::Alike)) offers_.push_back(r);
            }
        } else if (view.answered == pageAsk_) {
            page_.rows = view.rows;
            page_.total = view.total;
            page_.page = view.page;
            page_.kinds = view.kinds;
            // An alike row's page follows it: its cheapest listing, and so its id, change as it sells.
            if (item_ && (open_.flags & net::BrokerRow::Alike)) {
                for (const net::BrokerRow& r : page_.rows) {
                    if ((r.flags & net::BrokerRow::Alike) && r.what.item == open_.what.item) open_ = r;
                }
                qty_ = market::clampQty(qty_, open_);
            }
        }
    }
    if (view.parts & net::BrokerView::Mine) {
        mine_.mine = view.mine;
        // A listing being lowered that sold or ran out meanwhile: nothing left to lower.
        if (editing_ != 0 && std::none_of(mine_.mine.begin(), mine_.mine.end(), [&](const net::BrokerRow& r) { return r.id == editing_; })) {
            editing_ = 0;
            if (focus_ == Focus::Lower) focus_ = Focus::None;
        }
        mine_.accountCount = view.accountCount;
        mine_.soldLately = view.soldLately;
    }
    if (view.parts & net::BrokerView::Box) mine_.box = view.box;
    if (view.parts & net::BrokerView::Quoted) {
        if (view.answered != 0 && view.answered == salesAsk_) {
            sales_ = view.sales;
        } else if (view.answered == quoteAsk_) {
            quoteEach_ = view;
            requote(play);
            // A price to start from: the cheapest on sale, or what has sold, or four times the NPC's.
            if (priceText_.empty() && sellSeen_.item == view.item && !sellSeen_.empty()) {
                int64_t start = quote_.cheapest;
                if (start <= 0 && !quote_.sales.empty()) start = quote_.sales.front().price;
                if (start <= 0 && play.realm().tables()) {
                    start = 4 * std::max<int64_t>(1, sim::npcValue(*play.realm().tables(), sellPart(play)));
                }
                priceText_ = std::to_string(start);
            }
        }
    }
    if (!view.said.empty()) {
        said_ = view.said;
        saidBad_ = view.no != uint8_t(sim::BrokerNo::Yes);
        saidFor_ = 5.0f;
    }
    const sim::BrokerNo no = sim::BrokerNo(view.no);
    if (view.answered == 0 && !view.said.empty()) {
        // Something of his sold while the window was open: the coin's sound.
        play.sound().play(play.sound().load("money_drop", false));
    } else if (no != sim::BrokerNo::Yes && view.answered != 0) {
        play.ui(Play::Ui::Refused);
        confirm_ = 0;
    } else if (mine) {
        // Zen changes hands on a buy, a listing, a cancel and a collect: the coin's sound alone,
        // no item sound with it (the user, 2026-10-11).
        const net::BrokerWhat did = waitingWhat_;
        const bool coin = did == net::BrokerWhat::Buy || did == net::BrokerWhat::List ||
                          did == net::BrokerWhat::Cancel || did == net::BrokerWhat::Collect;
        if (coin) play.sound().play(play.sound().load("money_drop", false));
        else if (tab_ == Tab::Mine) play.ui(Play::Ui::Click);
        if (did == net::BrokerWhat::Buy) {
            confirm_ = 0;
            browseOwed_ = tickerOwed_ = true;
            browseIn_ = 0.0f;
            // One listing bought is gone from the market: back to the grid, if it is that one's
            // page still. Alike things stay open, their row asked again.
            if (item_ && !(open_.flags & net::BrokerRow::Alike) && open_.id == bought_) item_ = false;
            if (item_ && (open_.flags & net::BrokerRow::Alike) && play.realm().tables() &&
                size_t(open_.what.item) < play.realm().tables()->items.size()) {
                const content::ItemRow& shape = play.realm().tables()->items[size_t(open_.what.item)];
                net::BrokerAsk again;
                again.what = net::BrokerWhat::Browse;
                again.query.kind = int(sim::brokerKind(shape));
                again.query.text = shape.label;
                again.query.sort = sim::BrokerQuery::Sort::Price;
                itemAsk_ = askQuiet(play, again);
            }
            qty_ = 1;
        } else if (did == net::BrokerWhat::List) {
            sellSlot_ = -1;
            sellSeen_ = sim::Held{};
            priceText_.clear();
            browseOwed_ = tickerOwed_ = true;
        }
    }
}

void Broker::gather(Play& play) {
    things_.clear();
    const sim::Realm& realm = play.realm();
    if (!realm.tables()) return;
    const content::Tables& tables = *realm.tables();
    const auto add = [&](int slot, const sim::Held& what) {
        if (what.empty()) return;
        things_.push_back({slot, what, sim::brokerable(tables, what)});
    };
    if (fromVault_) {
        for (int cell = 0; cell < sim::kVaultCells; ++cell) add(cell, realm.vault()[cell]);
    } else {
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) add(slot, realm.satchel()[slot]);
    }
    // What may be listed first, then what may not with its reason.
    std::stable_partition(things_.begin(), things_.end(), [](const Thing& t) { return t.no == sim::BrokerNo::Yes; });
    // The chosen slot as it stands now: gone, moved or changed, it is no longer chosen.
    const auto at = std::find_if(things_.begin(), things_.end(), [&](const Thing& t) { return t.slot == sellSlot_; });
    if (sellSlot_ >= 0 && (at == things_.end() || at->no != sim::BrokerNo::Yes)) {
        sellSlot_ = -1;
        sellSeen_ = sim::Held{};
    } else if (at != things_.end() && sellSeen_.item != at->what.item) {
        sellSeen_ = at->what;
        sellQty_ = 0;  // the whole stack
        priceText_.clear();
        net::BrokerAsk asked;
        asked.what = net::BrokerWhat::Quote;
        asked.item = sellSeen_.item;
        quoteAsk_ = ask(play, asked);
    } else if (at != things_.end()) {
        const bool resized = sellSeen_.durability != at->what.durability;
        sellSeen_ = at->what;
        if (resized) requote(play);
    }
    // Nothing chosen: the first that may be listed, so the form is never empty.
    if (sellSlot_ < 0 && !things_.empty() && things_.front().no == sim::BrokerNo::Yes) {
        sellSlot_ = things_.front().slot;
        sellSeen_ = sim::Held{};
    }
    const int pages = std::max(1, int((things_.size() + kThingRows - 1) / kThingRows));
    thingsPage_ = std::clamp(thingsPage_, 0, pages - 1);
}

std::vector<Broker::Filter> Broker::filters() const {
    std::vector<Filter> out;
    if (!query_.text.empty()) out.push_back({"\xE2\x80\x9C" + query_.text + "\xE2\x80\x9D", 0});
    if (!query_.sellerName.empty()) out.push_back({"Sold by " + query_.sellerName, 1});
    const bool flags[kOptions] = {query_.usableBy >= 0, query_.excellentOnly, query_.sevenUp, query_.luckOnly, query_.socketOnly};
    for (int i = 0; i < kOptions; ++i) {
        if (flags[i]) out.push_back({market::kOptionWords[i], 2 + i});
    }
    if (query_.minPrice > 0 || query_.maxPrice > 0) {
        const auto m = [](int64_t v) {
            std::string s = std::to_string(v / 1000000);
            if (const int64_t part = (v % 1000000) / 100000; part > 0) s += "." + std::to_string(part);
            return s + "M";
        };
        out.push_back({(query_.minPrice > 0 ? m(query_.minPrice) : "0") + " \xE2\x80\x93 " +
                           (query_.maxPrice > 0 ? m(query_.maxPrice) : "any"),
                       7});
    }
    return out;
}

// Every rectangle the pointer can press this frame, in the window's units.
void Broker::layout(Play& play) {
    spots_.clear();
    const auto spot = [&](Box box, Hit hit, int index = 0) { spots_.push_back({box, hit, index}); };
    const float k = panel::scale(), u = tip::unit();
    spot(g_.close, Hit::Close);
    for (int t = 0; t < 3; ++t) spot(g_.tabs[t], Hit::Tab, t);
    if (g_.search.w > 0.0f) spot(g_.search, Hit::Search);
    if (g_.collect.w > 0.0f) spot(g_.collect, Hit::Collect);

    if (tab_ == Tab::Shop && !item_) {
        for (int i = 0; i < kCategories; ++i) spot(g_.cats[i], Hit::Kind, i - 1);
        for (int i = 0; i < kOptions; ++i) spot(g_.opts[i], Hit::Option, i);
        spot(g_.minPrice, Hit::MinPrice);
        spot(g_.maxPrice, Hit::MaxPrice);
        const std::vector<Filter> on = filters();
        if (!on.empty() || query_.kind >= 0) spot(g_.clear, Hit::Clear);
        for (int i = 0; i < kTicker && size_t(i) < ticker_.size(); ++i) spot(g_.ticker[i], Hit::Ticker, i);
        // The bar's chips, after the title and its count, as far as the sort button.
        {
            const float title = controls::labelWidth(16.0f * u, query_.kind >= 0 ? market::kKinds[query_.kind] : "All items") / k;
            const std::string count = std::to_string(page_.total) + " on sale";
            float cx = g_.bar.x + title + 8.0f + controls::labelWidth(kText * u, count) / k + 12.0f;
            for (size_t i = 0; i < on.size(); ++i) {
                const float w = controls::labelWidth(kCaption * u, on[i].word) / k + 22.0f;
                if (cx + w > g_.sort.x - 8.0f) break;
                spot({cx, g_.bar.y + 1.0f, w, kButtonH - 2.0f}, Hit::Unfilter, on[i].which);
                cx += w + kGap;
            }
        }
        spot(g_.sort, Hit::SortOpen);
        for (int i = 0; i < kColumns * kCardRows && size_t(i) < page_.rows.size(); ++i) {
            const Box card = g_.card(i);
            spot(card, Hit::Card, i);
            const net::BrokerRow& row = page_.rows[size_t(i)];
            if (row.flags & net::BrokerRow::Own) continue;
            if (confirm_ != 0 && confirm_ == row.id) {
                spot(Geometry::confirmBack(card), Hit::CardBack, i);
                spot(Geometry::confirm(card), Hit::CardBuy, i);
            } else {
                spot(Geometry::buy(card), Hit::CardBuy, i);
            }
        }
        spot(g_.prev, Hit::Prev);
        spot(g_.next, Hit::Next);
        // The open sort list over the grid, last so it takes the pointer from the cards under it.
        if (sorting_) {
            for (int i = 0; i < kSorts; ++i) spot(g_.sorts[i], Hit::Sort, i);
        }
    } else if (tab_ == Tab::Shop) {
        spot(g_.back, Hit::Back);
        // The category in the breadcrumb, as wide as its word (the item's name after it is not a link).
        if (const content::Tables* tables = play.realm().tables(); tables && size_t(open_.what.item) < tables->items.size()) {
            const int kind = int(sim::brokerKind(tables->items[size_t(open_.what.item)]));
            if (kind >= 0 && kind < int(sim::BrokerKind::Count)) {
                spot({g_.back.right() + 12.0f, g_.back.y, controls::labelWidth(kText * u, market::kKinds[kind]) / k, g_.back.h}, Hit::Path);
            }
        }
        const Box& b = g_.buyBox;
        const float bx = b.x + 12.0f, bw = b.w - 24.0f;
        const bool alike = (open_.flags & net::BrokerRow::Alike) != 0;
        if (alike) {
            spot({bx, b.y + 66.0f, kButtonH, kButtonH}, Hit::Qty, -1);
            spot({bx + kButtonH + 34.0f, b.y + 66.0f, kButtonH, kButtonH}, Hit::Qty, 1);
            const float mx = bx + 2.0f * kButtonH + 34.0f + kGap;
            spot({mx, b.y + 66.0f, bx + bw - mx, kButtonH}, Hit::MostQty);
        } else {
            spot({bx, b.y + 120.0f, bw, 14.0f}, Hit::More);
        }
        if (!(open_.flags & net::BrokerRow::Own)) {
            const bool asking = confirm_ != 0 && confirm_ == open_.id;
            if (asking) {
                spot({bx, b.bottom() - 44.0f, 44.0f, kPrimaryH}, Hit::BuyBack);
                spot({bx + 48.0f, b.bottom() - 44.0f, bw - 48.0f, kPrimaryH}, Hit::Buy);
            } else {
                spot({bx, b.bottom() - 44.0f, bw, kPrimaryH}, Hit::Buy);
            }
        }
        const Box& o = g_.offers;
        const int rows = std::max(0, int((o.h - 26.0f) / 20.0f));
        for (int i = 0; i < rows && size_t(i) < offers_.size(); ++i) {
            if (offers_[size_t(i)].flags & net::BrokerRow::Own) continue;
            spot({o.right() - 10.0f - 44.0f, o.y + 24.0f + float(i) * 20.0f + 1.0f, 44.0f, kButtonH}, Hit::Offer, i);
        }
    } else if (tab_ == Tab::Sell) {
        spot(g_.source[0], Hit::Source, 0);
        spot(g_.source[1], Hit::Source, 1);
        const int first = thingsPage_ * kThingRows;
        for (int i = 0; i < kThingRows && first + i < int(things_.size()); ++i) spot(g_.thing(i), Hit::Thing, first + i);
        if (int(things_.size()) > kThingRows) {
            spot(g_.thingsPrev, Hit::ThingPrev);
            spot(g_.thingsNext, Hit::ThingNext);
        }
        if (sellSlot_ >= 0 && !sellSeen_.empty()) {
            const Box& f = g_.form;
            const float fx = f.x + 14.0f, fw = f.w - 28.0f, half = (fw - 16.0f) * 0.5f, rx = fx + half + 16.0f;
            spot({fx, f.y + 104.0f, half, kPrimaryH}, Hit::Price);
            const float hw = (half - 6.0f) * 0.5f;
            spot({rx, f.y + 104.0f, hw, 30.0f}, Hit::Help, 0);
            spot({rx + hw + 6.0f, f.y + 104.0f, hw, 30.0f}, Hit::Help, 1);
            if (sellStack(play) && sellSeen_.durability > 1) {
                spot({fx, f.y + 178.0f, kButtonH, kButtonH}, Hit::Pieces, -1);
                spot({fx + kButtonH + 34.0f, f.y + 178.0f, kButtonH, kButtonH}, Hit::Pieces, 1);
                spot({fx + 2.0f * kButtonH + 34.0f + 6.0f, f.y + 178.0f, 70.0f, kButtonH}, Hit::AllPieces);
            }
            for (int h = 0; h < 3; ++h) spot({fx + float(h) * 76.0f, f.y + 220.0f, 70.0f, kButtonH}, Hit::Hours, h);
            const Box sum{fx, f.bottom() - 62.0f, fw, 50.0f};
            spot({sum.right() - 10.0f - 150.0f, sum.y + 10.0f, 150.0f, kPrimaryH}, Hit::List);
        }
    } else {
        if (!mine_.box.empty()) spot(g_.collectAll, Hit::CollectAll);
        // A page of his listings, each spot's index the listing's in the whole list (Gordichan on
        // Discord: "if I have 20 items listed, it only shows 12").
        const int pages = market::pagesOf(int64_t(mine_.mine.size()), kMineRows);
        minePage_ = std::clamp(minePage_, 0, pages - 1);
        const int first = minePage_ * kMineRows;
        const Box& m = g_.mine;
        for (int r = 0; r < kMineRows && first + r < int(mine_.mine.size()); ++r) {
            const int i = first + r;
            const Box row = g_.mineRow(r);
            // The listing's name, for its card on hover; clicks nothing.
            spot({row.x, row.y, row.w - 300.0f, row.h}, Hit::Listing, i);
            if (mine_.mine[size_t(i)].id == editing_) {
                spot({m.right() - 210.0f, row.y + 1.0f, 96.0f, kButtonH}, Hit::LowerField, i);
                spot({m.right() - 108.0f, row.y + 1.0f, 46.0f, kButtonH}, Hit::Save, i);
                spot({m.right() - 56.0f, row.y + 1.0f, 46.0f, kButtonH}, Hit::Keep, i);
            } else {
                spot({m.right() - 108.0f, row.y + 1.0f, 46.0f, kButtonH}, Hit::Lower, i);
                spot({m.right() - 56.0f, row.y + 1.0f, 46.0f, kButtonH}, Hit::Cancel, i);
            }
        }
        if (pages > 1) {
            spot(g_.minePrev, Hit::MinePrev);
            spot(g_.mineNext, Hit::MineNext);
        }
    }
}

void Broker::buy(Play& play, const net::BrokerRow& row, int count) {
    const sim::Realm& realm = play.realm();
    if (waiting_ != 0) return;
    const bool alike = (row.flags & net::BrokerRow::Alike) != 0;
    const int n = alike ? market::clampQty(count, row) : 1;
    const int64_t cost = market::costOf(row, n);
    if (cost < 0 || cost > realm.money() + realm.vault().zen()) {
        play.ui(Play::Ui::Refused);
        said_ = "You cannot pay for it, with your vault's Zen too.";
        saidBad_ = true;
        saidFor_ = 5.0f;
        confirm_ = 0;
        return;
    }
    if (cost >= market::kConfirmFrom && confirm_ != row.id) {
        confirm_ = row.id;
        play.ui(Play::Ui::Click);
        return;
    }
    confirm_ = 0;
    ask(play, {.what = net::BrokerWhat::Buy, .listing = row.id, .count = n});
}

void Broker::press(Play& play, const Spot& s) {
    const sim::Realm& realm = play.realm();
    const auto click = [&]() { play.ui(Play::Ui::Click); };
    focus_ = s.hit == Hit::Search     ? Focus::Search
             : s.hit == Hit::MinPrice ? Focus::MinPrice
             : s.hit == Hit::MaxPrice ? Focus::MaxPrice
             : s.hit == Hit::Price    ? Focus::Price
             : s.hit == Hit::LowerField ? Focus::Lower
                                        : Focus::None;
    // A second press is asked only of the Buy it was asked of.
    if (s.hit != Hit::CardBuy && s.hit != Hit::Buy) confirm_ = 0;
    if (s.hit != Hit::SortOpen) sorting_ = false;
    const auto refilter = [&]() {
        query_.page = 0;
        browseOwed_ = true;
        browseIn_ = 0.0f;
        click();
    };
    switch (s.hit) {
        case Hit::Close: play.closeBroker(); click(); break;
        case Hit::Tab:
            tab_ = Tab(s.index);
            editing_ = 0;
            item_ = false;
            click();
            if (tab_ == Tab::Shop) browseOwed_ = tickerOwed_ = true;
            if (tab_ == Tab::Mine) ask(play, {.what = net::BrokerWhat::Look});
            break;
        case Hit::Search:
            tab_ = Tab::Shop;
            item_ = false;
            break;
        case Hit::Collect:
        case Hit::CollectAll:
            if (waiting_ == 0) ask(play, {.what = net::BrokerWhat::Collect});
            break;
        case Hit::Kind: query_.kind = s.index; refilter(); break;
        case Hit::Option:
            if (s.index == 0) query_.usableBy = query_.usableBy >= 0 ? -1 : int(realm.hero().kin);
            if (s.index == 1) query_.excellentOnly = !query_.excellentOnly;
            if (s.index == 2) query_.sevenUp = !query_.sevenUp;
            if (s.index == 3) query_.luckOnly = !query_.luckOnly;
            if (s.index == 4) query_.socketOnly = !query_.socketOnly;
            refilter();
            break;
        case Hit::Clear:
            query_ = sim::BrokerQuery{.sort = query_.sort, .descending = query_.descending};
            minText_.clear();
            maxText_.clear();
            refilter();
            break;
        case Hit::Unfilter:
            if (s.index == 0) query_.text.clear();
            if (s.index == 1) query_.sellerName.clear();
            if (s.index == 2) query_.usableBy = -1;
            if (s.index == 3) query_.excellentOnly = false;
            if (s.index == 4) query_.sevenUp = false;
            if (s.index == 5) query_.luckOnly = false;
            if (s.index == 6) query_.socketOnly = false;
            if (s.index == 7) query_.minPrice = query_.maxPrice = 0, minText_.clear(), maxText_.clear();
            refilter();
            break;
        case Hit::SortOpen: sorting_ = !sorting_; click(); break;
        case Hit::Sort:
            query_.sort = market::kSortsOf[s.index].sort;
            query_.descending = market::kSortsOf[s.index].descending;
            refilter();
            break;
        case Hit::Ticker:
            if (size_t(s.index) < ticker_.size()) openItem(play, ticker_[size_t(s.index)]);
            click();
            break;
        case Hit::Card:
            if (const net::BrokerRow* row = cardRow(s.index)) openItem(play, *row);
            click();
            break;
        case Hit::CardBuy:
            if (const net::BrokerRow* row = cardRow(s.index)) {
                // Alike things are bought by how many: their page asks it.
                if (row->flags & net::BrokerRow::Alike) {
                    openItem(play, *row);
                    click();
                } else {
                    buy(play, *row, 1);
                }
            }
            break;
        case Hit::CardBack:
        case Hit::BuyBack: confirm_ = 0; click(); break;
        case Hit::Prev:
        case Hit::Next: {
            const int pages = market::pagesOf(page_.total, kColumns * kCardRows);
            query_.page = std::clamp(query_.page + (s.hit == Hit::Next ? 1 : -1), 0, pages - 1);
            browseOwed_ = true;
            browseIn_ = 0.0f;
            click();
            break;
        }
        case Hit::Back: item_ = false; click(); break;
        case Hit::Path:
            if (realm.tables() && size_t(open_.what.item) < realm.tables()->items.size()) {
                query_ = sim::BrokerQuery{.sort = query_.sort, .descending = query_.descending};
                query_.kind = std::clamp(int(sim::brokerKind(realm.tables()->items[size_t(open_.what.item)])), -1,
                                         int(sim::BrokerKind::Count) - 1);
                minText_.clear();
                maxText_.clear();
                item_ = false;
                refilter();
            }
            break;
        case Hit::More:
            // Everything he sells, by the name on his listings: a token is never sent.
            query_ = sim::BrokerQuery{.sort = query_.sort, .descending = query_.descending};
            query_.sellerName = open_.seller;
            minText_.clear();
            maxText_.clear();
            item_ = false;
            refilter();
            break;
        case Hit::Qty: qty_ = market::clampQty(qty_ + s.index, open_); click(); break;
        case Hit::MostQty: qty_ = market::mostAffordable(open_, realm.money() + realm.vault().zen()); click(); break;
        case Hit::Buy: buy(play, open_, qty_); break;
        case Hit::Offer:
            if (size_t(s.index) < offers_.size()) {
                // Another seller's: that one's own page, where its Buy asks as every Buy does.
                openItem(play, offers_[size_t(s.index)]);
                click();
            }
            break;
        case Hit::Source:
            fromVault_ = s.index == 1;
            sellSlot_ = -1;
            sellSeen_ = sim::Held{};
            priceText_.clear();
            thingsPage_ = 0;
            click();
            break;
        case Hit::Thing: {
            const Thing& t = things_[size_t(s.index)];
            if (t.no != sim::BrokerNo::Yes) {
                play.ui(Play::Ui::Refused);
                break;
            }
            sellSlot_ = t.slot;
            sellSeen_ = sim::Held{};  // gather asks its quote
            priceText_.clear();
            click();
            break;
        }
        case Hit::ThingPrev: thingsPage_ = std::max(0, thingsPage_ - 1); click(); break;
        case Hit::ThingNext: ++thingsPage_; click(); break;
        case Hit::MinePrev: minePage_ = std::max(0, minePage_ - 1); editing_ = 0; click(); break;
        case Hit::MineNext: ++minePage_; editing_ = 0; click(); break;
        case Hit::Help: {
            // Cheapest on sale, or what it last sold for: nothing to set without the figure.
            const bool quoted = quote_.item == sellSeen_.item;
            const int64_t v = s.index == 0 ? (quoted ? quote_.cheapest : 0) : (quoted && !quote_.sales.empty() ? quote_.sales.front().price : 0);
            if (v <= 0) {
                play.ui(Play::Ui::Refused);
                break;
            }
            priceText_ = std::to_string(v);
            click();
            break;
        }
        case Hit::Hours: hours_ = s.index; click(); break;
        case Hit::Pieces:
        case Hit::AllPieces: {
            const int before = sellPieces(play);
            const int whole = std::max<int>(1, sellSeen_.durability);
            sellQty_ = s.hit == Hit::AllPieces ? whole : std::clamp(before + s.index, 1, whole);
            // A price typed for the old count stays the same each.
            if (const int64_t typed = priceTyped(); typed > 0 && sellQty_ != before) {
                priceText_ = std::to_string(std::max<int64_t>(1, typed * sellQty_ / before));
            }
            requote(play);
            click();
            break;
        }
        case Hit::List:
            if (waiting_ != 0 || sellSeen_.empty()) break;
            // count: how many of a stack, 0 the whole thing (server/src/broker.cpp piecesOf).
            ask(play, {.what = net::BrokerWhat::List, .fromVault = fromVault_, .slot = sellSlot_, .seen = sellSeen_,
                       .price = priceTyped(), .hours = uint8_t(sim::kBrokerHours[hours_]),
                       .count = sellStack(play) ? sellPieces(play) : 0});
            break;
        case Hit::Lower:
            editing_ = mine_.mine[size_t(s.index)].id;
            lowerText_ = std::to_string(mine_.mine[size_t(s.index)].price);
            focus_ = Focus::Lower;
            click();
            break;
        case Hit::Save:
            ask(play, {.what = net::BrokerWhat::Lower, .price = market::parsed(lowerText_), .listing = editing_});
            editing_ = 0;
            break;
        case Hit::Keep: editing_ = 0; click(); break;
        case Hit::Cancel:
            ask(play, {.what = net::BrokerWhat::Cancel, .listing = mine_.mine[size_t(s.index)].id});
            break;
        default: break;
    }
}

void Broker::type(Play& play, const Keys& keys) {
    if (focus_ == Focus::None) return;
    std::string& text = focus_ == Focus::Search     ? query_.text
                        : focus_ == Focus::MinPrice ? minText_
                        : focus_ == Focus::MaxPrice ? maxText_
                        : focus_ == Focus::Price    ? priceText_
                                                    : lowerText_;
    const bool millions = focus_ == Focus::MinPrice || focus_ == Focus::MaxPrice;
    const size_t before = text.size();
    for (const char c : keys.typed) {
        const bool fits = focus_ == Focus::Search ? (unsigned char)c >= 32 && text.size() < 40
                          : millions              ? (numeric(c) || c == '.') && text.size() < 7
                                                  : numeric(c) && text.size() < 10;
        if (fits) text += c;
    }
    for (int i = 0; i < keys.backspaces && !text.empty(); ++i) {
        // A whole UTF-8 letter.
        while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80) text.pop_back();
        if (!text.empty()) text.pop_back();
    }
    if ((focus_ == Focus::Search || millions) && text.size() != before) {
        query_.minPrice = market::parsedMillions(minText_);
        query_.maxPrice = market::parsedMillions(maxText_);
        query_.page = 0;
        browseOwed_ = true;
        browseIn_ = 0.3f;
    }
    if (keys.enter) {
        if (focus_ == Focus::Search || millions) browseIn_ = 0.0f;
        if (focus_ == Focus::Lower && editing_ != 0) {
            ask(play, {.what = net::BrokerWhat::Lower, .price = market::parsed(lowerText_), .listing = editing_});
            editing_ = 0;
        }
        focus_ = Focus::None;
    }
    if (keys.escape) {
        if (focus_ == Focus::Lower) editing_ = 0;
        focus_ = Focus::None;
    }
}

void Broker::update(float seconds, float width, float height, Play& play, const Pointer& pointer,
                    const Keys& keys, Stage* stage) {
    clock_ += seconds;
    const bool was = up_;
    up_ = play.isOpen() && play.realm().brokering() >= 0 && play.realm().tables();
    // The server's answers, taken whether the window is up or not: an answer late to a window
    // closed since is spent here and not left for the next opening.
    for (net::BrokerView view; play.takeBrokerView(view);) heard(play, view);
    if (!up_) {
        focus_ = Focus::None;
        hovered_ = pressed_ = -1;
        if (was) {
            canvas_.clear();
            tip_.clear();
        }
        return;
    }
    if (!was) {
        // Opened: the shop's first page, the jewels' strip and his own market, at once.
        play.ui(Play::Ui::Opened);
        tab_ = Tab::Shop;
        item_ = false;
        editing_ = 0;
        sorting_ = false;
        confirm_ = 0;
        browseOwed_ = tickerOwed_ = true;
        browseIn_ = 0.0f;
        ask(play, {.what = net::BrokerWhat::Look});
    }
    screenW_ = width;
    screenH_ = height;
    const float k = panel::scale(), u = tip::unit();
    // From the left margin to the bag's column, the bag open beside it; where that is too narrow
    // for six cards a row, centred over the screen with the bag shut (besideBag). The panels' top
    // and height either way.
    const bool beside = besideBag(width);
    const float room = beside ? (panel::columnX(width, 1) - panel::kColumnGap * k - panel::sideMargin()) / k
                              : (width - 2.0f * panel::sideMargin()) / k;
    const std::string collect = market::collectWord(mine_.box);
    const float collectW = collect.empty() ? 0.0f : controls::labelWidth(14.0f * u, collect) / k + 34.0f;
    g_ = broker_layout::geometry(room, collectW);
    x_ = beside ? panel::columnX(width, 1) - panel::kColumnGap * k - g_.wide * k : std::max(0.0f, (width - g_.wide * k) * 0.5f);
    y_ = panel::panelY(height);
    saidFor_ = std::max(0.0f, saidFor_ - seconds);
    if (tab_ == Tab::Sell) gather(play);
    type(play, keys);
    if (browseOwed_ || tickerOwed_) {
        browseIn_ -= seconds;
        if (browseIn_ <= 0.0f || !browseOwed_) browse(play);
    }
    layout(play);

    const float ux = (pointer.x - x_) / k, uy = (pointer.y - y_) / k;
    const auto under = [&]() {
        int at = -1;
        if (covers(pointer.x, pointer.y)) {
            for (size_t i = 0; i < spots_.size(); ++i) {
                if (spots_[i].box.has(ux, uy)) at = int(i);
            }
        }
        return at;
    };
    hovered_ = under();
    if (pointer.pressed) {
        pressed_ = hovered_;
        if (hovered_ < 0) focus_ = Focus::None, sorting_ = false;
    }
    if (pointer.released) {
        if (pressed_ >= 0 && pressed_ == hovered_) press(play, spots_[size_t(pressed_)]);
        pressed_ = -1;
        layout(play);
        hovered_ = under();
    }
    rebuild(play, stage);
}

}  // namespace mu::game
