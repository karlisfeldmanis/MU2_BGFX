// The Broker's window drawn (game/ui/broker.h): the frame and its head, every pane in the
// controls' own Sanctuary pieces (game/ui/controls.h) and nothing of its own colour but the gold
// of a chosen thing, the user's for a chosen row since the travel window (2026-09-30), and the
// blood red of the one Primary button a screen has.
#include "game/ui/broker.h"
#include "game/ui/broker_logic.h"

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

bool stackOf(const content::Tables& tables, const sim::Held& what) {
    return what.durability > 1 && size_t(what.item) < tables.items.size() && sim::stacks(tables.items[size_t(what.item)]) &&
           !sim::ammunition(tables.items[size_t(what.item)]);
}

}  // namespace

void Broker::rebuild(Play& play, Stage* stage) {
    canvas_.clear();
    tip_.clear();
    const sim::Realm& realm = play.realm();
    const content::Tables& tables = *realm.tables();
    const float k = panel::scale(), u = tip::unit();
    const float x = x_, y = y_;
    const float W = g_.wide;
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
    const auto wide = [&](float size, const std::string& s) { return controls::labelWidth(S(size), s) / k; };
    // A line cut to `room` units with an ellipsis, a whole UTF-8 letter at a time.
    const auto fitted = [&](const std::string& s, float size, float room) {
        if (wide(size, s) <= room) return s;
        std::string t = s;
        while (!t.empty() && wide(size, t + "\xE2\x80\xA6") > room) {
            while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();
            if (!t.empty()) t.pop_back();
        }
        return t + "\xE2\x80\xA6";
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
    const auto hair = [&](float ux, float uy, float run, uint32_t ink) {
        canvas_.rect({std::round(x + ux * k), std::round(y + uy * k), std::round(run * k), std::max(1.0f, std::round(u))}, ink);
    };
    const auto kicker = [&](float ux, float baselineU, const std::string& s) {
        controls::kicker(canvas_, x + ux * k, y + baselineU * k, s, u);
    };
    // A pane: the sheet's ash with an iron edge, as the canvas's panels are.
    const auto pane = [&](const Box& b, uint32_t edge = style::kIronLo) {
        canvas_.rect(P(b), style::kAsh1);
        canvas_.outline(P(b), std::max(1.0f, u), edge);
    };
    std::vector<Standing> standing;
    uint32_t standKey = 0;
    float standW = 0.0f, standH = 0.0f;
    const gfx::Art turned = stage ? stage->picture() : gfx::Art{};
    // What stands, as one number: the stage's picture is drawn only when it was taken of this.
    const auto keyOf = [&](uint32_t base) {
        uint32_t key = base * 2654435761u;
        for (const Standing& s : standing) {
            key = (key ^ uint32_t(s.item + 1)) * 16777619u;
            key = (key ^ uint32_t(s.refinement)) * 16777619u;
            key = (key ^ uint32_t(s.box.x * 8.0f) ^ (uint32_t(s.box.y * 8.0f) << 16)) * 16777619u;
        }
        return key;
    };
    // The thing, market-style (the user, 2026-10-12): one framed block, the picture turning on the
    // left and its name, kind and lines on the right, a buyer's lines only. `pic` is the square's
    // side; lines that do not fit beside it run on under the block to `stop`.
    const auto thingCard = [&](const sim::Held& what, float ax, float ay, float aw, float pic, const std::string& kind,
                               float stop) {
        const tip::Sheet sheet = describe(tables, what, realm.wearer(), realm.satchel());
        const uint32_t ink = inkOf(tables, what);
        const bool prize = prized(tables, what);
        const Box block{ax, ay, aw, pic + 8.0f};
        canvas_.rect(P(block), style::kAsh2);
        canvas_.outline(P(block), std::max(1.0f, u), prize ? kGoldRim : style::kIronDk);
        const Box well{ax + 4.0f, ay + 4.0f, pic, pic};
        const uint32_t floor = style::kVoid;
        const uint32_t lit = prize && ink == tip::colourOf(tip::Tone::White) ? kGold : ink;
        // A large picture glows lower and fainter: at 150 units the small card's glow read as a
        // grey slab under the thing.
        const float strength = (prize ? 0.34f : 0.16f) * (pic > 80.0f ? 0.45f : 1.0f);
        const uint32_t glow = (lit & 0x00FFFFFFu) | (uint32_t(strength * 255.0f) << 24), none = lit & 0x00FFFFFFu;
        canvas_.shade(P(well), floor, floor, style::kAsh1, style::kAsh1);
        const float band = pic > 80.0f ? 0.3f : 0.45f;
        canvas_.shade(P({well.x, well.y + pic * (1.0f - band), pic, pic * band}), none, none, glow, glow);
        canvas_.rect({std::round(x + (well.right() + 4.0f) * k), std::round(y + (ay + 10.0f) * k), std::max(1.0f, std::round(u)),
                      std::round((pic - 12.0f) * k)},
                     style::kIronDk);
        standing.push_back({what.item, {0.0f, 0.0f, pic, pic}, what.refinement, true, what.excellent != 0});
        standKey = 2u + uint32_t(pic);
        standW = standH = pic;
        if (turned.valid() && stood_ == keyOf(standKey)) {
            // A thing as big as its cells (describe's artScale, a jewel 0.42): in the 150-unit
            // square a one-cell jewel at the small card's 0.62 filled it (the user, 2026-10-24:
            // "jewels to big on large contrainer").
            const float least = pic > 80.0f ? 0.42f : 0.62f;
            const float side = (pic - 2.0f * std::round(pic * 0.06f)) * std::clamp(sheet.artScale, least, 1.0f);
            canvas_.image(turned, P({well.midX() - side * 0.5f, well.midY() - side * 0.5f, side, side}));
        }
        struct Said {
            std::string left, value;
            uint32_t leftInk = 0, valueInk = 0;
            std::vector<const gfx::Art*> faces;
        };
        const float ix = well.right() + 12.0f, iw = block.right() - 8.0f - ix;
        constexpr float kFace = 14.0f;
        constexpr float kStep = 13.0f;
        constexpr float kFaceRoom = 3.0f;
        const std::string name = nameOf(tables, what);
        const float nameSize = pic >= 100.0f && wide(20.0f, name) <= iw ? 20.0f : wide(16.0f, name) > iw ? 13.5f : 16.0f;
        std::vector<std::string> named{name};
        if (wide(nameSize, name) > iw) {
            named.clear();
            std::string row, word;
            for (size_t i = 0; i <= name.size(); ++i) {
                if (i < name.size() && name[i] != ' ') {
                    word += name[i];
                    continue;
                }
                const std::string tried = row.empty() ? word : row + " " + word;
                if (!row.empty() && named.empty() && wide(nameSize, tried) > iw) named.push_back(row), row = word;
                else row = tried;
                word.clear();
            }
            if (!row.empty()) named.push_back(row);
        }
        const float nameStep = nameSize + 1.0f;
        const float nameDrop = float(named.size() - 1) * nameStep;
        const float head = 30.0f + nameDrop + (nameSize > 16.0f ? 4.0f : 0.0f);
        const float room = pic - head - 4.0f;
        std::vector<Said> said;
        int beside = 0;
        float besideTall = 0.0f;
        bool under = room < kStep;
        const auto wrapped = [&](const std::string& left, float run) {
            std::vector<std::string> lines;
            std::string row, word;
            for (size_t i = 0; i <= left.size(); ++i) {
                if (i < left.size() && left[i] != ' ') {
                    word += left[i];
                    continue;
                }
                const std::string tried = row.empty() ? word : row + " " + word;
                if (!row.empty() && wide(kText, tried) > run) lines.push_back(row), row = word;
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
        if (sheet.classed && !sheet.who.empty()) {
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
            const std::string& names = sheet.who[0].text;
            const uint32_t namesInk = tip::colourOf(sheet.who[0].tone);
            const float facesWide = float(faces.size()) * (kFace + 3.0f);
            if (!faces.empty() && facesWide + wide(kText, names) > iw) {
                add("", namesInk, "", 0, faces);
                add(names, namesInk, "", 0);
            } else {
                add(names, namesInk, "", 0, faces);
            }
        }
        if (!sheet.hero.empty()) add(sheet.hero.word, style::kBone2, sheet.hero.value, tip::colourOf(sheet.hero.tone));
        for (const tip::Section& section : sheet.sections) {
            for (const tip::Row& r : section.rows) {
                if (r.keyword == "Dual Wield") continue;
                if (!r.free.empty()) {
                    add((r.keyword.empty() ? "" : r.keyword + " ") + r.free + (r.tail.empty() ? "" : " " + r.tail),
                        tip::colourOf(r.freeTone), "", 0);
                } else if (!r.values.empty()) {
                    add(r.label, style::kBone2, r.values.front().text, tip::colourOf(r.values.front().tone));
                }
            }
        }
        for (size_t i = 0; i < sheet.who.size(); ++i) {
            const tip::FootLine& w = sheet.who[i];
            if (i == 0 && sheet.classed) continue;
            if (!w.text.empty()) add(w.text, w.quiet ? style::kAshInk : tip::colourOf(w.tone), "", 0);
        }
        if (!sheet.wear.empty()) {
            add(sheet.wear, sheet.wearTone == tip::Tone::White ? style::kAshInk : tip::colourOf(sheet.wearTone), "", 0);
        }
        if (!sheet.note.empty() && sheet.noteTone != tip::Tone::Gray) add(sheet.note, tip::colourOf(sheet.noteTone), "", 0);
        if (stop <= ay) said.clear(), beside = 0, besideTall = 0.0f;
        const float tall = head + besideTall + (beside > 0 ? 4.0f : 0.0f);
        const float top = ay + 4.0f + std::max(0.0f, (pic - tall) * 0.5f);
        for (size_t i = 0; i < named.size(); ++i) text(ix, top + nameSize - 3.0f + float(i) * nameStep, nameSize, ink, named[i]);
        {
            const std::string& kindWord = kind.empty() ? sheet.base : kind;
            const float ky = top + head - 4.0f;
            float kx = ix;
            if (prize) {
                tip::glyphAt(canvas_, tip::Mark::Star, x + (kx + 4.0f) * k, y + (ky - 4.0f) * k, 8.0f * k, kGold);
                kx += 10.0f;
                kx += text(kx, ky, kCaption, kGold, kindWord.empty() ? "Rare" : "Rare \xC2\xB7 ") / k;
            }
            if (!kindWord.empty()) text(kx, ky, kCaption, style::kAshInk, kindWord);
        }
        float ly = top + head + 12.0f;
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
    };
    // The online mark: a round dot, two units across, centred on (ux + 2, midU).
    const auto dot = [&](float ux, float midU, uint32_t ink) {
        constexpr float kPi = 3.14159265f;
        float ring[24];
        const float cx = x + (ux + 2.0f) * k, cy = y + midU * k, r = 2.0f * k;
        for (int i = 0; i < 12; ++i) {
            ring[i * 2] = cx + std::cos(float(i) * kPi / 6.0f) * r;
            ring[i * 2 + 1] = cy + std::sin(float(i) * kPi / 6.0f) * r;
        }
        canvas_.polygon(nullptr, ring, nullptr, 12, ink);
    };
    // A choice: ash and an iron edge at rest, gold when it is the one chosen. Actions are buttons
    // (controls::button); choices are chips.
    const auto chip = [&](const Box& b, const std::string& word, bool on, float lift, float size = kText) {
        canvas_.rect(P(b), on ? kGoldWash : lift > 0.0f ? style::kAsh3 : style::kAsh2);
        canvas_.outline(P(b), std::max(1.0f, u), on ? kGoldRim : lift > 0.0f ? style::kIron : style::kIronLo);
        if (word.empty()) return;
        const float w = controls::labelWidth(S(size), word);
        controls::label(canvas_, std::round(x + b.midX() * k - w * 0.5f), y + mid(b.y, b.h, size) * k, S(size),
                        on ? kGold : lift > 0.0f ? style::kBoneHi : style::kBone2, word);
    };
    const auto button = [&](Hit hit, int index, const std::string& word, controls::Kind kind, bool off = false) {
        if (const Spot* s = spotOf(hit, index)) {
            controls::State st = state(hit, index);
            st.off = off;
            controls::button(canvas_, P(s->box), word, kind, st, u, kind == controls::Kind::Primary ? 0.0f : S(12.5f));
        }
    };
    const auto square = [&](Hit hit, int index, controls::Glyph glyph) {
        if (const Spot* s = spotOf(hit, index)) controls::square(canvas_, P(s->box), glyph, state(hit, index), u);
    };
    // A coin before a figure, as the canvas sets its prices: a small gold disc.
    const auto coin = [&](float ux, float midU, float r) {
        constexpr float kPi = 3.14159265f;
        float ring[32];
        const float cx = x + (ux + r) * k, cy = y + midU * k;
        for (int i = 0; i < 16; ++i) {
            ring[i * 2] = cx + std::cos(float(i) * kPi / 8.0f) * r * k;
            ring[i * 2 + 1] = cy + std::sin(float(i) * kPi / 8.0f) * r * k;
        }
        canvas_.polygon(nullptr, ring, nullptr, 16, kGold);
        return 2.0f * r + 3.0f;
    };
    const bool caret = std::fmod(clock_, 1.0) < 0.5;
    const int64_t funds = realm.money() + realm.vault().zen();

    // ---- the head ------------------------------------------------------------------------------
    const Box window{0.0f, 0.0f, W, kTall};
    controls::frame(canvas_, P(window), u, "", nullptr, kHead * k);
    controls::caps(canvas_, x + kPad * k, y + (controls::capsMiddle(y, kHead * k, S(15.0f)) - y), S(15.0f), style::kBoneHi,
                   "MARKET", 0.18f);
    square(Hit::Close, 0, controls::Glyph::Close);
    const char* tabs[3] = {"Shop", "Sell", "My listings"};
    for (int t = 0; t < 3; ++t) {
        if (const Spot* s = spotOf(Hit::Tab, t)) {
            controls::tab(canvas_, P(s->box), tabs[t], int(tab_) == t, state(Hit::Tab, t), u, false);
            if (t == 2 && !mine_.box.empty()) {
                // What waits, counted on the tab as the canvas's badge.
                const std::string n = std::to_string(mine_.box.size());
                const float bw = std::max(12.0f, wide(kCaption, n) + 7.0f);
                const Box badge{s->box.right() - bw - 2.0f, s->box.y + 3.0f, bw, 11.0f};
                tip::panel(canvas_, P(badge), badge.h * k * 0.5f, kGold, kGold);
                const float nw = controls::labelWidth(S(kCaption), n);
                controls::label(canvas_, std::round(x + badge.midX() * k - nw * 0.5f), y + mid(badge.y, badge.h, kCaption) * k,
                                S(kCaption), style::kVoid, n);
            }
        }
    }
    if (const Spot* s = spotOf(Hit::Search, 0)) {
        controls::field(canvas_, P(s->box), query_.text, "Search items", focus_ == Focus::Search, false,
                        focus_ == Focus::Search && caret, false, u);
    }
    if (spotOf(Hit::Collect, 0)) {
        // The head's Collect: the Primary with the coin going in, the Zen and things that wait.
        if (const Spot* s = spotOf(Hit::Collect, 0)) {
            controls::State st = state(Hit::Collect, 0);
            st.off = waiting_ != 0;
            controls::button(canvas_, P(s->box), market::collectWord(mine_.box), controls::Glyph::CoinIn, controls::Kind::Primary,
                             st, u, S(14.0f));
        }
    }
    {
        // His Zen, carried over the vault's, ranged against the close.
        const float rx = g_.zen.right();
        const float cw = right(rx, g_.zen.y + 9.5f, kName, kGold, market::commas(realm.money())) / k;
        coin(rx - cw - 11.0f, g_.zen.y + 5.5f, 4.0f);
        right(rx, g_.zen.y + 20.5f, kCaption, style::kAshInk, "Vault " + market::commas(realm.vault().zen()));
    }

    // ---- Shop ----------------------------------------------------------------------------------
    if (tab_ == Tab::Shop && !item_) {
        // The rail.
        int all = 0;
        const size_t kinds = size_t(sim::BrokerKind::Count);
        for (size_t i = 0; i < kinds && i < page_.kinds.size(); ++i) all += page_.kinds[i];
        const auto countOf = [&](int kind) {
            return kind == -1 ? all : (size_t(kind) < page_.kinds.size() ? int(page_.kinds[size_t(kind)]) : 0);
        };
        kicker(g_.rail.x, kCatTop - 6.0f, "Category");
        for (int i = 0; i < kCategories; ++i) {
            const int kind = i - 1;
            const Spot* s = spotOf(Hit::Kind, kind);
            if (!s) continue;
            const bool on = query_.kind == kind;
            const float lift = state(Hit::Kind, kind).lift;
            if (on) chosenWash(s->box);
            else {
                canvas_.rect(P(s->box), lift > 0.0f ? style::kAsh3 : style::kAsh1);
                canvas_.outline(P(s->box), std::max(1.0f, u), lift > 0.0f ? style::kIronLo : style::kIronDk);
            }
            const int count = countOf(kind);
            const float base = mid(s->box.y, s->box.h, kText);
            text(s->box.x + 7.0f, base, kText, on ? kGold : count > 0 || lift > 0.0f ? style::kBone : style::kAshInk,
                 kind == -1 ? "All items" : market::kKinds[kind]);
            right(s->box.right() - 6.0f, base, kCaption, on ? kGold : style::kAshInk, std::to_string(count));
        }
        kicker(g_.rail.x, kOptTop - 6.0f, "Options");
        const bool flags[kOptions] = {query_.usableBy >= 0, query_.excellentOnly, query_.sevenUp, query_.luckOnly, query_.socketOnly};
        for (int i = 0; i < kOptions; ++i) {
            if (const Spot* s = spotOf(Hit::Option, i)) chip(s->box, market::kOptionWords[i], flags[i], state(Hit::Option, i).lift, kCaption);
        }
        kicker(g_.rail.x, kPriceTop - 6.0f, "Price, millions");
        if (const Spot* s = spotOf(Hit::MinPrice, 0)) {
            controls::field(canvas_, P(s->box), minText_, "From", focus_ == Focus::MinPrice, false, focus_ == Focus::MinPrice && caret,
                            false, u);
        }
        if (const Spot* s = spotOf(Hit::MaxPrice, 0)) {
            controls::field(canvas_, P(s->box), maxText_, "To", focus_ == Focus::MaxPrice, false, focus_ == Focus::MaxPrice && caret,
                            false, u);
        }
        text(g_.minPrice.right() + 3.0f, mid(g_.minPrice.y, g_.minPrice.h, kText), kText, style::kAshInk, "\xE2\x80\x93");
        if (const Spot* s = spotOf(Hit::Clear, 0)) {
            text(s->box.x, mid(s->box.y, s->box.h, kText), kText, state(Hit::Clear, 0).lift > 0.0f ? style::kBoneHi : kGold,
                 "Clear every filter");
        }

        // The jewels' strip: the cheapest of each, its picture and its price against its last sale.
        controls::caps(canvas_, x + g_.strip.x * k, y + (g_.strip.y + 12.0f) * k, S(12.0f), style::kBoneHi, "Jewels", 0.12f);
        text(g_.strip.x, g_.strip.y + 23.0f, kCaption, style::kAshInk, "cheapest, each");
        for (int i = 0; i < kTicker; ++i) {
            const Box t = g_.ticker[i];
            const Spot* s = spotOf(Hit::Ticker, i);
            if (!s) continue;  // only the jewels on sale: no empty tiles
            pane(t, state(Hit::Ticker, i).lift > 0.0f ? style::kIron : style::kIronDk);
            const net::BrokerRow& row = ticker_[size_t(i)];
            const float side = 20.0f;
            standing.push_back({row.what.item, {t.x + 4.0f, t.y + (t.h - side) * 0.5f, side, side}, row.what.refinement, false,
                                row.what.excellent != 0});
            const float tx = t.x + side + 9.0f, room = t.right() - 4.0f - tx;
            text(tx, t.y + 11.0f, kCaption, inkOf(tables, row.what), fitted(nameOf(tables, row.what), kCaption, room));
            // A stack's price a piece, as the strip prices every jewel.
            const int64_t each = !(row.flags & net::BrokerRow::Alike) && stackOf(tables, row.what) ? row.price / row.what.durability : row.price;
            const float pw = text(tx, t.y + 23.0f, kText, style::kBoneHi, market::commas(each)) / k;
            if (row.lastSale > 0) {
                const int64_t pct = (each - row.lastSale) * 100 / row.lastSale;
                if (pct != 0 && tx + pw + 4.0f + wide(kCaption, "+99%") <= t.right() - 3.0f) {
                    text(tx + pw + 4.0f, t.y + 23.0f, kCaption, pct < 0 ? style::kLive : style::kAshInk,
                         (pct < 0 ? "\xE2\x88\x92" : "+") + std::to_string(std::llabs(pct)) + "%");
                }
            }
        }
        if (ticker_.empty()) {
            pane({g_.ticker[0].x, g_.strip.y, g_.strip.right() - g_.ticker[0].x, g_.strip.h}, style::kIronDk);
            text(g_.ticker[0].x + 10.0f, mid(g_.strip.y, g_.strip.h, kText), kText, style::kAshInk, "No jewels on sale right now.");
        }

        // The bar: the category, how many, the filters as chips, the sort.
        {
            const std::string title = query_.kind >= 0 ? market::kKinds[query_.kind] : "All items";
            const float base = mid(g_.bar.y, g_.bar.h, 16.0f);
            const float tw = text(g_.bar.x, base, 16.0f, style::kBoneHi, title) / k;
            text(g_.bar.x + tw + 8.0f, mid(g_.bar.y, g_.bar.h, kText), kText, style::kAshInk, std::to_string(page_.total) + " on sale");
            for (const Filter& f : filters()) {
                const Spot* s = spotOf(Hit::Unfilter, f.which);
                if (!s) continue;
                const float lift = state(Hit::Unfilter, f.which).lift;
                canvas_.rect(P(s->box), lift > 0.0f ? kGoldRim : kGoldWash);
                canvas_.outline(P(s->box), std::max(1.0f, u), kGoldRim);
                const float base2 = mid(s->box.y, s->box.h, kCaption);
                text(s->box.x + 6.0f, base2, kCaption, style::kBoneHi, f.word);
                right(s->box.right() - 6.0f, base2, kCaption, kGold, "\xC3\x97");
            }
            if (const Spot* s = spotOf(Hit::SortOpen, 0)) {
                const Box b = s->box;
                chip(b, "", sorting_, state(Hit::SortOpen, 0).lift);
                const float bb = mid(b.y, b.h, kText);
                const float lw = text(b.x + 7.0f, bb, kCaption, style::kAshInk, "Sort ") / k;
                text(b.x + 7.0f + lw, bb, kText, style::kBoneHi, market::kSortsOf[market::sortIndex(query_)].word);
                const float cx = x + (b.right() - 9.0f) * k, cy = y + b.midY() * k, r = 2.5f * k;
                const float down[6] = {cx - r, cy - r * 0.45f, cx + r, cy - r * 0.45f, cx, cy + r * 0.6f};
                canvas_.polygon(nullptr, down, nullptr, 3, style::kBone2);
            }
        }

        // The cards: a picture over its name, what it carries, its price against its last sale,
        // and at its foot the seller and Buy -- everything shown, nothing behind a hover.
        for (int i = 0; i < kColumns * kCardRows; ++i) {
            const net::BrokerRow* row = cardRow(i);
            if (!row) break;
            const Box c = g_.card(i);
            const bool alike = (row->flags & net::BrokerRow::Alike) != 0;
            const bool own = (row->flags & net::BrokerRow::Own) != 0;
            const bool asking = confirm_ != 0 && confirm_ == row->id;
            const bool prize = prized(tables, row->what);
            const float lift = state(Hit::Card, i).lift > 0.0f || state(Hit::CardBuy, i).lift > 0.0f ? 1.0f : 0.0f;
            canvas_.rect(P(c), lift > 0.0f ? style::kAsh2 : style::kAsh1);
            const uint32_t ink = inkOf(tables, row->what);
            // The picture's floor, its foot glowing faintly in the name's colour.
            const Box well{c.x + 1.0f, c.y + 1.0f, c.w - 2.0f, kCardPicture - 1.0f};
            const uint32_t lit = prize && ink == tip::colourOf(tip::Tone::White) ? kGold : ink;
            const uint32_t glow = (lit & 0x00FFFFFFu) | (uint32_t((prize ? 0.30f : 0.14f) * 255.0f) << 24), none = lit & 0x00FFFFFFu;
            canvas_.shade(P(well), style::kVoid, style::kVoid, style::kAsh2, style::kAsh2);
            canvas_.shade(P({well.x, well.y + well.h * 0.5f, well.w, well.h * 0.5f}), none, none, glow, glow);
            hair(c.x + 1.0f, c.y + kCardPicture, c.w - 2.0f, style::kIronDk);
            const float side = std::min(c.w - 20.0f, kCardPicture - 10.0f);
            standing.push_back({row->what.item, {c.midX() - side * 0.5f, c.y + (kCardPicture - side) * 0.5f, side, side},
                                row->what.refinement, false, row->what.excellent != 0});
            if (prize) tip::glyphAt(canvas_, tip::Mark::Star, x + (c.x + 9.0f) * k, y + (c.y + 9.0f) * k, 9.0f * k, kGold);
            const float tx = c.x + 7.0f, room = c.w - 14.0f;
            float ly = c.y + kCardPicture + 13.0f;
            text(tx, ly, kName, ink, fitted(nameOf(tables, row->what), kName, room));
            ly += 11.0f;
            const bool stack = !alike && stackOf(tables, row->what);
            const std::string sub = alike   ? std::to_string(row->count) + " on sale"
                                    : stack ? "\xC3\x97" + std::to_string(row->what.durability) + " \xC2\xB7 " +
                                                  market::commas(row->price / row->what.durability) + " each"
                                            : traits(row->what);
            if (!sub.empty()) text(tx, ly, kCaption, alike || stack ? style::kAshInk : tip::colourOf(tip::Tone::Blue), fitted(sub, kCaption, room));
            ly += 15.0f;
            float px = tx;
            if (alike) px += text(px, ly, kCaption, style::kAshInk, "from ") / k;
            px += coin(px, ly - 3.5f, 3.5f);
            text(px, ly, kName, style::kBoneHi, fitted(market::commas(row->price), kName, c.right() - 7.0f - px));
            ly += 11.0f;
            if (asking) {
                text(tx, ly, kCaption, kGold, fitted("Pay " + market::commas(row->price) + "?", kCaption, room));
            } else {
                // A stack against the last sale of one piece of it: its price a piece.
                const int64_t each = stack ? row->price / row->what.durability : row->price;
                const std::string vs = market::againstLast(each, row->lastSale);
                text(tx, ly, kCaption, vs.empty() ? style::kAshInk2 : each < row->lastSale ? style::kLive : style::kAshInk,
                     fitted(vs.empty() ? "New on the market" : vs, kCaption, room));
            }
            // The foot.
            const float footY = c.bottom() - kCardFoot;
            hair(c.x + 1.0f, footY, c.w - 2.0f, style::kIronDk);
            const Box buyBox = Geometry::buy(c);
            const float fb = mid(footY, kCardFoot, kCaption);
            if (asking) {
                canvas_.rect(P({c.x + 1.0f, footY + 1.0f, c.w - 2.0f, kCardFoot - 2.0f}), kGoldWash);
                square(Hit::CardBack, i, controls::Glyph::Close);
                button(Hit::CardBuy, i, "Confirm", controls::Kind::Primary);
            } else {
                const float room2 = (own ? c.right() - 6.0f : buyBox.x - 4.0f) - tx;
                if (alike) {
                    text(tx, fb, kCaption, style::kAshInk, fitted(std::to_string(row->tiers.size()) + " sellers", kCaption, room2));
                } else {
                    const bool elsewhere = (row->flags & net::BrokerRow::Elsewhere) != 0;
                    dot(tx, footY + kCardFoot * 0.5f, elsewhere || !(row->flags & net::BrokerRow::Online) ? kOffline : kOnline);
                    const std::string who = fitted(row->seller, kCaption, room2 * 0.6f);
                    const float sw = text(tx + 7.0f, fb, kCaption, style::kBone2, who) / k;
                    text(tx + 7.0f + sw + 4.0f, fb, kCaption, row->secondsLeft < 7200 ? style::kDanger : style::kAshInk,
                         fitted(market::hoursLeft(row->secondsLeft), kCaption, room2 - 11.0f - sw));
                }
                if (own) {
                    right(c.right() - 6.0f, fb, kCaption, style::kAshInk, "Yours");
                } else {
                    button(Hit::CardBuy, i, alike ? "Choose" : "Buy", controls::Kind::Secondary, waiting_ != 0 || row->price > funds);
                }
            }
            canvas_.outline(P(c), std::max(1.0f, u), asking ? kGold : prize ? kGoldRim : lift > 0.0f ? style::kIron : style::kIronLo);
        }
        if (page_.rows.empty()) {
            const Box gr = g_.grid;
            const bool filtered = !filters().empty() || query_.kind >= 0;
            const std::string say = filtered ? "Nothing on sale matches." : "Nothing is on sale yet.";
            text(gr.midX() - wide(kName, say) * 0.5f, gr.y + gr.h * 0.42f, kName, style::kBoneHi, say);
            if (filtered) {
                const std::string hint = "Fewer filters or another word may find it.";
                text(gr.midX() - wide(kText, hint) * 0.5f, gr.y + gr.h * 0.42f + 16.0f, kText, style::kAshInk, hint);
            }
        }
        // The pager.
        {
            const int per = kColumns * kCardRows;
            const int pages = market::pagesOf(page_.total, per);
            const int first = int(page_.page) * per;
            const std::string showing = page_.total == 0 ? ""
                                                         : "Showing " + std::to_string(first + 1) + "\xE2\x80\x93" +
                                                               std::to_string(first + int(page_.rows.size())) + " of " +
                                                               std::to_string(page_.total);
            text(g_.pager.x, mid(g_.pager.y, g_.pager.h, kText), kText, style::kAshInk, showing);
            right(g_.prev.x - 8.0f, mid(g_.pager.y, g_.pager.h, kText), kText, style::kBone2,
                  "Page " + std::to_string(page_.page + 1) + " of " + std::to_string(pages));
            square(Hit::Prev, 0, controls::Glyph::Left);
            square(Hit::Next, 0, controls::Glyph::Right);
        }
        // The shop's pictures, stood on a stage the window's size and drawn over the cards' floors.
        standKey = 1;
        standW = W, standH = kTall;
        if (turned.valid() && stood_ == keyOf(standKey)) canvas_.image(turned, P(window));
        // The open sort list, over the grid under its button.
        if (sorting_) {
            const Box sheet{g_.sort.x, g_.sort.bottom() + 1.0f, g_.sort.w, float(kSorts) * 16.0f + 3.0f};
            canvas_.rect(P(sheet), style::kAsh1);
            canvas_.outline(P(sheet), std::max(1.0f, u), style::kIron);
            for (int i = 0; i < kSorts; ++i) {
                const Spot* s = spotOf(Hit::Sort, i);
                if (!s) continue;
                const bool on = market::sortIndex(query_) == i;
                if (on) canvas_.rect(P(s->box), kGoldWash);
                else if (state(Hit::Sort, i).lift > 0.0f) canvas_.rect(P(s->box), kHover);
                text(s->box.x + 7.0f, mid(s->box.y, s->box.h, kText), kText, on ? kGold : style::kBone2, market::kSortsOf[i].word);
            }
        }
    }

    // ---- An item's page ------------------------------------------------------------------------
    if (tab_ == Tab::Shop && item_) {
        if (const Spot* s = spotOf(Hit::Back, 0)) {
            controls::button(canvas_, P(s->box), "Back to results", controls::Glyph::Left, controls::Kind::Secondary,
                             state(Hit::Back, 0), u, S(12.5f));
        }
        if (const Spot* s = spotOf(Hit::Path, 0); s && size_t(open_.what.item) < tables.items.size()) {
            const int kind = std::clamp(int(sim::brokerKind(tables.items[size_t(open_.what.item)])), 0, int(sim::BrokerKind::Count) - 1);
            const float base = mid(s->box.y, s->box.h, kText);
            float px = s->box.x;
            px += text(px, base, kText, state(Hit::Path, 0).lift > 0.0f ? style::kBoneHi : kGold, market::kKinds[kind]) / k;
            text(px + 4.0f, base, kText, style::kAshInk, "\xE2\x80\xBA  " + nameOf(tables, open_.what));
        }
        const bool alike = (open_.flags & net::BrokerRow::Alike) != 0;
        const bool own = (open_.flags & net::BrokerRow::Own) != 0;
        // The thing in full: the picture large and turning, its lines beside and under it.
        thingCard(open_.what, g_.info.x, g_.info.y, g_.info.w, 150.0f, alike ? std::to_string(open_.count) + " on sale" : "",
                  g_.info.bottom());

        // The buy box.
        const Box& b = g_.buyBox;
        pane(b);
        const float bx = b.x + 12.0f, bw = b.w - 24.0f;
        const int n = alike ? market::clampQty(qty_, open_) : 1;
        const int64_t cost = market::costOf(open_, n);
        text(bx, b.y + 18.0f, kCaption, style::kAshInk, alike ? "Cheapest, each" : "Price");
        {
            const float cw = coin(bx, b.y + 36.0f, 5.0f);
            text(bx + cw, b.y + 42.0f, 22.0f, style::kBoneHi, market::commas(open_.price));
        }
        {
            const int64_t each = !alike && stackOf(tables, open_.what) ? open_.price / open_.what.durability : open_.price;
            const std::string vs = market::againstLast(each, open_.lastSale);
            text(bx, b.y + 56.0f, kCaption, vs.empty() ? style::kAshInk2 : each < open_.lastSale ? style::kLive : style::kAshInk,
                 vs.empty() ? "Not sold before" : fitted(vs + " \xC2\xB7 last " + market::commas(open_.lastSale) + (each != open_.price ? " each" : ""), kCaption, bw));
        }
        if (alike) {
            square(Hit::Qty, -1, controls::Glyph::Left);
            if (const Spot* s = spotOf(Hit::Qty, -1)) {
                const std::string nn = std::to_string(n);
                const float cx = s->box.right() + 17.0f;
                text(cx - wide(kName, nn) * 0.5f, mid(s->box.y, s->box.h, kName), kName, style::kBoneHi, nn);
            }
            square(Hit::Qty, 1, controls::Glyph::Right);
            button(Hit::MostQty, 0, "Most I can pay", controls::Kind::Secondary);
            // The ladder, cheapest first, the tiers this many takes lit in gold.
            float ly = b.y + 92.0f;
            int left = n;
            for (const net::BrokerRow::Tier& t : open_.tiers) {
                if (ly + 16.0f > b.bottom() - 70.0f) break;
                const int take = std::min(left, t.count);
                left -= take;
                const Box tier{bx, ly, bw, 16.0f};
                if (take > 0) chosenWash(tier);
                const float base = mid(tier.y, tier.h, kText);
                text(tier.x + 4.0f, base, kText, take > 0 ? kGold : style::kBone2,
                     (take > 0 ? std::to_string(take) : std::to_string(t.count)) + " \xC3\x97 " + market::commas(t.price));
                right(tier.right() - 4.0f, base, kCaption, take > 0 ? style::kBone : style::kAshInk, fitted(t.seller, kCaption, bw * 0.4f));
                ly += 18.0f;
            }
            text(bx, b.bottom() - 54.0f, kText, style::kBone2, "Total");
            right(b.right() - 12.0f, b.bottom() - 54.0f, kName, kGold, cost < 0 ? "\xE2\x80\x94" : market::commas(cost));
        } else {
            hair(bx, b.y + 66.0f, bw, style::kIronDk);
            const bool elsewhere = (open_.flags & net::BrokerRow::Elsewhere) != 0;
            const bool online = (open_.flags & net::BrokerRow::Online) != 0;
            text(bx, b.y + 82.0f, kCaption, style::kAshInk, "Sold by");
            dot(bx, b.y + 93.5f, !elsewhere && online ? kOnline : kOffline);
            text(bx + 8.0f, b.y + 97.0f, kName, style::kBoneHi, own ? open_.seller + " (you)" : open_.seller);
            text(bx, b.y + 111.0f, kCaption, open_.secondsLeft < 7200 ? style::kDanger : style::kAshInk,
                 std::string(elsewhere ? "Another realm" : online ? "Online" : "Offline") + " \xC2\xB7 ends in " +
                     market::hoursLeft(open_.secondsLeft));
            if (const Spot* s = spotOf(Hit::More, 0)) {
                text(s->box.x, s->box.y + 10.0f, kText, state(Hit::More, 0).lift > 0.0f ? style::kBoneHi : kGold,
                     fitted("More from " + open_.seller, kText, bw));
            }
        }
        if (own) {
            text(bx, b.bottom() - 26.0f, kText, style::kAshInk, "Your own listing.");
        } else {
            const bool asking = confirm_ != 0 && confirm_ == open_.id;
            const bool short_ = cost < 0 || cost > funds;
            if (asking) button(Hit::BuyBack, 0, "Back", controls::Kind::Secondary);
            const std::string word = short_ ? "Not enough Zen"
                                     : asking ? "Confirm " + market::commas(cost)
                                     : alike  ? "Buy " + std::to_string(n) + " for " + market::commas(cost)
                                              : "Buy now";
            button(Hit::Buy, 0, word, controls::Kind::Primary, waiting_ != 0 || short_);
            const int64_t carried = realm.money();
            const int64_t fromVault = std::max<int64_t>(0, cost - carried);
            text(bx, b.bottom() - 8.0f, kCaption, style::kAshInk,
                 fitted(short_ ? "Carried and vault Zen together fall short."
                               : fromVault > 0 ? market::commas(fromVault) + " of it from the vault"
                                               : "Paid from the Zen you carry",
                        kCaption, bw));
        }

        // Other offers, or the ladder's sellers for alike things; and its last sales.
        const Box& o = g_.offers;
        pane(o);
        kicker(o.x + 10.0f, o.y + 14.0f, alike ? "Every seller, cheapest first" : "Other offers, cheapest first");
        const int rows = std::max(0, int((o.h - 26.0f) / 20.0f));
        if (alike) {
            for (int i = 0; i < rows && size_t(i) < open_.tiers.size(); ++i) {
                const net::BrokerRow::Tier& t = open_.tiers[size_t(i)];
                const float ry = o.y + 24.0f + float(i) * 20.0f, base = mid(ry, 20.0f, kText);
                hair(o.x + 8.0f, ry, o.w - 16.0f, style::kAsh3);
                text(o.x + 10.0f, base, kText, style::kBone2, t.seller);
                right(o.right() - 120.0f, base, kText, style::kAshInk, std::to_string(t.count) + " on sale");
                right(o.right() - 10.0f, base, kName, kGold, market::commas(t.price));
            }
        } else {
            for (int i = 0; i < rows && size_t(i) < offers_.size(); ++i) {
                const net::BrokerRow& r = offers_[size_t(i)];
                const float ry = o.y + 24.0f + float(i) * 20.0f, base = mid(ry, 20.0f, kText);
                hair(o.x + 8.0f, ry, o.w - 16.0f, style::kAsh3);
                const float nw = text(o.x + 10.0f, base, kText, inkOf(tables, r.what),
                                      fitted(nameOf(tables, r.what), kText, (o.w - 300.0f) * 0.5f)) / k;
                const std::string t = traits(r.what);
                if (!t.empty()) text(o.x + 16.0f + nw, base, kCaption, tip::colourOf(tip::Tone::Blue), fitted(t, kCaption, o.w * 0.25f));
                text(o.right() - 250.0f, base, kText, style::kBone2, fitted(r.seller, kText, 70.0f));
                right(o.right() - 128.0f, base, kText, style::kAshInk, market::hoursLeft(r.secondsLeft));
                right(o.right() - 62.0f, base, kName, kGold, market::commas(r.price));
                button(Hit::Offer, i, "View", controls::Kind::Secondary);
            }
            if (offers_.empty()) text(o.x + 10.0f, o.y + 38.0f, kText, style::kAshInk, "No one else sells this now.");
        }
        const Box& h = g_.history;
        pane(h);
        kicker(h.x + 10.0f, h.y + 14.0f, "Last sales");
        if (sales_.empty()) {
            text(h.x + 10.0f, h.y + 38.0f, kText, style::kAshInk, "Not sold yet. The first sale sets the going price.");
        } else {
            // Bars, the oldest at the left, against the dearest; the newest lit.
            int64_t lo = sales_.front().price, hi = lo;
            for (const sim::Sale& sale : sales_) lo = std::min(lo, sale.price), hi = std::max(hi, sale.price);
            const Box chart{h.x + 10.0f, h.y + 22.0f, h.w - 20.0f, h.h - 48.0f};
            hair(chart.x, chart.bottom(), chart.w, style::kIronDk);
            const int nsales = int(sales_.size());
            const float bwid = std::min(18.0f, (chart.w - float(nsales - 1) * 3.0f) / float(nsales));
            for (int i = 0; i < nsales; ++i) {
                const sim::Sale& sale = sales_[size_t(nsales - 1 - i)];
                const float share = hi > 0 ? 0.25f + 0.75f * float(sale.price) / float(hi) : 1.0f;
                const float bh = std::max(2.0f, chart.h * share);
                canvas_.rect(P({chart.x + float(i) * (bwid + 3.0f), chart.bottom() - bh, bwid, bh}),
                             i == nsales - 1 ? kGold : kGoldRim);
            }
            text(h.x + 10.0f, h.bottom() - 9.0f, kCaption, style::kAshInk, "Low " + market::commas(lo));
            right(h.right() - 10.0f, h.bottom() - 9.0f, kCaption, style::kBone2, "Last " + market::commas(sales_.front().price));
        }
    }

    // ---- Sell ----------------------------------------------------------------------------------
    if (tab_ == Tab::Sell) {
        pane(g_.things);
        kicker(g_.things.x + 8.0f, g_.things.y + 13.0f, "Choose what to sell");
        int inBag = 0, inVault = 0;
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            inBag += !realm.satchel()[slot].empty() && sim::brokerable(tables, realm.satchel()[slot]) == sim::BrokerNo::Yes;
        }
        for (int cell = 0; cell < sim::kVaultCells; ++cell) {
            inVault += !realm.vault()[cell].empty() && sim::brokerable(tables, realm.vault()[cell]) == sim::BrokerNo::Yes;
        }
        if (const Spot* s = spotOf(Hit::Source, 0)) chip(s->box, "Bag \xC2\xB7 " + std::to_string(inBag), !fromVault_, state(Hit::Source, 0).lift, kCaption);
        if (const Spot* s = spotOf(Hit::Source, 1)) chip(s->box, "Vault \xC2\xB7 " + std::to_string(inVault), fromVault_, state(Hit::Source, 1).lift, kCaption);
        for (const Spot& s : spots_) {
            if (s.hit != Hit::Thing) continue;
            const Thing& t = things_[size_t(s.index)];
            const bool ok = t.no == sim::BrokerNo::Yes;
            const Box b{s.box.x + 4.0f, s.box.y, s.box.w - 8.0f, s.box.h};
            const float lift = state(Hit::Thing, s.index).lift;
            if (ok && t.slot == sellSlot_) chosenWash(b);
            else {
                canvas_.rect(P(b), ok && lift > 0.0f ? style::kAsh3 : style::kAsh1);
                canvas_.outline(P(b), std::max(1.0f, u), ok ? (lift > 0.0f ? style::kIronLo : style::kIronDk) : style::kAsh3);
            }
            const float base = mid(b.y, b.h, kText);
            const std::string sub = ok ? traits(t.what) : whyNot(t.no);
            const float subW = sub.empty() ? 0.0f : wide(kCaption, sub) + 8.0f;
            text(b.x + 7.0f, base, kText, ok ? inkOf(tables, t.what) : style::kAshInk2,
                 fitted(nameOf(tables, t.what) + (stackOf(tables, t.what) ? " \xC3\x97" + std::to_string(t.what.durability) : ""),
                        kText, b.w - 14.0f - subW));
            if (!sub.empty()) right(b.right() - 7.0f, base, kCaption, ok ? tip::colourOf(tip::Tone::Blue) : style::kDanger, sub);
        }
        if (things_.empty()) {
            text(g_.things.x + 10.0f, g_.things.y + 44.0f, kText, style::kAshInk, fromVault_ ? "Your vault is empty." : "Your bag is empty.");
        }
        square(Hit::ThingPrev, 0, controls::Glyph::Left);
        square(Hit::ThingNext, 0, controls::Glyph::Right);
        if (!spotOf(Hit::ThingPrev, 0)) text(g_.things.x + 8.0f, kBottom - 6.0f, kCaption, style::kAshInk, "Or drag one here from your bag.");

        const Box& f = g_.form;
        pane(f);
        const float fx = f.x + 14.0f, fw = f.w - 28.0f, half = (fw - 16.0f) * 0.5f, rx = fx + half + 16.0f;
        if (sellSlot_ < 0 || sellSeen_.empty()) {
            text(fx, f.y + 40.0f, kName, style::kBoneHi, "Choose something to sell");
            text(fx, f.y + 58.0f, kText, style::kAshInk, "Pick it from the list, or drag it here from your bag.");
        } else {
            const sim::Held part = sellPart(play);
            thingCard(part, fx, f.y + 12.0f, fw, 52.0f, fromVault_ ? "From your vault" : "From your bag", f.y + 12.0f);
            hair(fx, f.y + 82.0f, fw, style::kIronDk);
            const bool quoted = quote_.item == sellSeen_.item;
            const int64_t last = quoted && !quote_.sales.empty() ? quote_.sales.front().price : 0;
            const int64_t cheapest = quoted ? quote_.cheapest : 0;
            const int pieces = sellPieces(play);
            kicker(fx, f.y + 98.0f, pieces > 1 ? "Your price for " + std::to_string(pieces) : "Your price");
            if (const Spot* s = spotOf(Hit::Price, 0)) {
                const std::string shown = priceText_.empty() ? "" : market::commas(priceTyped());
                const int64_t floor = std::max<int64_t>(1, sim::npcValue(tables, part));
                const market::Verdict v = market::verdict(priceTyped(), floor, cheapest, quoted ? quote_.onSale : 0);
                const bool bad = v.says == market::Verdict::Says::Low || v.says == market::Verdict::Says::High;
                controls::field(canvas_, P(s->box), shown, "0", focus_ == Focus::Price, bad, focus_ == Focus::Price && caret, false, u);
                std::string line;
                uint32_t ink = style::kAshInk;
                switch (v.says) {
                    case market::Verdict::Says::None: line = "Set a price."; break;
                    case market::Verdict::Says::Low: line = "At least " + market::commas(floor) + ", what a merchant pays."; ink = style::kDanger; break;
                    case market::Verdict::Says::High: line = "At most 2,000,000,000."; ink = style::kDanger; break;
                    case market::Verdict::Says::Only: line = "No one else sells this now: yours is the only one."; ink = style::kLive; break;
                    case market::Verdict::Says::Cheapest:
                        line = "Cheapest of " + std::to_string(quote_.onSale + 1) + " on sale: buyers see yours first.";
                        ink = style::kLive;
                        break;
                    case market::Verdict::Says::Behind: line = "Cheaper ones are on sale: buyers see those first."; ink = style::kBone2; break;
                }
                dot(fx, f.y + 140.5f, ink);
                text(fx + 8.0f, f.y + 144.0f, kText, ink, fitted(line, kText, half - 8.0f));
            }
            kicker(rx, f.y + 98.0f, "Price help, click to use");
            const std::string help[2][2] = {{"Cheapest on sale", cheapest > 0 ? market::commas(cheapest) : "None on sale"},
                                            {"Last sold for", last > 0 ? market::commas(last) : "Never sold"}};
            for (int i = 0; i < 2; ++i) {
                const Spot* s = spotOf(Hit::Help, i);
                if (!s) continue;
                const bool has = i == 0 ? cheapest > 0 : last > 0;
                const int64_t priceNow = priceTyped();
                const bool on = has && priceNow == (i == 0 ? cheapest : last);
                chip(s->box, "", on, has ? state(Hit::Help, i).lift : 0.0f);
                text(s->box.x + 7.0f, s->box.y + 11.0f, kCaption, style::kAshInk, help[i][0]);
                text(s->box.x + 7.0f, s->box.y + 24.0f, kName, has ? (on ? kGold : style::kBoneHi) : style::kAshInk2,
                     fitted(help[i][1], kName, s->box.w - 14.0f));
            }
            text(rx, f.y + 148.0f, kCaption, style::kAshInk,
                 quoted ? std::to_string(quote_.onSale) + " on sale now from other sellers" : "Asking the Broker\xE2\x80\xA6");
            if (const Spot* s = spotOf(Hit::Pieces, -1)) {
                kicker(fx, f.y + 172.0f, "How many, of " + std::to_string(sellSeen_.durability));
                controls::square(canvas_, P(s->box), controls::Glyph::Left, state(Hit::Pieces, -1), u);
                const std::string nn = std::to_string(pieces);
                text(s->box.right() + 17.0f - wide(kName, nn) * 0.5f, mid(s->box.y, s->box.h, kName), kName, style::kBoneHi, nn);
                square(Hit::Pieces, 1, controls::Glyph::Right);
                button(Hit::AllPieces, 0, "All " + std::to_string(sellSeen_.durability), controls::Kind::Secondary,
                       pieces >= sellSeen_.durability);
            }
            kicker(fx, f.y + 214.0f, "On sale for");
            for (int h = 0; h < 3; ++h) {
                if (const Spot* s = spotOf(Hit::Hours, h)) {
                    chip(s->box, std::to_string(sim::kBrokerHours[h]) + " hours", h == hours_, state(Hit::Hours, h).lift);
                }
            }
            // What listing it means, over the button: what he gets, free, and his twenty.
            const Box sum{fx, f.bottom() - 62.0f, fw, 50.0f};
            canvas_.rect(P(sum), style::kAsh0);
            canvas_.outline(P(sum), std::max(1.0f, u), style::kIronDk);
            const int64_t asked = priceTyped();
            text(sum.x + 12.0f, sum.y + 17.0f, kCaption, style::kAshInk, "When it sells you get");
            const float cw = coin(sum.x + 12.0f, sum.y + 33.0f, 5.0f);
            text(sum.x + 12.0f + cw, sum.y + 39.0f, 20.0f, kGold, market::commas(std::max<int64_t>(0, asked)));
            const float tail = sum.x + 12.0f + cw + wide(20.0f, market::commas(std::max<int64_t>(0, asked))) + 10.0f;
            text(tail, sum.y + 38.0f, kCaption, style::kAshInk, "No fee. It waits in Collect.");
            const int64_t floor = std::max<int64_t>(1, sim::npcValue(tables, part));
            const bool full = mine_.accountCount >= sim::kBrokerListings;
            const market::Verdict v = market::verdict(asked, floor, cheapest, quoted ? quote_.onSale : 0);
            button(Hit::List, 0, full ? "All 20 listings in use" : "List it", controls::Kind::Primary, waiting_ != 0 || !v.listable || full);
            if (const Spot* s = spotOf(Hit::List, 0)) {
                right(s->box.right(), sum.y + 46.0f, kCaption, style::kAshInk,
                      std::to_string(mine_.accountCount) + " of 20 listings in use");
            }
        }
    }

    // ---- My listings ---------------------------------------------------------------------------
    if (tab_ == Tab::Mine) {
        int64_t zen = 0;
        int things = 0, days = 30;
        for (const net::BrokerParcel& p : mine_.box) {
            zen += p.zen;
            things += p.what.empty() ? 0 : 1;
            days = std::min(days, std::max(1, p.secondsLeft / 86400));
        }
        const Box t0 = g_.tiles[0];
        if (mine_.box.empty()) {
            pane(t0);
            kicker(t0.x + 12.0f, t0.y + 16.0f, "Nothing waits");
            text(t0.x + 12.0f, t0.y + 34.0f, kText, style::kAshInk, "What sells, and what comes back, waits here 30 days.");
        } else {
            chosenWash(t0);
            kicker(t0.x + 12.0f, t0.y + 14.0f, "Waiting to collect \xC2\xB7 " + std::to_string(mine_.box.size()));
            // The Zen as its figure, or with none waiting, how many things.
            const std::string items = std::to_string(things) + (things == 1 ? " item" : " items");
            if (zen > 0) {
                const float cw = coin(t0.x + 12.0f, t0.y + 25.0f, 4.5f);
                text(t0.x + 12.0f + cw, t0.y + 30.0f, 17.0f, style::kBoneHi, market::commas(zen));
            } else {
                text(t0.x + 12.0f, t0.y + 30.0f, 17.0f, style::kBoneHi, items);
            }
            text(t0.x + 12.0f, t0.y + 41.0f, kCaption, style::kAshInk,
                 fitted((zen > 0 && things > 0 ? "and " + items + " \xC2\xB7 " : std::string()) + "the oldest has " + std::to_string(days) +
                            (days == 1 ? " day left" : " days left"),
                        kCaption, g_.collectAll.x - 8.0f - (t0.x + 12.0f)));
            button(Hit::CollectAll, 0, "Collect all", controls::Kind::Primary, waiting_ != 0);
        }
        int64_t worth = 0;
        for (const net::BrokerRow& r : mine_.mine) worth += r.price;
        const Box t1 = g_.tiles[1];
        pane(t1);
        kicker(t1.x + 12.0f, t1.y + 14.0f, "On sale");
        {
            const float vw = text(t1.x + 12.0f, t1.y + 31.0f, 17.0f, style::kBoneHi, std::to_string(mine_.accountCount)) / k;
            text(t1.x + 16.0f + vw, t1.y + 31.0f, kText, style::kAshInk, "of 20 for the account");
            text(t1.x + 12.0f, t1.y + 42.0f, kCaption, style::kAshInk, market::commas(worth) + " Zen at your prices");
        }
        int64_t soldZen = 0;
        for (const sim::Sale& sale : mine_.soldLately) soldZen += sale.price;
        const Box t2 = g_.tiles[2];
        pane(t2);
        kicker(t2.x + 12.0f, t2.y + 14.0f, "Sold lately");
        {
            const float vw = text(t2.x + 12.0f, t2.y + 31.0f, 17.0f, style::kBoneHi, std::to_string(mine_.soldLately.size())) / k;
            text(t2.x + 16.0f + vw, t2.y + 31.0f, kText, style::kAshInk, mine_.soldLately.size() == 1 ? "item" : "items");
            text(t2.x + 12.0f, t2.y + 42.0f, kCaption, style::kAshInk, market::commas(soldZen) + " Zen in all");
        }

        // His listings.
        const Box& m = g_.mine;
        pane(m);
        const float leftAt = m.right() - 300.0f, priceAt = m.right() - 122.0f;
        const float headBase = m.y + 13.0f;
        kicker(m.x + 10.0f, headBase, "On sale");
        right(leftAt, headBase, kCaption, style::kAshInk, "Ends");
        right(priceAt, headBase, kCaption, style::kAshInk, "Your price");
        hair(m.x + 1.0f, m.y + 19.0f, m.w - 2.0f, style::kIronDk);
        const int first = minePage_ * kMineRows;
        for (int r = 0; r < kMineRows && first + r < int(mine_.mine.size()); ++r) {
            const int i = first + r;
            const net::BrokerRow& row = mine_.mine[size_t(i)];
            const Box rb = g_.mineRow(r);
            const float base = mid(rb.y, rb.h, kText);
            hair(m.x + 1.0f, rb.bottom() - 1.0f, m.w - 2.0f, style::kAsh3);
            if (state(Hit::Listing, i).lift > 0.0f) canvas_.rect(P({rb.x + 1.0f, rb.y, rb.w - 300.0f, rb.h - 1.0f}), kHover);
            const float nw = text(m.x + 10.0f, base, kText, inkOf(tables, row.what), fitted(nameOf(tables, row.what), kText, leftAt - 80.0f - m.x)) / k;
            const std::string sub = traits(row.what);
            if (!sub.empty() && m.x + 16.0f + nw + wide(kCaption, sub) < leftAt - 40.0f) {
                text(m.x + 16.0f + nw, base, kCaption, tip::colourOf(tip::Tone::Blue), sub);
            }
            right(leftAt, base, kText, row.secondsLeft < 7200 ? style::kDanger : style::kAshInk, market::hoursLeft(row.secondsLeft));
            if (row.id == editing_) {
                if (const Spot* s = spotOf(Hit::LowerField, i)) {
                    controls::field(canvas_, P(s->box), lowerText_.empty() ? "" : market::commas(market::parsed(lowerText_)), "",
                                    focus_ == Focus::Lower, false, focus_ == Focus::Lower && caret, false, u);
                }
                button(Hit::Save, i, "Save", controls::Kind::Secondary);
                button(Hit::Keep, i, "Back", controls::Kind::Quiet);
            } else {
                right(priceAt, base, kName, kGold, market::commas(row.price));
                button(Hit::Lower, i, "Lower", controls::Kind::Secondary);
                button(Hit::Cancel, i, "Cancel", controls::Kind::Quiet);
            }
        }
        if (mine_.mine.empty()) text(m.x + 10.0f, m.y + 40.0f, kText, style::kAshInk, "Nothing on sale. List something from Sell.");
        if (spotOf(Hit::MinePrev, 0)) {
            const int pages = market::pagesOf(int64_t(mine_.mine.size()), kMineRows);
            right(g_.minePrev.x - 8.0f, mid(g_.minePrev.y, g_.minePrev.h, kText), kText, style::kBone2,
                  "Page " + std::to_string(minePage_ + 1) + " of " + std::to_string(pages));
        }
        square(Hit::MinePrev, 0, controls::Glyph::Left);
        square(Hit::MineNext, 0, controls::Glyph::Right);
        text(m.x + 10.0f, kBottom - 9.0f, kCaption, style::kAshInk, "Lowering is free. To raise a price, cancel and list again.");

        // What waits, then what sold.
        const Box& sd = g_.sold;
        pane(sd);
        kicker(sd.x + 10.0f, sd.y + 13.0f, "Collect and sold");
        float ly = sd.y + 22.0f;
        for (const net::BrokerParcel& p : mine_.box) {
            if (ly + 28.0f > sd.bottom()) break;
            const Box rb{sd.x + 4.0f, ly, sd.w - 8.0f, 26.0f};
            canvas_.rect(P(rb), kGoldWash);
            const std::string what = p.what.empty() ? "Zen" : nameOf(tables, p.what);
            text(rb.x + 6.0f, rb.y + 11.0f, kText, p.what.empty() ? style::kBoneHi : inkOf(tables, p.what), fitted(what, kText, rb.w - 100.0f));
            const char* why = p.why == uint8_t(sim::Parcel::Why::Sold)        ? "Sold \xC2\xB7 to collect"
                              : p.why == uint8_t(sim::Parcel::Why::Cancelled) ? "Cancelled \xC2\xB7 to collect"
                              : p.why == uint8_t(sim::Parcel::Why::Bought)    ? "Bought, bag full \xC2\xB7 to collect"
                                                                               : "Came back unsold \xC2\xB7 to collect";
            text(rb.x + 6.0f, rb.y + 22.0f, kCaption, kGold, why);
            if (p.zen > 0) right(rb.right() - 6.0f, rb.y + 11.0f, kText, style::kBoneHi, "+" + market::commas(p.zen));
            ly += 28.0f;
        }
        for (const sim::Sale& sale : mine_.soldLately) {
            if (ly + 28.0f > sd.bottom()) break;
            text(sd.x + 10.0f, ly + 11.0f, kText, inkOf(tables, sale.what), fitted(nameOf(tables, sale.what), kText, sd.w - 110.0f));
            right(sd.right() - 10.0f, ly + 11.0f, kText, kGold, "+" + market::commas(sale.price));
            text(sd.x + 10.0f, ly + 22.0f, kCaption, style::kAshInk, ago(std::max<int64_t>(0, realm.wallClock() - sale.at)));
            hair(sd.x + 8.0f, ly + 27.0f, sd.w - 16.0f, style::kAsh3);
            ly += 28.0f;
        }
        if (mine_.box.empty() && mine_.soldLately.empty()) text(sd.x + 10.0f, sd.y + 40.0f, kText, style::kAshInk, "Nothing yet.");
    }

    // What the Broker just said, or that he is looking: one line over the window's foot.
    {
        std::string line;
        uint32_t ink = style::kBone2, edge = style::kIron;
        if (saidFor_ > 0.0f && !said_.empty()) {
            line = said_;
            ink = saidBad_ ? style::kDanger : style::kLiveHi;
            edge = saidBad_ ? style::kDanger : kGoldRim;
        } else if (waiting_ != 0) {
            line = "The Broker is looking\xE2\x80\xA6";
        }
        if (!line.empty()) {
            const float tw = wide(kText, line) + 28.0f;
            // In the window's foot under every pane, so it covers no figure (it sat over Sell's total).
            const Box b{W * 0.5f - tw * 0.5f, kBottom - 9.0f, tw, 18.0f};
            canvas_.rect(P(b), style::kAsh3);
            canvas_.outline(P(b), std::max(1.0f, u), edge);
            text(b.x + 14.0f, mid(b.y, b.h, kText), kText, ink, line);
        }
    }

    // The stage: the shop's still pictures at their places on a window-sized stage, or one
    // thing turning on a stage no bigger than its square (a window-sized stage redrawn every
    // frame halved the frame rate).
    if (stage) {
        if (standing.empty()) standKey = 0, standW = standH = 1.0f;
        stage->stand(standing, standW, standH);
        stood_ = keyOf(standKey);
    }

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
