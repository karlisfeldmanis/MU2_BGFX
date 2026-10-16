// The game's windows, composed: the HUD, the character window, and the rules for how they
// open, close, sit beside each other and take the pointer from the world.
//
// MU2's `client/core/Desk.cs`, smaller. What it keeps is the one decision that has to live
// above every window: a click is the interface's when it lands on any window, and the world's
// only when it lands on none. A HUD that let a click through walked the character every time
// he opened his bag.
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "content/texture.h"
#include "game/ui/amount.h"
#include "game/ui/arrival.h"
#include "game/ui/bag.h"
#include "game/ui/beacon.h"
#include "game/ui/card.h"
#include "game/ui/chat.h"
#include "game/ui/broker.h"
#include "game/ui/chest.h"
#include "game/ui/deal.h"
#include "game/ui/mixer.h"
#include "game/ui/endurance.h"
#include "game/ui/go_back.h"
#include "game/ui/guild.h"
#include "game/ui/herald.h"
#include "game/ui/cursor.h"
#include "game/ui/hud.h"
#include "game/item_models.h"
#include "game/ui/items_stage.h"
#include "game/ui/menu.h"
#include "game/ui/minimap.h"
#include "game/ui/quest_dialog.h"
#include "game/ui/report.h"
#include "game/ui/square_result.h"
#include "game/ui/tracker.h"
#include "game/ui/whats_new.h"
#include "game/ui/travel.h"
#include "game/ui/shelf.h"
#include "game/ui/specimen.h"
#include "game/ui/tally.h"
#include "game/ui/speech.h"
#include "game/ui/boss_bar.h"
#include "game/ui/vitals.h"
#include "game/ui/panel.h"
#include "game/ui/party_bar.h"
#include "game/ui/player_menu.h"
#include "game/ui/inspect.h"
#include "game/ui/target_frame.h"
#include "game/portrait.h"
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
    // The director's hand, whole and every frame, so not logged (app/modes/director.h).
    void scriptHand(const Pointer& hand) {
        scripted_ = true;
        script_ = hand;
    }
    // Typing for the next update, as a script gives it: digits, or "enter" or "escape".
    void scriptType(const std::string& text) {
        if (text == "enter") scriptEnter_ = true;
        else if (text == "escape") scriptEscape_ = true;
        else if (text == "tab") scriptTab_ = true;  // M, the travel list
        else if (text == "map") scriptMap_ = true;  // Tab held, the whole map: latched
        else if (text == "journal") scriptJournal_ = true;  // L, the quest journal
        else if (text == "guild") scriptGuild_ = true;      // G, the guild window
        else if (text == "bag") scriptBag_ = true;          // I, the bag
        else scriptTyped_ += text;
    }
    // Whether a box has the keyboard, which the window is told so Escape cancels the box
    // rather than quitting the game.
    bool typing() const {
        return amount_.up() || report_.up() || whatsNew_.up() || chat_.typing() || broker_.typing() || guild_.typing() ||
               guildMake_.typing();
    }
    // A scripted potion key for the next update, 0 to 3.
    void scriptKey(int key) { scriptedKey_ = key; }
    // A skill key pressed by a script: 0 is Q. `--press q` in a headless run, so the cast path is
    // reachable without a window.
    void scriptSkill(int key) { scriptedSkill_ = key; }
    // And a skill key held down this frame (--ui-hold), as a charge asks after it.
    void scriptHold(int key) { scriptedHold_ = key; }
    // The camera this frame, for the names over what lies on the ground.
    void setView(const float* viewProj) {
        for (int i = 0; i < 16; ++i) viewProj_[i] = viewProj[i];
    }
    // And its view matrix alone, whose axes turn the minimap.
    void setCamera(const float* view) { minimap_.setView(view); }
    // And the land it charts, whose floor says where the water is.
    void setGround(const content::Ground* ground) { minimap_.setGround(ground); }
    // The monster's health bar, once the frame has placed every body and the camera: run after
    // Play::update and World::update, so the bar is hung on the crown drawn THIS frame. Done in
    // update() it trailed a walking spider by a frame, which a bar over its head shows.
    void overhead(float seconds, const Play& play, const float* viewProj, int width, int height);

    // Whether the pointer this frame belongs to a window rather than to the ground.
    bool takesPointer() const { return takesPointer_; }
    // Whether a window, the HUD or a box covers this pixel, as the windows stand now.
    bool overWindows(float x, float y) const;
    // The drop whose name plate is under this pixel, or 0. The last drawn is on top and wins.
    uint32_t labelUnder(float x, float y) const;

    // The item pictures, taken after update() has said what stands on each stage and before
    // the renderer's frame is submitted. Sprint 7 step 5: the bag and the shelf draw the real
    // models, as MU2's Panel.Stage does and MU's RenderObjectScreen did.
    void photograph(gfx::Renderer& renderer, double seconds);
    // The target frame's portrait (game/portrait.h): his head as the world just gathered him.
    // After Play::gather, before the scene's draw.
    void portrait(gfx::Renderer& renderer, Play& play);
    // The store the pictures are read from. Not owned: the same one draws what lies on the
    // ground, so a sword in the bag and the same sword on the grass are one mesh, and the
    // drops do not go away with the windows.
    void useModels(ItemModels* models) {
        models_ = models;
        bagStagePicture_.open(models, gfx::ViewStageBag);
        shelfStagePicture_.open(models, gfx::ViewStageShelf);
        quickStagePicture_.open(models, gfx::ViewStageQuick);
        tipStagePicture_.open(models, gfx::ViewStageTip);
        inspectStagePicture_.open(models, gfx::ViewStageInspect);
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
    // `--windows inspect`: the inspect window on the first other player standing on this map, as
    // soon as there is one (a review run has nobody to right-click him).
    void setInspectFirst(bool on) { scriptInspect_ = on; }
    // The Sanctuary bench (game/ui/specimen.h), from `--windows sanctuary`.
    void setSpecimenOpen(bool open) { specimenOpen_ = open; }
    // The game menu up on a page, for a review: --windows menu, options or graphics.
    void showMenu(Menu::Page page) { menu_.show(page); }
    // What's New up on its welcome: the first entry after an update (PlayMode), --windows whatsnew.
    void showWhatsNew() { whatsNew_.show(); }
    // What's New went down since the last ask: the edition is seen, for options.txt.
    bool takeWhatsNewSeen() {
        const bool seen = whatsNewSeen_;
        whatsNewSeen_ = false;
        return seen;
    }
    bool specimenOpen() const { return specimenOpen_; }
    // The quest the tracker is pinned to, -1 for none: what the save keeps (Tracker::pinned).
    int followedQuest() const { return tracker_.pinned(); }
    void followQuest(int quest) { tracker_.pin(quest); }
    // The four skill keys, by MU's skill number, 0 for empty: what the save keeps. Restoring
    // marks the arrangement as the player's, so the first-free-key convenience does not put
    // back on the next frame what he took off before he quit -- see `autoBound_`.
    int32_t bound(int key) const { return key >= 0 && key < Hud::kSkillBoxes ? bound_[key] : 0; }
    void restoreBar(const int32_t* numbers, int count) {
        bool any = false;
        for (int key = 0; key < Hud::kSkillBoxes && key < count; ++key) {
            bound_[key] = numbers[key];
            any |= numbers[key] != 0;
        }
        // An EMPTY bar is not an arrangement, it is a file that has none: every save written
        // before the keys were saved at all reads as four noughts, and taking that for "he
        // cleared his bar" left those characters with a bar that could never fill itself again.
        // A player who really did empty all four gets the convenience back on his next login,
        // which is the harmless half of the two mistakes.
        barRestored_ = any;
    }
    // The world's name comes up over the scene after `delay` seconds: see game/arrival.h.
    void arrive(const std::string& world, float delay, const std::string& caption = {}) {
        arrival_.announce(world, delay, caption);
    }
    // A banner over the tracker's, from outside a quest (Tracker::announce).
    void announce(const std::string& kicker, const std::string& title, int64_t experience,
                  int64_t zen) {
        tracker_.announce(kicker, title, experience, zen);
    }
    // The map's name, for the menu's foot: where he is standing.
    void setWorld(const std::string& world) { worldName_ = world; }
    // His name off a server, from the save the lobby made him in; the server's own wins there.
    void setHeroName(const std::string& name) { heroName_ = name; }
    // Go Back! (game/ui/go_back.h), told each frame by the mode, which keeps the spot and the
    // clock: `secondsLeft` above 0 is the plate, 0 with `closed` the closed line, `shown` false
    // neither. And whether it was clicked since this was last asked, once.
    void goBack(bool shown, int secondsLeft, bool closed, const std::string& where) {
        goBackShown_ = shown;
        goBackLeft_ = secondsLeft;
        goBackClosed_ = closed;
        goBackWhere_ = where;
    }
    bool takeGoBack() {
        const bool was = goBackAsked_;
        goBackAsked_ = false;
        return was;
    }

    // The game menu (game/ui/menu.h) is up. The world goes on behind it.
    bool menuUp() const { return menu_.up(); }
    // Exit Game was pressed: the run ends after this frame, and shutdown saves.
    bool quitAsked() const { return quitAsked_; }
    // A bug report written and Send pressed (game/ui/report.h): his words and where he stood,
    // handed on once, for the mode to send (launcher/bug_report.h); its answer comes back
    // through reportAnswered.
    struct BugAsk {
        std::string text, world, character, kind;
        int x = 0, y = 0, level = 0;
    };
    bool takeReport(BugAsk* ask) {
        if (!reportReady_) return false;
        reportReady_ = false;
        *ask = reportAsk_;
        return true;
    }
    void reportAnswered(bool sent, const std::string& why) { report_.answered(sent, why); }
    // Switch Character was pressed: the run goes back to the character screen after this frame.
    bool switchAsked() const { return switchAsked_; }
    void allowSwitch(bool on) { menu_.allowSwitch(on); }
    // Whether Escape is the game's: in a played world it opens the menu rather than quitting.
    void holdEscape(bool held) { holdEscape_ = held; }
    // Watched, not played (--raid-watch): none of the hero's own interface is drawn -- the plate,
    // his windows, his wear, his quest -- only what reads the scene (the user, 2026-10-06: 'when
    // we are on specator mode we dont need to show character UI').
    void setWatching(bool watching) { watching_ = watching; }
    // What Options edits: filled by PlayMode from the window and the run, and applied back when
    // `settingsChanged` says so.
    Menu::Settings& settings() { return menu_.settings(); }
    // A line of the game's own in the chat (Chat::notice): the Graphics eased, say.
    void notice(const std::string& text) { chat_.notice(text); }
    bool settingsChanged() {
        const bool changed = settingsChanged_;
        settingsChanged_ = false;
        return changed;
    }

private:
    // What a window does with a command's answer (Play::answers), by its ticket: the asks are
    // answered a tick later, as MU's are a round trip later, and the window hears its yes or no
    // then. Quiet for one heard with others; ZenBox shuts the number box on a yes.
    // Strike rings the quick box `key` on a yes (a potion key drunk).
    // CloseQuest shuts the giver's dialog on a yes; Travel shuts the list and clicks.
    // Click clicks on a yes and refuses on a no; CloseAngel shuts the Archangel's page on a yes.
    // Bound is TookOrRefused for a bound thing, whose no the tally's lane explains (Tally::bound).
    enum class Then : uint8_t {
        TookOrRefused, Refused, Spark, ZenBox, Strike, CloseQuest, Travel, Click, CloseAngel, Quiet,
        Bound
    };
    struct Waiting {
        uint32_t ticket = 0;
        Then then = Then::Quiet;
        int key = -1;
    };
    std::vector<Waiting> waiting_;
    void expect(uint32_t ticket, Then then, int key = -1);
    gfx::Interface interface_;
    panel::Arts arts_;
    Hud hud_;
    Card card_;
    Bag bag_;
    Shelf shelf_;
    Chest chest_;
    Broker broker_;  // the market (game/ui/broker.h), at the vault's place, three columns wide
    // A trade with another player (game/ui/deal.h), which borrows the shelf's stage as the vault
    // does: a counter, the vault and a trade are never open together (the realm cancels it).
    Deal deal_;
    // The Chaos Goblin's machine (game/ui/mixer.h), which borrows the shelf's stage as the vault
    // does: the two are never open together.
    Mixer mixer_;
    bool mixing_ = false;
    bool bagForMachine_ = false;
    // The number box the vault's coin buttons open. Modal: see Desk::update.
    Amount amount_;
    Report report_;
    BugAsk reportAsk_;
    bool reportReady_ = false;
    WhatsNew whatsNew_;
    bool whatsNewSeen_ = false;
    // A quest giver's window and the quest on screen (game/ui/quest_dialog.h, tracker.h).
    QuestDialog questDialog_;
    // The travel list, Tab's (game/ui/travel.h).
    Travel travel_;
    // Go Back!, over the HUD's middle while the way back to the field is open.
    GoBackPlate goBack_;
    // Devil Square's result at its end, and the rows it is told each frame.
    SquareResult squareResult_;
    std::vector<sim::Realm::SquareResult> squareResults_;
    std::vector<SquareResult::Row> squareRows_;
    bool goBackShown_ = false, goBackClosed_ = false, goBackAsked_ = false;
    int goBackLeft_ = 0;
    std::string goBackWhere_;
    // The events coming for his level, one line at the top centre (game/ui/herald.h).
    Herald herald_;
    int journal_ = -1;  // the quest the journal (L) is reading, away from its giver, or -1
    bool scriptJournal_ = false;
    bool scriptBag_ = false;
    bool scriptGuild_ = false;  // a script's G for the next update (--ui-type FRAME:guild)  // a script's L for the next update (--ui-type FRAME:journal)
    // The HUD's quest button, pressed last frame: the journal opens or shuts on this one.
    bool questButton_ = false;
    bool questing_ = false;  // a giver's window was up last frame, so its opening is heard once
    // A giver with a list (Realm::questListed): the folk it was read for, and the quest picked
    // from it, -1 while the list itself is up.
    int listFolk_ = -1, picked_ = -1;
    std::string voiced_;  // the voice clip last started, or empty
    Tracker tracker_;
    // The chat, bottom left over the HUD's wing (game/ui/chat.h).
    Chat chat_;
    // The other members of his party, top left (game/ui/party_bar.h).
    PartyBar partyBar_;
    // The guild's windows (game/ui/guild.h, docs/game/guild.md): G's, and the Guild Master's.
    GuildWindow guild_;
    GuildMake guildMake_;
    int guildColumn() const;
    // The player picked by a click, over the plate (game/ui/target_frame.h), his face, and the
    // one right-click menu on a player wherever he is clicked (game/ui/player_menu.h).
    TargetFrame targetFrame_;
    Portrait portrait_;
    // His party's faces on their capsules, by name, taken one a frame as the target's is; and how
    // many have been taken, which the bar redraws on.
    std::unordered_map<std::string, Portrait> partyFaces_;
    uint64_t partyFacesTaken_ = 0;
    std::vector<uint32_t> faceIds_;  // whose faces Play gathers (Play::wantFaces)
    PlayerMenu playerMenu_;
    bool menuTook_ = false;  // the player menu took this frame's press
    // Another player, inspected (game/ui/inspect.h): by name, and the body he was when it opened.
    // In the character window's column, which it shuts; his figure is his doll (Play::wantDoll),
    // photographed whole (Portrait's figure), and opening it targets him.
    Inspect inspect_;
    std::string inspectName_;
    uint32_t inspectId_ = 0;
    Portrait inspectFigure_{true};
    std::vector<gfx::Drawable> inspectBody_;  // his doll's drawables and its wings', photographed together
    float inspectTurned_ = 0.0f;               // the turn the picture was taken at
    bool scriptInspect_ = false;
    bool inspecting() const { return !inspectName_.empty(); }
    void openInspect(Play& play, const std::string& name);
    void closeInspect();
    // The map around him, top right over the tracker, always up in a played world (game/ui/minimap.h).
    Minimap minimap_;
    Menu menu_;
    std::string worldName_;
    std::string heroName_;
    bool quitAsked_ = false;
    bool switchAsked_ = false;
    bool holdEscape_ = false;
    bool settingsChanged_ = false;
    std::string scriptTyped_;
    bool scriptEnter_ = false, scriptEscape_ = false;
    bool scriptTab_ = false;
    bool scriptMap_ = false;
    bool scriptMapHeld_ = false;
    Endurance endurance_;
    Cursor cursor_;
    Vitals vitals_;
    BossBar bossBar_;  // the Golden Dragon's bar and its party's (game/ui/boss_bar.h)
    Speech speech_;
    Beacon beacon_;
    Tally tally_;
    Arrival arrival_;
    ItemModels* models_ = nullptr;
    ItemStage bagStagePicture_, shelfStagePicture_, quickStagePicture_, tipStagePicture_;
    ItemStage inspectStagePicture_;  // the inspect window's, open beside the bag's
    std::string shaderDir_, assetDir_;
    content::Textures* textures_ = nullptr;
    Stage* bagStage_ = nullptr;
    // The names over the drops, Diablo IV's way since 2026-10-06: a serif name on a dark strip
    // that fades out at both ends. Rebuilt when one moves on screen, or the pointer moves on one.
    gfx::Canvas ground_;
    // Their face, Amarante, baked here; the canvas's own face stands in when it failed.
    gfx::Face groundFace_;
    bgfx::TextureHandle groundTexture_ = BGFX_INVALID_HANDLE;
    uint32_t groundLit_ = 0;  // the drop whose strip was drawn lit, under the pointer
    std::vector<Play::OnScreen> onScreen_, drawnOnScreen_;
    // Each name's plate as it was drawn, so the pointer can be on a name as well as on a thing.
    struct Plate {
        uint32_t id = 0;
        gfx::Box box;
    };
    std::vector<Plate> plates_;
    // The order the names were stacked in last time, bottom first, so a turn of the camera does
    // not reshuffle a pile whose drops stand at nearly one height (Desk::labelGround).
    std::vector<uint32_t> stacked_;
    float viewProj_[16] = {};
    uint64_t groundRebuilds_ = 0;
    void labelGround(const Play& play, int width, int height);
    Stage* shelfStage_ = nullptr;
    bool trading_ = false;
    // A mending counter's repair mode: a bag click mends instead of lifting. The desk's, as the
    // shelf draws it and the bag obeys it.
    bool mending_ = false;
    // The vault, which borrows the shelf's stage: a counter and the vault are never open
    // together, since each is closed by any order and opened by one.
    bool banking_ = false;
    bool bagForVault_ = false;
    bool brokering_ = false, bagForBroker_ = false;
    bool dealing_ = false;
    bool bagForDeal_ = false;
    // The trade news said and not yet put in the chat (Play::takeDealNews).
    void dealNews(Play& play);
    // The four potion keys' bindings, as MU's item row. The interface's; game/save.cpp writes
    // them (mu.db's character_hotkeys is where MU2 kept them).
    int32_t quick_[Hud::kQuickKeys] = {-1, -1, -1, -1, -1};
    int scriptedKey_ = -1;
    void quickKeys(const gfx::Window& window, Play& play, const Pointer& pointer);
    // What is on Q W E R, by MU's skill number. A newly learned skill takes the first free key
    // ONCE -- `autoBound_` is what stops it coming back the moment the player takes it off -- and
    // after that the bar is his: right-click a box to open the list, drag a row onto a key, drag
    // a key's skill onto another to swap the two, drag it back into the list to clear it
    // (docs/game/skills-dk.md §3.4, and the user's own gesture, 2026-09-23).
    // Not saved, which is faithful -- there is no SaveHotKey anywhere in MuMain.
    // And the sixth, `Hud::kRightSlot`: what a right-click on a monster throws.
    int32_t bound_[Hud::kSkillBoxes] = {0, 0, 0, 0, 0, 0};
    // What each key was bound to and whether it could be thrown, last frame: a key that goes
    // from cooling or short of mana to throwable is told so (Hud::readySkill). By the number,
    // so a skill dropped onto a key is not announced as having come back.
    int32_t readyFor_[Hud::kSkillBoxes] = {0, 0, 0, 0, 0, 0};
    bool wasReady_[Hud::kSkillBoxes] = {false, false, false, false, false, false};
    float lastCooling_[Hud::kSkillBoxes] = {};  // last frame's wipe, to see one start over
    // The skill the realm last threw: only its box wears the cast's wait, not every primary's.
    int32_t lastThrown_ = 0;
    uint64_t autoBound_ = 0;  // skills that have had their one free key, by index (kSkills)
    bool barRestored_ = false;
    // The list above the plate: latched open by a click on the gold box, and open anyway while
    // the pointer rests on that box or on the list itself. The cells are rebuilt every frame off
    // what the realm says he has learned.
    bool fanLatched_ = false;
    std::vector<Hud::FanCell> fan_;
    int32_t carrying_ = 0;    // the skill the pointer is holding, 0 for none
    int carryFrom_ = -1;      // the key it was lifted off, or -1 out of the list
    int liftedQuick_ = -1;    // the potion box the pointer is holding, or -1
    int scriptedSkill_ = -1;
    int scriptedHold_ = -1;
    void skillKeys(const gfx::Window& window, Play& play, const Pointer& pointer);
    // No `why` any more: the card carries every refusal as one of its own rows (2026-09-23), so
    // there is nothing left for a sentence to add.
    tip::Sheet skillSheet(const sim::SkillRow& row, const sim::Realm& realm) const;
    bool bagForShop_ = false;  // the bag was opened by the counter, and goes when it does
    bool inventoryOpen_ = false;
    bool characterOpen_ = false;
    bool watching_ = false;
    Specimen specimen_;
    bool specimenOpen_ = false;
    bool takesPointer_ = false;
    // Whether the press now held began over a window (Desk::update): the world hears none of it.
    bool pressOnWindow_ = false;
    bool scripted_ = false;
    Pointer script_;
};

}  // namespace mu::game
