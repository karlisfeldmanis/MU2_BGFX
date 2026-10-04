#include "game/ui/bag.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "game/ui/controls.h"
#include "game/ui/describe.h"
#include "game/ui/sheet.h"
#include "sim/wear.h"

namespace mu::game {
namespace {

using gfx::Box;

// Bag.cs's table, in MU's panel units -- Create(x + 15, y + 200, ...) at INVENTORY_SQUARE_WIDTH
// 20 -- with the grid moved onto the skin's shared pitch (`panel::kPitch`), so its wells run
// edge to edge with the worn slots' above it, and down seven units: MU's grid began on the very
// line the boots' slot ended, and with the leather's border gone the two blocks touched.
constexpr float kOriginX = panel::kGridX, kOriginY = 207.0f;
constexpr float kCell = panel::kPitch;

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
        // The mount's, ours: the free narrow column beside the armour, opposite the amulet, and
        // a short slot's height so the horse is not a speck (the user: "pets slot ghost icon is
        // small").
        case sim::kMount: return at(3, 1, kNarrow, kShort);
        default: return at(4, 2, kWide, kShort);  // boots
    }
}

// **The cell a slot is drawn as.** MU's worn slots are each a unit wider than their pitch, so
// two neighbours share a border; drawn a unit narrower, two neighbours butt exactly. The worn
// slots are then drawn kWornGap apart, half off each side, so no two of them touch -- the user,
// 2026-10-02: "ewuipent slots dont overlap, we need at least minimal gap". The grid below keeps
// its cells the whole pitch with no gutter. Hit-testing keeps MU's boxes; only the paint is cut.
constexpr float kWornGap = 3.0f;
Box wellOf(const Box& slot, bool worn) {
    if (!worn) return slot;
    const float half = kWornGap / 2.0f;
    return Box{slot.x + half, slot.y + half, slot.w - 1.0f - kWornGap, slot.h - 1.0f - kWornGap};
}

// The ghost in an empty worn slot: MU's own silhouette, cut out of its plate by
// pipeline/slot_ghosts.py as a white mask. MU names the art for the side of the SCREEN: the
// right-hand slot takes weapon(L). Bag.GhostFor.
const char* ghostFor(int slot) {
    switch (slot) {
        case sim::kPet: return "bag_ghost_pet";
        case sim::kMount: return "bag_ghost_mount";
        case sim::kHelm: return "bag_ghost_helm";
        case sim::kWings: return "bag_ghost_wings";
        case sim::kWeaponRight: return "bag_ghost_weapon_left";
        case sim::kWeaponLeft: return "bag_ghost_weapon_right";
        case sim::kArmour: return "bag_ghost_armour";
        case sim::kPants: return "bag_ghost_pants";
        case sim::kGloves: return "bag_ghost_gloves";
        case sim::kBoots: return "bag_ghost_boots";
        case sim::kAmulet: return "bag_ghost_amulet";
        default: return "bag_ghost_ring";
    }
}

// Where a ghost stands in its cell: fitted to the cell less kGhostAir of its shorter side (five
// units on a 41-wide slot, two and a half on a ring's) and stood down to kGhostFill of that, at
// its own aspect, centred. A flat five took half a ring's cell and left a speck. Every ghost then has the same air round it
// whatever its shape, and none touches the cell's hairline. The fill is what it is because a
// ghost drawn out to the cell's edge is a picture in the cell rather than a mark on it, and it
// then stood taller than the piece the slot holds -- the user, 2026-09-23: *"icoons to large"*.
constexpr float kGhostFill = 0.80f;
constexpr float kGhostAir = 0.12f;

