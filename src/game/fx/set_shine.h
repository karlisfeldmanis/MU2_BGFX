// A complete set at +10 or +11: flares circling up out of the ground around whoever wears it,
// and at +11 a pair of ribbons now and then, winding feet to head -- the user, 2026-10-06: 'i remember their was some
// sparkles or ribbons flying when char had completed +11 set'.
//
// MuMain's (ZzzCharacter.cpp:11094-11115), for a player whose five pieces are one set and every
// one at +9 or more (CheckFullSet, :5387), EquipmentLevelSet being the lowest plus among them --
// the Magic Gladiator's armour to boots, his head not asked (`end = EQUIPMENT_ARMOR`):
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
// grip, elbow and shoulder, flare01 at 1.3 in the boots' chrome colour at half. Presentation only:
// the set is read off his worn pieces.
//
// And Kanturu's four sets at +10 and over (:11004-11075, boots MODEL_DRAGON_KNIGHT_BOOTS to
// MODEL_VOLCANO_BOOTS): three puffs at once, at the root bone (Bip01, the pelvis, 107 up) plus
// (0, -18, 50), (0, 0, 70) and (0, 18, 50), so round the shoulders and head -- one frame in four at +10, three at +11, two at +12, every frame past -- each
// CreateParticle(BITMAP_WATERFALL_2, ..., 3) in the set's colour, Dragon Knight (0.65, 0.3, 0.1),
// Venom Mist (0.1, 0.1, 0.9), Sylphid Ray (0, 0.32, 0.24), Volcano (0.5, 0.24, 0.8). Subtype 3
// (ZzzEffectParticle.cpp:3499-3550, 8476-8515) is BITMAP_LIGHT + 2 (Effect/cra_04.jpg), twelve
// frames, set off up to 10 aside and 20 up or down, scale 0.54 to 0.9 losing 0.008 a frame,
// climbing from 2 a frame by 0.08 more, wandering up to 1 aside, its light / 1.02 a frame and
// / 1.1 more in its last ten. A soft glow climbing off his shoulders and head in the set's colour.
//
// For every player drawn, not the hero alone: MuMain asks CheckFullSet of each MODEL_PLAYER it
// renders (ZzzCharacter.cpp:9156-9161) and draws the set's light round whoever wears one. Each
// wearer has a slot of his own -- his feet, his clock, his ribbons -- and the flares are one pool.
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
    // EquipmentLevelSet, which is all this reads. The Magic Gladiator's is four, his helm slot
    // not looked at.
    static int setPlus(const content::Tables& tables, const sim::Satchel& bag, sim::Kin kin);

    // One wearer this frame, before update(): `id` the body's, `plus` setPlus's (nothing is
    // drawn below +9), `feet` world metres, `yaw` his facing, which a ribbon's cross is laid in
    // as MU lays it in o->Angle; `arms` his six lights' points, MU's CreateSprite(BITMAP_LIGHT,
    // ..., 1.3f, Light) (ZzzCharacter.cpp:10988-10999), Light being the boots' chrome colour at
    // half. A wearer not handed in on a frame lights nothing and throws nothing; what he threw
    // stays in the air where he stood.
    // `spray` a Kanturu set's colour (sprayOf), null for any other set; `puffAt` the three
    // points it throws from this frame, world metres (kPuffPoints carried by his pelvis), or null
    // to throw from a still body's.
    void wear(uint32_t id, int plus, const float feet[3], float yaw, const float (*arms)[3],
              int lit, const float colour[3], const float* spray = nullptr,
              const float (*puffAt)[3] = nullptr);
    // The puffs' colour for a Kanturu set's boots, at its brightness, or null.
    static const float* sprayOf(const content::ItemRow& boots);
    // MU's (0, -18, 50), (0, 0, 70), (0, 18, 50) off Bip01 in the bind pose, in the figure's
    // model space in metres (MU's x, z, -y): 18 either side at 157 up, and 177 up the middle.
    static constexpr float kPuffPoints[3][3] = {
        {-0.18f, 1.569f, 0.0f}, {0.0f, 1.769f, 0.0f}, {0.18f, 1.569f, 0.0f}};
    // One frame: the flares and ribbons in the air age, and each wearer handed in throws anew.
    void update(float seconds, float metresPerTile);
    void gather(gfx::Effects& effects, const content::Ground& ground, const float eye[3]) const;

private:
    struct Flare {
        bool alive = false;
        int wearer = 0;       // whose ring it turns on, where he was last drawn
        float phase = 0.0f;   // Velocity[0], -150 to 149
        float life = 0.0f;    // frames left, from kLife down
        float climb = 0.0f;   // units a frame
        float height = 0.0f;  // units climbed
        float scale = 0.0f;
        float spin = 0.0f;
    };
    struct Wearer {
        uint32_t id = 0;    // the body's; 0 a free slot
        bool seen = false;  // handed in this frame
        int plus = -1;
        // Where he stands now, so the ring and the ribbons go with him (the user, 2026-10-08:
        // "+11 aura ring has to stay with chaarcter body"). MU's stay where he was; ours follow.
        float feet[3] = {};
        float yaw = 0.0f;
        float clock = 0.0f;  // reference frames owed
        // Reference frames since the last ribbon was thrown; high, so a new wearer's goes up at once.
        float sinceRings = 1.0e6f;
        Aura ribbons;
        float lightAt[6][3] = {};
        int lights = 0;
        float lightColour[3] = {};
        bool spray = false;  // a Kanturu set's
        float sprayColour[3] = {};
        bool posed = false;  // puffAt handed in this frame
        float puffAt[3][3] = {};
    };
    struct Puff {
        bool alive = false;
        float at[3] = {};    // world metres
        float rise = 0.0f;   // units a frame
        float life = 0.0f;   // frames left
        float scale = 0.0f;
        float light[3] = {};
        float spin = 0.0f;
    };

    static constexpr float kReferenceFps = 25.0f;
    static constexpr float kLife = 60.0f;
    static constexpr float kOrbit = 40.0f;
    static constexpr float kTexels = 64.0f;  // Flare's own width, which MU sizes a sprite by
    static constexpr int kFlares = 96;
    // Players in one sight with a set of +9 or more. One past it is drawn without; said once.
    static constexpr int kWearers = 12;

    bgfx::TextureHandle flare_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle light_ = BGFX_INVALID_HANDLE;  // flare01, MU's BITMAP_LIGHT
    float per_ = 0.01f;
    Wearer wearers_[kWearers];
    Flare flares_[kFlares] = {};
    static constexpr int kPuffs = 96;
    Puff puffs_[kPuffs] = {};
    bgfx::TextureHandle puff_ = BGFX_INVALID_HANDLE;  // cra_04, MU's BITMAP_LIGHT + 2
    void throwPuffs(const Wearer& one);
    bool full_ = false;  // said that a wearer found no slot
    int wearing_ = 0;    // wearers seen last frame, logged when it changes
    // Drawn only by the drawing and never seeded: the realm's log must not see it.
    uint32_t dice_ = 0x51A7E11Du;
    float unit();
    void throwFlare(int wearer);
    int slotFor(uint32_t id);
};

}  // namespace mu::game
