// The worn pet, drawn: the Guardian Angel flying about the character and the Imp riding his
// shoulder. Drawing only -- what a pet DOES is the realm's (sim::PetPower); this reads slot 8.
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
// Not yet here: the Angel's four grey sparks a frame and green BITMAP_LIGHT, and the Imp's red
// one (docs/pets.md).
#pragma once

#include <cstdint>
#include <vector>

#include "game/crowd.h"
#include "game/figures.h"
#include "gfx/renderer.h"

namespace mu::game {

class Pets {
public:
    void open(const Figures& figures);
    // `pet` is the row's number in group 13 -- 0 the Angel, 1 the Imp -- or -1 for none worn.
    // `hero` is his drawn figure, `alive` whether he stands.
    void update(float seconds, const Figure& hero, int pet, bool alive);
    // After the hero is posed this frame: the Imp takes his clavicle as it is now.
    void gather(gfx::Renderer& renderer, const Figure& hero, std::vector<float>& scratch,
                std::vector<gfx::Drawable>& out, std::vector<gfx::Drawable>* casters);

private:
    void spawnAngel(const float owner[3]);
    void stepAngel(const float owner[3]);
    float roll();  // 0..1
    int dice(int n) { return int(roll() * float(n)) % n; }  // rand() % n

    const FigureBody* angelBody_ = nullptr;
    const FigureBody* impBody_ = nullptr;
    Figure angel_, imp_;
    int shown_ = -1;
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
};

}  // namespace mu::game
