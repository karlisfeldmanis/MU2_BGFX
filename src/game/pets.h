// The worn pet, drawn: the Guardian Angel flying about the character and the Imp riding his
// shoulder, and the mount under him. Drawing only -- what a pet DOES is the realm's
// (sim::PetPower); this reads slot 8 and the mount's slot.
//
// **The Guardian Angel flies.** MuMain's GOBoid.cpp: CreateMount puts it at the owner +-256 on
// the ground's two axes and 128-256 up (:67-126), and MoveMount (:605-660) steers it on every
// 25 Hz reference frame -- within FlyRange (150) of the owner it wanders on a random heading,
// outside it turns toward him at up to 20 degrees a frame, re-rolls its speed about one frame
// in 32, jitters +-8 in height and is pushed back into 100-200 above his feet. It plays its
// one action at Velocity 0.5, at the 0.7 CreateMount gives it (see kAngelScale). Gone while he is
// dead (:161-165).
//
// **The Imp rides the shoulder and never flies.** ChangeCharacterExt makes a follower for
// helper 0 alone (ZzzCharacter.cpp:12650-12662); the Imp is drawn by RenderLinkObject, unlinked,
// on player bone 34 -- Bip01 L Clavicle -- at (20, 0, 0) in the bone's frame, in the bone's
// own rotation, at the character's scale, PlaySpeed 0.5 (ZzzCharacter.cpp:15433-15466).
//
// **The Horn of Uniria is ridden** (docs/game/mount.md): its horse, Rider01, stands on his spot
// facing his way, under his ride clips, and is hidden in town where he walks. The Dinorant's
// dragon, Rider02, and the Dark Lord's Dark Horse, DarkHorse, the same way.
//
// Not yet here: the Angel's four grey sparks a frame and green BITMAP_LIGHT, and the Imp's red
// one (docs/game/pets.md).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "game/crowd.h"
#include "game/figures.h"
#include "gfx/renderer.h"

namespace mu::game {
// How far the Dinorant lifts its rider, metres: on a ground map MU sets him 30 over the terrain
// (ZzzCharacter.cpp:6381-6390) and draws the dragon 30 under him (GOBoid.cpp:517-523).
constexpr float kDinorantLift = 0.30f;
// And over Tarkan (and Icarus) he flies: 90 over the terrain (ZzzCharacter.cpp:6387-6390) with
// the dragon only 10 under him (GOBoid.cpp:517-520), so it is in the air at 80.
constexpr float kDinorantFlyLift = 0.90f;
constexpr float kDinorantFlyUnder = 0.10f;
// Whether a world is one the Dinorant flies over is its manifest's `mounts_fly`
// (game/world/world_show.h): Icarus alone. MU flies it over Tarkan too; ours walks it there, the
// user's (2026-10-16): "dyno has to fly only on fly maps not walking maps".

// And how far to lift him again, metres, at `through` (0..1) of his run ride: the dragon's back
// less his pelvis, key by key. MU plays one run ride, 36/37, on both mounts, and Uniria's leap
// rises and falls with it key for key -- its Bip01 Spine at +6 -18 +8 +6 -18 +8 +7 MU units about
// its mean, the rider's Bip01 Pelvis at +7 -18 +7 +6 -17 +8 +7 -- but the Dinorant's run is its
// own, +16 -16 -3 +11 -19 -5 +17, down on keys 2 and 5 where his pelvis is up (measured off both
// rigs and player.bmd's 36, 2026-10-02; the user: "bouncing with dyno is not perfectly synced").
// Added to his seat it puts his pelvis on the dragon's back through the whole bound. **ours**.
inline float dinorantBob(float through) {
    constexpr float kDragonLessRider[7] = {9.4f, 1.3f, -10.4f, 5.0f, -2.6f, -12.6f, 10.1f};
    const float key = std::fmod(std::max(0.0f, through), 1.0f) * 6.0f;
    const int at = std::min(5, int(key));
    const float t = key - float(at);
    return (kDragonLessRider[at] * (1.0f - t) + kDragonLessRider[at + 1] * t) * 0.01f;
}
}  // namespace mu::game

namespace mu::game {

class Pets {
public:
    void open(const Figures& figures);
    // Over a world where the Dinorant flies (`mounts_fly`), the dragon is drawn 10 under him,
    // not 30.
    void setFlying(bool flying) { flying_ = flying; }
    // `pet` is the row's number in group 13 -- 0 the Angel, 1 the Imp -- or -1 for none worn.
    // `hero` is his drawn figure, `alive` whether he stands.
    void update(float seconds, const Figure& hero, int pet, bool alive);
    // And the horse, after `update`: `mount` is the mount slot's row number in group 13 -- 2 the
    // Horn of Uniria, 3 Dinorant, 4 the Dark Horse -- or -1. Drawn under him while `riding`
    // (sim::Body::riding), gone in town; `action` is its own clip as GOBoid picks it off his --
    // 2 riding on, 3 a swing, 0.
    void ride(float seconds, const Figure& hero, int mount, bool riding, int action);
    // The Dark Horse's breath, after `ride`: GOBoid's rand_fps_check(3) on every reference frame,
    // a BITMAP_SMOKE at (50, -4, 0) in bone 27's frame, Bone03, its head (GOBoid.cpp:432-447) --
    // whatever it is doing. True, with the world point in `at`, when one is due this frame.
    bool breathe(float seconds, float at[3]);
    // The mount's gallop, for a recording's cut (Play::Stride).
    float gallop() const { return horse_.clock(); }
    void setGallop(float clock) { horse_.setClock(clock); }
    // After the hero is posed this frame: the Imp takes his clavicle as it is now.
    void gather(gfx::Renderer& renderer, const Figure& hero, std::vector<float>& scratch,
                std::vector<gfx::Drawable>& out, std::vector<gfx::Drawable>* casters);

