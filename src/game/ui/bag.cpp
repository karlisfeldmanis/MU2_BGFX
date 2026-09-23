#include "game/ui/bag.h"

#include <algorithm>
#include <string>

#include "game/ui/describe.h"
#include "game/ui/sheet.h"

namespace mu::game {
namespace {

using gfx::Box;

// Bag.cs's table, in MU's panel units.
constexpr float kOriginX = 15.0f, kOriginY = 200.0f;  // Create(x + 15, y + 200, ...)
constexpr float kCell = 20.0f;                        // INVENTORY_SQUARE_WIDTH
// MU's cell art was 21 across at a pitch of 20 -- the extra unit was a border two neighbours
// shared. The skin draws a well inside the pitch instead, and the difference is the gutter: at a
// unit the grid read as a table with lines, at a unit and a half as sixty-four wells.
constexpr float kGutter = 1.5f;

// The equipment grid: five columns and three rows, each slot one unit wider than its pitch so
// neighbours share a border. EquipColumn = Run(11, [41, 25, 41, 25, 41]), EquipRow = Run(44,
// [46, 66, 46]).
constexpr float kWide = 41.0f, kNarrow = 25.0f, kShort = 46.0f, kTall = 66.0f;
constexpr float kEquipColumn[5] = {11.0f, 51.0f, 75.0f, 115.0f, 139.0f};
constexpr float kEquipRow[3] = {44.0f, 89.0f, 154.0f};

Box at(int column, int row, float wide, float tall) {
    // A ring is square and its row is not, so the small slots are centred down their row.
    return {kEquipColumn[column], kEquipRow[row] + ((row == 1 ? kTall : kShort) - tall) / 2.0f,
            wide, tall};
}

Box wornBox(int slot) {
    switch (slot) {
        case sim::kPet: return at(0, 0, kWide, kShort);
        case sim::kHelm: return at(2, 0, kWide, kShort);
        case sim::kWings: return at(3, 0, kNarrow + kWide - 1.0f, kShort);
        case sim::kWeaponRight: return at(0, 1, kWide, kTall);
        case sim::kAmulet: return at(1, 1, kNarrow, kNarrow);
        case sim::kArmour: return at(2, 1, kWide, kTall);
        case sim::kWeaponLeft: return at(4, 1, kWide, kTall);
        case sim::kGloves: return at(0, 2, kWide, kShort);
        case sim::kRingRight: return at(1, 2, kNarrow, kNarrow);
        case sim::kPants: return at(2, 2, kWide, kShort);
        case sim::kRingLeft: return at(3, 2, kNarrow, kNarrow);
        default: return at(4, 2, kWide, kShort);  // boots
    }
}

// The ghost in an empty worn slot. MU names the art for the side of the SCREEN: the
// right-hand slot takes weapon(L). Bag.GhostFor.
const char* ghostFor(int slot) {
    switch (slot) {
        case sim::kPet: return "bag_slot_pet";
        case sim::kHelm: return "bag_slot_helm";
        case sim::kWings: return "bag_slot_wings";
        case sim::kWeaponRight: return "bag_slot_weapon_left";
        case sim::kWeaponLeft: return "bag_slot_weapon_right";
        case sim::kArmour: return "bag_slot_armour";
        case sim::kPants: return "bag_slot_pants";
        case sim::kGloves: return "bag_slot_gloves";
        case sim::kBoots: return "bag_slot_boots";
        case sim::kAmulet: return "bag_slot_amulet";
        default: return "bag_slot_ring";
    }
}

// The Zen strip: MU's (11, 364, 170, 26) moved to the foot, the coins at its left and the
// figure set against them at MU's own distance.
constexpr Box kMoneyStrip{11.0f, 380.0f, 170.0f, 26.0f};
constexpr Box kMoneyIcon{18.0f, 384.0f, 20.0f, 18.0f};
constexpr float kMoneyFrom = 18.0f + 20.0f + 6.0f;
constexpr float kMoneySize = 9.5f;
constexpr float kTipSize = 8.0f;

// The drop target is the skin's own two cell states now (`sheet::Cell::Fits` and `Blocked`),
// which are MU's blue and red at the weight the rest of this window is drawn at.

}  // namespace

bool Bag::Contents::operator==(const Contents& o) const {
    return version == o.version && money == o.money && dragging == o.dragging &&
           (dragging < 0 || (dragX == o.dragX && dragY == o.dragY)) && hovered == o.hovered &&
           (hovered < 0 || (pointerX == o.pointerX && pointerY == o.pointerY)) &&
           closing == o.closing && overClose == o.overClose && level == o.level && strength == o.strength &&
           agility == o.agility && vitality == o.vitality && energy == o.energy && x == o.x &&
           y == o.y && scale == o.scale && picture == o.picture;
}

Box Bag::slotBox(int slot) {
    if (sim::wearable(slot)) return wornBox(slot);
    const int cell = slot - sim::kWorn;
    return {kOriginX + float(cell % sim::kBagColumns) * kCell,
            kOriginY + float(cell / sim::kBagColumns) * kCell, kCell, kCell};
}

int Bag::slotAt(float ux, float uy) {
    for (int slot = 0; slot < sim::kWorn; ++slot) {
        if (wornBox(slot).has(ux, uy)) return slot;
    }
    const float cx = (ux - kOriginX) / kCell, cy = (uy - kOriginY) / kCell;
    if (cx < 0.0f || cy < 0.0f || cx >= float(sim::kBagColumns) || cy >= float(sim::kBagRows)) {
        return -1;
    }
    return sim::kWorn + int(cy) * sim::kBagColumns + int(cx);
}

Box Bag::itemBox(const content::Tables& tables, int slot, const sim::Held& what) const {
    // A worn slot is its own size whatever the item: a breastplate does not make its slot
    // taller. A bag item covers its footprint.
    const Box box = slotBox(slot);
    if (sim::wearable(slot) || what.empty()) return box;
    const content::ItemRow& row = tables.items[size_t(what.item)];
    return {box.x, box.y, kCell * float(row.width), kCell * float(row.height)};
}

void Bag::open(const gfx::Interface& interface, panel::Arts* arts) {
    interface.adopt(canvas_);
    interface.adopt(tip_);
    arts_ = arts;
}

bool Bag::covers(float x, float y) const {
    return up_ && Box{x_, y_, panel::kWidth * panel::scale(), panel::kHeight * panel::scale()}
                      .has(x, y);
}

void Bag::update(float width, float height, int column, const sim::Realm& realm,
                 const Pointer& pointer, Stage* stage, BagRequests* out) {
    up_ = realm.tables() != nullptr;
    screenW_ = width;
    screenH_ = height;
    x_ = panel::columnX(width, column);
    y_ = panel::panelY(height);
    const float k = panel::scale();
    const content::Tables& tables = *realm.tables();
    const sim::Satchel& bag = realm.satchel();
    const float ux = (pointer.x - x_) / k, uy = (pointer.y - y_) / k;
    const bool inside = covers(pointer.x, pointer.y);
    pointerX_ = pointer.x;
    pointerY_ = pointer.y;

    // Resolved to the item's own slot rather than the cell under the pointer, so a two-cell
    // potion hovered by its lower half is still the potion.
    const int cell = inside ? slotAt(ux, uy) : -1;
    hovered_ = cell >= 0 ? bag.holder(tables, cell) : -1;

    const Box cross = panel::frameClose();
    overClose_ = inside && cross.has(ux, uy);
    if (pointer.pressed && inside) {
        if (cross.has(ux, uy)) {
            closing_ = true;
        } else if (hovered_ >= 0) {
            // The item covering the cell: a click on the blade picks up the sword.
            dragging_ = hovered_;
        }
    }
    if (pointer.rightPressed && inside && dragging_ < 0 && hovered_ >= 0 && out) {
        out->use = hovered_;
    }
    if (pointer.released) {
        if (closing_ && inside && cross.has(ux, uy) && out) out->close = true;
        closing_ = false;
        if (dragging_ >= 0 && out) {
            const int from = dragging_;
            if (!inside) {
                out->outside = from;
                out->outsideX = pointer.x;
                out->outsideY = pointer.y;
            } else if (cell >= 0 && cell != from) {
                out->moveFrom = from;
                out->moveTo = cell;
            }
        }
        dragging_ = -1;
    }
    // A drag that the item has vanished from under -- sold, drunk -- is over.
    if (dragging_ >= 0 && bag[dragging_].empty()) dragging_ = -1;

    // What stands on the stage: every carried thing at its picture's box, the hovered one
    // turning. MU's RenderObjectScreen spins the one under the pointer and nothing else.
    standing_.clear();
    for (int slot = 0; slot < sim::kSlots; ++slot) {
        const sim::Held& what = bag[slot];
        if (what.empty()) continue;
        Box box = itemBox(tables, slot, what);
        // **The picture is fitted to the WELL, not to the pitch.** A footprint is whole cells
        // and the well inside it is a gutter narrower, so a picture measured against the pitch
        // stands on its own border: an armour drawn two cells by two touched all four edges and
        // read as a sticker over the grid. Two units of air inside the well on top of that, so
        // every picture has the same margin whatever its footprint (the stage adds its own).
        if (!sim::wearable(slot)) {
            box = Box{box.x, box.y, box.w - kGutter, box.h - kGutter}.grown(-2.0f);
        } else {
            box = box.grown(-3.0f);
        }
        standing_.push_back({what.item, box, what.refinement,
                             slot == hovered_ && dragging_ < 0});
    }
    if (stage) stage->stand(standing_, panel::kWidth, panel::kHeight);

    const sim::Body& hero = realm.hero();
    now_ = Contents{};
    now_.version = bag.version();
    now_.money = realm.money();
    now_.dragging = dragging_;
    now_.dragX = pointer.x;
    now_.dragY = pointer.y;
    now_.hovered = dragging_ < 0 ? hovered_ : -1;
    now_.pointerX = pointer.x;
    now_.pointerY = pointer.y;
    now_.closing = closing_;
    now_.overClose = overClose_;
    // The five stats every requirement is read against: they colour the tooltip and the drop
    // target without appearing anywhere else, and a level-up changes both with nothing in the
    // bag having moved. Bag.Contents.Asked.
    now_.level = hero.level;
    now_.strength = hero.points.strength;
    now_.agility = hero.points.agility;
    now_.vitality = hero.points.vitality;
    now_.energy = hero.points.energy;
    now_.x = x_;
    now_.y = y_;
    now_.scale = k;
    now_.picture = stage && stage->picture().valid() ? stage->picture().handle.idx : 0xFFFF;
    if (now_ == drawn_ && rebuilds_ > 0) return;
    drawn_ = now_;
    rebuild(realm, stage);
}

void Bag::rebuild(const sim::Realm& realm, Stage* stage) {
    ++rebuilds_;
    canvas_.clear();
    tip_.clear();
    if (!arts_ || !realm.tables()) return;
    panel::Arts& arts = *arts_;
    const content::Tables& tables = *realm.tables();
    const sim::Satchel& bag = realm.satchel();
    const float x = x_, y = y_, k = panel::scale();
    const gfx::Face& face = canvas_.face();

    panel::frame(canvas_, arts, x, y, "Inventory");

    // **The worn slots: a deep well, and MU's own silhouette in it.** The art is the game's
    // (`bag_slot_helm` and its ten fellows) and it stays -- the user, 2026-09-23: *"it could help
    // if you used MU graphics for equipment"* -- drawn at two fifths over the well rather than at
    // full strength on leather, so an empty slot says what it is for without competing with the
    // piece in the slot beside it.
    for (int slot = 0; slot < sim::kWorn; ++slot) {
        const Box box = wornBox(slot);
        panel::cell(canvas_, x, y, box,
                    slot == hovered_ && dragging_ < 0 ? sheet::Cell::Over
                    : slot == dragging_              ? sheet::Cell::Held
                                                     : sheet::Cell::Rest);
        if (bag[slot].empty()) {
            const gfx::Art& ghost = arts.get(ghostFor(slot));
            if (ghost.valid()) {
                // **Tinted dark, not merely faded.** `bag_slot_*` is not a cutout: each one is a
                // pale plate with the shape painted on it, cut for MU's leather. Drawn white at
                // any alpha the plate itself survives and every empty slot reads as a light tile
                // -- which is exactly what the first pass did, and what the wells are supposed to
                // be the opposite of. The canvas multiplies the vertex colour through the
                // texture, so a dark warm tint sinks the plate into the well and leaves the
                // silhouette, which is darker in the art still, as the only thing that reads.
                canvas_.image(ghost, panel::scaled(x, y, box.grown(-3.0f)),
                              gfx::rgba(0.50f, 0.45f, 0.34f, 0.46f));
            }
        }
    }
    // The satchel, one well a cell. MU's shared-border 21-unit frame is gone with the art: the
    // wells are drawn at the pitch less a unit, which is the gutter this skin reads by.
    for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
        const Box box = slotBox(slot);
        panel::cell(canvas_, x, y, {box.x, box.y, kCell - kGutter, kCell - kGutter},
                    sheet::Cell::Rest);
    }
    // And the thing under the pointer lit over its whole footprint, not over the one cell it is
    // recorded in: a shield is two cells by two and it is the shield that is hovered.
    if (hovered_ >= 0 && dragging_ < 0 && !bag[hovered_].empty()) {
        const Box over = itemBox(tables, hovered_, bag[hovered_]);
        panel::cell(canvas_, x, y, {over.x, over.y, over.w - kGutter, over.h - kGutter},
                    sheet::Cell::Over);
    }

