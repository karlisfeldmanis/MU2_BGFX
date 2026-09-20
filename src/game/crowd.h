// One figure's pose, and the crowd of them standing in the town.
//
// The pose is composed in LOCAL space: two frames of the clip blended per bone, then one
// walk of the hierarchy, then the inverse bind. Blending two MODEL-space poses is cheaper
// and skips the hierarchy walk, and it slides a limb through the body when the two poses
// differ by much -- over a 0.18 s crossfade it would mostly hide, which is what makes it the
// wrong kind of cheap.
#pragma once

#include <string>
#include <vector>

#include "content/ground.h"
#include "game/figures.h"
#include "gfx/renderer.h"

namespace mu::game {

// One figure standing somewhere, playing one clip and possibly fading out of another.
class Figure {
public:
    // `safe` is MU's own per-tile safe-zone bit, which decides both the stance and where
    // the weapon is: inside one a character carries it on his back and stands unarmed.
    void stand(const FigureBody* body, const float position[3], float yaw, float scale,
               bool safe = false);
    // Moves a figure that is already standing. `stand` restarts it -- it clears the clip and
    // the clock, which is right when a figure is put down and wrong every frame after: a
    // walker re-stood each frame holds the first pose of its walk forever. So the sim's view
    // moves one with this and stands one only once. See game/play.cpp.
    void place(const float position[3], float yaw, bool safe);
    // `restart` replays a clip that is already running; without it, asking for the clip that
    // is playing is ignored, which is what stops a per-frame request resetting the clock.
    void play(int clip, bool restart = false);
    void update(float seconds);

    // The palette rows this figure's bones occupy: `bones x 12` floats, already transposed
    // the way the shader reads them. Returns how many bones were written, and keeps the
    // bones' world matrices, which is what a held item rides.
    int pose(float* rows12);

    // What the renderer draws: the parts against `row`, and the held items at their bone's
    // own place -- rigid, or, for a bow with its own small rig, against the renderer's bind
    // row. A bow's own clip is owed with the items.
    void gather(int row, std::vector<gfx::Drawable>& out) const;

    const FigureBody* body() const { return body_; }
    int clip() const { return clip_; }
    // Where the clock stands and how long the clip is. A clip that was started and froze on
    // its first frame reports the same NAME as one that is running, and that difference is
    // most of what goes wrong with an animation and none of it is visible in a still.
    float clock() const { return time_; }
    float length() const;
    // The clip's position as a fraction, which is how index.json expresses an effect's
    // window into a clip, and so what sprint 6 will ask this for.
    float through() const;
    const float* position() const { return position_; }
    float radius() const;

private:
    // `limit` is how many of the rig's bones the caller has room for: a rig wider than the
    // palette is posed as far as it fits rather than refused.
    void sample(int clip, float time, size_t limit, float* rotations,
                float* translations) const;

    // The last pose's bone world matrices, 16 floats a bone. Kept rather than recomputed
    // because a held item needs exactly one of them and the walk that built them has just
    // finished.
    std::vector<float> world_;

    const FigureBody* body_ = nullptr;
    float position_[3] = {0, 0, 0};
    float yaw_ = 0.0f;
    float scale_ = 1.0f;
    int clip_ = -1;
    int previous_ = -1;
    float time_ = 0.0f;
    float previousTime_ = 0.0f;
    float fade_ = 0.0f;  // seconds left of the crossfade
    bool safe_ = false;  // standing on a safe tile: weapon on the back, unarmed stance
};

// Who is standing in the town: the fourteen figures MU's own placement list carries, a
// representative crowd of monsters, and one Dark Knight in armour.
//
// The crowd is placed and animated, NOT alive. Spawn tables, AI and pathing are sprint 5;
// these stand and idle where they are put, drawn from the map's own spawn mix so that the
// breeds and their counts are MU's rather than chosen.
class Crowd {
public:
    // `player` is the character who stands at the focus -- the Dark Knight in armour the
    // sprint is judged by. Empty stands nobody there.
    void open(const Figures& figures, const content::Ground& ground, int monsters,
              float focusColumn, float focusRow, const std::string& player = "DarkKnight");
    void update(float seconds);
    void shutdown();

    // Poses every figure, takes a palette row for each, and appends the drawables. Every
    // figure is posed whether or not it survives the cull, because the sun's pass draws what
    // the camera's does not and both read the one palette.
    void gather(gfx::Renderer& renderer, const float* viewProj, std::vector<gfx::Drawable>& out,
                std::vector<gfx::Drawable>* casters);

    size_t figureCount() const { return figures_.size(); }
    uint32_t drawn() const { return drawn_; }
    uint32_t culled() const { return culled_; }
    // What the last gather spent composing poses, in milliseconds. The sprint's own account:
    // 0.5 ms of CPU for the whole crowd.
    double poseMs() const { return poseMs_; }
    size_t boneCount() const { return bones_; }

private:
    std::vector<Figure> figures_;
    std::vector<float> scratch_;
    uint32_t drawn_ = 0;
    uint32_t culled_ = 0;
    double poseMs_ = 0.0;
    size_t bones_ = 0;
};

}  // namespace mu::game