Box ghostBox(const Box& cell, const gfx::Art& art) {
    const Box room = cell.grown(-std::min(5.0f, kGhostAir * std::min(cell.w, cell.h)));
    const float s = std::min(room.w / art.width, room.h / art.height) * kGhostFill;
    const float w = art.width * s, h = art.height * s;
    return {room.x + (room.w - w) * 0.5f, room.y + (room.h - h) * 0.5f, w, h};
}
// Faint, and the window's own warm lettering rather than white: a ghost says what the slot is
// for and must lose to any piece standing in the slot beside it. A slot with a piece in it keeps
// its ghost, fainter, because the piece is drawn over it a shade transparent -- the user,
// 2026-09-23: *"we want some transparencey so we can little bit see that icoon behind weared
// item"* -- so the slot goes on saying what it is for with a helm standing in it.
constexpr uint32_t kGhostInk = gfx::rgba(0.90f, 0.86f, 0.76f, 0.17f);
constexpr uint32_t kGhostUnder = gfx::rgba(0.90f, 0.86f, 0.76f, 0.22f);

// Where the worn block ends and the satchel begins: below the boots' row (154 + 46) and above
// the grid (kOriginY). The stage's picture is cut on this line so the worn half can be drawn a
// shade transparent -- the piece, the slot's ghost under it, the cell under that -- while a
// thing in the satchel, which has no ghost beneath it and nothing to say, stays solid.
constexpr float kWornFoot = 203.0f;
// The foot's hammer: MU's inventory repair button (CNewUIMyInventory, `IMAGE_INVENTORY_REPAIR_BTN`),
// cut to the foot's strip as the vault's coin buttons are and set against the wells' right edge,
// with the Zen figure ranged against it.
constexpr Box kHammer{panel::kWellRight - 30.0f, panel::kFootTop + 1.0f, 30.0f, 24.0f};
constexpr uint32_t kWornInk = gfx::rgba(1.0f, 1.0f, 1.0f, 0.88f);

// A piece's sockets over its middle, in units: the item card's socket ring (tip::Mark::Ring), a
// bronze rim round the orange stone of a Rune of Creation or round a dark hole, stacked down the
// piece when it has two or three, as Diablo II sets its holes over the picture. As large as
// `kSocketStone` where the piece has the room, and smaller on a narrow one.
constexpr float kSocketStone = 7.5f;
// A socket is the jewel's own shape (the user, 2026-09-29: "border has to be same shape as
// jewel"): the Rune of Creation photographs as an upright lozenge this much narrower than it is
// tall, and the setting, the hole and the stone all follow it. `kRuneFills` is how much of its
// square the stage's picture of the rune is jewel, so the stone can be sized to its hole.
constexpr float kJewelAspect = 0.8f;
constexpr float kRuneFills = 0.81f;
constexpr float kSocketGap = 2.0f;
// **The Rune of Creation in its socket, as its own picture** (the user, 2026-09-29: "we need to
// show that jewel creation is attached in rune slot"). One rune stands on the bag's stage in a
// strip below the window, which the window never shows, and each set socket is given that
// picture inside its ring. The stage is taller than the window by the strip.
constexpr float kRuneStrip = 24.0f;
constexpr float kStageTall = panel::kHeight + kRuneStrip;
constexpr gfx::Box kRuneStands{4.0f, panel::kHeight + 2.0f, 20.0f, 20.0f};

// Where a piece's sockets sit, in whatever space `box` is in: centres down its middle, one
// radius for all. The bag's rings and its runes are laid by this one rule.
float socketsIn(const gfx::Box& box, int sockets, float unit, float* cy) {
    const float gap = kSocketGap * unit;
    const float r = std::min({kSocketStone * unit, box.w * 0.46f / kJewelAspect,
                              (box.h - gap * float(sockets + 1)) / float(sockets) * 0.5f});
    const float pitch = r * 2.0f + gap;
    const float top = box.midY() - pitch * float(sockets - 1) * 0.5f;
    for (int at = 0; at < sockets; ++at) cy[at] = top + pitch * float(at);
    return r;
}
constexpr uint32_t kSocketEmber = gfx::rgba(0.9f, 0.42f, 0.04f, 1.0f);
constexpr uint32_t kSocketGlint = gfx::rgba(1.0f, 0.78f, 0.45f, 0.85f);
constexpr uint32_t kSocketShade = gfx::rgba(0.0f, 0.0f, 0.0f, 0.7f);
constexpr uint32_t kSocketRim = gfx::rgba(0.62f, 0.45f, 0.24f, 1.0f);
constexpr uint32_t kSocketHole = gfx::rgba(0.05f, 0.035f, 0.03f, 0.82f);

