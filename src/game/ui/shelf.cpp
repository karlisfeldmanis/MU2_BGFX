#include "game/ui/shelf.h"

#include <algorithm>

#include "game/ui/controls.h"
#include "game/ui/describe.h"
#include "game/ui/sheet.h"
#include "game/ui/style.h"
#include "sim/wear.h"

namespace mu::game {
namespace {

using gfx::Box;

// Shelf.cs's table, in MU's panel units, redrawn to fill MU's window: CNewUINPCShop::Create's
// eight by fifteen at (x + 15, y + 50) stopped at 350 and left a fifth of the window bare, which
// the leather made a texture and this skin made a hole. The user, 2026-09-23: *"vendor height
// has to be same height as other windows, add additional grid slots to vendor if needed"*. So:
// the skin's shared pitch, the first row on the line the bag's worn slots start on (44), and
// EIGHTEEN rows, whose last ends at 422 -- seven over the foot, eight under the head's rule, as
// one ruled block with no air between the cells. MU's stock tables never fill a
// slot past the fifteenth row, so the three extra rows are empty wells, as most of the fifteen
// were. (Since 2026-09-28 every shelf stops at fifteen, below, for the foot's buy-back.) Prices are not printed on the shelf: the user, the same day, *"dont show prices on
// vendor without tooltip"* -- the card under the pointer carries the figure, in full and in the
// colour that says whether he can pay.
constexpr float kOriginX = panel::kGridX, kOriginY = 44.0f;
constexpr int kColumns = 8;
constexpr float kCell = panel::kPitch;
constexpr float kHeight = panel::kHeight;

// A counter that mends gives the three spare rows back to MU's own foot: fifteen rows, which is
// every row a stock table fills (44 + 15 x 21 = 359), then the Repair All strip and the two
// hammers (CNewUINPCShop::RenderRepairMoney at y + 355, SetButtonInfo's 36x29 at x + 54 and
// x + 98, y + 390), each moved down a few units onto this skin's taller grid.
constexpr int kMendingRows = 15;
constexpr Box kStrip{12.0f, 363.0f, 166.0f, 20.0f};
constexpr Box kHammers[2] = {{54.0f, 390.0f, 36.0f, 29.0f}, {98.0f, 390.0f, 36.0f, 29.0f}};
// The buy-back, on the same foot (the user, 2026-09-28: *"button at same position where
// blacksmith has repairs"*, then *"put on right side same as repairs"*): so every shelf is
// fifteen rows now, not only a mending one, and the button is the bag's hammer box exactly --
// the right end of the foot, level with the hammer in the bag beside it. At a mending counter it
// is the row's third, after MU's two hammers.
constexpr Box kUndo{panel::kWellRight - 30.0f, panel::kFootTop + 1.0f, 30.0f, 24.0f};

Box cellOf(int slot, const content::ItemRow& row) {
    return {kOriginX + float(slot % kColumns) * kCell, kOriginY + float(slot / kColumns) * kCell,
            float(row.width) * kCell, float(row.height) * kCell};
}

}  // namespace

bool Shelf::Drawn::operator==(const Drawn& o) const {
    return keeper == o.keeper && hovered == o.hovered &&
           (hovered < 0 || (pointerX == o.pointerX && pointerY == o.pointerY)) && x == o.x &&
           y == o.y && scale == o.scale && closing == o.closing && overClose == o.overClose &&
           level == o.level &&
           strength == o.strength && agility == o.agility && vitality == o.vitality &&
           energy == o.energy && money == o.money && version == o.version &&
           picture == o.picture && mendingOn == o.mendingOn && overHammer == o.overHammer &&
           pressedHammer == o.pressedHammer && mendAll == o.mendAll && undoItem == o.undoItem &&
           undoSeconds == o.undoSeconds && overUndo == o.overUndo &&
           pressingUndo == o.pressingUndo;
}

void Shelf::open(const gfx::Interface& interface, panel::Arts* arts) {
    interface.adopt(canvas_);
    interface.adopt(tip_);
    arts_ = arts;
}

bool Shelf::covers(float x, float y) const {
    return up_ && Box{x_, y_, panel::kWidth * panel::scale(), kHeight * panel::scale()}
                      .has(x, y);
}

void Shelf::restock(const sim::Realm& realm, int folk) {
    // Rebuilt only when the merchant changes: a merchant's stock does not change because a
    // window closed, and reopening the same one is not a shelf refilling.
    keeper_ = folk;
    lines_.clear();
    const content::Tables& tables = *realm.tables();
    const content::Townsperson& person = tables.folk[size_t(folk)];
    merchant_ = person.name;
    int count = 0;
    const sim::Offer* stock = sim::stockOf(person.number, &count);
    for (int i = 0; i < count; ++i) {
        const sim::Offer& offer = stock[i];
        const int32_t item = tables.itemAt(offer.group, offer.number);
        // A shelf drops what it cannot draw: a line with no row costs its slot and nothing
        // else. Market's own rule.
        if (item < 0) continue;
        const content::ItemRow& row = tables.items[size_t(item)];
        Line line{offer, item,
                  sim::buyingPrice(row, offer.refinement, offer.pieces > 0 ? offer.pieces : 1,
                                   offer.skill, row.durability, row.durability)};
        // Last one wins where a table lists a slot twice -- Hanzo's 73.
        bool replaced = false;
        for (Line& had : lines_) {
            if (had.offer.slot == offer.slot) {
                had = line;
                replaced = true;
            }
        }
        if (!replaced) lines_.push_back(line);
    }
}

int Shelf::lineAt(const content::Tables& tables, float ux, float uy) const {
    const float cx = (ux - kOriginX) / kCell, cy = (uy - kOriginY) / kCell;
    const int rows = kMendingRows;
    if (cx < 0.0f || cy < 0.0f || cx >= float(kColumns) || cy >= float(rows)) return -1;
    const int column = int(cx), row = int(cy);
    // Recorded at its top-left, so the cell under the pointer may be covered by something that
    // starts above or to the left of it -- the bag's own walk.
    for (size_t i = 0; i < lines_.size(); ++i) {
        const content::ItemRow& r = tables.items[size_t(lines_[i].item)];
        const int left = lines_[i].offer.slot % kColumns, top = lines_[i].offer.slot / kColumns;
        if (column >= left && column < left + r.width && row >= top && row < top + r.height) {
            return int(i);
        }
    }
    return -1;
}

void Shelf::update(float width, float height, int column, const sim::Realm& realm,
                   const Pointer& pointer, Stage* stage, int* buy, bool* close,
                   ShelfMending* mend, bool* undo) {
    const int trading = realm.trading();
    up_ = trading >= 0 && realm.tables();
    if (!up_) {
        keeper_ = -1;
        return;
    }
    const content::Tables& tables = *realm.tables();
    if (trading != keeper_) restock(realm, trading);
    mends_ = sim::repairsAt(tables.folk[size_t(trading)].number);
    screenW_ = width;
    screenH_ = height;
    x_ = panel::columnX(width, column);
    y_ = panel::panelY(height);
    const float k = panel::scale();
    const float ux = (pointer.x - x_) / k, uy = (pointer.y - y_) / k;
    const bool inside = covers(pointer.x, pointer.y);
    hovered_ = inside ? lineAt(tables, ux, uy) : -1;

    const Box cross = panel::frameClose();
    overClose_ = inside && cross.has(ux, uy);
    int64_t undoLeft = 0;
    const sim::Realm::Sale* sale = realm.lastSale(&undoLeft);
    undoable_ = sale != nullptr;
    overUndo_ = inside && kUndo.has(ux, uy);
    overHammer_ = -1;
    for (int i = 0; mends_ && inside && i < 2; ++i) {
        if (kHammers[i].has(ux, uy)) overHammer_ = i;
    }
    if (pointer.pressed && inside) {
        if (cross.has(ux, uy)) closing_ = true;
        pressing_ = hovered_ >= 0;
        pressedHammer_ = overHammer_;
        pressingUndo_ = overUndo_;
    }
    // Bought on release, not on press -- CNewUINPCShop tests IsRelease(VK_LBUTTON) before it
    // sends, so sliding off an item you did not mean to buy costs nothing. The hammers the same.
    if (pointer.released) {
        if (closing_ && inside && cross.has(ux, uy) && close) *close = true;
        if (pressing_ && hovered_ >= 0 && buy) *buy = lines_[size_t(hovered_)].offer.slot;
        if (mend && pressedHammer_ >= 0 && pressedHammer_ == overHammer_) {
            if (pressedHammer_ == 0) mend->toggle = true;
            else mend->all = true;
        }
        if (undo && undoable_ && pressingUndo_ && overUndo_) *undo = true;
        closing_ = false;
        pressing_ = false;
        pressedHammer_ = -1;
        pressingUndo_ = false;
    }

    standing_.clear();
    for (size_t i = 0; i < lines_.size(); ++i) {
        const content::ItemRow& row = tables.items[size_t(lines_[i].item)];
        // Two units inside its cell's hairline, as the bag fits its own pictures.
        const Box box = cellOf(lines_[i].offer.slot, row).grown(-2.0f);
        standing_.push_back({lines_[i].item, box, lines_[i].offer.refinement, int(i) == hovered_});
    }
    if (stage) stage->stand(standing_, panel::kWidth, kHeight);

    const sim::Body& hero = realm.hero();
    now_ = Drawn{};
    now_.keeper = keeper_;
    now_.hovered = hovered_;
    now_.pointerX = pointer.x;
    now_.pointerY = pointer.y;
    now_.x = x_;
    now_.y = y_;
    now_.scale = k;
    now_.closing = closing_;
    now_.overClose = overClose_;
    now_.level = hero.level;
    now_.strength = hero.points.strength;
    now_.agility = hero.points.agility;
    now_.vitality = hero.points.vitality;
    now_.energy = hero.points.energy;
    now_.money = realm.money();
    // The tip compares with what he wears, which the bag's version moves.
    now_.version = realm.satchel().version();
    now_.picture = stage && stage->picture().valid() ? stage->picture().handle.idx : 0xFFFF;
    now_.mendingOn = mends_ && mendingOn_;
    now_.overHammer = overHammer_;
    now_.pressedHammer = pressedHammer_;
    // RepairAllGold, re-reckoned every frame as CNewUINPCShop::Update does: it moves with the
    // purse's colour and with every point a fight takes off the gear.
    now_.mendAll = mends_ ? realm.repairAllCost() : -1;
    now_.undoItem = sale ? sale->what.item : -1;
    // Rounded up, so the hint never reads 0s while the arrow is still there.
    now_.undoSeconds = sale ? int((undoLeft + 19) / 20) : -1;
    now_.overUndo = overUndo_;
    now_.pressingUndo = pressingUndo_;
    if (now_ == drawn_ && rebuilds_ > 0) return;
    drawn_ = now_;
    rebuild(realm, stage);
}

void Shelf::rebuild(const sim::Realm& realm, Stage* stage) {
    ++rebuilds_;
    canvas_.clear();
    tip_.clear();
    if (!arts_ || !up_) return;
    panel::Arts& arts = *arts_;
    const content::Tables& tables = *realm.tables();
    const float x = x_, y = y_, k = panel::scale();
    const gfx::Face& face = canvas_.face();

    panel::frame(canvas_, arts, x, y, merchant_);
    // The shelf as one ruled block, so a half-stocked shelf reads as a shelf and not as a hole,
    // and the offer under the pointer lit over its whole footprint.
    panel::grid(canvas_, x, y, kOriginX, kOriginY, kColumns, kMendingRows);
    if (mends_) {
        // The strip: MU's `Repair All` and the sum, in gilt on a well, the figure red when the
        // purse cannot cover it (getGoldColor's job there).
        const float u = tip::unit();
        panel::field(canvas_, arts, x, y, kStrip);
        const float size = 14.0f * u;
        const Box strip = panel::scaled(x, y, kStrip);
        const float baseline = controls::middle(strip.y, strip.h, size);
        controls::label(canvas_, strip.x + 6.0f * k, baseline, size, style::kBone2, "Repair All");
        const std::string sum = panel::commas(now_.mendAll) + " Zen";
        const bool affords = realm.money() >= now_.mendAll;
        controls::ranged(canvas_, strip.right() - 6.0f * k, baseline, size,
                         affords ? tip::colourOf(tip::Tone::Yellow) : tip::colourOf(tip::Tone::Red),
                         sum);
        // The hammers: Sanctuary icon squares on MU's two rectangles. Repair is held down with a
        // red rim for as long as repair mode is on, which is how MU says the mode is on at all;
        // the user turned down the gilt ring that said it before.
        for (int i = 0; i < 2; ++i) {
            const Box to = panel::scaled(x, y, kHammers[i]);
            const float side = std::round(std::min(to.w, to.h));
            controls::State state;
            state.lift = overHammer_ == i ? 1.0f : 0.0f;
            state.held = pressedHammer_ == i;
            controls::square(canvas_,
                             {std::round(to.midX() - side * 0.5f), std::round(to.midY() - side * 0.5f), side, side},
                             i == 0 ? controls::Glyph::Hammer : controls::Glyph::Hammers, state, u,
                             false, i == 0 && now_.mendingOn);
        }
        // And what each one is, over it while the pointer is on it: MU's own tooltips,
        // `Repair (L)` and `Repair All (Shift+L)`.
        if (overHammer_ >= 0) {
            const Box over = panel::scaled(x, y, kHammers[overHammer_]);
            controls::hint(canvas_, over.midX(), over.y - 6.0f * u,
                           overHammer_ == 0 ? "Repair (L)" : "Repair All (Shift+L)", u);
        }
    }
    if (hovered_ >= 0) {
        const Line& over = lines_[size_t(hovered_)];
        panel::cell(canvas_, x, y, cellOf(over.offer.slot, tables.items[size_t(over.item)]),
                    sheet::Cell::Over);
    }
    panel::close(canvas_, x, y, now_.overClose, now_.closing);
    // The buy-back, always there and dark while there is nothing to take back, so the foot does
    // not shift under the pointer. Its tooltip over it as the hammers' are, on the tooltip layer
    // because the shelf's pictures draw after this: what, for how much and for how long.
    {
        const float u = tip::unit();
        const Box to = panel::scaled(x, y, kUndo);
        const float side = std::round(std::min(to.w, to.h));
        const Box at{std::round(to.midX() - side * 0.5f), std::round(to.midY() - side * 0.5f), side, side};
        controls::State state;
        state.lift = overUndo_ && undoable_ ? 1.0f : 0.0f;
        state.held = pressingUndo_ && undoable_;
        state.off = !undoable_;
        controls::square(canvas_, at, controls::Glyph::Undo, state, u);
        if (overUndo_) {
            const sim::Realm::Sale* sale = realm.lastSale();
            controls::hint(tip_, at.midX(), at.y - 6.0f * u,
                           sale ? "Buy back " + tables.items[size_t(sale->what.item)].label +
                                      " for " + panel::commas(sale->paid) + " Zen (" +
                                      std::to_string(now_.undoSeconds) + "s)"
                                : std::string("Buy back (nothing sold)"),
                           u);
        }
    }

    const gfx::Art picture = stage ? stage->picture() : gfx::Art{};
    if (picture.valid()) {
        canvas_.image(picture, panel::scaled(x, y, {0.0f, 0.0f, panel::kWidth, kHeight}));
    } else {
        for (const Standing& one : standing_) {
            const Box box = panel::scaled(x, y, one.box);
            canvas_.outline(box, 1.0f, gfx::rgba(0.68f, 0.60f, 0.40f, 0.9f));
            const std::string& name = tables.items[size_t(one.item)].label;
            canvas_.text(box.x + 2.0f, box.y + face.ascent(7.0f * k) + 2.0f, 7.0f * k,
                         panel::kLettering, name.substr(0, std::min<size_t>(name.size(), 6)));
        }
    }

    // The bag's own tooltip, because it is the same tooltip, with the price in the foot:
    // RenderItemInfo's Sell branch, which prints it above the name instead. It is the only place
    // the shelf says a price.
    if (hovered_ >= 0) {
        const Line& over = lines_[size_t(hovered_)];
        const content::ItemRow& row = tables.items[size_t(over.item)];
        const sim::Held carried{over.item, int16_t(over.offer.refinement),
                                int16_t(over.offer.pieces > 0 ? over.offer.pieces : row.durability),
                                over.offer.skill};
        tip::Sheet sheet = describe(tables, carried, realm.wearer(), realm.satchel());
        sheet.price = panel::commas(over.price) + " Zen";
        // The card says whether he can pay: yellow where he can, red where he cannot.
        sheet.priceTone = realm.money() >= over.price ? tip::Tone::Yellow : tip::Tone::Red;
        if (tipStage_) tip::stand(*tipStage_, carried.item, carried.refinement, sheet);
        const Box cell = panel::scaled(x, y, standing_[size_t(hovered_)].box);
        tip::draw(tip_, sheet, cell, screenW_, screenH_);
    }
}

}  // namespace mu::game
