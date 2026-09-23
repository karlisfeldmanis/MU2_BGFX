// The birds over the town, and the two calls they make.
//
// `GOBoid.cpp` keeps a small pool around the player -- five birds -- spawns them within a few
// tiles of wherever the hero is standing, steers them by a flocking rule, and lets them expire
// when they wander off. None of it is placed on the map: it exists only near whoever is
// looking, which is why walking across Lorencia never runs out of birds, and why the cook has
// to be told the model by name (tools/cook.py's AIRS) -- nothing else reaches it.
//
// Ported from MU2's `client/core/Boids.cs`, which traced the client's numbers down to the odd
// ones and then judged four of them. Both halves are carried:
//
// **The client's.** Birds cruise between two and six metres of ABSOLUTE height, turn no faster
// than thirteen degrees a reference frame, dive at the player only during the first quarter of
// each 8192 ms cycle and only from between two and four metres out, retire at a flat
// one-in-512 chance a frame, and call on two INDEPENDENT one-in-512 rolls -- `if
// (rand_fps_check(512)) PlayBuffer(SOUND_BIRD01, o); if (rand_fps_check(512))
// PlayBuffer(SOUND_BIRD02, o);` -- inside `Range < 600`, six tiles. A reference frame is one of
// MU's twenty-five a second (`REFERENCE_FPS` in ZzzAI.h) and every per-frame number here is
// scaled by that, never by the rate actually drawn. MU2 ran them at sixty for a while, which
// put every rate at two and a half times the client's.
//
// **MU2's four deviations, each kept for its stated reason.**
//   1. *The flock arrives together, on a ring.* The client fills every empty slot in the same
//      frame, in one ten-tile box, all with `Angle` zeroed -- five birds born side by side
//      pointing the same way, which is the only reason its flocking rule holds them together
//      at all (a bird with no neighbour within four tiles flies dead straight). But it spawns
//      them within five tiles of the hero, which is the middle of the screen, so birds
//      materialise in front of the player. Here the box's centre is on a ring 8 to 12 m out
//      and the bearing is redrawn until the whole box lands off screen, so the flock flies IN.
//   2. *The sky empties between flocks.* The client refills a slot the frame it empties and so
//      always has five. A flock crosses at six and a quarter metres a second and is past
//      within a few seconds: birds are passes, not residents.
//   3. *Nothing is deleted in shot.* The client drops a boid where it stands -- strayed, or its
//      one-in-512 came up -- which at MU's camera was mostly off screen and mostly got away
//      with. Everything that used to end a bird now only asks it to go; it turns away from the
//      player, keeps flying, and is taken off the first frame it is out of the frame.
//   4. *No birds indoors.* MU has no notion of a room and blows them through the tavern. The
//      roof fades off a building here, so a gull crossing the ceiling gives the whole thing
//      away. The same `World::indoors` test the roofs and the leaves use, so all three agree.
//
// The fish are not here, for the reason MU2 gave: MU's water is a texture painted on the ground
// rather than a surface with depth, so a fish either skates on the pond or is buried under it.
//
// Every length is MU's own divided by a hundred, because MU works in units where a tile is a
// hundred and this works in metres where a tile is one.
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
#include "gfx/renderer.h"

namespace mu::game {

class Sound;

// What a world's air does, which is a property of the PLACE and not of the thing flying in it:
// MU decides all of it with `switch (gMapManager.WorldActive)` in GOBoid.cpp. MU2 gathered the
// same four into `Airs.cs` and this is that record. The defaults are Lorencia's bird.
struct Airs {
    // `o->Velocity` for the pool: a bird's 1.0, Noria's butterfly's 0.3. A factor on the
    // cruise rather than a second cruise, because it is a factor in the client too --
    // `MoveBoidGroup` steps every boid `o->Velocity * 25.f`.
    float speed = 1.0f;
    // `o->LightEnable`: whether the terrain's light falls on it. True for a bird, which
    // darkens over the shaded side of a building; false for the butterfly, which is drawn at
    // full white. MU sets both explicitly and they are opposite, so neither is a default.
    bool lit = true;
    // The `o->Light` beside it, which means something only while `lit` is false.
    float tint[3] = {0.5f, 0.5f, 0.5f};
    // Whether it calls. GOBoid puts the two plays inside `o->Type == MODEL_BIRD01`, with a
    // bat's own sound and a crow's beside it in the same chain; the butterfly is not in that
    // chain at all. A butterfly that chirps is a plausible mistake about a different animal.
    bool calls = true;
};

// What a world flies, by the model name the cook knows it by, and how it flies. MU's own
// `switch (gMapManager.WorldActive)`. The name must agree with tools/cook.py's AIRS, which is
// the only reason the model is in the cooked directory at all; a world named in neither flies
// nothing, which is most of them.
std::string boidOf(const std::string& world);
Airs airsOf(const std::string& world);

class Boids {
public:
    // Opens the pool over a world. The mesh and the clip are `cooked/<world>/meshes/<model>.mum`
    // and `cooked/<world>/clips/<model>.muc`, which tools/cook.py writes for the boid although
    // nothing places it. A world with no cooked boid opens silently and flies nothing -- it is
    // not an error, it is a map with no wildlife.
    bool open(const std::string& assetDir, const std::string& world, const std::string& model,
              content::Textures& textures, const Airs& airs, Sound* sound);
    void shutdown();

