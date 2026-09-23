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
    // and the log says so; it is not a reason to stop.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    void shutdown();

    // One frame, around wherever the character is drawn. `eye` is the camera, which decides
    // which half of the wind a leaf gets -- see the quirk in the header. `indoors` empties the
    // air over about a third of a second.
    void update(float seconds, const float hero[3], const float eye[3], bool indoors,
                const content::Ground& ground);
    // Into the transparent pass.
    void gather(gfx::Effects& effects) const;

    bool isOpen() const { return bgfx::isValid(sheet_); }
    uint32_t blowing() const { return blowing_; }

private:
    // The client's `iMaxLeaves` for an ordinary map.
    static constexpr int kCount = 80;

    struct Leaf {
        float position[3] = {0.0f, 0.0f, 0.0f};
        // Metres per REFERENCE frame, which is what the client's numbers are.
        float velocity[3] = {0.0f, 0.0f, 0.0f};
        // The client's `Light`, which is both its colour and its life.
        float light = 0.0f;
        bool live = false;
    };

    void spawn(Leaf& leaf, const float hero[3], const float eye[3],
               const content::Ground& ground);
    void move(Leaf& leaf, float factor, const content::Ground& ground);
    float random01();
    float between(float low, float high) { return low + random01() * (high - low); }

    Leaf leaves_[kCount];
    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
    uint32_t blowing_ = 0;
    uint32_t seed_ = 0x2545F491u;
};

}  // namespace mu::game
