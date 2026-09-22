#include "game/ui/card.h"

#include <string>

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
constexpr Row kRows[4] = {
    {0, 120.0f, "Strength"}, {1, 175.0f, "Agility"}, {2, 240.0f, "Vitality"}, {3, 295.0f, "Energy"}};

// The plus beside a row, moved two down and five in from MU's (160, row + 2) so it sits in the
// well MU2 draws round the row. Card.PlusFor.
Box plusFor(float rowY) { return {160.0f, rowY + 2.0f, 16.0f, 15.0f}; }
Box rowField(float rowY) { return {11.0f, rowY, 170.0f, 21.0f}; }
constexpr Box kSummary{12.0f, 48.0f, 160.0f, 66.0f};
constexpr Box kSummaryField{11.0f, 45.0f, 170.0f, 65.0f};

constexpr float kTextSize = 10.0f, kSummarySize = 9.0f;
constexpr float kLeft = 18.0f, kMiddle = 100.0f, kFigureRight = 155.0f, kGutter = 6.0f;
constexpr float kDetailTop = 24.0f, kDetailStep = 13.0f;

// The two weights of type, both warm: a label steps down from the heading rather than being a
// colour of its own. Godot's Darkened(a) is rgb * (1 - a).
constexpr uint32_t kHeading = panel::kLettering;
constexpr uint32_t kPlain = gfx::rgba(0.90f * 0.72f, 0.86f * 0.72f, 0.76f * 0.72f);
constexpr uint32_t kDetailInk = gfx::rgba(0.90f * 0.58f, 0.86f * 0.58f, 0.76f * 0.58f);
constexpr uint32_t kSpendable = gfx::rgba(1.0f, 0.8f, 0.1f);

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
           pushed == o.pushed && closing == o.closing;
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
        now_.closing = closing_;
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
    // The wells the type sits in. MU draws none; MuDream's leather needs them, and the sheet
    // ships the well for exactly this.
    panel::field(canvas_, arts, x, y, kSummaryField);
    for (const Row& row : kRows) panel::field(canvas_, arts, x, y, rowField(row.y));

    // Card.Write: set from a top-left, the ascent below it. Right: the same, ranged right.
    auto write = [&](float ux, float uy, const std::string& text, uint32_t colour, float at) {
        canvas_.text(x + ux * k, y + uy * k + face.ascent(at), at, colour, text);
    };
    auto right = [&](float ux, float uy, float uw, const std::string& text, uint32_t colour,
                     float at) {
        canvas_.text(x + ux * k, y + uy * k + face.ascent(at), at, colour, text,
                     gfx::Align::Right, uw * k);
    };

    // The class, across the whole table.
    write(kLeft, kSummary.y + 6.0f, titled(now_.who->kin), kHeading, size);
    // Level and points on one line, experience beneath: MU's stack at its own places.
    const float pairY = kSummary.y + 24.0f;
    write(kLeft, pairY, "Level", kPlain, brief);
    right(kLeft, pairY, kMiddle - kGutter - kLeft, std::to_string(now_.level), kHeading, brief);
    write(kMiddle, pairY, "Points", kPlain, brief);
    right(kMiddle, pairY, kSummary.right() - 6.0f - kMiddle, std::to_string(now_.points),
          now_.points > 0 ? kSpendable : kPlain, brief);
    write(kLeft, kSummary.y + 44.0f, "Experience", kPlain, brief);
    right(kLeft, kSummary.y + 44.0f, kSummary.right() - 6.0f - kLeft,
          panel::commas((long long)now_.experience), kPlain, brief);

    const int values[4] = {now_.strength, now_.agility, now_.vitality, now_.energy};
    const gfx::Art& plusArt = arts.get("bag_plus");
    for (const Row& row : kRows) {
        // The name against the window's left edge and the figure against the plus, both
        // centred down the well.
        const Box well = panel::scaled(x, y, rowField(row.y));
        const float baseline = panel::centredBaseline(face, well, size);
        canvas_.text(x + kLeft * k, baseline, size, kPlain, row.label);
        canvas_.text(x + kLeft * k, baseline, size, kHeading, std::to_string(values[row.stat]),
                     gfx::Align::Right, (kFigureRight - kLeft) * k);

        // What the attribute buys, in the gap under its well: MU's own lines and strings. MU2
        // adds "Attack speed" under agility; the sim has no attack speed stat -- MU paces a swing
        // by its clip -- so that line is not printed here.
        std::string lines[2];
        int count = 0;
        switch (row.stat) {
            case 0:
                lines[count++] = "Dmg: " + std::to_string(now_.minimum) + "~" +
                                 std::to_string(now_.maximum);
                lines[count++] = "Attack rate: " + std::to_string(now_.attackRate);
                break;
            case 1:
                lines[count++] = "Defense: " + std::to_string(now_.defense);
                lines[count++] = "Defense rate: " + std::to_string(now_.defenseRate);
                break;
            case 2:
                lines[count++] = "HP: " + std::to_string(now_.health) + " / " +
                                 std::to_string(now_.maxHealth);
                break;
            default:
                lines[count++] = "Mana: " + std::to_string(now_.mana) + " / " +
                                 std::to_string(now_.maxMana);
                break;
        }
        for (int i = 0; i < count; ++i) {
            write(kLeft, row.y + kDetailTop + float(i) * kDetailStep, lines[i], kDetailInk, brief);
        }

        // The plus only where there is something to spend: MU hides it rather than greying it.
        if (now_.points <= 0) continue;
        const Box plus = panel::scaled(x, y, plusFor(row.y));
        if (plusArt.valid()) {
            canvas_.region(plusArt, plus, panel::buttonState(plusArt, now_.pushed == row.stat));
        } else {
            canvas_.rect(plus, gfx::rgba(0.16f, 0.14f, 0.10f, 0.9f));
            canvas_.outline(plus, 1.0f, gfx::rgba(0.80f, 0.70f, 0.45f, 0.95f));
        }
    }

    panel::close(canvas_, arts, x, y, now_.closing);
}

}  // namespace mu::game
