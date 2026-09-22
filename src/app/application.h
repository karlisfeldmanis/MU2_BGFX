// The application: what every run of this binary does whatever it was asked for.
//
// It raises the log, the window, the textures and the renderer; it chooses a mode and hands it
// a Context; it runs the loop and does the part of every frame that is nobody's mode's -- the
// resize, the lighting sheet's reload, the palette reset, the effects pool, the screenshot,
// bgfx::frame(), the clock and the statistics; and it tears the whole lot down in the one
// order that is safe.
//
// The division of labour, stated once so it is not re-argued per frame: **the Application owns
// the frame and the mode owns the picture.** A mode never calls bgfx::frame(), never reads the
// clock for its own delta and never takes a screenshot. It is handed the frame it is drawing
// and the seconds the last one took.
//
// The headless run is deliberately NOT a mode. It returns before GLFW, the device and the
// textures exist at all -- no window, no renderer, nothing graphical -- and that is exactly
// what makes it a measurement of the tick rather than of a frame with the drawing switched
// off. A Mode, by contrast, is a thing the frame loop runs. See run().
#pragma once

#include "app/context.h"
#include "app/mode.h"

namespace mu::app {

class Application {
public:
    // argc/argv straight from main(). The return is the process's: 0 for a clean run, 1 for a
    // failure or a run that logged errors, 2 for a frame budget overdrawn.
    int run(int argc, char** argv);

private:
    // The window, the device and the textures. False having already said why in the log.
    bool boot();
    // The effects probe (--effects): a named cooked sheet, strung along the line from the
    // camera to what it looks at, so the transparent pass has something to sort and batch.
    void openProbe();
    void submitProbe(Mode& mode);
    // One pass round the loop, and what it costs. Returns false when the run is over.
    void teardown();

    core::Args args_;
    Paths paths_;
    gfx::Window window_;
    content::Textures textures_;
    gfx::Renderer renderer_;
    gfx::Lighting lighting_;
    TimeOfDay time_;
    gfx::Overlay overlay_;
    gfx::Readout readout_;
    gfx::Overlay curtain_;

    int probeSprites_ = 0;
    bgfx::TextureHandle probeSheet_ = BGFX_INVALID_HANDLE;
};

}  // namespace mu::app