bool creationJewel(const content::Tables& tables, const sim::Held& held) {
    return !held.empty() && sim::creation(tables.items[size_t(held.item)]);
}

// A lozenge the jewel's shape, `h` from its middle to its point.
void lozenge(gfx::Canvas& canvas, float cx, float cy, float h, uint32_t colour) {
    const float w = h * kJewelAspect;
    const float xy[8] = {cx, cy - h, cx + w, cy, cx, cy + h, cx - w, cy};
    canvas.polygon(nullptr, xy, nullptr, 4, colour);
}

// A filled circle, as a fan of sixteen: the canvas has rectangles and polygons, no round.
void disc(gfx::Canvas& canvas, float cx, float cy, float r, uint32_t colour) {
    float xy[32];
    for (int i = 0; i < 16; ++i) {
        const float a = float(i) * 6.2831853f / 16.0f;
        xy[i * 2] = cx + std::cos(a) * r;
        xy[i * 2 + 1] = cy + std::sin(a) * r;
    }
    canvas.polygon(nullptr, xy, nullptr, 16, colour);
}

// The Zen strip is drawn by `panel::zenFoot`, on the panel's shared foot rule.

// The drop target is the skin's own two cell states now (`sheet::Cell::Fits` and `Blocked`),
// which are MU's blue and red at the weight the rest of this window is drawn at.

}  // namespace

const char* ghostArt(int slot) { return ghostFor(slot); }

