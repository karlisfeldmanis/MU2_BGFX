#include "game/ui/shelf.h"

#include "game/ui/describe.h"

namespace mu::game {
namespace {

using gfx::Box;

// Shelf.cs's table, in MU's panel units: CNewUINPCShop::Create's (x + 15, y + 50), eight by
// fifteen, at the bag's twenty-unit cell and 21-unit frame.
constexpr float kOriginX = 15.0f, kOriginY = 50.0f;
constexpr int kColumns = 8, kRows = 15;
constexpr float kCell = 20.0f, kCellArt = 21.0f;
constexpr float kTipSize = 8.0f;
constexpr uint32_t kHighlight = gfx::rgba(1.0f, 0.9f, 0.5f, 0.18f);

Box cellOf(int slot, const content::ItemRow& row) {
    return {kOriginX + float(slot % kColumns) * kCell, kOriginY + float(slot / kColumns) * kCell,
            float(row.width) * kCell, float(row.height) * kCell};
}

}  // namespace

bool Shelf::Drawn::operator==(const Drawn& o) const {
    return keeper == o.keeper && hovered == o.hovered &&
           (hovered < 0 || (pointerX == o.pointerX && pointerY == o.pointerY)) && x == o.x &&
           y == o.y && scale == o.scale && closing == o.closing && level == o.level &&
           strength == o.strength && agility == o.agility && vitality == o.vitality &&
           energy == o.energy && version == o.version && picture == o.picture;
}

void Shelf::open(const gfx::Interface& interface, panel::Arts* arts) {
    interface.adopt(canvas_);
    interface.adopt(tip_);
    arts_ = arts;
}

bool Shelf::covers(float x, float y) const {
    return up_ && Box{x_, y_, panel::kWidth * panel::scale(), panel::kHeight * panel::scale()}
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
    if (cx < 0.0f || cy < 0.0f || cx >= float(kColumns) || cy >= float(kRows)) return -1;
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
                   const Pointer& pointer, Stage* stage, int* buy, bool* close) {
    const int trading = realm.trading();
    up_ = trading >= 0 && realm.tables();
    if (!up_) {
        keeper_ = -1;
        return;
    }
    const content::Tables& tables = *realm.tables();
    if (trading != keeper_) restock(realm, trading);
    screenW_ = width;
    screenH_ = height;
    x_ = panel::columnX(width, column);
    y_ = panel::panelY(height);
    const float k = panel::scale();
    const float ux = (pointer.x - x_) / k, uy = (pointer.y - y_) / k;
    const bool inside = covers(pointer.x, pointer.y);
    hovered_ = inside ? lineAt(tables, ux, uy) : -1;

    const Box cross = panel::frameClose();
    if (pointer.pressed && inside) {
        if (cross.has(ux, uy)) closing_ = true;
        pressing_ = hovered_ >= 0;
    }
    // Bought on release, not on press -- CNewUINPCShop tests IsRelease(VK_LBUTTON) before it
    // sends, so sliding off an item you did not mean to buy costs nothing.
    if (pointer.released) {
        if (closing_ && inside && cross.has(ux, uy) && close) *close = true;
        if (pressing_ && hovered_ >= 0 && buy) *buy = lines_[size_t(hovered_)].offer.slot;
        closing_ = false;
        pressing_ = false;
    }

    standing_.clear();
    for (size_t i = 0; i < lines_.size(); ++i) {
        const content::ItemRow& row = tables.items[size_t(lines_[i].item)];
        Box box = cellOf(lines_[i].offer.slot, row);
        box = {box.x + 1.0f, box.y + 1.0f, box.w - 1.0f, box.h - 1.0f};
        standing_.push_back({lines_[i].item, box, lines_[i].offer.refinement, int(i) == hovered_});
    }
    if (stage) stage->stand(standing_, panel::kWidth, panel::kHeight);

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
    now_.level = hero.level;
    now_.strength = hero.points.strength;
    now_.agility = hero.points.agility;
    now_.vitality = hero.points.vitality;
    now_.energy = hero.points.energy;
    // The tip compares with what he wears, which the bag's version moves.
    now_.version = realm.satchel().version();
    now_.picture = stage && stage->picture().valid() ? stage->picture().handle.idx : 0xFFFF;
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
    // The empty cells, so a half-stocked shelf reads as a shelf and not as a hole.
    const gfx::Art& cellArt = arts.get("bag_cell");
    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kColumns; ++c) {
            canvas_.image(cellArt, panel::scaled(x, y, {kOriginX + float(c) * kCell,
                                                        kOriginY + float(r) * kCell, kCellArt,
                                                        kCellArt}));
        }
    }
    if (hovered_ >= 0) {
        const Line& over = lines_[size_t(hovered_)];
        canvas_.rect(panel::scaled(x, y, cellOf(over.offer.slot, tables.items[size_t(over.item)])),
                     kHighlight);
    }
    panel::close(canvas_, arts, x, y, now_.closing);

    const gfx::Art picture = stage ? stage->picture() : gfx::Art{};
    if (picture.valid()) {
        canvas_.image(picture, panel::scaled(x, y, {0.0f, 0.0f, panel::kWidth, panel::kHeight}));
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
    // RenderItemInfo's Sell branch, which prints it above the name instead.
    if (hovered_ >= 0) {
        const Line& over = lines_[size_t(hovered_)];
        const content::ItemRow& row = tables.items[size_t(over.item)];
        const sim::Held carried{over.item, int16_t(over.offer.refinement),
                                int16_t(over.offer.pieces > 0 ? over.offer.pieces : row.durability),
                                over.offer.skill};
        tip::Sheet sheet = describe(tables, carried, realm.wearer(), realm.satchel());
        sheet.price = panel::commas(over.price) + " Zen";
        sheet.priceTone = over.price >= 1000000 ? tip::Tone::Blue
                          : over.price >= 100000 ? tip::Tone::Green
                                                 : tip::Tone::Yellow;
        if (tipStage_) tip::stand(*tipStage_, carried.item, carried.refinement, sheet);
        const Box cell = panel::scaled(x, y, standing_[size_t(hovered_)].box);
        tip::draw(tip_, sheet, cell.midX(), cell.y, screenW_, screenH_);
    }
}

}  // namespace mu::game