    // The foot: a band of light coming up out of the window's bottom edge, then a rule, the
    // coins, and the figure ranged right against the window's own margin.
    sheet::band(canvas_, panel::scaled(x, y, {0.0f, kMoneyStrip.y - 6.0f, panel::kWidth,
                                              panel::kHeight - (kMoneyStrip.y - 6.0f)}),
                false);
    sheet::rule(canvas_, x + panel::kEdge * k, y + (kMoneyStrip.y - 6.0f) * k,
                (panel::kWidth - panel::kEdge * 2.0f) * k, std::max(1.0f, k * 0.5f));
    canvas_.image(arts.get("bag_zen"), panel::scaled(x, y, kMoneyIcon));
    const float moneySize = kMoneySize * k;
    const Box strip = panel::scaled(x, y, kMoneyStrip);
    sheet::kicker(canvas_, x + kMoneyFrom * k, panel::centredBaseline(face, strip, 8.0f * k),
                  8.0f * k, "ZEN");
    sheet::ranged(canvas_, strip.right() - 4.0f * k,
                  panel::centredBaseline(face, strip, moneySize), moneySize,
                  moneyColour(realm.money()), panel::commas(realm.money()));

    // The cell a dragged thing would land on: blue where the move would be taken and red where
    // not, asked of the realm's own gate. Bag.Target.
    const float ux = (now_.dragX - x) / k, uy = (now_.dragY - y) / k;
    if (dragging_ >= 0 && covers(now_.dragX, now_.dragY)) {
        const int cell = slotAt(ux, uy);
        const sim::Held& moving = bag[dragging_];
        if (cell >= 0 && cell != dragging_ && !moving.empty()) {
            const bool fits = sim::movable(tables, realm.wearer(), bag, dragging_, cell);
            const content::ItemRow& row = tables.items[size_t(moving.item)];
            int cells[sim::kSlots];
            const int count = bag.covered(cell, row.width, row.height, cells);
            const auto light = [&](int at, bool ok) {
                const Box box = slotBox(at);
                const bool worn = sim::wearable(at);
                panel::cell(canvas_, x, y,
                            worn ? box : Box{box.x, box.y, kCell - kGutter, kCell - kGutter},
                            ok ? sheet::Cell::Fits : sheet::Cell::Blocked);
            };
            if (count == 0) light(cell, false);
            for (int i = 0; i < count; ++i) light(cells[i], fits);
        }
    }

