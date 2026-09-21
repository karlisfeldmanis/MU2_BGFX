// The window's view onto a realm: the tick on its own accumulator, a figure for every body the
// sim has, and the pointer that turns a click into a request.
//
// The whole of the rule this file exists to keep is that **it reads the sim and never
// second-guesses it**. Nothing here decides whether a blow lands, where something may stand or
// who is fighting whom; it asks where a body is, draws it there, and hands clicks back the
// other way. What it does own is presentation: the smoothing between two ticks, which clip a
// figure plays, and which way it is turned.
//
// It is deliberately NOT `game/crowd.cpp`'s Crowd. A Crowd stands figures where it chooses and
// idles them; these stand where the sim says and walk when the sim says they are walking. The
// two are different jobs and the second one is the game.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/ground.h"
#include "content/tables.h"
#include "game/crowd.h"
#include "game/figures.h"
#include "game/showing.h"
#include "gfx/renderer.h"
#include "sim/audit.h"
#include "sim/realm.h"

namespace mu::game {

class Play {
public:
    // `column` and `row` are where the character is put down; the realm moves him to the
    // nearest standable tile. False when the world has no cooked tables -- which is not fatal
    // to the run, only to playing it.
    // `heroLook` is the body the character is drawn in -- the naked class body with whatever
    // the game has put in his hands, built by Figures::dress. Null falls back to the cook's
    // own armoured Dark Knight, which is what every run before the game had.
    bool open(const std::string& assetDir, const std::string& world, const content::Ground* ground,
              const Figures* figures, uint64_t seed, int kin, int level, int column, int row,
              const std::string& weapon = "", const std::string& shield = "",
              const FigureBody* heroLook = nullptr);
    void shutdown();

    bool isOpen() const { return realm_.tables() != nullptr; }

    // Steps the sim as many whole ticks as the frame's own seconds have earned, on the sim's
    // own accumulator, and never more than a handful at once. Presentation is smoothed between
    // the two ticks either side of where the clock stands.
    void update(double seconds);

    // The pointer, and what a click does with it. The ray is cast against the land's own
    // height field rather than against a flat plane: the town is up to two metres of relief
    // and a flat-plane pick is a tile or two out wherever the ground is not level.
    void point(const gfx::Camera& camera, const float* view, const float* proj, float pixelX,
               float pixelY, int width, int height);
    void leftClick();   // walk to the tile under the pointer, or fight what is standing on it
    void rightClick();  // stop

    void gather(gfx::Renderer& renderer, const float* viewProj, std::vector<gfx::Drawable>& out,
                std::vector<gfx::Drawable>* casters);

    // Where the camera should look, in tiles: the character, smoothed as he is drawn.
    void focus(float* column, float* row) const;

    const sim::Realm& realm() const { return realm_; }
    // The windows' requests, which the realm decides. A window never changes what it shows by
    // itself: it asks here and redraws from the realm afterwards (sprint 7, "a mirror").
    // One point into strength (0), agility (1), vitality (2) or energy (3), refused where
    // none is in hand -- OpenMU's IncreaseStatsAction, one point a press.
    bool spendPoint(int stat);
    // A drag from one slot to another, and a right-click on a carried thing. Both the realm's
    // to refuse; see sim/items.h for the gates.
    bool moveItem(int from, int to);
    bool useItem(int slot);
    // Puts things in his bag by the asset's name, for a scripted run: `--give Potion02:3`.
    // `count` is a stack's size for a potion and ignored for anything else.
    bool give(const std::string& name, int count);
    // The open counter's requests: a purchase by shelf slot, a sale by bag slot, and walking
    // away. Each the realm's to refuse.
    bool buy(int shelfSlot);
    bool sell(int bagSlot);
    void closeTrade() { realm_.closeTrade(); }
    // Zen, for a scripted run (`--zen`), and a walk to a townsperson by name (`--talk`): the
    // same Talk request a click on him raises.
    void earn(long long zen) { realm_.earn(zen); }
    bool talkTo(const std::string& name);
    const sim::Findings& findings() const { return findings_; }
    int pointedColumn() const { return pointedColumn_; }
    int pointedRow() const { return pointedRow_; }
    uint32_t pointedAt() const { return pointedAt_; }
    // The townsperson under the pointer, as an index into the tables' folk, or -1.
    int pointedFolk() const { return pointedFolk_; }
    int64_t ticks() const { return realm_.tick(); }
    double tickMs() const { return tickMs_; }
    // What the last line of the log said, so the run can be read without a HUD. Sprint 6 draws
    // these; sprint 5 writes them down.
    const std::string& lastLine() const { return lastLine_; }

