// A quest giver's window: what he says, what the quest asks, what it pays, and the answer.
//
// The proposal of 2026-09-28 (claude.ai/artifact/8nQVewJ3f2VkKd74T2ktn2, "Quest dialog"), in
// Sanctuary's controls, and shaped as World of Warcraft's quest frame on the user's word the same
// day ("WoW style with scrollbar for lore"): a window of one fixed size at the left of the frame,
// headed with the quest's name, whose body -- his words, the steps, the rewards -- scrolls in a
// pane with its own bar, while the answers stay fixed at the foot. It shows one of four things,
// by where the hero stands on the quest:
//
//   * the offer -- his story, the steps at nought, the pay -- Not now and Accept;
//   * under way -- a line of his and the steps as they stand -- Farewell;
//   * the hand-in -- his thanks and the pay, a choice of item that must be made before
//     Complete quest lights, from those the hero's class can use;
//   * resting -- handed in, and not his to give again yet: when it will be -- Farewell.
//
// Opened by the realm (a Talk order reaching the giver) and closed by it on any other order; the
// window only asks. Return is the primary answer, Escape the close, the wheel scrolls. Modal while
// it is up, as the Zen box is. Ours: MU 0.75 has no quests (sim/quests.h).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/ui/hud.h"
#include "game/ui/stage.h"
#include "gfx/interface.h"

namespace mu::game {

class Play;

class QuestDialog {
public:
    struct Result {
        bool close = false;
        bool accept = false;
        bool complete = false;
        int choice = -1;  // with `complete`: the quest row's choice index, or -1 for none owed
    };
    void open(const gfx::Interface& interface);
    void close();
    // One frame while the realm has a giver's window open (`quest` >= 0), and once more when it
    // shuts (`quest` < 0) to clear. `wheel` is the mouse wheel this frame, positive toward the
    // screen. `stage` photographs the rewards: the shelf's own, free whenever this is up (a giver
    // is not a counter).
    // `reading`: the quest journal (L), away from the giver -- the page as it stands, a quest ready
    // to hand in shown under way, and one answer, Close; it never accepts or completes.
    void update(float seconds, const Play& play, int quest, bool reading, float width, float height,
                const Pointer& pointer, float wheel, bool enter, bool escape, Stage* stage,
                Result* out);
    bool up() const { return quest_ >= 0; }
    // The page up -- 0 the offer, 1 under way, 2 the hand-in, 3 resting -- or -1: what the
    // giver's voice reads (QuestRow::voice).
    int page() const { return quest_ >= 0 ? int(mode_) : -1; }
    // The window's rectangle on screen, for the pointer the desk keeps from the world.
    bool covers(float x, float y) const;
    // The frame and the body, which scrolls clipped to its pane; drawn in that order.
    const gfx::Canvas& canvas() const { return canvas_; }
    const gfx::Canvas& body() const { return body_; }
    // The item card over a reward under the pointer, in any mode: the bag's own card, so a
    // reward is read before it is chosen. Drawn over the window, on the tooltip's stage.
    void useTipStage(Stage* stage) { tipStage_ = stage; }
    const gfx::Canvas& tipCanvas() const { return tip_; }
    // The rewards' picture wants photographing at this many pixels a unit.
    float pixelsPerUnit() const { return unit_; }

private:
    enum class Mode : uint8_t { Offer, Underway, HandIn, Resting };
    struct Cell {
        int choice = -1;  // the row's choice index, or -1 for a paid item
        int32_t item = -1;
        int plus = 0;
        int count = 1;
        bool luck = false;
        uint8_t sockets = 0;
        uint8_t power = 0;
        uint32_t ink = 0;  // its name's, the card's own tone for it
        gfx::Box box;     // in the body's own units, from the top of what scrolls
    };
    void layout(const Play& play);
    void rebuild(const Play& play, Stage* stage);
    int buttonAt(float ux, float uy) const;
    int cellAt(float ux, float uy, bool anyCell = false) const;
    void drawTip(const Play& play, int cell, float width, float height);
    float scrollMost() const;
    gfx::Box thumb() const;  // the scrollbar's thumb, in window units

    gfx::Canvas canvas_;
    gfx::Canvas body_;
    gfx::Canvas tip_;
    Stage* tipStage_ = nullptr;
    int quest_ = -1;
    bool reading_ = false;
    Mode mode_ = Mode::Offer;
    int chosen_ = -1;
    int over_ = -1, pressing_ = -1;  // buttons: 0 primary, 1 secondary, 2 close; 10 + a cell
    float lift_[3] = {};
    float x_ = 0.0f, y_ = 0.0f, unit_ = 1.0f;
    float scroll_ = 0.0f;       // units scrolled down the body
    float bodyTall_ = 0.0f;     // the body's whole height, in units
    bool dragging_ = false;     // the thumb held
    float grab_ = 0.0f;         // where on the thumb it was held
    bool overThumb_ = false;

    // What layout() settled: the paragraphs as wrapped lines, the cells, the buttons.
    std::vector<std::string> lines_;
    std::vector<Cell> cells_;
    gfx::Box buttons_[3];
    std::vector<Standing> standing_;

    struct Drawn {
        int quest = -1, mode = 0, chosen = -1, over = -1, pressing = -1;
        int lift[3] = {};
        float x = 0, y = 0, unit = 0, scroll = 0;
        bool overThumb = false, dragging = false;
        uint32_t version = 0;
        int64_t minutesLeft = 0;
        int counts[16] = {};
        uint16_t picture = 0xFFFF;
        bool reading = false;
        bool operator==(const Drawn& o) const;
    };
    Drawn drawn_;
    bool built_ = false;
};

}  // namespace mu::game
