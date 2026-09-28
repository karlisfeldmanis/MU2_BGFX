// The frame along the bottom of the screen: life and mana in two diamond gems, a row of
// eleven boxes between them, the level as a hairline beneath, and the buttons that open the
// windows.
//
// MU2's `client/core/Hud.cs`, which is `CNewUIMainFrameWindow` in MuDream's clothes. Kept of it
// is exactly what it kept of MuMain: the order the pieces go down in, a gauge filled by CROPPING
// its texture from the waterline rather than by scaling it (`RenderLifeMana`), and a level cut
// into ten (`buildExpSegment`). Every position is MuDream's base-plate pixel, and `kUnit` is the
// one number that turns those into MU's 640x480.
//
// What is not drawn, and why, in the order Hud.cs draws it:
//   * the shield bar: 0.75 has no shield, and MU2's bar was Season 3's carried in on purpose;
//     this sim is smaller and has no such pool, so the bar is not there to hide.
//   * the ability bar: off in MU2 too (`AbilityShown`), because nothing in 0.75 spends it.
//   * the buff strip and the skill fan: there are no skills yet (PLAN.md, their own sprint).
//
// The keys under the boxes are this sprint's: potions on 1 to 4 and skills on Q W E R, decided
// by the user on 2026-09-21. MuDream's plate paints the other arrangement, so its labels are
// covered with the rail's own dark and the real key printed over them.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/ui/panel.h"
#include "game/ui/stage.h"
#include "game/ui/tip.h"
#include "gfx/interface.h"
#include "sim/realm.h"

namespace mu::game {

// The pointer as the windows read it, in backbuffer pixels, with its two edges.
struct Pointer {
    float x = -1.0f, y = -1.0f;
    bool pressed = false;   // the left button went down this frame
    bool released = false;  // and came up
    bool held = false;
    bool rightPressed = false;
};

class Hud {
public:
    enum class Button { Menu, Chat, Inventory, Character };

    // The potion boxes, keyed 1 to 5.
    static constexpr int kQuickKeys = 5;

    // What one of the potion boxes shows: the row bound to it (or -1), what it is called
    // and how many of it and what may stand in for it he carries. Given by the desk, which owns
    // the binding; the frame draws what it is handed.
    struct Quick {
        int32_t item = -1;
        std::string label;
        int count = 0;
        bool operator==(const Quick& o) const {
            return item == o.item && count == o.count;
        }
    };
    void setQuick(int key, const Quick& quick) {
        if (key >= 0 && key < kQuickKeys) quick_[key] = quick;
    }
    // Which potion box a point is over, 0 to 4, or -1: where a drag from the bag binds.
    int quickAt(float x, float y) const;

    // A box that just fired. The realm answers a potion silently -- the bottle leaves the bag,
    // the gem fills, and the box itself says nothing -- so a key pressed in a fight and a key
    // pressed at a thing that was not there looked the same. This is the box's own answer: a
    // ring struck on its edge that steps outward and fades in a quarter of a second. Struck
    // only when the realm HONOURED the use, so it is a receipt and not an echo of the key.
    void strikeQuick(int key);
    // The same ring on a skill box, struck when the realm THREW the skill -- not on the key, which
    // the realm holds until he is in reach and refuses while the skill cools.
    void strikeSkill(int key);
    // And the other way round, when a skill comes back: off its cooldown AND with the mana for
    // it. The throw's ring leaves the edge; this one closes onto it from outside, with a bright
    // breath over the icon, so "gone" and "back" never read as each other.
    void readySkill(int key);

    // The five skill boxes, Q W E R T. The plate paints six -- five keys and the gold one the
    // list opens from -- and the fifth was drawn dead for two sprints on a note that said the
    // list would take its place. The list took the GOLD box's place instead, so T is a key like
    // the other four (the user, 2026-09-23, trying to drag a skill onto it).
    static constexpr int kSkillKeys = 5;
    // And the sixth box, the gold one, which is the RIGHT MOUSE BUTTON's quick slot: the plate's
    // own place for "the skill in hand", which MU casts from with a right-click. It holds a skill
    // as the keys do and is filled the same way -- dragged out of the list -- and right-clicking a
    // monster throws it (the user, 2026-09-28: *"right click is quick slot but for right
    // click"*). Index 5 is `kGoldBox`, so the box and the slot are one number.
    static constexpr int kRightSlot = 5;
    static constexpr int kSkillBoxes = kSkillKeys + 1;

