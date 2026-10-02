// What every mode is handed, and none of it owned by one.
//
// The Application owns the window, the renderer, the textures and the rest, and it outlives
// every mode that runs against them. So this carries references rather than pointers: not one
// of them is ever absent, and a pointer would invite a null test that can never fire and hide
// the one thing worth knowing -- that a mode which reaches for the renderer before the
// Application has raised one is a bug in the Application, not a case to handle.
//
// This replaces the twelve locals `main()` used to carry between its bootstrap and its loop.
// They were not ordered by anything, and a second reader could not tell which of them the
// loop still needed at the bottom; naming the set is most of what the carve bought.
#pragma once

#include <string>

#include "content/texture.h"
#include "core/args.h"
#include "gfx/lighting.h"
#include "gfx/overlay.h"
#include "gfx/readout.h"
#include "gfx/renderer.h"
#include "gfx/window.h"

namespace mu::app {

// Where the build put things, and where this run was told to put its own. The first four are
// the compile definitions in CMakeLists.txt; the last two are what the arguments made of them.
struct Paths {
    std::string assets;   // MU2_ASSET_DIR -- the cook's output
    std::string shaders;  // MU2_SHADER_DIR -- the compiled metal binaries
    std::string sheets;   // MU2_SHEET_DIR -- lighting.json and time/
    std::string root;     // MU2_ROOT_DIR
    std::string shots;    // --shot-path, or root/shots; made rather than assumed
    std::string log;      // --log, or root/mu2.log
    std::string sheet;    // --sheet, or sheets/lighting.json

    std::string under(const std::string& dir, const std::string& name) const {
        return dir + "/" + name;
    }
};

// The times of day the viewer's T key walks and the studio's sweep steps.
//
// The sheet alone is noon; dusk and night are sheets/time/<name>.json laid over it. Rebuilt
// from the sheet on every change rather than patched, so going from night back to noon cannot
// leave a night value behind -- which it did, when this was three lambdas and two locals in
// main() and the rebuild was one of them.
class TimeOfDay {
public:
    // `announce` logs the time on every change, which is the viewer's and the studio's want
    // and noise in a play run.
    void open(const Paths* paths, gfx::Lighting* lighting, int which, bool announce);
    // Noon, dusk or night by index, and the sheet re-read under it.
    void set(int which);
    void step() { set((which_ + 1) % 3); }
    int which() const { return which_; }
    const char* name() const;
    // Four times a second from the loop, counted in milliseconds rather than frames: the point
    // is to tune with the window open, and at 470 fps a count of frames stat'ed the file a
    // hundred times a second. Re-applies when either the sheet or the overlay has moved.
    void reloadIfChanged();
    // A sheet of the scene's own, laid over the base and the time of day: the character screen's
    // clearer air (sheets/lobby.json). Empty takes it off. Watched and re-applied as the others are.
    void setScene(const std::string& path);
    // The scene's wet sheet, laid over the scene for rain: sheets/worlds/<world>_rain.json.
    // Empty takes it off. rain() then blends the frame's light between the two by the weather's
    // share, 0 dry to 1 raining (game/world/weather.h) -- the rain darkens the world as it comes
    // in and lifts as it goes, rather than switching. `flash` is the lightning over it, 0 to 1
    // (Weather::flash): the sky and the air go blue-white for the moment of a strike.
    void setWet(const std::string& path);
    void rain(float share, float flash = 0.0f);

private:
    std::string overlayPath(int which) const;
    void wetten();      // the wet sheet blended in by the share
    void lightning();   // the flash over it
    const Paths* paths_ = nullptr;
    gfx::Lighting* lighting_ = nullptr;
    int which_ = 0;
    int64_t overlayStamp_ = 0;
    std::string scene_;
    int64_t sceneStamp_ = 0;
    std::string wetPath_;
    int64_t wetStamp_ = 0;
    gfx::Lighting dry_, wet_;  // the light as set() built it, and that with the wet sheet over it
    float share_ = 0.0f;
    float flash_ = 0.0f;
    bool announce_ = false;
};

// Go Back!: the spot in the field he left by magic -- a Town Portal Scroll, or a Tab trip --
// and the five minutes he has to sell, buy and come back to it. Ours: 0.75 has nothing like it.
//
// The Application's and not a mode's, because a Tab trip shuts this world's mode and raises the
// next one, and the way back has to come along. On the way to the character screen or out of
// the game it is written into the save (game::Saved goBack*) and given back when he is played
// again, its clock stood still while the game was shut.
struct GoBack {
    static constexpr double kSeconds = 300.0;
    static constexpr double kClosedSeconds = 3.0;  // "Go Back! has closed", then nothing
    std::string world;  // where it goes, empty for none
    int column = -1, row = -1;
    float facing = 0.0f;
    double left = 0.0;    // seconds of play left; 0 with `world` set is the closed line
    double closed = 0.0;  // how long the closed line has shown
    // The next world was come into by magic, and is owed the warp's sound and ring when he is
    // in it: a Tab trip, a Town Portal to another map, or Go Back! itself.
    bool landing = false;

    bool open() const { return !world.empty() && left > 0.0; }
    void arm(const std::string& to, int c, int r, float f) {
        world = to;
        column = c;
        row = r;
        facing = f;
        left = kSeconds;
        closed = 0.0;
    }
    void clear() {
        world.clear();
        left = closed = 0.0;
    }
};

struct Context {
    core::Args& args;
    const Paths& paths;
    gfx::Window& window;
    gfx::Renderer& renderer;
    content::Textures& textures;
    gfx::Lighting& lighting;
    TimeOfDay& time;
    // The viewer's list, the tile over the character's head and the frame rate. One overlay
    // for the three: an overlay nobody draws still costs a program and a texture, so it is
    // built by whichever mode draws something and left unbuilt otherwise.
    gfx::Overlay& overlay;
    gfx::Readout& readout;
    // The spinner's black and the entrance's fade up out of it. Its own overlay, because it
    // is drawn over the list rather than among it.
    gfx::Overlay& curtain;
    GoBack& goBack;
};

// What the day gives an unlit puff of smoke: the ambient and the sun on a flat surface, over
// what the default sheet's noon gives it. The transparent pass lights nothing, so the lamps'
// smoke is lit by this. Shared by the world and the viewer's stage.
float daylightOf(const gfx::Lighting& lighting);

}  // namespace mu::app
