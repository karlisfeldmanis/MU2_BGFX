// Atlans's bubbles. MU's BITMAP_BUBBLE, Object8/drop01.jpg: nine 16-texel bubbles on black,
// added (MapManager.cpp:41, ZzzEffectParticle.cpp:8949-8952). SubType 0 lives 30-39 frames at
// Scale 0.12-0.27 and climbs (10..29) * 2.5 * Scale units a frame, jittering (+-10) * 2.5 *
// Scale in x and y (:195-201, 4144-4152): about three metres in a second and a half, wobbling.
//
// Two of MU's sources:
//   * the 845 hidden vents, Object23 (type 22; cooked as bubble anchors): Timer += 0.1 a frame,
//     wrapping at 10, and while it is past 5 one bubble a frame -- two seconds on and two off
//     (ZzzObject.cpp:4055-4063);
//   * the hero's head, one a frame for the first second of every ten (ZzzCharacter.cpp:5703-5711,
//     at (0, 20, -10) off his head bone).
// Not yet: a dying body's four a frame (:3312-3320), and the Bahamut's (:2002-2012).
//
// Ours, marked where it is set: a vent rolls only within kNear of him (MU moves only the
// objects in the blocks round the hero), each vent starts at its own point of the cycle (MU's
// all start at 0 and breathe together), and a vent throws a fraction of MU's 25 a second --
// the house keeps effects faint.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <bgfx/bgfx.h>

#include "content/cooked.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class Sound;

class Bubbles {
public:
    // Opens in Atlans and takes the showing's `bubble`.
    void open(const std::string& assetDir, const std::string& world, const content::CookedTown& town,
              content::Textures& textures);
    void shutdown();
    bool isOpen() const { return open_; }
    // Ours: a gurgle as a near vent comes on and as his head's stream starts, MU's
    // mDeathBubble (Kalima's, not Atlans's: MU's Atlans bubbles are silent). The play's sound,
    // which opens after the world (World::raiseAirs).
    void setSound(Sound* sound);

    // One frame round `hero`, his feet, in metres.
    void update(float seconds, const float hero[3]);
    void gather(gfx::Effects& effects) const;

private:
    struct Bubble {
        float at[3] = {0, 0, 0};
        float scale = 0.2f;  // MU's Scale
        float age = 0.0f, life = 1.2f;
        int frame = 0;       // which of drop01's bubbles, row * 4 + column
    };
    struct Vent {
        float at[3] = {0, 0, 0};
        float timer = 0.0f;  // MU's Timer, 0 to 10, a tenth a reference frame
        float owed = 0.0f;   // seconds towards the next bubble while it is on
        bool on = false;     // on last frame, so its coming on is heard once
    };
    void throwOne(const float at[3]);
    float unit();

    bool open_ = false;
    std::vector<Vent> vents_;
    std::vector<Bubble> bubbles_;
    float head_ = 0.0f;      // seconds into his head's ten-second cycle
    float headOwed_ = 0.0f;
    uint32_t seed_ = 0xB0BB1Eu;
    Sound* sound_ = nullptr;
    int gurgle_ = -1;
    float quiet_ = 0.0f;  // seconds until a vent may gurgle again
    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
};

}  // namespace mu::game
