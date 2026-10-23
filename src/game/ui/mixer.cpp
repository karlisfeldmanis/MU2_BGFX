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

constexpr float kWide = Mixer::kWide;

// The list, on the left: a group's kicker, then its rows.
constexpr float kListX = panel::kEdge, kListW = 172.0f;
constexpr float kListTop = 44.0f;
constexpr float kGroupHead = 13.0f, kRowH = 19.0f, kRowGap = 2.0f, kGroupGap = 6.0f;
constexpr const char* kGroups[3] = {"MIXES", "TICKETS", "RUNES"};
// The fading rule between the list and the page.
constexpr float kDivideX = 193.0f;

// The page, on the right: the service's name, the box and the needs beside it, the chance under.
constexpr float kLeft = 205.0f;
constexpr float kRight = kWide - panel::kEdge;
constexpr float kNameTop = 42.0f;
constexpr float kBoxKicker = 70.0f;
// The box's own pitch, larger than the bag's: the page has the room (canvas B's 56-pixel cells).
constexpr float kCell = 26.0f;
constexpr float kOriginX = kLeft, kOriginY = 82.0f;
constexpr float kBoxW = kCell * float(sim::kMachineColumns), kBoxH = kCell * float(sim::kMachineRows);
constexpr float kNeedsX = kOriginX + kBoxW + 18.0f;
constexpr float kLine = 11.5f;
constexpr float kText = 7.6f;  // a line, in units
// Remove Rune's sockets, under its two needs.
constexpr float kSocketTop = kOriginY + kLine * 2.0f + 8.0f;
constexpr float kSocketRow = 17.0f;
constexpr float kPageTop = kOriginY + kBoxH + 14.0f;
constexpr float kAskTop = panel::kFootRule - 18.0f;

// The foot: the coin and the cost at the left, the buttons at the right.
constexpr float kButtonTop = panel::kFootTop + 2.0f, kButtonH = 22.0f;
constexpr Box kRunBox{kRight - 72.0f, kButtonTop, 72.0f, kButtonH};
constexpr Box kFillBox{kRunBox.x - 5.0f - 84.0f, kButtonTop, 84.0f, kButtonH};
constexpr Box kTakeBox{kFillBox.x - 5.0f - 64.0f, kButtonTop, 64.0f, kButtonH};
constexpr Box kConfirmRun = kRunBox;
constexpr Box kConfirmCancel{kRunBox.x - 5.0f - 56.0f, kButtonTop, 56.0f, kButtonH};
constexpr Box kCoin{kLeft, panel::kFootTop + 7.0f, 12.0f, 12.0f};

// The item stage: the box at its origin, and the carry strip under it.
constexpr float kStageW = kBoxW;
constexpr float kCarryTop = kBoxH + 2.0f;
constexpr float kStageH = kBoxH + kCell * 4.0f + 4.0f;
// The one rune set sockets are drawn with, at the strip's far end, clear of a carried thing.
constexpr Box kRuneAt{kStageW - 24.0f, kCarryTop, 20.0f, 20.0f};

// The travel list's gold (travel.cpp), the one a picked row is lit in.
constexpr uint32_t kGold = gfx::rgba(0.722f, 0.600f, 0.353f);
constexpr uint32_t kGoldHi = gfx::rgba(0.886f, 0.776f, 0.541f);
constexpr uint32_t kGoldDk = gfx::rgba(0.431f, 0.353f, 0.196f);
constexpr uint32_t kGoldFloor = gfx::rgba(0.114f, 0.090f, 0.047f);
constexpr uint32_t kLostFloor = gfx::rgba(0.14f, 0.06f, 0.047f);
constexpr uint32_t kLostRim = gfx::rgba(0.42f, 0.13f, 0.10f);
constexpr uint32_t kSocketRim = gfx::rgba(0.62f, 0.45f, 0.24f);
constexpr uint32_t kSocketHole = gfx::rgba(0.05f, 0.035f, 0.03f);

uint32_t alpha(uint32_t c, float a) {
    return (c & 0x00FFFFFFu) | (uint32_t(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f) << 24);
}

// WoW's ladder, as the card names a rune (describe's rarityTone).
uint32_t rarityInk(sim::Rarity rarity) {
    switch (rarity) {
        case sim::Rarity::Rare: return gfx::rgba(0.184f, 0.549f, 1.0f);
        case sim::Rarity::Epic: return gfx::rgba(0.698f, 0.361f, 0.961f);
        case sim::Rarity::Legendary: return gfx::rgba(1.0f, 0x80 / 255.0f, 0.0f);  // a rune's orange
        case sim::Rarity::Common: return gfx::rgba(0.949f, 0.949f, 0.949f);
        case sim::Rarity::Ancient: return gfx::rgba(0.0f, 0xcc / 255.0f, 1.0f);  // #00ccff
    }
    return style::kBone;
}

// Which of the list's three groups a service is under.
int groupOf(int row) {
    switch (sim::kRowServices[row]) {
        case sim::Service::Cloak:
        case sim::Service::Invitation: return 1;
        case sim::Service::RemoveRune:
        case sim::Service::AddSocket:
        case sim::Service::FuseRunes: return 2;
        default: return 0;
    }
}

Box rowBox(int row) {
    const int g = groupOf(row);
    const float y = kListTop + float(g + 1) * kGroupHead + float(g) * kGroupGap +
                    float(row) * kRowH + float(row - g) * kRowGap;
    return {kListX, y, kListW, kRowH};
}

// In the window's units.
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

// panel::landing at the box's own pitch.
int landing(float ux, float uy, int width, int height) {
    const float cx = (ux - kOriginX) / kCell, cy = (uy - kOriginY) / kCell;
    if (cx < 0.0f || cy < 0.0f || cx >= float(sim::kMachineColumns) ||
        cy >= float(sim::kMachineRows)) {
        return -1;
    }
    const int across = std::max(1, width), down = std::max(1, height);
    const int column = std::clamp(int(std::floor(cx - float(across - 1) * 0.5f)), 0,
                                  std::max(0, sim::kMachineColumns - across));
    const int row = std::clamp(int(std::floor(cy - float(down - 1) * 0.5f)), 0,
                               std::max(0, sim::kMachineRows - down));
    return row * sim::kMachineColumns + column;
}