bool Bag::Contents::operator==(const Contents& o) const {
    return version == o.version && money == o.money && dragging == o.dragging &&
           incoming == o.incoming && incomingCount == o.incomingCount &&
           ((dragging < 0 && incoming < 0) || (dragX == o.dragX && dragY == o.dragY)) &&
           hovered == o.hovered &&
           (hovered < 0 || (pointerX == o.pointerX && pointerY == o.pointerY)) &&
           closing == o.closing && overClose == o.overClose && level == o.level && strength == o.strength &&
           agility == o.agility && vitality == o.vitality && energy == o.energy && x == o.x &&
           y == o.y && scale == o.scale && picture == o.picture && mending == o.mending &&
           canMend == o.canMend && overHammer == o.overHammer && pressingHammer == o.pressingHammer;
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
    // A vault piece riding over the bag hovers nothing: no lit cell, no card under it.
    if (!incoming_.empty()) hovered_ = -1;

    const Box cross = panel::frameClose();
    overClose_ = inside && cross.has(ux, uy);
    overHammer_ = inside && kHammer.has(ux, uy);
    if (pointer.pressed && inside) {
        if (cross.has(ux, uy)) {
            closing_ = true;
        } else if (overHammer_) {
            pressingHammer_ = true;
        } else if (hovered_ >= 0 && mending_) {
            // Repair mode: the click mends it where it lies and lifts nothing
            // (CNewUIMyInventory's REPAIR_MODE_ON branch, SendRepairItemRequest).
            if (out) out->repair = hovered_;
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
        // On release, as every button in this interface: the realm and the desk decide whether
        // the mode may turn on, and a refusal is heard there.
        if (pressingHammer_ && overHammer_ && out) out->toggleMending = true;
        pressingHammer_ = false;
        if (dragging_ >= 0 && out) {
            const int from = dragging_;
            if (!inside) {
                out->outside = from;
                out->outsideX = pointer.x;
                out->outsideY = pointer.y;
            } else if (cell >= 0 && cell != from) {
                // ApplyJewels first, as HandlePickedItemPlacement asks it: over a thing the
                // jewel goes on, the drop is that and never a swap.
                const int under = bag.holder(tables, cell);
                // A Rune of Creation set in a socket rides the same request: the realm's refine
                // takes either, and refuses whole what it cannot do.
                if (under >= 0 && under != from &&
                    (sim::refinable(tables, bag[from], bag[under]) ||
                     sim::settable(tables, bag[from], bag[under], realm.wearer().kin,
                                   realm.wearer().second))) {
                    out->refineJewel = from;
                    out->refineTarget = under;
                } else {
                    out->moveFrom = from;
                    out->moveTo = cell;
                }
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
        // **The picture stands inside its cell's hairline**: two units in for a bag cell, whose
        // twenty-one are mostly picture, three for a worn slot, which has room to spare. Every
        // picture then has the same margin as its neighbours and none touches a rule (the stage
        // adds its own air on top).
        box = wellOf(box, sim::wearable(slot)).grown(sim::wearable(slot) ? -3.0f : -2.0f);
        standing_.push_back({what.item, box, what.refinement,
                             slot == hovered_ && dragging_ < 0, what.excellent != 0});
    }
    // The one rune the set sockets are drawn with, in the strip under the window.
    bool anySet = false;
    for (int slot = 0; slot < sim::kSlots && !anySet; ++slot) {
        for (int at = 0; at < std::min(socketsOf(bag[slot]), kMostSockets); ++at) {
            anySet |= !bag[slot].empty() && powerAt(bag[slot], at) != 0;
        }
    }
    if (const int32_t rune = anySet ? runeRow(tables) : -1; rune >= 0) {
        standing_.push_back({rune, kRuneStands, 0, false, false});
    }
    if (stage) stage->stand(standing_, panel::kWidth, kStageTall);

    const sim::Body& hero = realm.hero();
    now_ = Contents{};
    now_.version = bag.version();
    now_.money = realm.money();
    now_.dragging = dragging_;
    now_.incoming = incoming_.item;
    now_.incomingCount = incoming_.durability;
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
    now_.mending = mending_;
    // The foot's hammer is the SELF repair button: lit from level 50 only, even at a counter,
    // where the shelf has hammers of its own (the user, 2026-09-24).
    now_.canMend = realm.selfMending();
    now_.overHammer = overHammer_;
    now_.pressingHammer = pressingHammer_;
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
        const Box box = wellOf(wornBox(slot), true);
        panel::cell(canvas_, x, y, box,
                    slot == hovered_ && dragging_ < 0 ? sheet::Cell::Over
                    : slot == dragging_              ? sheet::Cell::Held
                                                     : sheet::Cell::Rest);
        const gfx::Art& ghost = arts.get(ghostFor(slot));
        if (ghost.valid()) {
            // **The shape alone, faint.** `bag_slot_*` was a plate -- leather, a gold rim, and
            // the shape painted a little lighter on it -- and drawn over the flat skin at any
            // tint it was a tile filling the cell. `bag_ghost_*` is MU's own silhouette cut off
            // that leather by pipeline/slot_ghosts.py, so the cell stays the cell and the shape
            // is the only thing laid on it. The user, 2026-09-23: *"equipment slots could looks
            // better"*. Under a worn piece it is drawn a little stronger, because the piece is
            // over it: what shows through the armour is that strength times what the picture
            // lets through, and what shows round the armour is the rest of the cell.
            canvas_.image(ghost, panel::scaled(x, y, ghostBox(box, ghost)),
                          bag[slot].empty() ? kGhostInk : kGhostUnder);
        }
    }
    // The satchel: one ruled block, eight by eight, no air between the cells.
    panel::grid(canvas_, x, y, kOriginX, kOriginY, sim::kBagColumns, sim::kBagRows);
    // **Wear, as a wash under the piece.** NewUIMyInventory.cpp:1313-1322 lays the band's colour
    // over a worn slot at a quarter strength -- yellow at half, orange at three tenths,
    // red-orange at a fifth, red broken -- and CNewUIInventoryCtrl colours a bag item by the same
    // four (ITEM_COLOR_DURABILITY_*). Both here are the cell's wash, under the picture.
    for (int slot = 0; slot < sim::kSlots; ++slot) {
        const sim::Held& held = bag[slot];
        if (held.empty()) continue;
        const content::ItemRow& row = tables.items[size_t(held.item)];
        if (!sim::wears(row)) continue;
        const sim::Worn band =
            sim::wornBand(held.durability, sim::maximumDurability(row, held));
        if (band == sim::Worn::Fine) continue;
        // Two tables, as MuMain has two: the worn slot's at a quarter (0.15 and 0.5 for the
        // middle bands), the bag cell's at 0.4 (0.33 and 0.66) -- NewUIInventoryCtrl.cpp:983-998.
        const bool worn = sim::wearable(slot);
        const float g = band == sim::Worn::Broken  ? 0.0f
                        : band == sim::Worn::Fifth ? (worn ? 0.15f : 0.33f)
                        : band == sim::Worn::Third ? (worn ? 0.5f : 0.66f)
                                                   : 1.0f;
        const Box cell = wellOf(itemBox(tables, slot, held), worn);
        canvas_.rect(panel::scaled(x, y, cell), gfx::rgba(1.0f, g, 0.0f, worn ? 0.25f : 0.4f));
    }
    // And the thing under the pointer lit over its whole footprint, not over the one cell it is
    // recorded in: a shield is two cells by two and it is the shield that is hovered.
    if (hovered_ >= 0 && dragging_ < 0 && !bag[hovered_].empty()) {
        const Box over = itemBox(tables, hovered_, bag[hovered_]);
        panel::cell(canvas_, x, y, wellOf(over, sim::wearable(hovered_)), sheet::Cell::Over);
    }

    // The foot: the panel's own, with the hero's Zen in it.
    panel::zenFoot(canvas_, arts, x, y, realm.money(), kHammer.x - 6.0f);
    {
        // The hammer: a Sanctuary icon square in the foot's strip. Held down with a red rim
        // while repair mode is on -- the user turned down the gilt ring it had -- and inactive
        // below kSelfRepairLevel, where it does nothing away from a blacksmith.
        const Box to = panel::scaled(x, y, kHammer);
        const float side = std::round(std::min(to.w, to.h));
        controls::State state;
        state.lift = overHammer_ ? 1.0f : 0.0f;
        state.held = pressingHammer_;
        state.off = !now_.canMend;
        controls::square(canvas_, {std::round(to.right() - side), std::round(to.midY() - side * 0.5f), side, side},
                         controls::Glyph::Hammer, state, tip::unit(), false,
                         mending_ && now_.canMend);
    }

    // The cell a dragged thing would land on: blue where the move would be taken and red where
    // not, asked of the realm's own gate. Bag.Target.
    const float ux = (now_.dragX - x) / k, uy = (now_.dragY - y) / k;
    // One box over the whole footprint and no rule between its cells: the vault's target, so
    // both windows mark a drop alike (the user, 2026-10-02: "just outline").
    const auto light = [&](const int* cells, int count, bool ok) {
        Box over = slotBox(cells[0]);
        for (int i = 1; i < count; ++i) {
            const Box one = slotBox(cells[i]);
            const float right = std::max(over.right(), one.right());
            const float bottom = std::max(over.bottom(), one.bottom());
            over.x = std::min(over.x, one.x);
            over.y = std::min(over.y, one.y);
            over.w = right - over.x;
            over.h = bottom - over.y;
        }
        panel::cell(canvas_, x, y, wellOf(over, sim::wearable(cells[0])),
                    ok ? sheet::Cell::Fits : sheet::Cell::Blocked);
    };
    if (dragging_ >= 0 && covers(now_.dragX, now_.dragY)) {
        const int cell = slotAt(ux, uy);
        const sim::Held& moving = bag[dragging_];
        if (cell >= 0 && cell != dragging_ && !moving.empty()) {
            const bool fits = sim::movable(tables, realm.wearer(), bag, dragging_, cell);
            const content::ItemRow& row = tables.items[size_t(moving.item)];
            int cells[sim::kSlots];
            const int count = bag.covered(cell, row.width, row.height, cells);
            // A jewel over a thing it goes on lights the thing, all of its cells, and not the
            // jewel's own footprint: MuMain's CanUpgradeItem colour, MU2's Bag.Target.
            const int under = bag.holder(tables, cell);
            // A Rune of Creation lights the piece it is over, all of it: blue where it would be
            // set, red over a socket it cannot go in -- taken already, the other kind of piece, or
            // another class's power. The drop there would only be a swap, and a swap is not what
            // the hand holding the jewel means.
            const bool setting = under >= 0 && under != dragging_ &&
                                 sim::settable(tables, moving, bag[under], realm.wearer().kin,
                                               realm.wearer().second);
            const bool refused = under >= 0 && under != dragging_ && !setting &&
                                 creationJewel(tables, moving) && socketsOf(bag[under]) > 0;
            if (under >= 0 && under != dragging_ &&
                (setting || refused || sim::refinable(tables, moving, bag[under]))) {
                const content::ItemRow& target = tables.items[size_t(bag[under].item)];
                const int covering = bag.covered(under, target.width, target.height, cells);
                light(cells, covering, !refused);
            } else if (count == 0) {
                light(&cell, 1, false);
            } else {
                light(cells, count, fits);
            }
        }
    }
    // The same for a vault piece carried over the bag, by Realm::withdraw's gate: onto a stack
    // of its kind with room, or into the satchel where its footprint is free. A worn slot
    // takes nothing from the vault.
    if (dragging_ < 0 && !incoming_.empty() && covers(now_.dragX, now_.dragY)) {
        const int cell = slotAt(ux, uy);
        if (cell >= 0) {
            const content::ItemRow& row = tables.items[size_t(incoming_.item)];
            const int under = sim::baggable(cell) ? bag.holder(tables, cell) : -1;
            int cells[sim::kSlots];
            if (under >= 0 && sim::tops(tables, bag[under], incoming_)) {
                const content::ItemRow& target = tables.items[size_t(bag[under].item)];
                const int covering = bag.covered(under, target.width, target.height, cells);
                light(cells, covering, true);
            } else {
                const bool fits = sim::baggable(cell) && bag.room(tables, cell, row.width, row.height);
                const int count = sim::baggable(cell) ? bag.covered(cell, row.width, row.height, cells) : 0;
                if (count == 0) light(&cell, 1, false);
                else light(cells, count, fits);
            }
        }
    }

    panel::close(canvas_, x, y, now_.overClose, now_.closing);

    // The item layer: the stage's one picture of the whole window, over the cells. Where there
    // is no stage yet, each thing's name in its box, so the bag can be read without pictures.
    const gfx::Art picture = stage ? stage->picture() : gfx::Art{};
    if (picture.valid()) {
        // In two bands on the line the worn block ends: the worn half a shade transparent, so a
        // slot goes on saying what it is for with a piece standing in it, and the satchel whole.
        const float sx = picture.width / panel::kWidth, sy = picture.height / kStageTall;
        const float below = panel::kHeight - kWornFoot;
        canvas_.region(picture, panel::scaled(x, y, {0.0f, 0.0f, panel::kWidth, kWornFoot}),
                       {0.0f, 0.0f, panel::kWidth * sx, kWornFoot * sy}, kWornInk);
        canvas_.region(picture, panel::scaled(x, y, {0.0f, kWornFoot, panel::kWidth, below}),
                       {0.0f, kWornFoot * sy, panel::kWidth * sx, below * sy});
    } else {
        for (const Standing& one : standing_) {
            const Box box = panel::scaled(x, y, one.box);
            canvas_.outline(box, 1.0f, gfx::rgba(0.68f, 0.60f, 0.40f, 0.9f));
            const std::string& name = tables.items[size_t(one.item)].label;
            canvas_.text(box.x + 2.0f, box.y + face.ascent(7.0f * k) + 2.0f, 7.0f * k,
                         panel::kLettering, name.substr(0, std::min<size_t>(name.size(), 6)));
        }
    }
    // **The sockets, over the piece's middle** (the user, 2026-09-28: "make socket placeholders
    // bigger and put in on item center"). Every one, empty or set, on every socketed piece in
    // the bag and on him: one in the middle, two or three stacked down it, each an orange stone
    // in its ring where a Rune of Creation is set and a dark hole where none is. Not on the one
    // riding the pointer, which is drawn whole above.
    for (int slot = 0; slot < sim::kSlots; ++slot) {
        const sim::Held& held = bag[slot];
        const int sockets = held.empty() ? 0 : std::min(socketsOf(held), kMostSockets);
        if (sockets == 0 || slot == dragging_) continue;
        const Box box =
            panel::scaled(x, y, wellOf(itemBox(tables, slot, held), sim::wearable(slot)));
        float centres[kMostSockets];
        const float r = socketsIn(box, sockets, k, centres);
        for (int at = 0; at < sockets; ++at) {
            const float cx = box.midX(), cy = centres[at];
            lozenge(canvas_, cx, cy, r + std::max(1.0f, 0.8f * k), kSocketShade);
            lozenge(canvas_, cx, cy, r, kSocketRim);
            lozenge(canvas_, cx, cy, r * 0.7f, kSocketHole);
            if (powerAt(held, at) == 0) continue;
            // The rune itself, cut from the strip and set in the hole at the hole's own size, so
            // the bronze shows all the way round it; the ember where there is no picture.
            if (picture.valid()) {
                const float sx = picture.width / panel::kWidth, sy = picture.height / kStageTall;
                const float side = r * 0.7f * 2.0f / kRuneFills;
                canvas_.region(picture, {cx - side * 0.5f, cy - side * 0.5f, side, side},
                               {kRuneStands.x * sx, kRuneStands.y * sy, kRuneStands.w * sx,
                                kRuneStands.h * sy});
            } else {
                disc(canvas_, cx, cy + r * 0.03f, r * 0.54f, kSocketEmber);
                disc(canvas_, cx - r * 0.18f, cy - r * 0.14f, r * 0.17f, kSocketGlint);
            }
        }
    }
    // A stack's count at its cell's foot, right-aligned, as WoW prints it; a single piece
    // says nothing. Not on the one riding the pointer.
    for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
        const sim::Held& held = bag[slot];
        if (held.empty() || held.durability <= 1 || slot == dragging_) continue;
        if (!sim::stacks(tables.items[size_t(held.item)])) continue;
        const Box box = panel::scaled(x, y, itemBox(tables, slot, held));
        canvas_.shadowed(box.x, box.bottom() - 2.0f * k, 8.0f * k, panel::kLettering,
                         gfx::rgba(0.0f, 0.0f, 0.0f, 0.8f), std::max(1.0f, 0.5f * k),
                         std::to_string(held.durability), gfx::Align::Right, box.w - 2.0f * k);
    }

    // What rides the pointer while it is dragged: its own picture, cut out of the stage at its
    // own footprint, so lifting a sword out of the bag does not resize it.
    if (dragging_ >= 0 && !bag[dragging_].empty()) {
        const Box units = itemBox(tables, dragging_, bag[dragging_]);
        const Box to{now_.dragX - units.w * k * 0.5f, now_.dragY - units.h * k * 0.5f, units.w * k,
                     units.h * k};
        if (picture.valid()) {
            const float sx = picture.width / panel::kWidth, sy = picture.height / kStageTall;
            // The picture alone, whole: the dark box once under it read as a background the
            // thing was carried on (the user, 2026-10-02).
            canvas_.region(picture, to, {units.x * sx, units.y * sy, units.w * sx, units.h * sy});
        } else {
            canvas_.rect(to, gfx::rgba(0.68f, 0.60f, 0.40f, 0.5f));
        }
    }

    // The hammer's card, kept short (the user, 2026-09-24: the first one said too much). Locked:
    // when it opens and where to go until then. Open: the price rule and what his worn gear
    // would cost him now.
    if (overHammer_ && dragging_ < 0) {
        const bool open = now_.canMend;
        tip::Sheet card;
        card.name = open ? "Self repair (L)" : "Self repair";
        card.nameTone = open ? tip::Tone::White : tip::Tone::Gray;
        card.base = open ? "2.5X THE BLACKSMITH'S PRICE" : "UNLOCKS AT LEVEL 50";
        card.wide = 220.0f;
        if (open) {
            int64_t own = 0;
            for (int slot = 0; slot < sim::kSlots; ++slot) {
                const sim::Held& h = bag[slot];
                if (h.empty()) continue;
                own += sim::repairPrice(tables.items[size_t(h.item)], h.refinement, h.skill,
                                        h.durability, false);
            }
            tip::Section cost;
            cost.rows.push_back(own > 0
                ? tip::Row{"Your gear", {tip::Value{panel::commas(own) + " Zen",
                                                    realm.money() >= own ? tip::Tone::Yellow
                                                                         : tip::Tone::Red}},
                           "", tip::Tone::White}
                : tip::Row{"", {}, "Nothing needs repair", tip::Tone::Gray});
            card.sections.push_back(cost);
        } else {
            card.note = "Until then, repair at Hanzo";
            card.noteTone = tip::Tone::Gray;
        }
        const Box over = panel::scaled(x, y, kHammer);
        tip::draw(tip_, card, over, screenW_, screenH_);
    }

    // The tip, last, and never during a drag. The card carries the thing's own picture, cut
    // out of the window's stage at its own footprint -- the same region the drag lifts.
    if (dragging_ < 0 && hovered_ >= 0 && !bag[hovered_].empty()) {
        tip::Sheet sheet = describe(tables, bag[hovered_], realm.wearer(), bag);
        // In repair mode the foot says what mending it costs: RenderRepairInfo's `Repairing
        // cost: %s` (GT 238), in the colour that says whether he can pay.
        if (mending_) {
            const int64_t cost = realm.repairCost(hovered_);
            if (cost > 0) {
                sheet.price = "Repair " + panel::commas(cost) + " Zen";
                sheet.priceTone = realm.money() >= cost ? tip::Tone::Yellow : tip::Tone::Red;
            } else if (sim::wears(tables.items[size_t(bag[hovered_].item)])) {
                sheet.price = "Whole";
                sheet.priceTone = tip::Tone::Gray;
            } else {
                sheet.price = "Cannot Repair";  // GT 926
                sheet.priceTone = tip::Tone::Red;
            }
        }
        // What it fetches over a counter, anywhere and not only at one: the realm's own sum, so
        // the card and the sale cannot disagree. Nothing on what is worn, which cannot be sold.
        if (const int64_t fetches = realm.sellValue(hovered_); fetches > 0) {
            sheet.sell = panel::commas(fetches);
            sheet.coin = arts.get("bag_zen");
        }
        if (tipStage_) {
            tip::stand(*tipStage_, bag[hovered_].item, bag[hovered_].refinement, sheet);
        }
        // Over the item's own cells rather than over the pointer: a tall thing hovered near its
        // top had the card lying across the rest of it.
        const Box cell = panel::scaled(x, y, itemBox(tables, hovered_, bag[hovered_]));
        tip::draw(tip_, sheet, cell, screenW_, screenH_);
    }
}

}  // namespace mu::game