    // What one skill box shows. Given by the desk, off the realm: the skill's number (0 for an
    // empty box), the art key for its icon, how much of its cooldown is left as a fraction and in
    // seconds, and whether the mana for it is there. The frame draws what it is handed and asks
    // nothing (sprint 7's mirror).
    struct Skill {
        int32_t number = 0;
        std::string icon;
        float cooling = 0.0f;   // 1 just thrown, 0 ready
        float seconds = 0.0f;   // what is left, for the figure over the icon
        bool affordable = true;
        bool operator==(const Skill& o) const {
            // The cooldown is compared in tenths, which is what stops the plate rebuilding on
            // every frame of a cooldown: a sweep that steps ten times a second reads as smooth
            // and costs ten redraws a second instead of five hundred. Nothing else here moves.
            return number == o.number && affordable == o.affordable &&
                   int(seconds * 10.0f) == int(o.seconds * 10.0f) &&
                   int(cooling * 40.0f) == int(o.cooling * 40.0f);
        }
    };
    void setSkill(int key, const Skill& skill) {
        if (key >= 0 && key < kSkillBoxes) skill_[key] = skill;
    }
    // The card the box shows when the pointer rests on it, built by the desk off the realm: the
    // frame draws what it is handed and works nothing out (sprint 7's mirror). It is the ITEM
    // tooltip's own card -- `tip::Sheet`, the design the user chose on 2026-09-22 -- rather than
    // the two-line strip the gauges use, because what a skill has to say is a table: what it
    // does, what it multiplies the blow by and where that came from, what it costs, how long the
    // wait is and why the key is dark.
    void setSkillSheet(int key, const tip::Sheet& sheet) {
        if (key >= 0 && key < kSkillBoxes) sheets_[key] = sheet;
    }
    // ---- the buff strip ----------------------------------------------------------------------
    // What is standing on him, drawn as a small icon above the shield bar's left end -- where
    // MuDream keeps its own, a 30-pixel square edged and spaced. **A strip at all is this
    // bench's**, as it was MU2's: 0.75's client draws no status icons and MuMain gives skill 18
    // no buff to draw -- `NewUIBuffWindow` is the later thing both borrow from.
    //
    // A row of cells, left to right in the order the desk hands them: the skill's boon, then the
    // Ale. The Ale's cell is as much this project's as the strip is -- MuMain gives the Ale no
    // eBuffState and tints the hero red instead (ZzzCharacter.cpp:9320) -- and the user asked for
    // it (2026-09-27): a thing drunk for eighty seconds wants to say how long it has left.
    struct Boon {
        int32_t skill = 0;    // MU's own number, 0 for nothing standing
        bool ale = false;     // or the Ale's, which is no skill
        float seconds = 0.0f; // what is left of it
        float share = 0.0f;   // and that as a fraction of its whole, for the bar under it
        bool empty() const { return skill == 0 && !ale; }
        bool operator==(const Boon& o) const {
            // Tenths, as the cooldown's sweep is compared: a strip that redrew on every frame
            // of four seconds would be eighty redraws for a number that changes forty times.
            return skill == o.skill && ale == o.ale &&
                   int(seconds * 10.0f) == int(o.seconds * 10.0f);
        }
    };
    static constexpr int kBoons = 4;
    // **A channel running**: which skill and how much of it is left, 1 at the cast and 0 at its
    // end, handed over each frame off the realm (Desk). 0 for no channel. Drawn as a bar that
    // drains, centred just above the plate -- the channel is the one thing on the bar that the
    // player is waiting on while he cannot act.
    void setChannel(int32_t skill, float share, float seconds) {
        channelSkill_ = skill;
        channelShare_ = share;
        channelSeconds_ = seconds;
    }
    // The cells for this frame, packed from the left; what is not handed is empty.
    void setBoons(const Boon* boons, int count) {
        for (int i = 0; i < kBoons; ++i) boons_[i] = i < count ? boons[i] : Boon{};
    }

