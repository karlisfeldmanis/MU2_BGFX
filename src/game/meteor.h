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

    // Advances everything by the frame's own seconds and appends this frame's landings, which
    // is what tells the caller to land a blow, sound an explosion and shake the town.
    struct Impact {
        float x, z;         // where it landed, world metres
        uint32_t attacker;  // who threw it
    };
    void update(float seconds, std::vector<Impact>& impacts);

    // Writes every live thing into the transparent pass.
    void gather(gfx::Effects& effects) const;

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

    // Pools, sized once. A thing past its pool is refused and counted, never grown -- which is
    // MU's own rule as well as this engine's.
    static constexpr int kMaxMeteors = 8;
    static constexpr int kMaxStones = 48;   // six a landing
    static constexpr int kMaxMotes = 160;   // MU's own ceiling for the shared particle pool

    Group fireGroups_[2];   // [0] the rock, opaque-ish; [1] the flame cone, additive
    int fireGroupCount_ = 0;
    Group stoneGroups_[2];
    bgfx::TextureHandle blastSheet_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle emberSheet_ = BGFX_INVALID_HANDLE;
    const content::Ground* ground_ = nullptr;

    Live meteors_[kMaxMeteors] = {};
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
    void land(const Live& rock);

    // Loads one .obj, scaled to metres, optionally keeping one named group only.
    bool loadObj(const std::string& path, float scale, const std::string& groupFilter,
                 std::vector<Corner>& out);
    // One model's triangles, placed and turned, as quads whose last two corners coincide.
    void submit(gfx::Effects& effects, const std::vector<Corner>& tris,
                bgfx::TextureHandle sheet, gfx::Blend blend, const float at[3], float lean,
                float tumble, float scale, const float colour[3], float alpha) const;
};

}  // namespace mu::game
