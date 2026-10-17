// **An item named in the chat** (the user, 2026-10-11: "we need to allow bots to write on chat
// [item name] with working tooltips for item mention on chat"): WoW's item link, in a line's own
// text. A link is
//
//     {i:GROUP.NUMBER.PLUS.COUNT.FLAGS.OPTION.EXCELLENT.SOCKETS.P0.P1.P2.A0.A1.A2.WING|Label +7}
//
// -- the thing whole, as sim::Held keeps it, and its name as it was said. The chat draws it as
// "[Label +7]" in the thing's quality colour and hovers it into the item card. It is read by MU's
// group and number, never by a row's index, since each world's tables number their rows apart and a
// party's line crosses maps. Fields from the right that are 0 are left off; FLAGS is 1 skill, 2
// luck, 4 bound. All of it printable, so net::saidText passes it, and about 30 bytes and the name:
// a 200-byte line (net::kMostSaid) carries three or four.
//
// The server passes it on unread, as it passes every line: a link says what its sayer claims, and
// the trade window (sim/realm_deal.cpp) shows what is really offered.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "content/tables.h"
#include "sim/items.h"

namespace mu::sim {

// One link found in a line: where it stands in the raw text, the name to draw, and the thing --
// `held.item` -1 until resolve() finds its row in a world's tables.
struct ItemLink {
    size_t from = 0, to = 0;  // the raw bytes "{i:...}"
    std::string label;
    int32_t group = -1, number = -1;
    Held held;
};

// The name a link carries: the row's, and its plus.
inline std::string linkLabel(const content::ItemRow& row, const Held& held) {
    std::string label = itemLabel(row, held);
    if (held.refinement > 0) label += " +" + std::to_string(held.refinement);
    return label;
}

// The link for `held`, whose row is `row`.
inline std::string itemLink(const content::ItemRow& row, const Held& held) {
    const int64_t fields[] = {row.group,
                              row.number,
                              held.refinement,
                              held.durability,
                              (held.skill ? 1 : 0) | (held.luck ? 2 : 0) | (held.bound ? 4 : 0),
                              held.option,
                              held.excellent,
                              held.sockets,
                              held.powers[0],
                              held.powers[1],
                              held.powers[2],
                              held.affixes[0],
                              held.affixes[1],
                              held.affixes[2],
                              held.wing};
    size_t n = sizeof fields / sizeof fields[0];
    while (n > 3 && fields[n - 1] == 0) --n;
    std::string out = "{i:";
    for (size_t k = 0; k < n; ++k) {
        if (k) out += '.';
        out += std::to_string(fields[k]);
    }
    std::string label = linkLabel(row, held);
    // The name may hold neither the link's marks nor the chat's brackets.
    for (char& c : label) {
        if (c == '}' || c == '|' || c == '{' || c == '[' || c == ']') c = ' ';
    }
    return out + "|" + label + "}";
}

// Every link in `text`, in order. A "{i:" that does not close or does not read is left as text.
inline std::vector<ItemLink> findLinks(const std::string& text) {
    std::vector<ItemLink> out;
    size_t at = 0;
    while ((at = text.find("{i:", at)) != std::string::npos) {
        const size_t end = text.find('}', at);
        const size_t bar = text.find('|', at);
        if (end == std::string::npos) break;
        if (bar == std::string::npos || bar > end) {
            at += 3;
            continue;
        }
        std::vector<int64_t> fields;
        const char* p = text.c_str() + at + 3;
        const char* stop = text.c_str() + bar;
        bool ok = true;
        while (p < stop && ok) {
            char* next = nullptr;
            const long long v = std::strtoll(p, &next, 10);
            if (next == p) ok = false;
            fields.push_back(v);
            p = next;
            if (p < stop && *p == '.') ++p;
            else if (p < stop) ok = false;
        }
        if (!ok || fields.size() < 3 || bar + 1 >= end) {
            at += 3;
            continue;
        }
        fields.resize(15, 0);
        ItemLink link;
        link.from = at;
        link.to = end + 1;
        link.label = text.substr(bar + 1, end - bar - 1);
        link.group = int32_t(fields[0]);
        link.number = int32_t(fields[1]);
        Held& h = link.held;
        h.refinement = int16_t(fields[2]);
        h.durability = int16_t(fields[3]);
        h.skill = (fields[4] & 1) != 0;
        h.luck = (fields[4] & 2) != 0;
        h.bound = (fields[4] & 4) != 0;
        h.option = int8_t(fields[5]);
        h.excellent = uint8_t(fields[6]);
        h.sockets = uint8_t(fields[7]);
        for (int k = 0; k < 3; ++k) h.powers[k] = uint8_t(fields[size_t(8 + k)]);
        for (int k = 0; k < 3; ++k) h.affixes[k] = uint8_t(fields[size_t(11 + k)]);
        h.wing = uint8_t(fields[14]);
        out.push_back(link);
        at = end + 1;
    }
    return out;
}

// The row of a link's group and number in these tables, or -1.
inline int32_t linkRow(const content::Tables& tables, int32_t group, int32_t number) {
    for (size_t i = 0; i < tables.items.size(); ++i) {
        if (tables.items[i].group == group && tables.items[i].number == number) return int32_t(i);
    }
    return -1;
}

// `text` as it reads: each link as "[Label]".
inline std::string linkedText(const std::string& text) {
    std::string out;
    size_t at = 0;
    for (const ItemLink& link : findLinks(text)) {
        out += text.substr(at, link.from - at);
        out += "[" + link.label + "]";
        at = link.to;
    }
    return out + text.substr(at);
}

}  // namespace mu::sim
