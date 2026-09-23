#include "game/world/world.h"

#include <cmath>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {

// MU's own camera, measured out of MU2's client/core/Walk.cs rather than chosen. The
// distance and the field of view set the framing together -- 8 metres back through a
// 74-degree lens is a far wider picture than 8 metres through a 55 -- so neither number
// means anything without the other, and both are the old client's.
constexpr float kFovDegrees = 55.0f;
constexpr float kPitchDegrees = -48.5f;
constexpr float kYawDegrees = 45.0f;
// 800 of MU's units, and now the only distance there is: played or measuring, the camera
// stands here. There was a wheel zoom between 3.5 and 8 m and a played camera that started
// at 6; both are gone, on 2026-09-24, and MU's own 8 m is what is left -- which is also the
// furthest the wheel had ever allowed.
//
// It was removed for the frame and not for the look. A distance that moves is a frustum that
// moves, and with it every cull the frame depends on: which chunks the camera keeps, how many
// placements the sun's split frames, which band of grass is standing and how tall its cards
// come out on screen. Each of those was tuned at one framing and each has a worst case, and
// with the wheel in the player's hand the worst case was whatever he had last scrolled to --
// so a frame measured at 8 m proved nothing about the frame he was looking at. Fixed, the
// cull set is the same set every frame, the numbers in docs/budget.md are numbers about the
// game, and a spike has one fewer thing it can be.
constexpr float kDistance = 8.0f;
constexpr float kFocusHeight = 1.5f;   // 150 units up the body
// How far above the middle of the frame a played character is drawn, as a fraction of the
// frame's height. Invention, the ARPG habit rather than MU's: MU centres him.
constexpr float kLiftFrame = 0.12f;

// The tile texture Lorencia floors its interiors with and uses nowhere outdoors: MuMain's
// HeroTile == 4, and MU2's World.IndoorTile.
constexpr int kIndoorFloor = 4;

// The played camera's follow, all three inventions -- MU's camera is nailed to the character.
// Nailed, it passes on every hitch in the drawn position, and it rises and falls with every
// bump in the ground under his feet, which on Lorencia's cobbles is a bob at walking pace. So
// a critically damped spring: no overshoot, no wobble, and it sets off and stops with the
// character instead of jerking. The times are how long it takes to close most of the gap --
// short across the ground, so he never drifts far off the middle of the frame (about 20 cm
// at a walk), and longer up and down, where there is nothing to catch up with but a bump.
constexpr float kFollowSeconds = 0.09f;
constexpr float kRiseSeconds = 0.25f;
// A jump further than this is a respawn or a gate, and the camera is simply there.
constexpr float kSnapMetres = 4.0f;
// Game Programming Gems 4's SmoothDamp, the closed form of a critically damped spring.
float smoothDamp(float current, float target, float& velocity, float time, float dt) {
    const float omega = 2.0f / time;
    const float x = omega * dt;
    const float decay = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    const float change = current - target;
    const float temp = (velocity + omega * change) * dt;
    velocity = (velocity - omega * temp) * decay;
    return target + (change + temp) * decay;
}

}  // namespace

void World::tileToMetres(float column, float row, float* x, float* z) const {
    // A tile's CENTRE, and in the world's own metres per tile. docs/conventions.md states
    // the centre as (column + 0.5, -(row + 0.5)); this dropped the half and hard-coded one
    // metre to the tile, so the camera stood on the corner of the tile it named and any map
    // whose units_per_tile is not 100 would have put it somewhere else entirely -- the very
    // division Ground::heightAt already reads from the world's json rather than assuming.
    const float metresPerTile = ground_.metresPerTile();
    *x = (column + 0.5f) * metresPerTile;
    *z = -(row + 0.5f) * metresPerTile;
}

void World::setFocusTile(float column, float row) {
    focusColumn_ = column;
    focusRow_ = row;
    focusSet_ = true;
}

