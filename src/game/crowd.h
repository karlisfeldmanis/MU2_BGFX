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
    // The pitch and roll a town placement is drawn at (Town::gather's own transform). A figure
    // stands upright and never sets it; a swaying lamp or fountain does, so that what rides
    // one of its bones -- see pointOn -- lands on the mesh the town drew and not beside it.
    void tilt(float pitch, float roll) {
        pitch_ = pitch;
        roll_ = roll;
    }
    // Swaps what is drawn without restarting it: the clip, its clock and the crossfade all
    // stay exactly where they were. For a figure re-dressed mid-stride -- an equip or an
    // unequip -- rather than one just put down; `body`'s rig and clip library must be the
    // ones this figure is already playing (Figures::dress keeps both, so any two dressings of
    // the same base are interchangeable here). See Play::redress.
    void reskin(const FigureBody* body) { if (body) body_ = body; }
    // Moves a figure that is already standing. `stand` restarts it -- it clears the clip and
    // the clock, which is right when a figure is put down and wrong every frame after: a
    // walker re-stood each frame holds the first pose of its walk forever. So the sim's view
    // moves one with this and stands one only once. See game/play.cpp.
    void place(const float position[3], float yaw, bool safe);
    // `restart` replays a clip that is already running; without it, asking for the clip that
    // is playing is ignored, which is what stops a per-frame request resetting the clock.
    //
    // `fade` is how long the crossfade into it takes, in seconds; negative takes the default.
    // The two directions of one change are not the same change -- setting off is a weight
    // shift the eye wants to see take a moment, and stopping is an arrival the body is already
    // late for -- so the caller says which it is. MU2's `Crowd.Gaiting` and `Crowd.Halting`.
    void play(int clip, bool restart = false, float fade = -1.0f);
    // Puts the clock somewhere in the clip. A walk resumes where it left off rather than at
    // its first key, which is one leg fully forward: taken from legs caught mid-cross, that is
    // the longest crossfade in the game and the one nobody asked for.
    void setClock(float seconds);
    // Seats whatever plays on a saddle: Bip01, the pelvis and both legs are taken from `clip`,
    // on its own clock, and the spine and everything above it from the clip playing. A rider's
    // standing swing -- the knight's paired blows, which MU has no ride clip for -- keeps his
    // seat (docs/mount.md, option 1). -1 lets the legs follow the clip again. `handsOnly` seats
    // all but the arms: everything from the clavicles out from the clip playing, the rest -- the
    // spine and the head with the legs -- from `clip`. A buff cast on a horse (the user: "we need
    // that only hands do the job").
    void seat(int clip, bool handsOnly = false);
    // The other way round: the spine and everything above it from `clip`, on its own clock,
    // over the clip playing -- a two-handed grip held over the ride's own seat, where MU's one
    // armed ride stance is a one-handed one. -1 for none.
    void upper(int clip);

    // `clipRate` advances the CLIP's clock faster or slower than the world's, while the
    // crossfade keeps running in real seconds. That split is the whole of walking without
    // sliding: the rate is the ground the body actually covered divided by the ground the clip
    // was authored to cover, so the feet are pinned to the earth by arithmetic rather than by
    // an animator's luck -- and a body covering no ground (turning on the spot, a step refused
    // by the grid) has its feet stop rather than skate. A fade measured in clip seconds would
    // stall with it and leave the body blended between two poses indefinitely.
    void update(float seconds, float clipRate = 1.0f);

    // The palette rows this figure's bones occupy: `bones x 12` floats, already transposed
    // the way the shader reads them. Returns how many bones were written, and keeps the
    // bones' world matrices, which is what a held item rides.
    int pose(float* rows12);
    // Poses each held item that carries a clip of its own -- a bow's or a crossbow's string
    // -- into its own palette row, which gather then draws it with. After pose(): it reads
    // the body's clip and clock. `rows12` is scratch of the renderer's kMaxBones rows.
    void poseHeld(gfx::Renderer& renderer, float* rows12);

    // What the renderer draws: the parts against `row`, and the held items at their bone's
    // own place -- rigid, or, for a bow with its own small rig, against the renderer's bind
    // row. A bow's own clip is owed with the items.
    void gather(int row, std::vector<gfx::Drawable>& out) const;
    // Where `local` -- a point in bone `bone`'s own frame, in metres -- stands in the world on
    // the last pose: the bone's world matrix, then the placement. False when the bone was not
    // posed. What MU's `b->TransformPosition(BoneTransform[n], p, Position)` does, for what
    // rides a bone without being drawn as a mesh: a lantern's glow, a spray's puff.
    bool pointOn(int bone, const float local[3], float out[3]) const;
    // A bone's whole world matrix on the last pose, the placement included: what rides it
    // rigidly -- the Imp on a shoulder -- is drawn in this frame. False when not posed.
    bool boneWorld(int bone, float out[16]) const;
    // Draws this figure in `parent`'s frame instead of at its own position and yaw: MU's
    // unlinked RenderLinkObject, a model riding another's bone (game/pets.h). `unmount` puts
    // it back on its own placement.
    void mount(const float parent[16]);
    void unmount() { mounted_ = false; }
    // Where `model` -- a point in the figure's own bind space, in metres -- stands in the
    // world: the placement alone, with no bone. For what is part of the model and never moves
    // with its clip, such as the coals in Hanzo's forge.
    void pointInModel(const float model[3], float out[3]) const;
    // Where `model`, a point in the figure's bind space in metres, has been carried by bone
    // `bone`'s pose: what a vertex weighted wholly to that bone does. False when the bone was
    // not posed. The street lamp's light rides its lantern this way (Lamps::follow).
    bool pointOnBind(int bone, const float model[3], float out[3]) const;
    // Plays its looping clip on a curve through the keys (Catmull-Rom) rather than on straight
    // lines between them. **Ours, and for the town's swaying objects alone**: MU interpolates
    // linearly, and on a lamp's 22 keys of pendulum the speed then jumps at every key, which
    // reads as a shudder rather than a swing. The keys themselves are MU's and still passed
    // through exactly; a figure's own clips are left linear, where a curve would round a
    // blow's snap.
    void smoothKeys(bool on) { smooth_ = on; }
    // Draws his right hand's weapon or not: Rageful Blow's is in the air while its clip is
    // under its fourth key (fx/fury.h). Only in the hand -- slung, it is drawn.
    void emptyHand(bool on) { emptyHand_ = on; }

    const FigureBody* body() const { return body_; }
    int clip() const { return clip_; }
    // Where the clock stands and how long the clip is. A clip that was started and froze on
    // its first frame reports the same NAME as one that is running, and that difference is
    // most of what goes wrong with an animation and none of it is visible in a still.
    float clock() const { return time_; }
    float length() const;
    // How far the clip now playing is meant to carry this body over one cycle, in metres, at
    // the size this body is drawn. Zero for everything that goes nowhere, which is every clip
    // but the locomotion set -- so it doubles as "is this a clip whose rate the ground
    // decides". The cook measured it (`action_travel`) and closed the looping clips so that
    // `duration` is a true cycle; dividing one by the other is the speed it was authored at.
    float travel() const;
    // The clip's position as a fraction, which is how index.json expresses an effect's
    // window into a clip, and so what sprint 6 will ask this for.
    float through() const;
    const float* position() const { return position_; }
    float yaw() const { return yaw_; }
    // Whether what it holds is on its back this frame rather than in its hands: standing on a
    // safe tile, on a rig with somewhere to sling it.
    bool slung() const { return safe_ && body_ && body_->backBone >= 0; }
    float scale() const { return scale_; }
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
    std::vector<int> heldRows_;  // each held item's palette row from poseHeld, -1 for none

    const FigureBody* body_ = nullptr;
    float position_[3] = {0, 0, 0};
    float yaw_ = 0.0f;
    // The seat (see `seat`): its clip and clock, and which bones are below the spine, worked out
    // once per rig.
    int seat_ = -1;
    float seatTime_ = 0.0f;
    bool seatHands_ = false;
    int upper_ = -1;
    float upperTime_ = 0.0f;
    const FigureBody* seatBody_ = nullptr;
    std::vector<uint8_t> seated_;
    std::vector<uint8_t> arm_;  // a clavicle or a child of one
    float pitch_ = 0.0f, roll_ = 0.0f;
    float scale_ = 1.0f;
    int clip_ = -1;
    int previous_ = -1;
    float time_ = 0.0f;
    float previousTime_ = 0.0f;
    float fade_ = 0.0f;        // seconds left of the crossfade
    // How long this crossfade was asked to take. Kept because the blend weight is
    // `1 - fade_ / fadeLength_`, and dividing by a constant instead was right only while every
    // fade was the same length -- the moment one transition wanted its own, a shorter fade
    // would have started at a weight above zero and a longer one would have run past the end.
    float fadeLength_ = 0.0f;
    bool safe_ = false;  // standing on a safe tile: weapon on the back, unarmed stance
    bool smooth_ = false;  // curved between keys; see smoothKeys
    bool mounted_ = false;  // drawn in mount_'s frame; see mount
    bool emptyHand_ = false;  // the right hand's weapon left out; see emptyHand
    float mount_[16] = {};
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
