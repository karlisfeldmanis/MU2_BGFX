#include "game/ui/hud.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "sim/rules.h"

namespace mu::game {
namespace {

using gfx::Box;

// ---- the plate, in its own pixels (Hud.cs) ----------------------------------------------------

// Interface units per plate pixel: the one size. At 0.34 the 1224-wide plate is 416 of MU's 640;
// MuDream's frame takes 490 (0.4), which the user found a little large on 2026-09-21.
constexpr float kUnit = 0.34f;
constexpr float kPlateW = 1224.0f, kPlateH = 180.0f;

// The row: six skill boxes 61 wide on a 65 pitch from x 290, y 96 to 157; five item boxes 48
// wide on a 51 pitch from x 681, y 111 to 159. Measured off the plate's own rims in MU2.
constexpr int kSlots = 11;
constexpr int kFirstQuick = 6;  // the first item box
constexpr float kSkillsX = 290.0f, kSkillsY = 96.0f, kSkillPitch = 65.0f;
constexpr float kSkillW = 61.0f, kSkillH = 62.0f;
constexpr float kQuickX = 681.0f, kQuickY = 111.0f, kQuickPitch = 51.0f;
constexpr float kQuickW = 48.0f, kQuickH = 49.0f;

// The two diamonds, at the ring's size rather than the hole's: the gem fills its setting to the
// metal and is drawn under the plate, so the ring's inner edge frames it.
constexpr Box kLifeHole{124.0f, 9.0f, 161.0f, 163.0f};
constexpr Box kManaHole{939.0f, 8.0f, 162.0f, 164.0f};

// The shield's bar, under the plate on the rail above the boxes. With the ability gauge not
// drawn (nothing in 0.75 spends AG) it takes the whole rail, to the ability window's right end
// at 944: the sheet's right-hand 299 of 346 stretched across, the plate's notches and centre
// ornament cutting it into cells. Hud.cs ShieldBar, ShieldBarFrom, ShieldBarSpan.
constexpr Box kShieldBar{276.0f, 72.0f, 944.0f - 276.0f, 11.0f};
constexpr float kShieldFrom = 346.0f - 299.0f, kShieldSpan = 299.0f;
constexpr float kBarReadingTall = 15.0f, kShieldReadingIn = 34.0f;

// The level's rail, under the plate and wider than it, centred on it.
constexpr Box kLevelTrack{(kPlateW - 1448.0f) / 2.0f, 180.0f, 1448.0f, 10.0f};
constexpr Box kLevelFill{(kPlateW - 1408.0f) / 2.0f, 181.0f, 1408.0f, 8.0f};

// The side buttons: a 60 disc, 8 apart, standing 26 off the rings' outer points at 115 and 1109.
constexpr float kButtonSide = 60.0f, kButtonGap = 8.0f, kButtonTuck = 26.0f;
constexpr float kLeftRing = 115.0f, kRightRing = 1109.0f;

struct ButtonRow {
    const char* art;
    bool left;
    bool live;
};
// Menu and chat left of the life gem, inventory and character right of the mana gem. The menu
// is MU2's live one and is drawn dim here: there is no menu window in this sprint.
constexpr ButtonRow kButtons[4] = {
    {"hud_button_menu", true, false},
    {"hud_button_chat", true, false},
    {"hud_button_inventory", false, true},
    {"hud_button_character", false, true},
};

// The gem sheets: six frames a row, ten rows, 152 square, turning at 24 a second.
constexpr int kGemsAcross = 6, kGemsDown = 10;
constexpr float kGemFrame = 152.0f, kTurn = 24.0f;
constexpr int kFacets = 8;

// The hairline chases the experience: a share of the gap closed each second, and a floor.
constexpr float kLevelEase = 6.0f, kLevelSlideLeast = 0.1f;
constexpr int kTenths = 10;

// Type, in plate pixels like everything else here.
constexpr float kReadingTall = 20.0f;
constexpr float kTipTall = 15.0f;

// The painted key labels: a dark cell on rows 164 to 177 under every box, the figure centred on
// the box. Measured off hud_base.png for this sprint; (15, 16, 17) is the cell's own dark.
constexpr float kLabelTop = 164.0f, kLabelTall = 14.0f, kLabelWide = 20.0f;
constexpr float kLabelBaseline = 176.0f, kLabelSize = 15.0f;
// What each box's key is now, left to right. Skills on Q W E R T, potions on 1 to 5, and the
// box in hand prints nothing -- the mouse under the gold box stays, since that box IS the
// button's.
const char* const kKeys[kSlots] = {"Q", "W", "E", "R", "T", nullptr, "1", "2", "3", "4", "5"};

constexpr uint32_t kSocketBack = gfx::rgba(0.05f, 0.055f, 0.07f, 0.9f);
constexpr uint32_t kInk = gfx::rgba(1.0f, 250.0f / 255.0f, 240.0f / 255.0f);
constexpr uint32_t kInkShadow = gfx::rgba(0.0f, 0.0f, 0.0f, 0.7f);
constexpr uint32_t kDeadIcon = gfx::rgba(0.55f, 0.55f, 0.55f, 0.6f);
constexpr uint32_t kTipColour = gfx::rgba(238.0f / 255.0f, 230.0f / 255.0f, 214.0f / 255.0f);
constexpr uint32_t kTipNameColour = gfx::rgba(236.0f / 255.0f, 198.0f / 255.0f, 92.0f / 255.0f);
constexpr uint32_t kLabelCell = gfx::rgba(15.0f / 255.0f, 16.0f / 255.0f, 17.0f / 255.0f);
constexpr uint32_t kSkillKey = gfx::rgba(141.0f / 255.0f, 127.0f / 255.0f, 125.0f / 255.0f);
constexpr uint32_t kQuickKey = gfx::rgba(206.0f / 255.0f, 186.0f / 255.0f, 73.0f / 255.0f);

// Where the plate's corner sits in MU's 640x480: centred, its rail on the foot of the screen.
constexpr float kPlateAtX = (640.0f - kPlateW * kUnit) / 2.0f;
constexpr float kPlateAtY = 480.0f - (kLevelTrack.y + kLevelTrack.h) * kUnit;

Box plate(const panel::Screen& s, const Box& px) {
    return s.of({kPlateAtX + px.x * kUnit, kPlateAtY + px.y * kUnit, px.w * kUnit, px.h * kUnit});
}

Box boxPx(int slot) {
    return slot < kFirstQuick
               ? Box{kSkillsX + float(slot) * kSkillPitch, kSkillsY, kSkillW, kSkillH}
               : Box{kQuickX + float(slot - kFirstQuick) * kQuickPitch, kQuickY, kQuickW,
                     kQuickH};
}

Box buttonPx(int which) {
    const bool left = kButtons[which].left;
    int rank = 0, pair = 0;
    for (int i = 0; i < 4; ++i) {
        if (kButtons[i].left == left) {
            if (i < which) ++rank;
            ++pair;
        }
    }
    const float y = kSkillsY + (kSkillH - kButtonSide) / 2.0f;
    const float x = left ? kLeftRing - kButtonTuck - float(pair - rank) * kButtonSide -
                               float(pair - rank - 1) * kButtonGap
                         : kRightRing + kButtonTuck + float(rank) * (kButtonSide + kButtonGap);
    return {x, y, kButtonSide, kButtonSide};
}

float fraction(long long part, long long whole) {
    return whole > 0 ? std::clamp(float(part) / float(whole), 0.0f, 1.0f) : 0.0f;
}

// The part of a diamond between two waterlines, textured with a cell of its sheet: walked down
// the right side and back up the left, so the points come out in order for a fan. The polygon
// is the mask and the cell is the crop, and neither needs a shader. Hud.Diamond.
void diamond(gfx::Canvas& canvas, const gfx::Art* sheet, const Box& hole, const Box& cell,
             float from, float to, uint32_t colour) {
    from = std::clamp(from, 0.0f, 1.0f);
    to = std::clamp(to, 0.0f, 1.0f);
    if (to <= from) return;
    constexpr int kPoints = (kFacets + 1) * 2;
    float xy[kPoints * 2], uv[kPoints * 2];
    const float mx = hole.midX();
    for (int i = 0; i <= kFacets; ++i) {
        const float down = from + (to - from) * float(i) / float(kFacets);
        const float y = hole.y + hole.h * down;
        // A diamond is as wide as it is far from its nearer point.
        const float reach = (1.0f - std::fabs(2.0f * down - 1.0f)) * hole.w * 0.5f;
        xy[i * 2] = mx + reach;
        xy[i * 2 + 1] = y;
        const int j = kPoints - 1 - i;
        xy[j * 2] = mx - reach;
        xy[j * 2 + 1] = y;
    }
    for (int i = 0; i < kPoints; ++i) {
        const float wx = (xy[i * 2] - hole.x) / hole.w, wy = (xy[i * 2 + 1] - hole.y) / hole.h;
        if (sheet && sheet->valid()) {
            uv[i * 2] = (cell.x + wx * cell.w) / sheet->width;
            uv[i * 2 + 1] = (cell.y + wy * cell.h) / sheet->height;
        } else {
            uv[i * 2] = uv[i * 2 + 1] = 0.0f;
        }
    }
    canvas.polygon(sheet, xy, uv, kPoints, colour);
}

}  // namespace

bool Hud::Face::operator==(const Face& o) const {
    return width == o.width && height == o.height && health == o.health &&
           maxHealth == o.maxHealth && mana == o.mana && maxMana == o.maxMana &&
           shield == o.shield && maxShield == o.maxShield &&
           level == o.level && gem == o.gem && slid == o.slid && inventory == o.inventory &&
           character == o.character && hovered == o.hovered && tip == o.tip &&
           (!tip || (pointerX == o.pointerX && pointerY == o.pointerY)) &&
           std::equal(quick, quick + kQuickKeys, o.quick) && picture == o.picture &&
           std::equal(skill, skill + kSkillKeys, o.skill);
}

int Hud::quickAt(float x, float y) const {
    for (int i = 0; i < kQuickKeys; ++i) {
        if (plate(screen_, boxPx(kFirstQuick + i)).has(x, y)) return i;
    }
    return -1;
}

void Hud::open(const gfx::Interface& interface, panel::Arts* arts) {
    interface.adopt(canvas_);
    interface.adopt(tip_);
    arts_ = arts;
}

void Hud::follow(const sim::Body* hero) {
    // A different character starts the hairline where he already is: being taken over is not
    // an event in his life.
    if (hero != hero_) {
        hero_ = hero;
        slid_ = hero ? progress() : 0.0f;
        drawnLevel_ = hero ? hero->level : 0;
    }
}

float Hud::progress() const {
    if (!hero_) return 0.0f;
    const uint64_t at = sim::neededExperience(hero_->level);
    const uint64_t next = sim::neededExperience(hero_->level + 1);
    if (next <= at) return 1.0f;
    const uint64_t into = hero_->experience > at ? hero_->experience - at : 0;
    return std::clamp(float(double(into) / double(next - at)), 0.0f, 1.0f);
}

int Hud::segment() const {
    return std::min(int(progress() * float(kTenths)), kTenths - 1);
}

void Hud::slide(float seconds) {
    if (!hero_) return;
    // A level-up has to FINISH the level it was on before it starts the next, or a kill that
    // levels him reads as the bar emptying. Hud.Slide.
    const bool behind = drawnLevel_ < hero_->level;
    const float wanted = behind ? 1.0f : progress();
    if (drawnLevel_ > hero_->level) {
        drawnLevel_ = hero_->level;
        slid_ = wanted;
    }
    const float gap = std::fabs(wanted - slid_);
    const float step = std::max(gap * kLevelEase, kLevelSlideLeast) * seconds;
    slid_ = wanted > slid_ ? std::min(wanted, slid_ + step) : std::max(wanted, slid_ - step);
    if (behind && slid_ >= 1.0f) {
        ++drawnLevel_;
        slid_ = 0.0f;
    }
}

int Hud::hoveredAt(float x, float y) const {
    for (int i = 0; i < 4; ++i) {
        if (kButtons[i].live && plate(screen_, buttonPx(i)).has(x, y)) return 100 + i;
    }
    for (int i = 0; i < kSlots; ++i) {
        if (plate(screen_, boxPx(i)).has(x, y)) return i;
    }
    return -1;
}

bool Hud::tipAt(float x, float y) const {
    return plate(screen_, kLifeHole).has(x, y) || plate(screen_, kManaHole).has(x, y) ||
           (hero_ && hero_->maxSd > 0 && plate(screen_, kShieldBar).has(x, y)) ||
           plate(screen_, kLevelTrack).has(x, y);
}

bool Hud::covers(float x, float y) const {
    if (!hero_) return false;
    if (plate(screen_, {0.0f, 0.0f, kPlateW, kPlateH}).has(x, y)) return true;
    if (plate(screen_, kLevelTrack).has(x, y)) return true;
    for (int i = 0; i < 4; ++i) {
        if (plate(screen_, buttonPx(i)).has(x, y)) return true;
    }
    return false;
}

void Hud::update(float seconds, float width, float height, const Pointer& pointer,
                 bool inventoryOpen, bool characterOpen, bool* toggleInventory,
                 bool* toggleCharacter) {
    clock_ += double(seconds);
    screen_ = panel::screenOf(width, height);
    slide(seconds);

    if (hero_ && pointer.pressed) {
        // On the press, as Hud.Hit answers: a button here is a toggle and not a commitment.
        const int over = hoveredAt(pointer.x, pointer.y);
        if (over == 102 && toggleInventory) *toggleInventory = true;
        if (over == 103 && toggleCharacter) *toggleCharacter = true;
    }

    now_ = Face{};
    if (hero_) {
        now_.width = width;
        now_.height = height;
        now_.health = hero_->health;
        now_.maxHealth = hero_->maxHealth;
        now_.mana = hero_->mana;
        now_.maxMana = hero_->maxMana;
        now_.shield = hero_->sd;
        now_.maxShield = hero_->maxSd;
        now_.level = hero_->level;
        // Worked out once a frame and read by both the comparison and the draw, so the two
        // can never disagree about which cell the frame was for. Hud.gem.
        now_.gem = int(clock_ * double(kTurn)) % (kGemsAcross * kGemsDown);
        now_.slid = slid_;
        now_.inventory = inventoryOpen;
        now_.character = characterOpen;
        now_.hovered = hoveredAt(pointer.x, pointer.y);
        now_.tip = tipAt(pointer.x, pointer.y);
        now_.pointerX = pointer.x;
        now_.pointerY = pointer.y;
        for (int i = 0; i < kQuickKeys; ++i) now_.quick[i] = quick_[i];
        for (int i = 0; i < kSkillKeys; ++i) now_.skill[i] = skill_[i];
        // What stands on the potion boxes' stage: each bound row in its box, in MU units from
        // the plate's corner. The same list twice is no redraw (Stage::stand).
        if (stage_) {
            standing_.clear();
            for (int i = 0; i < kQuickKeys; ++i) {
                if (quick_[i].item < 0) continue;
                const Box px = boxPx(kFirstQuick + i);
                Standing one;
                one.item = quick_[i].item;
                one.box = {px.x * kUnit, px.y * kUnit, px.w * kUnit, px.h * kUnit};
                standing_.push_back(one);
            }
            stage_->stand(standing_, kPlateW * kUnit, kPlateH * kUnit);
            const gfx::Art picture = stage_->picture();
            now_.picture = picture.valid() ? picture.handle.idx : 0xFFFF;
        }
    }
    if (now_ == drawn_ && rebuilds_ > 0) return;
    drawn_ = now_;
    rebuild();
}

void Hud::rebuild() {
    ++rebuilds_;
    canvas_.clear();
    tip_.clear();
    if (!hero_ || !arts_) return;
    panel::Arts& arts = *arts_;
    const panel::Screen& s = screen_;

    // The sockets' backs first: the plate's diamonds are holes, and a gem drained to nothing
    // would otherwise show the grass through its setting.
    diamond(canvas_, nullptr, plate(s, kLifeHole), {}, 0.0f, 1.0f, kSocketBack);
    diamond(canvas_, nullptr, plate(s, kManaHole), {}, 0.0f, 1.0f, kSocketBack);

    // The bar goes under the plate too: MuDream's rail is painted with notches and an ornament
    // that show across it, so what the rail carves out of the bar is the plate's own drawing.
    if (hero_->maxSd > 0) {
        const Box box = plate(s, kShieldBar);
        const gfx::Art& empty = arts.get("hud_bar_shield_empty");
        if (empty.valid()) canvas_.region(empty, box, {kShieldFrom, 0.0f, kShieldSpan, kShieldBar.h});
        const float full = fraction(hero_->sd, hero_->maxSd);
        const gfx::Art& bar = arts.get("hud_bar_shield");
        if (full > 0.0f && bar.valid()) {
            canvas_.region(bar, {box.x, box.y, box.w * full, box.h},
                           {kShieldFrom, 0.0f, kShieldSpan * full, kShieldBar.h});
        }
    }

    // The gems, under the plate: cropped from the waterline down, the frame the clock is on.
    const int tick = now_.gem;
    const Box cell{float(tick % kGemsAcross) * kGemFrame, float(tick / kGemsAcross) * kGemFrame,
                   kGemFrame, kGemFrame};
    const float life = fraction(hero_->health, hero_->maxHealth);
    const float mana = fraction(hero_->mana, hero_->maxMana);
    if (life > 0.0f) {
        diamond(canvas_, &arts.get("hud_gem_life"), plate(s, kLifeHole), cell, 1.0f - life, 1.0f,
                0xFFFFFFFFu);
    }
    if (mana > 0.0f) {
        diamond(canvas_, &arts.get("hud_gem_mana"), plate(s, kManaHole), cell, 1.0f - mana, 1.0f,
                0xFFFFFFFFu);
    }

    canvas_.image(arts.get("hud_base"), plate(s, {0.0f, 0.0f, kPlateW, kPlateH}));

    // The boxes' states: MuDream's sheen under the pointer. The skill boxes are empty until the
    // skill sprint and the item boxes until the quick bar is built.
    for (int i = 0; i < kSlots; ++i) {
        if (now_.hovered == i) canvas_.image(arts.get("hud_slot_hover"), plate(s, boxPx(i)));
    }

    // What is bound to the potion boxes: the thing's name, cut to the box, and how many
    // of it he carries at the box's foot -- Quick.cs's count, which counts what may stand in
    // for it too. Dim when he has none left, as MU draws an empty hotkey. The picture is the
    // stage's, and until the stage lands the name stands in for it.
    const float quickSize = std::round(11.0f * kUnit * s.scale);
    const gfx::Art picture = stage_ ? stage_->picture() : gfx::Art{};
    if (picture.valid() && !standing_.empty()) {
        canvas_.image(picture, plate(s, {0.0f, 0.0f, kPlateW, kPlateH}));
    }
    for (int i = 0; i < kQuickKeys; ++i) {
        const Quick& q = quick_[i];
        if (q.item < 0) continue;
        const Box box = plate(s, boxPx(kFirstQuick + i));
        const uint32_t ink = q.count > 0 ? kInk : kDeadIcon;
        if (!picture.valid()) {
            std::string word = q.label.substr(0, std::min<size_t>(q.label.size(), 5));
            canvas_.shadowed(box.midX(), box.midY(), quickSize, ink, kInkShadow, 1.0f, word,
                             gfx::Align::Centre, 0.0f);
        } else if (q.count == 0) {
            // None left: the picture dimmed, as MU draws a spent hotkey.
            canvas_.rect(box, gfx::rgba(0.0f, 0.0f, 0.0f, 0.55f));
        }
        canvas_.shadowed(box.x, box.bottom() - 3.0f, quickSize, ink, kInkShadow, 1.0f,
                         std::to_string(q.count), gfx::Align::Right, box.w - 3.0f);
    }

    // The skill boxes: the icon MuDream's own sheet gives the skill, the cooldown wiped down over
    // it, and what is left of it in seconds.
    //
    // A WIPE AND NOT A RADIAL SWEEP. LoL and WoW both turn a hand round the icon; this fills the
    // box from the top down as the cooldown runs, because a wipe is one rectangle and a sweep is
    // a triangle fan the canvas has no primitive for. What both conventions have in common -- and
    // what actually reads at a glance -- is that the dark shrinks as the skill comes back, and
    // that is kept.
    const float skillSize = std::round(13.0f * kUnit * s.scale);
    for (int i = 0; i < kSkillKeys; ++i) {
        const Skill& one = skill_[i];
        if (one.number == 0) continue;
        const Box box = plate(s, boxPx(i));
        const gfx::Art& icon = arts.get(one.icon);
        // **Disabled is a state and not a shade of the ready one.** A skill he cannot throw --
        // the mana is not there, or the hand that the skill needs is empty -- is drawn cold and
        // dark rather than merely dimmer: the icon goes through a blue-grey tint that takes the
        // colour out of it, and a wash over the top takes the brightness. MU dims a hotkey it
        // will not honour; this says the same thing louder, because a cooldown already owns the
        // "dark for a moment" language and the two must not read as each other.
        const uint32_t tint = one.affordable ? 0xFFFFFFFFu : gfx::rgba(0.42f, 0.44f, 0.52f, 1.0f);
        if (icon.valid()) canvas_.image(icon, box, tint);
        if (!one.affordable) canvas_.rect(box, gfx::rgba(0.0f, 0.0f, 0.02f, 0.45f));
        if (one.cooling > 0.0f) {
            const float tall = box.h * std::min(1.0f, one.cooling);
            canvas_.rect({box.x, box.y, box.w, tall}, gfx::rgba(0.0f, 0.0f, 0.0f, 0.62f));
            // The figure only while there is more than a second of it: a box flashing "0.3" is
            // noise, and both references stop printing tenths under a second for the same reason.
            if (one.seconds >= 1.0f) {
                canvas_.shadowed(box.midX(), box.midY() + skillSize * 0.35f, skillSize, kInk,
                                 kInkShadow, 1.0f, std::to_string(int(one.seconds + 0.5f)),
                                 gfx::Align::Centre, 0.0f);
            }
        }
    }

    // The keys, relabelled. The painted figure is covered by the rail's own dark and the key
    // that really fires the box printed in its place, in the plate's own two inks.
    const float labelSize = kLabelSize * kUnit * s.scale;
    for (int i = 0; i < kSlots; ++i) {
        if (kKeys[i] == nullptr) continue;
        const Box box = boxPx(i);
        const float cx = box.midX();
        canvas_.rect(plate(s, {cx - kLabelWide * 0.5f, kLabelTop, kLabelWide, kLabelTall}),
                     kLabelCell);
        if (kKeys[i][0] == '\0') continue;
        const Box at = plate(s, {cx, kLabelBaseline, 0.0f, 0.0f});
        canvas_.text(at.x, at.y, labelSize, i < kFirstQuick ? kSkillKey : kQuickKey, kKeys[i],
                     gfx::Align::Centre, 0.0f);
    }

    // The side buttons: a disc each, MuMain's icon on it, in the state the pointer and the
    // window behind it put it in. Four states stacked for the main frame's two (closed,
    // closed-hovered, open, open-hovered), two for the chat's and the menu's.
    const gfx::Art& disc = arts.get("hud_disc");
    for (int i = 0; i < 4; ++i) {
        const ButtonRow& row = kButtons[i];
        const Box box = plate(s, buttonPx(i));
        const bool hovered = now_.hovered == 100 + i;
        const bool open = (i == 2 && now_.inventory) || (i == 3 && now_.character);
        // Brightened under the pointer in MU2, by a modulate of 1.35; a byte cannot go past
        // white, so here the icon's own lit state is the whole of the hover. A departure.
        canvas_.image(disc, box, row.live ? 0xFFFFFFFFu : kDeadIcon);
        const gfx::Art& sheet = arts.get(row.art);
        if (!sheet.valid()) continue;
        const int states = sheet.height / sheet.width >= 3.0f ? 4 : 2;
        const float tall = sheet.height / float(states);
        const int state = states == 4 ? (open ? 2 : 0) + (hovered ? 1 : 0) : (open || hovered);
        const float share = states == 4 ? 0.7f : 0.5f;
        const float fit = std::min(box.w * share / sheet.width, box.w * share / tall);
        const float w = sheet.width * fit, h = tall * fit;
        canvas_.region(sheet, {box.midX() - w * 0.5f, box.midY() - h * 0.5f, w, h},
                       {0.0f, float(state) * tall, sheet.width, tall},
                       row.live ? 0xFFFFFFFFu : kDeadIcon);
    }

    // The level: the rail, and the fill as far as the hairline has slid.
    canvas_.image(arts.get("hud_level_track"), plate(s, kLevelTrack));
    const gfx::Art& fill = arts.get("hud_level_fill");
    if (slid_ > 0.0f && fill.valid()) {
        const Box box = plate(s, kLevelFill);
        canvas_.region(fill, {box.x, box.y, box.w * slid_, box.h},
                       {0.0f, 0.0f, fill.width * slid_, fill.height});
    }

    // The shield's amount, at the bar's left end. Hud.Amount.
    if (hero_->maxSd > 0) {
        const Box box = plate(s, kShieldBar);
        const float tall = std::max(6.0f, std::round(kBarReadingTall * kUnit * s.scale));
        canvas_.shadowed(box.x + kShieldReadingIn * kUnit * s.scale, box.midY() + tall * 0.36f,
                         tall, kInk, kInkShadow, std::max(1.0f, tall / 12.0f),
                         panel::grouped(hero_->sd), gfx::Align::Left, 0.0f);
    }

    // The gems' readings, across the middle of each diamond. Hud.Print: centred, the baseline
    // a third of a size below the middle, shadowed a twelfth of a size down and right.
    const float size = std::max(6.0f, std::round(kReadingTall * kUnit * s.scale));
    const float drop = std::max(1.0f, size / 12.0f);
    for (const auto& [hole, value] :
         {std::pair{kLifeHole, hero_->health}, std::pair{kManaHole, hero_->mana}}) {
        const Box box = plate(s, hole);
        canvas_.shadowed(box.midX(), box.midY() + size * 0.36f, size, kInk, kInkShadow, drop,
                         panel::grouped(value), gfx::Align::Centre, 0.0f);
    }

    // Last, because it goes over everything it describes: what the pointer is resting on.
    if (now_.tip) {
        const float px = now_.pointerX, py = now_.pointerY;
        std::string name, value;
        if (plate(s, kLifeHole).has(px, py)) {
            name = "Life";
            value = std::to_string(hero_->health) + " / " + std::to_string(hero_->maxHealth);
        } else if (plate(s, kManaHole).has(px, py)) {
            name = "Mana";
            value = std::to_string(hero_->mana) + " / " + std::to_string(hero_->maxMana);
        } else if (hero_->maxSd > 0 && plate(s, kShieldBar).has(px, py)) {
            name = "Shield";
            value = std::to_string(hero_->sd) + " / " + std::to_string(hero_->maxSd);
        } else {
            name = "Experience";
            const uint64_t at = sim::neededExperience(hero_->level);
            const uint64_t next = sim::neededExperience(hero_->level + 1);
            const uint64_t into = hero_->experience > at ? hero_->experience - at : 0;
            value = panel::commas((long long)into) + " / " + panel::commas((long long)(next - at)) +
                    "   (" + std::to_string(segment()) + "/" + std::to_string(kTenths) + ")";
        }
        const std::vector<panel::Line> lines = {{name, kTipNameColour, true},
                                                {value, kTipColour, false}};
        const Box top = plate(s, {0.0f, 0.0f, kPlateW, kPlateH});
        panel::tooltip(tip_, px, top.y, lines, std::round(kTipTall * kUnit * s.scale),
                       now_.width, now_.height);
    }
}

}  // namespace mu::game
