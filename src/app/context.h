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

private:
    std::string overlayPath(int which) const;
    const Paths* paths_ = nullptr;
    gfx::Lighting* lighting_ = nullptr;
    int which_ = 0;
    int64_t overlayStamp_ = 0;
    bool announce_ = false;
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
};

// What the day gives an unlit puff of smoke: the ambient and the sun on a flat surface, over
// what the default sheet's noon gives it. The transparent pass lights nothing, so the lamps'
// smoke is lit by this. Shared by the world and the viewer's stage.
float daylightOf(const gfx::Lighting& lighting);

}  // namespace mu::app
