// Lorencia's leaves: eighty of them on the wind, around wherever the player is.
//
// From `CreateLorenciaLeaf` and `MoveEtcLeaf` in `ZzzEffectFireLeave.cpp`, and they are not
// what the name suggests. **These do not fall.** They are blown roughly sideways across the
// town at head height, drifting on a random walk, and they reach the ground only by wandering
// down to it -- the client gives them no gravity at all. What settles one is landing, after
// which it lies still and darkens until it is gone.
//
// The wind has one deliberate quirk worth keeping. A leaf blows along negative X, but one that
// is NEARER THE CAMERA than the player has its wind reversed and slowed, so the near field and
// the far field blow opposite ways. It reads as a swirl through the square rather than a
// draught across it, and it is the single thing that makes eighty specks look like weather.
//
// Like the birds, the pool follows the player: the client spawns each leaf within eight tiles
// of the hero and never anywhere else, so the effect exists only where somebody is standing to
// see it. Eighty at once is `iMaxLeaves` for an ordinary map -- the client keeps room for 200
// and spends that only in the Devil Square.
//
// Ported from MU2's `client/core/Leaves.cs`, whose four judgements are kept with it:
//   * **They are specks.** MU's `RenderPlane3D(3, 3)` comes out 8.5 units by 6, a twelfth of a
//     tile. MU2 settled at 3.2 by 2.4 of MU's units after its own first pass drew them bigger:
//     the point of these is atmosphere at the edge of attention, and the moment a leaf is big
//     enough to identify it stops being weather and becomes a thing flying past.
//   * **The wind is a tenth of the client's face value.** The client scales a leaf's velocity
//     by its frame factor when it creates it and then by the factor AGAIN every frame it
//     integrates, so the speed is that speed times the factor squared and a leaf blows
//     measurably slower the faster the machine runs. Reproducing that ties the wind to the
//     frame rate; taking the numbers at face value gives about two and a half tiles a second
//     and reads as a gale. The second factor is pinned instead, and then brought down twice --
//     0.4 is what it works out to at sixty frames a second and still crossed the square faster
//     than the man walking through it; 0.10 is a drift, which is what a leaf on a still
//     afternoon does.
//   * **Two thirds brightness.** The sheet is a pale leaf, near white where it is solid, which
//     against Lorencia's paving reads as a bright fleck rather than as something caught in the
//     air.
//   * **A stray is taken back.** The client retires a leaf only when it lands, and since
//     nothing pulls one down, one that drifts upward is gone for good and its slot with it.
//     Over a few minutes of walking that thins the eighty down to whatever has not escaped.
//
// And the fifth, which is this engine's and the birds' too: **there is no wind indoors.** The
// client blows leaves through the tavern quite happily because nothing in it knows what a room
// is; the roof fades off a building here, so leaves drifting across a lit table give the whole
// thing away. Faded rather than switched off, over about a third of a second -- short enough to
// read as "there is no wind in here" and long enough not to read as a glitch.
//
// Every length is MU's own divided by a hundred, and the speeds are divided with them, so the
// wind is the client's wind at this scale rather than a new one.
//
// **And the rain, which is the same pool in the client.** CreateHeavenRain makes the first
// `RainCurrent` share of the slots drops and leaves the rest to be leaves, so as the rain comes
// in the leaves thin out, and as it goes they come back (game/world/weather.h holds the share).
// A drop is BITMAP_RAIN, `RenderPlane3D(1, 20)`: a streak two centimetres wide and forty long,
// laid along the way it falls and turned to face the eye. It lands, dies, and leaves a ring
// (BITMAP_RAIN_CIRCLE, MoveHeavenRain) that widens and fades over twenty frames. What is not
// MU's, and marked where it is set: more drops than the client's 80 slots, because at this
// camera eighty streaks over a sixteen-metre field read as a few specks rather than a shower,
// and the fall slanted across the screen rather than towards it.
//
// **And Devias's snow, which is the same pool again.** CreateDeviasSnow puts a flake in every
// slot a leaf would take: BITMAP_LEAF1, World3's leaf01.jpg, a soft white dot at scale 5, and
// one in ten BITMAP_LEAF2, a six-pointed glint at 10; 200 to 399 units over the hero in the
// leaves' own field, falling 8 to 23 units a frame on a slant of 30 degrees, then MoveEtcLeaf's
// random walk and its landing fade (ZzzEffectFireLeave.cpp:274-297, :400-420). Drawn as
// sprites facing the eye under EnableAlphaBlend, and never indoors (MainScene.cpp:81). What is
// not MU's, and marked where it is set: the fall is a tenth of the client's face value, as the
// leaves' wind is and for the same reason -- at face value it is a blizzard, and this is the
// daytime snow; the storm is a weather spell of its own, later -- and each flake has a slow
// sway of its own over the walk, since a flake that only jitters reads as noise.
#pragma once

#include <cstdint>
#include <string>

#include <bgfx/bgfx.h>

