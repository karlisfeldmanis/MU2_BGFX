// A world raised: the land, what stands on it, and MU's own camera looking at both.
#pragma once

#include <string>

#include "content/ground.h"
#include "content/texture.h"
#include "game/crowd.h"
#include "game/figures.h"
#include "game/play.h"
#include "game/town.h"
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
    void shutdown();

    // `seconds` moves the focus so the camera is not still: sprint 1 ran --still throughout
    // and a shadow that crawls with the camera cannot be seen from a fixed one.
    void update(double seconds, bool still);

    const gfx::Camera& camera() const { return camera_; }
    const content::Ground& ground() const { return ground_; }
    Town& town() { return town_; }
    const Town& town() const { return town_; }
    Crowd& crowd() { return crowd_; }
    const Crowd& crowd() const { return crowd_; }
    const Figures& figures() const { return figures_; }
    Play& played() { return play_; }
    const Play& played() const { return play_; }

    // Where the camera looks, in tiles. Set from --at, else the map's own middle.
    void setFocusTile(float column, float row);

    // Where the played character is drawn right now, in world metres, or false when no world
    // is being played. Asked AFTER the play has advanced, it is where the figure stands this
    // frame; the camera's own target is where it stood when the camera was placed.
    bool characterAt(float* x, float* z) const;

private:
    // A tile to the centre of that tile in world metres, through the map's own scale.
    void tileToMetres(float column, float row, float* x, float* z) const;

    content::Ground ground_;
    Town town_;
    Figures figures_;
    Crowd crowd_;
    Play play_;
    gfx::Camera camera_;
    float focusColumn_ = 0.0f;
    float focusRow_ = 0.0f;
    bool focusSet_ = false;
};

}  // namespace mu::game
