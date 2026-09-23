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
#include "game/ui/arrival.h"
#include "game/ui/bag.h"
#include "game/ui/card.h"
#include "game/ui/cursor.h"
#include "game/ui/hud.h"
#include "game/item_models.h"
#include "game/ui/items_stage.h"
#include "game/ui/shelf.h"
#include "game/ui/vitals.h"
#include "game/ui/panel.h"
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
    // A skill key pressed by a script: 0 is Q. `--press q` in a headless run, so the cast path is
    // reachable without a window.
    void scriptSkill(int key) { scriptedSkill_ = key; }
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
        quickStagePicture_.open(models, gfx::ViewStageQuick);
        tipStagePicture_.open(models, gfx::ViewStageTip);
    }

    void submit(bgfx::ViewId view, int width, int height);
    // One line for the log: what the windows cost this second.
    std::string line() const;

    bool inventoryOpen() const { return inventoryOpen_; }
    bool characterOpen() const { return characterOpen_; }
    void setInventoryOpen(bool open) { inventoryOpen_ = open; }
    // The four potion keys' bindings, as item rows, -1 for none: what the save keeps.
    int32_t quick(int key) const { return key >= 0 && key < Hud::kQuickKeys ? quick_[key] : -1; }
    void setQuick(int key, int32_t item) {
        if (key >= 0 && key < Hud::kQuickKeys) quick_[key] = item;
    }
    void setCharacterOpen(bool open) { characterOpen_ = open; }
    // The four skill keys, by MU's skill number, 0 for empty: what the save keeps. Restoring
    // marks the arrangement as the player's, so the first-free-key convenience does not put
    // back on the next frame what he took off before he quit -- see `autoBound_`.
    int32_t bound(int key) const { return key >= 0 && key < Hud::kSkillKeys ? bound_[key] : 0; }
    void restoreBar(const int32_t* numbers, int count) {
        for (int key = 0; key < Hud::kSkillKeys && key < count; ++key) bound_[key] = numbers[key];
        barRestored_ = true;
    }
    // The world's name comes up over the scene after `delay` seconds: see game/arrival.h.
    void arrive(const std::string& world, float delay) { arrival_.announce(world, delay); }

private:
    gfx::Interface interface_;
    panel::Arts arts_;
    Hud hud_;
    Card card_;
    Bag bag_;
    Shelf shelf_;
    Cursor cursor_;
    Vitals vitals_;
    Arrival arrival_;
    ItemModels* models_ = nullptr;
    ItemStage bagStagePicture_, shelfStagePicture_, quickStagePicture_, tipStagePicture_;
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
    // The four potion keys' bindings, as MU's item row. The interface's; game/save.cpp writes
    // them (mu.db's character_hotkeys is where MU2 kept them).
    int32_t quick_[Hud::kQuickKeys] = {-1, -1, -1, -1, -1};
    int scriptedKey_ = -1;
    void quickKeys(const gfx::Window& window, Play& play);
    // What is on Q W E R, by MU's skill number. A newly learned skill takes the first free key
    // ONCE -- `autoBound_` is what stops it coming back the moment the player takes it off -- and
    // after that the bar is his: right-click a box to open the list, drag a row onto a key, drag
    // a key's skill onto another to swap the two, drag it back into the list to clear it
    // (docs/skills-dk.md §3.4, and the user's own gesture, 2026-09-23).
    // Not saved, which is faithful -- there is no SaveHotKey anywhere in MuMain.
    int32_t bound_[Hud::kSkillKeys] = {0, 0, 0, 0};
    uint32_t autoBound_ = 0;  // skills that have had their one free key
    bool barRestored_ = false;
    // The list above the plate: latched open by a click on the gold box, and open anyway while
    // the pointer rests on that box or on the list itself. The cells are rebuilt every frame off
    // what the realm says he has learned.
    bool fanLatched_ = false;
    std::vector<Hud::FanCell> fan_;
    int32_t carrying_ = 0;    // the skill the pointer is holding, 0 for none
    int carryFrom_ = -1;      // the key it was lifted off, or -1 out of the list
    int scriptedSkill_ = -1;
    void skillKeys(const gfx::Window& window, Play& play, const Pointer& pointer);
    tip::Sheet skillSheet(const sim::SkillRow& row, const sim::Realm& realm, bool armed) const;
    bool bagForShop_ = false;  // the bag was opened by the counter, and goes when it does
    bool inventoryOpen_ = false;
    bool characterOpen_ = false;
    bool takesPointer_ = false;
    bool scripted_ = false;
    Pointer script_;
};

}  // namespace mu::game
