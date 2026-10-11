#include "game/ui/broker.h"
#include "game/ui/broker_layout.h"

#include <algorithm>
#include <cctype>

#include "game/play.h"
#include "game/ui/panel.h"
#include "sim/broker.h"
#include "sim/vault.h"

namespace mu::game {

namespace {

using gfx::Box;
using namespace broker_layout;

constexpr int64_t kConfirmFrom = 1000000;  // a buy this dear asks a second press

bool numeric(char c) { return c >= '0' && c <= '9'; }

int64_t parsed(const std::string& text) {
    int64_t v = 0;
    for (const char c : text) {
        if (!numeric(c)) continue;
        if (v > sim::kBrokerMostPrice) break;
        v = v * 10 + (c - '0');
    }
    return v;
}

}  // namespace

void Broker::open(const gfx::Interface& interface) {
    interface.adopt(canvas_);
    interface.adopt(tip_);
}

bool Broker::covers(float x, float y) const {
    const float k = panel::scale();
    return up_ && Box{x_, y_, kWide * k, kTall * k}.has(x, y);
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

bool Broker::takeWhisper(std::string* name) {
    if (whisper_.empty()) return false;
    *name = whisper_;
    whisper_.clear();
    return true;
}

const net::BrokerRow* Broker::chosenRow() const {
    for (const net::BrokerRow& row : page_.rows) {
        if (row.id == chosen_) return &row;
    }
    return nullptr;
}

int64_t Broker::priceTyped() const { return parsed(priceText_); }

void Broker::ask(Play& play, net::BrokerAsk asked) {
    asked.ask = nextAsk_++;
    const bool moves = asked.what == net::BrokerWhat::List || asked.what == net::BrokerWhat::Buy ||
                       asked.what == net::BrokerWhat::Collect || asked.what == net::BrokerWhat::Cancel ||
                       asked.what == net::BrokerWhat::Lower;
    if (moves) waiting_ = asked.ask, waitingWhat_ = asked.what;
    play.askBroker(asked);
}

void Broker::browse(Play& play) {
    net::BrokerAsk asked;
    asked.what = net::BrokerWhat::Browse;
    asked.query = query_;
    ask(play, asked);
    browseOwed_ = false;
}

// The server's answer: its parts laid over what the window holds, and the sound of it.
void Broker::heard(Play& play, const net::BrokerView& view) {
    const bool mine = view.answered != 0 && view.answered == waiting_;
    if (mine) waiting_ = 0;
    if (view.parts & net::BrokerView::Page) {
        page_.rows = view.rows;
        page_.total = view.total;
        page_.page = view.page;
        page_.kinds = view.kinds;
        if (!chosenRow()) chosen_ = page_.rows.empty() ? 0 : chosen_;
    }
    if (view.parts & net::BrokerView::Mine) {
        mine_.mine = view.mine;
        mine_.accountCount = view.accountCount;
        mine_.soldLately = view.soldLately;
    }
    if (view.parts & net::BrokerView::Box) mine_.box = view.box;
    if (view.parts & net::BrokerView::Quoted) {
        quote_ = view;
        // A price to start from: the cheapest on sale, or what has sold, or four times the NPC's.
        if (priceText_.empty() && sellSeen_.item == view.item && !sellSeen_.empty()) {
            int64_t start = view.cheapest;
            if (start <= 0 && !view.sales.empty()) start = view.sales.front().price;
            if (start <= 0 && play.realm().tables()) {
                start = 4 * std::max<int64_t>(1, sim::npcValue(*play.realm().tables(), sellSeen_));
            }
            priceText_ = std::to_string(start);
        }
    }
    if (!view.said.empty()) {
        said_ = view.said;
        saidBad_ = view.no != uint8_t(sim::BrokerNo::Yes);
        saidFor_ = 6.0f;
    }
    const sim::BrokerNo no = sim::BrokerNo(view.no);
    if (view.answered == 0 && !view.said.empty()) {
        // Something of his sold while the window was open: the coin's sound.
        play.sound().play(play.sound().load("money_drop", false));
    } else if (no != sim::BrokerNo::Yes && view.answered != 0) {
        play.ui(Play::Ui::Refused);
    } else if (mine) {
        // Zen changes hands on a buy, a listing, a cancel and a collect: the coin's sound alone,
        // no item sound with it (the user, 2026-10-11).
        const net::BrokerWhat did = waitingWhat_;
        const bool coin = did == net::BrokerWhat::Buy || did == net::BrokerWhat::List ||
                          did == net::BrokerWhat::Cancel || did == net::BrokerWhat::Collect;
        if (coin) play.sound().play(play.sound().load("money_drop", false));
        else if (tab_ == Tab::Mine) play.ui(Play::Ui::Click);
        if (tab_ == Tab::Buy) {
            chosen_ = 0;
            confirm_ = false;
            qty_ = 1;
            browseOwed_ = true;
        } else if (tab_ == Tab::Sell) {
            sellSlot_ = -1;
            sellSeen_ = sim::Held{};
            priceText_.clear();
            browseOwed_ = true;
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
        priceText_.clear();
        net::BrokerAsk asked;
        asked.what = net::BrokerWhat::Quote;
        asked.item = sellSeen_.item;
        ask(play, asked);
    } else if (at != things_.end()) {
        sellSeen_ = at->what;
    }
    const int pages = std::max(1, int((things_.size() + kThingRows - 1) / kThingRows));
    thingsPage_ = std::clamp(thingsPage_, 0, pages - 1);
}

// Every rectangle the pointer can press this frame, in the window's units.
void Broker::layout(Play& play) {
    spots_.clear();
    const auto spot = [&](Box box, Hit hit, int index = 0) { spots_.push_back({box, hit, index}); };
    spot({kWide - panel::kHeadInset - panel::kHeadButton, (panel::kHeadBand - panel::kHeadButton) * 0.5f,
          panel::kHeadButton, panel::kHeadButton},
         Hit::Close);
    for (int t = 0; t < 3; ++t) spot({kPad + float(t) * 82.0f, kBarY, 78.0f, kBarH}, Hit::Tab, t);
    if (!mine_.box.empty()) spot({kWide - kPad - 290.0f, kBarY + 2.0f, 116.0f, kBarH - 4.0f}, Hit::Collect, 1);

    if (tab_ == Tab::Buy) {
        spot({kRail.x, kRail.y, kRail.w, 20.0f}, Hit::Search);
        // All (-1), Watched (-2), then the kinds.
        for (int k = -2; k < int(sim::BrokerKind::Count); ++k) {
            const int at = k == -1 ? 0 : k == -2 ? 1 : k + 2;
            spot({kRail.x, kRail.y + 34.0f + float(at) * 15.0f, kRail.w, 14.0f}, Hit::Kind, k);
        }
        for (int f = 0; f < 3; ++f) spot({kRail.x, kRail.y + 246.0f + float(f) * 15.0f, kRail.w, 14.0f}, Hit::Flag, f);
        const bool filtered = query_.kind >= 0 || query_.watchedOnly || !query_.text.empty() || query_.usableBy >= 0 ||
                              query_.excellentOnly || query_.sevenUp || !query_.sellerName.empty();
        if (filtered) spot({kRail.x, kRail.y + 300.0f, kRail.w, 18.0f}, Hit::Clear);
        // The heads: Item, Seller, Left, Price, sorted on.
        const float hx[4] = {kList.x + 30.0f, kList.x + 186.0f, kList.x + 242.0f, kList.x + 268.0f};
        const float hw[4] = {150.0f, 54.0f, 24.0f, 54.0f};
        for (int h = 0; h < 4; ++h) spot({hx[h], kList.y, hw[h], 16.0f}, Hit::Head, h);
        for (int r = 0; r < int(page_.rows.size()) && r < kRows; ++r) {
            spot({kList.x, kList.y + 18.0f + float(r) * kRowH, kList.w, kRowH - 1.0f}, Hit::Row, r);
        }
        spot({kList.right() - 46.0f, kList.bottom() - 18.0f, 20.0f, 16.0f}, Hit::Prev);
        spot({kList.right() - 22.0f, kList.bottom() - 18.0f, 20.0f, 16.0f}, Hit::Next);
        if (const net::BrokerRow* row = chosenRow()) {
            const bool own = (row->flags & net::BrokerRow::Own) != 0;
            spot({kCard.x + 58.0f, kCard.y + 40.0f, 64.0f, 15.0f}, Hit::Watch);
            spot({kCard.x + 8.0f, kCard.y + 64.0f, 110.0f, 13.0f}, Hit::Seller);
            if (!own) spot({kCard.right() - 58.0f, kCard.y + 62.0f, 50.0f, 16.0f}, Hit::Whisper);
            if (row->flags & net::BrokerRow::Alike) {
                spot({kCard.x + 8.0f, kCard.bottom() - 92.0f, 20.0f, 20.0f}, Hit::Qty, -1);
                spot({kCard.x + 64.0f, kCard.bottom() - 92.0f, 20.0f, 20.0f}, Hit::Qty, 1);
                spot({kCard.x + 90.0f, kCard.bottom() - 92.0f, kCard.w - 98.0f, 20.0f}, Hit::MostQty);
            }
            if (!own) spot({kCard.x + 8.0f, kCard.bottom() - 32.0f, kCard.w - 16.0f, 24.0f}, Hit::Buy);
        }
    } else if (tab_ == Tab::Sell) {
        spot({kThings.x, kThings.y, kThings.w * 0.5f, 20.0f}, Hit::Source, 0);
        spot({kThings.x + kThings.w * 0.5f, kThings.y, kThings.w * 0.5f, 20.0f}, Hit::Source, 1);
        const int first = thingsPage_ * kThingRows;
        for (int i = 0; i < kThingRows && first + i < int(things_.size()); ++i) {
            spot({kThings.x, kThings.y + 26.0f + float(i) * 22.0f, kThings.w, 21.0f}, Hit::Thing, first + i);
        }
        if (int(things_.size()) > kThingRows) {
            spot({kThings.right() - 46.0f, kThings.bottom() - 18.0f, 20.0f, 16.0f}, Hit::ThingPrev);
            spot({kThings.right() - 22.0f, kThings.bottom() - 18.0f, 20.0f, 16.0f}, Hit::ThingNext);
        }
        if (sellSlot_ >= 0) {
            spot({kPrice.x + 8.0f, kPrice.y + 20.0f, kPrice.w - 16.0f, 24.0f}, Hit::Price);
            for (int q = 0; q < 3; ++q) {
                const float w = (kPrice.w - 16.0f - 8.0f) / 3.0f;
                spot({kPrice.x + 8.0f + float(q) * (w + 4.0f), kPrice.y + 50.0f, w, 18.0f}, Hit::Quick, q);
            }
            for (int h = 0; h < 3; ++h) {
                const float w = (kPrice.w - 16.0f - 8.0f) / 3.0f;
                spot({kPrice.x + 8.0f + float(h) * (w + 4.0f), kPrice.y + 112.0f, w, 20.0f}, Hit::Hours, h);
            }
            spot({kPrice.x + 8.0f, kPrice.bottom() - 32.0f, kPrice.w - 16.0f, 24.0f}, Hit::List);
        }
    } else {
        if (!mine_.box.empty()) spot({kBox.x + 8.0f, kBox.y + 122.0f, kBox.w - 16.0f, 22.0f}, Hit::Collect);
        for (int r = 0; r < int(mine_.mine.size()) && r < kRows; ++r) {
            const float y = kMine.y + 18.0f + float(r) * kRowH;
            // The listing's name, for its card on hover; clicks nothing.
            spot({kMine.x, y, kMine.w - 250.0f, kRowH}, Hit::Listing, r);
            if (mine_.mine[size_t(r)].id == editing_) {
                spot({kMine.right() - 186.0f, y + 3.0f, 80.0f, kRowH - 7.0f}, Hit::LowerField, r);
                spot({kMine.right() - 102.0f, y + 3.0f, 46.0f, kRowH - 7.0f}, Hit::Save, r);
                spot({kMine.right() - 52.0f, y + 3.0f, 46.0f, kRowH - 7.0f}, Hit::Back, r);
            } else {
                spot({kMine.right() - 102.0f, y + 3.0f, 46.0f, kRowH - 7.0f}, Hit::Lower, r);
                spot({kMine.right() - 52.0f, y + 3.0f, 46.0f, kRowH - 7.0f}, Hit::Cancel, r);
            }
        }
    }
    (void)play;
}

void Broker::press(Play& play, const Spot& s) {
    const sim::Realm& realm = play.realm();
    const auto click = [&]() { play.ui(Play::Ui::Click); };
    focus_ = s.hit == Hit::Search ? Focus::Search
             : s.hit == Hit::Price ? Focus::Price
             : s.hit == Hit::LowerField ? Focus::Lower
                                        : Focus::None;
    if (s.hit != Hit::Buy) confirm_ = false;
    switch (s.hit) {
        case Hit::Close: play.closeBroker(); click(); break;
        case Hit::Tab:
            tab_ = Tab(s.index);
            editing_ = 0;
            click();
            if (tab_ == Tab::Buy) browseOwed_ = true;
            if (tab_ == Tab::Mine) ask(play, {.what = net::BrokerWhat::Look});
            break;
        case Hit::Kind:
            query_.kind = std::max(-1, s.index);
            query_.watchedOnly = s.index == -2;
            query_.page = 0;
            browseOwed_ = true;
            click();
            break;
        case Hit::Flag:
            if (s.index == 0) query_.usableBy = query_.usableBy >= 0 ? -1 : int(realm.hero().kin);
            if (s.index == 1) query_.excellentOnly = !query_.excellentOnly;
            if (s.index == 2) query_.sevenUp = !query_.sevenUp;
            query_.page = 0;
            browseOwed_ = true;
            click();
            break;
        case Hit::Clear:
            query_ = sim::BrokerQuery{};
            browseOwed_ = true;
            click();
            break;
        case Hit::Head: {
            const sim::BrokerQuery::Sort by[4] = {sim::BrokerQuery::Sort::Name, sim::BrokerQuery::Sort::Seller,
                                                  sim::BrokerQuery::Sort::Left, sim::BrokerQuery::Sort::Price};
            query_.descending = query_.sort == by[s.index] ? !query_.descending : false;
            query_.sort = by[s.index];
            browseOwed_ = true;
            click();
            break;
        }
        case Hit::Row:
            chosen_ = page_.rows[size_t(s.index)].id;
            qty_ = 1;
            click();
            break;
        case Hit::Prev:
        case Hit::Next: {
            const int pages = std::max(1, int((page_.total + kRows - 1) / kRows));
            query_.page = std::clamp(query_.page + (s.hit == Hit::Next ? 1 : -1), 0, pages - 1);
            browseOwed_ = true;
            click();
            break;
        }
        case Hit::Seller:
            if (const net::BrokerRow* row = chosenRow(); row && !(row->flags & net::BrokerRow::Alike)) {
                // Everything he sells, by the name on his listings: a token is never sent.
                query_ = sim::BrokerQuery{};
                query_.sellerName = row->seller;
                browseOwed_ = true;
                click();
            }
            break;
        case Hit::Watch:
            // His watches are the server's, kept with the book: the answer is the page again.
            if (const net::BrokerRow* row = chosenRow()) {
                const bool alike = (row->flags & net::BrokerRow::Alike) != 0;
                ask(play, {.what = net::BrokerWhat::Watch, .query = query_, .item = alike ? row->what.item : -1,
                           .listing = alike ? 0u : row->id, .count = (row->flags & net::BrokerRow::Watched) ? 0 : 1});
            }
            click();
            break;
        case Hit::Whisper:
            if (const net::BrokerRow* row = chosenRow()) whisper_ = row->seller;
            click();
            break;
        case Hit::Qty:
            if (const net::BrokerRow* row = chosenRow()) qty_ = std::clamp(qty_ + s.index, 1, std::max(1, row->count));
            click();
            break;
        case Hit::MostQty:
            if (const net::BrokerRow* row = chosenRow()) {
                // As many as his carried and vault Zen pay for, cheapest first.
                int64_t funds = realm.money() + realm.vault().zen();
                int most = 0;
                for (const net::BrokerRow::Tier& t : row->tiers) {
                    for (int i = 0; i < t.count && funds >= t.price; ++i) {
                        funds -= t.price;
                        ++most;
                    }
                }
                qty_ = std::max(1, most);
            }
            click();
            break;
        case Hit::Buy: {
            const net::BrokerRow* row = chosenRow();
            if (!row || waiting_ != 0) break;
            const bool alike = (row->flags & net::BrokerRow::Alike) != 0;
            const int count = alike ? qty_ : 1;
            int64_t cost = 0;
            int left = count;
            for (const net::BrokerRow::Tier& t : row->tiers) {
                const int take = std::min(left, t.count);
                cost += int64_t(take) * t.price;
                left -= take;
            }
            if (!alike) cost = row->price;
            if (cost > realm.money() + realm.vault().zen()) {
                play.ui(Play::Ui::Refused);
                said_ = "You cannot pay for it, with your vault's Zen too.";
                saidBad_ = true;
                saidFor_ = 6.0f;
                break;
            }
            if (cost >= kConfirmFrom && !confirm_) {
                confirm_ = true;
                click();
                break;
            }
            confirm_ = false;
            ask(play, {.what = net::BrokerWhat::Buy, .listing = row->id, .count = count});
            break;
        }
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
        case Hit::Quick: {
            const int64_t npc = realm.tables() && !sellSeen_.empty() ? sim::npcValue(*realm.tables(), sellSeen_) : 0;
            const int64_t typical = !quote_.sales.empty() ? quote_.sales.front().price : 4 * std::max<int64_t>(1, npc);
            const int64_t cheapest = quote_.cheapest > 0 ? quote_.cheapest : typical;
            const int64_t v = s.index == 0 ? cheapest : s.index == 1 ? typical : cheapest * 95 / 100;
            priceText_ = std::to_string(std::max<int64_t>(v, npc));
            click();
            break;
        }
        case Hit::Hours: hours_ = s.index; click(); break;
        case Hit::List:
            if (waiting_ != 0 || sellSeen_.empty()) break;
            ask(play, {.what = net::BrokerWhat::List, .fromVault = fromVault_, .slot = sellSlot_, .seen = sellSeen_,
                       .price = priceTyped(), .hours = uint8_t(sim::kBrokerHours[hours_])});
            break;
        case Hit::Collect:
            if (s.index == 1) {  // the bar's pill: to My listings
                tab_ = Tab::Mine;
                click();
                break;
            }
            if (waiting_ == 0) ask(play, {.what = net::BrokerWhat::Collect});
            break;
        case Hit::Lower:
            editing_ = mine_.mine[size_t(s.index)].id;
            lowerText_ = std::to_string(mine_.mine[size_t(s.index)].price);
            focus_ = Focus::Lower;
            click();
            break;
        case Hit::Save:
            ask(play, {.what = net::BrokerWhat::Lower, .price = parsed(lowerText_), .listing = editing_});
            editing_ = 0;
            break;
        case Hit::Back: editing_ = 0; click(); break;
        case Hit::Cancel:
            ask(play, {.what = net::BrokerWhat::Cancel, .listing = mine_.mine[size_t(s.index)].id});
            break;
        default: break;
    }
}

void Broker::type(Play& play, const Keys& keys) {
    if (focus_ == Focus::None) return;
    std::string& text = focus_ == Focus::Search ? query_.text : focus_ == Focus::Price ? priceText_ : lowerText_;
    const size_t before = text.size();
    for (const char c : keys.typed) {
        if (focus_ == Focus::Search ? (unsigned char)c >= 32 && text.size() < 40 : numeric(c) && text.size() < 10) {
            text += c;
        }
    }
    for (int i = 0; i < keys.backspaces && !text.empty(); ++i) {
        // A whole UTF-8 letter.
        while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80) text.pop_back();
        if (!text.empty()) text.pop_back();
    }
    if (focus_ == Focus::Search && text.size() != before) {
        query_.page = 0;
        browseOwed_ = true;
        browseIn_ = 0.3f;
    }
    if (keys.enter) {
        if (focus_ == Focus::Search) browseIn_ = 0.0f;
        if (focus_ == Focus::Lower && editing_ != 0) {
            ask(play, {.what = net::BrokerWhat::Lower, .price = parsed(lowerText_), .listing = editing_});
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
        // Opened: the Buy tab's first page and his own market, at once.
        play.ui(Play::Ui::Opened);
        tab_ = Tab::Buy;
        editing_ = 0;
        browseOwed_ = true;
        browseIn_ = 0.0f;
        ask(play, {.what = net::BrokerWhat::Look});
    }
    screenW_ = width;
    screenH_ = height;
    const float k = panel::scale();
    // At the vault's place, three columns wide: its right edge where the bag's column begins.
    x_ = panel::columnX(width, 4);
    y_ = panel::panelY(height);
    saidFor_ = std::max(0.0f, saidFor_ - seconds);
    if (tab_ == Tab::Sell) gather(play);
    type(play, keys);
    if (browseOwed_) {
        browseIn_ -= seconds;
        if (browseIn_ <= 0.0f) browse(play);
    }
    layout(play);

    const float ux = (pointer.x - x_) / k, uy = (pointer.y - y_) / k;
    hovered_ = -1;
    if (covers(pointer.x, pointer.y)) {
        for (size_t i = 0; i < spots_.size(); ++i) {
            if (spots_[i].box.has(ux, uy)) hovered_ = int(i);
        }
    }
    if (pointer.pressed) {
        pressed_ = hovered_;
        if (hovered_ < 0) focus_ = Focus::None;
    }
    if (pointer.released) {
        if (pressed_ >= 0 && pressed_ == hovered_) press(play, spots_[size_t(pressed_)]);
        pressed_ = -1;
        layout(play);
        hovered_ = -1;
        for (size_t i = 0; i < spots_.size(); ++i) {
            if (covers(pointer.x, pointer.y) && spots_[i].box.has(ux, uy)) hovered_ = int(i);
        }
    }
    rebuild(play, stage);
}

}  // namespace mu::game
