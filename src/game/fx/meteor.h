// The Lich's Meteorite: a burning rock thrown out of the sky, the embers it sheds on the way
// down, the six stones and the fireball it leaves where it lands, and the jolt it gives the
// camera.
//
// MU's whole skill is two lines -- `CreateEffect(MODEL_FIRE, to->Position, to->Angle, o->Light)`
// and a sound (ZzzCharacter.cpp:5008) -- and almost nothing about it is in those two lines.
// What `to->Position` names is **where the rock will land**, not where it appears: the spawn
// puts it 400 units up and 130 to 161 units aside, and the fall carries it back. The chain is
// in ZzzEffect.cpp: the throw at :2546, the fall and the landing at :7732-7860, the quake and
// the shock at :7752-7773.
//
// **MU2's `client/core/Meteor.cs` is the second source and it is used deliberately.** That file
// is this same effect built in Godot, measured and judged by eye over a long sitting, and its
// remarks record both the numbers and the half-dozen ways of getting them wrong. Porting its
// findings is cheaper than rediscovering them, and every one is traceable from it back into the
// client. Where it invented something rather than transcribing it, this says so.
//
// The one thing on this path that is NOT presentation: nothing here rolls a die the sim can
// see, and no damage is computed. The blow was resolved on the tick the Lich swung; the rock is
// what the player watches while the sim's answer waits its 0.34 s. See the sprint file.
//
// It is `game`: it knows a blow, a ground and a target. The pass it draws through knows none of
// them.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/ground.h"
#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"
#include "gfx/renderer.h"

namespace mu::game {

class Meteor {
public:
    // The three models (Fire01, Stone01, Stone02) out of the effects folder, and three sheets
    // off the cooked showing: `explosion` for the blast, `fire` for the embers, `smoke` for
    // what is left smouldering. Returns false only when the rock itself could not be had --
    // every sheet is allowed to be missing, and what depends on it simply is not drawn.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table, const content::Ground* ground);
    void shutdown();

    // A Lich threw one. `targetX, targetZ` are where it will LAND, in world metres -- the
    // target's own tile -- and not where the rock appears. `attacker` is the Lich's body id,
    // carried through to the impact so the blow it belongs to can be landed with the fire.
    void cast(float targetX, float targetZ, uint32_t attacker = 0);

    // How long a meteor is in the air, in seconds, the same every throw because nothing about
    // the fall varies: 400 units of height at 50·cos20 = 46.98 units a reference frame is
    // 8.513 frames of MU's 25, which is **0.3405 s**.
    //
    // Not 400/50. The speed is along the heading and only its vertical part is descent; taking
    // the whole of it lands the blow a fifteenth of a second before the rock arrives, which is
    // a number appearing over a man nothing has hit yet.
    //
    // The cue is fused with this, so the damage number and the blood land WITH the fire rather
    // than halfway through a clip. It is the first cue in this game paced by an effect instead
    // of by an animation, and the impact rushes the cue as well, so a rock refused by a full
    // pool still lands its blow on time.
    static constexpr float fallSeconds() {
        constexpr float kCosEntry = 0.93969262f;  // cos(20 deg); std::cos is not constexpr
        return kLift / (kFallSpeed * kCosEntry) / kReferenceFps;
    }

    // **The wizard's Fire Ball: the same model thrown flat at a body instead of dropped on a
    // tile.** `CreateEffect(MODEL_FIRE, o->Position, ..., 1, to)` -- subtype 1, the `else` at the
    // end of the creation switch, and subtype is the whole difference between a meteor and a
    // fireball (MU2/docs/spells.md; MU2's `Meteor.Hurl` is the second source, ported). `from` is
    // the caster's feet, `to` the middle of the body it is thrown at, `target` that body's id.
    //
    // MU's: sixty frames of life, a size rolled 0.8 to 1.1, lifted 120 units off his feet, fifty
    // units a reference frame (twelve and a half tiles a second, `SkillRow::flies`), an ember a
    // frame, the rock alone with no flame cone (the mover gives subtype 1 `BlendMeshLight = 0`),
    // its orange light on the ground, and two stones where it arrives.
    //
    // Ours, and marked: a glow over the rock, cooling embers, aimed at the body's middle and
    // steered after it, as the bolt is; and a half-size Explotion01 on the body where MU draws
    // only the stones -- a bolt of light ends in a flash, a ball of fire ends in a burst. No
    // smoke: MU2 laid a soot trail here and the user took it out on the bench (2026-09-28,
    // "there is already smoke in fireball") -- the ember sheet carries its own.
    void hurl(const float from[3], const float to[3], uint32_t target);
    // The realm said the blow missed: the fireball nearest that body flies on past and out.
    void missHurl(uint32_t target);
    // Advances the fireballs by the frame's seconds, steering each after where its target is
    // drawn -- `alive(id)` and `where(id, out)` as `Bolt::update` takes them. Call beside update().
    template <typename Alive, typename Where>
    void fly(float seconds, Alive alive, Where where);
    uint32_t liveFireballs() const;

