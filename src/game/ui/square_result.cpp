#include "game/ui/square_result.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "game/ui/controls.h"
#include "game/ui/panel.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"

namespace mu::game {
namespace {

using gfx::Box;

// In tip::unit() at 1080 lines, the design page's C: the head; "Your rank" over the rank in
// Cinzel and MU's line; the well of what he earned; the column heads under a rule and a row a
// player (ten at most: the top nine and his own), small; and the foot with the clock and Claim.
constexpr float kWide = 580.0f;
constexpr float kInset = 22.0f;
constexpr float kKickerBase = style::kHead + 34.0f;
constexpr float kRankSize = 64.0f, kRankBase = kKickerBase + 70.0f;
constexpr float kLineBase = kRankBase + 30.0f;
constexpr float kWellTop = kLineBase + 18.0f, kWellTall = 66.0f;
constexpr float kFigureSize = 22.0f;
constexpr float kHeadsTop = kWellTop + kWellTall + 16.0f, kHeadsTall = 20.0f;
constexpr float kRowsTop = kHeadsTop + kHeadsTall + 6.0f, kRowTall = 24.0f;
constexpr float kRowSize = 14.0f;
constexpr float kFootGap = 18.0f;
constexpr float kButtonW = 130.0f;
constexpr float kFootPad = 20.0f;
constexpr size_t kShownRows = 10;

// The table's columns: the rank and the name from the left, the three figures ranged right.
constexpr float kRankX = kInset + 10.0f;
constexpr float kNameX = kInset + 50.0f;
constexpr float kPointsRight = 340.0f;
constexpr float kExpRight = 450.0f;
constexpr float kZenRight = kWide - kInset - 10.0f;
// The table's Zen a muted gold, so the well's own figure is the one that shines.
constexpr uint32_t kQuietGold = gfx::rgba(0.788f, 0.651f, 0.251f);

std::string ordinal(int n) {
    const int tens = n % 100, ones = n % 10;
    const char* tail = tens >= 11 && tens <= 13 ? "th" : ones == 1 ? "st" : ones == 2 ? "nd" : ones == 3 ? "rd" : "th";
    return std::to_string(n) + tail;
}

float tallFor(size_t rows) {
    return kRowsTop + kRowTall * float(std::max<size_t>(rows, 1)) + kFootGap + style::kButtonM + kFootPad;
}

Box buttonBox(int which, float tall) {
    if (which == 1) {
        const float side = style::kSmallSquare;
        return {kWide - style::kPad - side, (style::kHead - side) * 0.5f, side, side};
    }
    return {kWide - kInset - kButtonW, tall - kFootPad - style::kButtonM, kButtonW, style::kButtonM};
}

Box placed(float x, float y, const Box& b, float u) {
    return {std::round(x + b.x * u), std::round(y + b.y * u), std::round(b.w * u), std::round(b.h * u)};
}

std::string clock(int seconds) {
    char text[16];
    std::snprintf(text, sizeof text, "%d:%02d", seconds / 60, seconds % 60);
    return text;
}

}  // namespace

void SquareResult::close() {
    canvas_.clear();
    up_ = false;
    built_ = false;
    rows_.clear();
}

int SquareResult::buttonAt(float ux, float uy) const {
    for (int which = 0; which < 2; ++which) {
        if (buttonBox(which, tall_).has(ux, uy)) return which;
    }
    return -1;
}

bool SquareResult::covers(float x, float y) const {
    if (!up_) return false;
    const float u = tip::unit();
    return Box{x_, y_, kWide * u, tall_ * u}.has(x, y);
}

bool SquareResult::update(float seconds, bool shown, int square, const std::vector<Row>& rows,
                          int secondsLeft, const Pointer& pointer, float width, float height) {
    if (!shown || dismissed_ || rows.empty()) {
        if (up_) close();
        return false;
    }
    up_ = true;
    square_ = square;
    secondsLeft_ = secondsLeft;
    // The top nine, and his own after them when he is lower: WebZen's ten (DevilSquareGround.cpp:
    // 364-418); with ten or fewer in the square, all of them.
    if (rows.size() <= kShownRows) {
        rows_ = rows;
    } else {
        rows_.assign(rows.begin(), rows.begin() + (kShownRows - 1));
        const auto mine = std::find_if(rows.begin(), rows.end(), [](const Row& r) { return r.mine; });
        const bool lower = mine != rows.end() && size_t(mine - rows.begin()) >= kShownRows - 1;
        rows_.push_back(lower ? *mine : rows[kShownRows - 1]);
    }
    const auto own = std::find_if(rows.begin(), rows.end(), [](const Row& r) { return r.mine; });
    mine_ = own != rows.end() ? *own : Row{};
    count_ = int(rows.size());
    tall_ = tallFor(rows_.size());

    const float u = tip::unit();
    x_ = std::round((width - kWide * u) * 0.5f);
    y_ = std::round(height * 0.42f - tall_ * u * 0.5f);
    const float ux = (pointer.x - x_) / u, uy = (pointer.y - y_) / u;
    over_ = buttonAt(ux, uy);
    for (int which = 0; which < 2; ++which) {
        const float step = seconds / style::kHoverSeconds;
        lift_[which] = over_ == which ? std::min(1.0f, lift_[which] + step) : std::max(0.0f, lift_[which] - step);
    }
    if (pointer.pressed) pressing_ = over_;
    bool claim = false;
    if (pointer.released) {
        if (pressing_ >= 0 && pressing_ == over_) {
            if (pressing_ == 0) claim = true;
            else dismissed_ = true;
        }
        pressing_ = -1;
    }
    if (dismissed_) {
        close();
        return false;
    }

    Drawn now{square_, secondsLeft_, over_, pressing_, count_, rows_, x_, y_, u, lift_[0], lift_[1]};
    if (!built_ || !(now == drawn_)) {
        drawn_ = std::move(now);
        built_ = true;
        rebuild();
    }
    return claim;
}

void SquareResult::rebuild() {
    canvas_.clear();
    const float x = x_, y = y_, u = tip::unit();
    controls::frame(canvas_, placed(x, y, {0.0f, 0.0f, kWide, tall_}, u), u,
                    "Devil Square " + std::to_string(square_));
    const auto state = [&](int which) {
        const float t = lift_[which] * lift_[which] * (3.0f - 2.0f * lift_[which]);
        return controls::State{t, pressing_ == which && over_ == which, false};
    };
    controls::square(canvas_, placed(x, y, buttonBox(1, tall_), u), controls::Glyph::Close, state(1), u);

    const float mid = x + kWide * 0.5f * u;
    const auto centred = [&](float baseline, float size, uint32_t ink, const std::string& text) {
        controls::label(canvas_, std::round(mid - controls::labelWidth(size, text) * 0.5f), baseline, size,
                        ink, text);
    };
    const auto centredCaps = [&](float cx, float baseline, const std::string& text) {
        const float kick = style::kKickerSize * u;
        controls::caps(canvas_, std::round(cx - controls::capsWidth(kick, text) * 0.5f), baseline, kick,
                       style::kAshInk, text);
    };

    // The headline: his rank in Cinzel and "of" how many, centred together, under its kicker.
    centredCaps(mid, y + kKickerBase * u, "Your rank");
    {
        const std::string rank = ordinal(std::max(1, mine_.rank));
        const std::string of = "of " + std::to_string(count_);
        const float big = kRankSize * u, small = 18.0f * u, gap = 10.0f * u;
        const gfx::Face* gothic = panel::titleFace();
        const float rankWide = gothic ? gothic->measure(big, rank) : controls::labelWidth(big, rank);
        const float left = std::round(mid - (rankWide + gap + controls::labelWidth(small, of)) * 0.5f);
        const float base = y + kRankBase * u;
        if (gothic) {
            canvas_.lettered(*gothic, panel::titleTexture(), left + 1.0f, base + 2.0f, big, 0.0f, style::kDrop, rank);
            canvas_.lettered(*gothic, panel::titleTexture(), left, base, big, 0.0f, style::kBoneHi, rank);
        } else {
            controls::label(canvas_, left, base, big, style::kBoneHi, rank);
        }
        controls::label(canvas_, left + rankWide + gap, base, small, style::kAshInk, of);
    }
    // MU's line (text 648), without the name it opens with: the headline is his.
    centred(y + kLineBase * u, style::kBodySize * u, style::kBone, "Your bravery is proven in Devil Square.");

    // The well: what he earned, three figures under their kickers, parted by dark rules.
    const Box well = placed(x, y, {kInset, kWellTop, kWide - kInset * 2.0f, kWellTall}, u);
    controls::well(canvas_, well, u);
    const float third = well.w / 3.0f;
    for (int i = 1; i < 3; ++i) {
        canvas_.rect({std::round(well.x + third * float(i)), well.y + 8.0f * u, 1.0f, well.h - 16.0f * u},
                     style::kIronDk);
    }
    const char* kicks[3] = {"Points", "Experience", "Zen"};
    const std::string figures[3] = {panel::commas(mine_.points), "+" + panel::commas(mine_.experience),
                                    "+" + panel::commas(mine_.zen)};
    for (int i = 0; i < 3; ++i) {
        const float cx = well.x + third * (float(i) + 0.5f);
        centredCaps(cx, well.y + 24.0f * u, kicks[i]);
        const float size = kFigureSize * u;
        controls::label(canvas_, std::round(cx - controls::labelWidth(size, figures[i]) * 0.5f),
                        well.y + 52.0f * u, size, i == 2 ? panel::kRefined : style::kBoneHi, figures[i]);
    }

    // The square's ranks, small: the column heads under a rule, then a row a player, his marked.
    const Box heads = placed(x, y, {0.0f, kHeadsTop, kWide, kHeadsTall}, u);
    const float headBase = controls::middle(heads.y, heads.h, style::kKickerSize * u);
    const float kick = style::kKickerSize * u;
    controls::caps(canvas_, x + kRankX * u, headBase, kick, style::kAshInk, "Rank");
    controls::caps(canvas_, x + kNameX * u, headBase, kick, style::kAshInk, "Name");
    const auto rangedCaps = [&](float right, const char* text) {
        controls::caps(canvas_, x + right * u - controls::capsWidth(kick, text), headBase, kick,
                       style::kAshInk, text);
    };
    rangedCaps(kPointsRight, "Points");
    rangedCaps(kExpRight, "Exp");
    rangedCaps(kZenRight, "Zen");
    controls::rule(canvas_, x + kInset * u, heads.y + heads.h + 2.0f * u, (kWide - kInset * 2.0f) * u, u);
    const float size = kRowSize * u;
    for (size_t i = 0; i < rows_.size(); ++i) {
        const Row& one = rows_[i];
        const Box row = placed(x, y, {kInset, kRowsTop + kRowTall * float(i), kWide - kInset * 2.0f, kRowTall}, u);
        if (one.mine) {
            canvas_.rect(row, gfx::rgba(0.761f, 0.212f, 0.169f, 0.16f));
        } else if (i % 2 == 1) {
            canvas_.rect(row, gfx::rgba(1.0f, 1.0f, 1.0f, 0.025f));
        }
        const float base = controls::middle(row.y, row.h, size);
        const uint32_t ink = one.mine ? style::kBoneHi : style::kBone2;
        controls::label(canvas_, x + kRankX * u, base, size, style::kAshInk, std::to_string(one.rank));
        controls::label(canvas_, x + kNameX * u, base, size, ink, one.name);
        controls::ranged(canvas_, x + kPointsRight * u, base, size, ink, panel::commas(one.points));
        controls::ranged(canvas_, x + kExpRight * u, base, size, ink, panel::commas(one.experience));
        controls::ranged(canvas_, x + kZenRight * u, base, size, kQuietGold, panel::commas(one.zen));
    }

    // The foot: the clock home, and Claim.
    const Box claim = placed(x, y, buttonBox(0, tall_), u);
    controls::label(canvas_, x + kInset * u, controls::middle(claim.y, claim.h, style::kBodySize * u),
                    style::kBodySize * u, style::kAshInk, "Home to Noria in " + clock(secondsLeft_));
    controls::button(canvas_, claim, "Claim", controls::Kind::Primary, state(0), u);
}

}  // namespace mu::game
