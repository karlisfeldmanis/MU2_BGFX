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
constexpr uint32_t kOnline = gfx::rgba(0.498f, 0.753f, 0.416f);
constexpr uint32_t kOffline = gfx::rgba(0.29f, 0.27f, 0.235f);
// A prize's row: the market's gold, fainter than a chosen row's wash.
constexpr uint32_t kPrizeWash = gfx::rgba(0.851f, 0.706f, 0.373f, 0.09f);

const char* const kKinds[] = {"Swords and axes", "Bows", "Staves", "Shields", "Armour", "Rings and pendants",
                              "Jewels", "Runes", "Wings", "Pets", "Event tickets", "Orbs and scrolls"};
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
    const std::string label = sim::itemLabel(tables.items[size_t(what.item)], what);
    return what.refinement > 0 ? label + " +" + std::to_string(what.refinement) : label;
}

uint32_t inkOf(const content::Tables& tables, const sim::Held& what) {
    if (what.empty() || size_t(what.item) >= tables.items.size()) return style::kBone;
    return tip::colourOf(qualityOf(tables.items[size_t(what.item)], what));
}

// A super rare thing, lit in the market's gold wherever it is listed (the user, 2026-10-22:
// "we need to highlight super rare items like wings more"): wings of either level, and a rune or
// a ring at the top of the ladder, Legendary or Ancient.
bool prized(const content::Tables& tables, const sim::Held& what) {
    if (what.empty() || size_t(what.item) >= tables.items.size()) return false;
    const content::ItemRow& row = tables.items[size_t(what.item)];
    if (sim::anyWing(row)) return true;
    const tip::Tone tone = qualityOf(row, what);
    return tone == tip::Tone::RuneLegendary || tone == tip::Tone::RuneAncient;
}

