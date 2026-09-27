#include "game/ui/chest.h"

#include <algorithm>
#include <string>

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
constexpr float kMoneySize = 9.5f;
constexpr const char* kButtonArt[2] = {"vault_deposit", "vault_withdraw"};
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
           dragging == o.dragging && (dragging < 0 || (dragX == o.dragX && dragY == o.dragY)) &&
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

int Chest::cellUnder(float x, float y) const {
    if (!covers(x, y)) return -1;
    const float k = panel::scale();
    return cellAt((x - x_) / k, (y - y_) / k);
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
        dragging_ = -1;
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
    hovered_ = cell >= 0 ? vault.holder(tables, cell) : -1;
    button_ = inside ? buttonAt(ux, uy) : -1;

    const Box cross = panel::frameClose();
    overClose_ = inside && cross.has(ux, uy);
    if (pointer.pressed && inside) {
        if (overClose_) {
            closing_ = true;
        } else if (button_ >= 0) {
            pressing_ = button_;
        } else if (hovered_ >= 0) {
            dragging_ = hovered_;
        }
    }
    if (pointer.released) {
        if (out) {
            if (closing_ && overClose_) out->close = true;
            // On release and over the same button, as MU's CNewUIButton fires.
            if (pressing_ >= 0 && pressing_ == button_) {
                (pressing_ == 0 ? out->depositZen : out->withdrawZen) = true;
            }
            if (dragging_ >= 0) {
                if (!inside) {
                    out->outside = dragging_;
                    out->outsideX = pointer.x;
                    out->outsideY = pointer.y;
                } else if (cell >= 0 && cell != dragging_) {
                    out->moveFrom = dragging_;
                    out->moveTo = cell;
                }
            }
        }
        closing_ = false;
        pressing_ = -1;
        dragging_ = -1;
    }
    if (dragging_ >= 0 && vault[dragging_].empty()) dragging_ = -1;

    standing_.clear();
    for (int at = 0; at < sim::kVaultCells; ++at) {
        const sim::Held& what = vault[at];
        if (what.empty()) continue;
        standing_.push_back({what.item, itemBox(tables, at, what).grown(-2.0f), what.refinement,
                             at == hovered_ && dragging_ < 0, what.excellent != 0});
    }
    if (stage) stage->stand(standing_, panel::kWidth, panel::kHeight);

    const sim::Body& hero = realm.hero();
    now_ = Contents{};
    now_.version = vault.version();
    // The tip compares with what he wears, which the bag's version moves.
    now_.bagVersion = realm.satchel().version();
    now_.money = vault.zen();
    now_.dragging = dragging_;
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
    if (hovered_ >= 0 && dragging_ < 0 && !vault[hovered_].empty()) {
        panel::cell(canvas_, x, y, itemBox(tables, hovered_, vault[hovered_]), sheet::Cell::Over);
    }
    if (dragging_ >= 0) {
        panel::cell(canvas_, x, y, itemBox(tables, dragging_, vault[dragging_]),
                    sheet::Cell::Held);
    }

    // The drop target inside the window, asked of the realm's own gate.
    if (dragging_ >= 0 && covers(now_.dragX, now_.dragY)) {
        const int cell = cellAt((now_.dragX - x) / k, (now_.dragY - y) / k);
        const sim::Held& moving = vault[dragging_];
        if (cell >= 0 && cell != dragging_ && !moving.empty()) {
            const content::ItemRow& row = tables.items[size_t(moving.item)];
            const bool fits = vault.room(tables, cell, row.width, row.height, dragging_);
            const int column = cell % sim::kVaultColumns, line = cell / sim::kVaultColumns;
            const int w = std::min<int>(row.width, sim::kVaultColumns - column);
            const int h = std::min<int>(row.height, sim::kVaultRows - line);
            panel::cell(canvas_, x, y, cellBox(cell, w, h),
                        fits ? sheet::Cell::Fits : sheet::Cell::Blocked);
        }
    }

    // The foot: the bag's band and rule, the coins, the vault's own Zen, and the two buttons.
    sheet::band(canvas_,
                panel::scaled(x, y, {0.0f, panel::kFootRule, panel::kWidth,
                                     panel::kHeight - panel::kFootRule}),
                false, panel::kRadius * k);
    sheet::rule(canvas_, x + panel::kEdge * k, y + panel::kFootRule * k,
                (panel::kWidth - panel::kEdge * 2.0f) * k, std::max(1.0f, k * 0.5f));
    canvas_.image(arts.get("bag_zen"), panel::scaled(x, y, kCoins));
    const Box strip = panel::scaled(x, y, kStrip);
    const float size = kMoneySize * k;
    sheet::ranged(canvas_, x + (buttonBox(0).x - 6.0f) * k,
                  panel::centredBaseline(face, strip, size), size, moneyColour(vault.zen()),
                  panel::commas(vault.zen()));
    for (int which = 0; which < 2; ++which) {
        const Box to = panel::scaled(x, y, buttonBox(which));
        const gfx::Art& art = arts.get(kButtonArt[which]);
        const bool pressed = pressing_ == which && button_ == which;
        if (art.valid()) {
            // The sheet's lower state is MU's dimmed one, drawn while it is held down; under the
            // pointer the resting state is lifted a little, as the skin lights a cell.
            canvas_.region(art, to, panel::buttonState(art, pressed),
                           button_ == which && !pressed ? gfx::rgba(1.0f, 1.0f, 1.0f, 1.0f)
                                                        : gfx::rgba(0.86f, 0.86f, 0.86f, 1.0f));
        } else {
            canvas_.outline(to, 1.0f, gfx::rgba(0.68f, 0.60f, 0.40f, 0.9f));
            canvas_.text(to.x + 3.0f, to.y + face.ascent(7.0f * k) + 3.0f, 7.0f * k,
                         panel::kLettering, which == 0 ? "In" : "Out");
        }
    }

    panel::close(canvas_, x, y, now_.overClose, now_.closing);

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

    // What rides the pointer, cut out of the stage at its own footprint -- the bag's drag.
    if (dragging_ >= 0 && !vault[dragging_].empty()) {
        const Box units = itemBox(tables, dragging_, vault[dragging_]);
        const Box to{now_.dragX - units.w * k * 0.5f, now_.dragY - units.h * k * 0.5f,
                     units.w * k, units.h * k};
        if (picture.valid()) {
            const float sx = picture.width / panel::kWidth, sy = picture.height / panel::kHeight;
            canvas_.rect(to.grown(2.0f * k), gfx::rgba(0.0f, 0.0f, 0.0f, 0.28f));
            canvas_.region(picture, to, {units.x * sx, units.y * sy, units.w * sx, units.h * sy},
                           gfx::rgba(1.0f, 1.0f, 1.0f, 0.92f));
        } else {
            canvas_.rect(to, gfx::rgba(0.68f, 0.60f, 0.40f, 0.5f));
        }
    }

    if (dragging_ < 0 && hovered_ >= 0 && !vault[hovered_].empty()) {
        tip::Sheet sheet = describe(tables, vault[hovered_], realm.wearer(), realm.satchel());
        if (tipStage_) tip::stand(*tipStage_, vault[hovered_].item, vault[hovered_].refinement, sheet);
        const Box cell = panel::scaled(x, y, itemBox(tables, hovered_, vault[hovered_]));
        tip::draw(tip_, sheet, cell.midX(), cell.y, screenW_, screenH_);
    } else if (dragging_ < 0 && button_ >= 0) {
        const Box to = panel::scaled(x, y, buttonBox(button_));
        panel::tooltip(tip_, to.midX(), to.y, {{kButtonTip[button_], panel::kOrdinary}},
                       8.0f * panel::unit(), screenW_, screenH_);
    }
}

}  // namespace mu::game
