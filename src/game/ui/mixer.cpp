#include "game/ui/mixer.h"

#include <algorithm>
#include <string>

#include "game/ui/controls.h"
#include "game/ui/describe.h"
#include "game/ui/sheet.h"
#include "game/ui/tip.h"
#include "sim/machine.h"

namespace mu::game {
namespace {

using gfx::Box;

// MU's RenderFrame from its top, in the bag's units: three lines of what the box is, the grid,
// and the prediction under it. MU's own offsets (24, 110, 203 on a 429-unit window) are kept in
// proportion to a head band that is this skin's and not MU's.
constexpr float kInfoTop = 46.0f;
constexpr float kLine = 12.0f;
constexpr float kText = 8.0f;
constexpr float kOriginX = panel::kGridX, kOriginY = 90.0f;
constexpr float kCell = panel::kPitch;
constexpr float kPredictionTop = kOriginY + kCell * float(int(sim::kMachineRows)) + 12.0f;
constexpr float kAskTop = panel::kFootRule - 22.0f;

// MU's inks: g_pRenderText's colours in RenderFrame and GetSourceName.
constexpr uint32_t kReady = gfx::rgba(1.0f, 1.0f, 0.19f);
constexpr uint32_t kNotReady = gfx::rgba(1.0f, 0.19f, 0.19f);
constexpr uint32_t kFigure = gfx::rgba(0.82f, 0.9f, 1.0f);
constexpr uint32_t kPrediction = gfx::rgba(0.86f, 0.86f, 0.86f);
constexpr uint32_t kMissing = gfx::rgba(1.0f, 0.2f, 0.08f);

// The foot: Combine alone, or the confirm's Cancel and Combine.
constexpr float kButtonH = 22.0f, kButtonTop = panel::kFootTop + 2.0f;
Box buttonBox(bool confirming, int which) {
    if (!confirming) return {panel::kWidth * 0.5f - 40.0f, kButtonTop, 80.0f, kButtonH};
    const float w = 72.0f, gap = 8.0f, left = panel::kWidth * 0.5f - w - gap * 0.5f;
    return which == 1 ? Box{left, kButtonTop, w, kButtonH}
                      : Box{left + w + gap, kButtonTop, w, kButtonH};
}

Box cellBox(int cell, int width, int height) {
    return {kOriginX + float(cell % sim::kMachineColumns) * kCell,
            kOriginY + float(cell / sim::kMachineColumns) * kCell, kCell * float(width),
            kCell * float(height)};
}

int cellAt(float ux, float uy) {
    const float cx = (ux - kOriginX) / kCell, cy = (uy - kOriginY) / kCell;
    if (cx < 0.0f || cy < 0.0f || cx >= float(sim::kMachineColumns) ||
        cy >= float(sim::kMachineRows)) {
        return -1;
    }
    return int(cy) * sim::kMachineColumns + int(cx);
}

Box itemBox(const content::Tables& tables, int cell, const sim::Held& what) {
    if (what.empty()) return cellBox(cell, 1, 1);
    const content::ItemRow& row = tables.items[size_t(what.item)];
    return cellBox(cell, row.width, row.height);
}

uint32_t inkOf(sim::Met met) {
    switch (met) {
        case sim::Met::Yes: return kReady;
        case sim::Met::Partly: return kFigure;
        case sim::Met::No: break;
    }
    return kMissing;
}

}  // namespace

bool Mixer::Contents::operator==(const Contents& o) const {
    return version == o.version && bagVersion == o.bagVersion && money == o.money &&
           answer == o.answer && dragging == o.dragging && incoming == o.incoming &&
           incomingTakes == o.incomingTakes &&
           ((dragging < 0 && incoming < 0) || (dragX == o.dragX && dragY == o.dragY)) &&
           hovered == o.hovered &&
           (hovered < 0 || (pointerX == o.pointerX && pointerY == o.pointerY)) &&
           button == o.button && pressing == o.pressing && confirming == o.confirming &&
           closing == o.closing && overClose == o.overClose && spark == o.spark && x == o.x &&
           y == o.y && scale == o.scale && picture == o.picture;
}

void Mixer::open(const gfx::Interface& interface, panel::Arts* arts) {
    interface.adopt(canvas_);
    interface.adopt(tip_);
    arts_ = arts;
}

bool Mixer::covers(float x, float y) const {
    return up_ && Box{x_, y_, panel::kWidth * panel::scale(), panel::kHeight * panel::scale()}
                      .has(x, y);
}

int Mixer::cellUnder(float x, float y) const {
    if (!covers(x, y)) return -1;
    const float k = panel::scale();
    return cellAt((x - x_) / k, (y - y_) / k);
}

int Mixer::buttonAt(float ux, float uy) const {
    if (buttonBox(confirming_, 0).has(ux, uy)) return 0;
    if (confirming_ && buttonBox(true, 1).has(ux, uy)) return 1;
    return -1;
}

void Mixer::update(float seconds, float width, float height, int column, const sim::Realm& realm,
                   int answer, const Pointer& pointer, Stage* stage, MixerRequests* out) {
    up_ = realm.mixing() >= 0 && realm.tables();
    if (!up_) {
        dragging_ = -1;
        pressing_ = -1;
        confirming_ = false;
        sparks_ = 0.0f;
        return;
    }
    const content::Tables& tables = *realm.tables();
    const sim::Machine& box = realm.machine();
    const sim::Judged judged = realm.judged();
    screenW_ = width;
    screenH_ = height;
    x_ = panel::columnX(width, column);
    y_ = panel::panelY(height);
    answer_ = answer;
    const float k = panel::scale();
    const float ux = (pointer.x - x_) / k, uy = (pointer.y - y_) / k;
    const bool inside = covers(pointer.x, pointer.y);
    // The box is locked while the answer stands, as MU locks it at MIX_FINISHED: things come out
    // and nothing goes in until it is empty.
    const bool ready = judged.recipe != sim::Recipe::None && !realm.mixed();
    if (!ready) confirming_ = false;

    const int cell = inside ? cellAt(ux, uy) : -1;
    hovered_ = cell >= 0 && incoming_.empty() ? box.holder(tables, cell) : -1;
    button_ = inside ? buttonAt(ux, uy) : -1;

    const Box cross = panel::frameClose();
    overClose_ = inside && cross.has(ux, uy);
    if (pointer.pressed && inside) {
        if (overClose_) {
            closing_ = true;
        } else if (button_ >= 0) {
            pressing_ = button_;
        } else if (hovered_ >= 0 && sparks_ <= 0.0f) {
            dragging_ = hovered_;
        }
    }
    // A right-click on a thing sends it back to the bag: ProcessMixItemAutoMoveToInventory.
    if (pointer.rightPressed && inside && dragging_ < 0 && hovered_ >= 0 && out) {
        out->back = hovered_;
    }
    if (pointer.released) {
        if (out) {
            if (closing_ && overClose_) out->close = true;
            if (pressing_ >= 0 && pressing_ == button_) {
                if (!confirming_) {
                    // MU's Mix(): refused with a click when nothing is ready, else the box asks.
                    out->click = true;
                    if (ready) confirming_ = true;
                } else if (pressing_ == 0) {
                    out->mix = true;
                    confirming_ = false;
                } else {
                    out->click = true;
                    confirming_ = false;
                }
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
    if (dragging_ >= 0 && box[dragging_].empty()) dragging_ = -1;
    if (confirming_ && !ready) confirming_ = false;

    if (sparks_ > 0.0f) {
        sparks_ = std::max(0.0f, sparks_ - seconds);
        ++sparkFrame_;
    }

    standing_.clear();
    for (int at = 0; at < sim::kMachineCells; ++at) {
        const sim::Held& what = box[at];
        if (what.empty()) continue;
        standing_.push_back({what.item, itemBox(tables, at, what).grown(-2.0f), what.refinement,
                             at == hovered_ && dragging_ < 0, what.excellent != 0});
    }
    if (stage) stage->stand(standing_, panel::kWidth, panel::kHeight);

    now_ = Contents{};
    now_.version = box.version();
    now_.bagVersion = realm.satchel().version();
    now_.money = realm.money();
    now_.answer = answer;
    now_.dragging = dragging_;
    now_.incoming = incoming_.item;
    now_.incomingTakes = incomingTakes_;
    now_.dragX = pointer.x;
    now_.dragY = pointer.y;
    now_.hovered = dragging_ < 0 ? hovered_ : -1;
    now_.pointerX = pointer.x;
    now_.pointerY = pointer.y;
    now_.button = button_;
    now_.pressing = pressing_;
    now_.confirming = confirming_;
    now_.closing = closing_;
    now_.overClose = overClose_;
    now_.spark = sparks_ > 0.0f ? int(sparkFrame_) : -1;
    now_.x = x_;
    now_.y = y_;
    now_.scale = k;
    now_.picture = stage && stage->picture().valid() ? stage->picture().handle.idx : 0xFFFF;
    if (now_ == drawn_ && rebuilds_ > 0) return;
    drawn_ = now_;
    rebuild(realm, stage);
}

void Mixer::rebuild(const sim::Realm& realm, Stage* stage) {
    ++rebuilds_;
    canvas_.clear();
    tip_.clear();
    if (!arts_ || !up_) return;
    panel::Arts& arts = *arts_;
    const content::Tables& tables = *realm.tables();
    const sim::Machine& box = realm.machine();
    const sim::Judged judged = realm.judged();
    const float x = x_, y = y_, k = panel::scale();
    const float u = tip::unit();
    const gfx::Face& face = canvas_.face();
    const float size = kText * k * 1.1f;
    const auto line = [&](float uy, uint32_t ink, const std::string& text) {
        controls::label(canvas_, x + (panel::kEdge + 4.0f) * k, y + uy * k + face.ascent(size),
                        size, ink, text);
    };

    panel::frame(canvas_, arts, x, y, "Chaos Machine");

    // What the box is. MU returns early here at MIX_FINISHED and leaves the title alone; the
    // answer goes in its place, the system log's line being nowhere else to go.
    if (answer_ >= 0) {
        line(kInfoTop, answer_ == 1 ? kReady : kNotReady,
             answer_ == 1 ? "Chaos combination has succeeded" : "Chaos combination has failed");
    } else {
        const bool ready = judged.recipe != sim::Recipe::None;
        line(kInfoTop, ready ? kReady : kNotReady,
             ready ? sim::recipeName(judged.recipe) : "Improper items for combination");
        line(kInfoTop + kLine, kFigure,
             "Combining success rate: " + std::to_string(judged.rate) + "%");
        // Red where he has not the Zen, ours: MU says so only when Combine is pressed.
        line(kInfoTop + kLine * 2.0f,
             judged.zen > realm.money() ? kNotReady : kFigure,
             "Required Zen: " + panel::commas(judged.zen));
    }

    panel::grid(canvas_, x, y, kOriginX, kOriginY, sim::kMachineColumns, sim::kMachineRows);
    if (hovered_ >= 0 && dragging_ < 0 && !box[hovered_].empty()) {
        panel::cell(canvas_, x, y, itemBox(tables, hovered_, box[hovered_]), sheet::Cell::Over);
    }
    if (dragging_ >= 0) {
        panel::cell(canvas_, x, y, itemBox(tables, dragging_, box[dragging_]), sheet::Cell::Held);
    }
    // The drop target inside the box, asked of the realm's own gate.
    if (dragging_ >= 0 && covers(now_.dragX, now_.dragY)) {
        const int cell = cellAt((now_.dragX - x) / k, (now_.dragY - y) / k);
        const sim::Held& moving = box[dragging_];
        if (cell >= 0 && cell != dragging_ && !moving.empty()) {
            const content::ItemRow& row = tables.items[size_t(moving.item)];
            const int under = box.holder(tables, cell);
            const bool fits = box.room(tables, cell, row.width, row.height, dragging_) ||
                              (under >= 0 && under != dragging_ &&
                               sim::tops(tables, box[under], moving));
            const int column = cell % sim::kMachineColumns, rowAt = cell / sim::kMachineColumns;
            const int w = std::min<int>(row.width, sim::kMachineColumns - column);
            const int h = std::min<int>(row.height, sim::kMachineRows - rowAt);
            panel::cell(canvas_, x, y, cellBox(cell, w, h),
                        fits ? sheet::Cell::Fits : sheet::Cell::Blocked);
        }
    }
    // A bag piece carried over the box, by Realm::putIn's gate.
    if (dragging_ < 0 && !incoming_.empty() && covers(now_.dragX, now_.dragY)) {
        const int cell = cellAt((now_.dragX - x) / k, (now_.dragY - y) / k);
        if (cell >= 0) {
            const content::ItemRow& row = tables.items[size_t(incoming_.item)];
            const int under = box.holder(tables, cell);
            const bool topping =
                incomingTakes_ && under >= 0 && sim::tops(tables, box[under], incoming_);
            const int at = topping ? under : cell;
            const content::ItemRow& shape = topping ? tables.items[size_t(box[under].item)] : row;
            const bool fits =
                topping || (incomingTakes_ && box.room(tables, cell, row.width, row.height));
            const int column = at % sim::kMachineColumns, rowAt = at / sim::kMachineColumns;
            const int w = std::min<int>(shape.width, sim::kMachineColumns - column);
            const int h = std::min<int>(shape.height, sim::kMachineRows - rowAt);
            panel::cell(canvas_, x, y, cellBox(at, w, h),
                        fits ? sheet::Cell::Fits : sheet::Cell::Blocked);
        }
    }

    // The prediction: MU's text from 203 down.
    {
        float at = kPredictionTop;
        if (judged.nearest != sim::Recipe::None) {
            if (judged.recipe == sim::Recipe::None) {
                line(at, kPrediction,
                     std::string("Assembly prediction: ") + sim::recipeName(judged.nearest));
                at += kLine + 3.0f;
            }
            for (int i = 0; i < judged.sources; ++i) {
                line(at, inkOf(judged.met[i]), sim::sourceLine(judged.nearest, i));
                at += kLine + 3.0f;
            }
        } else if (judged.empty) {
            line(at, kMissing, "Please put the items to combine");
        } else {
            line(at, kMissing, "Assembly prediction:");
            line(at + kLine + 3.0f, kMissing, "Improper items for combination");
        }
    }

    // The foot: Combine, or the box MU raises over it to ask.
    controls::foot(canvas_, panel::scaled(x, y, {0.0f, 0.0f, panel::kWidth, panel::kHeight}),
                   y + panel::kFootRule * k, u);
    const bool ready = judged.recipe != sim::Recipe::None && !realm.mixed();
    if (confirming_) {
        line(kAskTop, panel::kOrdinary, "Do you want to combine your items?");
        for (int which = 0; which < 2; ++which) {
            controls::State state;
            state.lift = button_ == which ? 1.0f : 0.0f;
            state.held = pressing_ == which && button_ == which;
            controls::button(canvas_, panel::scaled(x, y, buttonBox(true, which)),
                             which == 0 ? "Combine" : "Cancel",
                             which == 0 ? controls::Kind::Primary : controls::Kind::Secondary,
                             state, u);
        }
    } else {
        controls::State state;
        state.lift = button_ == 0 ? 1.0f : 0.0f;
        state.held = pressing_ == 0 && button_ == 0;
        state.off = !ready;
        controls::button(canvas_, panel::scaled(x, y, buttonBox(false, 0)), "Combine",
                         controls::Kind::Primary, state, u);
    }

    panel::close(canvas_, x, y, now_.overClose, now_.closing);

    const gfx::Art picture = stage ? stage->picture() : gfx::Art{};
    if (picture.valid()) {
        canvas_.image(picture, panel::scaled(x, y, {0.0f, 0.0f, panel::kWidth, panel::kHeight}));
    } else {
        for (const Standing& one : standing_) {
            const Box at = panel::scaled(x, y, one.box);
            canvas_.outline(at, 1.0f, gfx::rgba(0.68f, 0.60f, 0.40f, 0.9f));
            const std::string& name = tables.items[size_t(one.item)].label;
            canvas_.text(at.x + 2.0f, at.y + face.ascent(7.0f * k) + 2.0f, 7.0f * k,
                         panel::kLettering, name.substr(0, std::min<size_t>(name.size(), 6)));
        }
    }
    for (int at = 0; at < sim::kMachineCells; ++at) {
        const sim::Held& held = box[at];
        if (held.empty() || held.durability <= 1 || at == dragging_) continue;
        if (!sim::stacks(tables.items[size_t(held.item)])) continue;
        const Box cellAtBox = panel::scaled(x, y, itemBox(tables, at, held));
        canvas_.shadowed(cellAtBox.x, cellAtBox.bottom() - 2.0f * k, 8.0f * k,
                         panel::kLettering, gfx::rgba(0.0f, 0.0f, 0.0f, 0.8f),
                         std::max(1.0f, 0.5f * k), std::to_string(held.durability),
                         gfx::Align::Right, cellAtBox.w - 2.0f * k);
    }

    // RenderMixEffect: every covered cell, every frame, a spark at a random point in it, in
    // `(rand() % 6 + 6) * 0.1, (rand() % 4 + 4) * 0.1, 0.2` -- here a speck and its halo.
    if (sparks_ > 0.0f) {
        uint32_t seed = sparkFrame_ * 2654435761u + 1u;
        const auto next = [&seed](int range) {
            seed = seed * 1103515245u + 12345u;
            return int((seed >> 16) % uint32_t(range));
        };
        const float fade = std::min(1.0f, sparks_ / 0.5f);
        for (int at = 0; at < sim::kMachineCells; ++at) {
            const sim::Held& held = box[at];
            if (held.empty()) continue;
            const content::ItemRow& row = tables.items[size_t(held.item)];
            for (int h = 0; h < row.height; ++h) {
                for (int w = 0; w < row.width; ++w) {
                    const float r = float(next(6) + 6) * 0.1f, g = float(next(4) + 4) * 0.1f;
                    const float cx = kOriginX + (float((at % sim::kMachineColumns) + w) +
                                                 float(next(100)) * 0.01f) * kCell;
                    const float cy = kOriginY + (float((at / sim::kMachineColumns) + h) +
                                                 float(next(100)) * 0.01f) * kCell;
                    const float s = 1.5f + float(next(10)) * 0.25f;
                    canvas_.rect(panel::scaled(x, y, {cx - s * 2.0f, cy - s * 2.0f, s * 4.0f, s * 4.0f}),
                                 gfx::rgba(std::min(r, 1.0f), g, 0.2f, 0.18f * fade));
                    canvas_.rect(panel::scaled(x, y, {cx - s * 0.5f, cy - s * 0.5f, s, s}),
                                 gfx::rgba(1.0f, std::min(1.0f, g + 0.3f), 0.5f, 0.9f * fade));
                }
            }
        }
    }

    // What rides the pointer.
    if (dragging_ >= 0 && !box[dragging_].empty()) {
        const Box units = itemBox(tables, dragging_, box[dragging_]);
        const Box to{now_.dragX - units.w * k * 0.5f, now_.dragY - units.h * k * 0.5f,
                     units.w * k, units.h * k};
        if (picture.valid()) {
            const float sx = picture.width / panel::kWidth, sy = picture.height / panel::kHeight;
            canvas_.region(picture, to, {units.x * sx, units.y * sy, units.w * sx, units.h * sy});
        } else {
            canvas_.rect(to, gfx::rgba(0.68f, 0.60f, 0.40f, 0.5f));
        }
    }

    if (dragging_ < 0 && hovered_ >= 0 && !box[hovered_].empty()) {
        tip::Sheet sheet = describe(tables, box[hovered_], realm.wearer(), realm.satchel());
        if (tipStage_) tip::stand(*tipStage_, box[hovered_].item, box[hovered_].refinement, sheet);
        const Box cell = panel::scaled(x, y, itemBox(tables, hovered_, box[hovered_]));
        tip::draw(tip_, sheet, cell, screenW_, screenH_);
    }
}

}  // namespace mu::game