    // One frame. `hero` is where the character is drawn, `walking` whether he is moving (which
    // startles a perched bird), `indoors` whether he is under a roof. `viewProj` and the
    // viewport's size decide what counts as on screen -- the arrival and the retirement both
    // read it, so a bird never appears or vanishes in shot. A null `viewProj` means nobody is
    // looking, and the pool then retires its birds exactly as the client does.
    void update(float seconds, const float hero[3], bool walking, bool indoors,
                const content::Ground& ground, const float* viewProj, gfx::Renderer& renderer);
    // The posed birds, into the frame's drawable list. After update.
    void gather(std::vector<gfx::Drawable>& out) const;

    bool isOpen() const { return body_ != nullptr; }
    // --birds-now: the sky's FIRST flock arrives on the next frame rather than 20 to 90
    // seconds in. For a review run, which is a few seconds long.
    //
    // One-shot, because the caller has it in the frame loop and cannot easily say "once":
    // zeroing the wait every frame refills the sky the instant it empties, which is the one
    // thing deviation 2 exists to prevent -- a review run showed 5 birds, then 1, then 4 a
    // second later, which is a carousel and not a flock.
    void hurry() {
        if (hurried_) return;
        hurried_ = true;
        wait_ = 0.0f;
    }
    // How many are in the air right now, for the stats line.
    uint32_t flying() const { return flying_; }

private:
    // Birds in the air at once. The client breaks its loop at five.
    static constexpr int kMaxBirds = 5;

    enum class Flight : uint8_t { Fly, Down, Ground, Up };

    struct Bird {
        Figure figure;
        float position[3] = {0.0f, 0.0f, 0.0f};
        // Where it will be three of MU's frames from now, which is the point the others steer
        // by -- the client's `Direction = Position + 3 * p`, said in time so the flock reads
        // the same at any frame rate.
        float heading[2] = {0.0f, 0.0f};
        float facing = 0.0f;
        float speed = 0.0f;
        float climb = 0.0f;
        Flight state = Flight::Fly;
        bool live = false;
        // Told to go, and flying out rather than gone. See deviation 3 in the header.
        bool leaving = false;
        int paletteRow = -1;
        // MU's baked terrain light under it this frame, or the unlit boid's own colour.
        float light[3] = {1.0f, 1.0f, 1.0f};
    };

    void arrive(const float hero[3], const content::Ground& ground, const float* viewProj);
    void move(Bird& bird, const float hero[3], bool walking, float seconds, float factor,
              const content::Ground& ground, const float* viewProj);
    void call(const Bird& bird, const float hero[3], float factor);
    void flock(Bird& bird, float factor);
    void away(Bird& bird, const float hero[3], float factor);
    void step(Bird& bird, float speed, float seconds);
    float wander(float seconds);
    bool chance(float perFrame, float factor);
    float random01();

    std::unique_ptr<content::Mesh> mesh_;
    std::unique_ptr<ClipLibrary> library_;
    std::unique_ptr<FigureBody> body_;
    Bird birds_[kMaxBirds];
    std::vector<float> scratch_;
    Airs airs_;
    Sound* sound_ = nullptr;
    int call1_ = -1;
    int call2_ = -1;
    // Seconds until the next flock, once the sky is empty, and where MU's 8.192 s dive cycle
    // has got to.
    float wait_ = 0.0f;
    float cycle_ = 0.0f;
    bool hurried_ = false;
    uint32_t flying_ = 0;
    uint32_t seed_ = 0x9E3779B9u;
};

}  // namespace mu::game