Box itemBox(const content::Tables& tables, int cell, const sim::Held& what) {
    if (what.empty()) return cellBox(cell, 1, 1);
    const content::ItemRow& row = tables.items[size_t(what.item)];
    return cellBox(cell, row.width, row.height);
}

// From the window's units to the stage's, whose origin is the box's.
Box onStage(const Box& units) { return {units.x - kOriginX, units.y - kOriginY, units.w, units.h}; }

Box socketRow(int i) {
    return {kNeedsX, kSocketTop + 10.0f + float(i) * (kSocketRow + 2.0f), kRight - kNeedsX, kSocketRow};
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

// A colour with no alpha, mixed and scaled before it is laid on as light.
struct Tint {
    float r, g, b;
    Tint mix(const Tint& o, float t) const {
        return {r + (o.r - r) * t, g + (o.g - g) * t, b + (o.b - b) * t};
    }
};
constexpr Tint kFlashGold{1.0f, 0.78f, 0.36f};

// Light of `tint` at `strength`, premultiplied with no alpha: added to what is under it.
uint32_t added(const Tint& tint, float strength) {
    const float s = std::clamp(strength, 0.0f, 1.0f);
    return gfx::rgba(tint.r * s, tint.g * s, tint.b * s, 0.0f);
}

// A number in [0, 1) from a key and a channel: the same every frame for the same particle.
float hash01(uint32_t key, uint32_t channel) {
    uint32_t h = key * 0x9E3779B1u ^ (channel + 0x7F4A7C15u) * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return float(h & 0xFFFFFFu) / float(0x1000000);
}

// A soft round glow: `strength` at its middle, nothing at its rim.
void glowDisc(gfx::Canvas& canvas, float cx, float cy, float r, const Tint& tint, float strength) {
    constexpr int kSides = 16;
    float xy[(kSides + 2) * 2];
    uint32_t ink[kSides + 2];
    xy[0] = cx;
    xy[1] = cy;
    ink[0] = added(tint, strength);
    for (int i = 0; i <= kSides; ++i) {
        const float a = float(i) * 6.2831853f / float(kSides);
        xy[(i + 1) * 2] = cx + std::cos(a) * r;
        xy[(i + 1) * 2 + 1] = cy + std::sin(a) * r;
        ink[i + 1] = added(tint, 0.0f);
    }
    canvas.light(xy, ink, kSides + 2);
}

// A glow over a closed rim of `count` points (the last the first again), `middle` at `centre`.
void glowFan(gfx::Canvas& canvas, const float centre[2], const float* rim, int count,
             const Tint& tint, float middle, float edge) {
    float xy[2 * 8];
    uint32_t ink[8];
    xy[0] = centre[0];
    xy[1] = centre[1];
    ink[0] = added(tint, middle);
    for (int i = 0; i < count && i < 7; ++i) {
        xy[(i + 1) * 2] = rim[i * 2];
        xy[(i + 1) * 2 + 1] = rim[i * 2 + 1];
        ink[i + 1] = added(tint, edge);
    }
    canvas.light(xy, ink, std::min(count, 7) + 1);
}

// A four-pointed glint: two thin lit diamonds crossed, `reach` from the middle to a point.
void star(gfx::Canvas& canvas, float cx, float cy, float reach, float waist, const Tint& tint,
          float strength) {
    const float across[12] = {cx, cy, cx - reach, cy, cx, cy - waist, cx + reach, cy,
                              cx, cy + waist, cx - reach, cy};
    const float down[12] = {cx, cy, cx, cy - reach, cx + waist, cy, cx, cy + reach,
                            cx - waist, cy, cx, cy - reach};
    const uint32_t ink[6] = {added(tint, strength), added(tint, 0.0f), added(tint, strength * 0.5f),
                             added(tint, 0.0f), added(tint, strength * 0.5f), added(tint, 0.0f)};
    canvas.light(across, ink, 6);
    canvas.light(down, ink, 6);
    glowDisc(canvas, cx, cy, waist * 3.0f, tint, strength * 0.6f);
}

std::string needCount(const sim::Need& n) {
    if (n.need == 0) return n.have > 0 ? std::to_string(n.have) : "any";
    return std::to_string(n.have) + " / " + std::to_string(n.need) + (n.atLeast ? "+" : "");
}

// The confirm's question, MU's for a Combine.
std::string question(sim::Service service, const sim::Judged& j) {
    switch (service) {
        case sim::Service::Combine: return "Do you want to combine your items?";
        case sim::Service::RemoveRune: return j.title + "?";
        case sim::Service::AddSocket: return "Add the socket?";
        case sim::Service::FuseRunes: return "Fuse the three runes?";
        case sim::Service::FirstWings:
        case sim::Service::SecondWings: return "Do you want to create the wings?";
        case sim::Service::ChaosWeapon: return "Do you want to create a Chaos weapon?";
        case sim::Service::Upgrade: return "Do you want to upgrade the item?";
        case sim::Service::Dinorant: return "Do you want to create the Dinorant?";
        case sim::Service::Cloak: return "Do you want to create the cloak?";
        case sim::Service::Invitation: return "Do you want to create the invitation?";
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
        case sim::Service::FirstWings:
        case sim::Service::SecondWings: return "Wings created";
        case sim::Service::ChaosWeapon: return "Chaos weapon created";
        case sim::Service::Upgrade: return "Upgraded";
        case sim::Service::Dinorant: return "Dinorant created";
        case sim::Service::Cloak: return "Cloak created";
        case sim::Service::Invitation: return "Invitation created";
    }
    return "";
}

// A row's word at its right: what the bag makes of the service.
std::string standing(const sim::Planned& p) {
    if (p.judged.needsLevel > 0) return "Level " + std::to_string(p.judged.needsLevel);
    if (p.ready()) return "Ready";
    if (p.missing > 0) return std::to_string(p.missing) + " missing";
    return "";
}

}  // namespace

bool Mixer::Contents::operator==(const Contents& o) const {
    return version == o.version && bagVersion == o.bagVersion && money == o.money &&
           answer == o.answer && service == o.service && socket == o.socket &&
           dragging == o.dragging && part == o.part && incoming == o.incoming && incomingTakes == o.incomingTakes &&
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
    return up_ && Box{x_, y_, kWide * panel::scale(), panel::kHeight * panel::scale()}.has(x, y);
}

