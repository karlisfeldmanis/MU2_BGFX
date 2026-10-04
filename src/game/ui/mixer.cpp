#include "game/ui/mixer.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "game/ui/controls.h"
#include "game/ui/describe.h"
#include "game/ui/sheet.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"
#include "sim/machine.h"

namespace mu::game {
namespace {

using gfx::Box;

// In the bag's units, top to bottom: the service row, the box, then the page.
constexpr Box kRow{panel::kEdge, 44.0f, panel::kWidth - panel::kEdge * 2.0f, 22.0f};
constexpr Box kPrevBox{79.0f, 47.0f, 14.0f, 16.0f};
constexpr Box kNextBox{161.0f, 47.0f, 14.0f, 16.0f};
constexpr float kBoxKicker = 78.0f;
constexpr float kOriginX = panel::kGridX, kOriginY = 90.0f;
constexpr float kCell = panel::kPitch;
constexpr float kPageTop = kOriginY + kCell * 4.0f + 8.0f;  // 182
constexpr float kLeft = panel::kEdge + 2.0f;
constexpr float kRight = panel::kWidth - panel::kEdge - 2.0f;
constexpr float kLine = 11.5f;
constexpr float kText = 7.6f;    // a line, in units
constexpr float kTitle = 9.5f;   // the recipe
constexpr float kSocketRow = 15.0f;
constexpr float kAskTop = panel::kFootRule - 18.0f;

// The foot: the cost at the left, one button or the confirm's two at the right.
constexpr float kButtonTop = panel::kFootTop + 2.0f, kButtonH = 22.0f;
constexpr Box kRunBox{kRight - 64.0f, kButtonTop, 64.0f, kButtonH};
constexpr Box kConfirmRun{kRight - 62.0f, kButtonTop, 62.0f, kButtonH};
constexpr Box kConfirmCancel{kRight - 62.0f - 4.0f - 50.0f, kButtonTop, 50.0f, kButtonH};
constexpr Box kCoin{panel::kEdge + 2.0f, panel::kFootTop + 7.0f, 12.0f, 12.0f};

// The journal's page turn (quest_dialog.cpp): out quicker than in, sliding as it fades.
constexpr float kTurnOut = 0.14f;
constexpr float kTurnIn = 0.20f;
constexpr float kTurnSlide = 22.0f;

// MuMain's inks, GetSourceName's and RenderFrame's; the gold is the travel list's selection.
constexpr uint32_t kReady = gfx::rgba(1.0f, 1.0f, 0.19f);
constexpr uint32_t kMissing = gfx::rgba(1.0f, 0.2f, 0.08f);
constexpr uint32_t kFigure = gfx::rgba(0.82f, 0.9f, 1.0f);
constexpr uint32_t kGold = gfx::rgba(1.0f, 0.82f, 0.0f);
constexpr uint32_t kGoldSoft = gfx::rgba(0.79f, 0.66f, 0.35f);
constexpr uint32_t kGoldFloor = gfx::rgba(0.114f, 0.090f, 0.047f);
constexpr uint32_t kLostFloor = gfx::rgba(0.14f, 0.06f, 0.047f);
constexpr uint32_t kLostRim = gfx::rgba(0.42f, 0.13f, 0.10f);
constexpr uint32_t kZen = gfx::rgba(0.5f, 0.7f, 1.0f);
constexpr uint32_t kSocketRim = gfx::rgba(0.62f, 0.45f, 0.24f);
constexpr uint32_t kSocketHole = gfx::rgba(0.05f, 0.035f, 0.03f);

// WoW's ladder, as the card names a rune (describe's rarityTone).
uint32_t rarityInk(sim::Rarity rarity) {
    switch (rarity) {
        case sim::Rarity::Rare: return gfx::rgba(0.0f, 0.44f, 0.87f);
        case sim::Rarity::Epic: return gfx::rgba(0.64f, 0.21f, 0.93f);
        case sim::Rarity::Legendary: return gfx::rgba(0x1e / 255.0f, 1.0f, 0.0f);  // green, 2026-10-04
    }
    return kFigure;
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

Box socketRow(int i) {
    return {panel::kEdge, kPageTop + 10.0f + float(i) * kSocketRow, panel::kWidth - panel::kEdge * 2.0f,
            kSocketRow - 1.0f};
}

// A filled circle, as a fan.
void disc(gfx::Canvas& canvas, float cx, float cy, float r, uint32_t ink) {
    constexpr int kSides = 20;
    float xy[kSides * 2];
    for (int i = 0; i < kSides; ++i) {
        const float a = float(i) * 6.2831853f / float(kSides);
        xy[i * 2] = cx + std::cos(a) * r;
        xy[i * 2 + 1] = cy + std::sin(a) * r;
    }
    canvas.polygon(nullptr, xy, nullptr, kSides, ink);
}

// How a need reads: met yellow, partly met pale blue, missing or too many red. A need of any
// number is pale blue until one is in.
uint32_t needInk(const sim::Need& n) {
    if (n.need == 0) return n.have > 0 ? kReady : kFigure;
    if (n.have == n.need) return kReady;
    return n.have > 0 && n.have < n.need ? kFigure : kMissing;
}

std::string needCount(const sim::Need& n) {
    if (n.need == 0) return n.have > 0 ? std::to_string(n.have) : "any";
    return std::to_string(n.have) + "/" + std::to_string(n.need);
}

// The confirm's question, MU's for a Combine.
std::string question(sim::Service service, const sim::Judged& j) {
    switch (service) {
        case sim::Service::Combine: return "Do you want to combine your items?";
        case sim::Service::RemoveRune: return j.title + "?";
        case sim::Service::AddSocket: return "Add the socket?";
        case sim::Service::FuseRunes: return "Fuse the three runes?";
    }
    return "";
}

// The answer's head, gold for a success.
const char* madeHead(sim::Service service) {
    switch (service) {
        case sim::Service::Combine: return "Combined";
        case sim::Service::RemoveRune: return "Rune removed";
        case sim::Service::AddSocket: return "Socket added";
        case sim::Service::FuseRunes: return "Runes fused";
    }
    return "";
}

}  // namespace

bool Mixer::Contents::operator==(const Contents& o) const {
    return version == o.version && bagVersion == o.bagVersion && money == o.money &&
           answer == o.answer && service == o.service && socket == o.socket && turn == o.turn &&
           dragging == o.dragging && incoming == o.incoming && incomingTakes == o.incomingTakes &&
           ((dragging < 0 && incoming < 0) || (dragX == o.dragX && dragY == o.dragY)) &&
           hovered == o.hovered &&
           (hovered < 0 || (pointerX == o.pointerX && pointerY == o.pointerY)) &&
           over == o.over && pressing == o.pressing && confirming == o.confirming &&
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

float Mixer::turnAlpha() const {
    const float t = std::clamp(std::fabs(turn_), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float Mixer::turnShift() const {
    const float away = 1.0f - turnAlpha();
    return (turn_ < 0.0f ? -float(turnDir_) : float(turnDir_)) * away * kTurnSlide;
}

int Mixer::hitAt(float ux, float uy, const sim::Realm& realm) const {
    if (confirming_) {
        if (kConfirmRun.has(ux, uy)) return kRun;
        if (kConfirmCancel.has(ux, uy)) return kCancel;
    } else if (kRunBox.has(ux, uy)) {
        return kRun;
    }
    if (kPrevBox.grown(3.0f).has(ux, uy)) return kPrev;
    if (kNextBox.grown(3.0f).has(ux, uy)) return kNext;
    if (sim::Service(service_) == sim::Service::RemoveRune && turn_ >= 1.0f) {
        const sim::Judged j = realm.judged(sim::Service::RemoveRune, socket_);
        if (j.target >= 0) {
            const sim::Held& thing = realm.machine()[j.target];
            for (int i = 0; i < thing.sockets && i < sim::kMostSockets; ++i) {
                if (socketRow(i).has(ux, uy)) return kSocket0 + i;
            }
        }
    }
    return kNone;
}

void Mixer::update(float seconds, float width, float height, int column, const sim::Realm& realm,
                   int answer, const std::string& words, const Pointer& pointer, Stage* stage,
                   MixerRequests* out) {
    up_ = realm.mixing() >= 0 && realm.tables();
    if (!up_) {
        dragging_ = -1;
        pressing_ = kNone;
        confirming_ = false;
        sparks_ = 0.0f;
        turn_ = 1.0f;
        pending_ = -1;
        return;
    }
    const content::Tables& tables = *realm.tables();
    const sim::Machine& box = realm.machine();
    screenW_ = width;
    screenH_ = height;
    x_ = panel::columnX(width, column);
    y_ = panel::panelY(height);
    answer_ = answer;
    words_ = words;
    const float k = panel::scale();
    const float ux = (pointer.x - x_) / k, uy = (pointer.y - y_) / k;
    const bool inside = covers(pointer.x, pointer.y);

    // The page turn: the old page out, then the service changes and the new one comes in.
    if (turn_ < 1.0f) {
        const bool wasOut = turn_ < 0.0f;
        turn_ = std::min(1.0f, turn_ + seconds / (turn_ < 0.0f ? kTurnOut : kTurnIn));
        if (wasOut && turn_ >= 0.0f && pending_ >= 0) {
            service_ = pending_;
            pending_ = -1;
            socket_ = -1;
        }
    }
    const sim::Judged judged = realm.judged(service(), socket_);
    if (service() == sim::Service::RemoveRune) socket_ = judged.socket;
    // Things come out and nothing goes in while the answer stands, as MU locks the box at
    // MIX_FINISHED.
    const bool takeOut = realm.mixed() || (answer >= 0 && !box.empty());
    const bool ready = judged.ready && !realm.mixed();
    if (!ready || turn_ < 1.0f) confirming_ = false;

    const int cell = inside ? cellAt(ux, uy) : -1;
    hovered_ = cell >= 0 && incoming_.empty() ? box.holder(tables, cell) : -1;
    over_ = inside ? hitAt(ux, uy, realm) : kNone;

    const Box cross = panel::frameClose();
    overClose_ = inside && cross.has(ux, uy);
    if (pointer.pressed && inside) {
        if (overClose_) {
            closing_ = true;
        } else if (over_ != kNone) {
            pressing_ = over_;
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
            if (pressing_ != kNone && pressing_ == over_) {
                if (pressing_ == kPrev || pressing_ == kNext) {
                    const int step = pressing_ == kNext ? 1 : -1;
                    const int from = pending_ >= 0 ? pending_ : service_;
                    pending_ = (from + step + sim::kServices) % sim::kServices;
                    turnDir_ = step;
                    if (turn_ >= 0.0f) turn_ = -std::min(1.0f, turn_);
                    confirming_ = false;
                    out->turned = true;
                } else if (pressing_ >= kSocket0) {
                    socket_ = pressing_ - kSocket0;
                    out->click = true;
                } else if (pressing_ == kCancel) {
                    confirming_ = false;
                    out->click = true;
                } else if (confirming_) {
                    out->mix = true;
                    confirming_ = false;
                } else if (takeOut) {
                    out->takeAll = true;
                } else {
                    out->click = true;
                    if (ready && turn_ >= 1.0f) confirming_ = true;
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
        pressing_ = kNone;
        dragging_ = -1;
    }
    if (dragging_ >= 0 && box[dragging_].empty()) dragging_ = -1;

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
    now_.service = service_;
    now_.socket = socket_;
    now_.turn = int(turn_ * 64.0f);
    now_.dragging = dragging_;
    now_.incoming = incoming_.item;
    now_.incomingTakes = incomingTakes_;
    now_.dragX = pointer.x;
    now_.dragY = pointer.y;
    now_.hovered = dragging_ < 0 ? hovered_ : -1;
    now_.pointerX = pointer.x;
    now_.pointerY = pointer.y;
    now_.over = over_;
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
    const sim::Service service = this->service();
    const sim::Judged j = realm.judged(service, socket_);
    const float x = x_, y = y_, k = panel::scale();
    const float u = tip::unit();
    const gfx::Face& face = canvas_.face();
    const float text = kText * k * 1.1f;
    const auto P = [&](const Box& units) { return panel::scaled(x, y, units); };
    // A line of `size` sat in the band from `top`, `tall` high, in units.
    const auto base = [&](float top, float tall, float size) {
        return controls::middle(y + top * k, tall * k, size);
    };
    const auto left = [&](float top, uint32_t ink, const std::string& s, float size = 0.0f) {
        const float at = size > 0.0f ? size : text;
        controls::label(canvas_, x + kLeft * k, base(top, kLine, at), at, ink, s);
    };
    const auto right = [&](float top, uint32_t ink, const std::string& s, float size = 0.0f) {
        const float at = size > 0.0f ? size : text;
        controls::ranged(canvas_, x + kRight * k, base(top, kLine, at), at, ink, s);
    };
    const auto kicker = [&](float top, const std::string& s) {
        controls::kicker(canvas_, x + kLeft * k, y + (top + 7.0f) * k, s, u);
    };
    const bool takeOut = realm.mixed() || (answer_ >= 0 && !box.empty());
    const bool ready = j.ready && !realm.mixed();

    panel::frame(canvas_, arts, x, y, "Chaos Machine");

    // The service row: Options' row, its chevrons, and the service in gold between them.
    controls::row(canvas_, P(kRow), "Service", 0.0f, u);
    controls::chevron(canvas_, P(kPrevBox), false, over_ == kPrev ? 1.0f : 0.0f, u);
    controls::chevron(canvas_, P(kNextBox), true, over_ == kNext ? 1.0f : 0.0f, u);
    {
        const size_t turning = canvas_.mark();
        const std::string name = sim::serviceName(service);
        const float size = 8.6f * k * 1.1f;
        const float mid = (kPrevBox.right() + kNextBox.x) * 0.5f;
        controls::caps(canvas_, x + mid * k - controls::capsWidth(size, name, 0.0f) * 0.5f,
                       base(kRow.y, kRow.h, size), size, kGold, name, 0.0f);
        canvas_.fadeSince(turning, turnAlpha(), turnShift() * k);
    }
    // The box, the same for every service.
    kicker(kBoxKicker, "BOX");
    right(kBoxKicker - 2.0f, style::kAshInk2, "drag or right-click", 6.6f * k * 1.1f);
    panel::grid(canvas_, x, y, kOriginX, kOriginY, sim::kMachineColumns, sim::kMachineRows);
    if (hovered_ >= 0 && dragging_ < 0 && !box[hovered_].empty()) {
        panel::cell(canvas_, x, y, itemBox(tables, hovered_, box[hovered_]), sheet::Cell::Over);
    }
    if (dragging_ >= 0) {
        panel::cell(canvas_, x, y, itemBox(tables, dragging_, box[dragging_]), sheet::Cell::Held);
    }
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

    // The page, which turns with the service.
    const size_t page = canvas_.mark();
    float at = kPageTop;
    if (service == sim::Service::RemoveRune) {
        kicker(at, "SOCKETS");
        if (j.target >= 0) {
            const sim::Held& thing = box[j.target];
            for (int i = 0; i < thing.sockets && i < sim::kMostSockets; ++i) {
                const Box row = socketRow(i);
                const sim::PowerRow* power = sim::powerOf(thing.powers[i]);
                if (i == j.socket) {
                    canvas_.rect(P(row), kGoldFloor);
                    canvas_.outline(P(row), std::max(1.0f, k * 0.6f), kGoldSoft);
                } else if (over_ == kSocket0 + i && power) {
                    canvas_.outline(P(row), std::max(1.0f, k * 0.6f), style::kIron);
                }
                const float cx = x + (row.x + 9.0f) * k, cy = y + (row.y + row.h * 0.5f) * k;
                disc(canvas_, cx, cy, 5.2f * k, kSocketRim);
                disc(canvas_, cx, cy, 3.9f * k, kSocketHole);
                if (power) disc(canvas_, cx, cy, 2.7f * k, rarityInk(power->rarity));
                const float size = text;
                const float line = base(row.y, row.h, size);
                controls::label(canvas_, x + (row.x + 19.0f) * k, line, size,
                                power ? rarityInk(power->rarity) : kZen,
                                power ? power->name : "Empty Socket");
                if (power) {
                    controls::ranged(canvas_, x + (row.right() - 5.0f) * k, line, 6.6f * k * 1.1f,
                                     style::kAshInk, sim::rarityName(power->rarity));
                }
            }
            at += 10.0f + kSocketRow * float(std::max<int>(1, thing.sockets)) + 6.0f;
        } else {
            left(at + 10.0f, kMissing, j.title);
            at += 10.0f + kSocketRow + 6.0f;
        }
    } else {
        kicker(at, "RECIPE");
        const float size = kTitle * k * 1.1f;
        controls::label(canvas_, x + kLeft * k, base(at + 10.0f, 13.0f, size), size,
                        j.ready ? kReady : kMissing, j.title);
        at += 10.0f + 13.0f + 6.0f;
    }

    if (j.needCount > 0) {
        kicker(at, "NEEDS");
        at += 10.0f;
        for (int i = 0; i < j.needCount; ++i) {
            const sim::Need& n = j.needs[i];
            left(at, needInk(n), n.name);
            right(at, needInk(n), needCount(n));
            at += kLine;
        }
        at += 6.0f;
    }

    if (answer_ >= 0) {
        // The answer, where the chance was: gold for a success, red for a failure.
        const Box well{panel::kEdge, at, panel::kWidth - panel::kEdge * 2.0f, 30.0f};
        canvas_.rect(P(well), answer_ == 1 ? kGoldFloor : kLostFloor);
        canvas_.outline(P(well), std::max(1.0f, k * 0.6f), answer_ == 1 ? kGoldSoft : kLostRim);
        const float head = 8.6f * k * 1.1f;
        controls::label(canvas_, x + (well.x + 6.0f) * k, base(well.y + 2.0f, 13.0f, head), head,
                        answer_ == 1 ? kGold : kMissing,
                        answer_ == 1 ? madeHead(service) : "Failed");
        controls::label(canvas_, x + (well.x + 6.0f) * k, base(well.y + 15.0f, 12.0f, text * 0.92f),
                        text * 0.92f, style::kBone2, words_);
    } else {
        kicker(at, "CHANCE");
        right(at - 2.0f, j.ready ? kReady : style::kAshInk,
              j.rate > 0 ? std::to_string(j.rate) + "%" : "-");
        at += 11.0f;
        // The card's meter, and the share luck would add, faint, on a thing that is not lucky.
        const Box meter{kLeft, at, kRight - kLeft, 6.0f};
        controls::meter(canvas_, P(meter), float(j.rate) / 100.0f,
                        gfx::rgba(0.910f, 0.863f, 0.773f), gfx::rgba(0.66f, 0.60f, 0.50f), u);
        if (j.luck > 0 && !j.lucky && j.rate < 100) {
            const float from = meter.x + meter.w * float(j.rate) / 100.0f;
            const float wide = meter.w * float(std::min(j.luck, 100 - j.rate)) / 100.0f;
            canvas_.rect(P({from, meter.y + 1.0f, wide, meter.h - 2.0f}),
                         gfx::rgba(0.91f, 0.86f, 0.77f, 0.3f));
        }
        at += 12.0f;
        if (j.luck > 0) {
            left(at, style::kAshInk,
                 j.lucky ? "Lucky item: +" + std::to_string(j.luck) + "% counted"
                         : "A lucky item adds " + std::to_string(j.luck) + "%");
            at += kLine;
        }
        // An outcome on one row when its words fit beside the label, else under it, indented.
        const auto outcome = [&](const char* label, uint32_t ink, const std::string& s) {
            const float room = (kRight - kLeft) * k - controls::labelWidth(text, label) - 8.0f * k;
            left(at, style::kBone2, label);
            if (controls::labelWidth(text, s) <= room) {
                right(at, ink, s);
            } else {
                at += kLine - 1.5f;
                controls::label(canvas_, x + (kLeft + 8.0f) * k, base(at, kLine, text), text, ink,
                                s);
            }
            at += kLine;
        };
        if (!j.success.empty()) outcome("Success", kFigure, j.success);
        if (!j.failure.empty()) {
            outcome("Failure", kMissing, j.failure);
        } else if (!j.success.empty()) {
            outcome("Failure", style::kAshInk, "cannot fail");
        }
    }

    // The foot: the coin and the cost, red when he is short, and the button.
    controls::foot(canvas_, P({0.0f, 0.0f, panel::kWidth, panel::kHeight}),
                   y + panel::kFootRule * k, u);
    canvas_.image(arts.get("bag_zen"), P(kCoin));
    {
        const float size = 8.0f * k * 1.1f;
        controls::label(canvas_, x + (kCoin.right() + 4.0f) * k,
                        base(panel::kFootTop + 2.0f, kButtonH, size), size,
                        j.zen > realm.money() ? kMissing : kZen, panel::commas(j.zen));
    }
    const auto stateOf = [&](int which, bool off) {
        controls::State state;
        state.lift = over_ == which ? 1.0f : 0.0f;
        state.held = pressing_ == which && over_ == which;
        state.off = off;
        return state;
    };
    if (confirming_) {
        left(kAskTop, panel::kOrdinary, question(service, j));
        controls::button(canvas_, P(kConfirmCancel), "Cancel", controls::Kind::Secondary,
                         stateOf(kCancel, false), u);
        controls::button(canvas_, P(kConfirmRun), sim::serviceVerb(service),
                         controls::Kind::Primary, stateOf(kRun, false), u);
    } else if (takeOut) {
        controls::button(canvas_, P(kRunBox), "Take out", controls::Kind::Secondary,
                         stateOf(kRun, box.empty()), u);
    } else {
        controls::button(canvas_, P(kRunBox), sim::serviceVerb(service), controls::Kind::Primary,
                         stateOf(kRun, !ready), u);
    }
    if (turn_ < 1.0f) canvas_.fadeSince(page, turnAlpha(), turnShift() * k);

    panel::close(canvas_, x, y, now_.overClose, now_.closing);

    const gfx::Art picture = stage ? stage->picture() : gfx::Art{};
    if (picture.valid()) {
        canvas_.image(picture, P({0.0f, 0.0f, panel::kWidth, panel::kHeight}));
    } else {
        for (const Standing& one : standing_) {
            const Box to = P(one.box);
            canvas_.outline(to, 1.0f, gfx::rgba(0.68f, 0.60f, 0.40f, 0.9f));
            const std::string& name = tables.items[size_t(one.item)].label;
            canvas_.text(to.x + 2.0f, to.y + face.ascent(7.0f * k) + 2.0f, 7.0f * k,
                         panel::kLettering, name.substr(0, std::min<size_t>(name.size(), 6)));
        }
    }
    for (int cell = 0; cell < sim::kMachineCells; ++cell) {
        const sim::Held& held = box[cell];
        if (held.empty() || held.durability <= 1 || cell == dragging_) continue;
        if (!sim::stacks(tables.items[size_t(held.item)])) continue;
        const Box to = P(itemBox(tables, cell, held));
        canvas_.shadowed(to.x, to.bottom() - 2.0f * k, 8.0f * k, panel::kLettering,
                         gfx::rgba(0.0f, 0.0f, 0.0f, 0.8f), std::max(1.0f, 0.5f * k),
                         std::to_string(held.durability), gfx::Align::Right, to.w - 2.0f * k);
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
        for (int cell = 0; cell < sim::kMachineCells; ++cell) {
            const sim::Held& held = box[cell];
            if (held.empty()) continue;
            const content::ItemRow& row = tables.items[size_t(held.item)];
            for (int h = 0; h < row.height; ++h) {
                for (int w = 0; w < row.width; ++w) {
                    const float r = float(next(6) + 6) * 0.1f, g = float(next(4) + 4) * 0.1f;
                    const float cx = kOriginX + (float((cell % sim::kMachineColumns) + w) +
                                                 float(next(100)) * 0.01f) * kCell;
                    const float cy = kOriginY + (float((cell / sim::kMachineColumns) + h) +
                                                 float(next(100)) * 0.01f) * kCell;
                    const float s = 1.5f + float(next(10)) * 0.25f;
                    disc(canvas_, x + cx * k, y + cy * k, s * 2.0f * k,
                         gfx::rgba(std::min(r, 1.0f), g, 0.2f, 0.18f * fade));
                    disc(canvas_, x + cx * k, y + cy * k, s * 0.5f * k,
                         gfx::rgba(1.0f, std::min(1.0f, g + 0.3f), 0.5f, 0.9f * fade));
                }
            }
        }
    }

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
        const Box cell = P(itemBox(tables, hovered_, box[hovered_]));
        tip::draw(tip_, sheet, cell, screenW_, screenH_);
    }
}

}  // namespace mu::game