bool World::open(const std::string& assetDir, const std::string& name,
                 content::Textures& textures, int crowd, bool figures) {
    const std::string dir = core::join(assetDir, "world/" + name);
    textures_ = &textures;
    if (!ground_.load(dir, name, textures)) return false;
    // The town is not required: the land is a world on its own, and a cook that has not been
    // run yet says so in the log rather than failing the launch.
    town_.open(assetDir, name, textures);
    // And what burns in it. Nothing without a town, since every light hangs on a placement.
    if (town_.isOpen()) lamps_.open(assetDir, town_, ground_, textures);
    if (town_.isOpen()) sway_.open(assetDir, name, town_);
    // And what rides the swaying bones: the fountain's spray, the lanterns.
    if (town_.isOpen()) ornaments_.open(assetDir, town_, textures);
    // The near field: the card strip, built once, and MU's own painted grass sheets for
    // whichever tile slots this world calls grass. Which TILES are grassy is not settled here
    // -- it is asked of the ground every frame, because the disc moves with the camera.
    // docs/grass.md.
    grass_.build(assetDir, name, ground_, textures);

    if (!focusSet_) {
        // Lorencia's safe zone is around tile 142,126 -- the middle of the town rather than
        // the middle of the map, which is sea and empty grass.
        focusColumn_ = 142.0f;
        focusRow_ = 126.0f;
    } else {
        // A camera off the map fails the run rather than drawing the empty frame it would
        // otherwise draw. `--at 9999,9999` exited 0, showed nothing, and reported a *better*
        // frame time than any real camera can -- a supported way to publish a good number for
        // a picture of nothing. The map's own size is the thing to say in the message,
        // because it is what the caller has to know to fix the command.
        const float size = float(ground_.size());
        if (focusColumn_ < 0.0f || focusRow_ < 0.0f || focusColumn_ >= size ||
            focusRow_ >= size) {
            core::logError("--at %.0f,%.0f is off %s: the map is %d tiles a side, so a column "
                           "and a row run 0 to %d",
                           focusColumn_, focusRow_, name.c_str(), ground_.size(),
                           ground_.size() - 1);
            return false;
        }
    }
    // The figures are not required either, for the same reason the town is not: a cook that
    // has not been run says so in the log rather than failing the launch.
    if (figures && figures_.open(assetDir, name, textures)) {
        int monsters = crowd;
        if (monsters < 0) {
            monsters = 0;
            for (const Breed& breed : figures_.breeds()) monsters += breed.total;
        }
        crowd_.open(figures_, ground_, monsters, focusColumn_, focusRow_);
    }

    camera_.fovDegrees = kFovDegrees;
    camera_.nearPlane = 0.05f;
    camera_.farPlane = 1200.0f;
    float focusX = 0.0f, focusZ = 0.0f;
    tileToMetres(focusColumn_, focusRow_, &focusX, &focusZ);
    core::logf("world %s: looking at tile %.0f,%.0f, ground %.2f m up", name.c_str(),
               focusColumn_, focusRow_, ground_.heightAt(focusX, focusZ));
    return true;
}

bool World::play(const std::string& assetDir, const std::string& name, uint64_t seed, int kin,
                 int level, const std::string& weapon, const std::string& shield) {
    // What the character looks like, built here rather than taken off a cooked row: the naked
    // class body -- which is what a character IS before he has picked anything up -- holding
    // whatever the sim is about to be told he holds. mu.db's own class enumeration, which is
    // what `--kin` speaks: 0 Dark Wizard, 1 Fairy Elf, 2 Dark Knight.
    //
    // `FairyElf` is the elf's bare body and is named without the suffix the other two carry;
    // that is index.json's spelling and not a slip.
    const char* bare = kin == 0 ? "DarkWizardBare" : (kin == 1 ? "FairyElf" : "DarkKnightBare");
    const FigureBody* look = figures_.dress("Hero", bare, weapon, shield);
    // The character is put down where the camera was told to look, which is the town by
    // default and `--at` otherwise. The realm moves him to the nearest tile he may stand on.
    if (!play_.open(assetDir, name, &ground_, &figures_, seed, kin, level, int(focusColumn_),
                    int(focusRow_), weapon, shield, look, bare)) {
        return false;
    }
    // And the crowd stands down: the same monsters would otherwise be drawn twice, once where
    // the sim has them and once where the crowd chose to put them.
    crowd_.shutdown();
    return true;
}

void World::raiseAirs(const std::string& assetDir, const std::string& name) {
    // What flies over the character and what blows past him. Both pools are centred on HIM and
    // exist nowhere else -- MU spawns them around the player and never anywhere on the map --
    // so a world standing with nobody in it raises neither.
    //
    // **Called after the play's showing and its sound are open, and not from play() itself.**
    // The bird takes its two calls off that sound and the leaf its sheet off that table, and
    // both of those open several lines LATER than play() does: raised from play(), the birds
    // came up "silent" and the leaves said there was no cooked effect named 'leaf' when there
    // plainly was one. Neither failure was fatal and neither was a lie -- at the moment they
    // asked, the table was empty and the device was shut.
    if (name.empty() || textures_ == nullptr || !play_.isOpen()) return;
    boids_.open(assetDir, name, boidOf(name), *textures_, airsOf(name), &play_.sound());
    leaves_.open(assetDir, *textures_, play_.showing().table());
}

