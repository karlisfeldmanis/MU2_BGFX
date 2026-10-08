// A complete set at +10 or +11: flares circling up out of the ground around whoever wears it,
// and at +11 a ribbon among them now and then -- the user, 2026-10-06: 'i remember their was some
// sparkles or ribbons flying when char had completed +11 set'.
//
// MuMain's (ZzzCharacter.cpp:11094-11115), for a player whose five pieces are one set and every
// one at +9 or more (CheckFullSet, :5387), EquipmentLevelSet being the lowest plus among them:
//
//     if (EquipmentLevelSet > 9 && rand_fps_check(20)) {           // one frame in twenty
//         if (EquipmentLevelSet == 10)
//             CreateParticle(BITMAP_FLARE, o->Position, o->Angle, white, 0, 0.19f, o);
//         else if (EquipmentLevelSet == 11) {
//             if (rand_fps_check(8)) CreateJoint(BITMAP_FLARE, o->Position, o->Position, o->Angle, 0, o);
//             else                   CreateParticle(BITMAP_FLARE, ...same...);
//         }
//     }
//
// The particle (ZzzEffectParticle.cpp:763, :4995, :9121): sixty frames, its start where he stood,
// on a ring of forty units turning a tenth of a radian a frame from a random phase, climbing one
// to five units a frame, scale 0.19 plus up to 0.05 shrinking 0.002 a frame, Flare at its own
// 64 texels times the scale, added, white, not drawn on its first frame. The joint
// (ZzzEffectJoint.cpp:1669) at CreateJoint's own scale of ten: a hundred frames, a thin cross
// ten units wide on the same ring, climbing up to 1.5 units a frame, twenty tails -- the
// level-up's flare (game/fx/aura.h) narrower, slower and longer, drawn by an Aura of its own so
// it never takes a level-up's place.
//
// And from +9 a complete set glows at the arms (:10988): every frame a soft light on each hand's
// grip, elbow and shoulder, flare01 at 1.3 in the boots' chrome colour at half. The knight's
// boots' waterfalls are a later set's and not here. Presentation only: the set is read off his
// worn pieces.
#pragma once

#include <cstdint>
#include <string>

#include "content/tables.h"
#include "content/texture.h"
#include "game/fx/aura.h"
#include "gfx/effects.h"
#include "sim/items.h"

namespace mu::content {
class Ground;
}

namespace mu::game {

class SetShine {
public:
    // Takes Flare off the level-up's sheets, and opens the ribbons' Aura on the same. False when
    // the flare is missing, and then nothing is drawn.
    bool open(const std::string& assetDir, content::Textures& textures);
    void shutdown();

    // The lowest plus of the five worn pieces when they are one set, or -1: CheckFullSet's
    // EquipmentLevelSet, which is all this reads.
    static int setPlus(const content::Tables& tables, const sim::Satchel& bag);

    // One frame. `plus` is setPlus's, or -1 while he is not drawn; `feet` world metres, `yaw`
    // his facing, which a ribbon's cross is laid in as MU lays it in o->Angle.
    void update(float seconds, int plus, const float feet[3], float yaw, float metresPerTile);
    // And this frame's six lights for a set of +9 and up, or none: MU's CreateSprite(BITMAP_LIGHT,
    // ..., 1.3f, Light) at each of `points` (world metres), Light being the boots' chrome colour
    // at half (PartObjectColor(..., 0.5f), ZzzCharacter.cpp:10988-10999), drawn this frame only.
    void lights(const float (*points)[3], int count, const float colour[3], float metresPerTile);
    void gather(gfx::Effects& effects, const content::Ground& ground, const float eye[3]) const;

private:
    struct Flare {
        bool alive = false;
        float start[3] = {};  // where he stood as it rose, world metres
        float per = 0.01f;    // metres in one of MU's units
        float phase = 0.0f;   // Velocity[0], -150 to 149
        float life = 0.0f;    // frames left, from kLife down
        float climb = 0.0f;   // units a frame
        float height = 0.0f;  // units climbed
        float scale = 0.0f;
        float spin = 0.0f;
    };

    static constexpr float kReferenceFps = 25.0f;
    static constexpr float kLife = 60.0f;
    static constexpr float kOrbit = 40.0f;
    static constexpr float kTexels = 64.0f;  // Flare's own width, which MU sizes a sprite by
    static constexpr int kFlares = 64;

    bgfx::TextureHandle flare_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle light_ = BGFX_INVALID_HANDLE;  // flare01, MU's BITMAP_LIGHT
    float lightAt_[6][3] = {};
    int lights_ = 0;
    float lightColour_[3] = {};
    float lightPer_ = 0.01f;
    Aura ribbons_;
    Flare flares_[kFlares] = {};
    float clock_ = 0.0f;  // reference frames owed
    // Where he stands now, so the ring and the ribbons go with him (the user, 2026-10-08:
    // "+11 aura ring has to stay with chaarcter body"). MU's stay where he was; ours follow.
    float feet_[3] = {};
    bool placed_ = false;
    // Drawn only by the drawing and never seeded: the realm's log must not see it.
    uint32_t dice_ = 0x51A7E11Du;
    float unit();
    void throwFlare(const float feet[3], float per);
};

}  // namespace mu::game