    // Which skill box the pointer is over, or -1, so the desk builds one card and not four.
    // Only a box with something in it: an empty box has no card and nothing to hover.
    int skillAt(float x, float y) const;
    // And which box a point falls in whether or not anything is on it, which is what a drag
    // needs: a skill dropped on an empty key is the whole point of the list.
    int skillSlotAt(float x, float y) const;

    // ---- the fan: the skill list, open above the plate ---------------------------------------
    //
    // MU2's `client/core/Fan.cs`, which is `CNewUISkillList` in MuDream's clothes, and the user
    // asked for it by its shape on 2026-09-23: *"it was a horizontal list above the HUD, when
    // clicked or hovered on the right-click slot."* The cells are laid out from the GOLD box --
    // the one a right-click casts from in MU -- outward, alternating right and left, so the list
    // grows symmetrically around the box it belongs to instead of hanging off one side.
    //
    // The list is the HUD's because it is part of the plate's own furniture: the same boxes, the
    // same pitch, the same sheen under the pointer, and one canvas.
    static constexpr int kGoldBox = 5;  // the box in hand: what opens the list

    // One entry in the list: what it is, what it costs, and which key it is already on. Given
    // by the desk, which owns the keys and asks the realm; the frame draws what it is handed.
    //
    // **No cold state here**, which the bar's boxes do have: the user, 2026-09-23, *"on the
    // skill overlay they can be hot."* The list is the catalogue of what he has learned and it
    // is there to be chosen from and dragged out of, not cast from -- and in a safe zone, where
    // nothing at all can be thrown, every cell in it went grey at once and the list read as
    // broken rather than as a list.
    struct FanCell {
        int32_t number = 0;
        std::string name;
        int mana = 0;
        int key = -1;           // Q W E R T, 5 the right button, or -1 for none
        bool operator==(const FanCell& o) const {
            return number == o.number && mana == o.mana && key == o.key;
        }
    };
    void setFan(bool open, const std::vector<FanCell>& cells, int32_t carrying) {
        fanOpen_ = open;
        fan_ = cells;
        carrying_ = carrying;
    }
    // The card for the entry under the pointer, built by the desk off the realm -- the same card
    // the keys raise, because it is the same skill.
    void setFanSheet(const tip::Sheet& sheet) { fanSheet_ = sheet; }
    // Which box a point falls in at all, 0 to 10, or -1: the gold box is how the list opens.
    int boxAt(float x, float y) const;
    // Which cell of the open list a point falls in, or -1.
    int fanAt(float x, float y) const;
    // Whether a point is over the open list, so a click there is the interface's.
    bool coversFan(float x, float y) const;
    // And whether it is anywhere the list should STAY open for: the list, the gold box it opens
    // from, and the air between them. They do not touch -- the list floats clear of the plate --
    // so a pointer on its way from one to the other crosses a strip that is neither, and the
    // list shut in that strip before it could be reached. The user, 2026-09-23: *"when I hover
    // the skills list I can't reach them because they hide when I leave the right-click
    // button."*
    bool nearFan(float x, float y) const;

    void open(const gfx::Interface& interface, panel::Arts* arts);
    void follow(const sim::Body* hero);
    // The stage the potion boxes' pictures are taken on, the plate's own size: the bag's way of
    // drawing an item, so a bound apple is an apple in its box and not the word. Until its
    // first picture the box shows the name, as the bag does.
    void useStage(Stage* stage) { stage_ = stage; }
    // Pixels per MU unit the plate is drawn at, which is what the stage renders at.
    float pixelsPerUnit() const { return screen_.scale; }
    // The top edge of the plate in backbuffer pixels: what the gain lane stacks up from, so it
    // sits over the HUD at every window size without a second copy of the plate's arithmetic.
    float plateTop() const;

    // A frame: slides the hairline, answers the pointer, and rebuilds the canvas only if what
    // it draws moved. Returns the button pressed this frame, if any, through `pressed`.
    void update(float seconds, float width, float height, const Pointer& pointer,
                bool inventoryOpen, bool characterOpen, bool* toggleInventory,
                bool* toggleCharacter, bool* toggleMenu = nullptr);

    // Whether a point is over the frame -- plate, rail or a side button -- so a click there
    // is the frame's and not the ground's. The plate's transparent corners count: a click in
    // the gap beside a gem's wing that walked the character is a click nobody meant.
    bool covers(float x, float y) const;