// One line of what a thing carries, short, empty for a plain one. No "Skill": a weapon's skill
// comes from orbs here, never from the weapon (the user, 2026-10-12).
std::string traits(const sim::Held& what) {
    std::string out;
    const auto add = [&](const std::string& s) { out += (out.empty() ? "" : " · ") + s; };
    if (what.luck) add("Luck");
    if (what.option > 0) add("Option +" + std::to_string(what.option * 4));
    if (what.excellent != 0) add("Excellent");
    if (what.sockets > 0) add(std::to_string(what.sockets) + (what.sockets == 1 ? " socket" : " sockets"));
    return out;
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
    // A line of type centred on its capitals in a band `tall` units high from `top`.
    const auto mid = [&](float top, float tall, float size) {
        return (controls::middle(y + top * k, tall * k, S(size)) - y) / k;
    };
    // A rule one pixel thick across a run of the window.
    const auto hair = [&](float ux, float uy, float wide, uint32_t ink) {
        canvas_.rect({std::round(x + ux * k), std::round(y + uy * k), std::round(wide * k), std::max(1.0f, std::round(u))}, ink);
    };
    std::vector<Standing> standing;
    // The thing, market-style (the user, 2026-10-12: "item card is to detailed, and we need that
    // thumb is above item info and bigger", "i like styling but it has to be more on market
    // style"): its picture large in a framed block on top, turning ("lets use rotating items in
    // big thumbnail"), then its name and kind, then only what a buyer weighs, in the item card's
    // own colours: the headline figure, what it carries, who wears it and an unmet need.
    constexpr float kTurnSquare = 64.0f;
    const gfx::Art turned = stage ? stage->picture() : gfx::Art{};
    const auto thingCard = [&](const sim::Held& what, float ax, float ay, float aw, float pic, const std::string& kind,
                               float stop) {
        // One framed block: the picture on the left, its name and lines on the right (the user,
        // 2026-10-12: "thumb has to be left side and info right side", then "better overall
        // integration with thumb and info"). The picture stands on a dark floor that fades into
        // the block's own ash at its foot and glows there faintly in the thing's own colour, the
        // colour of its name beside it; a hairline parts the two; the words are centred on it.
        const tip::Sheet sheet = describe(tables, what, realm.wearer(), realm.satchel());
        const uint32_t ink = inkOf(tables, what);
        // A prize stands in a gold frame, its floor glowing twice as bright, in gold where its
        // name is white (a plain wing).
        const bool prize = prized(tables, what);
        const Box block{ax, ay, aw, pic + 8.0f};
        canvas_.rect(P(block), style::kAsh2);
        canvas_.outline(P(block), std::max(1.0f, u), prize ? kGoldRim : style::kIronDk);
        const Box well{ax + 4.0f, ay + 4.0f, pic, pic};
        const uint32_t floor = style::kVoid;
        const uint32_t lit = prize && ink == tip::colourOf(tip::Tone::White) ? kGold : ink;
        const uint32_t glow = (lit & 0x00FFFFFFu) | (uint32_t((prize ? 0.34f : 0.16f) * 255.0f) << 24), none = lit & 0x00FFFFFFu;
        canvas_.shade(P(well), floor, floor, style::kAsh1, style::kAsh1);
        canvas_.shade(P({well.x, well.y + pic * 0.55f, pic, pic * 0.45f}), none, none, glow, glow);
        canvas_.rect({std::round(x + (well.right() + 4.0f) * k), std::round(y + (ay + 10.0f) * k), std::max(1.0f, std::round(u)),
                      std::round((pic - 12.0f) * k)},
                     style::kIronDk);
        standing.push_back({what.item, {0.0f, 0.0f, kTurnSquare, kTurnSquare}, what.refinement, true, what.excellent != 0});
        if (turned.valid()) {
            // A sixteenth of the square kept clear, and a one-cell jewel smaller than a sword.
            const float side = (pic - 2.0f * std::round(pic * 0.06f)) * std::clamp(sheet.artScale, 0.62f, 1.0f);
            canvas_.image(turned, P({well.midX() - side * 0.5f, well.midY() - side * 0.5f, side, side}));
        }

        // The words first, wrapped to the column, then set: centred on the picture when they are
        // shorter than it, and on under the block at its full width when they are taller.
        struct Said {
            std::string left, value;
            uint32_t leftInk = 0, valueInk = 0;
            std::vector<const gfx::Art*> faces;  // the class line's faces, before its words
        };
        const float ix = well.right() + 12.0f, iw = block.right() - 8.0f - ix;
        constexpr float kFace = 14.0f;  // a class's face before the class line
        constexpr float kStep = 13.0f;
        // A faces line stands this much clear of the lines above and below it: the 14-unit face in
        // a 13-unit line touched "Excellent one-handed weapon" over it and "Damage" under it (the
        // user, 2026-10-22: "overlaping text").
        constexpr float kFaceRoom = 3.0f;
        // The name at 16, at 13.5 when that is too wide, and on two lines at 13.5 when even that
        // runs past the block's edge ("Rune of Greater Wellspring", the user, 2026-10-22:
        // "overlaping text on market").
        const std::string name = nameOf(tables, what);
        const float nameSize = controls::labelWidth(S(16.0f), name) / k > iw ? 13.5f : 16.0f;
        std::vector<std::string> named{name};
        if (controls::labelWidth(S(nameSize), name) / k > iw) {
            named.clear();
            std::string row, word;
            for (size_t i = 0; i <= name.size(); ++i) {
                if (i < name.size() && name[i] != ' ') {
                    word += name[i];
                    continue;
                }
                const std::string tried = row.empty() ? word : row + " " + word;
                if (!row.empty() && named.empty() && controls::labelWidth(S(nameSize), tried) / k > iw) named.push_back(row), row = word;
                else row = tried;
                word.clear();
            }
            if (!row.empty()) named.push_back(row);
        }
        constexpr float kNameStep = 15.0f;  // a second line of the name
        const float nameDrop = float(named.size() - 1) * kNameStep;
        const float kHead = 30.0f + nameDrop;  // the name and its kind, then a gap
        // How tall the lines beside the picture may stand; the rest run under the block at its width.
        const float room = pic - kHead - 4.0f;
        std::vector<Said> said;
        int beside = 0;  // how many of `said` stand beside the picture
        float besideTall = 0.0f;  // and how tall they stand
        bool under = room < kStep;
        // A line of words wrapped to where it will stand: the column beside the picture while it
        // ends there, the block's full width under it once it would not (the narrow column's wrap
        // split "Increase 32% of / Damage" across the block's foot, the user, 2026-10-22).
        const auto wrapped = [&](const std::string& left, float wide) {
            std::vector<std::string> lines;
            std::string row, word;
            for (size_t i = 0; i <= left.size(); ++i) {
                if (i < left.size() && left[i] != ' ') {
                    word += left[i];
                    continue;
                }
                const std::string tried = row.empty() ? word : row + " " + word;
                if (!row.empty() && controls::labelWidth(S(kText), tried) / k > wide) lines.push_back(row), row = word;
                else row = tried;
                word.clear();
            }
            if (!row.empty()) lines.push_back(row);
            return lines;
        };
        const auto add = [&](const std::string& left, uint32_t leftInk, const std::string& value, uint32_t valueInk,
                             std::vector<const gfx::Art*> faces = {}) {
            const float lead = float(faces.size()) * (kFace + 3.0f);
            const float extra = faces.empty() ? 0.0f : 2.0f * kFaceRoom;
            std::vector<std::string> lines;
            if (!value.empty() || left.empty()) lines.push_back(left);
            if (!under) {
                if (lines.empty()) lines = wrapped(left, iw - lead);
                if (besideTall + float(lines.size()) * kStep + extra > room) under = true, lines.clear();
            }
            if (lines.empty()) lines = wrapped(left, aw - lead);
            for (const std::string& line : lines) {
                if (!under) ++beside, besideTall += kStep + (faces.empty() ? 0.0f : extra);
                said.push_back({line, value, leftInk, valueInk, faces});
                faces.clear();
            }
        };
        // Whose class it is, first: under a scroll's long words it ran off the card's foot (the
        // user, 2026-10-19: "missing info on market opened item which class item is that").
        if (sheet.classed && !sheet.who.empty()) {
            // And each class's face before it (the user, 2026-10-19: "show class picture on
            // market"): the second's where the thing asks it, the gladiator's own either way.
            std::vector<const gfx::Art*> faces;
            if (size_t(what.item) < tables.items.size()) {
                const content::ItemRow& row = tables.items[size_t(what.item)];
                for (int i = 0; i < sim::kKinCount; ++i) {
                    if (!((row.classes >> i) & 1)) continue;
                    const bool second = ((row.classes >> (8 + i)) & 1) && sim::Kin(i) != sim::Kin::MagicGladiator;
                    const gfx::Art* face = classFaces_[i * 2 + (second ? 1 : 0)];
                    if (face && face->valid()) faces.push_back(face);
                }
            }
            // The faces and the names on one line when both fit the column, the names on the
            // line under the faces when not: "DW / FE / DK" after three faces ran past the card's
            // edge (the user, 2026-10-21: "overlap issue").
            const std::string& names = sheet.who[0].text;
            const uint32_t namesInk = tip::colourOf(sheet.who[0].tone);
            const float facesWide = float(faces.size()) * (kFace + 3.0f);
            if (!faces.empty() && facesWide + controls::labelWidth(S(kText), names) / k > iw) {
                add("", namesInk, "", 0, faces);
                add(names, namesInk, "", 0);
            } else {
                add(names, namesInk, "", 0, faces);
            }
        }
        if (!sheet.hero.empty()) add(sheet.hero.word, style::kBone2, sheet.hero.value, tip::colourOf(sheet.hero.tone));
        for (const tip::Section& section : sheet.sections) {
            for (const tip::Row& r : section.rows) {
                // The dual-wield rule is the game's, not the thing's: left to the bag's card.
                if (r.keyword == "Dual Wield") continue;
                if (!r.free.empty()) {
                    add((r.keyword.empty() ? "" : r.keyword + " ") + r.free + (r.tail.empty() ? "" : " " + r.tail),
                        tip::colourOf(r.freeTone), "", 0);
                } else if (!r.values.empty()) {
                    add(r.label, style::kBone2, r.values.front().text, tip::colourOf(r.values.front().tone));
                }
            }
        }
        // Who wears it, and every need, a met one quiet and a short one red (the user,
        // 2026-10-22: "we need more info on market opened item"), then its wear and the card's
        // verdict ("Already learned", "Broken").
        for (size_t i = 0; i < sheet.who.size(); ++i) {
            const tip::FootLine& w = sheet.who[i];
            if (i == 0 && sheet.classed) continue;  // put first, above
            if (!w.text.empty()) add(w.text, w.quiet ? style::kAshInk : tip::colourOf(w.tone), "", 0);
        }
        if (!sheet.wear.empty()) {
            add(sheet.wear, sheet.wearTone == tip::Tone::White ? style::kAshInk : tip::colourOf(sheet.wearTone), "", 0);
        }
        if (!sheet.note.empty() && sheet.noteTone != tip::Tone::Gray) add(sheet.note, tip::colourOf(sheet.noteTone), "", 0);
        if (stop <= ay) said.clear(), beside = 0;
        const float tall = kHead + besideTall + (beside > 0 ? 4.0f : 0.0f);
        const float top = ay + 4.0f + std::max(0.0f, (pic - tall) * 0.5f);
        for (size_t i = 0; i < named.size(); ++i) text(ix, top + 13.0f + float(i) * kNameStep, nameSize, ink, named[i]);
        {
            // A prize's kind line opens on a gold star and "Rare", its dot only when a kind
            // follows it.
            const std::string& kindWord = kind.empty() ? sheet.base : kind;
            const float ky = top + 26.0f + nameDrop;
            float kx = ix;
            if (prize) {
                tip::glyphAt(canvas_, tip::Mark::Star, x + (kx + 4.0f) * k, y + (ky - 4.0f) * k, 8.0f * k, kGold);
                kx += 10.0f;
                kx += text(kx, ky, kCaption, kGold, kindWord.empty() ? "Rare" : "Rare \xC2\xB7 ") / k;
            }
            if (!kindWord.empty()) text(kx, ky, kCaption, style::kAshInk, kindWord);
        }
        float ly = top + kHead + 12.0f;
        for (int i = 0; i < int(said.size()); ++i) {
            const bool low = i >= beside;
            if (low && i == beside) ly = block.bottom() + 16.0f;
            if (low && ly > stop) break;
            float lx = low ? ax : ix;
            const float rx = low ? block.right() : ix + iw;
            const bool faced = !said[size_t(i)].faces.empty();
            if (faced) ly += kFaceRoom;
            for (const gfx::Art* face : said[size_t(i)].faces) {
                const Box disc{lx, ly - kFace + 3.0f, kFace, kFace};
                canvas_.rect(P(disc), style::kVoid);
                canvas_.image(*face, P(disc));
                canvas_.outline(P(disc), std::max(1.0f, u), style::kIronDk);
                lx += kFace + 3.0f;
            }
            text(lx, ly, kText, said[size_t(i)].leftInk, said[size_t(i)].left);
            if (!said[size_t(i)].value.empty()) right(rx, ly, kText, said[size_t(i)].valueInk, said[size_t(i)].value);
            ly += kStep + (faced ? kFaceRoom : 0.0f);
        }
        return int(said.size()) > beside ? ly + 4.0f : block.bottom() + 16.0f;
    };
    // The online mark: a round dot, two units across, centred on (ux + 2, midU).
    const auto dot = [&](float ux, float midU, bool on) {
        constexpr float kPi = 3.14159265f;
        float ring[24];
        const float cx = x + (ux + 2.0f) * k, cy = y + midU * k, r = 2.0f * k;
        for (int i = 0; i < 12; ++i) {
            ring[i * 2] = cx + std::cos(float(i) * kPi / 6.0f) * r;
            ring[i * 2 + 1] = cy + std::sin(float(i) * kPi / 6.0f) * r;
        }
        canvas_.polygon(nullptr, ring, nullptr, 12, on ? kOnline : kOffline);
    };
    // A choice, as the chips and the two-way switches are: ash and an iron edge at rest, gold
    // when it is the one chosen. Actions are buttons (controls::button); choices are chips.
    const auto chip = [&](const Box& b, const std::string& word, bool on, float lift) {
        canvas_.rect(P(b), on ? kGoldWash : lift > 0.0f ? style::kAsh3 : style::kAsh2);
        canvas_.outline(P(b), std::max(1.0f, u), on ? kGoldRim : style::kIronLo);
        if (word.empty()) return;
        const float w = controls::labelWidth(S(kText), word);
        controls::label(canvas_, std::round(x + b.midX() * k - w * 0.5f), y + mid(b.y, b.h, kText) * k, S(kText),
                        on ? kGold : style::kBone2, word);
    };
    const auto button = [&](Hit hit, int index, const std::string& word, controls::Kind kind, bool off = false) {
        if (const Spot* s = spotOf(hit, index)) {
            controls::State st = state(hit, index);
            st.off = off;
            controls::button(canvas_, P(s->box), word, kind, st, u, kind == controls::Kind::Primary ? 0.0f : S(12.5f));
        }
    };
    const auto square = [&](Hit hit, controls::Glyph glyph) {
        if (const Spot* s = spotOf(hit, 0)) controls::square(canvas_, P(s->box), glyph, state(hit, 0), u);
    };

    // ---- the head, the tabs, the foot ---------------------------------------------------------
    // No title (the user, 2026-10-12: "we dont need MARKET header it take to much space"): the
    // tabs sit in the head band, the rule under them their floor.
    const Box window{0.0f, 0.0f, kWide, kTall};
    controls::frame(canvas_, P(window), u, "", nullptr, kHead * k);
    square(Hit::Close, controls::Glyph::Close);
    const char* tabs[3] = {"Buy", "Sell", "My listings"};
    for (int t = 0; t < 3; ++t) {
        if (const Spot* s = spotOf(Hit::Tab, t)) controls::tab(canvas_, P(s->box), tabs[t], int(tab_) == t, state(Hit::Tab, t), u, false);
    }
    if (const Spot* pill = spotOf(Hit::Collect, 1)) {
        // What waits, as a gold pill with its dot: the Zen, or the one thing, or how many.
        int64_t zen = 0;
        const sim::Held* only = nullptr;
        int things = 0;
        for (const net::BrokerParcel& p : mine_.box) {
            zen += p.zen;
            if (!p.what.empty()) only = &p.what, ++things;
        }
        std::string word = zen > 0 ? "Collect " + panel::commas(zen) + " Zen"
                           : things == 1 ? "Collect " + nameOf(tables, *only)
                                         : "Collect " + std::to_string(things) + " things";
        if (zen > 0 && things > 0) word += " +" + std::to_string(things);
        const Box b = pill->box;
        tip::panel(canvas_, P(b), b.h * k * 0.5f, state(Hit::Collect, 1).lift > 0.0f ? kGoldRim : kGoldWash, kGoldWash);
        dot(b.x + 6.0f, b.midY(), true);
        text(b.x + 14.0f, mid(b.y, b.h, kText), kText, kGold, word);
    }
    // The foot, on the bag's foot rule: his Zen at the right, level with the bag's, and at the
    // left the tab's own line or what the Broker just said.
    controls::foot(canvas_, P(window), y + panel::kFootRule * k, u);
    {
        const float wide = right(kWide - kPad, kFootBaseline, 16.5f, kGold, panel::commas(realm.money()) + " Zen");
        right(kWide - kPad - wide / k - 8.0f, kFootBaseline, kCaption, style::kAshInk,
              "+ " + panel::commas(realm.vault().zen()) + " in vault");
    }
    const auto foot = [&](const std::string& line) {
        std::string said = line;
        uint32_t ink = style::kAshInk;
        if (saidFor_ > 0.0f && !said_.empty()) {
            said = said_, ink = saidBad_ ? style::kDanger : style::kLiveHi;
        } else if (waiting_ != 0) {
            said = "The Broker is looking...";
        }
        text(kPad, kFootBaseline, 12.5f, ink, said);
    };

    const bool caret = std::fmod(clock_, 1.0) < 0.5;
    const int64_t funds = realm.money() + realm.vault().zen();

    // ---- Buy -----------------------------------------------------------------------------------
    if (tab_ == Tab::Buy) {
        if (const Spot* s = spotOf(Hit::Search, 0)) {
            controls::field(canvas_, P(s->box), query_.text, "Search", focus_ == Focus::Search, false,
                            focus_ == Focus::Search && caret, false, u);
        }
        int all = 0;
        const size_t kinds = size_t(sim::BrokerKind::Count);
        for (size_t i = 0; i < kinds && i < page_.kinds.size(); ++i) all += page_.kinds[i];
        const auto countOf = [&](int kind) {
            return kind == -1 ? all : (size_t(kind) < page_.kinds.size() ? int(page_.kinds[size_t(kind)]) : 0);
        };
        // The category button: a chip with the chosen one, its count and a chevron down.
        if (const Spot* s = spotOf(Hit::Category, 0)) {
            const Box b = s->box;
            const bool picked = query_.kind >= 0;
            chip(b, "", picked, categories_ ? 1.0f : state(Hit::Category, 0).lift);
            text(b.x + kRowPad, mid(b.y, b.h, kText), kText, picked ? kGold : style::kBoneHi,
                 picked ? kKinds[query_.kind] : "All categories");
            right(b.right() - 18.0f, mid(b.y, b.h, kCaption), kCaption, style::kAshInk, std::to_string(countOf(query_.kind)));
            const float cx = x + (b.right() - 9.0f) * k, cy = y + b.midY() * k, r = 2.5f * k;
            const float down[6] = {cx - r, cy - r * 0.45f, cx + r, cy - r * 0.45f, cx, cy + r * 0.6f};
            canvas_.polygon(nullptr, down, nullptr, 3, style::kBone2);
        }
        const bool flags[3] = {query_.usableBy >= 0, query_.excellentOnly, query_.sevenUp};
        for (int f = 0; f < 3; ++f) {
            if (const Spot* s = spotOf(Hit::Flag, f)) chip(s->box, kFlags[f], flags[f], state(Hit::Flag, f).lift);
        }
        button(Hit::Clear, 0, "Clear", controls::Kind::Quiet);

        // The list: a head of captions on a dark rule, then rows centred on one line, every
        // column ranged from the right so the heads and the figures share an edge.
        controls::well(canvas_, P(kList), u);
        const float sellerX = kList.right() - kSellerFromRight;
        const char* heads[4] = {"Item", "Seller", "Left", "Each"};
        const sim::BrokerQuery::Sort sorts[4] = {sim::BrokerQuery::Sort::Name, sim::BrokerQuery::Sort::Seller,
                                                 sim::BrokerQuery::Sort::Left, sim::BrokerQuery::Sort::Price};
        const float headBase = mid(kList.y, kListHead, kCaption);
        for (int h = 0; h < 4; ++h) {
            const Spot* s = spotOf(Hit::Head, h);
            if (!s) continue;
            const bool on = query_.sort == sorts[h];
            const std::string word = std::string(heads[h]) + (on ? (query_.descending ? " v" : " ^") : "");
            const uint32_t ink = on ? kGold : state(Hit::Head, h).lift > 0.0f ? style::kBone2 : style::kAshInk;
            if (h >= 2) right(s->box.right(), headBase, kCaption, ink, word);
            else text(h == 1 ? sellerX + 8.0f : s->box.x, headBase, kCaption, ink, word);
        }
        right(kList.right() - kVsRight, headBase, kCaption, style::kAshInk, "vs last sale");
        hair(kList.x + 1.0f, kList.y + kListHead - 1.0f, kList.w - 2.0f, style::kIronDk);
        for (int r = 0; r < int(page_.rows.size()); ++r) {
            const Spot* s = spotOf(Hit::Row, r);
            if (!s) continue;
            const net::BrokerRow& row = page_.rows[size_t(r)];
            const Box b{s->box.x, s->box.y, s->box.w, kRowH};
            const bool prize = prized(tables, row.what);
            if (row.id == chosen_) chosenWash({b.x + 1.0f, b.y, b.w - 2.0f, b.h - 1.0f});
            else if (state(Hit::Row, r).lift > 0.0f) canvas_.rect(P({b.x + 1.0f, b.y, b.w - 2.0f, b.h - 1.0f}), kHover);
            else if (prize) canvas_.rect(P({b.x + 1.0f, b.y, b.w - 2.0f, b.h - 1.0f}), kPrizeWash);
            hair(b.x + 1.0f, b.bottom() - 1.0f, b.w - 2.0f, style::kAsh3);
            // A prize: a gold bar down the row's left edge and a star after its name.
            if (prize) canvas_.rect(P({b.x + 1.0f, b.y + 3.0f, 2.0f, b.h - 7.0f}), kGold);
            // Words only, no picture and no card on hover: the card beside shows the chosen one
            // (the user, 2026-10-11). What it carries follows the name on the same line.
            const bool alike = (row.flags & net::BrokerRow::Alike) != 0;
            const float nameBase = mid(b.y, kRowH, kName), textBase = mid(b.y, kRowH, kText);
            float named = text(b.x + kRowPad, nameBase, kName, inkOf(tables, row.what), nameOf(tables, row.what)) / k;
            if (prize) {
                tip::glyphAt(canvas_, tip::Mark::Star, x + (b.x + kRowPad + named + 7.0f) * k, y + (nameBase - 4.5f) * k, 9.0f * k, kGold);
                named += 12.0f;
            }
            // A stack of jewels, bought whole: how many, and a piece's price.
            const bool stack = !alike && row.what.durability > 1 && size_t(row.what.item) < tables.items.size() &&
                               sim::stacks(tables.items[size_t(row.what.item)]);
            const std::string sub = alike   ? std::to_string(row.count) + " on sale"
                                    : stack ? "\xC3\x97" + std::to_string(row.what.durability) + " \xC2\xB7 " +
                                                  panel::commas(row.price / row.what.durability) + " each"
                                            : traits(row.what);
            const float subX = b.x + kRowPad + named + 6.0f;
            if (!sub.empty() && subX + controls::labelWidth(S(kCaption), sub) / k < sellerX - 6.0f) {
                text(subX, nameBase, kCaption, alike ? style::kAshInk : tip::colourOf(tip::Tone::Blue), sub);
            }
            // The seller: the dot's place kept on every row, so every name starts on one edge.
            const bool own = (row.flags & net::BrokerRow::Own) != 0;
            if (!alike) {
                dot(sellerX, b.y + kRowH * 0.5f, (row.flags & net::BrokerRow::Online) != 0);
                text(sellerX + 8.0f, textBase, kText, own ? style::kAshInk : style::kBone2, own ? row.seller + " (you)" : row.seller);
                right(kList.right() - kLeftRight, textBase, kText, row.secondsLeft < 7200 ? style::kDanger : style::kAshInk,
                      hoursLeft(row.secondsLeft));
            } else if (row.tiers.size() == 1) {
                text(sellerX + 8.0f, textBase, kText, style::kBone2, row.tiers.front().seller);
            } else {
                text(sellerX + 8.0f, textBase, kText, style::kAshInk, std::to_string(row.tiers.size()) + " sellers");
            }
            right(kList.right() - kPriceRight, nameBase, kName, kGold, panel::commas(row.price));
            // Against the newest sale of the same thing: green under it, quiet over it.
            if (row.lastSale > 0) {
                const int64_t pct = (row.price - row.lastSale) * 100 / row.lastSale;
                const std::string word = pct == 0 ? "0%" : (pct < 0 ? "-" : "+") + std::to_string(std::llabs(pct)) + "%";
                right(kList.right() - kVsRight, textBase, kText, pct < 0 ? style::kLive : style::kAshInk, word);
            } else {
                right(kList.right() - kVsRight, textBase, kText, style::kAshInk2, "new");
            }
        }
        if (page_.rows.empty()) {
            text(kList.x + kRowPad, kList.y + kListHead + 24.0f, kText, style::kAshInk,
                 query_.text.empty() && query_.kind < 0 ? "Nothing is on sale yet." : "Nothing on sale matches.");
        }
        const int pages = std::max(1, int((page_.total + kRows - 1) / kRows));
        std::string count = std::to_string(page_.total) + (page_.total == 1 ? " listing" : " listings");
        if (!query_.sellerName.empty()) count += " sold by " + query_.sellerName;
        foot(count);
        if (const Spot* s = spotOf(Hit::Prev, 0)) {
            right(s->box.x - 8.0f, kFootBaseline, 12.5f, style::kBone2,
                  "Page " + std::to_string(page_.page + 1) + " of " + std::to_string(pages));
        }
        square(Hit::Prev, controls::Glyph::Left);
        square(Hit::Next, controls::Glyph::Right);

        // The card: the picture turning, the name; the thing's lines as label and value; at its
        // foot, from the bottom up, Buy, the total, and the seller or the quantity.
        const Box card = kCard;
        const float in = 10.0f;
        controls::well(canvas_, P(card), u);
        const net::BrokerRow* row = chosenRow();
        if (!row) {
            text(card.x + in, card.y + 150.0f, kText, style::kAshInk, "Choose a listing to see it in full.");
            text(card.x + in, card.y + 166.0f, kText, style::kAshInk, "Jewels fill from the cheapest first.");
        } else {
            const bool alike = (row->flags & net::BrokerRow::Alike) != 0;
            const bool own = (row->flags & net::BrokerRow::Own) != 0;
            float ly = thingCard(row->what, card.x + in, card.y + in, card.w - 2.0f * in, 76.0f,
                                 alike ? std::to_string(row->count) + " on sale" : "", alike ? 0.0f : card.bottom() - 104.0f);
            if (alike) {
                // The ladder as a table, cheapest first, the tiers this many takes lit in gold.
                controls::kicker(canvas_, x + (card.x + in) * k, y + ly * k, "On sale, cheapest first", u);
                ly += 14.0f;
                text(card.x + in + 4.0f, ly, kCaption, style::kAshInk2, "Each");
                text(card.x + in + 62.0f, ly, kCaption, style::kAshInk2, "Seller");
                right(card.right() - in - 4.0f, ly, kCaption, style::kAshInk2, "Count");
                ly += 4.0f;
                int left = qty_;
                for (const net::BrokerRow::Tier& t : row->tiers) {
                    if (ly + 18.0f > card.bottom() - 84.0f) break;
                    const int take = std::min(left, t.count);
                    left -= take;
                    const Box tier{card.x + in, ly, card.w - 2.0f * in, 18.0f};
                    if (take > 0) chosenWash(tier);
                    const float base = mid(tier.y, tier.h, kText);
                    text(tier.x + 4.0f, base, kText, take > 0 ? kGold : style::kBone2, panel::commas(t.price));
                    text(tier.x + 62.0f, base, kText, take > 0 ? style::kBone : style::kAshInk, t.seller);
                    right(tier.right() - 4.0f, base, kText, take > 0 ? style::kBoneHi : style::kAshInk,
                          take > 0 && take < t.count ? std::to_string(take) + " of " + std::to_string(t.count)
                                                     : std::to_string(t.count));
                    ly += 20.0f;
                }
                if (row->lastSale > 0 && ly + 14.0f < card.bottom() - 84.0f) {
                    text(card.x + in, ly + 12.0f, kCaption, style::kAshInk, "Last sold for " + panel::commas(row->lastSale) + " each.");
                }
                if (const Spot* s = spotOf(Hit::Qty, -1)) controls::square(canvas_, P(s->box), controls::Glyph::Left, state(Hit::Qty, -1), u);
                if (const Spot* s = spotOf(Hit::Qty, -1)) {
                    const float cx = s->box.right() + 15.0f;
                    const std::string n = std::to_string(qty_);
                    text(cx - controls::labelWidth(S(kName), n) / k * 0.5f, mid(s->box.y, s->box.h, kName), kName, style::kBoneHi, n);
                }
                if (const Spot* s = spotOf(Hit::Qty, 1)) controls::square(canvas_, P(s->box), controls::Glyph::Right, state(Hit::Qty, 1), u);
                button(Hit::MostQty, 0, "Most I can afford", controls::Kind::Secondary);
            } else {
                // The seller.
                hair(card.x + in, card.bottom() - 96.0f, card.w - 2.0f * in, style::kIronDk);
                const bool online = (row->flags & net::BrokerRow::Online) != 0;
                // One market for every realm (docs/game/shared-market.md): a seller elsewhere is
                // named so, and has no Whisper.
                const bool elsewhere = (row->flags & net::BrokerRow::Elsewhere) != 0;
                dot(card.x + in, card.bottom() - 82.0f, online);
                const float w = text(card.x + in + 8.0f, card.bottom() - 78.0f, kCaption, style::kAshInk, "Sold by ");
                text(card.x + in + 8.0f + w / k, card.bottom() - 78.0f, kText,
                     state(Hit::Seller, 0).lift > 0.0f ? style::kBoneHi : kGold, own ? row->seller + " (you)" : row->seller);
                text(card.x + in + 8.0f, card.bottom() - 65.0f, kCaption, row->secondsLeft < 7200 ? style::kDanger : style::kAshInk,
                     std::string(elsewhere ? "Another realm" : online ? "Online" : "Offline") + " · " +
                         hoursLeft(row->secondsLeft) + " left");
                if (!elsewhere) button(Hit::Whisper, 0, "Whisper", controls::Kind::Quiet);
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
            // Into the vault, the bag if the vault is full (sim::Realm::brokerBuy).
            const bool vault = realm.vault().free(tables, shape.width, shape.height) >= 0;
            const bool fits = vault || realm.satchel().free(tables, shape.width, shape.height) >= 0;
            const std::string where = vault ? "goes to your vault" : fits ? "vault full: to your bag" : "waits in Collect";
            text(card.x + in, card.bottom() - 42.0f, kCaption, fits ? style::kAshInk : style::kDanger,
                 std::string(alike ? "Total" : "Buyout") + " · " + where);
            right(card.right() - in, card.bottom() - 41.0f, 20.0f, kGold, panel::commas(cost));
            if (spotOf(Hit::Buy, 0)) {
                const std::string word = cost > funds ? "Not enough Zen" : confirm_ ? "Confirm " + panel::commas(cost) : "Buy";
                button(Hit::Buy, 0, word, controls::Kind::Primary, waiting_ != 0 || cost > funds);
            } else if (own) {
                text(card.x + in, card.bottom() - 18.0f, kCaption, style::kAshInk, "Your own listing: not yours to buy.");
            }
        }

        // The open category list, over the page under its button.
        if (categories_) {
            const Box sheet{kPad + kSearchW + kGap, kBodyY + kBarH + 2.0f, 150.0f, float(int(sim::BrokerKind::Count) + 1) * 16.0f + 4.0f};
            canvas_.rect(P(sheet), style::kAsh1);
            canvas_.outline(P(sheet), std::max(1.0f, u), style::kIron);
            for (int kind = -1; kind < int(sim::BrokerKind::Count); ++kind) {
                const Spot* s = spotOf(Hit::Kind, kind);
                if (!s) continue;
                const bool on = query_.kind == kind;
                if (on) canvas_.rect(P(s->box), kGoldWash);
                else if (state(Hit::Kind, kind).lift > 0.0f) canvas_.rect(P(s->box), kHover);
                const int count = countOf(kind);
                const uint32_t ink = on ? kGold : count > 0 ? style::kBone2 : style::kAshInk2;
                const float base = mid(s->box.y, s->box.h, kText);
                text(s->box.x + 6.0f, base, kText, ink, kind == -1 ? "All categories" : kKinds[kind]);
                right(s->box.right() - 6.0f, base, kCaption, on ? kGold : style::kAshInk, std::to_string(count));
            }
        }
    }

    // ---- Sell ----------------------------------------------------------------------------------
    if (tab_ == Tab::Sell) {
        // Bag or vault, each with how many things it holds: a two-way choice, so two chips.
        int inBag = 0, inVault = 0;
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) inBag += realm.satchel()[slot].empty() ? 0 : 1;
        for (int cell = 0; cell < sim::kVaultCells; ++cell) inVault += realm.vault()[cell].empty() ? 0 : 1;
        if (const Spot* s = spotOf(Hit::Source, 0)) chip(s->box, "Bag · " + std::to_string(inBag), !fromVault_, state(Hit::Source, 0).lift);
        if (const Spot* s = spotOf(Hit::Source, 1)) chip(s->box, "Vault · " + std::to_string(inVault), fromVault_, state(Hit::Source, 1).lift);
        controls::well(canvas_, P({kThings.x, kThings.y + 26.0f, kThings.w, kThings.h - 26.0f}), u);
        for (const Spot& s : spots_) {
            if (s.hit != Hit::Thing) continue;
            const Thing& t = things_[size_t(s.index)];
            const bool ok = t.no == sim::BrokerNo::Yes;
            const Box b{s.box.x + 1.0f, s.box.y, s.box.w - 2.0f, s.box.h};
            if (t.slot == sellSlot_) chosenWash(b);
            else if (ok && state(Hit::Thing, s.index).lift > 0.0f) canvas_.rect(P(b), kHover);
            hair(b.x, s.box.y + 21.0f, b.w, style::kAsh3);
            // No picture: the user wants the list words only, the picture large once a thing is chosen.
            const float base = mid(s.box.y, 22.0f, kName);
            const float named = text(s.box.x + kRowPad, base, kName, ok ? inkOf(tables, t.what) : style::kAshInk2, nameOf(tables, t.what)) / k;
            if (ok && prized(tables, t.what)) {
                tip::glyphAt(canvas_, tip::Mark::Star, x + (s.box.x + kRowPad + named + 7.0f) * k, y + (base - 4.5f) * k, 9.0f * k, kGold);
            }
            const std::string sub = ok ? traits(t.what) : whyNot(t.no);
            if (!sub.empty()) right(s.box.right() - kRowPad, base, kCaption, ok ? tip::colourOf(tip::Tone::Blue) : style::kAshInk, sub);
        }
        if (things_.empty()) {
            text(kThings.x + kRowPad, kThings.y + 50.0f, kText, style::kAshInk, fromVault_ ? "Your vault is empty." : "Your bag is empty.");
        }
        square(Hit::ThingPrev, controls::Glyph::Left);
        square(Hit::ThingNext, controls::Glyph::Right);

        const Box thing = kThing;
        const float in = 10.0f;
        controls::well(canvas_, P(thing), u);
        if (sellSlot_ < 0 || sellSeen_.empty()) {
            text(thing.x + 20.0f, thing.y + 150.0f, kText, style::kAshInk, "Choose a thing from your bag or vault,");
            text(thing.x + 20.0f, thing.y + 166.0f, kText, style::kAshInk, "or drag one here from your bag.");
        } else {
            // The thing in full beside its picture (the user, 2026-10-11): its lines, the market's
            // three figures in one framed strip, and the last sales.
            float ly = thingCard(sellPart(play), thing.x + in, thing.y + in, thing.w - 2.0f * in, 108.0f,
                                 fromVault_ ? "From your vault" : "From your bag", thing.bottom() - 100.0f) + 2.0f;
            const bool quoted = quote_.item == sellSeen_.item;
            const int64_t typical = quoted && !quote_.sales.empty() ? quote_.sales.front().price : 0;
            const std::string cells[3][2] = {
                {"Cheapest now", quoted && quote_.cheapest > 0 ? panel::commas(quote_.cheapest) : "None"},
                {"Last sold for", typical > 0 ? panel::commas(typical) : "Never sold"},
                {"On sale now", quoted ? std::to_string(quote_.onSale) : "..."}};
            const Box facts{thing.x + in, ly, thing.w - 2.0f * in, 34.0f};
            canvas_.outline(P(facts), std::max(1.0f, u), style::kIronDk);
            for (int c = 0; c < 3; ++c) {
                const float cx = facts.x + float(c) * (facts.w / 3.0f);
                if (c > 0) canvas_.rect({std::round(x + cx * k), y + facts.y * k, std::max(1.0f, std::round(u)), facts.h * k}, style::kIronDk);
                text(cx + 7.0f, facts.y + 13.0f, kCaption, style::kAshInk, cells[c][0]);
                text(cx + 7.0f, facts.y + 28.0f, kName, c == 0 && quoted && quote_.cheapest > 0 ? kGold : style::kBoneHi, cells[c][1]);
            }
            ly = facts.bottom() + 18.0f;
            controls::kicker(canvas_, x + (thing.x + in) * k, y + ly * k, "Last sales", u);
            ly += 16.0f;
            for (size_t i = 0; quoted && i < quote_.sales.size() && i < 8 && ly < thing.bottom() - 6.0f; ++i) {
                const sim::Sale& sale = quote_.sales[i];
                text(thing.x + in, ly, kText, style::kBone2, ago(std::max<int64_t>(0, realm.wallClock() - sale.at)));
                right(thing.right() - in, ly, kText, kGold, panel::commas(sale.price));
                hair(thing.x + in, ly + 5.0f, thing.w - 2.0f * in, style::kAsh3);
                ly += 17.0f;
            }
            if (quoted && quote_.sales.empty()) {
                text(thing.x + in, ly, kText, style::kAshInk, "None yet. The first sale sets the going price.");
            }

            // The price.
            const Box price = kPrice;
            controls::kicker(canvas_, x + price.x * k, y + (price.y + 12.0f) * k, "Buyout price", u);
            if (const Spot* s = spotOf(Hit::Price, 0)) {
                const std::string shown = priceText_.empty() ? "" : panel::commas(priceTyped());
                controls::field(canvas_, P(s->box), shown, "0", focus_ == Focus::Price, false, focus_ == Focus::Price && caret, false, u);
            }
            const char* quick[3] = {"Cheapest", "Last sold", "5% under"};
            for (int q = 0; q < 3; ++q) {
                // Without a figure to go by, the button has nothing to set.
                button(Hit::Quick, q, quick[q], controls::Kind::Secondary, q == 1 ? typical <= 0 : !(quoted && quote_.cheapest > 0));
            }
            const int64_t asked = priceTyped();
            const int64_t floor = std::max<int64_t>(1, sim::npcValue(tables, sellPart(play)));
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
            text(price.x, price.y + 84.0f, kCaption, guardInk, guard);
            controls::kicker(canvas_, x + price.x * k, y + (price.y + 104.0f) * k, "Listed for", u);
            for (int h = 0; h < 3; ++h) {
                if (const Spot* s = spotOf(Hit::Hours, h)) {
                    chip(s->box, std::to_string(sim::kBrokerHours[h]) + " hours", h == hours_, state(Hit::Hours, h).lift);
                }
            }
            // How many of the stack go up, the rest left where it lies.
            if (const Spot* s = spotOf(Hit::Pieces, -1)) {
                const int pieces = sellPieces(play);
                controls::kicker(canvas_, x + price.x * k, y + (price.y + 152.0f) * k,
                                 "How many, of " + std::to_string(sellSeen_.durability), u);
                controls::square(canvas_, P(s->box), controls::Glyph::Left, state(Hit::Pieces, -1), u);
                const float cx = s->box.right() + 20.0f;
                const std::string n = std::to_string(pieces);
                text(cx - controls::labelWidth(S(kName), n) / k * 0.5f, mid(s->box.y, s->box.h, kName), kName, style::kBoneHi, n);
                if (const Spot* r = spotOf(Hit::Pieces, 1)) controls::square(canvas_, P(r->box), controls::Glyph::Right, state(Hit::Pieces, 1), u);
                button(Hit::AllPieces, 0, "All " + std::to_string(sellSeen_.durability), controls::Kind::Secondary,
                       pieces >= sellSeen_.durability);
            }
            // What listing it means, line by line over the button.
            hair(price.x, price.bottom() - 78.0f, price.w, style::kIronDk);
            text(price.x, price.bottom() - 64.0f, kText, style::kBone2, "You get when it sells");
            right(price.right(), price.bottom() - 64.0f, kName, kGold, panel::commas(std::max<int64_t>(0, asked)));
            text(price.x, price.bottom() - 50.0f, kText, style::kBone2, "To list it");
            right(price.right(), price.bottom() - 50.0f, kText, style::kBoneHi, "Free");
            text(price.x, price.bottom() - 36.0f, kText, style::kBone2, "Your listings");
            right(price.right(), price.bottom() - 36.0f, kText, style::kBoneHi,
                  std::to_string(std::min(mine_.accountCount + 1, sim::kBrokerListings)) + " of 20 after this");
            const bool full = mine_.accountCount >= sim::kBrokerListings;
            button(Hit::List, 0, full ? "Full" : "List", controls::Kind::Primary,
                   waiting_ != 0 || asked < floor || asked > sim::kBrokerMostPrice || full);
        }
        foot("Or drag a thing here from your bag.");
    }

    // ---- My listings ---------------------------------------------------------------------------
    if (tab_ == Tab::Mine) {
        // What waits, as one strip across the top: its Zen, its things, Collect all.
        const Box strip = kStrip;
        int64_t zen = 0;
        std::vector<const net::BrokerParcel*> things;
        for (const net::BrokerParcel& p : mine_.box) {
            zen += p.zen;
            if (!p.what.empty()) things.push_back(&p);
        }
        if (mine_.box.empty()) {
            controls::well(canvas_, P(strip), u);
            text(strip.x + 12.0f, mid(strip.y, strip.h, kText), kText, style::kAshInk,
                 "Nothing waits for you. What sells, and what comes back, waits here 30 days.");
        } else {
            chosenWash(strip);
            controls::kicker(canvas_, x + (strip.x + 10.0f) * k, y + (strip.y + 15.0f) * k, "Waiting to collect", u);
            float wx = strip.x + 10.0f;
            const float base = strip.y + 31.0f;
            if (zen > 0) wx += text(wx, base, 17.0f, kGold, panel::commas(zen) + " Zen") / k + 8.0f;
            if (!things.empty()) {
                const net::BrokerParcel& p = *things.front();
                const std::string what = things.size() == 1 ? nameOf(tables, p.what) : std::to_string(things.size()) + " things";
                wx += text(wx, base, kName, things.size() == 1 ? inkOf(tables, p.what) : style::kBoneHi,
                           (zen > 0 ? "and " : "") + what) / k + 8.0f;
                if (things.size() == 1) {
                    const char* why = p.why == uint8_t(sim::Parcel::Why::Cancelled) ? "Cancelled"
                                      : p.why == uint8_t(sim::Parcel::Why::Bought) ? "Bought, bag full"
                                                                                   : "Came back unsold";
                    text(wx, base, kCaption, style::kAshInk, why);
                }
            }
            int days = 30;
            for (const net::BrokerParcel& p : mine_.box) days = std::min(days, std::max(1, p.secondsLeft / 86400));
            right(strip.right() - 118.0f, mid(strip.y, strip.h, kCaption), kCaption, style::kAshInk,
                  "Kept 30 days. The oldest has " + std::to_string(days) + (days == 1 ? " day left." : " days left."));
            button(Hit::Collect, 0, "Collect all", controls::Kind::Primary, waiting_ != 0);
        }

        // His listings.
        const Box list = kMine;
        controls::well(canvas_, P(list), u);
        const float leftAt = list.right() - 196.0f, priceAt = list.right() - 108.0f;
        const float headBase = mid(list.y, kListHead, kCaption);
        text(list.x + kRowPad, headBase, kCaption, style::kAshInk, "On sale");
        right(leftAt, headBase, kCaption, style::kAshInk, "Left");
        right(priceAt, headBase, kCaption, style::kAshInk, "Price");
        right(list.right() - kRowPad, headBase, kCaption, style::kAshInk, std::to_string(mine_.accountCount) + " of 20 used");
        hair(list.x + 1.0f, list.y + kListHead - 1.0f, list.w - 2.0f, style::kIronDk);
        const int first = minePage_ * kRows;
        for (int r = 0; r < kRows && first + r < int(mine_.mine.size()); ++r) {
            const int i = first + r;
            const net::BrokerRow& row = mine_.mine[size_t(i)];
            const float ry = list.y + kListHead + float(r) * kMineRowH;
            const float nameBase = mid(ry, kMineRowH, kName), textBase = mid(ry, kMineRowH, kText);
            hair(list.x + 1.0f, ry + kMineRowH - 1.0f, list.w - 2.0f, style::kAsh3);
            // No picture: its card on hover instead (the user, 2026-10-11).
            if (state(Hit::Listing, i).lift > 0.0f) canvas_.rect(P({list.x + 1.0f, ry, list.w - 110.0f, kMineRowH - 1.0f}), kHover);
            const float named = text(list.x + kRowPad, nameBase, kName, inkOf(tables, row.what), nameOf(tables, row.what)) / k;
            const std::string sub = traits(row.what);
            if (!sub.empty() && list.x + kRowPad + named + 6.0f + controls::labelWidth(S(kCaption), sub) / k < leftAt - 30.0f) {
                text(list.x + kRowPad + named + 6.0f, nameBase, kCaption, tip::colourOf(tip::Tone::Blue), sub);
            }
            right(leftAt, textBase, kText, row.secondsLeft < 7200 ? style::kDanger : style::kAshInk, hoursLeft(row.secondsLeft));
            if (row.id == editing_) {
                if (const Spot* s = spotOf(Hit::LowerField, i)) {
                    controls::field(canvas_, P(s->box), lowerText_.empty() ? "" : panel::commas(std::stoll("0" + lowerText_)),
                                    "", focus_ == Focus::Lower, false, focus_ == Focus::Lower && caret, false, u);
                }
                button(Hit::Save, i, "Save", controls::Kind::Secondary);
                button(Hit::Back, i, "Back", controls::Kind::Quiet);
            } else {
                right(priceAt, nameBase, kName, kGold, panel::commas(row.price));
                button(Hit::Lower, i, "Lower", controls::Kind::Secondary);
                button(Hit::Cancel, i, "Cancel", controls::Kind::Quiet);
            }
        }
        if (const Spot* s = spotOf(Hit::MinePrev, 0)) {
            const int pages = int((mine_.mine.size() + kRows - 1) / kRows);
            right(s->box.x - 8.0f, kFootBaseline, 12.5f, style::kBone2,
                  "Page " + std::to_string(minePage_ + 1) + " of " + std::to_string(pages));
        }
        square(Hit::MinePrev, controls::Glyph::Left);
        square(Hit::MineNext, controls::Glyph::Right);
        if (mine_.mine.empty()) {
            text(list.x + kRowPad, list.y + kListHead + 24.0f, kText, style::kAshInk, "Nothing on sale. List something from the Sell tab.");
        }

        // What sold lately.
        const Box sold = kSold;
        controls::well(canvas_, P(sold), u);
        controls::kicker(canvas_, x + (sold.x + 10.0f) * k, y + (sold.y + 16.0f) * k, "Sold lately", u);
        float ly = sold.y + 34.0f;
        for (const sim::Sale& sale : mine_.soldLately) {
            if (ly > sold.bottom() - 14.0f) break;
            text(sold.x + 10.0f, ly, kName, inkOf(tables, sale.what), nameOf(tables, sale.what));
            right(sold.right() - 10.0f, ly, kName, kGold, "+" + panel::commas(sale.price));
            text(sold.x + 10.0f, ly + 13.0f, kCaption, style::kAshInk, ago(std::max<int64_t>(0, realm.wallClock() - sale.at)));
            hair(sold.x + 10.0f, ly + 19.0f, sold.w - 20.0f, style::kAsh3);
            ly += 30.0f;
        }
        if (mine_.soldLately.empty()) text(sold.x + 10.0f, ly, kText, style::kAshInk, "Nothing yet.");
        foot("Lowering a price is free. To raise it, cancel and list again.");
    }

    // The turning picture stood for next frame on a stage no bigger than itself (a stage the size
    // of the window, redrawn every frame while the thing turns, halved the frame rate); the card
    // above cut this frame's from the last.
    if (stage) stage->stand(standing, kTurnSquare, kTurnSquare);

    // The item card over his own listing under the pointer, the bag's own card: My listings has
    // no card beside it, so this is where its details show (the user, 2026-10-11).
    if (hovered_ >= 0 && size_t(hovered_) < spots_.size() && pressed_ < 0) {
        const Spot& s = spots_[size_t(hovered_)];
        if (s.hit == Hit::Listing && size_t(s.index) < mine_.mine.size()) {
            const sim::Held& what = mine_.mine[size_t(s.index)].what;
            tip::Sheet sheet = describe(tables, what, realm.wearer(), realm.satchel());
            if (tipStage_) tip::stand(*tipStage_, what.item, what.refinement, sheet);
            tip::draw(tip_, sheet, P(s.box), screenW_, screenH_);
        }
    }
}

}  // namespace mu::game
