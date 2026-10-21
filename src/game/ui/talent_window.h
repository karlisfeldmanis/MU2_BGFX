// **Talents** (docs/plans/talents.md, phase 5): his class's tree, a window in the middle of the
// screen, the user's board E as it stood on the Talents canvas on 2026-10-21
// (https://claude.ai/artifact/RTLpPhd7Prn4G6eX86FCaL, project/Passive.dc.html), in Sanctuary.
//
//   * Six columns a class, the Magic Gladiator's seven, so the window is 200 x columns + 60 wide
//     (1260, 1460) in tip::unit(). Each column is a tree: a tier is two square smalls leading to
//     one bigger major, three tiers, with the lines between.
//   * A node is MU's own master-tree icon (new_Master_Icon.OZJ, pipeline/talent_icons.py): in
//     colour once open, MU's grey twin while closed. Its x/10 on a dark band over the icon's
//     foot, and a − | + strip inside its frame: + on an open node not full, − on one that has
//     points and can give one back (sim::talentTakeable). Chosen nodes are blood, not gold:
//     gold is loot.
//   * The strip at the foot names the chosen node, its rank, what it gives now and at the next
//     point, and Reset, which asks a second click.
//   * A card over the node under the pointer, as the plan's "Cards" has it.
//
// **A mirror** (memory: the interface is a mirror). A press raises a request through Desk and
// Play; the window draws Body::talents and nothing else, so a point is never drawn before the
// realm took it. What the realm changed is seen by `watch`, and the unlock's animation (the plan's
// "The unlock") starts there, from the realm's answer, never from the click.
#pragma once

#include <cstdint>
#include <string>

#include "game/ui/hud.h"
#include "gfx/interface.h"
#include "sim/talents.h"

namespace mu::sim {
struct Body;
}

namespace mu::game {

class Play;
namespace panel {
class Arts;
}

class TalentWindow {
public:
    struct Result {
        bool close = false;
        bool clicked = false;        // a press that asks nothing of the realm: the window's click
        int column = -1, node = -1;  // a + (take) or a − (give back) on this node
        bool take = false;
        bool reset = false;          // Reset, pressed the second time
    };
    void open(const gfx::Interface& interface, panel::Arts* arts);
    void close();
    void show();
    void hide();
    bool up() const { return up_; }
    // His ranks, every frame, up or not: what the realm changed since the last look starts the
    // animation while the window is up, and is only taken in while it is down (no animation of a
    // tree nobody saw change). `seconds` is the frame's, and is the window's clock.
    void watch(float seconds, const sim::Body* hero);
    // One frame while up. `floor` is the HUD plate's top, in pixels: the window keeps over it.
    void update(const Play& play, float width, float height, float floor, const Pointer& pointer,
                Result* out);
    bool covers(float x, float y) const;
    const gfx::Canvas& canvas() const { return canvas_; }
    const gfx::Canvas& card() const { return card_; }
    // Whether `took` points into node `node` of `column`, as the realm answered them, opened
    // something: the node's first point, or a node of the column closed before them and open
    // after. `now` is his ranks with the points in. The unlock's sound plays on it (Desk).
    static bool unlocks(const sim::TalentRanks& now, int column, int node, int took);

private:
    // A node's look moving: from one level to another (0 grey, 1 open, 2 taken) after `at`.
    enum class Motion : uint8_t { None, Wake, Point, Fade };
    struct Glow {
        double at = -1.0e9;
        float seconds = 0.0f;
        float from = 0.0f, to = 0.0f;
        Motion motion = Motion::None;
    };
    // A line filling from its upper node to its lower, from one state's ink to another's.
    struct Fill {
        double at = -1.0e9;
        uint8_t from = 0, to = 0;
    };
    static constexpr int kEdges = 10;  // in a column: 2 a tier into its major, 2 out of each of two
    // Everything the drawing reads, so a frame that changes none of it draws nothing.
    struct Drawn {
        sim::TalentRanks ranks{};
        int kin = -1, free = 0;
        bool second = false;
        float x = 0, y = 0, u = 0, width = 0, height = 0;
        int over = -1, pressing = -1, column = 0, node = 0;
        bool armed = false;
        bool operator==(const Drawn&) const = default;
    };
    void animate(const sim::TalentRanks& before, const sim::TalentRanks& after);
    void rebuild(const Play& play);
    void rebuildCard(const Play& play);
    int hitAt(float ux, float uy) const;

    gfx::Canvas canvas_, card_;
    panel::Arts* arts_ = nullptr;
    bool up_ = false;
    int columns_ = 6;
    float x_ = 0.0f, y_ = 0.0f, u_ = 1.0f, wide_ = 1260.0f;
    float width_ = 0.0f, height_ = 0.0f;
    int over_ = -1, pressing_ = -1;
    int column_ = 0, node_ = 0;  // the chosen node, named by the strip
    bool armed_ = false;         // Reset pressed once: the next press resets
    Drawn drawn_;
    bool built_ = false;
    int carded_ = -1;            // the node the card was built for, -1 none
    sim::TalentRanks cardRanks_{};

    // What the window last saw, and the clock the animation runs on.
    sim::TalentRanks seen_{};
    uint32_t seenId_ = 0;
    int seenKin_ = -1;
    // A double: a session's hours of seconds would blur a float's twentieths.
    double clock_ = 0.0, busyUntil_ = -1.0;
    Glow glow_[sim::kTalentColumnsMost][sim::kTalentNodes];
    double ringAt_[sim::kTalentColumnsMost][sim::kTalentNodes];
    Fill fill_[sim::kTalentColumnsMost][kEdges];
};

}  // namespace mu::game
