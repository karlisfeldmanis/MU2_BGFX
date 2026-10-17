// Everything a world needs, loaded on a worker thread while this one does nothing but draw a
// spinner and present it.
//
// The spinner therefore cannot hitch on a load, however long one takes: this thread never
// waits on a file, a decode or a build, only on the display's own refresh.
//
// What makes it legal is bgfx's resource lock (CMakeLists.txt, BGFX_CONFIG_MULTITHREADED): the
// loaders create textures, buffers, shaders and uniforms and call nothing else in bgfx, and
// those calls are locked against this thread's bgfx::frame(). The uploads themselves still
// happen here, inside frame(), a frame's worth of creations at a time. Nothing else is shared:
// this thread touches neither the world nor the textures until the worker has been joined.
#pragma once

#include <bgfx/bgfx.h>

#include <functional>
#include <string>
#include <vector>

#include "app/context.h"

namespace mu::app {

// How the spinner dresses. The game's (the default) is the ring, its number, MU's letter at
// the foot and the loading music -- and no word for what is loading (the user, 2026-10-06).
// The studio's (`studio()`, the user, 2026-10-12: "develop also preloader for this app") is
// a work tool's: its name over the ring, the stage being loaded under it in place of the
// letter, and no music.
struct PreloaderLook {
    const char* title = nullptr;   // over the ring; none for the game
    bool stages = false;           // the stage's word under the ring, in place of the letter
    bool music = true;             // the loading ambient
};

// The loading screen's picture -- the ring and its number, the MU Classic logo, the letter and the
// build, on black -- laid over a frame: the Preloader's own, and the character screen's while
// the first run's graphics are set up under it (app/tuner.h; the user, 2026-10-17: "use existing
// preloader but with text under").
class LoadingScreen {
public:
    // Its dot and, `withLogo`, the logo: the game's look, not the studio's.
    void open(Context& ctx, bool withLogo);
    // `done` the share done, 0 to 1, or negative for core::Loading's own, eased; `under` a line
    // under the ring, or null. The HUD view cleared to black, so nothing drawn before shows.
    void draw(Context& ctx, const PreloaderLook& look, float alpha, double seconds, float done,
              const char* under);
    void close();
    bool isOpen() const { return open_; }

private:
    bool open_ = false;
    bgfx::TextureHandle dot_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle logo_ = BGFX_INVALID_HANDLE;
    float logoAspect_ = 1.0f;
    float shown_ = 0.0f;
    double shownAt_ = 0.0;
    int letterWidth_ = -1;
    std::vector<std::string> letterLines_;
};

class Preloader {
public:
    using Look = PreloaderLook;
    static Look studio() { return {"MU Studio", true, false}; }

    // `load` runs on the worker and returns whether what it loaded came up. This call returns
    // once it has been joined, so everything `load` touched is this thread's again afterwards.
    //
    // `quitEarly` is set when the window was closed while the spinner was up. A window closed
    // meanwhile still waits for the worker -- a load cannot be abandoned half-made -- and then
    // leaves.
    //
    // The return is `load`'s own answer.
    //
    // The loading ambient plays under the spinner and fades out when it goes, unless
    // `keepAmbient`: the character screen's load, which leaves it playing on the screen.
    //
    // `settle`, when given, runs on THIS thread once the load has come up, a spinner frame at a
    // time with the seconds since the last, and the spinner stays until it says true: the
    // server's warm-up before the world is shown (Play::settle).
    static bool run(Context& ctx, const std::function<bool()>& load, bool* quitEarly,
                    bool keepAmbient = false,
                    const std::function<bool(double seconds)>& settle = {},
                    const PreloaderLook& look = PreloaderLook());
    // The ambient's level, 0 to 1, as the menu's volume changes; a muted run stays silent.
    static void ambientVolume(float level);
    // One sound, once, on the ambient's engine, which outlives the mode that asked: the
    // character screen's Enter World runs on under the loading screen. Silent until the
    // ambient has opened, and at its volume.
    static void cue(const std::string& path);
};

}  // namespace mu::app
