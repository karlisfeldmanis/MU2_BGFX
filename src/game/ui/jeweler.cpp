#include "game/ui/jeweler.h"

#include <algorithm>
#include <cmath>

#include "game/ui/controls.h"
#include "game/ui/describe.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"
#include "sim/jeweler.h"

namespace mu::game {
namespace {

using gfx::Box;

constexpr float kWide = panel::kWidth;
constexpr float kLeft = panel::kEdge, kRight = kWide - panel::kEdge;
constexpr float kInner = kRight - kLeft;
constexpr float kText = 7.6f;  // a line, in units
constexpr float kLine = 11.5f;

// The tabs under the head.
constexpr float kTabTop = 40.0f, kTabH = 18.0f;
Box tabBox(int i) { return {kLeft + float(i) * kInner / 3.0f, kTabTop, kInner / 3.0f, kTabH}; }

// The pictures stand on a stage from the slots' row down past the prizes'.
constexpr float kStageX = kLeft, kStageY = 66.0f;
constexpr float kStageW = kInner, kStageH = 96.0f;
constexpr float kSlot = 30.0f;
Box slotBox(int i) { return {kStageX + float(i) * (kSlot + 6.0f), kStageY, kSlot, kSlot}; }
// Salvage's five prizes, a row of pictures under the kicker.
constexpr float kPrizeTop = 124.0f, kPrize = 28.0f;
// The jewel stands inside its cell with this much air round it, not edge to edge (the user,
// 2026-10-20: "on salvage work on paddings for jewel containers").
constexpr float kPrizePad = 5.0f;
Box prizeBox(int i) { return {kLeft + float(i) * (kInner / 5.0f) + (kInner / 5.0f - kPrize) * 0.5f, kPrizeTop, kPrize, kPrize}; }
Box onStage(const Box& b) { return {b.x - kStageX, b.y - kStageY, b.w, b.h}; }

// Recut's rows: the signature, then the three further lines.
constexpr float kRowTop = 118.0f, kRowH = 19.0f, kRowGap = 3.0f;
Box rowBox(int i) { return {kLeft, kRowTop + float(i + 1) * (kRowH + kRowGap), kInner, kRowH}; }

// What was said, over the foot; the foot's cost and buttons.
constexpr float kSaidTop = 316.0f;
constexpr float kNeedTop = 350.0f, kCostTop = 364.0f;
constexpr float kButtonTop = panel::kFootTop + 2.0f, kButtonH = 22.0f;
constexpr Box kRunBox{kRight - 78.0f, kButtonTop, 78.0f, kButtonH};
constexpr Box kYesBox = kRunBox;
constexpr Box kNoBox{kRunBox.x - 5.0f - 52.0f, kButtonTop, 52.0f, kButtonH};

constexpr uint32_t kGold = gfx::rgba(0.722f, 0.600f, 0.353f);
constexpr uint32_t kGoldHi = gfx::rgba(0.886f, 0.776f, 0.541f);
constexpr uint32_t kGoldFloor = gfx::rgba(0.114f, 0.090f, 0.047f);
constexpr uint32_t kRowFloor = gfx::rgba(0.110f, 0.082f, 0.067f);
constexpr uint32_t kRowRim = gfx::rgba(0.239f, 0.192f, 0.157f);
constexpr uint32_t kLegendary = gfx::rgba(1.0f, 0x80 / 255.0f, 0.0f);

const char* kTabs[3] = {"Recut", "Salvage", "Combine"};
const char* kPrizeNames[sim::kSalvagePrizes] = {"Potions", "Chaos", "Bless", "Soul", "Life"};

const content::ItemRow* rowOf(const content::Tables& tables, const sim::Held& what) {
    if (what.empty() || what.item < 0 || size_t(what.item) >= tables.items.size()) return nullptr;
    return &tables.items[size_t(what.item)];
}

// "Item find +6%", "Life per hit +3", "Fire resistance +2".
std::string powerLine(sim::Affix affix, int refinement) {
    const int v = sim::affixValue(affix, refinement);
    const bool points = affix == sim::Affix::Leech || sim::affixElement(affix) != sim::Element::None ||
                        affix == sim::Affix::Haste;
    return std::string(sim::affixName(affix)) + " +" + std::to_string(v) + (points ? "" : "%");
}

const char* colourWord(int powers) {
    switch (powers) {
        case 1: return "white";
        case 2: return "blue";
        case 3: return "purple";
        default: return "legendary";
    }
}

std::string pieceName(const content::ItemRow& row, const sim::Held& what) {
    return row.label + (what.refinement > 0 ? " +" + std::to_string(what.refinement) : "");
}

int jewelsCarried(const sim::Realm& realm, int32_t item) {
    int count = 0;
    for (int s = sim::kWorn; s < sim::kSlots; ++s) {
        const sim::Held& h = realm.satchel()[s];
        if (!h.empty() && h.item == item) count += std::max<int>(1, h.durability);
    }
    return count;
}

}  // namespace

void Jeweler::open(const gfx::Interface& interface, panel::Arts* arts) {
    interface.adopt(canvas_);
    arts_ = arts;
}

bool Jeweler::covers(float x, float y) const {
    return up_ && Box{x_, y_, kWide * panel::scale(), panel::kHeight * panel::scale()}.has(x, y);
}

void Jeweler::cancel() {
    for (Slot& s : slots_) s = Slot{};
    line_ = lit_ = -1;
    confirming_ = waiting_ = false;
    said_.clear();
    pressing_ = over_ = kNone;
}

bool Jeweler::offer(int slot, const sim::Realm& realm) {
    if (!realm.tables() || slot < sim::kWorn || slot >= sim::kSlots) return false;
    const sim::Held& what = realm.satchel()[slot];
    const content::ItemRow* row = rowOf(*realm.tables(), what);
    if (row == nullptr || !sim::jewelerTakes(*row)) return false;
    said_.clear();
    lit_ = -1;
    confirming_ = false;
    if (page_ != Page::Combine) {
        slots_[0] = {slot, what.item};
        line_ = -1;
        return true;
    }
    for (const Slot& s : slots_) {
        if (s.bag == slot) return true;  // in already
    }
    for (Slot& s : slots_) {
        if (s.bag < 0) {
            s = {slot, what.item};
            return true;
        }
    }
    slots_[2] = {slot, what.item};
    return true;
}

void Jeweler::answered(bool ok, int32_t value, const std::string& refusal) {
    waiting_ = false;
    saidOk_ = ok;
    if (!ok) {
        said_ = refusal.empty() ? "Refused." : "Refused: " + refusal + ".";
        return;
    }
    switch (asked_) {
        case Page::Recut:
            lit_ = line_;
            said_ = "Recut to " + std::string(sim::affixName(sim::Affix(value))) + ".";
            break;
        case Page::Salvage: {
            const int prize = std::clamp(int(value) - 1, 0, sim::kSalvagePrizes - 1);
            said_ = prize == 0 ? "Only potions. Not this time."
                               : std::string("You got a Jewel of ") + kPrizeNames[prize] + "!";
            slots_[0] = Slot{};
            break;
        }
        case Page::Combine:
            said_ = "Made a new piece. It is in your bag.";
            for (Slot& s : slots_) s = Slot{};
            // The made one into the first slot, so it is there to look at, or to recut.
            slots_[0].bag = int(value) - 1;
            slots_[0].item = -2;  // taken on the next keep()
            break;
    }
}

void Jeweler::keep(const sim::Realm& realm) {
    // A slot whose bag thing is gone or changed is let go; a made one is taken as it stands.
    for (Slot& s : slots_) {
        if (s.bag < 0) continue;
        const sim::Held& what = s.bag < sim::kSlots ? realm.satchel()[s.bag] : sim::Held{};
        if (s.item == -2 && !what.empty()) s.item = what.item;
        if (what.empty() || what.item != s.item) s = Slot{};
    }
    if (page_ != Page::Combine) {
        slots_[1] = slots_[2] = Slot{};
    }
}

int Jeweler::hitAt(float ux, float uy) const {
    if (confirming_) {
        if (kYesBox.has(ux, uy)) return kYes;
        if (kNoBox.has(ux, uy)) return kNo;
    } else if (kRunBox.has(ux, uy)) {
        return kRun;
    }
    for (int i = 0; i < 3; ++i) {
        if (tabBox(i).has(ux, uy)) return kTab0 + i;
    }
    const int slots = page_ == Page::Combine ? 3 : 1;
    for (int i = 0; i < slots; ++i) {
        if (slotBox(i).has(ux, uy)) return kSlot0 + i;
    }
    if (page_ == Page::Recut) {
        for (int i = 0; i < 3; ++i) {
            if (rowBox(i).has(ux, uy)) return kRow0 + i;
        }
    }
    return kNone;
}

void Jeweler::update(float width, float height, int column, const sim::Realm& realm,
                     const Pointer& pointer, Stage* stage, JewelerRequests* out) {
    up_ = realm.jeweling() >= 0 && realm.tables();
    if (!up_) {
        cancel();
        return;
    }
    const content::Tables& tables = *realm.tables();
    x_ = panel::columnX(width, column);
    y_ = panel::panelY(height);
    keep(realm);
    const float k = panel::scale();
    const float ux = (pointer.x - x_) / k, uy = (pointer.y - y_) / k;
    const bool inside = covers(pointer.x, pointer.y);
    over_ = inside ? hitAt(ux, uy) : kNone;
    overClose_ = inside && panel::frameClose().has(ux, uy);

    const sim::Held& piece = slots_[0].bag >= 0 ? realm.satchel()[slots_[0].bag] : sim::Held{};
    if (line_ >= 0 && (piece.empty() || piece.affixes[line_] == 0)) line_ = -1;

    if (pointer.pressed && inside) {
        if (overClose_) closing_ = true;
        else pressing_ = over_;
    }
    if (pointer.released) {
        if (out && closing_ && overClose_) out->close = true;
        if (out && pressing_ != kNone && pressing_ == over_) {
            const int hit = pressing_;
            if (hit >= kTab0 && hit < kTab0 + 3) {
                const Page to = Page(hit - kTab0);
                if (to != page_) {
                    // A piece stays in the first slot from page to page; Combine's other two go.
                    page_ = to;
                    line_ = lit_ = -1;
                    confirming_ = false;
                    said_.clear();
                    out->turned = true;
                }
            } else if (hit >= kRow0 && hit < kRow0 + 3) {
                const int line = hit - kRow0;
                if (!piece.empty() && piece.affixes[line] != 0) {
                    line_ = line;
                    lit_ = -1;
                    said_.clear();
                    out->click = true;
                }
            } else if (hit >= kSlot0 && hit < kSlot0 + 3) {
                slots_[hit - kSlot0] = Slot{};
                line_ = lit_ = -1;
                confirming_ = false;
                out->click = true;
            } else if (hit == kNo) {
                confirming_ = false;
                out->click = true;
            } else if (hit == kYes && !waiting_) {
                confirming_ = false;
                asked_ = Page::Salvage;
                waiting_ = true;
                out->salvageSlot = slots_[0].bag;
            } else if (hit == kRun && !waiting_) {
                if (page_ == Page::Recut && slots_[0].bag >= 0 && line_ >= 0) {
                    asked_ = Page::Recut;
                    waiting_ = true;
                    out->recutSlot = slots_[0].bag;
                    out->recutLine = line_;
                } else if (page_ == Page::Salvage && slots_[0].bag >= 0) {
                    confirming_ = true;
                    out->click = true;
                } else if (page_ == Page::Combine && slots_[0].bag >= 0 && slots_[1].bag >= 0 &&
                           slots_[2].bag >= 0) {
                    asked_ = Page::Combine;
                    waiting_ = true;
                    for (int i = 0; i < 3; ++i) out->combine[i] = slots_[i].bag;
                }
            }
        }
        closing_ = false;
        pressing_ = kNone;
    }

    // The pictures: the slots' pieces, and on Salvage the five prizes.
    standing_.clear();
    const int slots = page_ == Page::Combine ? 3 : 1;
    for (int i = 0; i < slots; ++i) {
        if (slots_[i].bag < 0) continue;
        const sim::Held& what = realm.satchel()[slots_[i].bag];
        if (what.empty()) continue;
        standing_.push_back({what.item, onStage(slotBox(i)).grown(-2.0f), what.refinement,
                             over_ == kSlot0 + i, what.excellent != 0});
    }
    if (page_ == Page::Salvage) {
        for (int p = 0; p < sim::kSalvagePrizes; ++p) {
            const sim::ItemKey key = sim::salvagePrize(p);
            const int32_t item = tables.itemAt(key.group, key.number);
            if (item >= 0) standing_.push_back({item, onStage(prizeBox(p)).grown(-kPrizePad), 0, false, false});
        }
    }
    if (stage) stage->stand(standing_, kStageW, kStageH);

    std::string now = key(realm);
    now += "|" + std::to_string(stage && stage->picture().valid() ? stage->picture().handle.idx : 0xFFFF);
    if (now == drawn_ && rebuilds_ > 0) return;
    drawn_ = now;
    rebuild(realm, stage);
}

std::string Jeweler::key(const sim::Realm& realm) const {
    std::string s;
    s.reserve(128);
    const auto add = [&](long long v) { s += std::to_string(v) + ","; };
    add(int(page_));
    for (const Slot& one : slots_) {
        add(one.bag);
        if (one.bag >= 0) {
            const sim::Held& h = realm.satchel()[one.bag];
            add(h.item);
            add(h.refinement);
            for (uint8_t a : h.affixes) add(a);
        }
    }
    add(line_);
    add(lit_);
    add(confirming_);
    add(waiting_);
    add(over_);
    add(pressing_);
    add(overClose_);
    add(closing_);
    add(realm.money());
    add(realm.satchel().version());
    add(std::lround(x_));
    add(std::lround(y_));
    add(std::lround(panel::scale() * 1000.0f));
    s += said_;
    return s;
}

void Jeweler::rebuild(const sim::Realm& realm, Stage* stage) {
    ++rebuilds_;
    canvas_.clear();
    if (!arts_ || !up_) return;
    const content::Tables& tables = *realm.tables();
    const float x = x_, y = y_, k = panel::scale();
    const float u = tip::unit();
    const float text = kText * k * 1.1f;
    const auto P = [&](const Box& units) { return panel::scaled(x, y, units); };
    const auto base = [&](float top, float tall, float size) {
        return controls::middle(y + top * k, tall * k, size);
    };
    const auto at = [&](float left, float top, uint32_t ink, const std::string& s, float size = 0.0f) {
        const float sz = size > 0.0f ? size : text;
        controls::label(canvas_, x + left * k, base(top, kLine, sz), sz, ink, s);
    };
    const auto right = [&](float edge, float top, uint32_t ink, const std::string& s) {
        controls::ranged(canvas_, x + edge * k, base(top, kLine, text), text, ink, s);
    };
    const auto kicker = [&](float top, const std::string& s) {
        controls::kicker(canvas_, x + kLeft * k, y + (top + 7.0f) * k, s, u);
    };
    const auto stateOf = [&](int which, bool off) {
        controls::State state;
        state.lift = over_ == which && !off ? 1.0f : 0.0f;
        state.held = pressing_ == which && over_ == which && !off;
        state.off = off;
        return state;
    };

    controls::frame(canvas_, P({0.0f, 0.0f, kWide, panel::kHeight}), u, "Jeweler", nullptr,
                    panel::kHeadBand * k);
    for (int i = 0; i < 3; ++i) {
        controls::tab(canvas_, P(tabBox(i)), kTabs[i], int(page_) == i, stateOf(kTab0 + i, false), u);
    }

    // The slots: a cell each, empty or holding its piece (the stage's picture, below).
    const int slots = page_ == Page::Combine ? 3 : 1;
    for (int i = 0; i < slots; ++i) {
        controls::cell(canvas_, P(slotBox(i)),
                       over_ == kSlot0 + i ? controls::Cell::Over : controls::Cell::Rest, u);
    }
    const sim::Held& piece = slots_[0].bag >= 0 ? realm.satchel()[slots_[0].bag] : sim::Held{};
    const content::ItemRow* row = rowOf(tables, piece);
    const int powers = row ? sim::affixCount(*row, piece) : 0;
    if (page_ != Page::Combine) {
        const float nx = kLeft + kSlot + 8.0f;
        if (row) {
            at(nx, kStageY + 2.0f, tip::colourOf(qualityOf(*row, piece)), pieceName(*row, piece));
            std::string meta = std::to_string(powers) + (powers == 1 ? " power" : " powers");
            if (piece.luck) meta = "Luck  \xC2\xB7  " + meta;
            if (piece.bound) meta += "  \xC2\xB7  Bound";
            at(nx, kStageY + 15.0f, style::kAshInk, meta);
        } else {
            at(nx, kStageY + 2.0f, style::kBone2, "Right-click a ring or");
            at(nx, kStageY + 13.0f, style::kBone2, "pendant in your bag.");
        }
    }

    // ---- the pages ---------------------------------------------------------------------------
    int64_t zen = 0;
    std::string need;
    // The one jewel Recut and Combine each take, and how many he carries.
    const int32_t chaosItem = tables.itemAt(sim::kJewelerJewel.group, sim::kJewelerJewel.number);
    const int chaos = chaosItem >= 0 ? jewelsCarried(realm, chaosItem) : 0;
    const std::string chaosLine = "and 1 Jewel of Chaos  (" + std::to_string(chaos) + " in bag)";
    bool ready = false;
    std::string verb = kTabs[int(page_)];
    if (page_ == Page::Recut) {
        kicker(kRowTop - 17.0f, row ? "PICK A LINE TO RECUT" : "RECUT ONE POWER");
        if (row) {
            // The signature, locked: it never changes.
            const Box sig = rowBox(-1);
            canvas_.rect(P(sig), gfx::rgba(0.05f, 0.04f, 0.03f));
            canvas_.outline(P(sig), std::max(1.0f, std::round(u)), kRowRim);
            at(sig.x + 6.0f, sig.y + 4.0f, style::kBone, powerLine(sim::signatureOf(*row), piece.refinement));
            right(sig.right() - 6.0f, sig.y + 4.0f, style::kAshInk, "Signature");
            for (int i = 0; i < 3; ++i) {
                if (piece.affixes[i] == 0) continue;
                const Box b = rowBox(i);
                const bool chosen = line_ == i;
                const bool lit = lit_ == i;
                canvas_.rect(P(b), chosen ? kGoldFloor : kRowFloor);
                canvas_.outline(P(b), std::max(1.0f, std::round(u)),
                                chosen || lit ? kGold : over_ == kRow0 + i ? style::kIron : kRowRim);
                at(b.x + 6.0f, b.y + 4.0f, chosen || lit ? kGoldHi : style::kBone,
                   powerLine(sim::Affix(piece.affixes[i]), piece.refinement));
                if (lit) right(b.right() - 6.0f, b.y + 4.0f, kGoldHi, "New");
                else if (chosen) right(b.right() - 6.0f, b.y + 4.0f, kGold, "Recut this");
            }
            if (powers <= 1) {
                at(kLeft, rowBox(0).y + 4.0f, style::kAshInk, "A white piece has no line to recut.");
            }
            at(kLeft, rowBox(3).y + 2.0f, style::kAshInk, "The signature and the colour never change.");
            zen = sim::recutZen(piece.refinement);
            need = chaosLine;
            ready = line_ >= 0 && chaos > 0 && realm.money() >= zen;
        }
    } else if (page_ == Page::Salvage) {
        kicker(kPrizeTop - 16.0f, "IT COULD GIVE YOU");
        for (int p = 0; p < sim::kSalvagePrizes; ++p) {
            const Box b = prizeBox(p);
            controls::cell(canvas_, P(b), controls::Cell::Rest, u);
            const float w = controls::labelWidth(text * 0.9f, kPrizeNames[p]);
            controls::label(canvas_, x + (b.x + b.w * 0.5f) * k - w * 0.5f,
                            base(b.bottom() + 2.0f, kLine, text * 0.9f), text * 0.9f,
                            p == sim::kSalvagePrizes - 1 ? kLegendary : style::kBone2, kPrizeNames[p]);
        }
        at(kLeft, kPrizeTop + 49.0f, style::kBone, "Breaking it gives one of these. Often a");
        at(kLeft, kPrizeTop + 61.0f, style::kBone, "jewel, sometimes only potions, and now");
        at(kLeft, kPrizeTop + 73.0f, style::kBone, "and then a Jewel of Life. The rarer the");
        at(kLeft, kPrizeTop + 85.0f, style::kBone, "piece, the better your luck.");
        ready = row != nullptr;
    } else {
        kicker(kStageY + kSlot + 10.0f, "THREE OF ONE COLOUR");
        int count = 0, colour = -1, plus = sim::kRefineCap;
        bool same = true, bound = false;
        for (const Slot& s : slots_) {
            if (s.bag < 0) continue;
            const sim::Held& h = realm.satchel()[s.bag];
            const content::ItemRow* r = rowOf(tables, h);
            if (!r) continue;
            const int c = sim::affixCount(*r, h);
            same = same && (colour < 0 || c == colour);
            colour = c;
            plus = std::min<int>(plus, h.refinement);
            bound = bound || h.bound;
            ++count;
        }
        const float t = kStageY + kSlot + 26.0f;
        if (count == 0) {
            at(kLeft, t, style::kBone2, "Right-click three rings or pendants");
            at(kLeft, t + 11.0f, style::kBone2, "of one colour in your bag.");
        } else if (!same) {
            at(kLeft, t, style::kDanger, "They are not all one colour.");
        } else if (colour >= 3) {
            at(kLeft, t, style::kDanger, "Purple and legendary do not combine.");
        } else {
            at(kLeft, t, style::kBone, std::string("Makes a random ") + colourWord(colour + 1) +
                                           " ring or pendant,");
            at(kLeft, t + 11.0f, style::kBone, "+" + std::to_string(plus) + ", the lowest of the three.");
            if (bound) at(kLeft, t + 25.0f, kGold, "One is bound, so the new piece is bound.");
        }
        at(kLeft, t + 44.0f, style::kAshInk, "White to blue: 500,000 Zen and a Chaos.");
        at(kLeft, t + 55.0f, style::kAshInk, "Blue to purple: 2,000,000 Zen and a Chaos.");
        at(kLeft, t + 66.0f, style::kAshInk, "Legendary is only found.");
        if (same && count > 0 && colour >= 1 && colour <= 2) zen = sim::combineZen(colour);
        if (zen > 0) need = chaosLine;
        ready = count == 3 && same && zen > 0 && chaos > 0 && realm.money() >= zen;
    }

    // ---- what was said, the foot -------------------------------------------------------------
    if (!said_.empty()) at(kLeft, kSaidTop, saidOk_ ? kGoldHi : style::kDanger, said_);
    if (waiting_) at(kLeft, kSaidTop, style::kAshInk, "...");
    if (!need.empty()) at(kLeft, kNeedTop, style::kBone2, need);
    if (zen > 0) {
        at(kLeft, kCostTop, style::kAshInk, "Cost");
        right(kRight, kCostTop, realm.money() >= zen ? moneyColour(zen) : style::kDanger,
              panel::commas(zen) + " Zen");
    } else if (page_ == Page::Salvage && row) {
        at(kLeft, kCostTop, style::kAshInk, "Cost");
        right(kRight, kCostTop, style::kBone, "Free");
    }
    controls::foot(canvas_, P({0.0f, 0.0f, kWide, panel::kHeight}), y + panel::kFootRule * k, u);
    if (confirming_) {
        at(kLeft, panel::kFootTop + 7.0f, style::kBoneHi, "Break it?");
        controls::button(canvas_, P(kNoBox), "No", controls::Kind::Secondary, stateOf(kNo, false), u);
        controls::button(canvas_, P(kYesBox), "Salvage", controls::Kind::Primary, stateOf(kYes, false), u);
    } else {
        controls::button(canvas_, P(kRunBox), verb,
                         page_ == Page::Salvage ? controls::Kind::Danger : controls::Kind::Primary,
                         stateOf(kRun, !ready || waiting_), u);
    }
    panel::close(canvas_, x, y, overClose_, closing_);

    // ---- the pictures --------------------------------------------------------------------------
    const gfx::Art picture = stage ? stage->picture() : gfx::Art{};
    if (picture.valid()) {
        const float sx = picture.width / kStageW, sy = picture.height / kStageH;
        canvas_.region(picture, P({kStageX, kStageY, kStageW, kStageH}), {0.0f, 0.0f, kStageW * sx, kStageH * sy});
    } else {
        const gfx::Face& face = canvas_.face();
        for (const Standing& one : standing_) {
            const Box to = P({one.box.x + kStageX, one.box.y + kStageY, one.box.w, one.box.h});
            const std::string& name = tables.items[size_t(one.item)].label;
            canvas_.text(to.x + 2.0f, to.y + face.ascent(7.0f * k) + 2.0f, 7.0f * k, panel::kLettering,
                         name.substr(0, std::min<size_t>(name.size(), 6)));
        }
    }
}

}  // namespace mu::game