    // Advances everything by the frame's own seconds and appends this frame's landings, which
    // is what tells the caller to land a blow, sound an explosion and shake the town.
    struct Impact {
        float x, z;         // where it landed, world metres
        uint32_t attacker;  // who threw it
    };
    void update(float seconds, std::vector<Impact>& impacts);

    // Writes every live thing into the transparent pass. `eye` is the camera, which the
    // fireball's glow is drawn toward; null draws it at the rock.
    void gather(gfx::Effects& effects, const float* eye = nullptr) const;

    // The light a burning rock throws on the town, as MU's own `AddTerrainLight(..., 2, ...)`:
    // a deep orange-red at two tiles, its energy the SAME 0.7-1.0 roll the rock's body takes,
    // so the pool on the ground flickers with the flame above it rather than beside it. One
    // light a live rock, up to `max`; returns how many were written.
    //
    // It ends with the rock. MU creates no light at the landing -- the blast is a sprite and
    // the fire on the ground is its picture, not its illumination -- and adding one there
    // would be an invention this has no reason to make.
    uint32_t lights(gfx::PointLight* out, uint32_t max) const;

    // The camera's jolt, in DEGREES OF PITCH, which is what MU's EarthQuake is: it is added to
    // `m_State.Angle[0]` (DefaultCamera.cpp:700) and decayed by 0.2 a frame
    // (MainScene.cpp:199). Always negative -- the meteor's own assignment is
    // `(rand() % 4 - 4) * 0.1`, which spans -0.4 to -0.1 and never reaches zero. Slid instead
    // of tilted, as this was first written, it is four millimetres on a camera six metres out,
    // which is nothing at all.
    float quakeDegrees() const { return quake_; }

    uint32_t liveMeteors() const;
    uint32_t liveStones() const;
    uint32_t liveMotes() const;
    uint32_t refused() const { return refused_; }

private:
    // A corner of an .obj triangle, as the Marker keeps its pin.
    struct Corner {
        float x, y, z, u, v;
    };

    // One model's group: its triangles and its sheet.
    struct Group {
        std::vector<Corner> triangles;  // in threes
        bgfx::TextureHandle sheet = BGFX_INVALID_HANDLE;
        gfx::Blend blend = gfx::Blend::Additive;
    };

    // The falling rock.
    struct Live {
        bool alive = false;
        float x, y, z;         // world metres
        float driftX, fallY;   // its heading, metres a second
        float floorY;          // where the ground was under the tile it was thrown at
        float size;            // 1.0 to 1.7, its own roll, on both meshes
        float flown;           // metres travelled since the last ember
        float left;            // life remaining, reference frames
        float bodyLight;       // this frame's 0.7-1.0 roll, for the rock
        float flameLight;      // this frame's INDEPENDENT 0.4-0.7 roll, for the cone
        uint32_t attacker;
    };

    // A Fire Ball in the air. Apart from `Live` rather than a flag on it, for MU2's reason: a
    // meteor falls on a tile and ends on the ground, a fireball is aimed at a body and ends on it.
    struct Hurled {
        bool alive = false;
        float at[3];          // world metres
        float along[3];       // unit
        float floorY;         // the ground under where it was aimed, for the stones
        float size;           // 0.8 to 1.1
        float flown;          // metres since the last ember
        float left;           // reference frames
        float bodyLight;      // this frame's 0.7-1.0 roll
        float flameLight;     // and the cone's own 0.4-0.7, as the meteor's
        float tumble;         // radians the rock has turned
        uint32_t target;      // 0 for the bench's fixed point, `aim`
        float aim[3];
        bool missing, passed;
    };

    // One of the six stones, on MU's shared ballistic arm -- the same one its bones and its
    // broken ice ride.
    struct Stone {
        bool alive = false;
        float position[3];
        float velocity[3];
        float gravity;   // metres a second squared, its own roll
        float size;      // 0.8 to 1.1, ITS OWN roll and never the meteor's
        float left;      // reference frames
        float lean[2];   // tumble about two axes, radians
        float fade;      // 0 until it has landed, then up to 1
        bool landed;
        int which;       // 0 = Stone01, 1 = Stone02
    };