int Mixer::cellUnder(float x, float y, int width, int height) const {
    if (!covers(x, y)) return -1;
    const float k = panel::scale();
    return landing((x - x_) / k, (y - y_) / k, width, height);
}

void Mixer::plans(const sim::Realm& realm) {
    const uint32_t box = realm.machine().version(), bag = realm.satchel().version();
    if (box == plannedBox_ && bag == plannedBag_ && service_ == plannedService_ &&
        socket_ == plannedSocket_) {
        return;
    }
    if (box != plannedBox_ || bag != plannedBag_) {
        for (int i = 0; i < sim::kRowCount; ++i) {
            rowPlans_[i] = realm.planned(sim::kRowServices[i], -1, true);
        }
    }
    readyPlan_ = realm.planned(service(), socket_, true);
    fillPlan_ = realm.planned(service(), socket_, false);
    plannedBox_ = box;
    plannedBag_ = bag;
    plannedService_ = service_;
    plannedSocket_ = socket_;
}

int Mixer::hitAt(float ux, float uy, const sim::Realm& realm) const {
    const bool takeOut = realm.mixed() || (answer_ >= 0 && !realm.machine().empty());
    if (confirming_) {
        if (kConfirmRun.has(ux, uy)) return kRun;
        if (kConfirmCancel.has(ux, uy)) return kCancel;
    } else {
        if (kRunBox.has(ux, uy)) return kRun;
        if (!takeOut && kFillBox.has(ux, uy)) return kFill;
        if (!takeOut && kTakeBox.has(ux, uy)) return kTake;
    }
    for (int i = 0; i < sim::kRowCount; ++i) {
        if (rowBox(i).has(ux, uy)) return kRow0 + i;
    }
    if (service() == sim::Service::RemoveRune && (answer_ < 0 || service_ != ranService_)) {
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
        dragging_ = lifting_ = -1;
        pressing_ = kNone;
        confirming_ = false;
        sparks_ = 0.0f;
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

    const sim::Judged judged = realm.judged(service(), socket_);
    if (service() == sim::Service::RemoveRune) socket_ = judged.socket;
    plans(realm);
    // Things come out and nothing goes in while the answer stands, as MU locks the box at
    // MIX_FINISHED.
    const bool takeOut = realm.mixed() || (answer >= 0 && !box.empty());
    const bool ready = judged.ready && !realm.mixed();
    if (!ready) confirming_ = false;

    const int cell = inside ? cellAt(ux, uy) : -1;
    hovered_ = cell >= 0 && incoming_.empty() ? box.holder(tables, cell) : -1;
    over_ = inside ? hitAt(ux, uy, realm) : kNone;

    // The close button in the head's right-hand socket, the bag's own, at this window's end.
    Box cross = panel::frameClose();
    cross.x += kWide - panel::kWidth;
    overClose_ = inside && cross.has(ux, uy);
    // Click to pick and click to place, the bag's (bag.h): while a thing rides the pointer a
    // press is the start of placing it and nothing else.
    if (pointer.pressed && inside && dragging_ < 0) {
        if (overClose_) {
            closing_ = true;
        } else if (over_ != kNone) {
            pressing_ = over_;
        } else if (hovered_ >= 0 && sparks_ <= 0.0f) {
            lifting_ = hovered_;
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
                if (pressing_ >= kRow0) {
                    const int row = pressing_ - kRow0;
                    if (row != service_) {
                        service_ = row;
                        socket_ = -1;
                        confirming_ = false;
                        out->turned = true;
                    }
                } else if (pressing_ >= kSocket0) {
                    socket_ = pressing_ - kSocket0;
                    out->click = true;
                } else if (pressing_ == kCancel) {
                    confirming_ = false;
                    out->click = true;
                } else if (pressing_ == kTake) {
                    if (!box.empty()) out->takeAll = true;
                } else if (pressing_ == kFill) {
                    if (fillPlan_.count > 0 && !realm.mixed()) {
                        out->fill = true;
                        confirming_ = false;
                    }
                } else if (confirming_) {
                    out->mix = true;
                    ranService_ = service_;
                    ranZen_ = judged.zen;
                    confirming_ = false;
                } else if (takeOut) {
                    out->takeAll = true;
                } else {
                    out->click = true;
                    if (ready) confirming_ = true;
                }
            }
            // Placed by this release, or left on the pointer where it cannot go -- the frame, a
            // button, a red cell -- and back where it was over its own place.
            bool placed = true;
            if (dragging_ >= 0) {
                out->pieces = part_;
                if (!inside) {
                    out->outside = dragging_;
                    out->outsideX = pointer.x;
                    out->outsideY = pointer.y;
                } else if (cell < 0 || box[dragging_].empty()) {
                    placed = box[dragging_].empty();
                } else {
                    // A stack goes on the stack under the pointer; anything else stands with the
                    // pointer at its middle (panel::landing).
                    const content::ItemRow& row = tables.items[size_t(box[dragging_].item)];
                    const int under = box.holder(tables, cell);
                    const bool onto = under >= 0 && under != dragging_ &&
                                      sim::tops(tables, box[under], box[dragging_]);
                    const int to = onto ? cell : landing(ux, uy, row.width, row.height);
                    if (to == dragging_) {
                        // Back where it was lifted from.
                    } else if (onto || (to >= 0 && box.room(tables, to, row.width, row.height, dragging_))) {
                        out->moveFrom = dragging_;
                        out->moveTo = to;
                    } else {
                        placed = false;
                    }
                }
                if (placed) dragging_ = -1;
            } else if (lifting_ >= 0 && lifting_ == hovered_) {
                dragging_ = lifting_;
                part_ = 0;
            }
        }
        closing_ = false;
        pressing_ = kNone;
        lifting_ = -1;
    }
    if (dragging_ >= 0 && box[dragging_].empty()) dragging_ = -1;
    if (dragging_ < 0 || part_ >= box[dragging_].durability) part_ = 0;

    if (sparks_ > 0.0f) {
        sparks_ = std::max(0.0f, sparks_ - seconds);
        ++sparkFrame_;
    }

    // The pictures stand on a stage the box's size, with the carry strip under it.
    standing_.clear();
    for (int at = 0; at < sim::kMachineCells; ++at) {
        const sim::Held& what = box[at];
        if (what.empty()) continue;
        const Box place = onStage(itemBox(tables, at, what));
        const Box stood = at == dragging_ ? Box{4.0f, kCarryTop, place.w, place.h} : place;
        standing_.push_back({what.item, stood.grown(-2.0f), what.refinement,
                             at == hovered_ && dragging_ < 0, what.excellent != 0});
        // A part riding: the rest of the stack stays standing in its cell.
        if (at == dragging_ && part_ > 0) standing_.push_back({what.item, place.grown(-2.0f), what.refinement, false, what.excellent != 0});
    }
    // The one rune the set sockets are drawn with -- the bag's and the vault's.
    bool anySet = false;
    for (int at = 0; at < sim::kMachineCells && !anySet; ++at) {
        for (int socket = 0; socket < std::min(socketsOf(box[at]), sim::kMostSockets); ++socket) {
            anySet |= !box[at].empty() && powerAt(box[at], socket) != 0;
        }
    }
    if (const int32_t rune = anySet ? runeRow(tables) : -1; rune >= 0) {
        standing_.push_back({rune, kRuneAt, 0, false, false});
    }
    if (stage) stage->stand(standing_, kStageW, kStageH);

    now_ = Contents{};
    now_.version = box.version();
    now_.bagVersion = realm.satchel().version();
    now_.money = realm.money();
    now_.answer = answer;
    now_.service = service_;
    now_.socket = socket_;
    now_.dragging = dragging_;
    now_.part = part_;
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
    const float line = std::max(1.0f, std::round(u));
    const gfx::Face& face = canvas_.face();
    const float text = kText * k * 1.1f;
    const auto P = [&](const Box& units) { return panel::scaled(x, y, units); };
    // A line of `size` sat in the band from `top`, `tall` high, in units.
    const auto base = [&](float top, float tall, float size) {
        return controls::middle(y + top * k, tall * k, size);
    };
    const auto at = [&](float left, float top, uint32_t ink, const std::string& s, float size = 0.0f) {
        const float sz = size > 0.0f ? size : text;
        controls::label(canvas_, x + left * k, base(top, kLine, sz), sz, ink, s);
    };
    const auto right = [&](float edge, float top, uint32_t ink, const std::string& s, float size = 0.0f) {
        const float sz = size > 0.0f ? size : text;
        controls::ranged(canvas_, x + edge * k, base(top, kLine, sz), sz, ink, s);
    };
    const auto kicker = [&](float left, float top, const std::string& s) {
        controls::kicker(canvas_, x + left * k, y + (top + 7.0f) * k, s, u);
    };
    const bool takeOut = realm.mixed() || (answer_ >= 0 && !box.empty());
    const bool ready = j.ready && !realm.mixed();
    // The answer stands on the page of the service that gave it, not on every page picked after.
    const int answer = service_ == ranService_ ? answer_ : -1;

    controls::frame(canvas_, P({0.0f, 0.0f, kWide, panel::kHeight}), u, "Chaos Machine", nullptr,
                    panel::kHeadBand * k);

    // ---- the list ----------------------------------------------------------------------------
    for (int g = 0; g < 3; ++g) {
        int first = -1;
        for (int i = 0; i < sim::kRowCount && first < 0; ++i) {
            if (groupOf(i) == g) first = i;
        }
        if (first >= 0) kicker(kListX + 2.0f, rowBox(first).y - kGroupHead + 1.0f, kGroups[g]);
    }
    const float r = style::kRadiusSmall * u;
    for (int i = 0; i < sim::kRowCount; ++i) {
        const bool here = i == service_;
        const bool over = !here && over_ == kRow0 + i;
        Box b = P(rowBox(i));
        b = {std::round(b.x), std::round(b.y), std::round(b.w), std::round(b.h)};
        if (over) b.y -= line;
        tip::shadowUnder(canvas_, b, u * (over ? 0.7f : 0.45f), r);
        const uint32_t rim = here ? kGoldDk : over ? style::kIronHi : style::kIronLo;
        tip::panel(canvas_, b, r, rim, rim);
        const Box in = b.grown(-line);
        if (over) tip::panel(canvas_, in, r - line, style::kAsh4, style::kAsh2);
        else tip::panel(canvas_, in, r - line, style::kAsh3, style::kAsh1);
        if (here) {
            // The gold: a wash from the left, a stripe down the edge.
            canvas_.shade({in.x, in.y, in.w * 0.75f, in.h}, alpha(kGold, 0.18f), alpha(kGold, 0.0f),
                          alpha(kGold, 0.0f), alpha(kGold, 0.18f));
            canvas_.rect({b.x, b.y + 5.0f * u, std::round(2.0f * u), b.h - 10.0f * u}, kGold);
        }
        const float nameSize = 8.4f * k * 1.1f;
        const float mid = controls::middle(b.y, b.h, nameSize);
        controls::label(canvas_, b.x + 8.0f * k, mid, nameSize, here ? kGoldHi : style::kBoneHi,
                        sim::serviceName(sim::kRowServices[i]));
        const sim::Planned& p = rowPlans_[i];
        const std::string word = standing(p);
        if (!word.empty()) {
            const float size = text * 0.92f;
            controls::ranged(canvas_, b.right() - 7.0f * k, controls::middle(b.y, b.h, size), size,
                             p.judged.needsLevel > 0 ? style::kDanger
                             : p.ready()             ? style::kFits
                                                     : style::kAshInk,
                             word);
        }
    }
    {
        // The fading rule between the list and the page, down the window.
        const float top = y + (kListTop - 2.0f) * k, tall = (panel::kHeight - kListTop - 10.0f) * k;
        const float half = tall * 0.5f, lx = std::round(x + kDivideX * k);
        const uint32_t mid = style::kIron, clear = style::kIronLo & 0x00FFFFFFu;
        canvas_.shade({lx, top, line, half}, clear, clear, mid, mid);
        canvas_.shade({lx, top + half, line, half}, mid, mid, clear, clear);
    }

    // ---- the page: its name, the box, the needs ---------------------------------------------
    {
        const std::string name = sim::serviceName(service);
        const float size = 10.0f * k;
        if (const gfx::Face* tf = panel::titleFace()) {
            const float baseline = std::round(y + (kNameTop + 11.0f) * k);
            canvas_.lettered(*tf, panel::titleTexture(), x + kLeft * k + 1.0f, baseline + 1.0f, size,
                             0.0f, style::kDrop, name);
            canvas_.lettered(*tf, panel::titleTexture(), x + kLeft * k, baseline, size, 0.0f,
                             kGoldHi, name);
        } else {
            at(kLeft, kNameTop + 2.0f, kGoldHi, name, size);
        }
        at(kLeft, kNameTop + 14.0f,
           j.needsLevel > 0 ? style::kDanger : ready ? style::kFits : style::kAshInk, j.title);
    }
    kicker(kLeft, kBoxKicker, "BOX");
    right(kOriginX + kBoxW, kBoxKicker - 2.0f, style::kAshInk2, "drag or right-click", 6.6f * k * 1.1f);
    controls::grid(canvas_, P({kOriginX, kOriginY, kBoxW, kBoxH}), sim::kMachineColumns,
                   sim::kMachineRows, u);
    // Each piece's cells washed in the satchel's teal, as the bag and the vault have them, so a
    // thing sits in its cells and the empty ones read as empty.
    for (int at = 0; at < sim::kMachineCells; ++at) {
        if (box[at].empty() || at == dragging_) continue;
        canvas_.rect(P(itemBox(tables, at, box[at])), panel::kTakenWash);
    }
    if (hovered_ >= 0 && dragging_ < 0 && !box[hovered_].empty()) {
        panel::cell(canvas_, x, y, itemBox(tables, hovered_, box[hovered_]), sheet::Cell::Over);
    }
    if (dragging_ >= 0 && covers(now_.dragX, now_.dragY)) {
        const float ux = (now_.dragX - x) / k, uy = (now_.dragY - y) / k;
        const int pointed = cellAt(ux, uy);
        const sim::Held& moving = box[dragging_];
        const content::ItemRow* shape = moving.empty() ? nullptr : &tables.items[size_t(moving.item)];
        const int under = pointed >= 0 ? box.holder(tables, pointed) : -1;
        const bool onto = shape && under >= 0 && under != dragging_ && sim::tops(tables, box[under], moving);
        // The cell the release asks for: the stack under the pointer, or the footprint with the
        // pointer at its middle.
        const int cell = !shape || pointed < 0 ? -1
                         : onto ? pointed
                                : landing(ux, uy, shape->width, shape->height);
        if (cell >= 0 && cell != dragging_) {
            const content::ItemRow& row = *shape;
            const bool fits = onto || box.room(tables, cell, row.width, row.height, dragging_);
            const int column = cell % sim::kMachineColumns, rowAt = cell / sim::kMachineColumns;
            const int w = std::min<int>(row.width, sim::kMachineColumns - column);
            const int h = std::min<int>(row.height, sim::kMachineRows - rowAt);
            panel::cell(canvas_, x, y, cellBox(cell, w, h),
                        fits ? sheet::Cell::Fits : sheet::Cell::Blocked);
        }
    }
    if (dragging_ < 0 && !incoming_.empty() && covers(now_.dragX, now_.dragY)) {
        const float ux = (now_.dragX - x) / k, uy = (now_.dragY - y) / k;
        const int pointed = cellAt(ux, uy);
        if (pointed >= 0) {
            const content::ItemRow& row = tables.items[size_t(incoming_.item)];
            const int cell = landing(ux, uy, row.width, row.height);
            const int under = box.holder(tables, pointed);
            const bool topping =
                incomingTakes_ && under >= 0 && sim::tops(tables, box[under], incoming_);
            const int place = topping ? under : cell;
            const content::ItemRow& shape = topping ? tables.items[size_t(box[under].item)] : row;
            const bool fits =
                topping || (incomingTakes_ && box.room(tables, cell, row.width, row.height));
            const int column = place % sim::kMachineColumns, rowAt = place / sim::kMachineColumns;
            const int w = std::min<int>(shape.width, sim::kMachineColumns - column);
            const int h = std::min<int>(shape.height, sim::kMachineRows - rowAt);
            panel::cell(canvas_, x, y, cellBox(place, w, h),
                        fits ? sheet::Cell::Fits : sheet::Cell::Blocked);
        }
    }

    // The needs, beside the box: in the box over wanted, and what more the bag holds of each --
    // green met, bright where the bag has the rest, red where it has not.
    if (j.needCount > 0) {
        kicker(kNeedsX, kBoxKicker, "NEEDS");
        float top = kOriginY;
        for (int i = 0; i < j.needCount; ++i) {
            const sim::Need& n = j.needs[i];
            const sim::Need* then =
                i < readyPlan_.judged.needCount && readyPlan_.judged.needs[i].name == n.name
                    ? &readyPlan_.judged.needs[i] : nullptr;
            const int more = then ? std::max(0, then->have - n.have) : 0;
            uint32_t ink = style::kBone;
            std::string bag;
            if (n.remove) {
                ink = style::kDanger;
            } else if (n.need == 0) {
                ink = n.have > 0 ? style::kBone : style::kAshInk;
            } else if (n.have == n.need || (n.atLeast && n.have > n.need)) {
                ink = style::kFits;
            } else if (n.have > n.need) {
                ink = style::kDanger;
            } else if (i == 0 && fillPlan_.choosing >= 2) {
                ink = style::kBoneHi;
                bag = std::to_string(fillPlan_.choosing) + " in bag, pick one";
            } else if (n.have + more >= n.need) {
                ink = style::kBoneHi;
                bag = std::to_string(more) + " in bag";
            } else {
                ink = style::kDanger;
                bag = more > 0 ? std::to_string(more) + " in bag" : "none in bag";
            }
            at(kNeedsX, top, ink, n.name);
            right(kRight, top, ink, needCount(n));
            if (!bag.empty()) right(kRight - 30.0f, top, style::kAshInk2, bag, text * 0.9f);
            top += kLine;
        }
    }
    if (service == sim::Service::RemoveRune && answer < 0 && j.target >= 0) {
        kicker(kNeedsX, kSocketTop, "SOCKET");
        const sim::Held& thing = box[j.target];
        for (int i = 0; i < thing.sockets && i < sim::kMostSockets; ++i) {
            const Box row = socketRow(i);
            const sim::PowerRow* power = sim::powerOf(thing.powers[i]);
            const bool here = i == j.socket;
            const bool over = !here && power && over_ == kSocket0 + i;
            const Box b = P(row);
            const uint32_t rim = here ? kGoldDk : over ? style::kIronHi : style::kIronLo;
            tip::panel(canvas_, b, r, rim, rim);
            tip::panel(canvas_, b.grown(-line), r - line, style::kAsh2, style::kAsh1);
            if (here) {
                const Box in = b.grown(-line);
                canvas_.shade({in.x, in.y, in.w * 0.75f, in.h}, alpha(kGold, 0.18f),
                              alpha(kGold, 0.0f), alpha(kGold, 0.0f), alpha(kGold, 0.18f));
            }
            const float cx = x + (row.x + 9.0f) * k, cy = y + (row.y + row.h * 0.5f) * k;
            disc(canvas_, cx, cy, 5.2f * k, kSocketRim);
            disc(canvas_, cx, cy, 3.9f * k, kSocketHole);
            if (power) disc(canvas_, cx, cy, 2.7f * k, rarityInk(power->rarity));
            const float mid = base(row.y + (row.h - kLine) * 0.5f, kLine, text);
            controls::label(canvas_, x + (row.x + 19.0f) * k, mid, text,
                            power ? rarityInk(power->rarity) : style::kAshInk2,
                            power ? power->name : "Empty socket");
            if (power) {
                controls::ranged(canvas_, x + (row.right() - 5.0f) * k, mid, text * 0.9f,
                                 panel::kRefined, panel::commas(sim::kRemoveRuneZen[int(power->rarity)]));
            }
        }
    }

    // ---- the chance, or the answer where it was ---------------------------------------------
    float top = kPageTop;
    if (answer >= 0) {
        kicker(kLeft, top, "RESULT");
        top += 12.0f;
        const Box well{kLeft, top, kRight - kLeft, 32.0f};
        canvas_.rect(P(well), answer == 1 ? kGoldFloor : kLostFloor);
        canvas_.outline(P(well), line, answer == 1 ? kGoldDk : kLostRim);
        const float head = 10.0f * k;
        const char* said = answer == 1 ? madeHead(service) : "Failed";
        const uint32_t ink = answer == 1 ? kGoldHi : style::kDanger;
        if (const gfx::Face* tf = panel::titleFace()) {
            const float baseline = std::round(y + (well.y + 14.0f) * k);
            canvas_.lettered(*tf, panel::titleTexture(), x + (well.x + 7.0f) * k + 1.0f,
                             baseline + 1.0f, head, 0.0f, style::kDrop, said);
            canvas_.lettered(*tf, panel::titleTexture(), x + (well.x + 7.0f) * k, baseline, head,
                             0.0f, ink, said);
        } else {
            at(well.x + 7.0f, well.y + 2.0f, ink, said, head);
        }
        at(well.x + 7.0f, well.y + 18.0f, style::kBone, words_);
    } else {
        kicker(kLeft, top, "CHANCE");
        right(kRight, top - 2.0f, j.ready ? style::kBoneHi : style::kAshInk,
              j.rate > 0 ? std::to_string(j.rate) + "%" : "-");
        top += 12.0f;
        // The card's meter, and the share luck would add, faint, on a thing that is not lucky.
        const Box meter{kLeft, top, kRight - kLeft, 5.0f};
        controls::meter(canvas_, P(meter), float(j.rate) / 100.0f,
                        gfx::rgba(0.851f, 0.812f, 0.741f), gfx::rgba(0.561f, 0.522f, 0.459f), u);
        if (j.luck > 0 && !j.lucky && j.rate < 100) {
            const float from = meter.x + meter.w * float(j.rate) / 100.0f;
            const float wide = meter.w * float(std::min(j.luck, 100 - j.rate)) / 100.0f;
            canvas_.rect(P({from, meter.y, wide, meter.h}), gfx::rgba(0.851f, 0.812f, 0.741f, 0.22f));
        }
        top += 11.0f;
        if (j.luck > 0) {
            at(kLeft, top, style::kAshInk,
               j.lucky ? "Lucky item: +" + std::to_string(j.luck) + "% counted"
                       : "A lucky item adds " + std::to_string(j.luck) + "%");
            top += kLine;
        }
        const auto outcome = [&](const char* label, uint32_t ink, const std::string& s) {
            at(kLeft, top, style::kAshInk, label);
            at(kLeft + 46.0f, top, ink, s);
            top += kLine;
        };
        if (!j.success.empty()) outcome("Success", style::kBone, j.success);
        if (!j.failure.empty()) {
            outcome("Failure", style::kDanger, j.failure);
        } else if (!j.success.empty()) {
            outcome("Failure", style::kAshInk, "Cannot fail");
        }
    }

    // ---- the foot ----------------------------------------------------------------------------
    {
        const Box band = P({kDivideX, panel::kFootRule, kWide - kDivideX, panel::kHeight - panel::kFootRule});
        const float corners[4] = {0.0f, 0.0f, style::kRadiusSheet * u, 0.0f};
        tip::rounded(canvas_, {std::round(band.x), std::round(band.y), std::round(band.w), std::round(band.h)},
                     corners, gfx::rgba(0, 0, 0, 0.0f), gfx::rgba(0, 0, 0, 0.38f));
        controls::rule(canvas_, band.x + 8.0f * k, band.y, band.w - 16.0f * k, u);
    }
    canvas_.image(arts.get("bag_zen"), P(kCoin));
    {
        // The cost, red when he is short -- or, while an answer stands, what it cost him: the box
        // as it stands after a mix is priced as the next one (a +10 as a +11).
        const float size = 8.4f * k * 1.1f;
        const long long zen = answer >= 0 ? ranZen_ : j.zen;
        const std::string cost = panel::commas(zen);
        const float from = x + (kCoin.right() + 4.0f) * k;
        const float baseline = base(panel::kFootTop + 2.0f, kButtonH, size);
        controls::label(canvas_, from, baseline, size,
                        answer < 0 && zen > realm.money() ? style::kDanger : panel::kRefined, cost);
        controls::label(canvas_, from + controls::labelWidth(size, cost) + 5.0f * k, baseline,
                        text * 0.92f, style::kAshInk2,
                        answer >= 0 ? "paid" : "of " + panel::commas(realm.money()));
    }
    const auto stateOf = [&](int which, bool off) {
        controls::State state;
        state.lift = over_ == which && !off ? 1.0f : 0.0f;
        state.held = pressing_ == which && over_ == which && !off;
        state.off = off;
        return state;
    };
    if (confirming_) {
        at(kLeft, kAskTop, style::kBoneHi, question(service, j));
        controls::button(canvas_, P(kConfirmCancel), "Cancel", controls::Kind::Secondary,
                         stateOf(kCancel, false), u);
        controls::button(canvas_, P(kConfirmRun), sim::serviceVerb(service),
                         controls::Kind::Primary, stateOf(kRun, false), u);
    } else if (takeOut) {
        controls::button(canvas_, P(kRunBox), "Take out", controls::Kind::Primary,
                         stateOf(kRun, box.empty()), u);
    } else {
        controls::button(canvas_, P(kTakeBox), "Take out", controls::Kind::Secondary,
                         stateOf(kTake, box.empty()), u);
        controls::button(canvas_, P(kFillBox), "Fill from bag", controls::Kind::Secondary,
                         stateOf(kFill, fillPlan_.count == 0 || realm.mixed()), u);
        controls::button(canvas_, P(kRunBox), sim::serviceVerb(service), controls::Kind::Primary,
                         stateOf(kRun, !ready), u);
    }

    panel::close(canvas_, x + (kWide - panel::kWidth) * k, y, now_.overClose, now_.closing);

    // ---- the things in the box ---------------------------------------------------------------
    const gfx::Art picture = stage ? stage->picture() : gfx::Art{};
    const float sx = picture.valid() ? picture.width / kStageW : 0.0f;
    const float sy = picture.valid() ? picture.height / kStageH : 0.0f;
    if (picture.valid()) {
        canvas_.region(picture, P({kOriginX, kOriginY, kBoxW, kBoxH}), {0.0f, 0.0f, kBoxW * sx, kBoxH * sy});
    } else {
        for (const Standing& one : standing_) {
            if (one.box.y >= kBoxH) continue;  // the carry strip
            const Box to = P({one.box.x + kOriginX, one.box.y + kOriginY, one.box.w, one.box.h});
            canvas_.outline(to, 1.0f, gfx::rgba(0.68f, 0.60f, 0.40f, 0.9f));
            const std::string& name = tables.items[size_t(one.item)].label;
            canvas_.text(to.x + 2.0f, to.y + face.ascent(7.0f * k) + 2.0f, 7.0f * k,
                         panel::kLettering, name.substr(0, std::min<size_t>(name.size(), 6)));
        }
    }
    // The sockets over each piece's middle, empty or set -- the bag's. Not on the one riding
    // the pointer.
    for (int cell = 0; cell < sim::kMachineCells; ++cell) {
        const sim::Held& held = box[cell];
        if (held.empty() || socketsOf(held) == 0 || cell == dragging_) continue;
        panel::sockets(canvas_, picture, P(itemBox(tables, cell, held)), held, k, kStageW, kStageH,
                       kRuneAt);
    }
    for (int cell = 0; cell < sim::kMachineCells; ++cell) {
        const sim::Held& held = box[cell];
        if (held.empty() || held.durability <= 1 || (cell == dragging_ && part_ == 0)) continue;
        if (sim::poursMost(tables.items[size_t(held.item)]) <= 0) continue;
        const int count = cell == dragging_ ? held.durability - part_ : held.durability;
        if (count <= 1) continue;
        const Box to = P(itemBox(tables, cell, held));
        canvas_.shadowed(to.x, to.bottom() - 2.0f * k, 8.0f * k, panel::kLettering,
                         gfx::rgba(0.0f, 0.0f, 0.0f, 0.8f), std::max(1.0f, 0.5f * k),
                         std::to_string(count), gfx::Align::Right, to.w - 2.0f * k);
    }

    // **The answer's sparks** (the user, 2026-10-20: "how good looking is our sparkle effect?",
    // then "improve it"). MuMain's RenderMixEffect threw one speck a covered cell a frame at a
    // random point, which on a flat window read as noise; here the same two seconds are a story.
    // A success flashes gold over each thing, then embers rise off it, shrink and go out, with a
    // few star glints early; a failure is a dark red pulse over the box and ash falling through
    // it. Each particle is a pure function of its index and the time since the answer, so the
    // picture moves smoothly and costs no state. Light is added (Canvas::light), not painted.
    if (sparks_ > 0.0f) {
        const float t = kSparkSeconds - sparks_;
        const bool won = answer_ != 0;
        const Box boxPx = P({kOriginX, kOriginY, kBoxW, kBoxH});
        const float cell = kCell * k;
        // How much of a point is shown: the box's own edges fade it out, so nothing leaves it.
        const auto inside = [&](float px, float py) {
            const float edge = cell * 0.35f;
            const float ex = std::min(px - boxPx.x, boxPx.right() - px) / edge;
            const float ey = std::min(py - boxPx.y, boxPx.bottom() - py) / edge;
            return std::clamp(std::min(ex, ey), 0.0f, 1.0f);
        };
        if (won) {
            for (int at = 0; at < sim::kMachineCells; ++at) {
                const sim::Held& held = box[at];
                if (held.empty()) continue;
                const Box item = P(itemBox(tables, at, held));
                const content::ItemRow& row = tables.items[size_t(held.item)];
                const int cells = std::max(1, int(row.width) * int(row.height));
                // The flash: up in 0.08 s, then dying away, brightest at the thing's middle.
                const float flash = t < 0.08f ? t / 0.08f : std::exp(-(t - 0.08f) * 4.5f);
                if (flash > 0.01f) {
                    const float c[2] = {item.midX(), item.midY()};
                    const float rim[10] = {item.x, item.y, item.right(), item.y, item.right(),
                                           item.bottom(), item.x, item.bottom(), item.x, item.y};
                    glowFan(canvas_, c, rim, 5, kFlashGold, 0.5f * flash, 0.0f);
                }
                // The embers, tiny (the user, 2026-10-20: "sparkles has to be tiny"): sixteen and five more a cell, born over the first 1.3 s, most early.
                const int embers = 16 + 5 * cells;
                for (int i = 0; i < embers; ++i) {
                    const uint32_t key = uint32_t(at * 977 + i * 131 + 7);
                    const float born = 1.3f * std::pow(hash01(key, 1), 1.6f);
                    const float life = 0.55f + 0.35f * hash01(key, 2);
                    const float age = t - born;
                    if (age < 0.0f || age > life) continue;
                    const float p = age / life;
                    const float sx = item.x + item.w * (0.15f + 0.7f * hash01(key, 3));
                    const float sy = item.y + item.h * (0.25f + 0.7f * hash01(key, 4));
                    const float rise = cell * (0.5f + 0.6f * hash01(key, 5));
                    const float eased = 1.0f - (1.0f - p) * (1.0f - p);
                    const float px = sx + std::sin(p * 4.7f + hash01(key, 6) * 6.28f) * cell * 0.08f;
                    const float py = sy - rise * eased;
                    const float in = std::min(1.0f, p / 0.12f), out = (1.0f - p) * (1.0f - p);
                    const float shown = in * out * inside(px, py);
                    if (shown <= 0.01f) continue;
                    const float r = k * (0.4f + 0.3f * hash01(key, 7)) * (1.0f - 0.5f * p);
                    // White-gold at birth, orange as it cools.
                    const Tint hot{1.0f, 0.93f, 0.68f}, cool{1.0f, 0.55f, 0.18f};
                    const Tint tint = hot.mix(cool, p);
                    glowDisc(canvas_, px, py, r * 3.0f, tint, 0.3f * shown);
                    glowDisc(canvas_, px, py, r, tint, 0.95f * shown);
                }
                // The glints: two short four-pointed stars on the thing in its first 0.7 s.
                for (int i = 0; i < 2; ++i) {
                    const uint32_t key = uint32_t(at * 613 + i * 71 + 3);
                    const float born = 0.05f + 0.45f * hash01(key, 1);
                    const float age = t - born;
                    if (age < 0.0f || age > 0.32f) continue;
                    const float p = age / 0.32f;
                    const float shown = std::sin(p * 3.14159f);
                    const float px = item.x + item.w * (0.25f + 0.5f * hash01(key, 2));
                    const float py = item.y + item.h * (0.2f + 0.6f * hash01(key, 3));
                    star(canvas_, px, py, cell * (0.11f + 0.05f * hash01(key, 4)), k * 0.45f,
                         Tint{1.0f, 0.95f, 0.8f}, 0.9f * shown);
                }
            }
        } else {
            // The pulse: the box goes dark red and comes back.
            const float pulse = t < 0.06f ? t / 0.06f : std::exp(-(t - 0.06f) * 3.2f);
            if (pulse > 0.01f) canvas_.rect(boxPx, gfx::rgba(0.40f, 0.04f, 0.03f, 0.38f * pulse));
            // The ash: grey flakes drifting down across the box.
            for (int i = 0; i < 34; ++i) {
                const uint32_t key = uint32_t(i * 389 + 11);
                const float born = 1.0f * hash01(key, 1);
                const float life = 0.9f + 0.5f * hash01(key, 2);
                const float age = t - born;
                if (age < 0.0f || age > life) continue;
                const float p = age / life;
                const float px = boxPx.x + boxPx.w * hash01(key, 3) +
                                 std::sin(p * 5.0f + hash01(key, 4) * 6.28f) * cell * 0.12f;
                const float py = boxPx.y + boxPx.h * 0.55f * hash01(key, 5) + cell * (0.5f + 0.7f * hash01(key, 6)) * p;
                const float shown = std::min(1.0f, p / 0.15f) * (1.0f - p) * inside(px, py);
                if (shown <= 0.01f) continue;
                disc(canvas_, px, py, k * (0.4f + 0.35f * hash01(key, 7)),
                     gfx::rgba(0.58f, 0.55f, 0.50f, 0.75f * shown));
            }
            // And a last dim ember or two dying in the red, early.
            for (int i = 0; i < 6; ++i) {
                const uint32_t key = uint32_t(i * 977 + 5);
                const float age = t - 0.25f * hash01(key, 1);
                if (age < 0.0f || age > 0.5f) continue;
                const float p = age / 0.5f;
                const float px = boxPx.x + boxPx.w * (0.2f + 0.6f * hash01(key, 2));
                const float py = boxPx.y + boxPx.h * (0.3f + 0.5f * hash01(key, 3)) - cell * 0.3f * p;
                glowDisc(canvas_, px, py, k * 1.6f, Tint{0.9f, 0.25f, 0.12f}, 0.3f * (1.0f - p));
            }
        }
    }

    // What rides the pointer, cut from the carry strip (panel::carried, on this stage).
    if (dragging_ >= 0 && !box[dragging_].empty()) {
        const Box place = itemBox(tables, dragging_, box[dragging_]);
        const Box to{now_.dragX - place.w * k * 0.5f, now_.dragY - place.h * k * 0.5f, place.w * k,
                     place.h * k};
        if (picture.valid()) {
            canvas_.region(picture, to, {4.0f * sx, kCarryTop * sy, place.w * sx, place.h * sy});
        } else {
            canvas_.rect(to, gfx::rgba(0.68f, 0.60f, 0.40f, 0.5f));
        }
        if (part_ > 1) panel::carriedCount(canvas_, place, now_.dragX, now_.dragY, part_);
    }

    if (dragging_ < 0 && hovered_ >= 0 && !box[hovered_].empty()) {
        tip::Sheet sheet = describe(tables, box[hovered_], realm.wearer(), realm.satchel());
        splitHint(tables, box[hovered_], sheet);
        if (tipStage_) tip::stand(*tipStage_, box[hovered_].item, box[hovered_].refinement, sheet);
        const Box cell = P(itemBox(tables, hovered_, box[hovered_]));
        tip::draw(tip_, sheet, cell, screenW_, screenH_);
    }
}

}  // namespace mu::game
