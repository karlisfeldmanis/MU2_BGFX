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

#include <functional>

#include "app/context.h"

namespace mu::app {

class Preloader {
public:
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
    static bool run(Context& ctx, const std::function<bool()>& load, bool* quitEarly,
                    bool keepAmbient = false);
    // The ambient's level, 0 to 1, as the menu's volume changes; a muted run stays silent.
    static void ambientVolume(float level);
};

}  // namespace mu::app