    // An ember shed by the rock, and the fireball at the landing: both are one sprite whose
    // picture is a sheet walked cell by cell, so they share a struct and differ in their rule.
    struct Mote {
        bool alive = false;
        float position[3];
        float velocity[3];
        float size;      // metres, a square
        float spin;      // radians
        float left;      // reference frames
        float born;      // what `left` started at
        float rise;      // the ember's accelerating lift
        float colour[3];
        // A fireball's ember, which cools as it goes where the meteor's holds its colour (ours):
        // born `colour`, dying toward the deep red and out.
        bool cools = false;
        enum class Kind : uint8_t { Ember, Blast, Smoke } kind = Kind::Ember;
    };

    // ---- MU's numbers ----------------------------------------------------------------------
    static constexpr float kReferenceFps = 25.0f;  // MU's animation clock
    static constexpr float kUnit = 0.01f;          // a hundred units to the tile, a tile to the metre

    // The throw. Born 400 up and 130 + rand % 32 aside, falling at 50 a frame along a heading
    // 20 degrees off vertical -- which is the `Angle (0, 20, 0)` of the spawn, and is the whole
    // reason the rock lands on anything. See cast().
    static constexpr float kLift = 400.0f;
    static constexpr float kSideways = 130.0f;
    static constexpr float kSidewaysSpread = 32.0f;
    static constexpr float kFallSpeed = 50.0f;
    static constexpr float kEntryDegrees = 20.0f;
    static constexpr float kRockFrames = 40.0f;  // its LifeTime; it lands long before this
    static constexpr float kSmallestRock = 1.0f, kLargestRock = 1.7f;  // (rand()%8+10)*0.1

    // The two flickers, and they are two rolls and not one: the body's is
    // `(rand()%4+7)*0.1` and the flame cone's own is `(rand()%4+4)*0.1`, re-rolled every frame
    // so the cone shimmers against its own light rather than in step with the rock's.
    static constexpr float kDimmestGlow = 0.7f, kBrightestGlow = 1.0f;
    static constexpr float kDimmestFlame = 0.4f, kBrightestFlame = 0.7f;
    // What an effect's last five frames do to its light, from the opening lines of MoveEffect,
    // which run before the switch that decides what an effect even is. A meteor lands with
    // most of its life in hand so this almost never runs -- it is kept because it is the rule,
    // and because the fireball that will share this file ends ON its target with its life
    // pinned to one, right inside the window.
    static constexpr float kFadesUnder = 5.0f, kFadeStep = 0.2f;

    // The ember trail. One per fifty units of travel, which at fifty units a frame is MU's own
    // one-a-frame and stays true at any frame rate. The streak itself is NOT these: it is the
    // 166-unit additive cone in the model, and packing billboards tightly enough to look
    // continuous lays discrete sprites over an already smooth flame and fractures both.
    // What the rock lights, and how far: `AddTerrainLight(..., 2, ...)`, two tiles. The
    // fireball's own is twice that -- `AddTerrainLight(..., 4)` in the particle.
    static constexpr float kGlowTiles = 2.0f;
    static constexpr float kBlastGlowTiles = 4.0f;

    static constexpr float kEmberSpacingUnits = 50.0f;
    static constexpr float kEmberFrames = 24.0f;
    static constexpr int kEmberCells = 4;     // a 256x64 strip
    static constexpr int kEmberHeld = 6;      // frames a cell
    static constexpr float kSmallestEmber = 1.28f, kLargestEmber = 1.92f;  // x the sheet's 64
    static constexpr float kEmberSheetUnits = 64.0f;
    static constexpr float kEmberShrink = 0.04f;   // of the sheet's height, a frame
    static constexpr float kEmberSpin = 5.0f;      // degrees a frame
    static constexpr float kEmberRise = 0.004f;    // an accelerating LIFT, not a pull
    static constexpr float kEmberRiseScale = 10.0f;
    static constexpr float kSlowestDrift = 3.2f, kFastestDrift = 4.8f;  // units a frame

    // The blast: born 80 units above the landing, because the stones are thrown from the floor
    // and the fire from above it, which puts the flame at the height of whatever was standing
    // there. Twenty frames of life, its 4x4 sheet walked one cell every two -- so ten of the
    // sixteen are ever seen -- drawn at the sheet's own 256 units across and ADDED, because
    // Explotion01 is a three-component picture with no alpha to mix by. White: MU's colour
    // argument there is uninitialised stack, so there is nothing to tint it with, and tinting
    // it with the trail's red takes the heat out of an already orange core.
    static constexpr float kBlastLift = 80.0f;
    static constexpr float kBlastFrames = 20.0f;
    static constexpr int kBlastHeld = 2;
    static constexpr int kBlastGrid = 4;
    static constexpr float kBlastUnits = 256.0f;
    static constexpr float kBlastInset = 0.005f;

