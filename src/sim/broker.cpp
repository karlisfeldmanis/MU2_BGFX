#include "sim/broker.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <type_traits>

#include "sim/market.h"
#include "sim/quests.h"
#include "sim/wear.h"

namespace mu::sim {

int64_t npcValue(const content::Tables& tables, const Held& what) {
    if (what.empty() || size_t(what.item) >= tables.items.size()) return -1;
    const content::ItemRow& row = tables.items[size_t(what.item)];
    // A quest item is not a merchant's: the class treasure.
    if (classTreasure(row)) return -1;
    const bool stacks = row.group == kGroupPotions;
    int64_t paid = sellingPrice(row, what.refinement, stacks ? std::max<int>(1, what.durability) : 1,
                                what.skill, what.durability, row.durability, what.luck,
                                what.option, excellentCount(what.excellent));
    // Worn gear fetches less: 0.6 of the share gone comes off (CalculateSellingPrice).
    if (wears(row)) paid = wornSellingPrice(paid, what.durability, maximumDurability(row, what));
    // A rune is a share of the Jewel of Creation's price by its rarity (sim/items.h kRuneSale).
    if (const PowerRow* power = powerOf(what.powers[0]); creation(row) && power != nullptr) {
        paid = int64_t(double(paid) * kRuneSale[int(power->rarity)]);
    }
    return paid > 0 ? paid : -1;
}

BrokerKind brokerKind(const content::ItemRow& row) {
    if (creation(row)) return BrokerKind::Runes;
    if (refiningJewel(row)) return BrokerKind::Jewels;
    if (anyWing(row)) return BrokerKind::Wings;
    if (jewellery(row)) return BrokerKind::Jewellery;
    if (scrollOfArchangel(row) || bloodBone(row) || invisibilityCloak(row) || devilsEye(row) ||
        devilsKey(row) || devilsInvitation(row)) {
        return BrokerKind::Tickets;
    }
    if (row.group == kGroupPets) return BrokerKind::Pets;
    if (row.teaches != 0) return BrokerKind::Skills;
    if (row.group == kGroupBows) return BrokerKind::Bows;
    if (row.group == 5) return BrokerKind::Staves;
    if (row.group == kGroupShields) return BrokerKind::Shields;
    if (row.group >= kGroupHelms && row.group <= kGroupBoots) return BrokerKind::Armour;
    if (row.group >= 0 && row.group <= 3) return BrokerKind::Swords;
    return BrokerKind::Count;
}

BrokerNo brokerable(const content::Tables& tables, const Held& what) {
    if (what.empty() || size_t(what.item) >= tables.items.size()) return BrokerNo::Empty;
    const content::ItemRow& row = tables.items[size_t(what.item)];
    if (!tradeable(row, what)) return BrokerNo::Bound;
    if (lochsFeather(row)) return BrokerNo::Quest;
    // The user's goods (2026-10-11): gear, jewels, runes, wings, pets and the events' tickets and
    // parts -- not potions, ammunition, scrolls or orbs.
    if (ammunition(row) || brokerKind(row) == BrokerKind::Count) return BrokerNo::Kind;
    // An orb or a scroll no merchant sells (the user, 2026-10-12: "tech bots to sell scrolls and
    // orbs which is not on vendors so bots can use strong skills"); a shelf's own stays a shelf's.
    // One a quest pays is the market's again (the user, 2026-10-13: "why bots dont put scroll of
    // evil spirits or great orb son market so much?", then "fix it"): kept off since 2026-10-12,
    // only Poison and Nova could be listed, and spare Evil Spirits went to the merchant.
    if (brokerKind(row) == BrokerKind::Skills && stocked(row.group, row.number)) {
        return BrokerNo::Kind;
    }
    // "Must be repaired to list" (the user, 2026-10-11).
    if (wears(row) && what.durability < maximumDurability(row, what)) return BrokerNo::Worn;
    return BrokerNo::Yes;
}

bool brokerAlike(const content::ItemRow& row, const Held& what) {
    const BrokerKind kind = brokerKind(row);
    // A stack of jewels is a listing of its own, bought whole (the user, 2026-10-20): only a
    // single one fills a row of alike things.
    return (kind == BrokerKind::Jewels || kind == BrokerKind::Tickets) && what.refinement == 0 &&
           (!stacks(row) || what.durability <= 1) &&
           !what.skill && !what.luck && what.option == 0 && what.excellent == 0;
}

// a: the item; b: refinement and durability; c: the flags, option, excellent and sockets;
// d: the three powers and the wing; target: the three affixes.
void packHeld(const Held& what, Command& into) {
    into.a = what.item;
    into.b = int32_t(uint32_t(uint16_t(what.refinement)) | uint32_t(uint16_t(what.durability)) << 16);
    into.c = int32_t(uint32_t((what.skill ? 1 : 0) | (what.luck ? 2 : 0) | (what.bound ? 4 : 0)) |
                     uint32_t(uint8_t(what.option)) << 8 | uint32_t(what.excellent) << 16 |
                     uint32_t(what.sockets) << 24);
    into.d = int32_t(uint32_t(what.powers[0]) | uint32_t(what.powers[1]) << 8 |
                     uint32_t(what.powers[2]) << 16 | uint32_t(what.wing) << 24);
    into.target = uint32_t(what.affixes[0]) | uint32_t(what.affixes[1]) << 8 |
                  uint32_t(what.affixes[2]) << 16;
}

Held unpackHeld(const Command& from) {
    Held what;
    what.item = from.a;
    what.refinement = int16_t(uint16_t(uint32_t(from.b)));
    what.durability = int16_t(uint16_t(uint32_t(from.b) >> 16));
    const uint32_t c = uint32_t(from.c), d = uint32_t(from.d);
    what.skill = (c & 1) != 0;
    what.luck = (c & 2) != 0;
    what.bound = (c & 4) != 0;
    what.option = int8_t(uint8_t(c >> 8));
    what.excellent = uint8_t(c >> 16);
    what.sockets = uint8_t(c >> 24);
    for (int i = 0; i < 3; ++i) what.powers[i] = uint8_t(d >> (8 * i));
    what.wing = uint8_t(d >> 24);
    for (int i = 0; i < 3; ++i) what.affixes[i] = uint8_t(from.target >> (8 * i));
    return what;
}

// ---- the book ----------------------------------------------------------------------------------

namespace {

bool hoursAllowed(int hours) {
    return std::find(std::begin(kBrokerHours), std::end(kBrokerHours), hours) != std::end(kBrokerHours);
}

}  // namespace

BrokerNo Book::canList(const std::string& account, int64_t price, int64_t floor, int hours) const {
    if (!hoursAllowed(hours)) return BrokerNo::Hours;
    if (price > kBrokerMostPrice) return BrokerNo::High;
    if (price <= 0 || price < floor) return BrokerNo::Low;
    if (countOf(account) >= kBrokerListings) return BrokerNo::Full;
    return BrokerNo::Yes;
}

uint32_t Book::list(Listing listing, int hours, int64_t now) {
    listing.id = nextId_++;
    listing.secondsLeft = hours * 3600;
    listing.listedAt = now;
    listings_.push_back(std::move(listing));
    ++version_;
    return listings_.back().id;
}

const Listing* Book::find(uint32_t id) const {
    for (const Listing& one : listings_) {
        if (one.id == id) return &one;
    }
    return nullptr;
}

BrokerNo Book::canBuy(uint32_t id, const std::string& buyerAccount) const {
    const Listing* one = find(id);
    if (one == nullptr) return BrokerNo::Gone;
    // Not his own account's, the user's (2026-10-11): no passing a thing round the market.
    if (one->account == buyerAccount) return BrokerNo::Own;
    return BrokerNo::Yes;
}

Listing Book::take(uint32_t id) {
    for (size_t i = 0; i < listings_.size(); ++i) {
        if (listings_[i].id != id) continue;
        Listing out = std::move(listings_[i]);
        listings_.erase(listings_.begin() + std::ptrdiff_t(i));
        ++version_;
        return out;
    }
    return Listing{};
}

void Book::restore(const Listing& listing) {
    if (listing.id == 0) return;
    listings_.push_back(listing);
    ++version_;
}

void Book::sold(const Listing& listing, int64_t now) {
    Parcel paid;
    paid.zen = listing.price;
    paid.at = now;
    paid.why = Parcel::Why::Sold;
    paid.what = Held{};
    deliver(listing.seller, paid);
    const Sale sale{listing.what, listing.price, now};
    std::vector<Sale>& kind = sales_[listing.what.item];
    kind.insert(kind.begin(), sale);
    if (kind.size() > kBrokerSalesKept) kind.resize(kBrokerSalesKept);
    std::vector<Sale>& his = soldBy_[listing.seller];
    his.insert(his.begin(), sale);
    if (his.size() > kBrokerSoldKept) his.resize(kBrokerSoldKept);
}

const std::vector<Sale>& Book::salesOf(int32_t item) const {
    static const std::vector<Sale> none;
    const auto it = sales_.find(item);
    return it == sales_.end() ? none : it->second;
}

const std::vector<Sale>& Book::soldBy(uint64_t seller) const {
    static const std::vector<Sale> none;
    const auto it = soldBy_.find(seller);
    return it == soldBy_.end() ? none : it->second;
}

BrokerNo Book::cancel(uint32_t id, uint64_t seller, int64_t now) {
    const Listing* one = find(id);
    if (one == nullptr) return BrokerNo::Gone;
    if (one->seller != seller) return BrokerNo::NotYours;
    const Listing back = take(id);
    deliver(seller, Parcel{back.what, 0, now, Parcel::Why::Cancelled});
    return BrokerNo::Yes;
}

BrokerNo Book::lower(uint32_t id, uint64_t seller, int64_t price) {
    for (Listing& one : listings_) {
        if (one.id != id) continue;
        if (one.seller != seller) return BrokerNo::NotYours;
        if (price >= one.price) return BrokerNo::Higher;
        if (price <= 0 || price < one.floor) return BrokerNo::Low;
        one.price = price;
        ++version_;
        return BrokerNo::Yes;
    }
    return BrokerNo::Gone;
}

int Book::expire(int seconds, int64_t now) {
    int ran = 0;
    for (size_t i = 0; i < listings_.size();) {
        listings_[i].secondsLeft -= seconds;
        if (listings_[i].secondsLeft > 0) {
            ++i;
            continue;
        }
        const Listing out = listings_[i];
        listings_.erase(listings_.begin() + std::ptrdiff_t(i));
        deliver(out.seller, Parcel{out.what, 0, now, Parcel::Why::Unsold});
        ++ran;
    }
    // What has lain in a Collect thirty days is gone (the user, 2026-10-11).
    for (auto box = collect_.begin(); box != collect_.end();) {
        std::vector<Parcel>& parcels = box->second;
        const size_t before = parcels.size();
        parcels.erase(std::remove_if(parcels.begin(), parcels.end(),
                                     [&](const Parcel& p) { return now - p.at >= kBrokerCollectSeconds; }),
                      parcels.end());
        if (parcels.size() != before) ++version_;
        box = parcels.empty() ? collect_.erase(box) : std::next(box);
    }
    if (ran > 0) ++version_;
    return ran;
}

void Book::deliver(uint64_t owner, const Parcel& parcel) {
    if (parcel.what.empty() && parcel.zen <= 0) return;
    collect_[owner].push_back(parcel);
    ++version_;
}

const std::vector<Parcel>& Book::collectOf(uint64_t owner) const {
    static const std::vector<Parcel> none;
    const auto it = collect_.find(owner);
    return it == collect_.end() ? none : it->second;
}

Parcel Book::takeParcel(uint64_t owner, int index) {
    const auto it = collect_.find(owner);
    if (it == collect_.end() || index < 0 || size_t(index) >= it->second.size()) return Parcel{};
    Parcel out = it->second[size_t(index)];
    it->second.erase(it->second.begin() + index);
    if (it->second.empty()) collect_.erase(it);
    ++version_;
    return out;
}

int Book::countOf(const std::string& account) const {
    return int(std::count_if(listings_.begin(), listings_.end(),
                             [&](const Listing& one) { return one.account == account; }));
}

bool Book::watch(uint64_t who, uint64_t key, bool on) {
    std::vector<uint64_t>& list = watches_[who];
    const auto at = std::find(list.begin(), list.end(), key);
    if (!on) {
        if (at != list.end()) list.erase(at);
        if (list.empty()) watches_.erase(who);
        ++version_;
        return true;
    }
    if (at != list.end()) return true;
    // Listings sold or gone since make room first: their ids are never given again.
    list.erase(std::remove_if(list.begin(), list.end(),
                              [&](uint64_t k) { return k < watchItem(0) && find(uint32_t(k)) == nullptr; }),
               list.end());
    if (list.size() >= kBrokerWatched) return false;
    list.push_back(key);
    ++version_;
    return true;
}

bool Book::watching(uint64_t who, const Listing& listing, bool alike) const {
    const auto it = watches_.find(who);
    if (it == watches_.end()) return false;
    const uint64_t key = alike ? watchItem(listing.what.item) : listing.id;
    return std::find(it->second.begin(), it->second.end(), key) != it->second.end();
}

// ---- bytes -------------------------------------------------------------------------------------
// Little-endian, a version first. The server keeps them in characters.db (server/src/broker.cpp).

namespace {

constexpr uint32_t kBookMagic = 0x4b4f4f42;  // "BOOK"
constexpr uint32_t kBookShape = 2;  // 2: the watches

struct Writer {
    std::vector<uint8_t>& out;
    void raw(const void* p, size_t n) {
        const auto* b = static_cast<const uint8_t*>(p);
        out.insert(out.end(), b, b + n);
    }
    void u8(uint8_t v) { out.push_back(v); }
    void u32(uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i))); }
    void u64(uint64_t v) { for (int i = 0; i < 8; ++i) out.push_back(uint8_t(v >> (8 * i))); }
    void i64(int64_t v) { u64(uint64_t(v)); }
    void text(const std::string& s) {
        u32(uint32_t(s.size()));
        raw(s.data(), s.size());
    }
    void held(const Held& h) {
        Command packed;
        packHeld(h, packed);
        u32(uint32_t(packed.a));
        u32(uint32_t(packed.b));
        u32(uint32_t(packed.c));
        u32(uint32_t(packed.d));
        u32(packed.target);
    }
};

