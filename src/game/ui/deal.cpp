#include "game/ui/deal.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "game/ui/controls.h"
#include "game/ui/describe.h"
#include "game/ui/sheet.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"
#include "sim/quests.h"

namespace mu::game {
namespace {

using gfx::Box;

// MU's column in the skin's units (CNewUITrade: the other's grid at y + 68, his own at y + 274),
// laid on the shared pitch: the other player's line, his eight by four and his Zen; what changed;
// a rule; his own line, box and Zen with the coin; whether it fits; and the foot's two buttons.
constexpr float kCell = panel::kPitch;
constexpr float kGridX = panel::kGridX;
constexpr float kTheirLine = 44.0f, kTheirGrid = 64.0f, kTheirZen = 152.0f;
constexpr float kNote = 174.0f, kRule = 206.0f;
constexpr float kMyLine = 212.0f, kMyGrid = 232.0f, kMyZen = 320.0f, kFit = 346.0f;
constexpr float kLineTall = 18.0f, kZenTall = 18.0f;
constexpr Box kCoin{panel::kWellRight - 20.0f, kMyZen, 20.0f, kZenTall};
constexpr float kButtonTop = panel::kFootTop + 2.0f, kButtonTall = 24.0f;
constexpr Box kAccept{panel::kEdge, kButtonTop, 104.0f, kButtonTall};
constexpr Box kCancel{panel::kEdge + 110.0f, kButtonTop, panel::kWellRight - panel::kEdge - 110.0f, kButtonTall};
// A player's name, in the green the world prints players' names in.
constexpr uint32_t kNameInk = gfx::rgba(0.29f, 0.86f, 0.36f);

Box cellBox(float gridY, int cell, int width, int height) {
    return {kGridX + float(cell % sim::kMachineColumns) * kCell, gridY + float(cell / sim::kMachineColumns) * kCell,
            kCell * float(width), kCell * float(height)};
}

Box itemBox(const content::Tables& tables, float gridY, int cell, const sim::Held& what) {
    if (what.empty()) return cellBox(gridY, cell, 1, 1);
    const content::ItemRow& row = tables.items[size_t(what.item)];
    return cellBox(gridY, cell, row.width, row.height);
}

int cellAt(float gridY, float ux, float uy) {
    const float cx = (ux - kGridX) / kCell, cy = (uy - gridY) / kCell;
    if (cx < 0.0f || cy < 0.0f || cx >= float(sim::kMachineColumns) || cy >= float(sim::kMachineRows)) return -1;
    return int(cy) * sim::kMachineColumns + int(cx);
}

std::string named(const content::Tables& tables, const sim::Held& what) {
    std::string out = tables.items[size_t(what.item)].label;
    if (what.refinement > 0) out += " +" + std::to_string(what.refinement);
    return out;
}

bool same(const sim::Held& a, const sim::Held& b) {
    return a.item == b.item && a.refinement == b.refinement && a.durability == b.durability && a.skill == b.skill &&
           a.luck == b.luck && a.option == b.option && a.excellent == b.excellent && a.sockets == b.sockets &&
           std::equal(std::begin(a.powers), std::end(a.powers), std::begin(b.powers)) &&
           std::equal(std::begin(a.affixes), std::end(a.affixes), std::begin(b.affixes)) && a.wing == b.wing &&
           a.bound == b.bound;
}

uint64_t mixed(uint64_t h, uint64_t v) { return (h ^ v) * 1099511628211ull; }

}  // namespace

void Deal::open(const gfx::Interface& interface, panel::Arts* arts) {
    interface.adopt(canvas_);
    interface.adopt(tip_);
    arts_ = arts;
}

bool Deal::covers(float x, float y) const {
    return up_ && Box{x_, y_, panel::kWidth * panel::scale(), panel::kHeight * panel::scale()}.has(x, y);
}

int Deal::cellUnder(float x, float y, int width, int height) const {
    if (!covers(x, y)) return -1;
    const float k = panel::scale();
    return panel::landing((x - x_) / k, (y - y_) / k, kGridX, kMyGrid, sim::kMachineColumns,
                          sim::kMachineRows, width, height);
}

Deal::Marks Deal::marks(const sim::Realm& realm) const {
    Marks out;
    if (!seenAny_) return out;
    const sim::Machine& theirs = realm.dealOffer(true);
    std::vector<bool> matched(seen_.size(), false);
    std::vector<int> unmatchedNow;
    for (int cell = 0; cell < sim::kMachineCells; ++cell) {
        if (theirs[cell].empty()) continue;
        bool found = false;
        for (size_t i = 0; i < seen_.size(); ++i) {
            if (!matched[i] && same(seen_[i].second, theirs[cell])) {
                matched[i] = true;
                found = true;
                break;
            }
        }
        if (!found) unmatchedNow.push_back(cell);
    }
    // A thing of the same row that went and came back different is Changed; anything else New.
    for (const int cell : unmatchedNow) {
        bool paired = false;
        for (size_t i = 0; i < seen_.size(); ++i) {
            if (!matched[i] && seen_[i].second.item == theirs[cell].item) {
                matched[i] = true;
                out.changed.push_back(cell);
                out.wasRow.push_back(seen_[i].second.item);
                out.wasPlus.push_back(seen_[i].second.refinement);
                paired = true;
                break;
            }
        }
        if (!paired) out.fresh.push_back(cell);
    }
    for (size_t i = 0; i < seen_.size(); ++i) {
        if (!matched[i]) out.gone.push_back(seen_[i].second);
    }
    const int64_t zen = realm.dealZenOf(true);
    if (zen != seenZen_) {
        out.zenMoved = true;
        out.zenWas = seenZen_;
    }
    return out;
}

void Deal::update(float seconds, float width, float height, int column, const sim::Realm& realm,
                  const std::string& name, const Pointer& pointer, Stage* stage, DealRequests* out) {
    (void)seconds;
    up_ = realm.dealing() != 0 && realm.tables();
    if (!up_) {
        dragging_ = pressing_ = lifting_ = -1;
        seen_.clear();
        seenAny_ = false;
        acceptedBefore_ = false;
        partner_ = 0;
        canvas_.clear();
        tip_.clear();
        drawnOnce_ = false;
        return;
    }
    if (partner_ != realm.dealing()) {
        partner_ = realm.dealing();
        seen_.clear();
        seenAny_ = false;
        acceptedBefore_ = false;
    }
    name_ = name;
    const content::Tables& tables = *realm.tables();
    const sim::Machine& mine = realm.dealOffer(false);
    const sim::Machine& theirs = realm.dealOffer(true);
    // His Accept going on is his look at the other's offer: the marks are against it from then.
    const bool accepted = realm.dealAccepted(false);
    if (accepted && !acceptedBefore_) {
        seen_.clear();
        for (int cell = 0; cell < sim::kMachineCells; ++cell) {
            if (!theirs[cell].empty()) seen_.push_back({cell, theirs[cell]});
        }
        seenZen_ = realm.dealZenOf(true);
        seenAny_ = true;
    }
    acceptedBefore_ = accepted;

    screenW_ = width;
    screenH_ = height;
    x_ = panel::columnX(width, column);
    y_ = panel::panelY(height);
    const float k = panel::scale();
    const float ux = (pointer.x - x_) / k, uy = (pointer.y - y_) / k;
    const bool inside = covers(pointer.x, pointer.y);
    pointerX_ = pointer.x;
    pointerY_ = pointer.y;

    hoveredBox_ = hovered_ = -1;
    if (inside && incoming_.empty()) {
        if (const int cell = cellAt(kMyGrid, ux, uy); cell >= 0 && mine.holder(tables, cell) >= 0) {
            hoveredBox_ = 0;
            hovered_ = mine.holder(tables, cell);
        } else if (const int at = cellAt(kTheirGrid, ux, uy); at >= 0 && theirs.holder(tables, at) >= 0) {
            hoveredBox_ = 1;
            hovered_ = theirs.holder(tables, at);
        }
    }
    button_ = -1;
    if (inside) {
        if (kCoin.has(ux, uy)) button_ = 0;
        else if (kAccept.has(ux, uy)) button_ = 1;
        else if (kCancel.has(ux, uy)) button_ = 2;
        else if (panel::frameClose().has(ux, uy)) button_ = 3;
    }
    const bool holding = realm.dealHold() > 0;
    const bool misfit = realm.dealMisfit(true) >= 0 || realm.dealMisfit(false) >= 0;
    // Click to pick and click to place, the bag's (bag.h): while a thing rides the pointer a
    // press is the start of placing it and nothing else.
    if (pointer.pressed && inside && dragging_ < 0) {
        if (button_ >= 0) pressing_ = button_;
        else if (hoveredBox_ == 0) lifting_ = hovered_;
    }
    if (pointer.released) {
        if (out) {
            if (pressing_ >= 0 && pressing_ == button_) {
                if (pressing_ == 0) out->zen = true;
                if (pressing_ == 1) {
                    if (accepted) out->accept = 0;
                    else if (!holding && !misfit) out->accept = 1;
                }
                if (pressing_ == 2 || pressing_ == 3) out->close = true;
            }
            // Over its own place it goes back; elsewhere in the window it is taken back to
            // the bag, as before; outside, the desk decides.
            if (dragging_ >= 0) {
                if (inside && !(hoveredBox_ == 0 && hovered_ == dragging_)) {
                    out->take = dragging_;
                } else if (!inside) {
                    out->outside = dragging_;
                    out->outsideX = pointer.x;
                    out->outsideY = pointer.y;
                }
                dragging_ = -1;
            } else if (lifting_ >= 0 && hoveredBox_ == 0 && lifting_ == hovered_) {
                dragging_ = lifting_;
            }
        }
        pressing_ = -1;
        lifting_ = -1;
    }
    if (dragging_ >= 0 && mine[dragging_].empty()) dragging_ = -1;

    standing_.clear();
    for (int box = 0; box < 2; ++box) {
        const sim::Machine& grid = box == 0 ? mine : theirs;
        const float gridY = box == 0 ? kMyGrid : kTheirGrid;
        for (int at = 0; at < sim::kMachineCells; ++at) {
            const sim::Held& what = grid[at];
            if (what.empty()) continue;
            const Box place = itemBox(tables, gridY, at, what);
            const bool lifted = box == 0 && at == dragging_;
            standing_.push_back({what.item, (lifted ? panel::carryBox(place) : place).grown(-2.0f), what.refinement,
                                 hoveredBox_ == box && at == hovered_ && dragging_ < 0, what.excellent != 0});
        }
    }
    if (stage) stage->stand(standing_, panel::kWidth, panel::kStageTall);

    // Redrawn on a change: the trade's own count, the bags, the hold's second, the pointer where
    // it lights something, and the stage's picture.
    uint64_t key = 1469598103934665603ull;
    key = mixed(key, realm.dealVersion());
    key = mixed(key, realm.satchel().version());
    key = mixed(key, uint64_t((realm.dealHold() + 19) / 20));
    key = mixed(key, uint64_t(realm.money()));
    key = mixed(key, uint64_t(hoveredBox_ + 1) * 64 + uint64_t(hovered_ + 1));
    key = mixed(key, uint64_t(button_ + 1) * 8 + uint64_t(pressing_ + 1));
    key = mixed(key, uint64_t(dragging_ + 1));
    key = mixed(key, uint64_t(incoming_.item + 1));
    key = mixed(key, seenAny_ ? seen_.size() + 1 : 0);
    key = mixed(key, std::hash<std::string>{}(name_));
    if (dragging_ >= 0 || !incoming_.empty() || hovered_ >= 0) {
        key = mixed(key, uint64_t(std::lround(pointer.x)) * 4099 + uint64_t(std::lround(pointer.y)));
    }
    key = mixed(key, uint64_t(std::lround(x_)) * 31 + uint64_t(std::lround(y_)) + uint64_t(std::lround(k * 1000.0f)));
    key = mixed(key, stage && stage->picture().valid() ? stage->picture().handle.idx : 0xFFFF);
    if (drawnOnce_ && key == drawnKey_) return;
    drawnKey_ = key;
    drawnOnce_ = true;
    rebuild(realm, stage);
}

void Deal::rebuild(const sim::Realm& realm, Stage* stage) {
    canvas_.clear();
    tip_.clear();
    if (!arts_ || !up_) return;
    panel::Arts& arts = *arts_;
    const content::Tables& tables = *realm.tables();
    const sim::Machine& mine = realm.dealOffer(false);
    const sim::Machine& theirs = realm.dealOffer(true);
    const float x = x_, y = y_, k = panel::scale(), u = tip::unit();
    const Marks marked = marks(realm);
    const bool accepted = realm.dealAccepted(false), theyAccepted = realm.dealAccepted(true);
    const int64_t hold = realm.dealHold();

    panel::frame(canvas_, arts, x, y, "Trade");

    // A side's line: who, and where his Accept stands.
    const auto line = [&](float top, const std::string& who, uint32_t whoInk, const std::string& what, bool on) {
        const Box band = panel::scaled(x, y, {kGridX, top, panel::kWellRight - kGridX, kLineTall});
        const float base = controls::middle(band.y, band.h, 15.0f * u);
        float at = band.x + controls::label(canvas_, band.x, base, 15.0f * u, whoInk, who);
        if (!what.empty()) controls::label(canvas_, at + 6.0f * u, base, 13.0f * u, style::kAshInk, what);
        const std::string chip = on ? "Accepted" : "Deciding";
        const float cw = controls::capsWidth(11.0f * u, chip, 0.0f) + 12.0f * u;
        const Box pill{band.right() - cw, band.y + 2.0f * u, cw, band.h - 4.0f * u};
        if (on) canvas_.rect(pill, style::kFits);
        else canvas_.outline(pill, std::max(1.0f, u), style::kIronLo);
        controls::caps(canvas_, pill.x + 6.0f * u, controls::middle(pill.y, pill.h, 11.0f * u), 11.0f * u,
                       on ? style::kVoid : style::kAshInk, chip, 0.0f);
    };
    // A Zen strip: the coins and the sum ranged right, in the colour the sum earns.
    const auto zenStrip = [&](float top, int64_t zen, float right) {
        const Box strip = panel::scaled(x, y, {kGridX, top, panel::kWellRight - kGridX, kZenTall});
        canvas_.rect(strip, style::kVoid);
        canvas_.outline(strip, std::max(1.0f, u), style::kIronDk);
        canvas_.image(arts.get("bag_zen"), panel::scaled(x, y, {kGridX + 3.0f, top + 2.0f, 16.0f, 14.0f}));
        controls::ranged(canvas_, x + right * k, controls::middle(strip.y, strip.h, 15.0f * u), 15.0f * u,
                         moneyColour(zen), panel::commas(zen));
    };

    // ---- the other player's half
    std::string kinWord;
    if (const sim::Body* them = realm.find(realm.dealing())) {
        kinWord = std::string(sim::className(int(them->kin), them->second)) + "  \xC2\xB7  Lv " +
                  std::to_string(them->level);
    }
    line(kTheirLine, name_.empty() ? std::string("Someone") : name_, kNameInk, kinWord, theyAccepted);
    panel::grid(canvas_, x, y, kGridX, kTheirGrid, sim::kMachineColumns, sim::kMachineRows);
    if (hoveredBox_ == 1 && hovered_ >= 0 && !theirs[hovered_].empty()) {
        panel::cell(canvas_, x, y, itemBox(tables, kTheirGrid, hovered_, theirs[hovered_]), sheet::Cell::Over);
    }
    for (const int cell : marked.changed) {
        panel::cell(canvas_, x, y, itemBox(tables, kTheirGrid, cell, theirs[cell]), sheet::Cell::Blocked);
    }
    zenStrip(kTheirZen, realm.dealZenOf(true), panel::kWellRight - 5.0f);

    // What changed since his last Accept, in words, the worst first; else what to do next.
    {
        std::vector<std::pair<std::string, std::string>> said;  // the word, the rest
        for (size_t i = 0; i < marked.changed.size(); ++i) {
            sim::Held was;
            was.item = marked.wasRow[i];
            was.refinement = int16_t(marked.wasPlus[i]);
            said.push_back({"Changed", named(tables, was) + "  \xE2\x86\x92  " + named(tables, theirs[marked.changed[i]])});
        }
        for (const sim::Held& gone : marked.gone) said.push_back({"Removed", named(tables, gone)});
        if (marked.zenMoved) {
            said.push_back({"Zen", panel::commas(marked.zenWas) + "  \xE2\x86\x92  " + panel::commas(realm.dealZenOf(true))});
        }
        const float left = x + kGridX * k;
        float base = y + (kNote + 11.0f) * k;
        if (!said.empty()) {
            for (size_t i = 0; i < std::min<size_t>(said.size(), 2); ++i) {
                const float w = controls::label(canvas_, left, base, 13.0f * u, style::kBloodHi, said[i].first);
                controls::label(canvas_, left + w + 6.0f * u, base, 13.0f * u, style::kDanger,
                                said.size() > 2 && i == 1 ? said[i].second + "  (+" + std::to_string(said.size() - 2) + ")"
                                                          : said[i].second);
                base += 13.0f * k;
            }
        } else {
            std::string hint;
            if (hold > 0) hint = "An offer changed. Accept again in " + std::to_string((hold + 19) / 20) + " s.";
            else if (theyAccepted && !accepted) hint = (name_.empty() ? std::string("They") : name_) + " accepted. Check the offer, then Accept.";
            else if (accepted && !theyAccepted) hint = "You accepted. Waiting for " + (name_.empty() ? std::string("them") : name_) + ".";
            else hint = "Click things from your bag here to offer them.";
            controls::label(canvas_, left, base, 13.0f * u, style::kAshInk, hint);
        }
    }
    controls::rule(canvas_, x + kGridX * k, y + kRule * k, (panel::kWellRight - kGridX) * k, u);

    // ---- his own half
    line(kMyLine, "You", style::kBoneHi, "", accepted);
    panel::grid(canvas_, x, y, kGridX, kMyGrid, sim::kMachineColumns, sim::kMachineRows);
    if (hoveredBox_ == 0 && hovered_ >= 0 && dragging_ < 0 && !mine[hovered_].empty()) {
        panel::cell(canvas_, x, y, itemBox(tables, kMyGrid, hovered_, mine[hovered_]), sheet::Cell::Over);
    }
    // A bag thing carried over his box: the cell it would land on, by the realm's own room.
    if (!incoming_.empty() && covers(pointerX_, pointerY_)) {
        const content::ItemRow& row = tables.items[size_t(incoming_.item)];
        const int cell = cellUnder(pointerX_, pointerY_, row.width, row.height);
        if (cell >= 0) {
            const int column = cell % sim::kMachineColumns, lineAt = cell / sim::kMachineColumns;
            const int w = std::min<int>(row.width, sim::kMachineColumns - column);
            const int h = std::min<int>(row.height, sim::kMachineRows - lineAt);
            panel::cell(canvas_, x, y, cellBox(kMyGrid, cell, w, h),
                        sim::tradeable(row, incoming_) && mine.room(tables, cell, row.width, row.height)
                            ? sheet::Cell::Fits
                            : sheet::Cell::Blocked);
        }
    }
    zenStrip(kMyZen, realm.dealZenOf(false), kCoin.x - 6.0f);
    {
        const Box to = panel::scaled(x, y, kCoin);
        const float side = std::round(std::min(to.w, to.h));
        controls::State state;
        state.lift = button_ == 0 ? 1.0f : 0.0f;
        state.held = pressing_ == 0 && button_ == 0;
        controls::square(canvas_, {std::round(to.midX() - side * 0.5f), std::round(to.midY() - side * 0.5f), side, side},
                         controls::Glyph::CoinIn, state, u);
    }

    // Whether it fits: his bag for what he gets, and hers for what she gets.
    {
        const int ours = realm.dealMisfit(true), hers = realm.dealMisfit(false);
        const Box band = panel::scaled(x, y, {kGridX, kFit, panel::kWellRight - kGridX, 14.0f});
        const float base = controls::middle(band.y, band.h, 13.0f * u);
        const float r = 3.0f * u;
        std::string words;
        uint32_t ink = style::kFits;
        if (ours >= 0) {
            words = "No room in your bag for " + named(tables, theirs[ours]);
            ink = style::kDanger;
        } else if (hers >= 0) {
            words = (name_.empty() ? std::string("They have") : name_ + " has") + " no room for " + named(tables, mine[hers]);
            ink = style::kDanger;
        } else if (!theirs.empty() || realm.dealZenOf(true) > 0) {
            words = "Everything fits in your bag";
        }
        if (!words.empty()) {
            canvas_.rect({band.x, band.midY() - r, 2.0f * r, 2.0f * r}, ink);
            controls::label(canvas_, band.x + 2.0f * r + 6.0f * u, base, 13.0f * u, ink, words);
        }
    }

    // The foot: Accept, greyed with its seconds while held or while something will not fit, and
    // Cancel.
    controls::foot(canvas_, panel::scaled(x, y, {0.0f, 0.0f, panel::kWidth, panel::kHeight}),
                   y + panel::kFootRule * k, u);
    {
        const bool misfit = realm.dealMisfit(true) >= 0 || realm.dealMisfit(false) >= 0;
        controls::State state;
        state.lift = button_ == 1 ? 1.0f : 0.0f;
        state.held = pressing_ == 1 && button_ == 1;
        state.off = !accepted && (hold > 0 || misfit);
        const std::string word = accepted ? "Accepted" : hold > 0 ? "Accept  " + std::to_string((hold + 19) / 20) : "Accept";
        controls::button(canvas_, panel::scaled(x, y, kAccept), word,
                         accepted ? controls::Kind::Secondary : controls::Kind::Primary, state, u);
        controls::State cancel;
        cancel.lift = button_ == 2 ? 1.0f : 0.0f;
        cancel.held = pressing_ == 2 && button_ == 2;
        controls::button(canvas_, panel::scaled(x, y, kCancel), "Cancel", controls::Kind::Secondary, cancel, u);
    }
    panel::close(canvas_, x, y, button_ == 3, pressing_ == 3);

    // The things, off the stage's picture.
    const gfx::Art picture = stage ? stage->picture() : gfx::Art{};
    if (picture.valid()) {
        panel::standing(canvas_, picture, x, y);
    } else {
        for (const Standing& one : standing_) {
            canvas_.outline(panel::scaled(x, y, one.box), 1.0f, gfx::rgba(0.68f, 0.60f, 0.40f, 0.9f));
        }
    }
    // The plus in each thing's corner, a stack's count at its foot, and the other's New and
    // Changed under his.
    for (int box = 0; box < 2; ++box) {
        const sim::Machine& grid = box == 0 ? mine : theirs;
        const float gridY = box == 0 ? kMyGrid : kTheirGrid;
        for (int at = 0; at < sim::kMachineCells; ++at) {
            const sim::Held& held = grid[at];
            if (held.empty() || (box == 0 && at == dragging_)) continue;
            const Box cell = panel::scaled(x, y, itemBox(tables, gridY, at, held));
            if (held.refinement > 0) {
                canvas_.shadowed(cell.x + 2.0f * k, cell.y + 9.0f * k, 9.0f * k, panel::kRefined,
                                 gfx::rgba(0.0f, 0.0f, 0.0f, 0.85f), std::max(1.0f, 0.5f * k),
                                 "+" + std::to_string(held.refinement));
            }
            if (held.durability > 1 && sim::poursMost(tables.items[size_t(held.item)]) > 0) {
                canvas_.shadowed(cell.x, cell.bottom() - 2.0f * k, 8.0f * k, panel::kLettering,
                                 gfx::rgba(0.0f, 0.0f, 0.0f, 0.8f), std::max(1.0f, 0.5f * k),
                                 std::to_string(held.durability), gfx::Align::Right, cell.w - 2.0f * k);
            }
            if (box != 1) continue;
            const bool changed = std::find(marked.changed.begin(), marked.changed.end(), at) != marked.changed.end();
            const bool fresh = std::find(marked.fresh.begin(), marked.fresh.end(), at) != marked.fresh.end();
            if (!changed && !fresh) continue;
            const std::string tag = changed ? "Changed" : "New";
            const float tw = controls::capsWidth(10.0f * u, tag, 0.0f) + 8.0f * u;
            const Box pill{std::round(cell.midX() - tw * 0.5f), std::round(cell.bottom() - 9.0f * u), tw, 13.0f * u};
            if (changed) {
                canvas_.rect(pill, style::kBlood);
            } else {
                canvas_.rect(pill, style::kAsh4);
                canvas_.outline(pill, std::max(1.0f, u), style::kIronHi);
            }
            controls::caps(canvas_, pill.x + 4.0f * u, controls::middle(pill.y, pill.h, 10.0f * u), 10.0f * u,
                           style::kBoneHi, tag, 0.0f);
        }
    }

    // What rides the pointer, cut out of the stage at its own footprint.
    if (dragging_ >= 0 && !mine[dragging_].empty()) {
        panel::carried(canvas_, picture, itemBox(tables, kMyGrid, dragging_, mine[dragging_]), pointerX_, pointerY_);
    }

    // The card of what is under the pointer, either side's.
    if (dragging_ < 0 && hovered_ >= 0) {
        const sim::Machine& grid = hoveredBox_ == 0 ? mine : theirs;
        const sim::Held& held = grid[hovered_];
        if (!held.empty()) {
            tip::Sheet sheet = describe(tables, held, realm.wearer(), realm.satchel());
            if (tipStage_) tip::stand(*tipStage_, held.item, held.refinement, sheet);
            const Box cell = panel::scaled(x, y, itemBox(tables, hoveredBox_ == 0 ? kMyGrid : kTheirGrid, hovered_, held));
            tip::draw(tip_, sheet, cell, screenW_, screenH_);
        }
    } else if (dragging_ < 0 && button_ == 0) {
        const Box to = panel::scaled(x, y, kCoin);
        panel::tooltip(tip_, to.midX(), to.y, {{"Offer Zen", panel::kOrdinary}}, 8.0f * panel::unit(), screenW_, screenH_);
    }
}

}  // namespace mu::game
