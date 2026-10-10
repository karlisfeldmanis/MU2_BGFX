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

// In tip::unit() at 1080 lines: the head, MU's line, the column heads under a rule, a row a
// player (ten at most: the top nine and his own), and the foot with the clock and Claim.
constexpr float kWide = 600.0f;
constexpr float kInset = 22.0f;
constexpr float kLineTop = style::kHead + 14.0f, kLineTall = 24.0f;
constexpr float kHeadsTop = kLineTop + kLineTall + 12.0f, kHeadsTall = 22.0f;
constexpr float kRowsTop = kHeadsTop + kHeadsTall + 6.0f, kRowTall = 30.0f;
constexpr float kFootGap = 18.0f;
constexpr float kButtonW = 130.0f;
constexpr float kFootPad = 20.0f;
constexpr size_t kShownRows = 10;

// The columns: the rank and the name from the left, the three figures ranged to their right edges.
constexpr float kRankX = kInset + 10.0f;
constexpr float kNameX = kInset + 62.0f;
constexpr float kPointsRight = 340.0f;
constexpr float kExpRight = 470.0f;
constexpr float kZenRight = kWide - kInset - 10.0f;

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

    Drawn now{square_, secondsLeft_, over_, pressing_, rows_, x_, y_, u, lift_[0], lift_[1]};
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

    // MU's line (text 648), without the name it opens with: the row marked below is his.
    const Box line = placed(x, y, {0.0f, kLineTop, kWide, kLineTall}, u);
    controls::label(canvas_, line.x + kInset * u, controls::middle(line.y, line.h, style::kBodySize * u),
                    style::kBodySize * u, style::kBone, "Your bravery is proven in Devil Square.");

    // The column heads, MU's five, as kickers under a rule.
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
    rangedCaps(kExpRight, "Experience");
    rangedCaps(kZenRight, "Zen");
    controls::rule(canvas_, x + kInset * u, heads.y + heads.h + 2.0f * u, (kWide - kInset * 2.0f) * u, u);

    // A row a player. His own is marked with a faint wash of the accent and set in the bright
    // bone; the Zen in gold, as Zen is everywhere (style.h: gold is loot).
    const float size = style::kBodySize * u;
    for (size_t i = 0; i < rows_.size(); ++i) {
        const Row& one = rows_[i];
        const Box row = placed(x, y, {kInset, kRowsTop + kRowTall * float(i), kWide - kInset * 2.0f, kRowTall}, u);
        if (one.mine) {
            canvas_.rect(row, gfx::rgba(0.761f, 0.212f, 0.169f, 0.16f));
        } else if (i % 2 == 1) {
            canvas_.rect(row, gfx::rgba(1.0f, 1.0f, 1.0f, 0.025f));
        }
        const float base = controls::middle(row.y, row.h, size);
        const uint32_t ink = one.mine ? style::kBoneHi : style::kBone;
        controls::label(canvas_, x + kRankX * u, base, size, one.rank == 1 ? style::kBoneHi : style::kAshInk,
                        std::to_string(one.rank));
        controls::label(canvas_, x + kNameX * u, base, size, ink, one.name);
        controls::ranged(canvas_, x + kPointsRight * u, base, size, ink, panel::commas(one.points));
        controls::ranged(canvas_, x + kExpRight * u, base, size, ink, panel::commas(one.experience));
        controls::ranged(canvas_, x + kZenRight * u, base, size, panel::kRefined, panel::commas(one.zen));
    }

    // The foot: the clock home, and Claim.
    const Box claim = placed(x, y, buttonBox(0, tall_), u);
    controls::label(canvas_, x + kInset * u, controls::middle(claim.y, claim.h, size), size,
                    style::kAshInk, "Home to Noria in " + clock(secondsLeft_));
    controls::button(canvas_, claim, "Claim", controls::Kind::Primary, state(0), u);
}

}  // namespace mu::game