struct Reader {
    const std::vector<uint8_t>& in;
    size_t at = 0;
    bool ok = true;
    bool need(size_t n) {
        if (at + n > in.size()) ok = false;
        return ok;
    }
    uint8_t u8() { return need(1) ? in[at++] : 0; }
    uint32_t u32() {
        if (!need(4)) return 0;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= uint32_t(in[at++]) << (8 * i);
        return v;
    }
    uint64_t u64() {
        if (!need(8)) return 0;
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= uint64_t(in[at++]) << (8 * i);
        return v;
    }
    int64_t i64() { return int64_t(u64()); }
    std::string text() {
        const uint32_t n = u32();
        if (n > (1u << 16) || !need(n)) {
            ok = false;
            return {};
        }
        std::string s(reinterpret_cast<const char*>(in.data() + at), n);
        at += n;
        return s;
    }
    Held held() {
        Command packed;
        packed.a = int32_t(u32());
        packed.b = int32_t(u32());
        packed.c = int32_t(u32());
        packed.d = int32_t(u32());
        packed.target = u32();
        return unpackHeld(packed);
    }
};

}  // namespace

std::vector<uint8_t> Book::write() const {
    std::vector<uint8_t> bytes;
    Writer w{bytes};
    w.u32(kBookMagic);
    w.u32(kBookShape);
    w.u32(nextId_);
    w.u32(uint32_t(listings_.size()));
    for (const Listing& one : listings_) {
        w.u32(one.id);
        w.u64(one.seller);
        w.text(one.account);
        w.text(one.name);
        w.held(one.what);
        w.i64(one.price);
        w.i64(one.floor);
        w.u32(uint32_t(one.secondsLeft));
        w.i64(one.listedAt);
    }
    w.u32(uint32_t(collect_.size()));
    for (const auto& [owner, parcels] : collect_) {
        w.u64(owner);
        w.u32(uint32_t(parcels.size()));
        for (const Parcel& p : parcels) {
            w.held(p.what);
            w.i64(p.zen);
            w.i64(p.at);
            w.u8(uint8_t(p.why));
        }
    }
    const auto sales = [&](const auto& table) {
        w.u32(uint32_t(table.size()));
        for (const auto& [key, list] : table) {
            w.u64(uint64_t(key));
            w.u32(uint32_t(list.size()));
            for (const Sale& one : list) {
                w.held(one.what);
                w.i64(one.price);
                w.i64(one.at);
            }
        }
    };
    sales(sales_);
    sales(soldBy_);
    w.u32(uint32_t(watches_.size()));
    for (const auto& [who, keys] : watches_) {
        w.u64(who);
        w.u32(uint32_t(keys.size()));
        for (const uint64_t key : keys) w.u64(key);
    }
    return bytes;
}

