#include "game/ui/chest.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "game/ui/controls.h"
#include "game/ui/describe.h"
#include "game/ui/sheet.h"

namespace mu::game {
namespace {

using gfx::Box;

// The grid on the line the bag's worn slots and the shelf's first row start on, at the skin's
// shared pitch: fifteen rows from 44 end at 359, and the foot's rule is at 382.
constexpr float kOriginX = panel::kGridX, kOriginY = 44.0f;
constexpr float kCell = panel::kPitch;

// The foot: MU's money strip and its two coin buttons on one line, the buttons against the
// window's right edge and the figure ranged against the first of them. MU's buttons are 36 x 29
// under a 170-wide strip; here they are cut to the foot's own 26-unit strip at MU's aspect.
constexpr float kButtonW = 30.0f, kButtonH = 24.0f, kButtonGap = 3.0f;
constexpr float kButtonTop = panel::kFootTop + 1.0f;
constexpr float kButtonRight = panel::kWellRight;
constexpr Box kStrip{panel::kEdge, panel::kFootTop, panel::kWidth - panel::kEdge * 2.0f, 26.0f};
constexpr Box kCoins{18.0f, panel::kFootTop + 4.0f, 20.0f, 18.0f};
constexpr const char* kButtonTip[2] = {"Deposit Zen", "Withdraw Zen"};

Box buttonBox(int which) {
    const float right = kButtonRight - float(1 - which) * (kButtonW + kButtonGap);
    return {right - kButtonW, kButtonTop, kButtonW, kButtonH};
}

Box cellBox(int cell, int width, int height) {
    return {kOriginX + float(cell % sim::kVaultColumns) * kCell,
            kOriginY + float(cell / sim::kVaultColumns) * kCell, kCell * float(width),
            kCell * float(height)};
}

int cellAt(float ux, float uy) {
    const float cx = (ux - kOriginX) / kCell, cy = (uy - kOriginY) / kCell;
    if (cx < 0.0f || cy < 0.0f || cx >= float(sim::kVaultColumns) ||
        cy >= float(sim::kVaultRows)) {
        return -1;
    }
    return int(cy) * sim::kVaultColumns + int(cx);
}

Box itemBox(const content::Tables& tables, int cell, const sim::Held& what) {
    if (what.empty()) return cellBox(cell, 1, 1);
    const content::ItemRow& row = tables.items[size_t(what.item)];
    return cellBox(cell, row.width, row.height);
}

}  // namespace

bool Chest::Contents::operator==(const Contents& o) const {
    return version == o.version && bagVersion == o.bagVersion && money == o.money &&
           dragging == o.dragging && incoming == o.incoming &&
           incomingCount == o.incomingCount && incomingTakes == o.incomingTakes &&
           ((dragging < 0 && incoming < 0) || (dragX == o.dragX && dragY == o.dragY)) &&
           hovered == o.hovered &&
           (hovered < 0 || (pointerX == o.pointerX && pointerY == o.pointerY)) &&
           button == o.button && pressing == o.pressing && closing == o.closing &&
           overClose == o.overClose && level == o.level && strength == o.strength &&
           agility == o.agility && vitality == o.vitality && energy == o.energy && x == o.x &&
           y == o.y && scale == o.scale && picture == o.picture;
}

void Chest::open(const gfx::Interface& interface, panel::Arts* arts) {
    interface.adopt(canvas_);
    interface.adopt(tip_);
    arts_ = arts;
}

bool Chest::covers(float x, float y) const {
    return up_ && Box{x_, y_, panel::kWidth * panel::scale(), panel::kHeight * panel::scale()}
                      .has(x, y);
}

int Chest::cellUnder(float x, float y, int width, int height) const {
    if (!covers(x, y)) return -1;
    const float k = panel::scale();
    return panel::landing((x - x_) / k, (y - y_) / k, kOriginX, kOriginY, sim::kVaultColumns, sim::kVaultRows, width, height);
}

int Chest::buttonAt(float ux, float uy) const {
    for (int which = 0; which < 2; ++which) {
        if (buttonBox(which).has(ux, uy)) return which;
    }
    return -1;
}

void Chest::update(float width, float height, int column, const sim::Realm& realm,
                   const Pointer& pointer, Stage* stage, ChestRequests* out) {
    up_ = realm.banking() >= 0 && realm.tables();
    if (!up_) {
        dragging_ = lifting_ = -1;
        pressing_ = -1;
        return;
    }
    const content::Tables& tables = *realm.tables();
    const sim::Vault& vault = realm.vault();
    screenW_ = width;
    screenH_ = height;
    x_ = panel::columnX(width, column);
    y_ = panel::panelY(height);
    const float k = panel::scale();
    const float ux = (pointer.x - x_) / k, uy = (pointer.y - y_) / k;
    const bool inside = covers(pointer.x, pointer.y);

    const int cell = inside ? cellAt(ux, uy) : -1;
    hovered_ = cell >= 0 && incoming_.empty() ? vault.holder(tables, cell) : -1;
    button_ = inside ? buttonAt(ux, uy) : -1;

    const Box cross = panel::frameClose();
    overClose_ = inside && cross.has(ux, uy);
    // Click to pick and click to place, the bag's (bag.h): while a thing rides the pointer a
    // press is the start of placing it and nothing else.
    if (pointer.pressed && inside && dragging_ < 0) {
        if (overClose_) {
            closing_ = true;
        } else if (button_ >= 0) {
            pressing_ = button_;
        } else if (hovered_ >= 0) {
            lifting_ = hovered_;
        }
    }
    // A right-click on a thing takes it out into the bag, as the machine's does (bug report #7,
    // 2026-10-18: "for storage (vault) add right click do deposit/withdraw items").
    if (pointer.rightPressed && inside && dragging_ < 0 && hovered_ >= 0 && out) {
        out->back = hovered_;
    }
    if (pointer.released) {
        if (out) {
            if (closing_ && overClose_) out->close = true;
            // On release and over the same button, as MU's CNewUIButton fires.
            if (pressing_ >= 0 && pressing_ == button_) {
                (pressing_ == 0 ? out->depositZen : out->withdrawZen) = true;
            }
            // Placed by this release, or left on the pointer where it cannot go -- the frame, a
            // red cell -- and back where it was over its own place.
            bool placed = true;
            if (dragging_ >= 0) {
                if (!inside) {
                    out->outside = dragging_;
                    out->outsideX = pointer.x;
                    out->outsideY = pointer.y;
                } else if (cell < 0 || vault[dragging_].empty()) {
                    placed = vault[dragging_].empty();
                } else {
                    const content::ItemRow& row = tables.items[size_t(vault[dragging_].item)];
                    const int to = panel::landing(ux, uy, kOriginX, kOriginY, sim::kVaultColumns,
                                                  sim::kVaultRows, row.width, row.height);
                    // A jewel or a stack goes on what is under the pointer; anything else
                    // stands with the pointer at its middle.
                    const int under = vault.holder(tables, cell);
                    const bool onto = under >= 0 && under != dragging_ &&
                                      (realm.worksOn(vault[dragging_], vault[under]) ||
                                       sim::tops(tables, vault[under], vault[dragging_]));
                    const int at = onto ? cell : to;
                    if (at == dragging_) {
                        // Back where it was lifted from.
                    } else if (onto || (to >= 0 && vault.room(tables, to, row.width, row.height, dragging_))) {
                        out->moveFrom = dragging_;
                        out->moveTo = at;
                    } else {
                        placed = false;
                    }
                }
                if (placed) dragging_ = -1;
            } else if (lifting_ >= 0 && lifting_ == hovered_) {
                dragging_ = lifting_;
            }
        }
        closing_ = false;
        pressing_ = -1;
        lifting_ = -1;
    }
    if (dragging_ >= 0 && vault[dragging_].empty()) dragging_ = -1;

    standing_.clear();
    for (int at = 0; at < sim::kVaultCells; ++at) {
        const sim::Held& what = vault[at];
        if (what.empty()) continue;
        const Box box = itemBox(tables, at, what);
        standing_.push_back({what.item, (at == dragging_ ? panel::carryBox(box) : box).grown(-2.0f), what.refinement,
                             at == hovered_ && dragging_ < 0, what.excellent != 0});
    }
    // The one rune the set sockets are drawn with, in the strip under the window -- the bag's.
    bool anySet = false;
    for (int at = 0; at < sim::kVaultCells && !anySet; ++at) {
        for (int socket = 0; socket < std::min(socketsOf(vault[at]), kMostSockets); ++socket) {
            anySet |= !vault[at].empty() && powerAt(vault[at], socket) != 0;
        }
    }
    if (const int32_t rune = anySet ? runeRow(tables) : -1; rune >= 0) {
        standing_.push_back({rune, panel::kRuneStands, 0, false, false});
    }
    if (stage) stage->stand(standing_, panel::kWidth, panel::kStageTall);

    const sim::Body& hero = realm.hero();
    now_ = Contents{};
    now_.version = vault.version();
    // The tip compares with what he wears, which the bag's version moves.
    now_.bagVersion = realm.satchel().version();
    now_.money = vault.zen();
    now_.dragging = dragging_;
    now_.incoming = incoming_.item;
    now_.incomingCount = incoming_.durability;
    now_.incomingTakes = incomingTakes_;
    now_.dragX = pointer.x;
    now_.dragY = pointer.y;
    now_.hovered = dragging_ < 0 ? hovered_ : -1;
    now_.pointerX = pointer.x;
    now_.pointerY = pointer.y;
    now_.button = button_;
    now_.pressing = pressing_;
    now_.closing = closing_;
    now_.overClose = overClose_;
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

void Chest::rebuild(const sim::Realm& realm, Stage* stage) {
    ++rebuilds_;
    canvas_.clear();
    tip_.clear();
    if (!arts_ || !up_) return;
    panel::Arts& arts = *arts_;
    const content::Tables& tables = *realm.tables();
    const sim::Vault& vault = realm.vault();
    const float x = x_, y = y_, k = panel::scale();
    const gfx::Face& face = canvas_.face();

    panel::frame(canvas_, arts, x, y, "Vault");
    panel::grid(canvas_, x, y, kOriginX, kOriginY, sim::kVaultColumns, sim::kVaultRows);
    // Each piece's cells washed teal, the bag's satchel wash, so the empty cells read as empty
    // (bug #35, 2026-10-20: "the items on the vault don't show the background like the
    // inventory").
    for (int at = 0; at < sim::kVaultCells; ++at) {
        if (vault[at].empty() || at == dragging_) continue;
        canvas_.rect(panel::scaled(x, y, itemBox(tables, at, vault[at])), panel::kTakenWash);
    }
    if (hovered_ >= 0 && dragging_ < 0 && !vault[hovered_].empty()) {
        panel::cell(canvas_, x, y, itemBox(tables, hovered_, vault[hovered_]), sheet::Cell::Over);
    }

    // The drop target inside the window, asked of the realm's own gate.
    if (dragging_ >= 0 && covers(now_.dragX, now_.dragY)) {
        const float ux = (now_.dragX - x) / k, uy = (now_.dragY - y) / k;
        const int pointed = cellAt(ux, uy);
        const sim::Held& moving = vault[dragging_];
        if (pointed >= 0 && !moving.empty()) {
            const content::ItemRow& row = tables.items[size_t(moving.item)];
            // The cell the release asks for: what is under the pointer for a jewel or a stack,
            // else the footprint with the pointer at its middle.
            const int under = vault.holder(tables, pointed);
            const bool onto = under >= 0 && under != dragging_ &&
                              (realm.worksOn(moving, vault[under]) || sim::tops(tables, vault[under], moving));
            const int cell = onto ? pointed
                                  : panel::landing(ux, uy, kOriginX, kOriginY, sim::kVaultColumns,
                                                   sim::kVaultRows, row.width, row.height);
            if (cell != dragging_) {
                // Or a stack of its kind with room, which Realm::rearrange pours into.
                // Or a thing a jewel works, lit whole, which Realm::rearrange applies it to.
                const bool working = onto && realm.worksOn(moving, vault[under]);
                const bool fits = onto || vault.room(tables, cell, row.width, row.height, dragging_);
                const int at = working ? under : cell;
                const content::ItemRow& shape = working ? tables.items[size_t(vault[under].item)] : row;
                const int column = at % sim::kVaultColumns, line = at / sim::kVaultColumns;
                const int w = std::min<int>(shape.width, sim::kVaultColumns - column);
                const int h = std::min<int>(shape.height, sim::kVaultRows - line);
                panel::cell(canvas_, x, y, cellBox(at, w, h),
                            fits ? sheet::Cell::Fits : sheet::Cell::Blocked);
            }
        }
    }
    // And a bag piece carried over the vault, by Realm::deposit's gate: onto a stack of its
    // kind with room, onto a thing it works if it is a jewel (Realm::refineAcross), or where its
    // footprint is free; a worn piece nowhere.
    if (dragging_ < 0 && !incoming_.empty() && covers(now_.dragX, now_.dragY)) {
        const float ux = (now_.dragX - x) / k, uy = (now_.dragY - y) / k;
        const int pointed = cellAt(ux, uy);
        if (pointed >= 0) {
            const content::ItemRow& row = tables.items[size_t(incoming_.item)];
            const int cell = panel::landing(ux, uy, kOriginX, kOriginY, sim::kVaultColumns,
                                            sim::kVaultRows, row.width, row.height);
            const int under = vault.holder(tables, pointed);
            const bool topping = incomingTakes_ && under >= 0 &&
                                 (sim::tops(tables, vault[under], incoming_) ||
                                  realm.worksOn(incoming_, vault[under]));
            const int at = topping ? under : cell;
            const content::ItemRow& shape = topping ? tables.items[size_t(vault[under].item)] : row;
            const bool fits =
                topping || (incomingTakes_ && vault.room(tables, cell, row.width, row.height));
            const int column = at % sim::kVaultColumns, line = at / sim::kVaultColumns;
            const int w = std::min<int>(shape.width, sim::kVaultColumns - column);
            const int h = std::min<int>(shape.height, sim::kVaultRows - line);
            panel::cell(canvas_, x, y, cellBox(at, w, h),
                        fits ? sheet::Cell::Fits : sheet::Cell::Blocked);
        }
    }

    // The foot: the windows' own foot and rule, the coins, the vault's own Zen, and the two
    // buttons -- MU's newui_Bt_money sprites drawn as Sanctuary icon squares, a coin with an
    // arrow into it and one out of it, on MU's own rectangles.
    const float u = tip::unit();
    controls::foot(canvas_, panel::scaled(x, y, {0.0f, 0.0f, panel::kWidth, panel::kHeight}),
                   y + panel::kFootRule * k, u);
    canvas_.image(arts.get("bag_zen"), panel::scaled(x, y, kCoins));
    const Box strip = panel::scaled(x, y, kStrip);
    const float size = 16.0f * u;
    controls::ranged(canvas_, x + (buttonBox(0).x - 6.0f) * k,
                     controls::middle(strip.y, strip.h, size), size, moneyColour(vault.zen()),
                     panel::commas(vault.zen()));
    for (int which = 0; which < 2; ++which) {
        const Box to = panel::scaled(x, y, buttonBox(which));
        const float side = std::round(std::min(to.w, to.h));
        controls::State state;
        state.lift = button_ == which ? 1.0f : 0.0f;
        state.held = pressing_ == which && button_ == which;
        controls::square(canvas_, {std::round(to.midX() - side * 0.5f), std::round(to.midY() - side * 0.5f), side, side},
                         which == 0 ? controls::Glyph::CoinIn : controls::Glyph::CoinOut, state, u);
    }

    panel::close(canvas_, x, y, now_.overClose, now_.closing);

    const gfx::Art picture = stage ? stage->picture() : gfx::Art{};
    if (picture.valid()) {
        panel::standing(canvas_, picture, x, y);
    } else {
        for (const Standing& one : standing_) {
            const Box box = panel::scaled(x, y, one.box);
            canvas_.outline(box, 1.0f, gfx::rgba(0.68f, 0.60f, 0.40f, 0.9f));
            const std::string& name = tables.items[size_t(one.item)].label;
            canvas_.text(box.x + 2.0f, box.y + face.ascent(7.0f * k) + 2.0f, 7.0f * k,
                         panel::kLettering, name.substr(0, std::min<size_t>(name.size(), 6)));
        }
    }
    // The sockets over each piece's middle, empty or set -- the bag's. Not on the one riding
    // the pointer.
    for (int at = 0; at < sim::kVaultCells; ++at) {
        const sim::Held& held = vault[at];
        if (held.empty() || socketsOf(held) == 0 || at == dragging_) continue;
        panel::sockets(canvas_, picture, panel::scaled(x, y, itemBox(tables, at, held)), held, k);
    }
    // A stack's count at its cell's foot -- the bag's.
    for (int at = 0; at < sim::kVaultCells; ++at) {
        const sim::Held& held = vault[at];
        if (held.empty() || held.durability <= 1 || at == dragging_) continue;
        if (sim::poursMost(tables.items[size_t(held.item)]) <= 0) continue;
        const Box box = panel::scaled(x, y, itemBox(tables, at, held));
        canvas_.shadowed(box.x, box.bottom() - 2.0f * k, 8.0f * k, panel::kLettering,
                         gfx::rgba(0.0f, 0.0f, 0.0f, 0.8f), std::max(1.0f, 0.5f * k),
                         std::to_string(held.durability), gfx::Align::Right, box.w - 2.0f * k);
    }

    // What rides the pointer, cut out of the stage at its own footprint -- the bag's drag.
    if (dragging_ >= 0 && !vault[dragging_].empty()) {
        panel::carried(canvas_, picture, itemBox(tables, dragging_, vault[dragging_]), now_.dragX, now_.dragY);
    }

    if (dragging_ < 0 && hovered_ >= 0 && !vault[hovered_].empty()) {
        tip::Sheet sheet = describe(tables, vault[hovered_], realm.wearer(), realm.satchel());
        if (tipStage_) tip::stand(*tipStage_, vault[hovered_].item, vault[hovered_].refinement, sheet);
        const Box cell = panel::scaled(x, y, itemBox(tables, hovered_, vault[hovered_]));
        tip::draw(tip_, sheet, cell, screenW_, screenH_);
    } else if (dragging_ < 0 && button_ >= 0) {
        const Box to = panel::scaled(x, y, buttonBox(button_));
        panel::tooltip(tip_, to.midX(), to.y, {{kButtonTip[button_], panel::kOrdinary}},
                       8.0f * panel::unit(), screenW_, screenH_);
    }
}

}  // namespace mu::game