    const gfx::Canvas& canvas() const { return canvas_; }
    // The tooltip, on a canvas of its own so the desk can lay it over every window: a tip is
    // drawn at the pointer and runs past its own window's edge, and in the window's canvas the
    // one beside it covered it -- the shelf's tip went under the bag. MU2's tooltip layer.
    const gfx::Canvas& tipCanvas() const { return tip_; }
    // How many times the canvas was rebuilt, for the log's "redraws on change" line.
    uint64_t rebuilds() const { return rebuilds_; }

private:
    // Everything the plate draws that is a number, compared whole to decide a rebuild.
    struct Face {
        float width = 0, height = 0;
        int health = -1, maxHealth = 0, mana = 0, maxMana = 0, shield = 0, maxShield = 0,
            level = 0;
        int gem = 0;
        float slid = 0;
        bool inventory = false, character = false;
        int hovered = -1;  // which button or slot is lit
        bool tip = false;  // a tip is up, so the pointer's place is part of the picture
        float pointerX = 0, pointerY = 0;
        Quick quick[kQuickKeys];
        // Where each box's ring is, in twelfths of its life, or -1 for a box not ringing. In
        // steps and not in seconds for the cooldown's own reason: twelve redraws for a quarter
        // of a second reads as continuous and costs twelve, not a hundred and eighty.
        int struck[kQuickKeys] = {-1, -1, -1, -1, -1};
        int skillStruck[kSkillBoxes] = {-1, -1, -1, -1, -1, -1};
        int skillBack[kSkillBoxes] = {-1, -1, -1, -1, -1, -1};
        Skill skill[kSkillBoxes];
        Boon boons[kBoons];
        int channelSkill = 0;
        int channelLeft = -1;  // hundredths left, so the bar redraws a hundred times at most
        int channelTenths = 0; // and the seconds left, in tenths, for the figure on it
        bool fanOpen = false;
        int fanOver = -1;           // the cell under the pointer
        int32_t carrying = 0;       // what the pointer is holding out of the list
        std::vector<FanCell> fan;   // the entries, in the order they are laid out
        uint16_t picture = 0xFFFF;  // the stage's picture, so its first render is a rebuild
        bool operator==(const Face& o) const;
    };

    void rebuild();
    void slide(float seconds);
    float progress() const;  // into the level, 0 to 1
    int segment() const;     // which tenth
    int hoveredAt(float x, float y) const;
    bool tipAt(float x, float y) const;
    // Which buff cell a point is over, only one with something standing in it, or -1.
    int boonAt(float x, float y) const;

    gfx::Canvas canvas_;
    gfx::Canvas tip_;
    panel::Arts* arts_ = nullptr;
    const sim::Body* hero_ = nullptr;
    panel::Screen screen_;
    Face drawn_;
    Face now_;
    double clock_ = 0.0;
    float slid_ = 0.0f;
    int drawnLevel_ = 0;
    uint64_t rebuilds_ = 0;
    Quick quick_[kQuickKeys];
    // How long ago each box fired, in seconds, counted up and left parked past the ring's life.
    float struck_[kQuickKeys] = {9.0f, 9.0f, 9.0f, 9.0f, 9.0f};
    float skillStruck_[kSkillBoxes] = {9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f};
    float skillBack_[kSkillBoxes] = {9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f};
    Skill skill_[kSkillBoxes];
    Boon boons_[kBoons];
    int32_t channelSkill_ = 0;
    float channelShare_ = 0.0f;
    float channelSeconds_ = 0.0f;
    bool fanOpen_ = false;
    int32_t carrying_ = 0;
    std::vector<FanCell> fan_;
    tip::Sheet fanSheet_;
    float width_ = 0.0f, height_ = 0.0f;
    tip::Sheet sheets_[kSkillBoxes];
    // The plate's hover cards, built when the pointer rests (hud.cpp says what each carries).
    tip::Sheet boonSheet(const Boon& one, panel::Arts& arts) const;
    tip::Sheet lifeSheet() const;
    tip::Sheet manaSheet() const;
    tip::Sheet shieldSheet() const;
    tip::Sheet experienceSheet() const;
    Stage* stage_ = nullptr;
    std::vector<Standing> standing_;
};

}  // namespace mu::game