void Book::renumber(const std::vector<int32_t>& map) {
    const auto to = [&](int32_t item) {
        return item >= 0 && item < int32_t(map.size()) ? map[size_t(item)] : -1;
    };
    const auto move = [&](Held& what) {
        if (what.empty()) return true;
        what.item = to(what.item);
        return what.item >= 0;
    };
    listings_.erase(std::remove_if(listings_.begin(), listings_.end(),
                                   [&](Listing& one) { return !move(one.what); }),
                    listings_.end());
    for (auto& [owner, parcels] : collect_) {
        for (Parcel& p : parcels) {
            if (!move(p.what)) p.what = Held{};  // its Zen still comes
        }
    }
    std::map<int32_t, std::vector<Sale>> sales;
    for (auto& [item, list] : sales_) {
        const int32_t moved = to(item);
        if (moved < 0) continue;
        for (Sale& s : list) move(s.what);
        sales[moved] = std::move(list);
    }
    sales_ = std::move(sales);
    for (auto& [seller, list] : soldBy_) {
        for (Sale& s : list) move(s.what);
    }
    // A watch on a kind is watchItem's: its row in the low half.
    for (auto& [who, keys] : watches_) {
        std::vector<uint64_t> kept;
        for (const uint64_t key : keys) {
            if (key < watchItem(0)) {
                kept.push_back(key);
            } else if (const int32_t moved = to(int32_t(uint32_t(key))); moved >= 0) {
                kept.push_back(watchItem(moved));
            }
        }
        keys = std::move(kept);
    }
    ++version_;
}