void World::update(double seconds, bool still) {
    // A world being played follows its character rather than the sine walk below: the camera
    // is MU's, fixed over the man, and where he is standing is the sim's answer and not the
    // frame's.
    if (play_.isOpen()) {
        play_.focus(&focusColumn_, &focusRow_);
        still = true;
    }
    // A slow walk across the town when the camera is not held. The point is not the walk:
    // it is that a still camera cannot show a shadow edge crawling, which is the one defect
    // sprint 1's bench was structurally unable to catch.
    float column = focusColumn_;
    float row = focusRow_;
    if (!still) {
        column += float(std::sin(seconds * 0.15) * 12.0);
        row += float(std::cos(seconds * 0.11) * 12.0);
    }

    // Column is +x and row is -z. docs/conventions.md.
    float x = 0.0f, z = 0.0f;
    tileToMetres(column, row, &x, &z);
    float groundY = ground_.heightAt(x, z);
    // The roofs, on the feet the camera is framing -- the character's own, not the eased
    // point, so a roof lifts the frame he steps under it.
    const float feetX = x, feetZ = z;

    // The frame's own seconds, which this is not handed: `seconds` is the run's clock.
    const float dt = lastSeconds_ < 0.0 ? 0.0f
                                         : float(std::fmin(0.1, std::fmax(0.0, seconds - lastSeconds_)));
    lastSeconds_ = seconds;
    if (play_.isOpen()) {
        const float jump = std::hypot(x - eased_[0], z - eased_[2]);
        if (!easedSet_ || jump > kSnapMetres || dt <= 0.0f) {
            eased_[0] = x;
            eased_[1] = groundY;
            eased_[2] = z;
            easing_[0] = easing_[1] = easing_[2] = 0.0f;
            easedSet_ = true;
        } else {
            eased_[0] = smoothDamp(eased_[0], x, easing_[0], kFollowSeconds, dt);
            eased_[1] = smoothDamp(eased_[1], groundY, easing_[1], kRiseSeconds, dt);
            eased_[2] = smoothDamp(eased_[2], z, easing_[2], kFollowSeconds, dt);
        }
        x = eased_[0];
        groundY = eased_[1];
        z = eased_[2];
    }

    const float pitch = kPitchDegrees * 3.14159265f / 180.0f;
    const float yaw = kYawDegrees * 3.14159265f / 180.0f;

    // The roofs, on the feet the camera is framing: the character when one is played, the
    // focus when not, so `--at` inside a house shows the room as walking into it would.
    town_.setRoofsHidden(indoors(feetX, feetZ));

    camera_.target[0] = x;
    camera_.target[1] = groundY + kFocusHeight;
    camera_.target[2] = z;

    // Walk.cs's own back vector: sin(yaw)cos(pitch), -sin(pitch), cos(yaw)cos(pitch).
    const float back[3] = {std::sin(yaw) * std::cos(pitch), -std::sin(pitch),
                           std::cos(yaw) * std::cos(pitch)};
    for (int i = 0; i < 3; ++i) camera_.position[i] = camera_.target[i] + back[i] * kDistance;

    // The ARPG framing: the played character stands above the middle of the frame, with more
    // ground ahead of him to the bottom of the screen, where the HUD's bar and orbs sit. The
    // whole camera slides down its own screen-up, so the angle and the lens are untouched and
    // only the picture moves. The slide is a fraction of the frame's height at the focus.
    if (play_.isOpen()) {
        const float lift = kLiftFrame * 2.0f * std::tan(kFovDegrees * 0.5f * 3.14159265f / 180.0f) *
                           kDistance;
        // Screen-up is (right x forward), with forward = -back and right = forward x world-up.
        const float fwd[3] = {-back[0], -back[1], -back[2]};
        float right[3] = {-fwd[2], 0.0f, fwd[0]};
        const float rl = std::hypot(right[0], right[2]);
        right[0] /= rl;
        right[2] /= rl;
        const float upward[3] = {right[1] * fwd[2] - right[2] * fwd[1],
                                 right[2] * fwd[0] - right[0] * fwd[2],
                                 right[0] * fwd[1] - right[1] * fwd[0]};
        for (int i = 0; i < 3; ++i) {
            camera_.target[i] -= upward[i] * lift;
            camera_.position[i] -= upward[i] * lift;
        }
    }
}

bool World::characterAt(float* x, float* z) const {
    if (!play_.isOpen()) return false;
    float column = focusColumn_, row = focusRow_;
    play_.focus(&column, &row);
    tileToMetres(column, row, x, z);
    return true;
}

bool World::indoors(float x, float z) const {
    // Column is +x and row is -z. docs/conventions.md.
    const float metresPerTile = ground_.metresPerTile();
    const int column = int(std::floor(x / metresPerTile));
    const int row = int(std::floor(-z / metresPerTile));
    return ground_.floorAt(column, row) == kIndoorFloor;
}

void World::shutdown() {
    play_.shutdown();
    crowd_.shutdown();
    figures_.shutdown();
    lamps_.shutdown();
    sway_.shutdown();
    ornaments_.shutdown();
    boids_.shutdown();
    leaves_.shutdown();
    grass_.shutdown();
    town_.shutdown();
    ground_.shutdown();
}

}  // namespace mu::game
