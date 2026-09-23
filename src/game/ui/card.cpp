#include "game/ui/card.h"

#include <string>

#include "game/ui/sheet.h"
#include "sim/rules.h"

namespace mu::game {
namespace {

using gfx::Box;

// Card.cs's table, in MU's panel units. The rows are 120, 175, 240, 295 -- not evenly spaced,
// because the gaps hold the lines under each stat (`HEIGHT_STRENGTH + 25`, `iY += 13`).
struct Row {
    int stat;
    float y;
    const char* label;
};
// **One rhythm.** MU's rows are at 120, 175, 240 and 295 -- gaps of 55, 65 and 55, because its
// own window had a carved divider to hide under the wide one. Every group here is a well and its
// lines, so they are spaced evenly: sixty between each, which leaves fifteen units of air under
// the two that carry two lines and twenty-seven under the two that carry one -- and the block
// then reaches down to the foot instead of stopping a third of the way up it.
// Sixty-four apart since the foot moved to the panel's shared rule at 382: the block starts
// eighteen under the summary and Energy's line ends twenty-seven over the rule.
constexpr Row kRows[4] = {
    {0, 128.0f, "Strength"}, {1, 192.0f, "Agility"}, {2, 256.0f, "Vitality"}, {3, 320.0f, "Energy"}};

// The plus beside a row, moved two down and five in from MU's (160, row + 2) so it sits in the
// well MU2 draws round the row. Card.PlusFor.
Box plusFor(float rowY) { return {158.0f, rowY + 2.0f, 16.0f, 15.0f}; }
// The wells run between the grid's two edges (`panel::kGridX` to `kWellRight`), as the bag's and
// the shelf's do: MU's 11 to 181 was three units wider on the right than the wells in the window
// beside it, and side by side the pair showed two right edges.
constexpr float kWellX = panel::kGridX, kWellW = panel::kWellRight - panel::kGridX;
Box rowField(float rowY) { return {kWellX, rowY, kWellW, 21.0f}; }
constexpr Box kSummary{12.0f, 48.0f, 160.0f, 66.0f};
constexpr Box kSummaryField{kWellX, 45.0f, kWellW, 65.0f};

// **The foot.** MU's character window stops at Energy and leaves its bottom fifth empty, which
// on leather was a texture and on this skin is a hole -- and the window beside it has a foot
// (the Zen strip), so the pair read as one finished window and one unfinished one. The level's
// own progress goes here: the figure is already in the summary, and what a player wants at a
// glance is how far through the level he is, which is the one thing a figure cannot say.
constexpr float kFootRule = panel::kFootRule;
constexpr float kFootTop = panel::kFootTop;
constexpr Box kBar{panel::kGridX, kFootTop + 12.0f, panel::kWellRight - panel::kGridX, 4.0f};

// **The type, tuned 2026-09-23.** A figure is a size and a half above its own label, which is
// what makes a stat window scannable: the eye runs down the numbers and reads a word only when
// it stops. The detail lines under a well are a label and a value like everything else on the
// card, right-ranged against the same edge the figure is, rather than MU's "Dmg: 22~33" run-on.
constexpr float kTextSize = 9.0f, kFigureSize = 11.5f, kSummarySize = 8.5f,
              kDetailSize = 8.0f;
// The summary's own labels, set as the head sets its title: small, tracked capitals.
constexpr float kSummaryLabel = 7.5f;
// The content's own margins: the wells run 11 to 181, so type sits a further seven in, and every
// value ranges against the same right edge -- the well's, less the same seven.
// Ten units between LEVEL's value and the POINTS label after it: at six the two ran together
// and "10 POINTS" read as a phrase.
constexpr float kLeft = 18.0f, kMiddle = 100.0f, kGutter = 10.0f;
constexpr float kRight = panel::kValueRight;  // where a value ends, the wells' own inset
// Where a stat's figure ends: on the same edge as every other value on the card, except while
// the diamonds are up, when it stands clear of them. With no points to spend the figures
// stopped twenty-two units short of the lines under them, and the card had two right edges.
constexpr float kFigureRight = 150.0f;
constexpr float kDetailTop = 25.0f, kDetailStep = 12.0f;

// The two weights of type, both warm: a label steps down from the heading rather than being a
// colour of its own. Godot's Darkened(a) is rgb * (1 - a).
constexpr uint32_t kHeading = sheet::ink::kFigure;
constexpr uint32_t kPlain = sheet::ink::kLabel;
constexpr uint32_t kDetailInk = sheet::ink::kQuiet;
constexpr uint32_t kSpendable = sheet::ink::kGold;

const char* titled(sim::Kin kin) {
    switch (kin) {
        case sim::Kin::DarkWizard: return "Dark Wizard";
        case sim::Kin::FairyElf: return "Fairy Elf";
        default: return "Dark Knight";
    }
}

}  // namespace

bool Card::Sheet::operator==(const Sheet& o) const {
    return who == o.who && width == o.width && height == o.height && level == o.level &&
           points == o.points && experience == o.experience && strength == o.strength &&
           agility == o.agility && vitality == o.vitality && energy == o.energy &&
           minimum == o.minimum && maximum == o.maximum && attackRate == o.attackRate &&
           defense == o.defense && defenseRate == o.defenseRate && health == o.health &&
           maxHealth == o.maxHealth && mana == o.mana && maxMana == o.maxMana &&
           pushed == o.pushed && over == o.over && closing == o.closing &&
           overClose == o.overClose;
}

void Card::open(const gfx::Interface& interface, panel::Arts* arts) {
    interface.adopt(canvas_);
    arts_ = arts;
}

bool Card::covers(float x, float y) const {
    return now_.who != nullptr &&
           Box{x_, y_, panel::kWidth * panel::scale(), panel::kHeight * panel::scale()}.has(x, y);
}

void Card::update(float width, float height, const sim::Body* hero, const Pointer& pointer,
                  int* spend, bool* close) {
    // The right-hand column, always.
    x_ = panel::columnX(width, 1);
    y_ = panel::panelY(height);
    if (hero) {
        const Box cross = panel::scaled(x_, y_, panel::frameClose());
        overClose_ = cross.has(pointer.x, pointer.y);
        // Which diamond the pointer is resting on, so it can light: the same walk the press
        // makes, made every frame and for the drawing rather than for the request.
        over_ = -1;
        if (hero->pointsInHand > 0) {
            for (const Row& row : kRows) {
                if (panel::scaled(x_, y_, plusFor(row.y)).has(pointer.x, pointer.y)) {
                    over_ = row.stat;
                }
            }
        }
        if (pointer.pressed) {
            if (cross.has(pointer.x, pointer.y)) closing_ = true;
            if (hero->pointsInHand > 0) {
                for (const Row& row : kRows) {
                    if (panel::scaled(x_, y_, plusFor(row.y)).has(pointer.x, pointer.y)) {
                        pushed_ = row.stat;
                    }
                }
            }
        }
        if (pointer.released) {
            // Only if the pointer is still on it, which is how a press is taken back.
            if (closing_ && cross.has(pointer.x, pointer.y) && close) *close = true;
            if (pushed_ >= 0 &&
                panel::scaled(x_, y_, plusFor(kRows[pushed_].y)).has(pointer.x, pointer.y) &&
                spend) {
                *spend = pushed_;
            }
            closing_ = false;
            pushed_ = -1;
        }
    }

    now_ = Sheet{};
    if (hero) {
        now_.who = hero;
        now_.width = width;
        now_.height = height;
        now_.level = hero->level;
        now_.points = hero->pointsInHand;
        now_.experience = hero->experience;
        now_.strength = hero->points.strength;
        now_.agility = hero->points.agility;
        now_.vitality = hero->points.vitality;
        now_.energy = hero->points.energy;
        now_.minimum = hero->stats.minimumDamage;
        now_.maximum = hero->stats.maximumDamage;
        now_.attackRate = int(hero->stats.attackRate);
        now_.defense = hero->stats.defense;
        now_.defenseRate = int(hero->stats.defenseRate);
        now_.health = hero->health;
        now_.maxHealth = hero->maxHealth;
        now_.mana = hero->mana;
        now_.maxMana = hero->maxMana;
        now_.pushed = pushed_;
        now_.over = over_;
        now_.closing = closing_;
        now_.overClose = overClose_;
    }
    if (now_ == drawn_ && rebuilds_ > 0) return;
    drawn_ = now_;
    rebuild();
}

void Card::rebuild() {
    ++rebuilds_;
    canvas_.clear();
    if (!now_.who || !arts_) return;
    panel::Arts& arts = *arts_;
    const float x = x_, y = y_, k = panel::scale();
    const gfx::Face& face = canvas_.face();
    const float size = kTextSize * k, brief = kSummarySize * k;

    panel::frame(canvas_, arts, x, y, "Character");
    // The wells the type sits in. MU draws none; this skin cuts one for every row and one for
    // the summary, which is what "deeper wells" means on the page the user chose.
    panel::cell(canvas_, x, y, kSummaryField, sheet::Cell::Rest);
    for (const Row& row : kRows) panel::cell(canvas_, x, y, rowField(row.y), sheet::Cell::Rest);

    // Card.Write: set from a top-left, the ascent below it. Right: the same, ranged right.
    auto write = [&](float ux, float uy, const std::string& text, uint32_t colour, float at) {
        canvas_.text(x + ux * k, y + uy * k + face.ascent(at), at, colour, text);
    };
    auto right = [&](float ux, float uy, float uw, const std::string& text, uint32_t colour,
                     float at) {
        canvas_.text(x + ux * k, y + uy * k + face.ascent(at), at, colour, text,
                     gfx::Align::Right, uw * k);
    };

    // The class, across the whole table, in the head's own tracked capitals a size down: it is
    // a heading and not a value, and it was the only line on the card set like a value.
    sheet::kicker(canvas_, x + kLeft * k,
                  y + (kSummary.y + 6.0f) * k + face.ascent(9.0f * k), 9.0f * k,
                  sheet::shouted(titled(now_.who->kin)), sheet::ink::kTitle, 0.12f);
    // Level and points on one line, experience beneath: MU's stack at its own places.
    const float pairY = kSummary.y + 24.0f;
    sheet::kicker(canvas_, x + kLeft * k, y + pairY * k + face.ascent(kSummaryLabel * k),
                  kSummaryLabel * k, "LEVEL");
    right(kLeft, pairY, kMiddle - kGutter - kLeft, std::to_string(now_.level), kHeading, brief);
    sheet::kicker(canvas_, x + kMiddle * k, y + pairY * k + face.ascent(kSummaryLabel * k),
                  kSummaryLabel * k, "POINTS");
    right(kMiddle, pairY, kRight - kMiddle, std::to_string(now_.points),
          now_.points > 0 ? kSpendable : kPlain, brief);
    sheet::kicker(canvas_, x + kLeft * k,
                  y + (kSummary.y + 44.0f) * k + face.ascent(kSummaryLabel * k),
                  kSummaryLabel * k, "EXPERIENCE");
    right(kLeft, kSummary.y + 44.0f, kRight - kLeft,
          panel::commas((long long)now_.experience), kPlain, brief);

    const int values[4] = {now_.strength, now_.agility, now_.vitality, now_.energy};
    for (const Row& row : kRows) {
        // The name against the window's left edge and the figure against the plus, both
        // centred down the well.
        const Box well = panel::scaled(x, y, rowField(row.y));
        // The label centred on the FIGURE's line rather than on its own: two sizes on one row
        // sit on one baseline, which is the difference between a row and two rows overlapping.
        const float figureSize = kFigureSize * k;
        const float baseline = panel::centredBaseline(face, well, figureSize);
        canvas_.text(x + kLeft * k, baseline, size, kPlain, row.label);
        const float figureRight = now_.points > 0 ? kFigureRight : kRight;
        canvas_.text(x + kLeft * k, baseline, figureSize, kHeading,
                     std::to_string(values[row.stat]), gfx::Align::Right,
                     (figureRight - kLeft) * k);

        // What the attribute buys, in the gap under its well: MU's own lines and strings. MU2
        // adds "Attack speed" under agility; the sim has no attack speed stat -- MU paces a swing
        // by its clip -- so that line is not printed here.
        std::string labels[2], lines[2];
        int count = 0;
        switch (row.stat) {
            case 0:
                labels[count] = "Damage";
                lines[count++] = std::to_string(now_.minimum) + " ~ " +
                                 std::to_string(now_.maximum);
                labels[count] = "Attack rate";
                lines[count++] = std::to_string(now_.attackRate);
                break;
            case 1:
                labels[count] = "Defence";
                lines[count++] = std::to_string(now_.defense);
                labels[count] = "Defence rate";
                lines[count++] = std::to_string(now_.defenseRate);
                break;
            case 2:
                labels[count] = "Life";
                lines[count++] = std::to_string(now_.health) + " / " +
                                 std::to_string(now_.maxHealth);
                break;
            default:
                labels[count] = "Mana";
                lines[count++] = std::to_string(now_.mana) + " / " +
                                 std::to_string(now_.maxMana);
                break;
        }
        const float detail = kDetailSize * k;
        for (int i = 0; i < count; ++i) {
            const float uy = row.y + kDetailTop + float(i) * kDetailStep;
            canvas_.text(x + kLeft * k, y + uy * k + face.ascent(detail), detail, kDetailInk,
                         labels[i]);
            canvas_.text(x + kLeft * k, y + uy * k + face.ascent(detail), detail, kPlain,
                         lines[i], gfx::Align::Right, (kRight - kLeft) * k);
        }

        // The plus only where there is something to spend: MU hides it rather than greying it.
        // `bag_plus`'s two-state button is gone with the rest of the art; the skin's own gold
        // diamond with a plus cut out of it stands in its place, at MU's own rectangle.
        if (now_.points <= 0) continue;
        sheet::diamond(canvas_, panel::scaled(x, y, plusFor(row.y)), now_.over == row.stat,
                       now_.pushed == row.stat);
    }

    // The foot: a band of light out of the bottom edge, the rule, the kicker, the share of the
    // level he has, and the percent -- the same foot the bag carries its Zen in.
    sheet::band(canvas_, panel::scaled(x, y, {0.0f, kFootRule, panel::kWidth,
                                              panel::kHeight - kFootRule}),
                false, panel::kRadius * k);
    sheet::rule(canvas_, x + panel::kEdge * k, y + kFootRule * k,
                (panel::kWidth - panel::kEdge * 2.0f) * k, std::max(1.0f, k * 0.5f));
    const uint64_t at = sim::neededExperience(now_.level);
    const uint64_t next = sim::neededExperience(now_.level + 1);
    const float share =
        next > at ? float(double(now_.experience > at ? now_.experience - at : 0) /
                          double(next - at))
                  : 1.0f;
    const float kicker = kDetailSize * k;
    sheet::kicker(canvas_, x + kLeft * k, y + kFootTop * k + face.ascent(kicker), kicker,
                  "EXPERIENCE");
    canvas_.text(x + kLeft * k, y + kFootTop * k + face.ascent(kicker), kicker, kDetailInk,
                 std::to_string(int(share * 100.0f + 0.5f)) + "%", gfx::Align::Right,
                 (kRight - kLeft) * k);
    sheet::bar(canvas_, panel::scaled(x, y, kBar), share, kSpendable, std::max(1.0f, k * 0.5f));

    panel::close(canvas_, x, y, now_.overClose, now_.closing);
}

}  // namespace mu::game
