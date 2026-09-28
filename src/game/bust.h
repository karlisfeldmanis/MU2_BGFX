// The create window's bust: the class's head and shoulders, photographed into the window.
//
// MuMain's CharMakeWin, by way of MU2's Lobby.Bust. `Data/Logo/NewFace0N.bmd` -- one per class,
// the file number the class plus one in MU's own count, so 01 is the Dark Wizard, 02 the Dark
// Knight and 03 the Fairy Elf (ZzzOpenData.cpp:5102). Not player models and not portraits: a
// hundred bones and two actions each, cooked here as standalone figures (tools/cook_one.py).
//
// Every number is MU's (CharMakeWin.cpp:97, :481):
//   camera   the model's position plus (10, -500, 48) MU units, level, a ten-degree VERTICAL
//            field -- which sees 87 centimetres at five metres, and is why a bust scaled six
//            times over is cropped at the shoulders rather than standing in the window
//   wizard   angle (0, 0, -40), scale 5.9
//   knight   angle (0, 0, -12), scale 6.05
//   elf      angle (8, 0, 5), scale 9.1, nudged 4.8 aside -- once, where MU adds the nudge on
//            every frame it draws and walks the elf out of shot
//   actions  1, the greeting, on choosing the class; then 0, the idle, when it ends (:462)
//
// Photographed on its own stage (gfx/stage_pass.cpp) through its own view, into a target the
// window draws as a picture: the scene behind the window is dimmed, and the bust is not.
#pragma once

#include <bgfx/bgfx.h>

#include <vector>

#include "game/crowd.h"
#include "game/figures.h"
#include "gfx/interface.h"
#include "gfx/renderer.h"
#include "sim/rules.h"

namespace mu::game {

class Bust {
public:
    void open(const Figures* figures) { figures_ = figures; }
    void shutdown();

    // The class to show; a change stands the new bust and plays its greeting. Called every
    // frame the window is up.
    void show(sim::Kin kin);
    void hide() { shown_ = false; }

    // Poses it and photographs it into a target `width` by `height` pixels. Before the frame's
    // scene draw, which is what uploads the palette the pose goes into.
    void render(gfx::Renderer& renderer, float seconds, int width, int height);

    // The picture, invalid until the first render.
    const gfx::Art& picture() const { return picture_; }
    bool shown() const { return shown_ && figure_.body() != nullptr; }

private:
    void resize(int width, int height);

    const Figures* figures_ = nullptr;
    Figure figure_;
    bool shown_ = false;
    bool standing_ = false;
    sim::Kin kin_ = sim::Kin::DarkKnight;
    int idle_ = -1, greeting_ = -1;
    bool greeting_now_ = false;
    float greetingLeft_ = 0.0f;
    std::vector<float> scratch_;
    std::vector<gfx::Drawable> drawables_;
    bgfx::FrameBufferHandle target_ = BGFX_INVALID_HANDLE;
    gfx::Art picture_;
    int width_ = 0, height_ = 0;
};

}  // namespace mu::game
