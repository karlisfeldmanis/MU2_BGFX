// The rats on the Dungeon's floor: MU's fish slot, which MoveFishs fills with MODEL_RAT01 on
// WD_1DUNGEON (GOBoid.cpp:1659-1860). Owned by Boids, opened, stepped and drawn with the birds.
//
// The client's rule, in metres and in MU's twenty-five reference frames a second:
//   * **Three at most** (the loop skips i >= 3 off Atlans and Tarkan). An empty slot is refilled
//     the frame it empties, on a tile within 5.12 m of the hero whose attribute is under
//     TW_NOGROUND; a draw that lands on the void waits for the next frame.
//   * **Scale 0.4 to 0.7, speed 0.6 / scale**, light 0.5 and lit by the terrain, faded in from
//     nothing (`Alpha = 0, AlphaTarget = 1`), facing MU's zeroed angle.
//   * **In bursts.** `LifeTime = rand() % 128` frames of running at `Velocity * (6..9)` units a
//     frame, steered by MoveBoid (ZzzAI.cpp:192, the birds' flocking at 4 m and 0.8 m, turning
//     `Gravity` 13 degrees a frame) and stuck to the ground; then it stands, and each frame after
//     it has one chance in 64 of another burst.
//   * **The void turns it back.** A step onto a tile at or past TW_NOGROUND turns it 180 degrees
//     and counts a strike, a step onto floor takes one off, and two strikes retire it. So does
//     wandering 15 m from the hero.
//   * **It squeaks** aMouse.wav at itself, one frame in 256 while running within 6 m.
//
// Ours, and marked: the clip holds while it stands (MU keeps playing the run at Velocity * 0.5,
// a rat running on the spot), and one retired fades out over the same time it faded in rather
// than blinking off where it stood (MU's `Live = false`).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "content/ground.h"
#include "content/mesh.h"
#include "content/texture.h"
#include "game/crowd.h"
#include "game/figures.h"
#include "gfx/effects.h"
#include "gfx/renderer.h"

namespace mu::game {

class Sound;

// What a world runs along its floor, by the model name tools/cook.py's CRAWLS cooks it under:
// the Dungeon's rats and Tarkan's scarabs. MoveFishs' other arms are Lorencia's fish (not built,
// see boids.h) and maps this game does not have.
std::string crawlOf(const std::string& world);

// MoveFishs' numbers for one world's crawler (GOBoid.cpp:1700-1735).
struct CrawlRow {
    int most = 3;            // slots filled: 3, or all ten on Atlans and Tarkan (:1668-1675)
    int scaleLeast = 4;      // Scale (rand() % 4 + least) * 0.1
    float speed = 0.6f;      // Velocity = speed / Scale
    float turn = 13.0f;      // Gravity, degrees a frame
    int life = -1;           // LifeTime: -1 is rand() % 128
    bool squeaks = false;    // aMouse.wav, the rat's
    // A BITMAP_JOINT_ENERGY trail behind each (Tarkan's SubType 4, ZzzEffectJoint.cpp:262-288,
    // 445): 20 tails, Scale 30, dull brown (0.3, 0.15, 0.1), added. False: none.
    bool trail = false;
    // Drawn at this of MU's Scale, its motion still MU's. Ours: Tarkan's scarabs at 0.55, about
    // 20 cm for MU's 35 (the user, 2026-10-05: 'make thos bugs smaller').
    float drawn = 1.0f;
};
CrawlRow crawlRowOf(const std::string& world);

class Scurry {
public:
    static constexpr int kMaxRats = 10;

    // Opens the pool over a world. An empty `model`, or one not cooked, opens nothing and is not
    // an error. `cooked/<world>/meshes/<model>.mum` and `clips/<model>.muc`, as the birds'.
    bool open(const std::string& assetDir, const std::string& world, const std::string& model,
              content::Textures& textures, Sound* sound);
    void shutdown();
    bool isOpen() const { return body_ != nullptr; }

    void update(float seconds, const float hero[3], const content::Ground& ground,
                gfx::Renderer& renderer);
    void gather(std::vector<gfx::Drawable>& out) const;
    // The trails, where the world's row has them (CrawlRow::trail).
    void gatherTrails(gfx::Effects& effects) const;

private:
    static constexpr int kTails = 20;
    struct Rat {
        float position[3] = {0.0f, 0.0f, 0.0f};
        // Where it will be three reference frames on, which the others steer by (MoveBoid reads
        // `Direction`).
        float heading[2] = {0.0f, 0.0f};
        float facing = 0.0f;
        float scale = 0.5f;
        float running = 0.0f;  // LifeTime, in reference frames
        float fade = 0.0f;
        int strikes = 0;       // SubType
        bool live = false;
        bool leaving = false;
        float tails[kTails][3] = {};  // where it was, a reference frame apart, newest first
        int tailCount = 0;
        float tailClock = 0.0f;
    };

    void spawn(Rat& rat, const float hero[3], const content::Ground& ground);
    void flock(Rat& rat, float factor);
    bool floor(const content::Ground& ground, float x, float z) const;
    float random01();

    Rat rats_[kMaxRats];
    std::unique_ptr<content::Mesh> mesh_;
    std::unique_ptr<ClipLibrary> library_;
    std::unique_ptr<FigureBody> body_;
    Figure figures_[kMaxRats];
    int paletteRows_[kMaxRats] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    CrawlRow row_;
    bgfx::TextureHandle trailSheet_ = BGFX_INVALID_HANDLE;
    bool standing_[kMaxRats] = {};
    float light_[kMaxRats][3] = {};
    std::vector<float> scratch_;
    Sound* sound_ = nullptr;
    int squeak_ = -1;
    uint32_t seed_ = 0x7A7B1E55u;
};

}  // namespace mu::game