    // The stones, on the shared ballistic arm: life (rand()%16+32) frames, scale
    // (rand()%4+8)*0.1 -- its OWN roll, never multiplied by the rock's, which would put a field
    // of boulders on the grass -- gravity (rand()%16+8) as an ACCELERATION in units a frame
    // squared, and a flat scatter of (rand()%256+64)*0.1 along a random yaw.
    static constexpr float kStoneLeastFrames = 32.0f, kStoneMoreFrames = 16.0f;
    static constexpr float kSmallestStone = 0.8f, kLargestStone = 1.1f;
    static constexpr float kLeastGravity = 8.0f, kMoreGravity = 16.0f;
    static constexpr float kLeastScatter = 64.0f, kMoreScatter = 256.0f;
    static constexpr float kStoneBounceDrag = 0.6f;   // horizontal, a frame, once landed
    static constexpr float kStoneRestUnder = 0.5f;    // units a frame, under which it lies still
    static constexpr float kStoneFade = 0.1f;         // a frame, once landed
    static constexpr float kStoneTumble = 0.5f;       // MU's `Angle += 0.5 * LifeTime`

    // The quake: degrees of camera pitch, decayed by MU's own 0.2 a frame. A jolt, not a rumble.
    static constexpr float kQuakeDecay = 0.2f;
    static constexpr float kQuakeEpsilon = 0.0001f;

    // The fireball, `MODEL_FIRE` subtype 1 (see hurl()).
    static constexpr float kHurlSpeed = 50.0f;      // units a reference frame
    static constexpr float kHurlFrames = 60.0f;     // LifeTime
    static constexpr float kHurlLift = 120.0f;      // Position[2] += 120
    static constexpr float kSmallestBall = 0.8f, kLargestBall = 1.1f;  // (rand()%4+8)*0.1
    static constexpr int kHurlStones = 2;           // CheckTargetRange's two
    // Ours: how near the body's middle it must come to have arrived, the bolt's own; how hard
    // it turns after a body that moved; the frames a miss has left once past; the burst's size
    // against the meteor's.
    static constexpr float kHurlStrikes = 0.3f;
    static constexpr float kHurlSteer = 0.35f;
    static constexpr float kHurlPastFrames = 4.0f;
    static constexpr float kHurlBlastShare = 0.5f;
    // The fire at the head, ours: MU's fireball is the dark rock under its own ember stream, and
    // on this camera the rock read as a black lump. Two added glows over it on the `light`
    // sheet -- a wide orange halo and a small yellow-white heart -- flickering with the body
    // roll, so it reads as a thing burning.
    //
    // Cut back once the meteor's flame cone was laid on it (below): at 2.2 and 1.1 m with a
    // yellow-white heart the glow WAS the fireball, and it read as an orange sphere (the user,
    // 2026-09-28, "not its just orange sphere"). Now it is the heat round the rock and the
    // cone is the fire.
    // The halo widened again for the blur, but kept deep and dim so it is a haze round the
    // flame and not a sphere.
    static constexpr float kHaloWide = 1.9f, kHeartWide = 0.55f;  // metres across
    static constexpr float kHalo[3] = {0.55f, 0.2f, 0.03f};
    static constexpr float kHeart[3] = {0.8f, 0.55f, 0.25f};
    // The meteor's flame cone on the fireball, ours: Fire01's `fire01` group, 1.66 m up the
    // model's Y from 0.27 below the rock's centre, laid BACK along the flight so the ball drags
    // its own fire -- the meteor's streak turned on its side. MU gives subtype 1 no cone
    // (`BlendMeshLight = 0`) and MU2 hid it because a yaw stood it straight up; a full turn
    // lays it down. Brighter than the meteor's 0.4-0.7, which is lit against the whole rock.
    static constexpr float kFireFlame = 1.35f;
    // And drawn longer than it is modelled, along its own axis only, on *"we need little bit
    // longer fire trail"*: 1.66 m of cone becomes three.
    static constexpr float kFlameStretch = 1.8f;
    // How far the cone reaches in front of the rock's centre, in model metres: 26.8 units below
    // its origin. Drawn from the centre as modelled, the stretch and the wide ghosts made that
    // cap a bright rim AHEAD of the ball (*"there is something front on fireball"*); pushed back
    // the whole of it, the flat end of the cone stood behind a bare rock (*"its to far behind"*).
    // So its front is put just inside the rock's own leading face (the rock is 0.22 m across the
    // middle): the fire wraps the stone and nothing leads it.
    static constexpr float kFlameAhead = 0.268f;
    static constexpr float kFlameLeads = 0.19f;
    // Blurred by drawing it again: two ghosts of the cone, each wider and dimmer, so its hard
    // mesh edge melts into a soft one (*"little bit more blurry fireball please"*). Added, so
    // they only ever brighten. Scale across the flight, then how much of the cone's light.
    static constexpr int kFlameGhosts = 2;
    static constexpr float kGhostWide[kFlameGhosts] = {1.25f, 1.55f};
    static constexpr float kGhostLight[kFlameGhosts] = {0.35f, 0.18f};
    // How fast the rock turns in the air, radians a reference frame. Ours: a dead-still stone
    // under a flickering cone reads as a model and not as a thing thrown.
    static constexpr float kBallSpin = 0.18f;
    static constexpr float kGlowForward = 0.5f;                   // metres toward the eye
    // Its embers, ours: half the meteor's, born orange and cooling to the red as they shrink,
    // so the stream tapers and breaks up instead of standing as one red tube.
    static constexpr float kFireEmberShare = 0.55f;
    static constexpr float kFireEmber[3] = {1.0f, 0.5f, 0.12f};
    // The fireball's light on the ground: the rock's red-orange, over four tiles.
    static constexpr float kHurlGlowTiles = 4.0f;
    static constexpr float kHurlGlow[3] = {1.0f, 0.35f, 0.08f};

