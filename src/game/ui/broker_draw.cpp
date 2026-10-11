// The Broker's window drawn (game/ui/broker.h): the frame and its three tabs, every pane in the
// controls' own Sanctuary pieces (game/ui/controls.h) and nothing of its own colour but the gold
// of a chosen row, the user's for a chosen row since the travel window (2026-09-30).
#include "game/ui/broker.h"
#include "game/ui/broker_layout.h"

#include <algorithm>
#include <cmath>

#include "game/play.h"
#include "game/ui/controls.h"
#include "game/ui/describe.h"
#include "game/ui/panel.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"
#include "sim/broker.h"
#include "sim/vault.h"

namespace mu::game {

namespace {

using gfx::Box;
using Hit = Broker::Hit;
using namespace broker_layout;

constexpr uint32_t kGold = gfx::rgba(0.851f, 0.706f, 0.373f);
constexpr uint32_t kGoldWash = gfx::rgba(0.851f, 0.706f, 0.373f, 0.13f);
constexpr uint32_t kGoldRim = gfx::rgba(0.851f, 0.706f, 0.373f, 0.55f);
constexpr uint32_t kHover = gfx::rgba(1.0f, 0.94f, 0.82f, 0.04f);
constexpr uint32_t kHair = gfx::rgba(0.0f, 0.0f, 0.0f, 0.55f);
constexpr uint32_t kOnline = gfx::rgba(0.498f, 0.753f, 0.416f);
constexpr uint32_t kOffline = gfx::rgba(0.29f, 0.27f, 0.235f);

const char* const kKinds[] = {"Swords and axes", "Bows", "Staves", "Shields", "Armour", "Rings and pendants",
                              "Jewels", "Runes", "Wings", "Pets", "Event tickets"};
const char* const kFlags[] = {"Usable by me", "Excellent", "+7 and up"};

std::string hoursLeft(int32_t seconds) {
    const int hours = seconds / 3600;
    if (hours >= 1) return std::to_string(hours) + " h";
    return std::to_string(std::max(1, seconds / 60)) + " min";
}

std::string ago(int64_t seconds) {
    if (seconds < 3600) return std::to_string(std::max<int64_t>(1, seconds / 60)) + " min ago";
    if (seconds < 86400) return std::to_string(seconds / 3600) + " h ago";
    const int64_t days = seconds / 86400;
    return days == 1 ? "yesterday" : std::to_string(days) + " days ago";
}

std::string nameOf(const content::Tables& tables, const sim::Held& what) {
    if (what.empty() || size_t(what.item) >= tables.items.size()) return "";
    const std::string& label = tables.items[size_t(what.item)].label;
    return what.refinement > 0 ? label + " +" + std::to_string(what.refinement) : label;
}

uint32_t inkOf(const content::Tables& tables, const sim::Held& what) {
    if (what.empty() || size_t(what.item) >= tables.items.size()) return style::kBone;
    return tip::colourOf(qualityOf(tables.items[size_t(what.item)], what));
}

// One line of what a thing carries, short: the row's sub line.
std::string traits(const sim::Held& what) {
    std::string out;
    const auto add = [&](const std::string& s) { out += (out.empty() ? "" : " · ") + s; };
    if (what.skill) add("Skill");
    if (what.luck) add("Luck");
    if (what.option > 0) add("Option +" + std::to_string(what.option * 4));
    if (what.excellent != 0) add("Excellent");
    if (what.sockets > 0) add(std::to_string(what.sockets) + (what.sockets == 1 ? " socket" : " sockets"));
    return out.empty() ? "Plain" : out;
}

// The watch star, MU has none: a pentagon and its five points, the canvas drawing convex shapes.
void star(gfx::Canvas& canvas, float cx, float cy, float r, uint32_t ink) {
    constexpr float kPi = 3.14159265f;
    float core[10];
    for (int i = 0; i < 5; ++i) {
        const float a = -kPi * 0.5f + kPi * 0.2f + float(i) * kPi * 0.4f;
        core[i * 2] = cx + std::cos(a) * r * 0.42f;
        core[i * 2 + 1] = cy + std::sin(a) * r * 0.42f;
    }
    canvas.polygon(nullptr, core, nullptr, 5, ink);
    for (int i = 0; i < 5; ++i) {
        const float a = -kPi * 0.5f + float(i) * kPi * 0.4f;
        const int left = (i + 4) % 5;
        const float point[6] = {cx + std::cos(a) * r, cy + std::sin(a) * r, core[i * 2], core[i * 2 + 1],
                                core[left * 2], core[left * 2 + 1]};
        canvas.polygon(nullptr, point, nullptr, 3, ink);
    }
}

const char* whyNot(sim::BrokerNo no) {
    switch (no) {
        case sim::BrokerNo::Bound: return "Bound to you";
        case sim::BrokerNo::Quest: return "A quest's";
        case sim::BrokerNo::Kind: return "Not sold here";
        case sim::BrokerNo::Worn: return "Repair it first";
        default: return "Not to be listed";
    }
}

// A row of the item card as one line of the market's card: its words in its own ink.
void sheetLines(const tip::Sheet& sheet, std::vector<std::pair<std::string, uint32_t>>* out) {
    for (const tip::Section& section : sheet.sections) {
        for (const tip::Row& row : section.rows) {
            std::string text;
            uint32_t ink = style::kBone;
            if (!row.free.empty()) {
                text = (row.keyword.empty() ? "" : row.keyword + " ") + row.free + (row.tail.empty() ? "" : " " + row.tail);
                ink = tip::colourOf(row.freeTone);
            } else {
                text = row.label;
                for (const tip::Value& v : row.values) text += (text.empty() ? "" : " ") + v.text;
                if (!row.values.empty()) ink = tip::colourOf(row.values.front().tone);
            }
            if (!text.empty()) out->push_back({text, ink});
        }
    }
}

}  // namespace

void Broker::rebuild(Play& play, Stage* stage) {
    canvas_.clear();
    tip_.clear();
    const sim::Realm& realm = play.realm();
    const content::Tables& tables = *realm.tables();
    const float k = panel::scale(), u = tip::unit();
    const float x = x_, y = y_;
    const auto P = [&](const Box& b) { return panel::scaled(x, y, b); };
    const auto S = [&](float px) { return px * u; };
    const auto state = [&](Hit hit, int index) {
        controls::State s;
        for (size_t i = 0; i < spots_.size(); ++i) {
            if (spots_[i].hit != hit || spots_[i].index != index) continue;
            s.lift = int(i) == hovered_ ? 1.0f : 0.0f;
            s.held = int(i) == pressed_ && int(i) == hovered_;
        }
        return s;
    };
    const auto spotOf = [&](Hit hit, int index) -> const Spot* {
        for (const Spot& s : spots_) {
            if (s.hit == hit && s.index == index) return &s;
        }
        return nullptr;
    };
    const auto text = [&](float ux, float baselineU, float size, uint32_t ink, const std::string& s) {
        return controls::label(canvas_, x + ux * k, y + baselineU * k, S(size), ink, s);
    };
    const auto right = [&](float ux, float baselineU, float size, uint32_t ink, const std::string& s) {
        return controls::ranged(canvas_, x + ux * k, y + baselineU * k, S(size), ink, s);
    };
    const auto chosenWash = [&](const Box& b) {
        canvas_.rect(P(b), kGoldWash);
        canvas_.outline(P(b), std::max(1.0f, u), kGoldRim);
    };
    std::vector<Standing> standing;
    const auto picture = [&](const Box& b, const sim::Held& what) {
        controls::cell(canvas_, P(b), controls::Cell::Rest, u);
        if (!what.empty()) standing.push_back({what.item, b.grown(-2.0f), what.refinement, false, what.excellent != 0});
    };
    const auto dot = [&](float ux, float midU, bool on) {
        const float r = std::max(2.0f, 3.0f * u);
        canvas_.rect({x + ux * k, y + midU * k - r, r * 2.0f, r * 2.0f}, on ? kOnline : kOffline);
    };

    // ---- the frame, the tabs, his Zen ----------------------------------------------------------
    controls::frame(canvas_, P({0.0f, 0.0f, kWide, kTall}), u, "Market", nullptr, panel::kHeadBand * k);
    if (const Spot* close = spotOf(Hit::Close, 0)) {
        const Box socket = P(close->box);
        const float side = std::round(std::min(style::kSmallSquare * u, socket.h));
        controls::square(canvas_, {std::round(socket.right() - side), std::round(socket.midY() - side * 0.5f), side, side},
                         controls::Glyph::Close, state(Hit::Close, 0), u);
    }
    const char* tabs[3] = {"Buy", "Sell", "My listings"};
    for (int t = 0; t < 3; ++t) {
        if (const Spot* s = spotOf(Hit::Tab, t)) controls::tab(canvas_, P(s->box), tabs[t], int(tab_) == t, state(Hit::Tab, t), u);
    }
    if (const Spot* pill = spotOf(Hit::Collect, 1)) {
        int64_t zen = 0;
        for (const net::BrokerParcel& p : mine_.box) zen += p.zen;
        const std::string word = zen > 0 ? "Collect " + panel::commas(zen) : "Collect " + std::to_string(mine_.box.size());
        controls::button(canvas_, P(pill->box), word, controls::Kind::Quiet, state(Hit::Collect, 1), u, S(12.0f));
    }
    right(kWide - kPad, 51.0f, 15.0f, kGold, panel::commas(realm.money()) + " Zen");
    right(kWide - kPad, 60.0f, 10.5f, style::kAshInk, "+ " + panel::commas(realm.vault().zen()) + " in vault");
    if (saidFor_ > 0.0f && !said_.empty()) {
        text(kPad, kTall - 13.0f, 12.5f, saidBad_ ? style::kDanger : style::kLiveHi, said_);
    } else if (waiting_ != 0) {
        text(kPad, kTall - 13.0f, 12.5f, style::kAshInk, "The Broker is looking...");
    }

    const bool caret = std::fmod(clock_, 1.0) < 0.5;
    const int64_t funds = realm.money() + realm.vault().zen();

    // ---- Buy -----------------------------------------------------------------------------------
    if (tab_ == Tab::Buy) {
        if (const Spot* s = spotOf(Hit::Search, 0)) {
            controls::field(canvas_, P(s->box), query_.text, "Search", focus_ == Focus::Search, false,
                            focus_ == Focus::Search && caret, false, u);
        }
        controls::kicker(canvas_, x + (kRail.x + 4.0f) * k, y + 100.0f * k, "Category", u);
        int all = 0;
        const size_t kinds = size_t(sim::BrokerKind::Count);
        for (size_t i = 0; i < kinds && i < page_.kinds.size(); ++i) all += page_.kinds[i];
        const int watched = page_.kinds.size() > kinds ? page_.kinds[kinds] : 0;
        for (int kind = -2; kind < int(sim::BrokerKind::Count); ++kind) {
            const Spot* s = spotOf(Hit::Kind, kind);
            if (!s) continue;
            const bool on = kind == -2 ? query_.watchedOnly : !query_.watchedOnly && query_.kind == kind;
            if (on) canvas_.rect(P(s->box), kGoldWash);
            else if (state(Hit::Kind, kind).lift > 0.0f) canvas_.rect(P(s->box), kHover);
            const int count = kind == -1 ? all : kind == -2 ? watched
                              : (size_t(kind) < page_.kinds.size() ? page_.kinds[size_t(kind)] : 0);
            const uint32_t ink = on ? kGold : count > 0 ? style::kBone2 : style::kAshInk2;
            text(s->box.x + 4.0f, s->box.y + 10.5f, 12.0f, ink, kind == -1 ? "All" : kind == -2 ? "Watched" : kKinds[kind]);
            right(s->box.right() - 4.0f, s->box.y + 10.5f, 11.0f, on ? kGold : style::kAshInk, std::to_string(count));
        }
        controls::kicker(canvas_, x + 16.0f * k, y + 310.0f * k, "Show only", u);
        const bool flags[3] = {query_.usableBy >= 0, query_.excellentOnly, query_.sevenUp};
        for (int f = 0; f < 3; ++f) {
            const Spot* s = spotOf(Hit::Flag, f);
            if (!s) continue;
            const Box box{s->box.x + 4.0f, s->box.y + 3.0f, 8.0f, 8.0f};
            canvas_.rect(P(box), flags[f] ? kGold : style::kVoid);
            canvas_.outline(P(box), std::max(1.0f, u), flags[f] ? kGold : style::kIronLo);
            text(s->box.x + 18.0f, s->box.y + 10.5f, 12.0f, flags[f] ? style::kBoneHi : style::kBone2, kFlags[f]);
        }
        if (const Spot* s = spotOf(Hit::Clear, 0)) {
            controls::button(canvas_, P(s->box), "Clear filters", controls::Kind::Quiet, state(Hit::Clear, 0), u, S(12.0f));
        }

        // The list.
        controls::well(canvas_, P(kList), u);
        const char* heads[4] = {"Item", "Seller", "Left", "Price"};
        const sim::BrokerQuery::Sort sorts[4] = {sim::BrokerQuery::Sort::Name, sim::BrokerQuery::Sort::Seller,
                                                 sim::BrokerQuery::Sort::Left, sim::BrokerQuery::Sort::Price};
        for (int h = 0; h < 4; ++h) {
            const Spot* s = spotOf(Hit::Head, h);
            if (!s) continue;
            const bool on = query_.sort == sorts[h];
            const std::string word = std::string(heads[h]) + (on ? (query_.descending ? " v" : " ^") : "");
            if (h >= 2) right(s->box.right(), s->box.y + 11.5f, 11.0f, on ? kGold : style::kAshInk, word);
            else text(s->box.x, s->box.y + 11.5f, 11.0f, on ? kGold : style::kAshInk, word);
        }
        for (int r = 0; r < int(page_.rows.size()); ++r) {
            const Spot* s = spotOf(Hit::Row, r);
            if (!s) continue;
            const net::BrokerRow& row = page_.rows[size_t(r)];
            const Box b = s->box;
            canvas_.rect(P({b.x + 6.0f, b.bottom(), b.w - 12.0f, 0.6f}), kHair);
            if (row.id == chosen_) chosenWash(b);
            else if (state(Hit::Row, r).lift > 0.0f) canvas_.rect(P(b), kHover);
            picture({b.x + 6.0f, b.y + 2.0f, 20.0f, 20.0f}, row.what);
            const bool alike = (row.flags & net::BrokerRow::Alike) != 0;
            const float named = text(b.x + 32.0f, b.y + 10.5f, 13.5f, inkOf(tables, row.what), nameOf(tables, row.what));
            if (row.flags & net::BrokerRow::Watched) star(canvas_, x + (b.x + 38.0f) * k + named, y + (b.y + 6.5f) * k, 4.5f * u, kGold);
            text(b.x + 32.0f, b.y + 20.5f, 11.0f, style::kAshInk,
                 alike ? std::to_string(row.count) + " on sale" : traits(row.what));
            const bool own = (row.flags & net::BrokerRow::Own) != 0;
            if (!alike) {
                dot(b.x + 186.0f, b.y + 12.0f, (row.flags & net::BrokerRow::Online) != 0);
                text(b.x + 192.0f, b.y + 15.5f, 12.0f, own ? style::kAshInk : style::kBone2, own ? row.seller + " (you)" : row.seller);
                right(b.x + 266.0f, b.y + 15.5f, 12.0f, row.secondsLeft < 7200 ? style::kDanger : style::kAshInk,
                      hoursLeft(row.secondsLeft));
            } else {
                text(b.x + 192.0f, b.y + 15.5f, 12.0f, style::kAshInk, std::to_string(row.tiers.size()) + " sellers");
            }
            right(b.right() - 6.0f, b.y + (alike ? 11.0f : 15.5f), 13.5f, kGold, panel::commas(row.price));
            if (alike) right(b.right() - 6.0f, b.y + 21.0f, 10.0f, style::kAshInk, "each, cheapest");
        }
        if (page_.rows.empty()) {
            text(kList.x + 20.0f, kList.y + 60.0f, 13.0f, style::kAshInk,
                 query_.watchedOnly ? "You watch nothing on sale. Choose one and press Watch."
                 : query_.text.empty() && query_.kind < 0 ? "Nothing is on sale yet." : "Nothing on sale matches.");
        }
        const int pages = std::max(1, int((page_.total + 11) / 12));
        std::string count = std::to_string(page_.total) + (page_.total == 1 ? " listing" : " listings");
        if (!query_.sellerName.empty()) count += " sold by " + query_.sellerName;
        text(kList.x + 8.0f, kList.bottom() - 6.0f, 11.0f, style::kAshInk, count);
        right(kList.right() - 50.0f, kList.bottom() - 6.0f, 11.0f, style::kBone2,
              std::to_string(page_.page + 1) + " / " + std::to_string(pages));
        if (const Spot* s = spotOf(Hit::Prev, 0)) controls::square(canvas_, P(s->box), controls::Glyph::Left, state(Hit::Prev, 0), u);
        if (const Spot* s = spotOf(Hit::Next, 0)) controls::square(canvas_, P(s->box), controls::Glyph::Right, state(Hit::Next, 0), u);

        // The card.
        const Box card = kCard;
        controls::well(canvas_, P(card), u);
        const net::BrokerRow* row = chosenRow();
        if (!row) {
            text(card.x + 12.0f, card.y + 150.0f, 12.5f, style::kAshInk, "Choose a listing to see it in full.");
            text(card.x + 12.0f, card.y + 164.0f, 12.5f, style::kAshInk, "Jewels fill from the cheapest first.");
        } else {
            const bool alike = (row->flags & net::BrokerRow::Alike) != 0;
            const bool own = (row->flags & net::BrokerRow::Own) != 0;
            picture({card.x + 8.0f, card.y + 8.0f, 44.0f, 44.0f}, row->what);
            text(card.x + 58.0f, card.y + 22.0f, 15.0f, inkOf(tables, row->what), nameOf(tables, row->what));
            tip::Sheet sheet = describe(tables, row->what, realm.wearer(), realm.satchel());
            text(card.x + 58.0f, card.y + 36.0f, 10.5f, style::kAshInk, sheet.base);
            if (const Spot* s = spotOf(Hit::Watch, 0)) {
                const bool on = (row->flags & net::BrokerRow::Watched) != 0;
                const Box b = s->box;
                canvas_.rect(P(b), on ? kGoldWash : state(Hit::Watch, 0).lift > 0.0f ? kHover : style::kVoid);
                canvas_.outline(P(b), std::max(1.0f, u), on ? kGoldRim : style::kIronLo);
                star(canvas_, x + (b.x + 9.0f) * k, y + b.midY() * k, 4.5f * u, on ? kGold : style::kAshInk);
                text(b.x + 17.0f, b.y + 11.0f, 11.0f, on ? kGold : style::kBone2, on ? "Watching" : "Watch");
            }
            if (!alike) {
                dot(card.x + 8.0f, card.y + 70.0f, (row->flags & net::BrokerRow::Online) != 0);
                const float w = text(card.x + 15.0f, card.y + 74.0f, 12.0f, style::kAshInk, "Sold by ");
                text(card.x + 15.0f + w / k, card.y + 74.0f, 12.0f, state(Hit::Seller, 0).lift > 0.0f ? style::kBoneHi : kGold,
                     own ? row->seller + " (you)" : row->seller);
                text(card.x + 8.0f, card.y + 88.0f, 11.0f, row->secondsLeft < 7200 ? style::kDanger : style::kAshInk,
                     ((row->flags & net::BrokerRow::Online) != 0 ? "online · " : "offline · ") + hoursLeft(row->secondsLeft) + " left");
                if (const Spot* s = spotOf(Hit::Whisper, 0)) {
                    controls::button(canvas_, P(s->box), "Whisper", controls::Kind::Quiet, state(Hit::Whisper, 0), u, S(11.0f));
                }
            }
            controls::rule(canvas_, x + (card.x + 8.0f) * k, y + (card.y + 96.0f) * k, (card.w - 16.0f) * k, u);
            float ly = card.y + 110.0f;
            if (alike) {
                int left = qty_;
                for (const net::BrokerRow::Tier& t : row->tiers) {
                    if (ly > card.bottom() - 110.0f) break;
                    const int take = std::min(left, t.count);
                    left -= take;
                    const uint32_t ink = take > 0 ? style::kBoneHi : style::kAshInk;
                    text(card.x + 8.0f, ly, 12.0f, ink, std::to_string(t.count) + " at");
                    text(card.x + 40.0f, ly, 12.0f, ink, panel::commas(t.price));
                    text(card.x + 98.0f, ly, 11.0f, style::kAshInk, t.seller);
                    if (take > 0) right(card.right() - 8.0f, ly, 12.0f, kGold, "+" + std::to_string(take));
                    ly += 14.0f;
                }
                if (const Spot* s = spotOf(Hit::Qty, -1)) controls::square(canvas_, P(s->box), controls::Glyph::Left, state(Hit::Qty, -1), u);
                if (const Spot* s = spotOf(Hit::Qty, 1)) controls::square(canvas_, P(s->box), controls::Glyph::Right, state(Hit::Qty, 1), u);
                text(card.x + 36.0f, card.bottom() - 77.0f, 15.0f, style::kBoneHi, std::to_string(qty_));
                if (const Spot* s = spotOf(Hit::MostQty, 0)) {
                    controls::button(canvas_, P(s->box), "Most I can afford", controls::Kind::Quiet, state(Hit::MostQty, 0), u, S(11.0f));
                }
            } else {
                std::vector<std::pair<std::string, uint32_t>> lines;
                sheetLines(sheet, &lines);
                for (const auto& [line, ink] : lines) {
                    if (ly > card.bottom() - 96.0f) break;
                    text(card.x + 8.0f, ly, 12.0f, ink, line);
                    ly += 13.5f;
                }
            }
            // What it will cost him, and where the thing goes.
            int64_t cost = row->price;
            if (alike) {
                cost = 0;
                int left = qty_;
                for (const net::BrokerRow::Tier& t : row->tiers) {
                    const int take = std::min(left, t.count);
                    cost += int64_t(take) * t.price;
                    left -= take;
                }
            }
            const content::ItemRow& shape = tables.items[size_t(row->what.item)];
            const bool fits = realm.satchel().free(tables, shape.width, shape.height) >= 0;
            text(card.x + 8.0f, card.bottom() - 50.0f, 11.0f, fits ? style::kLive : style::kDanger,
                 fits ? "Goes straight to your bag" : "Bag full: it waits in Collect");
            text(card.x + 8.0f, card.bottom() - 38.0f, 11.0f, style::kAshInk, alike ? "Total" : "Buyout");
            right(card.right() - 8.0f, card.bottom() - 37.0f, 17.0f, kGold, panel::commas(cost));
            if (const Spot* s = spotOf(Hit::Buy, 0)) {
                controls::State st = state(Hit::Buy, 0);
                st.off = waiting_ != 0 || cost > funds;
                const std::string word = cost > funds ? "Not enough Zen" : confirm_ ? "Confirm " + panel::commas(cost) : "Buy";
                controls::button(canvas_, P(s->box), word, controls::Kind::Primary, st, u);
            } else if (own) {
                text(card.x + 8.0f, card.bottom() - 14.0f, 12.0f, style::kAshInk, "Your own account's: not yours to buy.");
            }
        }
    }

    // ---- Sell ----------------------------------------------------------------------------------
    if (tab_ == Tab::Sell) {
        if (const Spot* s = spotOf(Hit::Source, 0)) {
            controls::toggle(canvas_, P({s->box.x, s->box.y, kThings.w, s->box.h}), "Bag", "Vault", fromVault_, u);
        }
        controls::well(canvas_, P({kThings.x, kThings.y + 24.0f, kThings.w, kThings.h - 24.0f}), u);
        for (const Spot& s : spots_) {
            if (s.hit != Hit::Thing) continue;
            const Thing& t = things_[size_t(s.index)];
            const bool ok = t.no == sim::BrokerNo::Yes;
            if (t.slot == sellSlot_) chosenWash(s.box);
            else if (ok && state(Hit::Thing, s.index).lift > 0.0f) canvas_.rect(P(s.box), kHover);
            // No picture: the user wants the list words only, the picture large once a thing is chosen.
            text(s.box.x + 8.0f, s.box.y + 9.5f, 12.0f, ok ? inkOf(tables, t.what) : style::kAshInk2, nameOf(tables, t.what));
            text(s.box.x + 8.0f, s.box.y + 18.5f, 10.0f, ok ? style::kAshInk : style::kAshInk2, ok ? traits(t.what) : whyNot(t.no));
        }
        if (things_.empty()) {
            text(kThings.x + 10.0f, kThings.y + 50.0f, 12.0f, style::kAshInk, fromVault_ ? "Your vault is empty." : "Your bag is empty.");
        }
        if (const Spot* s = spotOf(Hit::ThingPrev, 0)) controls::square(canvas_, P(s->box), controls::Glyph::Left, state(Hit::ThingPrev, 0), u);
        if (const Spot* s = spotOf(Hit::ThingNext, 0)) controls::square(canvas_, P(s->box), controls::Glyph::Right, state(Hit::ThingNext, 0), u);

        const Box thing = kThing;
        if (sellSlot_ < 0 || sellSeen_.empty()) {
            text(thing.x + 20.0f, thing.y + 120.0f, 13.0f, style::kAshInk, "Choose a thing from your bag or vault,");
            text(thing.x + 20.0f, thing.y + 135.0f, 13.0f, style::kAshInk, "or drag one here from your bag.");
        } else {
            controls::well(canvas_, P({thing.x, thing.y, thing.w, 96.0f}), u);
            picture({thing.x + 8.0f, thing.y + 8.0f, 80.0f, 80.0f}, sellSeen_);
            text(thing.x + 96.0f, thing.y + 34.0f, 15.0f, inkOf(tables, sellSeen_), nameOf(tables, sellSeen_));
            const tip::Sheet sheet = describe(tables, sellSeen_, realm.wearer(), realm.satchel());
            text(thing.x + 96.0f, thing.y + 50.0f, 10.5f, style::kAshInk, sheet.base);
            text(thing.x + 96.0f, thing.y + 64.0f, 10.5f, style::kAshInk, fromVault_ ? "From your vault" : "From your bag");
            const bool quoted = quote_.item == sellSeen_.item;
            const int64_t typical = quoted && !quote_.sales.empty() ? quote_.sales.front().price : 0;
            const std::string cells[3][2] = {
                {"Cheapest now", quoted && quote_.cheapest > 0 ? panel::commas(quote_.cheapest) : "None"},
                {"Last sold for", typical > 0 ? panel::commas(typical) : "Never sold"},
                {"On sale now", quoted ? std::to_string(quote_.onSale) : "..."}};
            for (int c = 0; c < 3; ++c) {
                const Box cell{thing.x + float(c) * (thing.w / 3.0f), thing.y + 104.0f, thing.w / 3.0f - 4.0f, 34.0f};
                controls::well(canvas_, P(cell), u);
                text(cell.x + 6.0f, cell.y + 12.0f, 10.5f, style::kAshInk, cells[c][0]);
                text(cell.x + 6.0f, cell.y + 27.0f, 14.0f, style::kBoneHi, cells[c][1]);
            }
            controls::kicker(canvas_, x + (thing.x + 2.0f) * k, y + (thing.y + 156.0f) * k, "Last sales", u);
            float ly = thing.y + 172.0f;
            for (size_t i = 0; quoted && i < quote_.sales.size() && i < 8; ++i) {
                const sim::Sale& sale = quote_.sales[i];
                text(thing.x + 4.0f, ly, 12.0f, style::kBone2, ago(std::max<int64_t>(0, realm.wallClock() - sale.at)));
                right(thing.right() - 4.0f, ly, 12.0f, style::kBoneHi, panel::commas(sale.price));
                canvas_.rect(P({thing.x, ly + 4.0f, thing.w, 0.6f}), kHair);
                ly += 17.0f;
            }
            if (quoted && quote_.sales.empty()) {
                text(thing.x + 4.0f, ly, 12.0f, style::kAshInk, "Never sold here yet.");
            }

            // The price.
            const Box price = kPrice;
            controls::kicker(canvas_, x + (price.x + 10.0f) * k, y + (price.y + 14.0f) * k, "Buyout price", u);
            if (const Spot* s = spotOf(Hit::Price, 0)) {
                const std::string shown = priceText_.empty() ? "" : panel::commas(priceTyped());
                controls::field(canvas_, P(s->box), shown, "0", focus_ == Focus::Price, false, focus_ == Focus::Price && caret, false, u);
            }
            const char* quick[3] = {"Cheapest", "Last sold", "5% under"};
            for (int q = 0; q < 3; ++q) {
                if (const Spot* s = spotOf(Hit::Quick, q)) {
                    controls::button(canvas_, P(s->box), quick[q], controls::Kind::Quiet, state(Hit::Quick, q), u, S(11.0f));
                }
            }
            const int64_t asked = priceTyped();
            const int64_t floor = std::max<int64_t>(1, sim::npcValue(tables, sellSeen_));
            uint32_t guardInk = style::kLive;
            std::string guard;
            if (asked <= 0) {
                guard = "Set a price.", guardInk = style::kAshInk;
            } else if (asked < floor) {
                guard = "Under what a merchant pays: " + panel::commas(floor) + ".", guardInk = style::kDanger;
            } else if (asked > sim::kBrokerMostPrice) {
                guard = "The most is 2,000,000,000.", guardInk = style::kDanger;
            } else if (typical > 0 && asked * 10 < typical * 7) {
                guard = std::to_string(100 - asked * 100 / typical) + "% under the last sale.", guardInk = style::kDanger;
            } else if (quoted && quote_.cheapest > 0 && asked * 100 > quote_.cheapest * 115) {
                guard = std::to_string(asked * 100 / quote_.cheapest - 100) + "% over the cheapest: it may not sell.";
                guardInk = gfx::rgba(0.851f, 0.639f, 0.373f);
            } else if (quoted && (quote_.cheapest == 0 || asked <= quote_.cheapest)) {
                guard = "The cheapest on the market.";
            } else {
                guard = "Close to the market's price.";
            }
            text(price.x + 10.0f, price.y + 84.0f, 11.0f, guardInk, guard);
            controls::kicker(canvas_, x + (price.x + 10.0f) * k, y + (price.y + 104.0f) * k, "Listed for", u);
            for (int h = 0; h < 3; ++h) {
                if (const Spot* s = spotOf(Hit::Hours, h)) {
                    controls::button(canvas_, P(s->box), std::to_string(sim::kBrokerHours[h]) + " hours",
                                     h == hours_ ? controls::Kind::Secondary : controls::Kind::Quiet, state(Hit::Hours, h), u, S(11.5f));
                    if (h == hours_) canvas_.rect(P({s->box.x + 4.0f, s->box.bottom() - 2.0f, s->box.w - 8.0f, 1.5f}), kGold);
                }
            }
            text(price.x + 10.0f, price.bottom() - 82.0f, 11.0f, style::kAshInk, "Listing is free. You collect");
            text(price.x + 10.0f, price.bottom() - 70.0f, 11.0f, style::kAshInk, "the whole price if it sells:");
            right(price.right() - 10.0f, price.bottom() - 52.0f, 18.0f, kGold, panel::commas(std::max<int64_t>(0, asked)));
            text(price.x + 10.0f, price.bottom() - 40.0f, 10.5f, style::kAshInk,
                 std::to_string(mine_.accountCount) + " of 20 listings on your account");
            if (const Spot* s = spotOf(Hit::List, 0)) {
                controls::State st = state(Hit::List, 0);
                st.off = waiting_ != 0 || asked < floor || asked > sim::kBrokerMostPrice || mine_.accountCount >= sim::kBrokerListings;
                controls::button(canvas_, P(s->box), mine_.accountCount >= sim::kBrokerListings ? "Full" : "List",
                                 controls::Kind::Primary, st, u);
            }
        }
    }

    // ---- My listings ---------------------------------------------------------------------------
    if (tab_ == Tab::Mine) {
        const Box box = kBox;
        controls::well(canvas_, P({box.x, box.y, box.w, 150.0f}), u);
        controls::kicker(canvas_, x + (box.x + 10.0f) * k, y + (box.y + 16.0f) * k, "Waiting to collect", u);
        int64_t zen = 0;
        for (const net::BrokerParcel& p : mine_.box) zen += p.zen;
        text(box.x + 10.0f, box.y + 40.0f, 20.0f, kGold, panel::commas(zen) + " Zen");
        text(box.x + 10.0f, box.y + 54.0f, 10.5f, style::kAshInk, "Kept 30 days, then gone.");
        float ly = box.y + 72.0f;
        int shown = 0;
        for (const net::BrokerParcel& p : mine_.box) {
            if (p.what.empty()) continue;
            if (shown == 3) {
                text(box.x + 10.0f, ly, 11.0f, style::kAshInk, "and more");
                break;
            }
            const char* why = p.why == uint8_t(sim::Parcel::Why::Cancelled) ? "Cancelled"
                              : p.why == uint8_t(sim::Parcel::Why::Bought) ? "Bought, bag full"
                                                                           : "Did not sell";
            text(box.x + 10.0f, ly, 11.5f, inkOf(tables, p.what), nameOf(tables, p.what));
            right(box.right() - 10.0f, ly, 10.0f, style::kAshInk,
                  std::string(why) + " · " + std::to_string(std::max(1, p.secondsLeft / 86400)) + " d");
            ly += 14.0f;
            ++shown;
        }
        if (mine_.box.empty()) text(box.x + 10.0f, box.y + 80.0f, 12.0f, style::kAshInk, "Nothing waits for you.");
        if (const Spot* s = spotOf(Hit::Collect, 0)) {
            controls::State st = state(Hit::Collect, 0);
            st.off = waiting_ != 0;
            controls::button(canvas_, P(s->box), "Collect all", controls::Kind::Primary, st, u);
        }
        controls::kicker(canvas_, x + (box.x + 4.0f) * k, y + (box.y + 168.0f) * k, "Sold lately", u);
        ly = box.y + 186.0f;
        for (const sim::Sale& sale : mine_.soldLately) {
            if (ly > box.bottom() - 4.0f) break;
            text(box.x + 4.0f, ly, 12.0f, inkOf(tables, sale.what), nameOf(tables, sale.what));
            right(box.right() - 4.0f, ly, 12.0f, kGold, "+" + panel::commas(sale.price));
            text(box.x + 4.0f, ly + 11.0f, 10.0f, style::kAshInk, ago(std::max<int64_t>(0, realm.wallClock() - sale.at)));
            ly += 26.0f;
        }
        if (mine_.soldLately.empty()) text(box.x + 4.0f, ly, 11.5f, style::kAshInk, "Nothing yet.");

        const Box list = kMine;
        controls::well(canvas_, P(list), u);
        text(list.x + 30.0f, list.y + 11.5f, 11.0f, style::kAshInk, "On sale");
        right(list.right() - 244.0f, list.y + 11.5f, 11.0f, style::kAshInk, "Left");
        right(list.right() - 112.0f, list.y + 11.5f, 11.0f, style::kAshInk, "Price");
        right(list.right() - 8.0f, list.y + 11.5f, 11.0f, style::kAshInk, std::to_string(mine_.accountCount) + " / 20");
        for (int r = 0; r < int(mine_.mine.size()) && r < 12; ++r) {
            const net::BrokerRow& row = mine_.mine[size_t(r)];
            const float ry = list.y + 18.0f + float(r) * 24.0f;
            canvas_.rect(P({list.x + 6.0f, ry + 23.0f, list.w - 12.0f, 0.6f}), kHair);
            picture({list.x + 6.0f, ry + 2.0f, 20.0f, 20.0f}, row.what);
            text(list.x + 32.0f, ry + 10.5f, 13.0f, inkOf(tables, row.what), nameOf(tables, row.what));
            text(list.x + 32.0f, ry + 20.5f, 10.5f, style::kAshInk, traits(row.what));
            right(list.right() - 244.0f, ry + 15.5f, 12.0f, row.secondsLeft < 7200 ? style::kDanger : style::kAshInk,
                  hoursLeft(row.secondsLeft));
            if (row.id == editing_) {
                if (const Spot* s = spotOf(Hit::LowerField, r)) {
                    controls::field(canvas_, P(s->box), lowerText_.empty() ? "" : panel::commas(std::stoll("0" + lowerText_)),
                                    "", focus_ == Focus::Lower, false, focus_ == Focus::Lower && caret, false, u);
                }
                if (const Spot* s = spotOf(Hit::Save, r)) controls::button(canvas_, P(s->box), "Save", controls::Kind::Primary, state(Hit::Save, r), u, S(11.5f));
                if (const Spot* s = spotOf(Hit::Back, r)) controls::button(canvas_, P(s->box), "Back", controls::Kind::Quiet, state(Hit::Back, r), u, S(11.5f));
            } else {
                right(list.right() - 112.0f, ry + 15.5f, 13.5f, kGold, panel::commas(row.price));
                if (const Spot* s = spotOf(Hit::Lower, r)) controls::button(canvas_, P(s->box), "Lower", controls::Kind::Quiet, state(Hit::Lower, r), u, S(11.5f));
                if (const Spot* s = spotOf(Hit::Cancel, r)) controls::button(canvas_, P(s->box), "Cancel", controls::Kind::Quiet, state(Hit::Cancel, r), u, S(11.5f));
            }
        }
        if (mine_.mine.empty()) {
            text(list.x + 30.0f, list.y + 60.0f, 13.0f, style::kAshInk, "Nothing on sale. List something from the Sell tab.");
        }
        text(list.x + 8.0f, list.bottom() - 6.0f, 10.5f, style::kAshInk,
             "A price may be lowered for nothing; to raise it, cancel and list it again.");
    }

    // The item pictures, one pass over the window, as the vault's are.
    if (stage) {
        stage->stand(standing, kWide, kTall);
        const gfx::Art art = stage->picture();
        if (art.valid()) canvas_.image(art, P({0.0f, 0.0f, kWide, kTall}));
    }

    // The item card over the row under the pointer: the bag's own card.
    if (hovered_ >= 0 && size_t(hovered_) < spots_.size() && pressed_ < 0) {
        const Spot& s = spots_[size_t(hovered_)];
        sim::Held what;
        if (s.hit == Hit::Row && size_t(s.index) < page_.rows.size()) what = page_.rows[size_t(s.index)].what;
        if (s.hit == Hit::Thing && size_t(s.index) < things_.size()) what = things_[size_t(s.index)].what;
        if (!what.empty()) {
            tip::Sheet sheet = describe(tables, what, realm.wearer(), realm.satchel());
            if (tipStage_) tip::stand(*tipStage_, what.item, what.refinement, sheet);
            tip::draw(tip_, sheet, P(s.box), screenW_, screenH_);
        }
    }
}

}  // namespace mu::game
