// The Broker (sim/broker.h, docs/game/broker.md): the book's rules, its bytes, the Buy tab's search,
// and the realm's four commands that move a player's things in and out of the market. Part of
// sim_test (tests/sim_test.cpp's check and main); a file of its own so that one stops growing.
#include <cstdio>
#include <string>
#include <vector>

#include "content/tables.h"
#include "net/broker_wire.h"
#include "net/wire.h"
#include "sim/broker.h"
#include "sim/realm.h"
#include "sim/vault.h"

using namespace mu;

namespace {

// sim_test.cpp's own, handed in: they are its file's and count its checks.
void (*check)(bool held, const char* what) = nullptr;
void (*checkEqual)(long long got, long long wanted, const char* what) = nullptr;

int32_t labelled(const content::Tables& tables, const char* label) {
    for (size_t i = 0; i < tables.items.size(); ++i) {
        if (tables.items[i].label == label) return int32_t(i);
    }
    return -1;
}

sim::Listing listingOf(const char* account, uint64_t seller, int32_t item, int64_t price) {
    sim::Listing one;
    one.account = account;
    one.seller = seller;
    one.name = account;
    one.what.item = item;
    one.what.durability = 1;
    one.price = price;
    one.floor = 1000;
    return one;
}

void testBrokerRules(const content::Tables& tables) {
    const int32_t bless = labelled(tables, "Jewel of Bless");
    const int32_t armour = tables.itemNamed("ArmorMale01");
    const int32_t potion = tables.itemNamed("Potion01");
    check(bless >= 0 && armour >= 0 && potion >= 0, "a jewel, an armour and a potion to list");
    if (bless < 0 || armour < 0 || potion < 0) return;

    // A whole Held through a command's fields and back.
    sim::Held rich;
    rich.item = armour;
    rich.refinement = 9;
    rich.durability = 41;
    rich.skill = true;
    rich.luck = true;
    rich.option = 3;
    rich.excellent = 0x2a;
    rich.sockets = 2;
    rich.powers[0] = 7;
    rich.powers[1] = 11;
    rich.affixes[2] = 5;
    rich.wing = 9;
    sim::Command packed;
    sim::packHeld(rich, packed);
    const sim::Held back = sim::unpackHeld(packed);
    check(back.item == rich.item && back.refinement == 9 && back.durability == 41 && back.skill &&
              back.luck && back.option == 3 && back.excellent == 0x2a && back.sockets == 2 &&
              back.powers[0] == 7 && back.powers[1] == 11 && back.affixes[2] == 5 && back.wing == 9 &&
              !back.bound,
          "a Held rides a command whole");

    sim::Held jewel;
    jewel.item = bless;
    jewel.durability = 1;
    check(sim::brokerable(tables, jewel) == sim::BrokerNo::Yes, "a Bless may be listed");
    check(sim::brokerKind(tables.items[size_t(bless)]) == sim::BrokerKind::Jewels, "under Jewels");
    check(sim::brokerAlike(tables.items[size_t(bless)], jewel), "and rows with the other Blesses");
    sim::Held bound = jewel;
    bound.bound = true;
    check(sim::brokerable(tables, bound) == sim::BrokerNo::Bound, "a bound thing may not");
    sim::Held drink;
    drink.item = potion;
    drink.durability = 3;
    check(sim::brokerable(tables, drink) == sim::BrokerNo::Kind, "nor a potion");
    sim::Held worn;
    worn.item = armour;
    worn.durability = 1;
    check(sim::brokerable(tables, worn) == sim::BrokerNo::Worn, "nor gear not mended");
    check(sim::npcValue(tables, jewel) > 0, "a Bless has an NPC value, the Broker's floor");

    sim::Book book;
    checkEqual(int(book.canList("acct", 500, 1000, 24)), int(sim::BrokerNo::Low), "under the floor is refused");
    checkEqual(int(book.canList("acct", sim::kBrokerMostPrice + 1, 1000, 24)), int(sim::BrokerNo::High),
               "over 2,000,000,000 is refused");
    checkEqual(int(book.canList("acct", 5000, 1000, 13)), int(sim::BrokerNo::Hours), "only 12, 24 or 48 hours");
    for (int i = 0; i < sim::kBrokerListings; ++i) {
        check(book.canList("acct", 5000, 1000, 24) == sim::BrokerNo::Yes, "an account lists up to 20");
        book.list(listingOf("acct", 1 + uint64_t(i % 2), bless, 5000 + i), 24, 100);
    }
    checkEqual(int(book.canList("acct", 5000, 1000, 24)), int(sim::BrokerNo::Full),
               "and not a 21st, across its characters");
    check(book.canList("other", 5000, 1000, 24) == sim::BrokerNo::Yes, "another account still may");

    const uint32_t first = book.listings().front().id;
    checkEqual(int(book.canBuy(first, "acct")), int(sim::BrokerNo::Own), "his own account's is not his to buy");
    check(book.canBuy(first, "buyer") == sim::BrokerNo::Yes, "another may");
    const sim::Listing taken = book.take(first);
    check(book.find(first) == nullptr && taken.id == first, "a buy takes it out of the book");
    book.restore(taken);
    check(book.find(first) != nullptr, "and a buyer who cannot pay puts it back");
    const sim::Listing bought = book.take(first);
    book.sold(bought, 200);
    checkEqual(book.collectOf(bought.seller).back().zen, bought.price, "the whole price to the seller's Collect");

    const uint32_t second = book.listings().front().id;
    const uint64_t owner = book.listings().front().seller;
    checkEqual(int(book.cancel(second, owner + 99, 300)), int(sim::BrokerNo::NotYours), "nobody cancels another's");
    checkEqual(int(book.lower(second, owner, 999999)), int(sim::BrokerNo::Higher), "a lowered price must be lower");
    checkEqual(int(book.lower(second, owner, 10)), int(sim::BrokerNo::Low), "and not under the floor");
    check(book.lower(second, owner, 4000) == sim::BrokerNo::Yes && book.find(second)->price == 4000,
          "lowered for nothing");
    const size_t parcels = book.collectOf(owner).size();
    check(book.cancel(second, owner, 300) == sim::BrokerNo::Yes, "cancelled by its seller");
    check(book.collectOf(owner).size() == parcels + 1 &&
              book.collectOf(owner).back().why == sim::Parcel::Why::Cancelled &&
              book.collectOf(owner).back().what.item == bless,
          "and the thing comes back to his Collect");

    // Bytes and back, then time.
    const std::vector<uint8_t> bytes = book.write();
    sim::Book read;
    check(read.read(bytes) && read.listings().size() == book.listings().size() &&
              read.collectOf(owner).size() == book.collectOf(owner).size() &&
              read.write() == bytes,
          "the book reads back as written");
    const size_t before = read.listings().size();
    checkEqual(read.expire(23 * 3600, 400), 0, "nothing runs out in 23 hours of a 24");
    checkEqual(read.expire(3600, 500), int(before), "every 24-hour listing runs out at 24");
    check(read.listings().empty() && read.collectOf(owner).back().why == sim::Parcel::Why::Unsold,
          "and goes back to its seller's Collect");
    read.expire(1, 500 + sim::kBrokerCollectSeconds);
    check(read.collectOf(owner).empty(), "what lies thirty days in a Collect is gone");
    const sim::Parcel none = read.takeParcel(owner, 0);
    check(none.what.empty() && none.zen == 0, "and an empty Collect gives nothing");

    // The Buy tab's search.
    sim::Book shop;
    shop.list(listingOf("a", 1, bless, 3000), 24, 1);
    shop.list(listingOf("b", 2, armour, 1500), 24, 2);
    shop.list(listingOf("c", 3, bless, 2000), 24, 3);
    sim::BrokerQuery query;
    query.kind = int(sim::BrokerKind::Jewels);
    std::vector<const sim::Listing*> found = sim::brokerMatches(shop, tables, query);
    check(found.size() == 2 && found[0]->price == 2000 && found[1]->price == 3000,
          "jewels only, the cheapest first");
    query = sim::BrokerQuery{};
    query.text = "bless";
    query.descending = true;
    found = sim::brokerMatches(shop, tables, query);
    check(found.size() == 2 && found[0]->price == 3000, "found by name, any case, dearest first");
    query = sim::BrokerQuery{};
    query.seller = 2;
    found = sim::brokerMatches(shop, tables, query);
    check(found.size() == 1 && found[0]->what.item == armour, "and one seller's alone");

    // The watch list: a gear piece by its listing, the Blesses all together, kept in the bytes.
    const uint32_t gear = found[0]->id;
    const uint64_t me = 77;
    check(shop.watch(me, gear, true) && shop.watch(me, sim::Book::watchItem(bless), true), "he watches two");
    query = sim::BrokerQuery{};
    query.watchedOnly = true;
    query.watcher = me;
    checkEqual(int(sim::brokerMatches(shop, tables, query).size()), 3, "the armour and both Blesses are watched");
    query.watcher = 78;
    check(sim::brokerMatches(shop, tables, query).empty(), "and nobody else's watches show him");
    sim::Book kept;
    check(kept.read(shop.write()) && kept.watching(me, *kept.find(gear), false), "watches read back");
    shop.watch(me, gear, false);
    query.watcher = me;
    checkEqual(int(sim::brokerMatches(shop, tables, query).size()), 2, "unwatched, the armour is gone from it");
    for (uint32_t i = 0; i < sim::kBrokerWatched - 1; ++i) shop.watch(me, sim::Book::watchItem(int32_t(1000 + i)), true);
    check(!shop.watch(me, gear, true), "and no more than 40");
}

void testBrokerRealm(const content::Tables& tables) {
    using Kind = sim::Command::Kind;
    const int32_t bless = labelled(tables, "Jewel of Bless");
    const int32_t armour = tables.itemNamed("ArmorMale01");
    if (bless < 0 || armour < 0) return;
    int baz = -1;
    for (size_t i = 0; i < tables.folk.size(); ++i) {
        if (tables.folk[i].number == sim::kVaultKeeper && baz < 0) baz = int(i);
    }
    check(baz >= 0, "Baz is in Lorencia's table");
    if (baz < 0) return;

    sim::Realm realm;
    const content::Townsperson& keeper = tables.folk[size_t(baz)];
    check(realm.raise(&tables, 9, keeper.x, keeper.y + 1, sim::Kin::DarkKnight, 50),
          "a realm raises beside Baz");
    const uint32_t me = realm.hero().id;
    std::vector<sim::Happening> heard;
    const auto run = [&](sim::Command c) -> int {
        c.player = me;
        c.ticket = 77;
        realm.command(c);
        realm.step();
        for (const sim::Happening& one : realm.happenings()) {
            if (one.what == sim::What::Answered && one.c == 77) return one.b;
        }
        return -99;
    };

    // Out of the bag: only the very thing the server saw.
    const int slot = realm.give(armour, -1, 4);
    sim::Held seen = realm.satchel()[slot];
    sim::Command take{.kind = Kind::BrokerTake, .zen = slot};
    sim::Held wrong = seen;
    wrong.refinement = 5;
    sim::packHeld(wrong, take);
    take.zen = slot;
    checkEqual(run(take), -1, "a slot that no longer holds what he listed is not taken");
    check(!realm.satchel()[slot].empty(), "and stays in his bag");
    sim::packHeld(seen, take);
    take.zen = slot;
    checkEqual(run(take), 1, "the very thing is taken");
    check(realm.satchel()[slot].empty(), "out of his bag, in every mirror's realm alike");

    // Out of the vault.
    sim::Request talk;
    talk.kind = sim::Request::Kind::Talk;
    talk.target = uint32_t(baz);
    realm.ask(talk);
    for (int tick = 0; tick < 200 && realm.banking() < 0; ++tick) realm.step();
    check(realm.banking() == baz, "served at Baz");
    const int jewelAt = realm.give(bless);
    const int cell = realm.deposit(jewelAt, -1);
    check(cell >= 0, "a Bless into the vault");
    sim::Command fromVault{.kind = Kind::BrokerTake};
    sim::packHeld(realm.vault()[cell], fromVault);
    fromVault.zen = cell | 0x10000;
    checkEqual(run(fromVault), 1, "taken straight out of the vault");
    check(realm.vault()[cell].empty(), "and the vault cell is clear");

    // A purchase: carried Zen first, then the vault's.
    realm.earn(1000);
    realm.depositZen(500);
    const int64_t carried = realm.money(), banked = realm.vault().zen();
    sim::Held jewelThing;
    jewelThing.item = bless;
    jewelThing.durability = 1;
    sim::Command buy{.kind = Kind::BrokerBuy};
    sim::packHeld(jewelThing, buy);
    buy.zen = carried + banked + 1;
    checkEqual(run(buy), -1, "a price over his carried and his vault's Zen is refused");
    buy.zen = carried + 200;
    checkEqual(run(buy), 1, "a price he and his vault can pay is paid, into his bag");
    checkEqual(realm.money(), 0, "his carried Zen first");
    checkEqual(realm.vault().zen(), banked - 200, "then the vault's");

    sim::Command give{.kind = Kind::BrokerGive};
    sim::packHeld(jewelThing, give);
    checkEqual(run(give), 1, "a thing out of his Collect into his bag");
    sim::Command zen{.kind = Kind::BrokerZen, .zen = 12345};
    checkEqual(run(zen), 12345, "and Zen");
    checkEqual(realm.money(), 12345, "carried");
    realm.earn(sim::kBrokerMostPrice - realm.money() - 10);
    checkEqual(run(zen), 10, "no more than MU's ceiling: the rest stays in his Collect");

    // A full bag: paid, and the thing waits in his Collect (answer 2).
    sim::Held big;
    big.item = armour;
    big.durability = 1;
    while (realm.give(armour) >= 0) {
    }
    sim::Command full{.kind = Kind::BrokerBuy};
    sim::packHeld(big, full);
    full.zen = 1;
    checkEqual(run(full), 2, "a bag with no room: paid, and it goes to his Collect");

    check(!realm.atBroker(me), "nobody is at a Broker who stands at Baz");

    // Orsin, Lorencia's Broker (tools/cook.py FOLK_VERSION075): talked to, his window opens and
    // the server would serve the hero; walking off closes it.
    int orsin = -1;
    for (size_t i = 0; i < tables.folk.size(); ++i) {
        if (tables.folk[i].number == sim::kBroker) orsin = int(i);
    }
    check(orsin >= 0, "Lorencia has a Broker");
    if (orsin < 0) return;
    sim::Realm market;
    const content::Townsperson& broker = tables.folk[size_t(orsin)];
    check(market.raise(&tables, 11, broker.x - 4, broker.y, sim::Kin::FairyElf, 30), "a realm raises near him");
    sim::Request talkTo;
    talkTo.kind = sim::Request::Kind::Talk;
    talkTo.target = uint32_t(orsin);
    market.ask(talkTo);
    for (int tick = 0; tick < 200 && market.brokering() < 0; ++tick) market.step();
    checkEqual(market.brokering(), orsin, "talked to, the Broker's window opens");
    check(market.atBroker(market.hero().id), "and the server would serve him there");
    sim::Request walk;
    walk.kind = sim::Request::Kind::WalkTo;
    walk.column = broker.x - 12;
    walk.row = broker.y;
    market.ask(walk);
    market.step();
    checkEqual(market.brokering(), -1, "any other order walks away from it");
}

void testBrokerWire() {
    net::BrokerAsk ask;
    ask.what = net::BrokerWhat::List;
    ask.ask = 41;
    ask.query.kind = int(sim::BrokerKind::Jewels);
    ask.query.text = "bless";
    ask.query.sevenUp = true;
    ask.query.sort = sim::BrokerQuery::Sort::Seller;
    ask.query.page = 3;
    ask.query.sellerName = "Elowen";
    ask.fromVault = true;
    ask.slot = 17;
    ask.seen.item = 12;
    ask.seen.refinement = 7;
    ask.price = 3400000;
    ask.hours = 48;
    std::vector<uint8_t> bytes;
    net::put(bytes, ask);
    net::BrokerView view;
    view.answered = 41;
    view.said = "Plate Armor +7 is on sale.";
    view.parts = net::BrokerView::Page | net::BrokerView::Mine | net::BrokerView::Box | net::BrokerView::Quoted;
    net::BrokerRow row;
    row.id = 9;
    row.seller = "Elowen";
    row.flags = net::BrokerRow::Alike | net::BrokerRow::Online;
    row.count = 37;
    row.tiers = {{1150000, 12, "Garran"}, {1180000, 9, "Maeve"}};
    view.rows.push_back(row);
    view.total = 30;
    view.page = 2;
    view.mine.push_back(row);
    view.accountCount = 6;
    view.soldLately.push_back({sim::Held{}, 4680000, 77});
    view.box.push_back({sim::Held{}, 6840000, 3600, 0});
    view.item = 12;
    view.cheapest = 1150000;
    view.onSale = 37;
    net::put(bytes, view);

    net::Kind kind{};
    std::vector<uint8_t> body;
    net::BrokerAsk askBack;
    check(net::take(bytes, kind, body) == 1 && kind == net::Kind::BrokerAsk && net::parse(body, askBack) &&
              askBack.what == net::BrokerWhat::List && askBack.ask == 41 && askBack.query.text == "bless" &&
              askBack.query.sevenUp && askBack.query.sort == sim::BrokerQuery::Sort::Seller &&
              askBack.query.page == 3 && askBack.query.sellerName == "Elowen" && askBack.fromVault && askBack.slot == 17 && askBack.seen.refinement == 7 &&
              askBack.price == 3400000 && askBack.hours == 48,
          "a BrokerAsk crosses the wire whole");
    net::BrokerView viewBack;
    check(net::take(bytes, kind, body) == 1 && kind == net::Kind::BrokerView && net::parse(body, viewBack) &&
              viewBack.answered == 41 && viewBack.said == view.said && viewBack.rows.size() == 1 &&
              viewBack.rows[0].tiers.size() == 2 && viewBack.rows[0].tiers[1].seller == "Maeve" &&
              viewBack.rows[0].count == 37 && viewBack.total == 30 && viewBack.page == 2 &&
              viewBack.mine.size() == 1 && viewBack.accountCount == 6 && viewBack.soldLately.size() == 1 &&
              viewBack.box.size() == 1 && viewBack.box[0].zen == 6840000 && viewBack.onSale == 37,
          "and a BrokerView");
}

}  // namespace

void testBroker(const content::Tables& tables, void (*checkOf)(bool, const char*),
                void (*checkEqualOf)(long long, long long, const char*)) {
    check = checkOf;
    checkEqual = checkEqualOf;
    std::printf("broker\n");
    testBrokerRules(tables);
    testBrokerRealm(tables);
    testBrokerWire();
}