    panel::close(canvas_, x, y, now_.overClose, now_.closing);

    // The item layer: the stage's one picture of the whole window, over the cells. Where there
    // is no stage yet, each thing's name in its box, so the bag can be read without pictures.
    const gfx::Art picture = stage ? stage->picture() : gfx::Art{};
    const Box whole = panel::scaled(x, y, {0.0f, 0.0f, panel::kWidth, panel::kHeight});
    if (picture.valid()) {
        canvas_.image(picture, whole);
    } else {
        for (const Standing& one : standing_) {
            const Box box = panel::scaled(x, y, one.box);
            canvas_.outline(box, 1.0f, gfx::rgba(0.68f, 0.60f, 0.40f, 0.9f));
            const std::string& name = tables.items[size_t(one.item)].label;
            canvas_.text(box.x + 2.0f, box.y + face.ascent(7.0f * k) + 2.0f, 7.0f * k,
                         panel::kLettering, name.substr(0, std::min<size_t>(name.size(), 6)));
        }
    }

    // What rides the pointer while it is dragged: its own picture, cut out of the stage at its
    // own footprint, so lifting a sword out of the bag does not resize it.
    if (dragging_ >= 0 && !bag[dragging_].empty()) {
        const Box units = itemBox(tables, dragging_, bag[dragging_]);
        const Box to{now_.dragX - units.w * k * 0.5f, now_.dragY - units.h * k * 0.5f, units.w * k,
                     units.h * k};
        if (picture.valid()) {
            const float sx = picture.width / panel::kWidth, sy = picture.height / panel::kHeight;
            // A shade under it and the thing itself a little transparent: what the hand is
            // holding is between the window and the pointer, and at full strength it reads as
            // something that has already been put down.
            canvas_.rect(to.grown(2.0f * k), gfx::rgba(0.0f, 0.0f, 0.0f, 0.28f));
            canvas_.region(picture, to, {units.x * sx, units.y * sy, units.w * sx, units.h * sy},
                           gfx::rgba(1.0f, 1.0f, 1.0f, 0.92f));
        } else {
            canvas_.rect(to, gfx::rgba(0.68f, 0.60f, 0.40f, 0.5f));
        }
    }

    // The tip, last, and never during a drag. The card carries the thing's own picture, cut
    // out of the window's stage at its own footprint -- the same region the drag lifts.
    if (dragging_ < 0 && hovered_ >= 0 && !bag[hovered_].empty()) {
        tip::Sheet sheet = describe(tables, bag[hovered_], realm.wearer(), bag);
        if (tipStage_) {
            tip::stand(*tipStage_, bag[hovered_].item, bag[hovered_].refinement, sheet);
        }
        // Over the item's own cells rather than over the pointer: a tall thing hovered near its
        // top had the card lying across the rest of it.
        const Box cell = panel::scaled(x, y, itemBox(tables, hovered_, bag[hovered_]));
        tip::draw(tip_, sheet, cell.midX(), cell.y, screenW_, screenH_);
    }
}

}  // namespace mu::game