    // **The Dark Raven** (game/pets_raven.cpp; docs/plans/dark-lord-pets/client.md section 1),
    // MuMain's CSPetSystem: `level` its level, 0 for none worn; `safe` his tile, where it perches on
    // his left hand; `horse` the Dark Horse worn, which lifts its flight to 350. After `update`.
    void raven(float seconds, const Figure& hero, int level, bool safe, bool horse);
    // Its strike as the realm said it (What::Ravened, MU's C1 A8): the dash on `target`, or the
    // ball let go at it after its charge.
    void ravenStrike(bool ball, const float target[3]);
    // Where the ball goes out from, once, the frame its charge ends: true with its head in `from`.
    bool ravenLetGo(float from[3], float to[3]);
    // Where it is, for its sounds and a level's flares; false while none flies.
    bool ravenAt(float out[3]) const;
    // Its cry, one reference frame in 6000 (CSPetSystem.cpp:592): true the frame it calls.
    bool ravenCry(float seconds);

private:
    void stepRaven(const float owner[3], const float hand[3], bool safe, bool horse);
    void drawRaven(gfx::Renderer& renderer, std::vector<float>& scratch,
                   std::vector<gfx::Drawable>& out, std::vector<gfx::Drawable>* casters);
    void spawnAngel(const float owner[3]);
    void stepAngel(const float owner[3]);
    float roll();  // 0..1
    int dice(int n) { return int(roll() * float(n)) % n; }  // rand() % n

    const FigureBody* angelBody_ = nullptr;
    const FigureBody* impBody_ = nullptr;
    const FigureBody* horseBody_ = nullptr;
    const FigureBody* dragonBody_ = nullptr;
    const FigureBody* darkHorseBody_ = nullptr;
    int breathBone_ = -1;      // the Dark Horse's Bone03, by name
    float breathOwed_ = 0.0f;  // reference frames toward the next roll
    const FigureBody* horseOf_ = nullptr;  // which of the two `horse_` stands as
    Figure angel_, imp_, horse_;
    bool horseUp_ = false;
    bool flying_ = false;  // setFlying
    float horseIn_ = 0.0f;  // 0 gone, 1 there: the fade at a safe zone's edge
    // Where the rider stood last frame: a jump past kHorseJump (a respawn, a warp) takes the horse
    // away at once rather than fading it in his new place.
    float riderAt_[3] = {};
    bool riderSeen_ = false;
    float horseHeld_ = 0.0f;  // seconds the horse stays away after that jump
    int shown_ = -1;    // the pet's number
    int mounted_ = -1;  // the mount's
    bool angelUp_ = false;
    float angelIn_ = 0.0f;  // seconds since it appeared, for its fade in
    // Its flight in MU's own space and units -- x east, y north, z up, 100 to a metre -- so
    // GOBoid's numbers are used as they are written.
    float at_[3] = {0, 0, 0};
    float direction_[3] = {0, 0, 0};
    float heading_ = 0.0f;  // Angle[2], degrees
    // Where the last step started, and the heading as drawn. The flight is stepped at MU's 25 Hz
    // and DRAWN between its last two steps, as the bodies are between their ticks; stepped and
    // drawn as it stands it jumped every seventh frame at 180 fps (the user: "laggy").
    float was_[3] = {0, 0, 0};
    float drawnHeading_ = 0.0f;
    float frames_ = 0.0f;   // reference frames owed
    uint32_t seed_ = 0x9e3779b9u;
    const FigureBody* heroBody_ = nullptr;
    int clavicle_ = -1;

    // The Dark Raven's: its figure and its three bodies' materials by level, and the feathers.
    enum class Bird : uint8_t { Fly, Flying, Perching, Perched, Dash, Away, Charge };
    const FigureBody* ravenBody_ = nullptr;
    Figure raven_;
    int ravenLevel_ = 0;
    int ravenMaterials_[4] = {-1, -1, -1, -1};  // dkonebody, dkswing, dkthreebody, dksbody
    int hand_ = -1;                              // his Bip01 L Hand
    bool ravenUp_ = false;
    float ravenIn_ = 0.0f;
    Bird bird_ = Bird::Fly;
    float bat_[3] = {}, batWas_[3] = {};  // in MU's space, as the Angel's at_
    float batDir_[3] = {};
    float batYaw_ = 0.0f, batPitch_ = 0.0f, batYawDrawn_ = 0.0f;
    float batSpeed_ = 0.0f, batPull_ = 0.0f;  // the dash's Velocity and Gravity
    float batPrey_[3] = {};                   // the strike's target
    int batTicks_ = 0, batLost_ = 0;
    float batFrames_ = 0.0f;
    bool letGo_ = false;
    int clipShown_ = -1;
};

}  // namespace mu::game
