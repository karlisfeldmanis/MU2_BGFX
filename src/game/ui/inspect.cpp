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
// **One gap everywhere** (the user, 2026-10-16: "work on gapings, paddings, positions"): four units
// between any two slots down a column, the jewellery's three spread over the armour's and the
// gloves' height so the right column's rows meet the left's at the pet, the mount and the hand.
constexpr float kLeft = 11.0f, kRight = 148.0f, kColumn = 31.0f, kSmall = 22.0f;
constexpr float kGap = 4.0f;
constexpr float kTop = 44.0f;
constexpr float kArmourTall = 44.0f, kHandTall = 46.0f;
constexpr float kRow[6] = {kTop,                                            // helm, wings
                           kTop + kColumn + kGap,                           // armour, the jewellery
                           kTop + kColumn + kArmourTall + 2.0f * kGap,      // gloves
                           kTop + 2.0f * kColumn + kArmourTall + 3.0f * kGap,  // pants, pet
                           kTop + 3.0f * kColumn + kArmourTall + 4.0f * kGap,  // boots, mount
                           kTop + 4.0f * kColumn + kArmourTall + 5.0f * kGap}; // the hands
// The whole width between the window's edges, behind both columns: his wings spread there and the
// slots stand over their tips. Portrait's figure is 432 by 640, the same shape.
constexpr Box kFigure{kLeft, 37.0f, 168.0f, 249.0f};
// Under the hands: the score's words, its figure large ("gear score has to be bigger"), the
// numbers' well down to the foot's rule with the same air over it as the hands have under them.
// The words sit close over the figure (the user: "gear score label has to be closer to score").
constexpr float kScoreWords = kRow[5] + kHandTall + 19.0f;
constexpr float kScoreFigure = kScoreWords + 18.0f;
constexpr float kNumbersTop = kScoreFigure + 9.0f;
// Down to the window's foot, with the side margin under it: no buttons there since the user's
// "remove footer buttons but show more stats" -- four rows of three.
constexpr int kNumberRows = 6, kNumberColumns = 2;
constexpr Box kNumbers{kLeft, kNumbersTop, panel::kWellRight - kLeft, panel::kHeight - kLeft - kNumbersTop};

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
    return (ink & 0x00FFFFFFu) | (uint32_t(0x90) << 24);
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
    // The jewellery's three over the armour's and the gloves' rows, the room left shared as two gaps.
    const float span = kArmourTall + kGap + kColumn;
    const float step = kSmall + (span - 3.0f * kSmall) * 0.5f;
    switch (slot) {
        case sim::kHelm: return {kLeft, kRow[0], kColumn, kColumn};
        case sim::kArmour: return {kLeft, kRow[1], kColumn, kArmourTall};
        case sim::kGloves: return {kLeft, kRow[2], kColumn, kColumn};
        case sim::kPants: return {kLeft, kRow[3], kColumn, kColumn};
        case sim::kBoots: return {kLeft, kRow[4], kColumn, kColumn};
        case sim::kWings: return {kRight, kRow[0], kColumn, kColumn};
        case sim::kAmulet: return {small, kRow[1], kSmall, kSmall};
        case sim::kRingRight: return {small, kRow[1] + step, kSmall, kSmall};
        case sim::kRingLeft: return {small, kRow[1] + 2.0f * step, kSmall, kSmall};
        case sim::kPet: return {kRight, kRow[3], kColumn, kColumn};
        case sim::kMount: return {kRight, kRow[4], kColumn, kColumn};
        // His right hand under the armour's column, his left under the jewellery's, as the bag puts
        // them: MU names the slots for the side of the screen.
        case sim::kWeaponRight: return {kLeft, kRow[5], kColumn, kHandTall};
        case sim::kWeaponLeft: return {kRight, kRow[5], kColumn, kHandTall};
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
    hovered_ = -1;
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

    // The pointer against this frame's layout: a worn piece, the figure, the close.
    hovered_ = -1;
    for (const int slot : kShown) {
        if (!bag[slot].empty() && slotBox(slot).has(ux, uy)) hovered_ = slot;
    }
    overClose_ = panel::scaled(x_, y_, panel::frameClose()).has(pointer.x, pointer.y);
    if (pointer.pressed) {
        closing_ = overClose_;
        // A press on him, between the columns and off every slot, takes hold of him to turn.
        turning_ = hovered_ < 0 && !overClose_ && kFigure.has(ux, uy) &&
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
        if (out && closing_ && overClose_) out->close = true;
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
    now_.hovered = hovered_;
    now_.overClose = overClose_, now_.closing = closing_;
    now_.figure = figure && figure->valid() ? figure->handle.idx : 0xFFFF;
    now_.items = stage && stage->picture().valid() ? stage->picture().handle.idx : 0xFFFF;
    now_.level = who.level;
    now_.kin = int(who.kin);
    now_.second = who.second;
    if (realm.tables()) now_.score = sim::gearScore(*realm.tables(), bag);
    // **Twelve of his numbers**, as the character window words them: his four attributes with the
    // stat runes in; his damage (a wizard's wizardry) and defence; the tops of his three pools; his
    // two rates and his critical chance.
    {
        const sim::HeroPoints total = who.totalPoints();
        const sim::Fighter& f = who.stats;
        const bool wizard = who.kin == sim::Kin::DarkWizard;
        const long long lo = wizard ? (long long)f.wizardMinimum : f.minimumDamage + f.offhandMinimumDamage;
        const long long hi = wizard ? (long long)f.wizardMaximum : f.maximumDamage + f.offhandMaximumDamage;
        const auto percent = [](double share) { return std::to_string(int(std::lround(share * 100.0))) + "%"; };
        // Down the left his attributes and his pools, down the right his fight, row by row: what
        // he is beside what it buys. Read row-major, two across (rebuild).
        const std::pair<const char*, std::string> rows[12] = {
            {"STRENGTH", commas(total.strength)},     {wizard ? "WIZARDRY" : "DAMAGE", commas(lo) + " ~ " + commas(hi)},
            {"AGILITY", commas(total.agility)},       {"DEFENSE", commas(f.defense)},
            {"VITALITY", commas(total.vitality)},     {"ATTACK RATE", commas((long long)f.attackRate)},
            {"ENERGY", commas(total.energy)},         {"DEFENSE RATE", commas((long long)f.defenseRate)},
            {"LIFE", commas(who.maxHealth)},          {"CRITICAL", percent(f.criticalChance)},
            {"MANA", commas(who.maxMana)},            {"SHIELD", commas(who.maxSd)}};
        // An attribute the stat runes raise reads in the option blue, as the character window has it.
        const bool raised[4] = {total.strength != who.points.strength, total.agility != who.points.agility,
                                total.vitality != who.points.vitality, total.energy != who.points.energy};
        for (int a = 0; a < 4; ++a) now_.raised[a * 2] = raised[a];
        for (int i = 0; i < 12; ++i) {
            now_.labels[i] = rows[i].first;
            now_.values[i] = rows[i].second;
        }
    }
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
        // And his resets after it (the user, 2026-10-16), as the character window prints his own:
        // the realm keeps none yet, so 0 until it does (Card says the same).
        bits.push_back({"RESETS", true, wide});
        bits.push_back({std::to_string(now_.resets), false, gap});
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
        pool(fig.midX(), fig.y + fig.h * 0.94f, fig.w * 0.26f, 6.0f * k, gfx::rgba(0.0f, 0.0f, 0.0f, 0.5f));
        if (figure && figure->valid()) canvas_.image(*figure, fig);
    }

    // **His pieces**: a well each, the bag's ghost in an empty one, the piece's rim in its name's
    // colour, and the stage's one picture over them all.
    for (const int slot : kShown) {
        const Box box = slotBox(slot);
        const sim::Held& held = bag[slot];
        // **A well with a small radius** (the user, 2026-10-16: "use some minimal border radius"): the
        // controls' three, its hairline the piece's rim in its name's colour, or the cell's own line
        // when empty, and lit under the pointer as the bag's cell is.
        {
            const Box b = panel::scaled(x, y, box);
            const Box r{std::round(b.x), std::round(b.y), std::round(b.w), std::round(b.h)};
            const float line = std::max(1.0f, std::round(u)), radius = style::kRadiusControl * u;
            const bool over = slot == now_.hovered;
            const uint32_t edge = over ? gfx::rgba(0.788f, 0.749f, 0.682f, 0.75f)
                                  : held.empty() ? style::kCellLine
                                                 : rimOf(tables, held);
            tip::panel(canvas_, r, radius, edge, edge);
            tip::panel(canvas_, r.grown(-line), std::max(0.0f, radius - line), style::kCellFloor, style::kCellFloor);
            if (over) {
                tip::panel(canvas_, r.grown(-line), std::max(0.0f, radius - line), gfx::rgba(1.0f, 0.922f, 0.824f, 0.08f),
                           gfx::rgba(1.0f, 0.922f, 0.824f, 0.08f));
            }
        }
        if (held.empty()) {
            const gfx::Art& ghost = arts.get(ghostArt(slot, who.kin == sim::Kin::FairyElf));
            if (ghost.valid()) canvas_.image(ghost, panel::scaled(x, y, ghostIn(box, ghost)), kGhostInk);
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
        const float from = std::round(x + (panel::kWidth * k - words) * 0.5f);
        controls::caps(canvas_, from, std::round(y + kScoreWords * k), small, style::kAshInk, "GEAR SCORE");
        // A rule either side of the words, fading out toward the window's edges: the score's own
        // heading, set apart from the figure above it.
        const float ruleY = std::round(y + (kScoreWords - 3.0f) * k), air = 8.0f * u;
        const float reach = 46.0f * k;
        canvas_.shade({from - air - reach, ruleY, reach, std::max(1.0f, u)}, style::kIron & 0x00FFFFFFu, style::kIron,
                      style::kIron, style::kIron & 0x00FFFFFFu);
        canvas_.shade({from + words + air, ruleY, reach, std::max(1.0f, u)}, style::kIron, style::kIron & 0x00FFFFFFu,
                      style::kIron & 0x00FFFFFFu, style::kIron);
        const std::string figureText = commas(now_.score);
        const float size = 32.0f * u;
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

    // **His numbers** under it (update's twelve): a table of two columns and six rows in one well,
    // each row a label in ash capitals at the left and its figure ranged right, the rows told apart
    // by a faint band on every other one rather than ruled -- no line stops short of another (the
    // user: "stats container has to be better designed without weird gaps") -- and one rule down
    // the middle, the well's whole height.
    {
        const Box well = panel::scaled(x, y, kNumbers);
        controls::well(canvas_, well, u);
        const float inset = 2.0f;  // the bands' and the rule's air inside the well's edge
        const float cellW = (kNumbers.w - 2.0f * inset) / float(kNumberColumns);
        const float cellH = (kNumbers.h - 2.0f * inset) / float(kNumberRows);
        for (int r = 1; r < kNumberRows; r += 2) {
            const uint32_t band = gfx::rgba(1.0f, 0.94f, 0.86f, 0.025f);
            tip::panel(canvas_, panel::scaled(x, y, {kNumbers.x + inset, kNumbers.y + inset + cellH * float(r),
                                                     kNumbers.w - 2.0f * inset, cellH}),
                       style::kRadiusSmall * u, band, band);
        }
        canvas_.rect(panel::scaled(x, y, {kNumbers.x + inset + cellW - 0.25f, kNumbers.y + inset, 0.5f,
                                          kNumbers.h - 2.0f * inset}),
                     style::kIronLo);
        const float pad = 7.0f;
        for (int i = 0; i < kNumberColumns * kNumberRows; ++i) {
            const float cx = kNumbers.x + inset + cellW * float(i % kNumberColumns);
            const float cy = kNumbers.y + inset + cellH * float(i / kNumberColumns);
            const float baseline = std::round(y + (cy + cellH * 0.5f + 3.5f) * k);
            controls::caps(canvas_, std::round(x + (cx + pad) * k), baseline, 8.0f * u, style::kAshInk, now_.labels[i],
                           0.06f);
            float size = 12.0f * u;
            const float room = cellW - 2.0f * pad - controls::capsWidth(8.0f * u, now_.labels[i], 0.06f) / k - 4.0f;
            while (size > 7.5f * u && controls::labelWidth(size, now_.values[i]) > room * k) size -= 0.5f * u;
            controls::ranged(canvas_, std::round(x + (cx + cellW - pad) * k), baseline, size,
                             now_.raised[i] ? panel::kOptioned : style::kBoneHi, now_.values[i]);
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
