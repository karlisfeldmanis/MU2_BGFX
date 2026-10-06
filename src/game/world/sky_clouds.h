// Icarus's cloud road: MU's RenderObjectVisual arm for WD_10HEAVEN (ZzzObject.cpp:3090-3169), six
// kinds of box MU never draws, cooked as world anchors (tools/cook.py ANCHOR_KINDS_BY_WORLD,
// EmitterKind::Cloud0 + the box's type):
//
//   Object01-03 (types 0-2): on their first drawn frame 20 BITMAP_CLOUD particles of SubType =
//   type, light 0.1; Object04-06 (types 3-5) 10. Then the box is hidden.
//
// Each puff (ZzzEffectParticle.cpp:3103-3117 create, :7911-7938 move, :9169-9200 draw) is
// scattered +-2.5 m about the box and 20-39 units up, Scale 1.8-2.0 of the 256-texel clouds.jpg
// (4.6-5.1 m across), and stands: its LifeTime is put back to 50 every frame its box is seen, so
// it never dies while in view. It bobs sin((WorldTime + g) / 5000) * 20 units, and turns at
// WorldTime * 0.02 * (1 + 0-0.3) degrees -- SubTypes 1 and 4 one way, 2 and 5 the other, 0 and 3
// by the particle's index -- about a turn in sixteen seconds. Added, one over the other: a single
// puff is a faint veil and the road's banks build to a bright cloud.
//
// And the lightning in them (MoveHeavenThunder, MoveObjectOnEffect, ZzzObject.cpp:4254-4405): one
// frame in fifty the sky flashes; then, one time in ten, a box in view takes a BITMAP_CLOUD + 1
// sprite (cloudLight.jpg, a violet-white lit edge) at Scale 0.5 in a random dim colour, which
// shrinks away over a few frames.
//
// Ours, the look (the user, 2026-10-06, of MU's puffs: 'look repetetive and not realistical'): MU's
// banks stand where MU put them, but each wears a few of nine smoky wisps (`sky_clouds`,
// pipeline/cloud_sheet.py's smoke(): warped streaks, blurred, no outline and no lit side; the
// user: 'more blury and smoky, and they have to be not look like puffs') in place of clouds.jpg --
// whose one hot white spot, stamped four hundred times and turning, is what read as repetition --
// alpha-blended and shaded in the moon's blue-white, each its own wisp, size, stretch and
// brightness, drifting slowly
// rather than turning at MU's rate; and under the road a sparse darker deck, so the navy has
// depth. Thin, so the navy shows through every one (the user: 'much more transparent'), and alive:
// each wanders a couple of metres about its place, breathes and deforms, and is always becoming
// another of the nine. Only the banks within kReach of the camera's point are drawn (MU draws what its frustum
// holds).
//
// And the rest of Icarus's light, which is all of it: MU's Icarus has no fire and no object light
// (MoveObject's WD_10HEAVEN arm is empty). MoveHeavenThunder and MoveObjectSetting
// (ZzzObject.cpp:4254-4363):
//   The flash: one frame in fifty, AddTerrainLight of L x (0.3, 0.3, 0.081), L 0.2-0.35, two
//   tiles round a point 1.5 m from the hero, and MODEL_CLOUD (Object11/cloud.bmd at Scale 10,
//   added, 2 m north and 1.9 m under him) for two frames. Ours: the light as a passing point light
//   for the same two frames, and the mesh as a big added cloud of the same colour.
//   The bolts: one flash in five, two BITMAP_JOINT_THUNDER + 1 between two points 4-15 m off at
//   one of four fixed layouts, 3 m under him, through fx/thunder's fork.
//   The glints: one frame in ten, a BITMAP_LIGHT effect 10 m under a point 25 m round him,
//   thrown up at 70 degrees on a heading of 30 at 4.5-6.5 units a frame and pulled down 0.01 a
//   frame, 400 frames, dropping a BITMAP_LIGHT particle each frame (flare01, light 0.3, Scale
//   1.5-3 shrinking 0.05 a frame, rising 2.5 units, 10-20 frames): sparks rising through the
//   clouds. Ours: only those within kReach are thrown.
// Not yet: MU's two thunder crackles at the lit edge. docs/icarus-port.md.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <bgfx/bgfx.h>

