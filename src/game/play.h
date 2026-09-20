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
    const sim::Findings& findings() const { return findings_; }
    int pointedColumn() const { return pointedColumn_; }
    int pointedRow() const { return pointedRow_; }
    uint32_t pointedAt() const { return pointedAt_; }
    int64_t ticks() const { return realm_.tick(); }
    double tickMs() const { return tickMs_; }
    // What the last line of the log said, so the run can be read without a HUD. Sprint 6 draws
    // these; sprint 5 writes them down.
    const std::string& lastLine() const { return lastLine_; }

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
    };

    Drawn* drawnOf(uint32_t id);
    void remember();  // the tick's positions become "was", the sim's become "now"
    void follow();    // clips, yaw and where each figure stands, at the smoothed position

    content::Tables tables_;
    sim::Realm realm_;
    sim::Findings findings_;
    const content::Ground* ground_ = nullptr;
    const Figures* figures_ = nullptr;

    std::vector<Drawn> drawn_;
    std::vector<float> scratch_;
    double accumulator_ = 0.0;
    double tickMs_ = 0.0;
    float through_ = 0.0f;  // how far between the last tick and the next, 0 to 1

    int pointedColumn_ = -1, pointedRow_ = -1;
    uint32_t pointedAt_ = 0;  // the body under the pointer, or 0
    std::string lastLine_;
};

}  // namespace mu::game