bool Book::read(const std::vector<uint8_t>& bytes) {
    Reader r{bytes};
    if (r.u32() != kBookMagic) return false;
    const uint32_t shape = r.u32();
    if (shape < 1 || shape > kBookShape) return false;
    Book book;
    book.nextId_ = r.u32();
    const uint32_t listed = r.u32();
    for (uint32_t i = 0; i < listed && r.ok; ++i) {
        Listing one;
        one.id = r.u32();
        one.seller = r.u64();
        one.account = r.text();
        one.name = r.text();
        one.what = r.held();
        one.price = r.i64();
        one.floor = r.i64();
        one.secondsLeft = int32_t(r.u32());
        one.listedAt = r.i64();
        book.listings_.push_back(std::move(one));
    }
    const uint32_t boxes = r.u32();
    for (uint32_t i = 0; i < boxes && r.ok; ++i) {
        const uint64_t owner = r.u64();
        const uint32_t count = r.u32();
        std::vector<Parcel>& parcels = book.collect_[owner];
        for (uint32_t k = 0; k < count && r.ok; ++k) {
            Parcel p;
            p.what = r.held();
            p.zen = r.i64();
            p.at = r.i64();
            p.why = Parcel::Why(r.u8());
            parcels.push_back(p);
        }
    }
    const auto sales = [&](auto& table) {
        const uint32_t keys = r.u32();
        for (uint32_t i = 0; i < keys && r.ok; ++i) {
            const uint64_t key = r.u64();
            const uint32_t count = r.u32();
            auto& list = table[static_cast<typename std::decay_t<decltype(table)>::key_type>(key)];
            for (uint32_t k = 0; k < count && r.ok; ++k) {
                Sale one;
                one.what = r.held();
                one.price = r.i64();
                one.at = r.i64();
                list.push_back(one);
            }
        }
    };
    sales(book.sales_);
    sales(book.soldBy_);
    const uint32_t watchers = shape >= 2 ? r.u32() : 0;
    for (uint32_t i = 0; i < watchers && r.ok; ++i) {
        const uint64_t who = r.u64();
        const uint32_t count = r.u32();
        for (uint32_t k = 0; k < count && r.ok; ++k) {
            const uint64_t key = r.u64();
            if (k < kBrokerWatched) book.watches_[who].push_back(key);
        }
    }
    if (!r.ok) return false;
    book.version_ = version_ + 1;
    *this = std::move(book);
    return true;
}