#include "content/ground.h"
#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class Leaves {
public:
    // The sheet is the showing's `leaf` (MU's `Effect/Leaf01.OZT`). Without it nothing blows
    // and the log says so; it is not a reason to stop. The rain's two, `rain` and `rain_ring`,
    // are asked for too, and without them it does not rain.
    // `snow` is Devias's: the same pool falls as CreateDeviasSnow's flakes instead of blowing
    // as leaves, off the showing's `snow` and `snow_star`. See the note on Flakes below.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table, bool snow = false);
    void shutdown();

    // One frame, around wherever the character is drawn. `eye` is the camera, which decides
    // which half of the wind a leaf gets -- see the quirk in the header. `indoors` empties the
    // air over about a third of a second. `rain` is the weather's share, 0 to 1: that much of
    // the leaves' slots stay empty and that much of the drops' fall.
    void update(float seconds, const float hero[3], const float eye[3], bool indoors,
                const content::Ground& ground, float rain = 0.0f);
    // Into the transparent pass. `eye` turns each streak to face the camera.
    void gather(gfx::Effects& effects, const float eye[3]) const;

    // Devias's blizzard, 0 calm to 1 full (game/world/weather.h's rain() on Devias): the snow
    // blows sideways at up to a storm wind in gusts, falls faster, fills a larger pool and
    // streaks, and what lands is blown on rather than lying. docs/devias-blizzard.md.
    void setStorm(float share) { storm_ = share; }
    // Where the blizzard's wind blows now, in the lighting sheet's grass_wind_degrees (turning
    // from +x towards -z): the leaves' -x is 180, and the heading turns it towards +z.
    float windDegrees() const { return 180.0f + windHeading_ * 57.2957795f; }

    bool isOpen() const { return bgfx::isValid(sheet_); }
    uint32_t blowing() const { return blowing_; }
    uint32_t falling() const { return falling_; }

private:
    // The client's `iMaxLeaves` for an ordinary map.
    static constexpr int kCount = 80;
    // Devias's flakes. **Invention:** the client's pool is the same 80 there; at this camera
    // eighty specks over a sixteen-metre field read as a few motes rather than a snowfall.
    static constexpr int kFlakes = 150;
    // And in Devias's blizzard, filled towards this as the storm comes in. **Invention.**
    static constexpr int kStormFlakes = 180;
    // Drops, and the rings they leave. **Invention:** the client has the leaves' 80 slots for
    // both; see the header.
    static constexpr int kDrops = 700;
    static constexpr int kRings = 480;

    struct Drop {
        float position[3] = {0.0f, 0.0f, 0.0f};
        float velocity[3] = {0.0f, 0.0f, 0.0f};  // metres a second
        float faint = 1.0f;   // its own share of the drop's alpha
        float length = 0.2f;  // its own half-length, metres
        bool live = false;
    };
    struct Ring {
        float position[3] = {0.0f, 0.0f, 0.0f};
        float scale = 0.0f;
        float life = 0.0f;  // seconds left
        float faint = 1.0f;
        bool live = false;
    };
    void spawnDrop(Drop& drop, const float hero[3], const content::Ground& ground);
    void landRing(const float at[3], float faint);

    struct Leaf {
        float position[3] = {0.0f, 0.0f, 0.0f};
        // Metres per REFERENCE frame, which is what the client's numbers are.
        float velocity[3] = {0.0f, 0.0f, 0.0f};
        // The client's `Light`, which is both its colour and its life.
        float light = 0.0f;
        bool live = false;
        // A flake's own: one in ten is BITMAP_LEAF2's glint at twice the size, and each is
        // drawn at its own share of the light so the fall has depth.
        bool star = false;
        float faint = 1.0f;
        float phase = 0.0f;  // where its sway is, radians
    };

    void spawn(Leaf& leaf, const float hero[3], const float eye[3],
               const content::Ground& ground);
    void move(Leaf& leaf, float factor, const content::Ground& ground);
    void spawnFlake(Leaf& flake, const float hero[3], const content::Ground& ground);
    void moveFlake(Leaf& flake, float seconds, float factor, const content::Ground& ground);
    float random01();
    float between(float low, float high) { return low + random01() * (high - low); }

    Leaf leaves_[kStormFlakes > kCount ? kStormFlakes : kCount];
    bool snow_ = false;
    float storm_ = 0.0f;  // setStorm: how far Devias's blizzard is in, 0 to 1
    // The blizzard's wind, which wanders: its heading off the leaves' -x in radians and its
    // strength as a share, each turning towards a target drawn afresh every few seconds.
    float windHeading_ = 0.0f, windHeadingTo_ = 0.0f;
    float windStrength_ = 0.8f, windStrengthTo_ = 0.8f;
    float windChangeIn_ = 0.0f;
    bgfx::TextureHandle starSheet_ = BGFX_INVALID_HANDLE;
    Drop drops_[kDrops];
    Ring rings_[kRings];
    int nextRing_ = 0;
    // The gust: seconds on a clock that swings the slant, MU's RainAngle on sin(WorldTime).
    float gust_ = 0.0f;
    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle rainSheet_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle ringSheet_ = BGFX_INVALID_HANDLE;
    uint32_t blowing_ = 0;
    uint32_t falling_ = 0;
    uint32_t seed_ = 0x2545F491u;
};

}  // namespace mu::game
