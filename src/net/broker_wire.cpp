#include "net/broker_wire.h"

#include <algorithm>

#include "net/wire.h"

namespace mu::net {

namespace {

// wire.cpp's frame and its little-endian readers, which are that file's own: a frame is its
// length, its kind and its body.
struct Out {
    std::vector<uint8_t>& bytes;
    void u8(uint8_t v) { bytes.push_back(v); }
    void u16(uint16_t v) {
        for (int i = 0; i < 2; ++i) bytes.push_back(uint8_t(v >> (8 * i)));
    }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) bytes.push_back(uint8_t(v >> (8 * i)));
    }
    void u64(uint64_t v) {
        for (int i = 0; i < 8; ++i) bytes.push_back(uint8_t(v >> (8 * i)));
    }
    void i32(int32_t v) { u32(uint32_t(v)); }
    void i64(int64_t v) { u64(uint64_t(v)); }
    void str(const std::string& s) {
        const size_t n = s.size() < 255 ? s.size() : 255;
        u8(uint8_t(n));
        bytes.insert(bytes.end(), s.begin(), s.begin() + long(n));
    }
    void held(const sim::Held& h) {
        sim::Command packed;
        sim::packHeld(h, packed);
        i32(packed.a);
        i32(packed.b);
        i32(packed.c);
        i32(packed.d);
        u32(packed.target);
    }
    void sales(const std::vector<sim::Sale>& list) {
        u8(uint8_t(std::min<size_t>(list.size(), 255)));
        for (size_t i = 0; i < list.size() && i < 255; ++i) {
            held(list[i].what);
            i64(list[i].price);
            i64(list[i].at);
        }
    }
};

struct In {
    const std::vector<uint8_t>& bytes;
    size_t at = 0;
    bool ok = true;
    bool need(size_t n) {
        if (at + n > bytes.size()) ok = false;
        return ok;
    }
    uint8_t u8() { return need(1) ? bytes[at++] : 0; }
    uint16_t u16() {
        if (!need(2)) return 0;
        const uint16_t v = uint16_t(bytes[at] | bytes[at + 1] << 8);
        at += 2;
        return v;
    }
    uint32_t u32() {
        if (!need(4)) return 0;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= uint32_t(bytes[at++]) << (8 * i);
        return v;
    }
    uint64_t u64() {
        if (!need(8)) return 0;
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= uint64_t(bytes[at++]) << (8 * i);
        return v;
    }
    int32_t i32() { return int32_t(u32()); }
    int64_t i64() { return int64_t(u64()); }
    std::string str() {
        const size_t n = u8();
        if (!need(n)) return {};
        std::string s(bytes.begin() + long(at), bytes.begin() + long(at + n));
        at += n;
        return s;
    }
    sim::Held held() {
        sim::Command packed;
        packed.a = i32();
        packed.b = i32();
        packed.c = i32();
        packed.d = i32();
        packed.target = u32();
        return sim::unpackHeld(packed);
    }
    std::vector<sim::Sale> sales() {
        std::vector<sim::Sale> list(u8());
        for (sim::Sale& one : list) {
            one.what = held();
            one.price = i64();
            one.at = i64();
        }
        return list;
    }
    bool done() const { return ok && at == bytes.size(); }
};

template <class Write>
void frame(std::vector<uint8_t>& out, Kind kind, Write write) {
    const size_t start = out.size();
    Out o{out};
    o.u32(0);
    o.u8(uint8_t(kind));
    write(o);
    const uint32_t length = uint32_t(out.size() - start - 4);
    for (int i = 0; i < 4; ++i) out[start + size_t(i)] = uint8_t(length >> (8 * i));
}

void putRow(Out& o, const BrokerRow& r) {
    o.u32(r.id);
    o.held(r.what);
    o.i64(r.price);
    o.i32(r.secondsLeft);
    o.str(r.seller);
    o.u8(r.flags);
    o.i32(r.count);
    o.i64(r.lastSale);
    o.u8(uint8_t(std::min<size_t>(r.tiers.size(), 255)));
    for (size_t i = 0; i < r.tiers.size() && i < 255; ++i) {
        o.i64(r.tiers[i].price);
        o.i32(r.tiers[i].count);
        o.str(r.tiers[i].seller);
    }
}

BrokerRow takeRow(In& in) {
    BrokerRow r;
    r.id = in.u32();
    r.what = in.held();
    r.price = in.i64();
    r.secondsLeft = in.i32();
    r.seller = in.str();
    r.flags = in.u8();
    r.count = in.i32();
    r.lastSale = in.i64();
    r.tiers.resize(in.u8());
    for (BrokerRow::Tier& t : r.tiers) {
        t.price = in.i64();
        t.count = in.i32();
        t.seller = in.str();
    }
    return r;
}

// A page is twelve rows and a man has twenty listings; more than this is not a frame of ours.
constexpr size_t kMostRows = 64;

}  // namespace