    // What a blow looks like. Opened by the caller rather than by Play::open, because the
    // sheets need a device and Play is given an asset directory and no Textures -- and
    // threading one through world.cpp for two texture loads would be a worse trade than
    // saying here that it is opened second. A Showing that never opened draws nothing and
    // the fight is otherwise unaffected.
    Showing& showing() { return showing_; }
    const Showing& showing() const { return showing_; }

private:
    // One body as it is drawn: the figure, and where it was at the last two ticks so a frame
    // between them can be interpolated.
    struct Drawn {
        Figure figure;
        uint32_t id = 0;
        float wasX = 0.0f, wasY = 0.0f;
        float nowX = 0.0f, nowY = 0.0f;
        // And which way it was pointing at those same two ticks. The facing is interpolated for
        // the same reason the position is, and it matters MORE: the sim turns a body up to 45
        // degrees in one tick (900 degrees a second at 20 Hz), so a facing read raw off the sim
        // snaps through a quarter turn every 50 ms while the position glides. At 180 fps that
        // is one jump every nine frames, and it is the stutter the first person to play this
        // reported.
        float wasFacing = 0.0f, nowFacing = 0.0f;
        float yaw = 0.0f;
        bool visible = false;
        int attackClip = -1;     // this body's swing, found once at open
        float swinging = 0.0f;   // seconds of it left to play before idle or walk take over
        float swingPace = 1.0f;  // how much faster than authored the swing clip must run
        // The walk. `groundSpeed` is what the last tick actually covered, in metres a second,
        // and is the numerator of the clip's rate; `still` is how long it has covered nothing,
        // which is what decides whether a stop is a stop or a stumble; `walkPhase` is where
        // the cycle was when the walk was last left, so it can be resumed rather than restarted.
        float groundSpeed = 0.0f;
        float still = 0.0f;
        float walkPhase = 0.0f;
        float clipRate = 1.0f;   // what this figure's clip runs at this frame
        // Counted up every time this body starts a swing. A landing cue carries the token of
        // the swing it belongs to, and a cue whose token no longer matches drops itself --
        // which is what "gated on the clip still being the swing" means. A step cancels a
        // swing here, so a cue really does get dropped in ordinary play.
        uint32_t swingToken = 0;
    };

    Drawn* drawnOf(uint32_t id);
    void remember();  // the tick's positions become "was", the sim's become "now"
    // Clips, yaw and where each figure stands, at the smoothed position. `seconds` is the
    // frame's own, which the coast and the stop are measured in.
    void follow(float seconds);

    content::Tables tables_;
    sim::Realm realm_;
    sim::Findings findings_;
    const content::Ground* ground_ = nullptr;
    const Figures* figures_ = nullptr;

    Showing showing_;
    // The cues that came due this frame. A member and not a local so that it keeps its
    // capacity: a fight must not allocate to show itself.
    std::vector<Cue> due_;

    std::vector<Drawn> drawn_;
    // The town's people who have a figure to wear and are not already stood by the world's
    // placements. They stand where the tables say, facing where MU faced them, and idle.
    struct Standing {
        Figure figure;
        int folk = -1;
    };
    std::vector<Standing> folk_;
    int pointedFolk_ = -1;
    std::vector<float> scratch_;
    double accumulator_ = 0.0;
    double tickMs_ = 0.0;
    float through_ = 0.0f;  // how far between the last tick and the next, 0 to 1

    int pointedColumn_ = -1, pointedRow_ = -1;
    uint32_t pointedAt_ = 0;  // the body under the pointer, or 0
    std::string lastLine_;
};

}  // namespace mu::game
