// The lamps: what burns in a town, and the three things MU makes of each. Sprint 8a;
// docs/sprints/08a-the-lamps.md has the design and the MuMain lines behind every number.
//
// 1. **The light**, a point light handed to the renderer once as a static set and flickered
//    every frame through its level.
// 2. **The glow**, MU's BlendMesh: drawn by the renderer from the town's own instances, and
//    only its flicker lives here -- a level per placement, written into the town.
// 3. **The fire**: flames, embers and smoke at every fire near the camera, written into the
//    transparent pass. MU's flame shapes, our motion and colour; lamps.cpp says why.
//
// It is `game`: it knows what a fire is. It is not `sim`, and nothing it rolls reaches the
// seeded log -- where a flame flickers is not a fact.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/cooked.h"
#include "content/ground.h"
#include "content/texture.h"
#include "gfx/effects.h"
#include "gfx/renderer.h"

namespace mu::game {

class Town;

class Lamps {
public:
    // Every light and fire the cooked town names, resolved into the world. The fire's sheet is
    // the showing's `fire`, Effect/Fire01; without it the fires burn with no flame drawn.
    bool open(const std::string& assetDir, const Town& town, const content::Ground& ground,
              content::Textures& textures);
    void shutdown();

    // Hands the static set to the renderer, once a world is open. The grid is built there.
    void light(gfx::Renderer& renderer) const;

    // One frame: every flicker eased towards its target, the glows written into the town, the
    // lights' levels into the renderer, and the fires near `near` burning.
    void update(float seconds, Town& town, gfx::Renderer& renderer, const float near[3]);

    // The flames within `kFlameMetres` of `near`, into the transparent pass. Every fire burns
    // wherever the camera is; only the near ones are drawn, so none is ever seen lighting.
    // `daylight` is how much light the scene has, 0 at night to 1 at noon; the smoke is lit
    // by it, since the transparent pass lights nothing.
    void gather(gfx::Effects& effects, const float near[3], float daylight) const;

    uint32_t lightCount() const { return uint32_t(lights_.size()); }
    uint32_t fireCount() const { return uint32_t(fires_.size()); }
    uint32_t flameCount() const { return uint32_t(particles_.size()); }
    uint32_t flamesDrawn() const { return drawn_; }

    static constexpr float kFlameMetres = 45.0f;

private:
    // MU2's answer to MU's re-roll: MU picks a new brightness every rendered frame, which on a
    // LIGHT is a strobe -- the pool on the ground jitters at the frame rate. So each flickers
    // at a rate and eases to it. Lamps.cs, and the numbers are index.json's.
    struct Flicker {
        float low = 1.0f, high = 1.0f, hz = 0.0f, smooth = 0.0f;
        float current = 1.0f, target = 1.0f, wait = 0.0f;
    };
    struct Light {
        Flicker flicker;
    };
    struct Glow {
        uint32_t instance = 0;
        Flicker flicker;
    };
    // Where a fire burns and which way its flames lean: MU's (0, -v, 0) turned by the
    // object's angle, a unit vector over a hundred, in metres per MU unit.
    struct Fire {
        float at[3] = {0, 0, 0};
        float drift[3] = {0, 0, 0};
        float spin = 0.0f;
        bool bonfire = false;
        // What each kind is owed, in particles; a whole one is spawned and taken off.
        float clock = 0.0f, embers = 0.0f, smoke = 0.0f;
    };
    // A flame, an ember or a puff, in metres and seconds. Sprint 8b; lamps.cpp says why none
    // of it is MU's clock any more.
    static constexpr uint8_t kFlame = 0, kEmber = 1, kSmoke = 2;
    struct Particle {
        float position[3] = {0, 0, 0};
        float velocity[3] = {0, 0, 0};
        float age = 0.0f, life = 1.0f;
        float size = 0.5f;
        float heat = 1.0f;
        float spin = 0.0f, spinRate = 0.0f;
        float phase = 0.0f;
        uint8_t kind = kFlame;
        uint8_t cell = 0;
    };

    void step(Flicker& one, float seconds);
    void spawn(const Fire& fire, uint8_t kind);
    uint32_t next();
    float unit();

    std::vector<gfx::PointLight> set_;
    std::vector<Light> lights_;
    std::vector<float> levels_;
    std::vector<Glow> glows_;
    std::vector<Fire> fires_;
    std::vector<Particle> particles_;
    float minX_ = 0.0f, minZ_ = 0.0f, side_ = 0.0f;
    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle spark_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;
    mutable uint32_t drawn_ = 0;
    uint32_t refused_ = 0;
    // Fixed, so two runs of the same --fixed-dt draw the same flames. Not the sim's dice.
    uint32_t seed_ = 0x2545F491u;
};

}  // namespace mu::game