void put(std::vector<uint8_t>& out, const BrokerAsk& one) {
    frame(out, Kind::BrokerAsk, [&](Out& o) {
        o.u8(uint8_t(one.what));
        o.u32(one.ask);
        const sim::BrokerQuery& q = one.query;
        o.i32(q.kind);
        o.str(q.text);
        o.i32(q.usableBy);
        o.u8(uint8_t((q.excellentOnly ? 1 : 0) | (q.sevenUp ? 2 : 0) | (q.descending ? 4 : 0) | (q.watchedOnly ? 8 : 0) |
                     (q.luckOnly ? 16 : 0) | (q.socketOnly ? 32 : 0)));
        o.str(q.sellerName);
        o.u8(uint8_t(q.sort));
        o.u16(uint16_t(std::max(0, q.page)));
        o.i64(q.minPrice);
        o.i64(q.maxPrice);
        o.i32(one.item);
        o.u8(one.fromVault ? 1 : 0);
        o.i32(one.slot);
        o.held(one.seen);
        o.i64(one.price);
        o.u8(one.hours);
        o.u32(one.listing);
        o.i32(one.count);
    });
}

bool parse(const std::vector<uint8_t>& body, BrokerAsk& out) {
    In in{body};
    const uint8_t what = in.u8();
    if (what > uint8_t(BrokerWhat::Watch)) return false;
    out.what = BrokerWhat(what);
    out.ask = in.u32();
    sim::BrokerQuery& q = out.query;
    q.kind = in.i32();
    q.text = in.str();
    q.usableBy = in.i32();
    const uint8_t flags = in.u8();
    q.excellentOnly = (flags & 1) != 0;
    q.sevenUp = (flags & 2) != 0;
    q.descending = (flags & 4) != 0;
    q.watchedOnly = (flags & 8) != 0;
    q.luckOnly = (flags & 16) != 0;
    q.socketOnly = (flags & 32) != 0;
    q.sellerName = in.str();
    const uint8_t sort = in.u8();
    if (sort > uint8_t(sim::BrokerQuery::Sort::Newest)) return false;
    q.sort = sim::BrokerQuery::Sort(sort);
    q.page = in.u16();
    q.minPrice = in.i64();
    q.maxPrice = in.i64();
    out.item = in.i32();
    out.fromVault = in.u8() != 0;
    out.slot = in.i32();
    out.seen = in.held();
    out.price = in.i64();
    out.hours = in.u8();
    out.listing = in.u32();
    out.count = in.i32();
    return in.done();
}

void put(std::vector<uint8_t>& out, const BrokerView& one) {
    frame(out, Kind::BrokerView, [&](Out& o) {
        o.u32(one.answered);
        o.u8(one.no);
        o.str(one.said);
        o.u8(one.parts);
        if (one.parts & BrokerView::Page) {
            o.u32(one.total);
            o.u16(one.page);
            o.u8(uint8_t(std::min(one.rows.size(), kMostRows)));
            for (size_t i = 0; i < one.rows.size() && i < kMostRows; ++i) putRow(o, one.rows[i]);
            o.u8(uint8_t(std::min<size_t>(one.kinds.size(), 32)));
            for (size_t i = 0; i < one.kinds.size() && i < 32; ++i) o.u16(one.kinds[i]);
        }
        if (one.parts & BrokerView::Mine) {
            o.u8(uint8_t(std::min(one.mine.size(), kMostRows)));
            for (size_t i = 0; i < one.mine.size() && i < kMostRows; ++i) putRow(o, one.mine[i]);
            o.i32(one.accountCount);
            o.sales(one.soldLately);
        }
        if (one.parts & BrokerView::Box) {
            o.u8(uint8_t(std::min(one.box.size(), kMostRows)));
            for (size_t i = 0; i < one.box.size() && i < kMostRows; ++i) {
                o.held(one.box[i].what);
                o.i64(one.box[i].zen);
                o.i32(one.box[i].secondsLeft);
                o.u8(one.box[i].why);
            }
        }
        if (one.parts & BrokerView::Quoted) {
            o.i32(one.item);
            o.i64(one.cheapest);
            o.i32(one.onSale);
            o.sales(one.sales);
        }
    });
}

bool parse(const std::vector<uint8_t>& body, BrokerView& out) {
    In in{body};
    out = BrokerView{};
    out.answered = in.u32();
    out.no = in.u8();
    out.said = in.str();
    out.parts = in.u8();
    if (out.parts & BrokerView::Page) {
        out.total = in.u32();
        out.page = in.u16();
        const uint8_t n = in.u8();
        if (n > kMostRows) return false;
        for (uint8_t i = 0; i < n && in.ok; ++i) out.rows.push_back(takeRow(in));
        out.kinds.resize(in.u8());
        for (uint16_t& k : out.kinds) k = in.u16();
    }
    if (out.parts & BrokerView::Mine) {
        const uint8_t n = in.u8();
        if (n > kMostRows) return false;
        for (uint8_t i = 0; i < n && in.ok; ++i) out.mine.push_back(takeRow(in));
        out.accountCount = in.i32();
        out.soldLately = in.sales();
    }
    if (out.parts & BrokerView::Box) {
        const uint8_t n = in.u8();
        if (n > kMostRows) return false;
        for (uint8_t i = 0; i < n && in.ok; ++i) {
            BrokerParcel p;
            p.what = in.held();
            p.zen = in.i64();
            p.secondsLeft = in.i32();
            p.why = in.u8();
            out.box.push_back(p);
        }
    }
    if (out.parts & BrokerView::Quoted) {
        out.item = in.i32();
        out.cheapest = in.i64();
        out.onSale = in.i32();
        out.sales = in.sales();
    }
    return in.done();
}

}  // namespace mu::net
