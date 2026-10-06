// The ring of eighteen Staffs of Destruction Tarkan's two bosses throw: the Zaikan's and the
// Death Beam Knight's AT_SKILL_BOSS blow, at the attack's fourteenth key (ZzzCharacter.cpp:
// 1816-1834, :1887-1897):
//
//   for (int i = 0; i < 18; i++) {
//       VectorCopy(o->Angle, Angle);
//       Angle[2] += i * 20.f;
//       CreateEffect(MODEL_STAFF_OF_DESTRUCTION, o->Position, Angle, o->Light);
//   }
//
// The effect (ZzzEffect.cpp:1330-1336): the item's own model, every mesh added (BlendMesh -2) at
// Scale 1, born 280 units over the boss, pitched 20 degrees and flying (0, -80, -10) units a
// frame turned by its Angle -- out along its own twentieth of the circle and down -- for at most
// thirty frames. Under the ground it is gone, leaving a BITMAP_EXPLOTION 80 units up and six
// MODEL_STONE1/2 (MoveHandlers.cpp:4342-4363); that landing is the caller's (the meteor's blast
// and stones, as Cometfall's), handed back by update().
//
// Not carried: the blue (0.2, 0.4, 1) terrain light each staff lays while it flies, eighteen
// lights at once for half a second.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/ground.h"
#include "content/texture.h"
#include "game/fx/effect_mesh.h"
#include "gfx/effects.h"

namespace mu::game {

class StaffRing {
public:
    // Staff09.obj and wand10.png out of effects/tarkan.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Ground* ground);
    void shutdown();

    // The eighteen, round `feet` (the boss's position on the ground), from its facing `yaw`.
    void cast(const float feet[3], float yaw);

    struct Landing {
        float x, y, z;  // world metres, the ground under it
    };
    void update(float seconds, std::vector<Landing>& landings);
    void gather(gfx::Effects& effects) const;

private:
    static constexpr int kStaffs = 18;
    static constexpr int kPool = kStaffs * 4;
    struct Staff {
        bool alive = false;
        float at[3] = {};
        float travel[3] = {};  // unit, the way it flies
        float speed[2] = {};   // metres a reference frame: along `travel`'s flat part, and up
        float left = 0.0f;     // reference frames
    };
    std::vector<EffectCorner> mesh_;
    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
    const content::Ground* ground_ = nullptr;
    Staff staffs_[kPool] = {};
};

}  // namespace mu::game
