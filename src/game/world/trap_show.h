// The Dungeon's traps, drawn: the realm's traps (sim/traps.h) as the Dungeon objects MuMain draws
// them as. MuMain hides the placement (HiddenMesh -2, ZzzObject.cpp:3893-3897) and draws the live
// trap -- a character of the server's -- as that same model (ZzzCharacter.cpp:14274-14282), so the
// placements stay hidden here and these are drawn from the realm's list instead, which is also
// what leaves the client's 59th Fire Trap out.
//
//   * Lance Trap (100) as Object40 and Fire Trap (102) as Object52: still, their one key.
//   * Iron Stick Trap (101) as Object41: held at rest (MuMain forces action 0,
//     ZzzCharacter.cpp:3516-3519), and on a strike its action 1 at PlaySpeed 0.4
//     (MapManager.cpp:1131-1134) -- ten keys a second over the cook's four. Its rest is held on
//     the strike's first key: the cook keeps the one clip, and action 0 is one key.
//
// Each stands on its tile's centre on the ground, turned to its facing: MU's placement angle is
// 0 for SouthWest, 90 for SouthEast and 180 for NorthEast, which is atan2(dx, -dy).
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "content/ground.h"
#include "content/mesh.h"
#include "content/showing.h"
#include "content/texture.h"
#include "game/crowd.h"
#include "game/figures.h"
#include "gfx/effects.h"
#include "gfx/renderer.h"
#include "sim/realm.h"

namespace mu::game {

class TrapShow {
public:
    // Loads the three models and the saw from `cooked/<world>/meshes`, and the Fire Trap's
    // flame (`fire2`, Effect/Fire02) off the showing. A world with none opens nothing.
    bool open(const std::string& assetDir, const std::string& world, content::Textures& textures,
              const content::Showing& table);
    void shutdown();
    bool isOpen() const { return !meshes_.empty(); }

    // Stands the realm's traps on the ground. Once, after the realm is raised; again is harmless.
    void stand(const std::vector<sim::Realm::Trap>& traps, const content::Ground& ground);
    // Trap `index` (the realm's) fired: an Iron Stick strikes. `at` is where it stands, for its
    // sound and its fire.
    void fired(size_t index, float at[3]);
    // Where trap `index` stands, the way it faces (x, z), and its breed; false if there is none.
    bool where(size_t index, float at[3], float facing[2], int32_t* number) const;

    // `hero` is where he stands, in world metres: the ceiling drops its stones only near him.
    void update(float seconds, const float hero[3], gfx::Renderer& renderer);
    void gather(std::vector<gfx::Drawable>& out) const;
    // The Fire Trap's flames.
    void gatherEffects(gfx::Effects& effects) const;
    // And its light on the floor while it burns: the effect's own AddTerrainLight, (1, 0.6, 0.3)
    // over two tiles (ZzzEffect.cpp:6738-6739). Into `out`, at most `max`; returns how many.
    uint32_t lights(gfx::PointLight* out, uint32_t max) const;

private:
    struct Stood {
        int32_t number = 0;
        const content::Mesh* mesh = nullptr;
        float position[3] = {0.0f, 0.0f, 0.0f};
        float yaw = 0.0f;
        float facing[2] = {0.0f, 0.0f};
        float light[3] = {1.0f, 1.0f, 1.0f};
        int figure = -1;         // into figures_, for an Iron Stick
        float striking = 0.0f;   // seconds of its strike left
        int paletteRow = -1;
    };

    std::vector<std::unique_ptr<content::Mesh>> meshes_;
    const content::Mesh* lance_ = nullptr;
    const content::Mesh* stick_ = nullptr;
    const content::Mesh* fire_ = nullptr;
    std::unique_ptr<ClipLibrary> library_;
    std::unique_ptr<FigureBody> body_;
    // The Lance Trap's saw in the air (ZzzEffect.cpp:1825-1830): MU's units a reference frame.
    struct Saw {
        float position[3];
        float along[2];
        float spin;     // Angle[2], degrees
        float life;     // frames
        float light[3];
    };
    // The Fire Trap's BITMAP_FIRE + 1 effect, an emitter parked out of the vent for ten frames
    // (ZzzEffect.cpp:1072-1078, :6735-6739), and the particles it throws (subtype 1,
    // ZzzEffectParticle.cpp:500-505, :4700-4708).
    struct Jet {
        float position[3];
        float along[2];
        float life;     // frames
        float owed;     // frames toward its next particle
    };
    struct Flame {
        float position[3];
        float velocity[2];  // metres a frame along the ground
        float life;         // frames
    };
    // A pebble from the ceiling (MODEL_DUNGEON_STONE01): MU's units and frames.
    struct Stone {
        float position[3];  // world metres
        float gravity;      // MU units a frame, up
        float life;         // frames
        float scale;
        float yaw;
        float light[3];
    };
    const content::Mesh* saw_ = nullptr;
    const content::Mesh* stone_ = nullptr;
    const content::Ground* ground_ = nullptr;
    std::vector<Stone> stones_;
    float stoneOwed_ = 0.0f;
    bgfx::TextureHandle fire2_ = BGFX_INVALID_HANDLE;
    std::vector<Saw> saws_;
    std::vector<Jet> jets_;
    std::vector<Flame> flames_;
    uint32_t seed_ = 0x51A7E5EDu;
    std::vector<Figure> figures_;
    std::vector<Stood> stood_;
    std::vector<float> scratch_;
};

}  // namespace mu::game