// ---- the Buy tab's search ----------------------------------------------------------------------

std::vector<const Listing*> brokerMatches(const Book& book, const content::Tables& tables,
                                          const BrokerQuery& query) {
    std::string text = query.text;
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    std::vector<const Listing*> out;
    for (const Listing& one : book.listings()) {
        if (size_t(one.what.item) >= tables.items.size()) continue;
        const content::ItemRow& row = tables.items[size_t(one.what.item)];
        if (query.kind >= 0 && int(brokerKind(row)) != query.kind) continue;
        if (query.seller != 0 && one.seller != query.seller) continue;
        if (!query.sellerName.empty() && one.name != query.sellerName) continue;
        if (query.watchedOnly && !book.watching(query.watcher, one, brokerAlike(row, one.what))) continue;
        if (query.usableBy >= 0 && (row.classes & 0xff) != 0 &&
            (row.classes & classBit(Kin(query.usableBy))) == 0) {
            continue;
        }
        if (query.excellentOnly && one.what.excellent == 0) continue;
        if (query.sevenUp && one.what.refinement < 7) continue;
        if (!text.empty()) {
            std::string label = row.label;
            std::transform(label.begin(), label.end(), label.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
            if (label.find(text) == std::string::npos) continue;
        }
        out.push_back(&one);
    }
    const auto label = [&](const Listing* l) -> const std::string& {
        return tables.items[size_t(l->what.item)].label;
    };
    std::stable_sort(out.begin(), out.end(), [&](const Listing* a, const Listing* b) {
        bool less = false;
        switch (query.sort) {
            case BrokerQuery::Sort::Price: less = a->price < b->price; break;
            case BrokerQuery::Sort::Name: less = label(a) < label(b); break;
            case BrokerQuery::Sort::Seller: less = a->name < b->name; break;
            case BrokerQuery::Sort::Left: less = a->secondsLeft < b->secondsLeft; break;
            case BrokerQuery::Sort::Newest:
                // Ids rise as things are listed, so they break a tie within one second.
                less = a->listedAt != b->listedAt ? a->listedAt > b->listedAt : a->id > b->id;
                break;
        }
        if (query.descending) {
            bool more = false;
            switch (query.sort) {
                case BrokerQuery::Sort::Price: more = a->price > b->price; break;
                case BrokerQuery::Sort::Name: more = label(a) > label(b); break;
                case BrokerQuery::Sort::Seller: more = a->name > b->name; break;
                case BrokerQuery::Sort::Left: more = a->secondsLeft > b->secondsLeft; break;
                case BrokerQuery::Sort::Newest:
                    more = a->listedAt != b->listedAt ? a->listedAt < b->listedAt : a->id < b->id;
                    break;
            }
            return more;
        }
        return less;
    });
    return out;
}

}  // namespace mu::sim
