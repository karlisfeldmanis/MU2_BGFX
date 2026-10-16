#include "game/ui/inspect.h"

#include <algorithm>
#include <cmath>

#include "game/ui/bag.h"
#include "game/ui/controls.h"
#include "game/ui/describe.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"
#include "sim/quests.h"

namespace mu::game {
namespace {

using gfx::Box;

// The layout, in MU's 190 x 429 panel units: the canvas's B3 (2x pixels there) halved and fitted
// under the panel's own 36-unit head. Armour down the left column, wings, jewellery, pet and mount
// down the right, a hand under each column, the figure between them.
constexpr float kLeft = 11.0f, kRight = 148.0f, kColumn = 31.0f, kSmall = 21.0f;
constexpr float kTop = 44.0f;
// The whole width between the window's edges, behind both columns: his wings spread there and the
// slots stand over their tips. Portrait's figure is 432 by 640, the same shape.
constexpr Box kFigure{kLeft, 38.0f, 168.0f, 249.0f};
constexpr float kScoreWords = 290.0f, kScoreFigure = 312.0f;
constexpr Box kNumbers{kLeft, 324.0f, panel::kWellRight - kLeft, 52.0f};
constexpr float kFootButtons = 390.0f, kButtonTall = 22.0f, kButtonGap = 4.0f;

Box footButton(int i) {
    const float wide = (panel::kWellRight - kLeft - 2.0f * kButtonGap) / 3.0f;
    return {kLeft + float(i) * (wide + kButtonGap), kFootButtons, wide, kButtonTall};
}

// The ghost in an empty slot: the bag's own silhouette, faint, at its own aspect.
constexpr uint32_t kGhostInk = gfx::rgba(0.90f, 0.86f, 0.76f, 0.17f);
Box ghostIn(const Box& cell, const gfx::Art& art) {
    const Box room = cell.grown(-std::min(5.0f, 0.12f * std::min(cell.w, cell.h)));
    const float s = std::min(room.w / art.width, room.h / art.height) * 0.8f;
    const float w = art.width * s, h = art.height * s;
    return {room.x + (room.w - w) * 0.5f, room.y + (room.h - h) * 0.5f, w, h};
}

// The gear score's ink by its rung, as the bag's head prints his own (bag.cpp scoreInk).
uint32_t scoreInk(int score) {
    static const tip::Tone kRungs[5] = {tip::Tone::Uncommon, tip::Tone::Legendary, tip::Tone::Rare,
                                        tip::Tone::Epic, tip::Tone::RuneLegendary};
    return tip::colourOf(kRungs[sim::gearRung(score)]);
}

// A worn piece's rim, faint in its name's colour: what a glance at the column reads first.
uint32_t rimOf(const content::Tables& tables, const sim::Held& held) {
    const uint32_t ink = tip::colourOf(qualityOf(tables.items[size_t(held.item)], held));
    return (ink & 0x00FFFFFFu) | (uint32_t(0x70) << 24);
}

constexpr int kShown[] = {sim::kHelm,  sim::kArmour,      sim::kGloves,  sim::kPants,     sim::kBoots,
                          sim::kWings, sim::kAmulet,      sim::kRingRight, sim::kRingLeft, sim::kPet,
                          sim::kMount, sim::kWeaponRight, sim::kWeaponLeft};

std::string commas(long long v) { return panel::commas(v); }

}  // namespace

bool inspectable(const sim::Realm& realm, uint32_t id) {
    const sim::Body* body = id != 0 ? realm.find(id) : nullptr;
    return body && body->player && !body->gone && id != realm.hero().id && realm.satchelOf(id);
}

Box Inspect::slotBox(int slot) {
    const float small = kRight + (kColumn - kSmall) * 0.5f;
    switch (slot) {
        case sim::kHelm: return {kLeft, kTop, kColumn, kColumn};
        case sim::kArmour: return {kLeft, kTop + 35.0f, kColumn, 44.0f};
        case sim::kGloves: return {kLeft, kTop + 83.0f, kColumn, kColumn};
        case sim::kPants: return {kLeft, kTop + 118.0f, kColumn, kColumn};
        case sim::kBoots: return {kLeft, kTop + 153.0f, kColumn, kColumn};
        case sim::kWings: return {kRight, kTop, kColumn, kColumn};
        case sim::kAmulet: return {small, kTop + 37.0f, kSmall, kSmall};
        case sim::kRingRight: return {small, kTop + 62.0f, kSmall, kSmall};
        case sim::kRingLeft: return {small, kTop + 87.0f, kSmall, kSmall};
        case sim::kPet: return {kRight, kTop + 118.0f, kColumn, kColumn};
        case sim::kMount: return {kRight, kTop + 153.0f, kColumn, kColumn};
        // His right hand under the armour's column, his left under the jewellery's, as the bag puts
        // them: MU names the slots for the side of the screen.
        case sim::kWeaponRight: return {kLeft, kTop + 190.0f, kColumn, 50.0f};
        case sim::kWeaponLeft: return {kRight, kTop + 190.0f, kColumn, 50.0f};
        default: return {};
    }
}

void Inspect::open(const gfx::Interface& interface, panel::Arts* arts) {
    interface.adopt(canvas_);
    interface.adopt(tip_);
    arts_ = arts;
}

void Inspect::hide() {
    up_ = false;
    turning_ = false;
    drawnOnce_ = false;
    hovered_ = over_ = pressing_ = -1;
    closing_ = overClose_ = false;
    canvas_.clear();
    tip_.clear();
}

bool Inspect::covers(float x, float y) const {
    return up_ && Box{x_, y_, panel::kWidth * panel::scale(), panel::kHeight * panel::scale()}.has(x, y);
}

void Inspect::update(float width, float height, int column, const sim::Realm& realm, const sim::Body& who,
                     const sim::Satchel& bag, const std::string& name, const std::string& guild,
                     const gfx::Art* figure, const Pointer& pointer, Stage* stage, InspectRequests* out) {
    up_ = true;
    x_ = panel::columnX(width, column);
    y_ = panel::panelY(height);
    const float k = panel::scale();
    const float ux = (pointer.x - x_) / k, uy = (pointer.y - y_) / k;

    // The pointer against this frame's layout: a worn piece, one of the foot's three, the close.
    hovered_ = -1;
    for (const int slot : kShown) {
        if (!bag[slot].empty() && slotBox(slot).has(ux, uy)) hovered_ = slot;
    }
    over_ = -1;
    for (int i = 0; i < 3; ++i) {
        if (footButton(i).has(ux, uy)) over_ = i;
    }
    overClose_ = panel::scaled(x_, y_, panel::frameClose()).has(pointer.x, pointer.y);
    if (pointer.pressed) {
        pressing_ = over_;
        closing_ = overClose_;
        // A press on him, between the columns and off every slot, takes hold of him to turn.
        turning_ = hovered_ < 0 && over_ < 0 && !overClose_ && kFigure.has(ux, uy) &&
                   ux > kLeft + kColumn && ux < kRight;
        turnFrom_ = pointer.x;
    }
    // Held, he follows the pointer across: a half turn for a third of the window's width.
    if (turning_ && pointer.held) {
        turn_ += (pointer.x - turnFrom_) / (kFigure.w * k) * 4.7f;
        turn_ = std::remainder(turn_, 6.2831853f);
        turnFrom_ = pointer.x;
    }
    if (pointer.released || !pointer.held) turning_ = false;
    if (pointer.released) {
        // Only if the pointer is still on it, which is how a press is taken back.
        if (out) {
            if (closing_ && overClose_) out->close = out->clicked = true;
            if (pressing_ >= 0 && pressing_ == over_) {
                out->whisper = pressing_ == 0;
                out->trade = pressing_ == 1;
                out->invite = pressing_ == 2;
                out->clicked = true;
            }
        }
        pressing_ = -1;
        closing_ = false;
    }

    // His pieces on the stage, each in its slot's well, the hovered one turning as in the bag.
    standing_.clear();
    for (const int slot : kShown) {
        const sim::Held& held = bag[slot];
        if (held.empty()) continue;
        standing_.push_back({held.item, slotBox(slot).grown(-3.0f), held.refinement, slot == hovered_,
                             held.excellent != 0});
    }
    if (stage) stage->stand(standing_, panel::kWidth, panel::kHeight);

    now_ = Look{};
    now_.id = who.id;
    now_.version = bag.version();
    now_.x = x_, now_.y = y_, now_.scale = k, now_.unit = tip::unit();
    now_.screenW = width, now_.screenH = height;
    now_.hovered = hovered_, now_.over = over_, now_.pressing = pressing_;
    now_.overClose = overClose_, now_.closing = closing_;
    now_.figure = figure && figure->valid() ? figure->handle.idx : 0xFFFF;
    now_.items = stage && stage->picture().valid() ? stage->picture().handle.idx : 0xFFFF;
    now_.level = who.level;
    now_.kin = int(who.kin);
    now_.second = who.second;
    if (realm.tables()) now_.score = sim::gearScore(*realm.tables(), bag);
    const sim::HeroPoints total = who.totalPoints();
    now_.numbers[0] = total.strength, now_.numbers[1] = total.agility;
    now_.numbers[2] = total.vitality, now_.numbers[3] = total.energy;
    now_.numbers[4] = who.stats.defense;
    const bool wizard = who.kin == sim::Kin::DarkWizard;
    now_.damage[0] = wizard ? (long long)who.stats.wizardMinimum
                            : who.stats.minimumDamage + who.stats.offhandMinimumDamage;
    now_.damage[1] = wizard ? (long long)who.stats.wizardMaximum
                            : who.stats.maximumDamage + who.stats.offhandMaximumDamage;
    now_.name = name;
    now_.guild = guild;
    if (drawnOnce_ && now_ == drawn_) return;
    drawn_ = now_;
    drawnOnce_ = true;
    rebuild(realm, who, bag, figure, stage);
}

void Inspect::rebuild(const sim::Realm& realm, const sim::Body& who, const sim::Satchel& bag,
                      const gfx::Art* figure, Stage* stage) {
    canvas_.clear();
    tip_.clear();
    if (!arts_ || !realm.tables()) return;
    panel::Arts& arts = *arts_;
    const content::Tables& tables = *realm.tables();
    const float x = x_, y = y_, k = panel::scale(), u = tip::unit();

    // **His name is the title**; under it, in the head as the character window prints his own,
    // his class, his level and his guild in brackets.
    panel::frame(canvas_, arts, x, y, now_.name.empty() ? std::string("Inspect") : now_.name);
    {
        const float size = 11.0f * u, small = 9.0f * u, gap = 4.0f * u, wide = 10.0f * u;
        struct Bit {
            std::string text;
            bool caps;
            float before;
        };
        std::vector<Bit> bits;
        bits.push_back({sim::className(now_.kin, now_.second), false, 0.0f});
        bits.push_back({"LEVEL", true, wide});
        bits.push_back({std::to_string(now_.level), false, gap});
        if (!now_.guild.empty()) bits.push_back({"[" + now_.guild + "]", true, wide});
        float total = 0.0f;
        for (const Bit& bit : bits)
            total += bit.before + (bit.caps ? controls::capsWidth(small, bit.text) : controls::labelWidth(size, bit.text));
        float at = std::round(x + (panel::kWidth * k - total) * 0.5f);
        const float baseline = std::round(y + panel::kHeadBand * k * 0.86f);
        for (const Bit& bit : bits) {
            at += bit.before;
            if (bit.caps) {
                controls::caps(canvas_, at, baseline, small, style::kAshInk, bit.text);
                at += controls::capsWidth(small, bit.text);
            } else {
                at += controls::label(canvas_, at, baseline, size, style::kBoneHi, bit.text);
            }
        }
    }
    panel::close(canvas_, x, y, now_.overClose, now_.closing);

    // **Him**, standing in a soft pool of the window's warm light, an edgeless shadow at his feet.
    {
        const Box fig = panel::scaled(x, y, kFigure);
        const auto pool = [&](float cx, float cy, float rx, float ry, uint32_t core) {
            constexpr int kSides = 40;
            float xy[2 * (kSides + 2)];
            uint32_t ink[kSides + 2];
            xy[0] = cx, xy[1] = cy, ink[0] = core;
            for (int i = 0; i <= kSides; ++i) {
                const float a = float(i) / float(kSides) * 6.2831853f;
                xy[2 * (i + 1)] = cx + std::cos(a) * rx;
                xy[2 * (i + 1) + 1] = cy + std::sin(a) * ry;
                ink[i + 1] = core & 0x00FFFFFFu;
            }
            canvas_.polygon(xy, ink, kSides + 2);
        };
        pool(fig.midX(), fig.y + fig.h * 0.5f, fig.w * 0.42f, fig.h * 0.46f, gfx::rgba(0.38f, 0.24f, 0.14f, 0.22f));
        pool(fig.midX(), fig.y + fig.h * 0.9f, fig.w * 0.26f, 6.0f * k, gfx::rgba(0.0f, 0.0f, 0.0f, 0.5f));
        if (figure && figure->valid()) canvas_.image(*figure, fig);
    }

    // **His pieces**: a well each, the bag's ghost in an empty one, the piece's rim in its name's
    // colour, and the stage's one picture over them all.
    for (const int slot : kShown) {
        const Box box = slotBox(slot);
        const sim::Held& held = bag[slot];
        panel::cell(canvas_, x, y, box, slot == now_.hovered ? sheet::Cell::Over : sheet::Cell::Rest);
        if (held.empty()) {
            const gfx::Art& ghost = arts.get(ghostArt(slot, who.kin == sim::Kin::FairyElf));
            if (ghost.valid()) canvas_.image(ghost, panel::scaled(x, y, ghostIn(box, ghost)), kGhostInk);
        } else {
            canvas_.outline(panel::scaled(x, y, box), std::max(1.0f, u), rimOf(tables, held));
        }
    }
    const gfx::Art picture = stage ? stage->picture() : gfx::Art{};
    if (picture.valid()) {
        const float sx = picture.width / panel::kWidth, sy = picture.height / panel::kHeight;
        canvas_.region(picture, panel::scaled(x, y, {0.0f, 0.0f, panel::kWidth, panel::kHeight}),
                       {0.0f, 0.0f, panel::kWidth * sx, panel::kHeight * sy});
    }
    // And each piece's plus in its corner, gold as the trade window prints it.
    for (const int slot : kShown) {
        const sim::Held& held = bag[slot];
        if (held.empty() || held.refinement <= 0) continue;
        const Box b = panel::scaled(x, y, slotBox(slot));
        const std::string plus = "+" + std::to_string(held.refinement);
        const float size = 9.5f * u;
        controls::ranged(canvas_, b.right() - 3.0f * u, b.bottom() - 3.0f * u, size,
                         tip::colourOf(qualityOf(tables.items[size_t(held.item)], held)), plus);
    }

    // **The gear score at his feet**, in its rung's colour, as the bag's head prints his own.
    {
        const float small = 9.0f * u;
        const float words = controls::capsWidth(small, "GEAR SCORE");
        controls::caps(canvas_, std::round(x + (panel::kWidth * k - words) * 0.5f),
                       std::round(y + kScoreWords * k), small, style::kAshInk, "GEAR SCORE");
        const std::string figureText = commas(now_.score);
        const float size = 20.0f * u;
        const gfx::Face* face = panel::titleFace();
        if (face && bgfx::isValid(panel::titleTexture())) {
            const float wide = face->measure(size, figureText);
            canvas_.lettered(*face, panel::titleTexture(), std::round(x + (panel::kWidth * k - wide) * 0.5f),
                             std::round(y + kScoreFigure * k), size, 0.0f, scoreInk(now_.score), figureText);
        } else {
            const float wide = controls::labelWidth(size, figureText);
            controls::label(canvas_, std::round(x + (panel::kWidth * k - wide) * 0.5f), std::round(y + kScoreFigure * k),
                            size, scoreInk(now_.score), figureText);
        }
    }

    // **Six of his numbers** under it, three across and two down, a label over its figure: his four
    // attributes with the stat runes in, his damage (a wizard's wizardry), his defence.
    {
        const Box well = panel::scaled(x, y, kNumbers);
        controls::well(canvas_, well, u);
        const bool wizard = now_.kin == int(sim::Kin::DarkWizard);
        const std::string labels[6] = {"STRENGTH", "AGILITY", "VITALITY", "ENERGY", wizard ? "WIZARDRY" : "DAMAGE",
                                       "DEFENSE"};
        const std::string values[6] = {commas(now_.numbers[0]), commas(now_.numbers[1]), commas(now_.numbers[2]),
                                       commas(now_.numbers[3]),
                                       commas(now_.damage[0]) + " ~ " + commas(now_.damage[1]),
                                       commas(now_.numbers[4])};
        const float cellW = kNumbers.w / 3.0f, cellH = kNumbers.h / 2.0f;
        for (int i = 0; i < 6; ++i) {
            const float cx = kNumbers.x + cellW * float(i % 3), cy = kNumbers.y + cellH * float(i / 3);
            if (i % 3 != 0) canvas_.rect(panel::scaled(x, y, {cx, cy + 3.0f, 0.5f, cellH - 6.0f}), style::kIronLo);
            if (i >= 3 && i % 3 == 0)
                canvas_.rect(panel::scaled(x, y, {kNumbers.x + 3.0f, cy, kNumbers.w - 6.0f, 0.5f}), style::kIronLo);
            controls::caps(canvas_, std::round(x + (cx + 6.0f) * k), std::round(y + (cy + 9.5f) * k), 8.0f * u,
                           style::kAshInk, labels[i], 0.06f);
            float size = 11.5f * u;
            while (size > 7.5f * u && controls::labelWidth(size, values[i]) > (cellW - 10.0f) * k) size -= 0.5f * u;
            controls::label(canvas_, std::round(x + (cx + 6.0f) * k), std::round(y + (cy + 21.0f) * k), size,
                            style::kBoneHi, values[i]);
        }
    }

    // **The foot**: the shared rule, and Whisper, Trade and Invite, each its icon and its word.
    canvas_.rect(panel::scaled(x, y, {kLeft, panel::kFootRule, panel::kWellRight - kLeft, 0.5f}), style::kIronLo);
    {
        static const char* const kWords[3] = {"Whisper", "Trade", "Invite"};
        static const controls::Glyph kIcons[3] = {controls::Glyph::Whisper, controls::Glyph::Trade,
                                                  controls::Glyph::Invite};
        for (int i = 0; i < 3; ++i) {
            controls::State state;
            state.lift = now_.over == i ? 1.0f : 0.0f;
            state.held = now_.pressing == i && now_.over == i;
            controls::button(canvas_, panel::scaled(x, y, footButton(i)), kWords[i], kIcons[i],
                             controls::Kind::Secondary, state, u);
        }
    }

    // The tip, last: the hovered piece's card, read against the viewer, over its own slot.
    if (now_.hovered >= 0 && !bag[now_.hovered].empty()) {
        const sim::Held& held = bag[now_.hovered];
        tip::Sheet sheet = describe(tables, held, realm.wearer(), bag);
        if (tipStage_) tip::stand(*tipStage_, held.item, held.refinement, sheet);
        tip::draw(tip_, sheet, panel::scaled(x, y, slotBox(now_.hovered)), now_.screenW, now_.screenH);
    }
}

}  // namespace mu::game