    // Pools, sized once. A thing past its pool is refused and counted, never grown -- which is
    // MU's own rule as well as this engine's.
    static constexpr int kMaxMeteors = 8;
    static constexpr int kMaxFireballs = 8;
    static constexpr int kMaxStones = 48;   // six a landing
    static constexpr int kMaxMotes = 160;   // MU's own ceiling for the shared particle pool

    Group fireGroups_[2];   // [0] the rock, opaque-ish; [1] the flame cone, additive
    int fireGroupCount_ = 0;
    Group stoneGroups_[2];
    bgfx::TextureHandle blastSheet_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle emberSheet_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle glowSheet_ = BGFX_INVALID_HANDLE;
    const content::Ground* ground_ = nullptr;

    Live meteors_[kMaxMeteors] = {};
    Hurled fireballs_[kMaxFireballs] = {};
    Stone stones_[kMaxStones] = {};
    Mote motes_[kMaxMotes] = {};

    float quake_ = 0.0f;

    // The drawing's own dice. Never the sim's: a rock that took a number out of the seeded
    // stream would make watching the fight change the fight.
    uint32_t dice_ = 0xA5A5A5A5u;
    uint32_t roll();
    float unit();                       // [0, 1)
    float between(float a, float b);    // [a, b)
    uint32_t refused_ = 0;

    Mote* freeMote();
    void ember(const Live& rock);
    void emberAt(const float at[3], const float heading[3], float light, bool fireball = false);
    void stonesAt(float x, float z, float floor, int count);
    void blastAt(float x, float y, float z, float share);
    void land(const Live& rock);
    // One fireball's frame, and whether it is still in the air.
    bool hurling(Hurled& ball, float seconds, bool standing, const float* there);

    // Loads one .obj, scaled to metres, optionally keeping one named group only.
    bool loadObj(const std::string& path, float scale, const std::string& groupFilter,
                 std::vector<Corner>& out);
    // One model's triangles, placed and turned, as quads whose last two corners coincide.
    // The same, turned by a whole basis: the model's X, Y and Z land on `x`, `y` and `z`.
    void submitAlong(gfx::Effects& effects, const std::vector<Corner>& tris,
                     bgfx::TextureHandle sheet, gfx::Blend blend, const float at[3],
                     const float x[3], const float y[3], const float z[3], float scale,
                     const float colour[3], float alpha) const;
    void submit(gfx::Effects& effects, const std::vector<Corner>& tris,
                bgfx::TextureHandle sheet, gfx::Blend blend, const float at[3], float lean,
                float tumble, float scale, const float colour[3], float alpha) const;
};

template <typename Alive, typename Where>
void Meteor::fly(float seconds, Alive alive, Where where) {
    for (Hurled& ball : fireballs_) {
        if (!ball.alive) continue;
        float there[3];
        bool standing = false;
        if (ball.target == 0) {
            standing = true;
            for (int k = 0; k < 3; ++k) there[k] = ball.aim[k];
        } else {
            standing = alive(ball.target) && where(ball.target, there);
        }
        if (!hurling(ball, seconds, standing, standing ? there : nullptr)) ball.alive = false;
    }
}

}  // namespace mu::game
