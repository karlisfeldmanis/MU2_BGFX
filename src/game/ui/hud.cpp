#include "game/ui/hud.h"

#include "sim/realm_tuning.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "game/ui/controls.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"
#include "sim/recovery.h"
#include "sim/rules.h"
#include "sim/skills.h"

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
// Menu and chat left of the life gem, inventory, character and quests right of the mana gem. The
// menu is live since 2026-09-27 and raises the game menu (game/ui/menu.h), as Escape does; the
// chat is drawn dim, there being no chat in a game for one. The quests' opens the journal, as L
// does (the user, 2026-10-05: "we need to find icon for quests button which to put on right side
// of hud where is inventiry and character buttons").
constexpr int kButtonCount = 5;
constexpr int kQuestButton = 4;
constexpr ButtonRow kButtons[kButtonCount] = {
    {"hud_button_menu", true, true},
    {"hud_button_chat", true, false},
    {"hud_button_inventory", false, true},
    {"hud_button_character", false, true},
    {"hud_button_quest", false, true},
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

// The buff strip: MuDream's own place for it, the row starting over the shield bar's left end
// and running right. MU2 draws a 30-pixel SQUARE there (`Hud.BuffsAt`), and this is bigger than
// that on two counts, the second of which is why it looked small at MU2's own number:
//
//   * the cell is MuDream's own 80 by 112 and not a square. The icons are cut at that shape --
//     a framed plate, taller than it is wide -- and a square cell squashed each by a third.
//   * and it is filled to the plate's upper band rather than to thirty: the band between the
//     plate's top edge and the shield rail is 64 pixels of empty ornament and the strip may
//     have it. The user, 2026-09-23: the icon is a little too small.
//   * and 12 higher than that, so a cell's foot clears the experience bar rather than resting
//     on it (the user, 2026-09-30: "so they dont touch HUD").
constexpr Box kBuffsAt{292.0f, 0.0f, 40.0f, 56.0f};
constexpr uint32_t kBuffEdge = gfx::rgba(0.627f, 0.549f, 0.373f, 0.55f);
constexpr uint32_t kBuffBack = gfx::rgba(0.0f, 0.0f, 0.0f, 0.45f);
constexpr uint32_t kBuffLeft = gfx::rgba(0.761f, 0.706f, 0.561f, 0.9f);
// A debuff's cell: the buffs' own hairline edge in a muted red, and a red bar, WoW's sign for
// something done to you (ours). Thin: a two-pixel bright red frame was too loud (the user).
constexpr uint32_t kDebuffEdge = gfx::rgba(0.72f, 0.22f, 0.18f, 0.75f);
constexpr uint32_t kDebuffLeft = gfx::rgba(0.94f, 0.30f, 0.22f, 0.95f);

// The gap between two cells of the row, in plate pixels: MuDream's own strip spaces its 80-wide
// cells by a fifth of one, and this is that at the cell's 40.
constexpr float kBuffGap = 8.0f;
Box buffPx(int i) {
    return {kBuffsAt.x + float(i) * (kBuffsAt.w + kBuffGap), kBuffsAt.y, kBuffsAt.w, kBuffsAt.h};
}

// Which of MuDream's status cells a skill wears. Defense and Soul Barrier are the two this game
// can put on a character; the elf's two Greaters and the wizard's two debuffs are cut and
// waiting. The Ale is not a skill and wears `buff_ale` (pipeline/buff_icons.py says which cell
// it borrows).
const char* buffArt(int32_t skill) {
    switch (skill) {
        case 16: return "buff_soul_barrier";
        case 18: return "buff_defense";
        case 27: return "buff_greater_defense";
        case 28: return "buff_greater_damage";
        default: return nullptr;
    }
}

// The pets' cells, ours: each rendered from the pet's own model by pipeline/model_icons.py.
const char* petArt(int pet) {
    return pet == 0 ? "buff_angel" : pet == 1 ? "buff_imp" : pet == 2 ? "buff_uniria" : "buff_dinorant";
}

// The painted key labels: a dark cell on rows 164 to 177 under every box, the figure centred on
// the box. Measured off hud_base.png for this sprint; (15, 16, 17) is the cell's own dark.
constexpr float kLabelTop = 164.0f, kLabelTall = 14.0f, kLabelWide = 20.0f;
// What each box's key is now, left to right. Skills on Q W E R T, potions on 1 to 5, and the
// box in hand prints nothing -- the mouse under the gold box stays, since that box IS the
// button's.
const char* const kKeys[kSlots] = {"Q", "W", "E", "R", "T", nullptr, "1", "2", "3", "4", "5"};

constexpr uint32_t kSocketBack = gfx::rgba(0.05f, 0.055f, 0.07f, 0.9f);
constexpr uint32_t kInk = gfx::rgba(1.0f, 250.0f / 255.0f, 240.0f / 255.0f);
constexpr uint32_t kInkShadow = gfx::rgba(0.0f, 0.0f, 0.0f, 0.7f);
constexpr uint32_t kDeadIcon = gfx::rgba(0.55f, 0.55f, 0.55f, 0.6f);
constexpr uint32_t kLabelCell = gfx::rgba(15.0f / 255.0f, 16.0f / 255.0f, 17.0f / 255.0f);

// The ring a fired potion box wears: the key's own gold, a quarter of a second of it, stepping
// four plate pixels out of the box's edge as it goes and fading as it steps. The ring is OUTSIDE
// the box, so the bottle and its count are never covered by the thing that says they changed --
// the eye sees the edge leave and the contents stay put. Under it, for the first breath only, a
// thin gold sheen inside the box: the sheen is what carries at the corner of the eye while the
// player is watching a monster, the ring is what says WHICH key answered. Both die to nothing,
// because a potion has no cooldown to draw and the box must be back to plain immediately.
constexpr float kStrike = 0.26f;    // seconds of ring
constexpr int kStrikeSteps = 12;    // and how many redraws that is
constexpr float kStrikeOut = 4.0f;  // plate pixels the ring ends up outside the box
constexpr float kStrikeLine = 2.0f;
// The sheen's alpha at the moment of the press. A fifth and not a third: at a third the apple
// went pale under it and the box read as the picture changing rather than as the box answering,
// which is the one thing this must not do.
constexpr float kStrikeWash = 0.20f;
// A skill come back: the throw's ring run backwards and a little slower, closing from six plate
// pixels out onto the box's edge, and a white breath over the icon that is brightest at the
// start. Longer than the strike because it is news the player was not waiting on a key for --
// it has to catch an eye that is on a monster, not on the bar.
constexpr float kBack = 0.40f;
constexpr int kBackSteps = 16;
constexpr float kBackIn = 6.0f;
constexpr float kBackWash = 0.35f;

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

// ---- the rail: the list of learned skills, above the plate ---------------------------------
//
// **In the card's own style, on the user's rule of 2026-09-23** -- *"you did a very good job with
// the map message and the tooltips, it has to be the same style"*. So the container is literally
// the item card's (`tip::glass`: the graded near-black body, the warm hairline ring, the three
// falloffs of shadow), the kicker is the card's tracked caps, and the inks are `tip::ink`. None
// of it is MuDream's bevelled box art, which is what the first pass used and what read as
// another game's furniture sitting on top of this one.
//
// **A grid of large icons**, and it is the second shape: the first was a rail of named pills,
// which the user rejected on sight for the two reasons that decide this layout --
// *"if the DK will have a lot of skills there will be issues, the skill icon has to be big
// enough"*. Names beside icons cost 180 pixels an entry and six of them spanned half a 1920
// screen; icons alone cost 62, wrap into a second row when there are more than six, and leave
// the picture big enough to recognise without reading. What a name is for, the card under the
// pointer does better -- and that is the card the keys already raise.
//
// The cell is LARGER than the bar's own box (56 against the plate's 46 at 1080 lines), because
// the list is what you are looking at while it is open and the bar is furniture you glance at.
// Diablo III's chooser does the same: the list's icons are the biggest thing on the screen.
//
// Measured in the card's own 1080-line pixels (`tip::unit`) and PLACED off the plate: centred on
// the gold box, floating clear of the plate's top edge. That is the same mixture the tooltip
// makes when it stands over a bag cell.
constexpr float kListPad = 12.0f;
// How far it floats clear of the plate, and WHAT it is measured from -- which is the whole of
// the answer to the user's "it has to be closer to the HUD" (2026-09-23). The plate's box runs
// up to its own y = 0, but the art up there is transparent in the middle: the painted furniture
// stops at the shield rail, and everything above it is the two gems' tops and open sky. Hung
// off the box, the list stood forty pixels clear of anything drawn. So it is hung off the RAIL,
// which is the top of what the plate actually paints.
// Thirty, found between two tries the user turned down: four off the plate's box left forty
// pixels of sky under it, and four off the rail sat the tray ON the experience bar. This is a
// finger's width of air over the frame -- near enough to belong to it, clear enough to read as
// a thing standing over it.
constexpr float kListLift = 30.0f;
// No kicker over it. It carried the card's tracked "SKILLS" and a hairline, which is the house
// style and was still the wrong thing here: the user asked on 2026-09-23 whether the title was
// needed, and it is not -- a tray of skill icons standing on the skill bar says what it is, and
// a heading costs it a row of height it then has to float further from the frame to keep.
constexpr float kCell = 56.0f;
// The cell's own rim, so the picture sits inside its edge rather than on it.
constexpr float kCellRim = 5.0f;
constexpr float kCellGap = 6.0f;
constexpr int kAcross = 6;              // before it wraps to a second row
constexpr float kChipWide = 15.0f;
constexpr float kChipTall = 15.0f;

constexpr uint32_t kCellBack = gfx::rgba(1.0f, 1.0f, 1.0f, 0.035f);
constexpr uint32_t kCellOver = gfx::rgba(1.0f, 1.0f, 1.0f, 0.10f);
constexpr uint32_t kCellEdge = gfx::rgba(0.627f, 0.549f, 0.373f, 0.22f);
constexpr uint32_t kCellEdgeOver = gfx::rgba(0.878f, 0.800f, 0.573f, 0.55f);

// How many columns and rows a count of entries is laid out in: up to six across, then wrapped.
int fanAcross(size_t count) { return int(std::min<size_t>(count, size_t(kAcross))); }
int fanDown(size_t count) {
    const int across = fanAcross(count);
    return across <= 0 ? 0 : int((count + size_t(across) - 1) / size_t(across));
}

// Where the whole list stands, in screen pixels: as wide as its widest row, centred on the
// PLATE, its foot a hair above it, and kept on screen.
//
// Centred on the plate and not on the gold box, which is the user's rule of 2026-09-23 and a
// departure from MU. `CNewUISkillList` fans its cells out of the box they belong to, and that
// box sits right of the plate's middle, so the list stood off-centre over a frame that is
// itself centred -- which reads as a mistake however traced it is. The gold box is still what
// opens it.
Box listBox(const panel::Screen& s, size_t count, float screenWidth) {
    const float u = tip::unit();
    const int across = fanAcross(count), down = fanDown(count);
    const float wide = kListPad * 2.0f * u + float(across) * kCell * u +
                       float(across > 0 ? across - 1 : 0) * kCellGap * u;
    const float tall = kListPad * 2.0f * u + float(down) * kCell * u +
                       float(down > 0 ? down - 1 : 0) * kCellGap * u;
    const float top = plate(s, kShieldBar).y - kListLift * u - tall;
    const float margin = 4.0f * u;
    const float x = std::clamp(plate(s, {0.0f, 0.0f, kPlateW, kPlateH}).midX() - wide * 0.5f,
                               margin, std::max(margin, screenWidth - margin - wide));
    return {x, top, wide, tall};
}

// And one cell in it, filled left to right and then down, in the order they were learned.
Box cellBox(const panel::Screen& s, size_t count, float screenWidth, int index) {
    const float u = tip::unit();
    const Box rail = listBox(s, count, screenWidth);
    const int across = std::max(1, fanAcross(count));
    const int column = index % across, row = index / across;
    return {rail.x + kListPad * u + float(column) * (kCell + kCellGap) * u,
            rail.y + kListPad * u + float(row) * (kCell + kCellGap) * u, kCell * u, kCell * u};
}

Box buttonPx(int which) {
    const bool left = kButtons[which].left;
    int rank = 0, pair = 0;
    for (int i = 0; i < kButtonCount; ++i) {
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
           character == o.character && quest == o.quest && hovered == o.hovered && tip == o.tip &&
           (!tip || (pointerX == o.pointerX && pointerY == o.pointerY)) &&
           std::equal(boons, boons + kBoons, o.boons) && fanOpen == o.fanOpen &&
           fanOver == o.fanOver &&
           carrying == o.carrying && liftedQuick == o.liftedQuick &&
           fan == o.fan &&
           ((carrying == 0 && liftedQuick < 0) ||
            (pointerX == o.pointerX && pointerY == o.pointerY)) &&
           std::equal(quick, quick + kQuickKeys, o.quick) &&
           std::equal(struck, struck + kQuickKeys, o.struck) &&
           std::equal(skillStruck, skillStruck + kSkillBoxes, o.skillStruck) &&
           std::equal(skillBack, skillBack + kSkillBoxes, o.skillBack) && picture == o.picture &&
           std::equal(skill, skill + kSkillBoxes, o.skill);
}

void Hud::strikeQuick(int key) {
    if (key >= 0 && key < kQuickKeys) struck_[key] = 0.0f;
}

void Hud::strikeSkill(int key) {
    if (key >= 0 && key < kSkillBoxes) skillStruck_[key] = 0.0f;
}

void Hud::readySkill(int key) {
    if (key >= 0 && key < kSkillBoxes) skillBack_[key] = 0.0f;
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

float Hud::plateTop() const { return plate(screen_, {0.0f, 0.0f, kPlateW, kPlateH}).y; }

gfx::Box Hud::wornCell(int i) const {
    // Right-aligned to the belt's last box, so the row stands over the potions as the buffs stand
    // over the skills (the user, 2026-10-05: "we need to find betetr place and choose betetr size
    // for broken item UI").
    // Round and a little smaller than a buff cell (the user, the same day: "make them little bit
    // smaller and in circle containers"), standing on the strip's foot.
    constexpr float kWornAcross = 40.0f;
    const float end = kQuickX + float(kQuickKeys - 1) * kQuickPitch + kQuickW;
    const float x = end - float(i + 1) * kWornAcross - float(i) * kBuffGap;
    return plate(screen_, {x, kBuffsAt.y + kBuffsAt.h - kWornAcross, kWornAcross, kWornAcross});
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
    for (int i = 0; i < kButtonCount; ++i) {
        if (kButtons[i].live && plate(screen_, buttonPx(i)).has(x, y)) return 100 + i;
    }
    for (int i = 0; i < kSlots; ++i) {
        if (plate(screen_, boxPx(i)).has(x, y)) return i;
    }
    return -1;
}

int Hud::skillAt(float x, float y) const {
    for (int i = 0; i < kSkillBoxes; ++i) {
        if (skill_[i].number != 0 && plate(screen_, boxPx(i)).has(x, y)) return i;
    }
    return -1;
}

int Hud::boxAt(float x, float y) const {
    for (int i = 0; i < kSlots; ++i) {
        if (plate(screen_, boxPx(i)).has(x, y)) return i;
    }
    return -1;
}

int Hud::fanAt(float x, float y) const {
    if (!fanOpen_) return -1;
    for (size_t i = 0; i < fan_.size(); ++i) {
        if (cellBox(screen_, fan_.size(), width_, int(i)).has(x, y)) return int(i);
    }
    return -1;
}

// The whole rail and not just its entries: the pointer crossing the padding between two pills is
// still on the list, and a list that shut there would shut halfway through every drag.
bool Hud::coversFan(float x, float y) const {
    return fanOpen_ && !fan_.empty() && listBox(screen_, fan_.size(), width_).has(x, y);
}

bool Hud::nearFan(float x, float y) const {
    if (!fanOpen_ || fan_.empty()) return false;
    // One box from the top of the list down to the foot of the row it fills, as wide as the
    // wider of the two. Geometry and not a timer: a grace period would be a second answer to
    // "is it open" that the drawing and the pointer could disagree about.
    const Box list = listBox(screen_, fan_.size(), width_);
    const Box gold = plate(screen_, boxPx(kGoldBox));
    const float left = std::min(list.x, gold.x);
    const float right = std::max(list.right(), gold.right());
    return Box{left, list.y, right - left, gold.bottom() - list.y}.has(x, y);
}

int Hud::skillSlotAt(float x, float y) const {
    for (int i = 0; i < kSkillBoxes; ++i) {
        if (plate(screen_, boxPx(i)).has(x, y)) return i;
    }
    return -1;
}

int Hud::boonAt(float x, float y) const {
    for (int i = 0; i < kBoons; ++i) {
        if (!boons_[i].empty() && plate(screen_, buffPx(i)).has(x, y)) return i;
    }
    return -1;
}

bool Hud::tipAt(float x, float y) const {
    return skillAt(x, y) >= 0 || boonAt(x, y) >= 0 ||
           plate(screen_, kLifeHole).has(x, y) || plate(screen_, kManaHole).has(x, y) ||
           (hero_ && hero_->maxSd > 0 && plate(screen_, kShieldBar).has(x, y)) ||
           plate(screen_, kLevelTrack).has(x, y);
}

bool Hud::covers(float x, float y) const {
    if (!hero_) return false;
    if (plate(screen_, {0.0f, 0.0f, kPlateW, kPlateH}).has(x, y)) return true;
    if (plate(screen_, kLevelTrack).has(x, y)) return true;
    for (int i = 0; i < kButtonCount; ++i) {
        if (plate(screen_, buttonPx(i)).has(x, y)) return true;
    }
    // And the open list, which stands off the plate over the world: a click on it is the
    // interface's, or choosing a skill would walk the character to where it was drawn.
    return coversFan(x, y);
}

void Hud::update(float seconds, float width, float height, const Pointer& pointer,
                 bool inventoryOpen, bool characterOpen, bool* toggleInventory,
                 bool* toggleCharacter, bool* toggleMenu, bool questOpen, bool* toggleQuest) {
    clock_ += double(seconds);
    screen_ = panel::screenOf(width, height);
    slide(seconds);

    if (hero_ && pointer.pressed) {
        // On the press, as Hud.Hit answers: a button here is a toggle and not a commitment.
        const int over = hoveredAt(pointer.x, pointer.y);
        if (over == 100 && toggleMenu) *toggleMenu = true;
        if (over == 102 && toggleInventory) *toggleInventory = true;
        if (over == 103 && toggleCharacter) *toggleCharacter = true;
        if (over == 100 + kQuestButton && toggleQuest) *toggleQuest = true;
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
        now_.quest = questOpen;
        now_.hovered = hoveredAt(pointer.x, pointer.y);
        now_.tip = tipAt(pointer.x, pointer.y);
        now_.pointerX = pointer.x;
        now_.pointerY = pointer.y;
        for (int i = 0; i < kQuickKeys; ++i) {
            now_.quick[i] = quick_[i];
            if (struck_[i] < kStrike) struck_[i] += seconds;
            now_.struck[i] =
                struck_[i] >= kStrike ? -1 : int(struck_[i] / kStrike * float(kStrikeSteps));
        }
        for (int i = 0; i < kSkillBoxes; ++i) {
            now_.skill[i] = skill_[i];
            if (skillStruck_[i] < kStrike) skillStruck_[i] += seconds;
            now_.skillStruck[i] = skillStruck_[i] >= kStrike
                                      ? -1
                                      : int(skillStruck_[i] / kStrike * float(kStrikeSteps));
            if (skillBack_[i] < kBack) skillBack_[i] += seconds;
            now_.skillBack[i] =
                skillBack_[i] >= kBack ? -1 : int(skillBack_[i] / kBack * float(kBackSteps));
        }
        width_ = width;
        height_ = height;
        std::copy(boons_, boons_ + kBoons, now_.boons);
        now_.fanOpen = fanOpen_;
        now_.fan = fan_;
        now_.carrying = carrying_;
        now_.liftedQuick = liftedQuick_;
        now_.fanOver = fanAt(pointer.x, pointer.y);
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

// ---- the cards over the plate ---------------------------------------------------------------
//
// Every hover on the plate is the skill card's glass since 2026-09-28 (the user: "improve the
// tooltips"): a name in its colour, the reading first and large enough to find, then what it is
// made of as label-and-figure rows, and the foot for how long or how full. The sums use the
// realm's own formulas and constants (sim/rules.h, sim/recovery.h, sim/skills.h), so a card can
// not say a number the game does not use.
namespace {

constexpr float kCardWide = 244.0f;

tip::Row said(const std::string& label, const std::string& value, tip::Tone tone) {
    tip::Row one;
    one.label = label;
    one.values.push_back({value, tone, false, "", 0});
    return one;
}

tip::Row prose(const std::string& text, tip::Tone tone = tip::Tone::Gray) {
    tip::Row one;
    one.free = text;
    one.freeTone = tone;
    return one;
}

std::string times(int count, float each) {
    char text[48];
    std::snprintf(text, sizeof text, each == float(int(each)) ? "%d x %.0f" : "%d x %.1f", count,
                  double(each));
    return text;
}

std::string percent(float share, int places = 0) {
    char text[16];
    std::snprintf(text, sizeof text, places == 0 ? "%.0f%%" : "%.1f%%", double(share) * 100.0);
    return text;
}

std::string of(int now, int most) { return panel::grouped(now) + " / " + panel::grouped(most); }

}  // namespace

tip::Sheet Hud::boonSheet(const Boon& one, panel::Arts& arts) const {
    tip::Sheet sheet;
    sheet.wide = kCardWide;
    const char* art = one.pet >= 0 ? petArt(one.pet)
                      : one.poison ? "buff_poison"
                      : one.ale    ? "buff_ale"
                      : one.frenzy ? "buff_frenzy"
                      : one.potion == 0 ? "buff_healing"
                      : one.potion == 1 ? "buff_mana"
                                   : buffArt(one.skill);
    if (art != nullptr) {
        const gfx::Art& icon = arts.get(art);
        if (icon.valid()) {
            sheet.picture = icon;
            sheet.from = {0.0f, 0.0f, icon.width, icon.height};
        }
    }
    sheet.wear = sim::spoken(one.seconds) + " left";
    sheet.worn = std::clamp(one.share, 0.0f, 1.0f);
    sheet.wearTone = tip::Tone::White;
    if (one.pet >= 0) {
        // What the pet does while it has life, in MU's words (the item card's own lines,
        // sim::petPower), and its Life in the foot where a boon keeps its time.
        sheet.name = one.pet == 0   ? "Guardian Angel"
                     : one.pet == 1 ? "Imp"
                     : one.pet == 2 ? "Horn of Uniria"
                                    : "Horn of Dinorant";
        sheet.nameTone = tip::Tone::White;
        sheet.base = "PET";
        content::ItemRow row;
        row.group = sim::kGroupPets;
        row.number = one.pet;
        sim::PetPower power = sim::petPower(row);
        // A Kinship rune lifts the price and keeps the gift, as rearm reckons it.
        const bool lifted = one.kinship && (power.dealt < 1.0 || power.lifeCost > 0);
        if (one.kinship) {
            power.dealt = std::max(1.0, power.dealt);
            power.lifeCost = 0;
        }
        const auto percent = [](double share) {
            return std::to_string(int(std::lround(share * 100.0))) + "%";
        };
        tip::Section what;
        if (power.taken != 1.0) {
            what.rows.push_back(said("Absorbs", percent(1.0 - power.taken), tip::Tone::Green));
            what.rows.push_back(prose("of every blow that reaches you"));
        }
        if (power.health > 0) {
            what.rows.push_back(said("Max HP", "+" + std::to_string(power.health), tip::Tone::Green));
        }
        if (power.dealt > 1.0) {
            what.rows.push_back(said("Damage", "+" + percent(power.dealt - 1.0), tip::Tone::Green));
            what.rows.push_back(prose("attacking and wizardry, skills too"));
        }
        // The Angel's price (ours): his own blows lighter.
        if (power.dealt < 1.0) {
            what.rows.push_back(said("Damage", "-" + percent(1.0 - power.dealt), tip::Tone::Red));
            what.rows.push_back(prose("attacking and wizardry, skills too"));
        }
        // The Imp's price (sim::PetPower::lifeCost).
        if (power.lifeCost > 0) {
            what.rows.push_back(said("Life", "-" + std::to_string(power.lifeCost), tip::Tone::Red));
            what.rows.push_back(prose("for every blow you land; no bonus at " +
                                      std::to_string(power.lifeCost) + " Life or less"));
        }
        // The Horn of Uniria's ride (sim::kRideFactor): off a safe tile, on any map
        // (sim::rideMap), in a fight or out.
        if (power.mount) {
            what.rows.push_back(said("Movement speed",
                                     "+" + percent(double(sim::kRideFactor / sim::kRunFactor) - 1.0),
                                     tip::Tone::Green));
            what.rows.push_back(prose("over running, ridden outside town"));
            what.rows.push_back(prose("in Lorencia, Devias, Noria and Atlans, never in a dungeon",
                                      one.idle ? tip::Tone::Red : tip::Tone::Gray));
            if (one.idle) what.rows.push_back(prose("Cannot be ridden here", tip::Tone::Red));
        }
        if (lifted) what.rows.push_back(prose("its price lifted by your Kinship rune"));
        what.rows.push_back(prose("loses Life as you take damage, and is gone at none"));
        sheet.sections.push_back(what);
        sheet.wear = "Life " + std::to_string(one.life) + " / " + std::to_string(one.lifeMost);
        return sheet;
    }
    if (one.chill) {
        // The Ice Monster's blow (sim::kChillers): half his pace for ten seconds, not renewed
        // while it is on (Realm::chillHero).
        sheet.name = "Chilled";
        sheet.nameTone = tip::Tone::Blue;
        sheet.base = "DEBUFF";
        tip::Section what;
        what.rows.push_back(said("Movement speed",
                                 "-" + std::to_string(int(std::lround((1.0f - sim::kChillFactor) *
                                                                     100.0f))) + "%",
                                 tip::Tone::Red));
        what.rows.push_back(prose("an Ice Monster's blow; it wears off on its own"));
        sheet.sections.push_back(what);
        return sheet;
    }
    if (one.poison) {
        // 0.75's poison on him: a share of what he has left every three seconds.
        sheet.name = "Poisoned";
        sheet.nameTone = tip::Tone::Green;
        sheet.base = "DEBUFF";
        tip::Section what;
        char share[32];
        std::snprintf(share, sizeof share, "%d%% of health left",
                      int(sim::kHeroPoisonShare * 100.0f + 0.5f));
        what.rows.push_back(said("Every 3 s", share, tip::Tone::Red));
        what.rows.push_back(prose("never the last point; an Antidote clears it"));
        sheet.sections.push_back(what);
        return sheet;
    }
    if (one.ale) {
        // OpenMU's effect: the twenty on AttackSpeedAny, for eighty seconds.
        sheet.name = "Ale";
        sheet.nameTone = tip::Tone::Yellow;
        sheet.base = "POTION";
        tip::Section what;
        what.rows.push_back(said("Attack speed", "+" + std::to_string(sim::kAleSpeed),
                                 tip::Tone::Green));
        sheet.sections.push_back(what);
        return sheet;
    }
    if (one.potion >= 0) {
        // A potion going down: its worth in three instalments over a second (Realm::useItem).
        const bool mana = one.potion == 1;
        sheet.name = mana ? "Mana Potion" : "Healing Potion";
        sheet.nameTone = mana ? tip::Tone::Blue : tip::Tone::Red;
        sheet.base = "POTION";
        tip::Section what;
        what.rows.push_back(said(mana ? "Mana" : "Health", "+" + panel::grouped(one.amount),
                                 tip::Tone::Green));
        what.rows.push_back(prose("still to come, over a second"));
        sheet.sections.push_back(what);
        return sheet;
    }
    if (one.frenzy) {
        // The Dungeon's boots rune, ours and Diablo 3's shape: a stack a wound, each
        // sim::kFrenzyStackSpeed on the swing and the cast, four seconds from the last.
        sheet.name = "Frenzy";
        sheet.nameTone = tip::Tone::Yellow;
        sheet.base = "RUNE";
        tip::Section what;
        const std::string speed = "+" + std::to_string(one.stacks * sim::kFrenzyStackSpeed);
        what.rows.push_back(said("Stacks",
                                 std::to_string(one.stacks) + " of " +
                                     std::to_string(sim::kFrenzyMostStacks),
                                 tip::Tone::Green));
        what.rows.push_back(said("Attack speed", speed, tip::Tone::Green));
        what.rows.push_back(said("Casting speed", speed, tip::Tone::Green));
        sheet.sections.push_back(what);
        return sheet;
    }
    const sim::SkillRow* row = sim::skillNumbered(one.skill);
    if (row == nullptr) return {};
    sheet.name = row->name;
    sheet.nameTone = tip::Tone::Blue;
    const sim::HeroPoints& has = hero_->points;
    if (row->mightTicks > 0) {
        // The elf's Greater Damage: `3 + energy / 7` on every blow after the defence
        // (sim::mightOf), held at the cast.
        sheet.base = "AURA \xc2\xb7 FAIRY ELF";
        const int held = hero_->might;
        const int now = sim::mightOf(has);
        tip::Section what;
        what.rows.push_back(said("Damage", "+" + std::to_string(held), tip::Tone::Green));
        what.rows.push_back(prose("on every blow, after the defence; arrows and spells too"));
        sheet.sections.push_back(what);
        tip::Section sum;
        sum.kicker = "Bonus";
        sum.rows.push_back(said("Base", "3", tip::Tone::White));
        sum.rows.push_back(said("Energy  " + std::to_string(has.energy) + " / 7",
                                std::to_string(std::max(0, has.energy) / 7), tip::Tone::White));
        sum.rows.push_back(said("Total", std::to_string(now), tip::Tone::Yellow));
        sheet.sections.push_back(sum);
        if (now != held) {
            sheet.note = "Cast again for +" + std::to_string(now);
            sheet.noteTone = now > held ? tip::Tone::Green : tip::Tone::Red;
        }
        return sheet;
    }
    const bool barrier = row->number == sim::skill::kSoulBarrier;
    const bool ward = row->number == sim::skill::kGreaterDefense;
    sheet.base = barrier ? "AURA \xc2\xb7 DARK WIZARD"
                 : ward  ? "AURA \xc2\xb7 FAIRY ELF"
                         : "AURA \xc2\xb7 DARK KNIGHT";

    // What it does now, which was fixed at the cast.
    const float held = 1.0f - hero_->boonDamageTaken;
    tip::Section what;
    what.rows.push_back(said("Absorbs", percent(held), tip::Tone::Green));
    what.rows.push_back(prose("of every blow that gets past armour"));
    sheet.sections.push_back(what);

    // And how it is reckoned, with his own numbers: the points, then the curve. Strength is the
    // knight's main stat and energy the wizard's (`guardPoints`, `barrierPoints`); the elf's ward
    // is agility with energy at half, and a flat 15 where a shield would be (`wardPoints`).
    const int shield = hero_->shieldDefense;
    const float now = sim::boonShare(*row, has, shield);
    tip::Section sum;
    float points = 0.0f;
    if (ward) {
        points = sim::wardPoints(has);
        sum.kicker = "Ward points";
        sum.rows.push_back(said("No shield", std::to_string(int(sim::kWardShieldPoints)),
                                tip::Tone::White));
        sum.rows.push_back(said("Agility  " + times(has.agility, 1.1f),
                                std::to_string(int(std::lround(1.1 * has.agility))),
                                tip::Tone::White));
        sum.rows.push_back(said("Energy  " + times(has.energy, 0.5f),
                                std::to_string(int(std::lround(0.5 * has.energy))),
                                tip::Tone::White));
    } else {
        const int main = barrier ? has.energy : has.strength;
        points = barrier ? sim::barrierPoints(has, shield) : sim::guardPoints(has, shield);
        sum.kicker = barrier ? "Barrier points" : "Guard points";
        sum.rows.push_back(said("Shield  " + times(shield, 5.0f), std::to_string(5 * shield),
                                tip::Tone::White));
        sum.rows.push_back(said(std::string(barrier ? "Energy  " : "Strength  ") +
                                    times(main, 1.1f),
                                std::to_string(int(std::lround(1.1 * main))), tip::Tone::White));
        sum.rows.push_back(said("Agility  " + times(has.agility, 0.5f),
                                std::to_string(int(std::lround(0.5 * has.agility))),
                                tip::Tone::White));
    }
    sum.rows.push_back(said("Total", std::to_string(int(std::lround(points))), tip::Tone::Yellow));
    char curve[96];
    std::snprintf(curve, sizeof curve, "%d%% x %.0f / (%.0f + 150) = %s  (cap %d%%)",
                  int(sim::kGuardCap * 100.0f + 0.5f), double(points), double(points),
                  percent(now).c_str(), int(sim::kGuardCap * 100.0f + 0.5f));
    sum.rows.push_back(prose(curve));
    sheet.sections.push_back(sum);

    // Held at the cast: a shield changed or points spent since show in the sum and not in the
    // share until he casts it again, and the card says so rather than disagree with itself.
    if (percent(now) != percent(held)) {
        sheet.note = "Cast again for " + percent(now);
        sheet.noteTone = now > held ? tip::Tone::Green : tip::Tone::Red;
    }
    return sheet;
}

tip::Sheet Hud::lifeSheet() const {
    tip::Sheet sheet;
    sheet.wide = kCardWide;
    sheet.name = "Life";
    sheet.nameTone = tip::Tone::Red;
    tip::Section now;
    now.rows.push_back(said("Life", of(hero_->health, hero_->maxHealth), tip::Tone::White));
    sheet.sections.push_back(now);

    // `reckon`'s own sum: the class's base, its rate a level and its rate a point of vitality,
    // and the excellent armour's four percent a piece on top.
    const sim::ClassRow& row = sim::rowOf(hero_->kin);
    tip::Section most;
    most.kicker = "Maximum";
    most.rows.push_back(said("Base", std::to_string(int(row.baseHealth)), tip::Tone::White));
    most.rows.push_back(said("Level  " + times(hero_->level, row.healthPerLevel),
                             std::to_string(int(float(hero_->level) * row.healthPerLevel)),
                             tip::Tone::White));
    most.rows.push_back(said("Vitality  " + times(hero_->points.vitality, row.healthPerVitality),
                             std::to_string(int(float(hero_->points.vitality) *
                                                row.healthPerVitality)),
                             tip::Tone::White));
    if (hero_->excel.healthRate != 1.0) {
        most.rows.push_back(said("Excellent armour", "+" + percent(float(hero_->excel.healthRate - 1.0)),
                                 tip::Tone::Green));
    }
    sheet.sections.push_back(most);

    tip::Section back;
    back.kicker = "Recovery";
    back.rows.push_back(prose(percent(sim::kHealthRecoveryInSafeZone) + " of the maximum every " +
                              std::to_string(sim::kRecoverEveryTicks / 20) +
                              " s, in town only. Potions everywhere else."));
    sheet.sections.push_back(back);
    sheet.wear = percent(fraction(hero_->health, hero_->maxHealth)) + " full";
    sheet.worn = fraction(hero_->health, hero_->maxHealth);
    sheet.wearTone = tip::Tone::Red;
    return sheet;
}

tip::Sheet Hud::manaSheet() const {
    tip::Sheet sheet;
    sheet.wide = kCardWide;
    sheet.name = "Mana";
    sheet.nameTone = tip::Tone::Blue;
    tip::Section now;
    now.rows.push_back(said("Mana", of(hero_->mana, hero_->maxMana), tip::Tone::White));
    sheet.sections.push_back(now);

    // `maximumMana`'s three parts, found by asking it with each input taken away: its rates are
    // its own and are not copied here.
    const sim::HeroPoints bare{hero_->points.strength, hero_->points.agility,
                               hero_->points.vitality, 0};
    const int base = sim::maximumMana(hero_->kin, 0, {});
    const int byLevel = sim::maximumMana(hero_->kin, hero_->level, {}) - base;
    const int byEnergy = sim::maximumMana(hero_->kin, hero_->level, hero_->points) -
                         sim::maximumMana(hero_->kin, hero_->level, bare);
    tip::Section most;
    most.kicker = "Maximum";
    if (base > 0) most.rows.push_back(said("Base", std::to_string(base), tip::Tone::White));
    most.rows.push_back(said("Level  " + std::to_string(hero_->level), std::to_string(byLevel),
                             tip::Tone::White));
    most.rows.push_back(said("Energy  " + std::to_string(hero_->points.energy),
                             std::to_string(byEnergy), tip::Tone::White));
    if (hero_->excel.manaRate != 1.0) {
        most.rows.push_back(said("Excellent armour", "+" + percent(float(hero_->excel.manaRate - 1.0)),
                                 tip::Tone::Green));
    }
    sheet.sections.push_back(most);

    tip::Section back;
    back.kicker = "Recovery";
    back.rows.push_back(prose(percent(sim::kManaRecoveryShare, 1) + " of the maximum every " +
                              std::to_string(sim::kRecoverEveryTicks / 20) + " s, anywhere."));
    back.rows.push_back(prose(percent(sim::kAttackManaShare) + " back on every landed attack."));
    sheet.sections.push_back(back);
    sheet.wear = percent(fraction(hero_->mana, hero_->maxMana)) + " full";
    sheet.worn = fraction(hero_->mana, hero_->maxMana);
    sheet.wearTone = tip::Tone::Blue;
    return sheet;
}

tip::Sheet Hud::shieldSheet() const {
    tip::Sheet sheet;
    sheet.wide = kCardWide;
    sheet.name = "Shield";
    sheet.nameTone = tip::Tone::Yellow;
    tip::Section now;
    now.rows.push_back(said("Shield", of(hero_->sd, hero_->maxSd), tip::Tone::White));
    now.rows.push_back(prose("Takes " + percent(sim::kShieldShare) +
                             " of every blow until it breaks; the rest reaches life."));
    sheet.sections.push_back(now);

    // `maximumShield`: 1.2 a point of all four stats, the defence, and the level squared over 30.
    const sim::HeroPoints& has = hero_->points;
    const int stats = has.strength + has.agility + has.vitality + has.energy;
    tip::Section most;
    most.kicker = "Maximum";
    most.rows.push_back(said("All stats  " + times(stats, 1.2f),
                             std::to_string(int(1.2f * float(stats))), tip::Tone::White));
    most.rows.push_back(said("Defense", std::to_string(hero_->stats.defense), tip::Tone::White));
    most.rows.push_back(said("Level  " + times(hero_->level, float(hero_->level)) + " / 30",
                             std::to_string(hero_->level * hero_->level / 30), tip::Tone::White));
    sheet.sections.push_back(most);

    tip::Section back;
    back.kicker = "Recovery";
    back.rows.push_back(prose(percent(sim::kShieldRecovery) + " of the maximum every " +
                              std::to_string(sim::kRecoveryTicks / 20) + " s, in town only."));
    sheet.sections.push_back(back);
    sheet.wear = percent(fraction(hero_->sd, hero_->maxSd)) + " full";
    sheet.worn = fraction(hero_->sd, hero_->maxSd);
    sheet.wearTone = tip::Tone::Yellow;
    return sheet;
}

tip::Sheet Hud::experienceSheet() const {
    tip::Sheet sheet;
    sheet.wide = kCardWide;
    sheet.name = "Experience";
    sheet.nameTone = tip::Tone::Yellow;
    const uint64_t at = sim::neededExperience(hero_->level);
    const uint64_t next = sim::neededExperience(hero_->level + 1);
    const uint64_t into = hero_->experience > at ? hero_->experience - at : 0;
    const uint64_t span = next > at ? next - at : 1;
    tip::Section now;
    now.rows.push_back(said("Level", std::to_string(hero_->level), tip::Tone::White));
    now.rows.push_back(said("This level", panel::commas((long long)into) + " / " +
                                              panel::commas((long long)span),
                            tip::Tone::White));
    now.rows.push_back(said("To the next", panel::commas((long long)(span - std::min(into, span))),
                            tip::Tone::Yellow));
    sheet.sections.push_back(now);
    sheet.wear = std::to_string(segment()) + " of " + std::to_string(kTenths) + " marks";
    sheet.worn = float(double(into) / double(span));
    sheet.wearTone = tip::Tone::Yellow;
    return sheet;
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
        if (i == liftedQuick_) {
            // Lifted: the box reads as left, and the bottle is at the pointer.
            canvas_.rect(box, gfx::rgba(0.0f, 0.0f, 0.0f, 0.7f));
            continue;
        }
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

    // A box that just answered. Drawn after the pictures and the counts so the ring is the last
    // thing on the box, and read off the live clock rather than off `drawn_`: the rebuild is
    // gated in twelfths, but what is drawn on the frame it fires is where the ring really is.
    const auto ring = [&](const Box& box, float struck) {
        if (struck >= kStrike) return;
        const float t = std::clamp(struck / kStrike, 0.0f, 1.0f);
        // Out fast and slowing, which is the shape of every ring that reads as a strike rather
        // than as a pulse: the distance eases out, the brightness falls off squared so the tail
        // is gone well before the ring stops moving.
        const float ease = 1.0f - (1.0f - t) * (1.0f - t);
        const float fade = (1.0f - t) * (1.0f - t);
        const float out = ease * kStrikeOut * kUnit * s.scale;
        const float line = std::max(1.0f, kStrikeLine * kUnit * s.scale);
        if (t < 0.5f) {
            const float sheen = kStrikeWash * (1.0f - t * 2.0f);
            canvas_.rect(box, gfx::rgba(1.0f, 0.90f, 0.55f, sheen));
        }
        canvas_.outline(box.grown(out), line, gfx::rgba(1.0f, 0.87f, 0.45f, fade));
    };
    for (int i = 0; i < kQuickKeys; ++i) ring(plate(s, boxPx(kFirstQuick + i)), struck_[i]);

    // The skill boxes: the icon MuDream's own sheet gives the skill, the cooldown wiped down over
    // it, and what is left of it in seconds.
    //
    // A WIPE AND NOT A RADIAL SWEEP. LoL and WoW both turn a hand round the icon; this fills the
    // box from the top down as the cooldown runs, because a wipe is one rectangle and a sweep is
    // a triangle fan the canvas has no primitive for. What both conventions have in common -- and
    // what actually reads at a glance -- is that the dark shrinks as the skill comes back, and
    // that is kept.
    // The seconds over a cooling box. Twenty-six plate pixels and not thirteen: at thirteen it
    // was ten pixels of figure at 1080 lines, which is smaller than the key's own letter under
    // the box and too small for the one number on this frame a player reads mid-fight. The
    // user, 2026-09-23.
    const float skillSize = std::round(26.0f * kUnit * s.scale);
    for (int i = 0; i < kSkillBoxes; ++i) {
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
        // A cooling skill is not throwable either, so it goes cold too, and the wipe over it
        // still says how long: a long wait with a thin wipe left the rest of the icon at full
        // colour and the box read as ready. The user, 2026-09-29.
        const bool ready = one.affordable && one.cooling <= 0.0f;
        const uint32_t tint = ready ? 0xFFFFFFFFu : gfx::rgba(0.42f, 0.44f, 0.52f, 1.0f);
        if (icon.valid()) canvas_.image(icon, box, tint);
        if (!ready) canvas_.rect(box, gfx::rgba(0.0f, 0.0f, 0.02f, 0.45f));
        if (one.cooling > 0.0f) {
            const float tall = box.h * std::min(1.0f, one.cooling);
            canvas_.rect({box.x, box.y, box.w, tall}, gfx::rgba(0.0f, 0.0f, 0.0f, 0.62f));
            // The figure only while there is more than a second of it: a box flashing "0.3" is
            // noise, and both references stop printing tenths under a second for the same reason.
            // From a minute up it is minutes, "4:59": Defense's five-minute wait read as "300".
            if (one.seconds >= 1.0f) {
                const int whole = int(one.seconds + 0.5f);
                char figure[16];
                if (whole >= 60) {
                    std::snprintf(figure, sizeof figure, "%d:%02d", whole / 60, whole % 60);
                } else {
                    std::snprintf(figure, sizeof figure, "%d", whole);
                }
                canvas_.shadowed(box.midX(), box.midY() + skillSize * 0.35f, skillSize, kInk,
                                 kInkShadow, 1.0f, figure, gfx::Align::Centre, 0.0f);
            }
        }
        // Over the wipe, which starts on the same frame: the sheen lights the dark for a breath
        // and the ring leaves the edge, so the throw reads before the wait does.
        ring(box, skillStruck_[i]);
        if (skillBack_[i] < kBack) {
            const float t = std::clamp(skillBack_[i] / kBack, 0.0f, 1.0f);
            const float ease = 1.0f - (1.0f - t) * (1.0f - t);
            const float in = (1.0f - ease) * kBackIn * kUnit * s.scale;
            const float line = std::max(1.0f, kStrikeLine * kUnit * s.scale);
            canvas_.rect(box, gfx::rgba(1.0f, 1.0f, 0.95f, kBackWash * (1.0f - t) * (1.0f - t)));
            canvas_.outline(box.grown(in), line, gfx::rgba(1.0f, 0.96f, 0.80f, 1.0f - t));
        }
    }

    // What is standing on him: the skill's boon and the Ale, a cell each, packed from the left.
    for (int i = 0; i < kBoons; ++i) {
        const Boon& one = boons_[i];
        if (one.empty()) continue;
        const Box box = plate(s, buffPx(i));
        const char* art = one.pet >= 0          ? petArt(one.pet)
                          : one.poison           ? "buff_poison"
                          : one.chill            ? "buff_ice"
                          : one.ale              ? "buff_ale"
                          : one.frenzy           ? "buff_frenzy"
                          : one.potion == 0      ? "buff_healing"
                          : one.potion == 1      ? "buff_mana"
                          : buffArt(one.skill)   ? buffArt(one.skill)
                                                 : "buff_defense";
        const gfx::Art& icon = arts.get(art);
        canvas_.rect(box, kBuffBack);
        // A mount it is not ridden here: cold and dark, the skill box's disabled state.
        if (icon.valid()) {
            canvas_.image(icon, box,
                          one.idle ? gfx::rgba(0.42f, 0.44f, 0.52f, 1.0f) : 0xFFFFFFFFu);
        }
        if (one.idle) canvas_.rect(box, gfx::rgba(0.0f, 0.0f, 0.02f, 0.45f));
        const bool debuff = one.debuff();
        canvas_.outline(box, std::max(1.0f, s.scale), debuff ? kDebuffEdge : kBuffEdge);
        // Diablo 4's cell (the user's pick, 2026-10-02), at the strip's own size ("it has to be
        // same size as other buffs"). What is gone of a buff's time is a dark
        // sweep down over the icon from its top, so the lit part left is the time left; a
        // Frenzy's stacks are one clean figure in the bottom right corner. A pet has no clock --
        // its hairline under the cell is its Life -- and a debuff keeps its big centred seconds
        // over a wash (the user: "just use big number at center with some opacity background for
        // debuffs time").
        const float left = std::clamp(one.share, 0.0f, 1.0f);
        if (one.pet >= 0) {
            const float line = std::max(1.0f, 2.0f * kUnit * s.scale);
            canvas_.rect({box.x, box.bottom() - line, box.w * left, line}, kBuffLeft);
            continue;
        }
        if (debuff) {
            canvas_.rect({box.x, box.bottom() - std::max(1.0f, 2.0f * kUnit * s.scale),
                          box.w * left, std::max(1.0f, 2.0f * kUnit * s.scale)},
                         kDebuffLeft);
            if (one.seconds >= 1.0f) {
                const int whole = int(one.seconds + 0.5f);
                char figure[16];
                if (whole >= 60) {
                    std::snprintf(figure, sizeof figure, "%d:%02d", whole / 60, whole % 60);
                } else {
                    std::snprintf(figure, sizeof figure, "%d", whole);
                }
                canvas_.rect(box, gfx::rgba(0.0f, 0.0f, 0.0f, 0.5f));
                // In the label face, not the default one (the user: "number to big, use other
                // font").
                const float big = std::round(20.0f * kUnit * s.scale);
                controls::label(canvas_, box.midX() - controls::labelWidth(big, figure) * 0.5f,
                                box.midY() + big * 0.33f, big, kInk, figure);
            }
            continue;
        }
        const float gone = std::round(box.h * (1.0f - left));
        if (gone > 0.0f) canvas_.rect({box.x, box.y, box.w, gone}, gfx::rgba(0.0f, 0.0f, 0.0f, 0.58f));
        if (one.frenzy && one.stacks > 0) {
            char count[8];
            std::snprintf(count, sizeof count, "%d", one.stacks);
            const float size = std::round(19.0f * kUnit * s.scale);
            const float in = std::round(4.0f * kUnit * s.scale);
            canvas_.shadowed(box.right() - in, box.bottom() - in * 2.0f, size, kInk, kInkShadow,
                             std::max(1.0f, s.scale), count, gfx::Align::Right, 0.0f);
        }
    }

    // ---- the list, open above the plate ------------------------------------------------------
    //
    // Drawn by the HUD and not by a window of its own because the list belongs to the bar: it is
    // centred on the gold box, it is measured off the plate, and what it is FOR is filling the
    // four keys six inches below it. What it is drawn IN is the item card's own container --
    // see the note on the metrics above, and the user's rule that it be the same style.
    if (fanOpen_ && !fan_.empty()) {
        const float u = tip::unit();
        const Box rail = listBox(s, fan_.size(), width_);
        tip::glass(canvas_, rail, u);

        for (size_t i = 0; i < fan_.size(); ++i) {
            const FanCell& one = fan_[i];
            const Box cell = cellBox(s, fan_.size(), width_, int(i));
            const bool over = now_.fanOver == int(i);
            canvas_.rect(cell, over ? kCellOver : kCellBack);
            canvas_.outline(cell, std::max(1.0f, u), over ? kCellEdgeOver : kCellEdge);

            // Always at full colour: see the note on FanCell. What he can throw right now is
            // the BAR's question, and the bar answers it a foot below this.
            const gfx::Art& art = arts.get("skill_" + std::to_string(one.number));
            const Box icon = cell.grown(-kCellRim * u);
            if (art.valid()) canvas_.image(art, icon);

            // The key it is already on, in a chip at the cell's bottom-right -- the one thing
            // the list has to say that the picture cannot.
            // The right button's slot is the gold box, which has no letter under it on the
            // plate; its chip names the button instead.
            const char* cap = one.key == kRightSlot ? "RMB"
                              : one.key >= 0 && one.key < kSkillKeys ? kKeys[one.key]
                                                                     : nullptr;
            if (cap != nullptr) {
                const Box chip{cell.right() - (kChipWide + 2.0f) * u,
                               cell.bottom() - (kChipTall + 2.0f) * u, kChipWide * u,
                               kChipTall * u};
                controls::keycap(canvas_, chip, cap, tip::unit());
            }
        }
    }

    // A lifted potion box, at the pointer: its own square of the stage's picture, so the bottle
    // moves and not a word.
    if (liftedQuick_ >= 0 && quick_[liftedQuick_].item >= 0) {
        const Box from = boxPx(kFirstQuick + liftedQuick_);
        const Box box = plate(s, from);
        const Box to{now_.pointerX - box.w * 0.5f, now_.pointerY - box.h * 0.5f, box.w, box.h};
        if (picture.valid() && !standing_.empty()) {
            const float sx = picture.width / kPlateW, sy = picture.height / kPlateH;
            canvas_.region(picture, to, {from.x * sx, from.y * sy, from.w * sx, from.h * sy},
                           gfx::rgba(1.0f, 1.0f, 1.0f, 0.85f));
        } else {
            const std::string& label = quick_[liftedQuick_].label;
            canvas_.shadowed(to.midX(), to.midY(), quickSize, kInk, kInkShadow, 1.0f,
                             label.substr(0, std::min<size_t>(label.size(), 5)),
                             gfx::Align::Centre, 0.0f);
        }
    }

    // What the pointer is holding, at the pointer: drawn last so it lies over the list it came
    // out of and over the key it is going to.
    if (carrying_ != 0) {
        const gfx::Art& icon = arts.get("skill_" + std::to_string(carrying_));
        const float side = kCell * tip::unit() * 0.9f;
        if (icon.valid()) {
            canvas_.image(icon, {now_.pointerX - side * 0.5f, now_.pointerY - side * 0.5f, side,
                                 side}, gfx::rgba(1.0f, 1.0f, 1.0f, 0.85f));
        }
    }

    // The keys, relabelled. The painted figure is covered by the rail's own dark and the key
    // that really fires the box stood in its place as a Sanctuary key cap (2026-09-28): bone on
    // iron for skills and potions alike, where the plate had them in its grey and its gold, and
    // gold is the loot's.
    for (int i = 0; i < kSlots; ++i) {
        if (kKeys[i] == nullptr) continue;
        const Box box = boxPx(i);
        const float cx = box.midX();
        const Box cell = plate(s, {cx - kLabelWide * 0.5f, kLabelTop, kLabelWide, kLabelTall});
        canvas_.rect(cell, kLabelCell);
        if (kKeys[i][0] == '\0') continue;
        // A little taller than the painted cell and square-ish, centred on it, so the letter
        // can be read at a glance down the bar.
        const float side = std::round(cell.h * 1.2f);
        controls::keycap(canvas_, {std::round(cell.midX() - side * 0.6f), std::round(cell.midY() - side * 0.5f),
                                   std::round(side * 1.2f), side},
                         kKeys[i], tip::unit());
    }

    // The side buttons: a disc each, MuMain's icon on it, in the state the pointer and the
    // window behind it put it in. Four states stacked for the main frame's two (closed,
    // closed-hovered, open, open-hovered), two for the chat's and the menu's.
    const gfx::Art& disc = arts.get("hud_disc");
    for (int i = 0; i < kButtonCount; ++i) {
        const ButtonRow& row = kButtons[i];
        const Box box = plate(s, buttonPx(i));
        const bool hovered = now_.hovered == 100 + i;
        const bool open = (i == 2 && now_.inventory) || (i == 3 && now_.character) ||
                          (i == kQuestButton && now_.quest);
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
    {
        // An entry of the open rail gets the card first, and over the rail rather than over the
        // key: the pill is what is being read. Nothing is drawn while a drag is in the air --
        // a card under the icon you are carrying is a card in the way.
        const int overCell = fanAt(now_.pointerX, now_.pointerY);
        if (overCell >= 0 && carrying_ == 0 && !fanSheet_.empty()) {
            const Box pill = cellBox(s, fan_.size(), width_, overCell);
            tip::draw(tip_, fanSheet_, pill, now_.width, now_.height);
            return;
        }
    }
    if (now_.tip) {
        const float px = now_.pointerX, py = now_.pointerY;
        // A skill box gets the card, anchored on the TOP of the box rather than at the pointer,
        // which is where `tip::draw` wants the thing being described: the box is never under the
        // card that explains it.
        const int overSkill = skillAt(px, py);
        if (overSkill >= 0 && !sheets_[overSkill].empty()) {
            const Box box = plate(s, boxPx(overSkill));
            tip::draw(tip_, sheets_[overSkill], box, now_.width, now_.height);
            return;
        }
        // The buff: what is on him, what it does, how that is reckoned and for how much longer,
        // on the skill card's own glass (the user, 2026-09-28).
        if (const int over = boonAt(px, py); over >= 0) {
            const Box cell = plate(s, buffPx(over));
            const tip::Sheet sheet = boonSheet(boons_[over], arts);
            if (!sheet.empty()) {
                tip::draw(tip_, sheet, cell, now_.width, now_.height);
                return;
            }
        }
        // And the plate's own readings: life, mana, the shield and experience, each on the same
        // card, standing on what it describes.
        tip::Sheet sheet;
        float anchorX = px, anchorY = plate(s, {0.0f, 0.0f, kPlateW, kPlateH}).y;
        if (const Box life = plate(s, kLifeHole); life.has(px, py)) {
            sheet = lifeSheet();
            anchorX = life.midX();
            anchorY = life.y;
        } else if (const Box mana = plate(s, kManaHole); mana.has(px, py)) {
            sheet = manaSheet();
            anchorX = mana.midX();
            anchorY = mana.y;
        } else if (hero_->maxSd > 0 && plate(s, kShieldBar).has(px, py)) {
            sheet = shieldSheet();
            anchorY = plate(s, kShieldBar).y;
        } else {
            sheet = experienceSheet();
        }
        tip::draw(tip_, sheet, anchorX, anchorY, now_.width, now_.height);
    }
}

}  // namespace mu::game
