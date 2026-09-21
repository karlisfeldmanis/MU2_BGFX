// The game's windows, composed: the HUD, the character window, and the rules for how they
// open, close, sit beside each other and take the pointer from the world.
//
// MU2's `client/core/Desk.cs`, smaller. What it keeps is the one decision that has to live
// above every window: a click is the interface's when it lands on any window, and the world's
// only when it lands on none. A HUD that let a click through walked the character every time
// he opened his bag.
#pragma once

#include <string>
#include <vector>

#include "content/texture.h"
#include "game/bag.h"
#include "game/card.h"
#include "game/cursor.h"
#include "game/hud.h"
#include "game/item_models.h"
#include "game/items_stage.h"
#include "game/shelf.h"
#include "game/vitals.h"
#include "game/panel.h"
#include "gfx/interface.h"
#include "gfx/window.h"
#include "game/play.h"

namespace mu::game {


class Desk {
public:
    bool open(const std::string& shaderDir, const std::string& assetDir,
              content::Textures* textures);
    void shutdown();
    bool ready() const { return interface_.ready(); }

    // A frame of input and redrawing, before the world is given the pointer. `pointerX` and
    // `pointerY` are the backbuffer pixels the pointer is really at, even on a scripted run --
    // the same ones the world's own raycast is given right after, so a window, the cursor drawn
    // over it and what lies under it on the ground never disagree about where the pointer is.
    void update(float seconds, const gfx::Window& window, Play& play, float pointerX,
                float pointerY);
    // A scripted pointer for the next update, replacing the mouse's: a press, a release, or
    // just a place. The run's --ui-click goes through here and then through exactly the
    // windows' own code.
    void script(float x, float y, bool press, bool release, bool right = false);
    // A scripted potion key for the next update, 0 to 3.
    void scriptKey(int key) { scriptedKey_ = key; }
    // The camera this frame, for the names over what lies on the ground.
    void setView(const float* viewProj) {
        for (int i = 0; i < 16; ++i) viewProj_[i] = viewProj[i];
    }
    // The monster's health bar, once the frame has placed every body and the camera: run after
    // Play::update and World::update, so the bar is hung on the crown drawn THIS frame. Done in
    // update() it trailed a walking spider by a frame, which a bar over its head shows.
    void overhead(float seconds, const Play& play, const float* viewProj, int width, int height);

    // Whether the pointer this frame belongs to a window rather than to the ground.
    bool takesPointer() const { return takesPointer_; }

    // The item pictures, taken after update() has said what stands on each stage and before
    // the renderer's frame is submitted. Sprint 7 step 5: the bag and the shelf draw the real
    // models, as MU2's Panel.Stage does and MU's RenderObjectScreen did.
    void photograph(gfx::Renderer& renderer, double seconds);
    // The store the pictures are read from. Not owned: the same one draws what lies on the
    // ground, so a sword in the bag and the same sword on the grass are one mesh, and the
    // drops do not go away with the windows.
    void useModels(ItemModels* models) {
        models_ = models;
        bagStagePicture_.open(models, gfx::ViewStageBag);
        shelfStagePicture_.open(models, gfx::ViewStageShelf);
    }

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
    Bag bag_;
    Shelf shelf_;
    Cursor cursor_;
    Vitals vitals_;
    ItemModels* models_ = nullptr;
    ItemStage bagStagePicture_, shelfStagePicture_;
    std::string shaderDir_, assetDir_;
    content::Textures* textures_ = nullptr;
    Stage* bagStage_ = nullptr;
    // The names over the drops, on MU's own black plate. Rebuilt when one moves on screen.
    gfx::Canvas ground_;
    std::vector<Play::OnScreen> onScreen_, drawnOnScreen_;
    float viewProj_[16] = {};
    uint64_t groundRebuilds_ = 0;
    void labelGround(const Play& play, int width, int height);
    Stage* shelfStage_ = nullptr;
    bool trading_ = false;
    // The four potion keys' bindings, as MU's item row. The interface's until sprint 9's save
    // writes them (mu.db's character_hotkeys is where MU2 kept them).
    int32_t quick_[4] = {-1, -1, -1, -1};
    int scriptedKey_ = -1;
    void quickKeys(const gfx::Window& window, Play& play);
    bool bagForShop_ = false;  // the bag was opened by the counter, and goes when it does
    bool inventoryOpen_ = false;
    bool characterOpen_ = false;
    bool takesPointer_ = false;
    bool scripted_ = false;
    Pointer script_;
};

}  // namespace mu::game