#include "content/cooked.h"
#include "content/texture.h"
#include "gfx/effects.h"
#include "gfx/renderer.h"

namespace mu::game {

class SkyClouds {
public:
    // Opens in Icarus: the showing's `cloud` and `cloud_light`, and the cooked town's cloud
    // anchors.
    void open(const std::string& assetDir, const std::string& world, const content::CookedTown& town,
              content::Textures& textures);
    void shutdown();
    bool isOpen() const { return open_; }

    // One frame round `near`, the point the camera follows, and `hero`, where he is, in metres.
    // `bolt` draws one thin bolt between two points (fx/thunder's fork).
    void update(float seconds, const float near[3], const float hero[3],
                const std::function<void(const float*, const float*)>& bolt);
    void gather(gfx::Effects& effects) const;
    // The flash's light while it lasts.
    uint32_t lights(gfx::PointLight* out, uint32_t max) const;

private:
    struct Puff {
        float at[3] = {0, 0, 0};
        float g = 0.0f;      // MU's Gravity, rand() % 1000: its own phase, ms
        float start = 0.0f;  // its turn at the start, radians
        float turn = 0.0f;   // radians a second, signed
        float half = 3.0f;   // the quad's half width, metres
        float stretch = 1.0f;
        float shade = 1.0f;  // times the moon's tint
        float alpha = 1.0f;
        uint8_t cell = 0;    // which of the sheet's nine
        bool deep = false;   // the deck under the road
        // Its life, ours (the user, 2026-10-06: 'they should animate'): a slow wander about its
        // place on two sines, a breath in its size, and a cross-fade between its two clouds.
        float wander = 2.0f;      // metres, the wander's reach
        float wanderHz[2] = {0.02f, 0.02f};
        float breathHz = 0.05f, morphHz = 0.04f;
        float phase[4] = {0, 0, 0, 0};
    };
    struct Bank {
        float at[3] = {0, 0, 0};
        uint32_t first = 0, count = 0;  // its puffs
    };
    struct Edge {  // a lit edge, shrinking away
        float at[3] = {0, 0, 0};
        float colour[3] = {0, 0, 0};
        float age = 0.0f;
    };
    struct Glint {  // a rising light, which only sheds sparks
        float at[3] = {0, 0, 0};
        float velocity[3] = {0, 0, 0};  // metres a reference frame
        float age = 0.0f;               // frames
    };
    struct Spark {
        float at[3] = {0, 0, 0};
        float scale = 1.0f, age = 0.0f, life = 15.0f;  // frames
    };
    std::vector<Glint> glints_;
    std::vector<Spark> sparks_;
    float flash_ = 0.0f;           // seconds left of the flash
    float flashAt_[3] = {0, 0, 0};
    float flashL_ = 0.0f;
    bgfx::TextureHandle light_ = BGFX_INVALID_HANDLE;  // flare01
    bgfx::TextureHandle flashCloud_ = BGFX_INVALID_HANDLE;  // MU's clouds.jpg
    float unit();
    void live(Puff& puff, float wander);

    bool open_ = false;
    std::vector<Bank> banks_;
    std::vector<Puff> puffs_;
    std::vector<Edge> edges_;
    float clock_ = 0.0f;  // seconds since the world opened: MU's WorldTime / 1000
    float owed_ = 0.0f;   // seconds towards the next of MU's frames
    float near_[3] = {0, 0, 0};
    uint32_t seed_ = 0x1CA705u;
    bgfx::TextureHandle cloud_ = BGFX_INVALID_HANDLE;  // sky_clouds, the nine
    bgfx::TextureHandle edge_ = BGFX_INVALID_HANDLE;
};

}  // namespace mu::game
