// The game's windows, composed: the HUD, the character window, and the rules for how they
// open, close, sit beside each other and take the pointer from the world.
//
// MU2's `client/core/Desk.cs`, smaller. What it keeps is the one decision that has to live
// above every window: a click is the interface's when it lands on any window, and the world's
// only when it lands on none. A HUD that let a click through walked the character every time
// he opened his bag.
#pragma once

#include <string>

#include "content/texture.h"
#include "game/card.h"
#include "game/hud.h"
#include "game/panel.h"
#include "gfx/interface.h"
#include "gfx/window.h"

namespace mu::game {

class Play;

class Desk {
public:
    bool open(const std::string& shaderDir, const std::string& assetDir,
              content::Textures* textures);
    void shutdown();
    bool ready() const { return interface_.ready(); }

    // A frame of input and redrawing, before the world is given the pointer. `pointerX` and
    // `pointerY` are the backbuffer pixels the pointer is really at, even on a scripted run.
    void update(float seconds, const gfx::Window& window, Play& play);
    // A scripted pointer for the next update, replacing the mouse's: a press, a release, or
    // just a place. The run's --ui-click goes through here and then through exactly the
    // windows' own code.
    void script(float x, float y, bool press, bool release);
    // Whether the pointer this frame belongs to a window rather than to the ground.
    bool takesPointer() const { return takesPointer_; }

    void submit(bgfx::ViewId view, int width, int height);
    // One line for the log: what the windows cost this second.
    std::string line() const;

    bool inventoryOpen() const { return inventoryOpen_; }
    bool characterOpen() const { return characterOpen_; }
    void setInventoryOpen(bool open) { inventoryOpen_ = open; }
    void setCharacterOpen(bool open) { characterOpen_ = open; }

private:
    gfx::Interface interface_;
    panel::Arts arts_;
    Hud hud_;
    Card card_;
    bool inventoryOpen_ = false;
    bool characterOpen_ = false;
    bool takesPointer_ = false;
    bool scripted_ = false;
    Pointer script_;
};

}  // namespace mu::game
