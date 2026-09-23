// A world raised: the land, what stands on it, and MU's own camera looking at both.
#pragma once

#include <string>

#include "content/ground.h"
#include "content/texture.h"
#include "game/crowd.h"
#include "game/figures.h"
#include "game/world/boids.h"
#include "game/world/grass.h"
#include "game/world/lamps.h"
#include "game/world/leaves.h"
#include "game/world/ornaments.h"
#include "game/play.h"
#include "game/world/sway.h"
#include "game/world/town.h"
#include "gfx/renderer.h"

namespace mu::game {

class World {
public:
    // `crowd` is how many monsters stand in the town, from --crowd; -1 is every one the
    // map's spawn table names.
    // `crowd` is how many monsters stand in the town, from --crowd; -1 is every one the
    // map's spawn table names, and a negative `crowd` with `figures` false raises none at
    // all -- the baseline the crowd is priced against.
    bool open(const std::string& assetDir, const std::string& name, content::Textures& textures,
              int crowd = 30, bool figures = true);
    // Raises the realm behind the window: the sim, a figure for every body in it, and the
    // pointer. The crowd is what stands in a world nobody is playing; this is what stands in
    // one somebody is. Only one of the two is ever open.
    bool play(const std::string& assetDir, const std::string& name, uint64_t seed, int kin,
              int level, const std::string& weapon = "", const std::string& shield = "");
    // The birds and the leaves, raised once the play's showing and sound are open. Separate
    // from play() on purpose; the reason is on the definition.
    void raiseAirs(const std::string& assetDir, const std::string& name);
    void shutdown();

    // `seconds` moves the focus so the camera is not still: sprint 1 ran --still throughout
    // and a shadow that crawls with the camera cannot be seen from a fixed one.
    void update(double seconds, bool still);

    const gfx::Camera& camera() const { return camera_; }
    // The point the played camera follows -- the character, eased. Not camera().target, which
    // a played camera slides off him so that he stands above the middle of the frame.
    const float* followed() const { return eased_; }
    const content::Ground& ground() const { return ground_; }
    Town& town() { return town_; }
    const Town& town() const { return town_; }
    Crowd& crowd() { return crowd_; }
    const Crowd& crowd() const { return crowd_; }
    const Figures& figures() const { return figures_; }
    Grass& grass() { return grass_; }
    const Grass& grass() const { return grass_; }
    Lamps& lamps() { return lamps_; }
    const Lamps& lamps() const { return lamps_; }
    Sway& sway() { return sway_; }
    const Sway& sway() const { return sway_; }
    Ornaments& ornaments() { return ornaments_; }
    Boids& boids() { return boids_; }
    const Boids& boids() const { return boids_; }
    Leaves& leaves() { return leaves_; }
    const Leaves& leaves() const { return leaves_; }
    Play& played() { return play_; }
    const Play& played() const { return play_; }

    // Where the camera looks, in tiles. Set from --at, else the map's own middle.
    void setFocusTile(float column, float row);
    // Where the played character is drawn right now, in world metres, or false when no world
    // is being played. Asked AFTER the play has advanced, it is where the figure stands this
    // frame; the camera's own target is where it stood when the camera was placed.
    bool characterAt(float* x, float* z) const;

    // Whether a point is inside a building, by MU's own test, which is blunter than anybody
    // expects: the tile texture under it is Lorencia's interior floor. No volume and no idea
    // which house -- standing on that floor hides every roof in the town at once.
    bool indoors(float x, float z) const;

private:
    // A tile to the centre of that tile in world metres, through the map's own scale.
    void tileToMetres(float column, float row, float* x, float* z) const;

    content::Ground ground_;
    Town town_;
    Grass grass_;
    Lamps lamps_;
    Sway sway_;
    Ornaments ornaments_;
    Boids boids_;
    Leaves leaves_;
    // Held from open() so play() can load the boid's mesh and the leaf's sheet. Those two
    // pools follow the PLAYER -- they are spawned around him and exist nowhere else, which is
    // MU's own arrangement -- so they are raised when somebody is played and not when the
    // world is merely standing, and by then the textures are long out of scope.
    content::Textures* textures_ = nullptr;
    Figures figures_;
    Crowd crowd_;
    Play play_;
    gfx::Camera camera_;
    // The played camera's eased target, in metres, and the spring's velocity on each axis.
    float eased_[3] = {0.0f, 0.0f, 0.0f};
    float easing_[3] = {0.0f, 0.0f, 0.0f};
    bool easedSet_ = false;
    double lastSeconds_ = -1.0;
    float focusColumn_ = 0.0f;
    float focusRow_ = 0.0f;
    bool focusSet_ = false;
};

}  // namespace mu::game
