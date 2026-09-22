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
#include "game/aura.h"
#include "game/crowd.h"
#include "game/figures.h"
#include "game/marker.h"
#include "game/showing.h"
#include "game/sound.h"
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
    // `bare` is the naked class body's own name (Figures::dress's `base`), kept so a later
    // equip or unequip can dress the hero again in the same body it started in. Left empty,
    // as it is where nothing calls `open` with one, `redress` has nothing to re-dress with
    // and does nothing -- the hero keeps whatever he was handed at the door.
    bool open(const std::string& assetDir, const std::string& world, const content::Ground* ground,
              Figures* figures, uint64_t seed, int kin, int level, int column, int row,
              const std::string& weapon = "", const std::string& shield = "",
              const FigureBody* heroLook = nullptr, const std::string& bare = "");
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

    // `hover` collects the SAME drawables -- same transform, same palette row -- for whichever
    // body is `pointedAt()` or whichever townsperson is `pointedFolk()`, so the outline ring
    // can draw them a second time without a second pose. Null skips the collecting.
    void gather(gfx::Renderer& renderer, const float* viewProj, std::vector<gfx::Drawable>& out,
                std::vector<gfx::Drawable>* casters, std::vector<gfx::Drawable>* hover = nullptr);

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
    // Re-dresses the hero over the realm's own idea of what his hands and his back hold, so
    // the figure never shows a weapon the bag no longer does. Called after anything that can
    // change a worn slot; a no-op where `open` was given no `bare` to dress over.
    void redress();
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
    // The thing on the ground under the pointer, by its id, or 0.
    uint32_t pointedLying() const { return pointedLying_; }

    // Where a monster's health bar sits on screen: the pixel a third of a tile over the top
    // of its body as drawn this frame, MU2's Crowd.Crown. False when the body has never been
    // placed or the point is behind the camera.
    bool crownOf(uint32_t id, const float* viewProj, int width, int height, float* x,
                 float* y) const;
    // A body's health as the DRAWING has shown it: the realm's, with every blow still waiting
    // for its landing cue added back, and nought once it is dead. See Showing::owed.
    int32_t shownHealth(uint32_t id) const;
    // Alive as the DRAWING has it: the realm's alive, or dead on the tick with the fall still
    // waiting for the killing blow to land. What the health bar reads, so it is not taken
    // away a swing before the blow that emptied it.
    bool shownAlive(uint32_t id) const;
    // The hero as a save keeps him, and laid back on a hero just raised -- see Realm::restore.
    // restore() also dresses the figure in what the record wears.
    sim::HeroRecord record() const { return realm_.record(); }
    void restore(const sim::HeroRecord& saved);
    // The drops on the ground in the realm that the drawing is still holding back, because
    // the monster that dropped them has not finished falling. Litter skips them, and so does
    // the pointer.
    const std::vector<uint32_t>& heldDrops() const { return heldIds_; }

    // Where each thing on the ground is on screen this frame, for its label: the id, and the
    // pixel a little above where it lies. Only those in front of the camera.
    struct OnScreen {
        uint32_t id = 0;
        float x = 0.0f, y = 0.0f;
    };
    void dropsOnScreen(const float* viewProj, int width, int height,
                       std::vector<OnScreen>& out) const;
    // Which drops have landed and lie still, from Litter::settled: only those are labelled.
    void setSettledDrops(const std::vector<uint32_t>& ids) { settled_.assign(ids.begin(), ids.end()); }
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
    // The character comes in: he is not there for `delay` seconds and then dissolves in, with
    // a warm edge, over kAppearSeconds. Called once the preloader has faded the frame up;
    // never called, he is simply there, which is what every review run wants.
    void appear(float delay) {
        appearing_ = true;
        appearAt_ = -delay;
    }
    // Where a click sent him. Opened by the caller for the same reason as the showing.
    Marker& marker() { return marker_; }
    void gatherMarker(gfx::Effects& effects) const {
        if (ground_) marker_.gather(effects, *ground_);
    }
    // What a level looks like, and sounds like. Opened by the caller for the same reason as
    // the showing.
    Aura& aura() { return aura_; }
    Sound& sound() { return sound_; }
    // Opens the sound and loads what this realm can say: the level-up, and every breed's
    // attack, death and wandering cries, found once per body as its clips are. Opened by the
    // caller after the showing, whose table the events are read from. Not fatal.
    void openSound(const std::string& assetDir, bool muted);
    // Where the ears are this frame: on the ground under the character as he is drawn,
    // turned by the camera's heading. MU's Update3DPositions, with the voices that follow a
    // body moved to where it is drawn now.
    // `indoors` is whether the character's tile is under a roof (World::indoors), which is
    // what switches the wind off -- the same read that lifts the roofs, so the two agree.
    void hear(const gfx::Camera& camera, bool indoors);
    // The interface's own noises, raised by the windows: a button acknowledging the finger
    // (SOUND_CLICK01), a request the realm said no to (iButtonError), and a thing going into
    // a slot -- MU has no equip or bind sound of its own and plays SOUND_GET_ITEM01 for both.
    enum class Ui { Click, Refused, Took };
    void ui(Ui which);
    void gatherAura(gfx::Effects& effects, const float eye[3]) const {
        if (ground_) aura_.gather(effects, *ground_, eye);
    }
    // Throws the level-up on the hero where he is drawn now. What a `Levelled` does once the
    // blow that earned it has landed, and what `--rise` does for a review run.
    void rise();

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
        // Where it was drawn when a click cut a tick short; see Play::update.
        float caughtX = 0.0f, caughtY = 0.0f, caughtFacing = 0.0f;
        float yaw = 0.0f;
        // The top of the body where follow() last stood it, in world metres, and whether it
        // has been stood anywhere yet. What the health bar is hung from; a corpse keeps the
        // last one, since follow() stops placing it.
        float crown[3] = {0.0f, 0.0f, 0.0f};
        bool placed = false;
        bool visible = false;
        int attackClip = -1;     // this body's swing, found once at open
        int deathClip = -1;      // MONSTER01_DIE, found once at open the same way
        // A monster's own sound events, as Sound handles, found once at openSound: its breed's
        // `_attack`, `_die` and `_move` by MU2's naming (the label lowered, no spaces). -1 for
        // the character and for a breed with nothing cooked, which is silence.
        int cryAttack = -1, cryDie = -1, cryMove = -1;
        // Negative while alive. Set to 0 the tick `Died` happens and counted up from there, so
        // the corpse holds its last pose and fades instead of vanishing on the tick it falls --
        // see kDeathHold and kDeathFade in play.cpp.
        float deadFor = -1.0f;
        int32_t health = 0;  // the body's health before the blows of the tick being read
        // Dead on the tick and not yet fallen on screen: the blow that killed it is still
        // being swung, and the fall waits for that blow's landing cue. See Play::fallWhenLanded.
        bool fallOwed = false;
        // Counted up from 0 the tick `Rose` happens, so a respawn eases in rather than popping
        // into being; left far above kSpawnFadeSeconds otherwise, which reads as "done fading".
        float spawnFade = 1e9f;
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
    // Starts a body's death clip, its hold and its fade.
    void fall(Drawn& dead);
    // Starts every owed fall whose killing blow is no longer waiting to be shown.
    void fallWhenLanded();
    // Lets go of every held drop a beat after its dropper's killing blow lands, and rewrites
    // heldIds_.
    void releaseDrops();
    struct HeldDrop {
        uint32_t drop = 0;
        uint32_t dropper = 0;
    };
    std::vector<HeldDrop> held_;
    std::vector<uint32_t> heldIds_;
    std::vector<uint32_t> settled_;

    content::Tables tables_;
    sim::Realm realm_;
    sim::Findings findings_;
    const content::Ground* ground_ = nullptr;
    Figures* figures_ = nullptr;
    std::string bare_;  // the naked class body redress() dresses back over; see open()

    Showing showing_;
    Marker marker_;
    Aura aura_;
    Sound sound_;
    // The wandering cry's own dice: the drawing's, so that hearing a spider never moves the
    // sim's seeded stream.
    uint32_t wanderDice_ = 0x6d2b79f5u;
    // The events that are not a breed's, as Sound handles, found once at openSound.
    struct Heard {
        int swing = -1, swingLong = -1, bow = -1, crossbow = -1;  // the character's swing
        int hit = -1;                                            // melee_hit, any landed blow
        int die = -1;                                            // pMaleDie, the knight's fall
        int grass = -1, soil = -1;                               // his footsteps
        int wind = -1;                                           // Lorencia's air
        int hammer = -1;                                         // Hanzo at his anvil
        int itemDrop = -1, moneyDrop = -1, jewel = -1;  // a thing landing; a jewel's own ring
        int take = -1;                                  // pGetItem: a pickup, an equip, a bind
        int drink = -1, apple = -1;                     // a potion going down
        int click = -1, refused = -1;                   // the windows
    } heard_;
    // The sound a player's swing makes, from what is in his hands. -1 bare-handed.
    int swingSound(const sim::Body& body) const;
    // The hero's footsteps and the smith's hammer, after the clips have been advanced this
    // frame, since both are read off where a clip's clock stands.
    void steps();
    // A drop coming into view -- its dropper down, or the hero's own discard -- makes its noise
    // where it lies. Called from releaseDrops for each one let go.
    void landed(uint32_t drop);
    // The coins of a purchase or a sale, at the hero.
    void coins();
    void hammer();
    // Whether each of the hero's feet has been heard on the walk cycle now playing, and whether
    // he was walking last frame. MU's c->Foot[0] and [1]; see steps().
    bool leftFoot_ = false, rightFoot_ = false, striding_ = false;
    // A level the realm has given and the drawing has not shown: it waits, as MU2's did, for
    // the blow that killed `levelOn_` to land, so the flares do not go up half a swing before
    // the monster that earned them is hit. 0 is no one, and shows at once.
    bool levelOwed_ = false;
    uint32_t levelOn_ = 0;
    // Whether the next Walked the hero says came from a click, and so puts the marker down.
    // Cleared by the first tick that runs after the ask, since that tick is the one the realm
    // takes the order on. One click is one walk: holding the button does not drag the walk
    // after the pointer (the user's call, 2026-09-21), so there is no held re-aim here.
    bool mark_ = false;
    // A click was made this frame: run the next tick now instead of waiting up to 50 ms for
    // it. See Play::update.
    bool stepNow_ = false;
    bool appearing_ = false;  // the character's fade-in is running; see appear()
    float appearAt_ = 0.0f;   // seconds into it, negative while it waits
    float sinceEarly_ = 1.0f;  // seconds since a click last took a tick early
    // The cues that came due this frame. A member and not a local so that it keeps its
    // capacity: a fight must not allocate to show itself.
    std::vector<Cue> due_;

    std::vector<Drawn> drawn_;
    // The town's people: those the table names a figure for, where the tables say, facing
    // where MU faced them, and those it leaves to the town's placements, as those stand them.
    struct Standing {
        Figure figure;
        int folk = -1;
        // Whether it takes turns among its clips, MuMain's way: see Play::fidget. A guard does
        // not -- he wears the player's 283 and MU stands him in one stop action for good.
        bool cycles = false;
        // Hanzo, whose hammer is heard; and whether this blow has rung yet (see hammer()).
        bool smith = false;
        bool rung = false;
        float lastClock = 0.0f;
        uint32_t dice = 1;
    };
    // Starts a townsperson who cycles: its own dice, a clip by the rule, and a clock put
    // somewhere in it so that two of a kind are not in step.
    void settle(Standing& one);
    // The next clip for one that has just finished its last: three times in four the first,
    // which is the resting one, and otherwise one of the others. MuMain's
    // `if (rand() % 16 < 12) SetAction(o, 0); else SetAction(o, rand() % 2 + 1);`, by way of
    // MU2's Scenery.Next, which generalised it past two alternates.
    static int fidget(Standing& one);
    std::vector<Standing> folk_;
    int pointedFolk_ = -1;
    uint32_t pointedLying_ = 0;
    std::vector<float> scratch_;
    double accumulator_ = 0.0;
    double tickMs_ = 0.0;
    float through_ = 0.0f;  // how far between the last tick and the next, 0 to 1

    int pointedColumn_ = -1, pointedRow_ = -1;
    uint32_t pointedAt_ = 0;  // the body under the pointer, or 0
    std::string lastLine_;
};

}  // namespace mu::game
