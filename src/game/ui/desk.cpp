#include "game/ui/desk.h"

#include "sim/realm_tuning.h"

#include <ctime>
#include <cstdio>

#include "core/log.h"
#include "game/ui/controls.h"
#include "game/ui/describe.h"
#include "game/world/maps.h"

namespace mu::game {

// Whether a row may be bound to a key at all: CanRegisterItemHotKey's list, of which this
// catalogue has the apple, the six potions, the Ale and the Town Portal Scroll. Held.Usable.
static bool usable(const content::Tables& tables, int32_t item) {
    if (item < 0) return false;
    const content::ItemRow& row = tables.items[size_t(item)];
    // And the Antidote, ours (the user, 2026-09-30: "i cant move antidote to potion quickslot"):
    // MU's list has no antidote, and the Dungeon's poisons want one to hand.
    return sim::heals(row) || sim::restores(row) || sim::ale(row) || sim::portal(row) ||
           sim::antidote(row);
}

// Whether a carried row may stand in for a bound one: the same group, and either exactly the
// Ale or the Town Portal, or the same family at no higher a rank -- the healing family falls
// to the apple and the mana family to the small mana potion. Quick.Substitutes.
static bool substitutes(const content::Tables& tables, int32_t carried, int32_t bound) {
    const content::ItemRow& c = tables.items[size_t(carried)];
    const content::ItemRow& b = tables.items[size_t(bound)];
    if (c.group != b.group) return false;
    if (sim::ale(b) || sim::portal(b) || sim::antidote(b)) return c.number == b.number;
    return c.number <= b.number && (b.number >= 4 ? sim::restores(c) : sim::heals(c));
}


// What each key is called, for the log. The plate prints these itself and `Hud::kKeys` has
// them too; this is the same five, where the desk can reach them.
static const char* keyName(int key) {
    static const char* kNames[Hud::kSkillBoxes] = {"Q", "W", "E", "R", "T", "the right button"};
    return key >= 0 && key < Hud::kSkillBoxes ? kNames[key] : "?";
}

bool Desk::open(const std::string& shaderDir, const std::string& assetDir,
                content::Textures* textures) {
    if (!interface_.init(shaderDir)) return false;
    shaderDir_ = shaderDir;
    assetDir_ = assetDir;
    textures_ = textures;
    arts_.open(assetDir, textures);
    panel::openTitleFace(interface_);
    // All of it, here on the preloader's worker rather than in the frame a window first wants
    // a piece: see Arts::warm.
    arts_.warm();
    // A stage each, so the bag and the shelf can hold different things at once, and each is
    // the window's own size: one pass draws every picture in a window and they line up with
    // its cells for free.
    bagStage_ = &bagStagePicture_;
    shelfStage_ = &shelfStagePicture_;
    hud_.open(interface_, &arts_);
    hud_.useStage(&quickStagePicture_);
    // One stage for whatever the tooltip is describing, shared: only one tip is up at a time.
    bag_.useTipStage(&tipStagePicture_);
    shelf_.useTipStage(&tipStagePicture_);
    chest_.useTipStage(&tipStagePicture_);
    mixer_.useTipStage(&tipStagePicture_);
    questDialog_.useTipStage(&tipStagePicture_);
    card_.open(interface_, &arts_);
    bag_.open(interface_, &arts_);
    shelf_.open(interface_, &arts_);
    chest_.open(interface_, &arts_);
    mixer_.open(interface_, &arts_);
    amount_.open(interface_, &arts_);
    questDialog_.open(interface_);
    tracker_.open(interface_);
    travel_.open(interface_, assetDir);
    goBack_.open(interface_);
    herald_.open(interface_);
    minimap_.open(interface_);
    menu_.open(interface_);
    endurance_.open(interface_, &arts_);
    cursor_.open(interface_, &arts_);
    vitals_.open(interface_);
    speech_.open(interface_);
    beacon_.open(interface_);
    tally_.open(interface_);
    arrival_.open(interface_);
    // The Sanctuary controls' faces and stone, which every window's frame is drawn with.
    controls::open();
    specimen_.open(interface_);
    interface_.adopt(ground_);
    return true;
}

void Desk::shutdown() {
    questDialog_.close();
    tracker_.close();
    travel_.close();
    goBack_.close();
    herald_.close();
    minimap_.close();
    specimen_.close();
    controls::close();
    panel::closeTitleFace();
    bagStagePicture_.shutdown();
    shelfStagePicture_.shutdown();
    quickStagePicture_.shutdown();
    tipStagePicture_.shutdown();
    arrival_.shutdown();
    tally_.shutdown();
    beacon_.close();
    interface_.shutdown();
}

void Desk::update(float seconds, const gfx::Window& window, Play& play, float pointerX,
                  float pointerY) {
    // The store is handed in from outside, with the item rows already in it; until it is, the
    // windows draw each thing's name in its cell.
    Pointer pointer;
    pointer.x = pointerX;
    pointer.y = pointerY;
    pointer.pressed = window.clicked(0);
    pointer.released = window.released(0);
    pointer.held = window.held(0);
    pointer.rightPressed = window.clicked(1);
    if (scripted_) {
        pointer = script_;
        scripted_ = false;
    }

    panel::setScreen(float(window.height()));
    panel::setFloor(hud_.plateTop());
    arrival_.update(seconds, float(window.width()), float(window.height()));
    const sim::Body* hero = play.isOpen() ? &play.realm().hero() : nullptr;
    hud_.follow(hero);

    // Every window opened or shut clicks, by key or by button: MU2's Desk.Click on each
    // Toggle, which is SOUND_CLICK01 off every button in the client.
    const auto click = [&]() {
        if (play.isOpen()) play.ui(Play::Ui::Click);
    };
    const auto refused = [&]() {
        if (play.isOpen()) play.ui(Play::Ui::Refused);
    };
    const auto took = [&]() {
        if (play.isOpen()) play.ui(Play::Ui::Took);
    };
    // **The number box, before anything else, and it takes everything.** It is modal, as MU's
    // message boxes are: the pointer's presses and the keys are its own while it is up, so the
    // windows under it do not hear a click on its OK and a 1 typed into it is not the first
    // potion drunk. `typing` is taken before the box answers, so the frame Return closes it on
    // does not hand the same keys on either. The cursor is still drawn where the pointer is.
    const bool typing = amount_.up();
    const Pointer real = pointer;
    // Escape this frame, taken before the box below spends the script's: the box has it first
    // while it is up, and only otherwise is it the menu's. A scripted one always is, so a review
    // run can open the menu; a real one only when the game holds Escape (PlayMode::open).
    // And a quest giver's window, which is answered as the box is and takes Escape before the menu.
    const bool questing = play.isOpen() && (play.realm().questing() >= 0 || journal_ >= 0);
    const bool escape =
        !typing && !questing && (scriptEscape_ || (holdEscape_ && window.escaped()));
    {
        Amount::Result result;
        const std::string typed = window.typed() + scriptTyped_;
        amount_.update(seconds, float(window.width()), float(window.height()), pointer, typed,
                       window.backspaces(), window.entered() || scriptEnter_,
                       window.escaped() || scriptEscape_, &result);
        scriptTyped_.clear();
        scriptEnter_ = scriptEscape_ = false;
        if (result.cancel) {
            amount_.hide();
            click();
        } else if (result.amount > 0) {
            // CZenReceiptMsgBoxLayout::ProcessOk: sent, clicked and shut -- or, short, said so.
            const bool deposit = amount_.purpose() == Amount::Purpose::Deposit;
            if (deposit ? play.depositZen(result.amount) : play.withdrawZen(result.amount)) {
                amount_.hide();
                click();
            } else {
                amount_.refuse();
                refused();
            }
        }
    }
    // The quest giver's window: the realm opened it when he was reached, and closes it on any other
    // order; this only answers. The wall clock a repeating quest waits on goes in first.
    if (play.isOpen()) {
        play.setWallClock(int64_t(std::time(nullptr)));
        const sim::Realm& realm = play.realm();
        // Or the journal (L), the same window read away from him; reaching him takes it over.
        int quest = -1;
        bool listed = false;
        if (realm.questing() >= 0) {
            const int32_t giver = realm.tables()->folk[size_t(realm.questing())].number;
            if (realm.questing() != listFolk_) picked_ = -1;
            listFolk_ = realm.questing();
            // More than one of his, or one waiting on his level: his list, then the one picked.
            listed = realm.questListed(giver);
            quest = !listed ? realm.questHere(giver) : picked_ >= 0 ? picked_ : QuestDialog::kList;
            journal_ = -1;
        } else {
            listFolk_ = picked_ = -1;
        }
        questDialog_.setListed(listed && picked_ >= 0);
        // The Messenger of Archangel's page, Blood Castle's door (QuestDialog::kGate).
        const bool gating = realm.gating() >= 0;
        if (gating) {
            quest = QuestDialog::kGate;
            journal_ = -1;
        }
        // And the Archangel's, in Blood Castle's court (QuestDialog::kArchangel).
        const bool angeling = realm.angeling() >= 0;
        if (angeling) {
            quest = QuestDialog::kArchangel;
            journal_ = -1;
        }
        const bool reading = quest < 0 && journal_ >= 0;
        if (reading) quest = journal_;
        // The live quests, for the journal's arrows: where this page is among them.
        int live[sim::kQuests];
        int lives = 0, liveAt = -1;
        for (int q = 0; q < sim::kQuests; ++q) {
            const sim::QuestState state = realm.quest(q).state;
            if (state != sim::QuestState::Active && state != sim::QuestState::Ready) continue;
            if (q == quest) liveAt = lives;
            live[lives++] = q;
        }
        questDialog_.setPages(reading && liveAt >= 0 ? liveAt + 1 : 0, reading && liveAt >= 0 ? lives : 0);
        QuestDialog::Result result;
        const bool free = !typing && quest >= 0;
        questDialog_.update(seconds, play, quest, reading, float(window.width()),
                            float(window.height()),
                            free ? pointer : Pointer{}, free ? window.scroll() : 0.0f,
                            free && window.entered(),
                            free && (window.escaped() || scriptEscape_), shelfStage_, &result);
        // His voice reads the page, from the frame it comes up: a new page cuts the last one
        // and the window shutting stops him mid-line. The Messenger and the Archangel say what
        // their page says, so theirs is the dialog's clip.
        const int page = questDialog_.page();
        std::string voiced;
        if (gating || angeling) {
            voiced = questDialog_.clip();
        } else if (page >= 0 && !reading) {
            static const char* const kPage[4] = {"offer", "underway", "handin", "resting"};
            const char* who = sim::questAt(quest).voice;
            if (who && *who) voiced = std::string("voice/") + who + "/" + who + "_" + kPage[page] + ".wav";
        }
        if (voiced != voiced_) {
            voiced_ = voiced;
            if (!voiced.empty()) play.sound().voice(voiced);
            else play.sound().stopVoice();
        }
        // A step struck off on the tracker last frame: the blade's cut on its flare, heard at the
        // ears as the interface's are.
        if (tracker_.takeStrike()) play.sound().play(play.sound().load("quest_step_done", false));
        // A giver's window opens as a counter does: ReceiveTalk's click and SOUND_INTERFACE01.
        const bool questing = realm.questing() >= 0 || gating || angeling;
        if (questing && !questing_) {
            click();
            play.ui(Play::Ui::Opened);
        }
        questing_ = questing;
        if (result.picked) click();
        // The Messenger's castles turned (QuestDialog::kGate): the journal's page sound.
        if (gating && result.turn != 0) {
            play.sound().play(play.sound().load("quest_page_turn", false));
        }
        // The journal's arrows: the live quest before or after, round the ends.
        if (reading && result.turn != 0 && liveAt >= 0 && lives > 1) {
            questDialog_.turning(result.turn);
            journal_ = live[(liveAt + result.turn + lives) % lives];
            // The page's own sound, not the click: the turn is timed to its two strokes.
            play.sound().play(play.sound().load("quest_page_turn", false));
        }
        if (result.pick >= 0) {
            picked_ = result.pick;
            click();
        } else if (result.back) {
            picked_ = -1;
            click();
        } else if (result.close) {
            if (reading) journal_ = -1;
            else if (gating) play.closeGate();
            else if (angeling) play.closeAngel();
            else play.closeQuest();
            click();
        } else if (result.give) {
            const bool given = play.handInStaff();
            core::logf("event: the Divine Staff to the Archangel -- %s", given ? "given" : "refused");
            if (given) click();
            else refused();
        } else if (result.claim) {
            const bool claimed = play.claimCastle();
            core::logf("event: Complete on the Archangel's thanks -- %s", claimed ? "to Devias" : "refused");
            if (claimed) {
                play.closeAngel();
                click();
            } else {
                refused();
            }
        } else if (result.enter) {
            const bool went = play.enterCastle(result.castle);
            core::logf("event: Enter on Blood Castle %d -- %s", result.castle,
                       went ? "through the gate" : "refused");
            if (went) click();
            else refused();
        } else if (result.accept) {
            if (play.acceptQuest(quest)) play.closeQuest();
        } else if (result.complete) {
            if (play.completeQuest(quest, result.choice)) play.closeQuest();
        }
    }
    // A Town Portal Scroll read shuts the bag and the character window, silently: ReceiveTeleport's
    // `g_pNewUISystem->HideAll()`. The realm has already closed the counter and the vault. And the
    // same handler ends `if (Data->Flag) g_pUIMapName->ShowMapName()`, which a warp has set even
    // into the map it left, so the town's name comes up at once, as it does on the way in.
    if (play.takeWarp()) {
        inventoryOpen_ = characterOpen_ = false;
        fanLatched_ = false;
        arrival_.announce(hero ? placeName(worldName_, hero->column(), hero->row()) : worldName_,
                          0.0f, play.zoneLevels());
    }
    // **Escape, in MU's order and then the menu.** A box takes it first (above). With any window
    // open it shuts them all, as CNewUIManager's Escape closes the open windows before anything
    // else; only with nothing open does it raise the menu. And up, the menu has it: back a page,
    // or down.
    const bool windowsOpen =
        inventoryOpen_ || characterOpen_ || trading_ || banking_ || mixing_ || travel_.up();
    {
        std::string place = hero ? placeName(worldName_, hero->column(), hero->row()) : worldName_;
        if (hero) {
            place += (place.empty() ? "" : "  \xC2\xB7  ") + std::to_string(hero->column()) +
                     ", " + std::to_string(hero->row());
        }
        const bool wasUp = menu_.up();
        if (!wasUp && escape) {
            if (mending_) {
                // Repair mode is the first rung: Escape puts the hammer down -- the cursor and
                // the foot's button both -- and leaves the bag or the counter open. The user:
                // 'if player press escape he cancels the repair icon and button state'.
                mending_ = false;
                core::logf("window: repair mode off");
            } else if (windowsOpen) {
                inventoryOpen_ = characterOpen_ = false;
                if (trading_) play.closeTrade();
                if (banking_) play.closeVault();
                if (mixing_) play.closeMachine();
                travel_.hide();
                fanLatched_ = false;
            } else {
                menu_.show();
                core::logf("window: menu up");
            }
            click();
        }
        Menu::Result asked;
        menu_.update(seconds, float(window.width()), float(window.height()),
                     wasUp ? pointer : Pointer{-1.0f, -1.0f}, wasUp && escape, place, &asked);
        if (asked.clicked) click();
        if (asked.closed) core::logf("window: menu down");
        if (asked.settings) settingsChanged_ = true;
        if (asked.quit) {
            quitAsked_ = true;
            core::logf("window: exit asked from the menu");
        }
        if (asked.switched) {
            switchAsked_ = true;
            core::logf("window: switch character asked from the menu");
        }
    }
    // Modal, as the box is: the menu has the pointer and the keys while it is up, and the frame
    // it went down on hands neither on, so the click that shut it does not walk him.
    const bool menuHeld = menu_.up() || escape;
    if (typing || menuHeld) {
        pointer.pressed = pointer.released = pointer.rightPressed = pointer.held = false;
        pointer.x = pointer.y = -1.0f;
    }
    const bool keysHeld = typing || menuHeld;

    if (!keysHeld && window.pressed(gfx::Window::Key::Inventory)) {
        inventoryOpen_ = !inventoryOpen_;
        click();
    }
    if (!keysHeld && window.pressed(gfx::Window::Key::Character)) {
        characterOpen_ = !characterOpen_;
        click();
    }
    // L, the quest journal: the tracked quest's page -- the one under way, else the one resting,
    // else the first on offer -- and L again shuts it. With the bag up L is the bag's repair.
    // Ours: MU 0.75 has no quests, and its L is only the repair.
    const bool journalAsked = scriptJournal_;
    scriptJournal_ = false;
    if (!keysHeld && !inventoryOpen_ && play.isOpen() && play.realm().questing() < 0 &&
        (window.pressed(gfx::Window::Key::Repair) || journalAsked)) {
        if (journal_ >= 0) {
            journal_ = -1;
        } else {
            const sim::Realm& realm = play.realm();
            // The one the tracker follows first: the quest he is fighting for on this map.
            journal_ = tracker_.following();
            for (int q = 0; q < sim::kQuests && journal_ < 0; ++q) {
                const sim::QuestState state = realm.quest(q).state;
                if (state == sim::QuestState::Active || state == sim::QuestState::Ready) journal_ = q;
            }
            for (int q = 0; q < sim::kQuests && journal_ < 0; ++q) {
                if (realm.quest(q).state == sim::QuestState::Resting && !realm.questOffered(q)) {
                    journal_ = q;
                }
            }
            if (journal_ < 0 && sim::kQuests > 0) journal_ = 0;
        }
        click();
    }

    // M, the travel list (game/ui/travel.h): M again or Escape shuts it, and a map pressed
    // on is asked of the realm, which takes the Zen; the mode changes the map (Play::takeTravel).
    // Ours: 0.75 has only the `/move` command.
    if (!keysHeld && play.isOpen() && (window.pressed(gfx::Window::Key::Travel) || scriptTab_)) {
        travel_.toggle();
        core::logf("window: travel %s", travel_.up() ? "up" : "down");
        click();
    }
    scriptTab_ = false;
    // Tab, held: the whole map over the middle of the screen, down again when it is let go (the
    // user, 2026-10-01: 'm opens world list tab opens (on hold) big map'). A scripted "map"
    // latches it instead, as there is no key to hold. Ours: 0.75 has no map at all (Season 3's
    // full map is on Tab).
    if (scriptMap_) scriptMapHeld_ = !scriptMapHeld_;
    const bool mapWanted = play.isOpen() && ((!keysHeld && window.down(gfx::Window::Key::Map)) ||
                                             scriptMapHeld_);
    if (mapWanted != minimap_.full()) {
        minimap_.setFull(mapWanted);
        core::logf("window: map %s", mapWanted ? "up" : "down");
        // The journal's page turn, on opening only (the user, 2026-10-01: 'plat map sound only
        // when open TAB, not close').
        if (mapWanted) play.sound().play(play.sound().load("quest_page_turn", false));
    }
    scriptMap_ = false;
    // The press that took him is the list's even as it shuts on it: on his own map no new world
    // eats it, and it would walk him from the landing to where the button was.
    bool travelPressed = false;
    if (travel_.up() && play.isOpen()) {
        const int go = travel_.update(play, pointer, window.width(), window.height());
        if (go >= 0 && play.travel(go)) {
            travel_.hide();
            click();
            travelPressed = true;
        }
    }
    // Go Back!: clicked, it clicks, and the mode takes him back (app/modes/play_mode.cpp), where
    // the warp's own sMagic plays as he lands. Not while a box or the menu has the screen.
    {
        const bool held = amount_.up() || menu_.up();
        if (goBack_.update(seconds, goBackShown_ && play.isOpen(), goBackLeft_, goBackClosed_,
                           goBackWhere_, held ? Pointer{} : pointer, window.width(),
                           window.height(), hud_.plateTop())) {
            goBackAsked_ = true;
            click();
        }
    }
    // The herald: the event coming for his level, at its three moments. Its cross clicks.
    if (herald_.update(seconds, play, amount_.up() || menu_.up() ? Pointer{} : pointer,
                       window.width(), window.height())) {
        click();
    }

    bool toggleInventory = false, toggleCharacter = false, toggleMenu = false;
    hud_.update(seconds, float(window.width()), float(window.height()), pointer, inventoryOpen_,
                characterOpen_, &toggleInventory, &toggleCharacter, &toggleMenu);
    if (toggleMenu) {
        menu_.show();
        core::logf("window: menu up");
        click();
    }
    if (toggleInventory) {
        inventoryOpen_ = !inventoryOpen_;
        click();
    }
    if (toggleCharacter) {
        characterOpen_ = !characterOpen_;
        click();
    }

    // The character window, while it is up. What it asks for is answered here, by the realm,
    // and the window sees the answer on its next frame.
    if (characterOpen_) {
        int spend = -1;
        bool close = false;
        card_.update(float(window.width()), float(window.height()), hero, pointer, &spend,
                     &close);
        // The stat button clicks whether or not the point lands: CNewUICharacterInfoWindow
        // sends the request and plays SOUND_CLICK01 on the next line without waiting.
        if (spend >= 0) {
            play.spendPoint(spend);
            click();
        }
        // The exit button clicks as the C key does. INVENTION, the user's (2026-09-30):
        // CNewUICharacterInfoWindow's m_BtnExit has no PlayBuffer, and a silent X read as broken.
        if (close) {
            characterOpen_ = false;
            click();
        }
    }

    // A merchant's counter opens the bag beside it and closes the character window, which is
    // MU's arrangement: the shop in column two and the inventory where it always is. Walking
    // away closes the counter in the realm, and the windows follow.
    const bool trading = play.isOpen() && play.realm().trading() >= 0;
    // A counter opens on ReceiveTalk's click and SOUND_INTERFACE01 together. Walking away
    // shuts it silently; the shelf's own X is Escape's stand-in and clicks (below).
    if (trading && !trading_) {
        click();
        if (play.isOpen()) play.ui(Play::Ui::Opened);
    }
    if (trading && !trading_) {
        characterOpen_ = false;
        bagForShop_ = !inventoryOpen_;
        inventoryOpen_ = true;
    }
    if (!trading && trading_ && bagForShop_) {
        inventoryOpen_ = false;
        bagForShop_ = false;
    }
    trading_ = trading;
    // Repair mode lives as long as something can mend: a counter that mends (CNewUINPCShop's
    // ClosingProcess puts the shop back to buy-and-sell), or the bag open on a character of
    // kSelfRepairLevel or more (CNewUIMyInventory's own repair, `m_bRepairEnableLevel`).
    const bool mends = trading_ && play.realm().mending();
    const bool canMend = mends || (inventoryOpen_ && play.isOpen() && play.realm().selfMending());
    if (!canMend) mending_ = false;
    // L turns it on and off, Shift+L mends everything at a counter (CNewUINPCShop::
    // UpdateKeyEvent) -- not while a box is taking letters. L below level 50 with no counter is
    // the interface's no, as the dark hammer is.
    if (inventoryOpen_ && window.pressed(gfx::Window::Key::Repair) && !window.typing() &&
        !keysHeld) {
        if (mends && window.shift()) {
            if (!play.repairAll()) refused();
        } else if (canMend) {
            mending_ = !mending_;
            click();
        } else {
            refused();
        }
    }
    if (trading_) {
        int buy = -1;
        bool close = false;
        bool undo = false;
        ShelfMending mend;
        shelf_.setMending(mending_);
        shelf_.update(float(window.width()), float(window.height()), 2, play.realm(), pointer,
                      shelfStage_, &buy, &close, &mend, &undo);
        // A purchase that goes through is heard as its coins, off the realm's Bought; one
        // refused is the interface's no.
        if (buy >= 0 && !play.buy(buy)) refused();
        if (mend.toggle) {
            mending_ = !mending_;
            click();
        }
        if (mend.all && !play.repairAll()) refused();
        if (undo && !play.buyBack()) refused();
        if (close) {
            play.closeTrade();
            click();
        }
    }
    bag_.setMending(mending_);

    // The vault, as a counter is: it opens the bag beside it and closes the character window,
    // and walking away closes it in the realm with the windows following.
    const bool banking = play.isOpen() && play.realm().banking() >= 0;
    if (banking && !banking_) {
        click();
        if (play.isOpen()) play.ui(Play::Ui::Opened);
        characterOpen_ = false;
        bagForVault_ = !inventoryOpen_;
        inventoryOpen_ = true;
    }
    if (!banking) amount_.hide();
    if (!banking && banking_ && bagForVault_) {
        inventoryOpen_ = false;
        bagForVault_ = false;
    }
    banking_ = banking;
    if (banking_) {
        ChestRequests asked;
        // The bag's drag as it stood last frame: the bag updates after the vault.
        const bool fromBag = inventoryOpen_ && bag_.dragging();
        chest_.carrying(fromBag ? &play.realm().satchel()[bag_.dragged()] : nullptr,
                        fromBag && sim::baggable(bag_.dragged()));
        chest_.update(float(window.width()), float(window.height()), 2, play.realm(), pointer,
                      shelfStage_, &asked);
        if (asked.moveFrom >= 0) {
            if (play.rearrange(asked.moveFrom, asked.moveTo)) took();
            else refused();
        }
        // Let go outside: into the bag cell under the pointer, and nowhere else -- a thing in
        // the vault is not thrown on the ground from it, which MU refuses too.
        if (asked.outside >= 0) {
            const int slot = inventoryOpen_ ? bag_.slotUnder(asked.outsideX, asked.outsideY) : -1;
            if (slot >= 0 && play.withdraw(asked.outside, slot)) took();
            else refused();
        }
        // MU's own answer to either coin: the box to type the sum in (CZenReceiptMsgBoxLayout,
        // CZenPaymentMsgBoxLayout), which does the moving on its OK.
        if (asked.depositZen || asked.withdrawZen) {
            amount_.show(asked.depositZen ? Amount::Purpose::Deposit : Amount::Purpose::Withdraw);
            click();
        }
        if (asked.close) {
            play.closeVault();
            click();
        }
    }

    // The Chaos Machine, as the vault is: it opens the bag beside it and closes the character
    // window, and walking away closes it in the realm -- which puts the box back in the bag.
    const bool mixing = play.isOpen() && play.realm().mixing() >= 0;
    if (mixing && !mixing_) {
        click();
        if (play.isOpen()) play.ui(Play::Ui::Opened);
        characterOpen_ = false;
        bagForMachine_ = !inventoryOpen_;
        inventoryOpen_ = true;
    }
    if (!mixing && mixing_ && bagForMachine_) {
        inventoryOpen_ = false;
        bagForMachine_ = false;
    }
    mixing_ = mixing;
    if (mixing_) {
        MixerRequests asked;
        const bool fromBag = inventoryOpen_ && bag_.dragging();
        mixer_.carrying(fromBag ? &play.realm().satchel()[bag_.dragged()] : nullptr,
                        fromBag && sim::baggable(bag_.dragged()) && !play.realm().mixed());
        mixer_.update(seconds, float(window.width()), float(window.height()), 2, play.realm(),
                      play.mixAnswer(), play.mixWords(), pointer, shelfStage_, &asked);
        if (asked.moveFrom >= 0) {
            if (play.shuffle(asked.moveFrom, asked.moveTo)) took();
            else refused();
        }
        // Let go outside: into the bag cell under the pointer, and nowhere else, as the vault.
        if (asked.outside >= 0) {
            const int slot = inventoryOpen_ ? bag_.slotUnder(asked.outsideX, asked.outsideY) : -1;
            if (slot >= 0 && play.takeOut(asked.outside, slot)) took();
            else refused();
        }
        if (asked.back >= 0) {
            if (play.takeOut(asked.back, -1)) took();
            else refused();
        }
        if (asked.click) click();
        // The service row stepped: the journal's and the map's page sound.
        if (asked.turned) play.sound().play(play.sound().load("quest_page_turn", false));
        // Take out: everything in the box back to the bag, as many right-clicks.
        if (asked.takeAll) {
            bool any = false, all = true;
            for (int cell = 0; cell < sim::kMachineCells; ++cell) {
                if (play.realm().machine()[cell].empty()) continue;
                if (play.takeOut(cell, -1)) any = true;
                else all = false;
            }
            if (any && all) took();
            else refused();
        }
        // CMixCheckMsgBoxLayout's OK: the click, and the realm's answer is heard by Play.
        if (asked.mix) {
            click();
            if (play.mix(mixer_.service(), mixer_.socket())) mixer_.spark();
            else refused();
        }
        if (asked.close) {
            play.closeMachine();
            click();
        }
    }

    // The bag, in the right-hand column or beside the character window when that is up.
    if (inventoryOpen_ && play.isOpen()) {
        BagRequests asked;
        bag_.carrying(banking_ && chest_.dragging()   ? &play.realm().vault()[chest_.dragged()]
                      : mixing_ && mixer_.dragging() ? &play.realm().machine()[mixer_.dragged()]
                                                     : nullptr);
        bag_.update(float(window.width()), float(window.height()), characterOpen_ ? 2 : 1,
                    play.realm(), pointer, bagStage_, &asked);
        // A move is ReceiveEquipmentItem, which ends its success branch on SOUND_GET_ITEM01 --
        // MU's equip sound is the pickup's -- and a use refused is iButtonError. A use that goes
        // through is heard as the potion going down, off the realm's Drank.
        if (asked.moveFrom >= 0) {
            if (play.moveItem(asked.moveFrom, asked.moveTo)) took();
            else refused();
        }
        // A jewel on a thing: Play rings both of its sounds, so a yes needs nothing here.
        if (asked.refineJewel >= 0 && !play.refine(asked.refineJewel, asked.refineTarget)) {
            refused();
        }
        // At the machine a right-click puts the thing in the box instead
        // (ProcessMyInvenItemAutoMove); a worn one, which the box refuses, is still used.
        if (asked.use >= 0 && mixing_ && sim::baggable(asked.use)) {
            if (play.putIn(asked.use, -1)) took();
            else refused();
        } else if (asked.use >= 0 && !play.useItem(asked.use)) {
            refused();
        }
        // A click in repair mode: mended where it lies, heard as SOUND_REPAIR by Play, and a
        // refusal -- whole already, not repairable, not the Zen -- is the interface's no.
        if (asked.repair >= 0 && !play.repair(asked.repair)) refused();
        // The foot's hammer is self repair: on or off from level 50, and the interface's no below
        // it -- at a counter too, whose own hammers are on the shelf.
        if (asked.toggleMending) {
            if (play.realm().selfMending()) {
                mending_ = !mending_;
                click();
            } else {
                refused();
            }
        }
        // Let go outside the window. Something else may want it before the ground does --
        // MU2's Bag.Caught, asked first -- and what nothing catches is thrown on the ground
        // at his feet (SendRequestDropItem). Over the shelf it is a sale instead
        // (SendSellItemToNpcRequest), and the realm refuses a worn slot again.
        if (asked.outside >= 0 && trading_ && shelf_.covers(asked.outsideX, asked.outsideY)) {
            if (!play.sell(asked.outside)) refused();
        } else if (asked.outside >= 0 && banking_ &&
                   chest_.covers(asked.outsideX, asked.outsideY)) {
            // Into the vault cell under the pointer; over the foot or the head, the first cell
            // it fits in.
            if (play.deposit(asked.outside, chest_.cellUnder(asked.outsideX, asked.outsideY))) {
                took();
            } else {
                refused();
            }
        } else if (asked.outside >= 0 && mixing_ &&
                   mixer_.covers(asked.outsideX, asked.outsideY)) {
            if (play.putIn(asked.outside, mixer_.cellUnder(asked.outsideX, asked.outsideY))) {
                took();
            } else {
                refused();
            }
        } else if (asked.outside >= 0 && hud_.quickAt(asked.outsideX, asked.outsideY) >= 0) {
            // Let go over a potion box: bound, and the thing stays in the bag. MU2's Caught.
            // Heard as every other drop in the window is, and a thing that will not go on the
            // bar is the interface's no rather than a silent snap back.
            const int key = hud_.quickAt(asked.outsideX, asked.outsideY);
            const sim::Held& what = play.realm().satchel()[asked.outside];
            if (!what.empty() && usable(*play.realm().tables(), what.item)) {
                quick_[key] = what.item;
                core::logf("window: slot %d bound to key %d", asked.outside, key + 1);
                took();
            } else {
                refused();
            }
        } else if (asked.outside >= 0) {
            // The one gesture in the interface that gives something away, which is why it
            // takes a deliberate drag out of the window and not a click. A refusal -- a dead
            // man's drag -- is the interface's no, and the window puts the item back by
            // redrawing from a satchel that never changed.
            if (!play.discard(asked.outside)) refused();
        }
        // Clicks as the I and V keys do. INVENTION, the user's (2026-09-30): CNewUIMyInventory's
        // exit button is silent in MU.
        if (asked.close) {
            inventoryOpen_ = false;
            click();
        }
    }

    // The worn-gear warning, in its row over the potion belt (Hud::wornCell).
    if (play.isOpen()) {
        endurance_.update(float(window.width()), float(window.height()), hud_, play.realm(),
                          pointer);
    }

    if (play.isOpen()) {
        labelGround(play, window.width(), window.height());
        if (!keysHeld) {
            quickKeys(window, play, pointer);
            skillKeys(window, play, pointer);
        }
    }

    // The bench takes the pointer over its sheets, so pressing a switch on it does not walk him.
    if (specimenOpen_) {
        specimen_.update(seconds, float(window.width()), float(window.height()), pointer);
    }
    // The minimap, always up: it answers the pointer only for its names and the wheel's zoom,
    // and neither while a box or the menu has the screen.
    minimap_.update(seconds, play, typing || menuHeld ? Pointer{} : pointer,
                    typing || menuHeld ? 0.0f : window.scroll(), window.width(), window.height());
    takesPointer_ = typing || amount_.up() || menuHeld || hud_.covers(pointer.x, pointer.y) ||
                    minimap_.covers(pointer.x, pointer.y) ||
                    questDialog_.covers(pointer.x, pointer.y) ||
                    travelPressed || travel_.covers(pointer.x, pointer.y) || goBack_.covers(pointer.x, pointer.y) ||
                    herald_.covers(pointer.x, pointer.y) ||
                    (specimenOpen_ && specimen_.covers(pointer.x, pointer.y)) || carrying_ != 0 ||
                    liftedQuick_ >= 0 ||
                    (characterOpen_ && card_.covers(pointer.x, pointer.y)) ||
                    (inventoryOpen_ && (bag_.covers(pointer.x, pointer.y) || bag_.dragging())) ||
                    (trading_ && shelf_.covers(pointer.x, pointer.y)) ||
                    (banking_ && (chest_.covers(pointer.x, pointer.y) || chest_.dragging())) ||
                    (mixing_ && (mixer_.covers(pointer.x, pointer.y) || mixer_.dragging()));

    // The pointer, drawn last of all: MU2's Pointer.Show and Step in one call. The flags are
    // last frame's raycast (Play::point runs after this, on the same frame it is drawn), which
    // never shows -- a claw a frame behind a moving mouse is not a thing anyone can see.
    //
    // And the raycast is ignored outright where the pointer belongs to a window. Play::point
    // runs every frame whatever is open, so what lies BEHIND a window is still pointed at: a
    // pointer resting on an item in Lumen's shelf, with Lumen herself under the glass, came up
    // as the talking mouth, and over a monster it was the claw -- a cursor offering a click
    // that play_mode has already refused, since `windowed` swallows both buttons. The window's
    // own hand is the plain one, which is what MU draws over its interface.
    const bool world = play.isOpen() && !takesPointer_;
    const bool onMonster = world && play.pointedAt() != 0;
    const bool onLoot = world && play.pointedAt() == 0 && play.pointedLying() != 0;
    const bool onFolk = world && play.pointedFolk() >= 0;
    Cursor::Perch perch = Cursor::Perch::None;
    if (world && play.pointedPerch() >= 0) {
        const bool leans = play.realm().tables()->perches[size_t(play.pointedPerch())].leans;
        perch = leans ? Cursor::Perch::Lean : Cursor::Perch::Sit;
    }
    // In repair mode the pointer is MU's hammer wherever it rests, so the player sees the mode is
    // on and that a click on a thing mends it -- the user: 'we need to show hammer cursor'.
    cursor_.update(seconds, real.x, real.y, onMonster, onLoot, onFolk, perch, mending_,
                   real.held || real.pressed);
}

void Desk::script(float x, float y, bool press, bool release, bool right) {
    scripted_ = true;
    script_ = Pointer{};
    script_.x = x;
    script_.y = y;
    script_.pressed = press && !right;
    script_.rightPressed = press && right;
    script_.released = release && !right;
    script_.held = !release && !right;
    core::logf("window: scripted %s at (%.0f, %.0f)",
               right ? "right press" : (press ? "press" : (release ? "release" : "drag")),
               double(x), double(y));
}

void Desk::quickKeys(const gfx::Window& window, Play& play, const Pointer& pointer) {
    const sim::Realm& realm = play.realm();
    const content::Tables& tables = *realm.tables();
    const sim::Satchel& bag = realm.satchel();
    // **The boxes rearrange by dragging**, as the skill keys do (the user, 2026-10-01: "i cant
    // reorder potions / drag/drop"). A press lifts a bound box; let go on another box it is a
    // swap, on its own box nothing, and anywhere off the boxes it is taken off the bar -- the
    // bottles stay in the bag either way.
    if (pointer.pressed && liftedQuick_ < 0 && carrying_ == 0) {
        const int key = hud_.quickAt(pointer.x, pointer.y);
        if (key >= 0 && quick_[key] >= 0) liftedQuick_ = key;
    }
    if (liftedQuick_ >= 0 && pointer.released) {
        const int onto = hud_.quickAt(pointer.x, pointer.y);
        if (onto >= 0 && onto != liftedQuick_) {
            std::swap(quick_[onto], quick_[liftedQuick_]);
            core::logf("window: key %d moved to key %d", liftedQuick_ + 1, onto + 1);
            play.ui(Play::Ui::Took);
        } else if (onto < 0) {
            quick_[liftedQuick_] = -1;
            core::logf("window: key %d taken off the bar", liftedQuick_ + 1);
            play.ui(Play::Ui::Took);
        }
        liftedQuick_ = -1;
    }
    hud_.liftQuick(liftedQuick_);
    const gfx::Window::Key keys[Hud::kQuickKeys] = {
        gfx::Window::Key::Potion1, gfx::Window::Key::Potion2, gfx::Window::Key::Potion3,
        gfx::Window::Key::Potion4, gfx::Window::Key::Potion5};
    for (int key = 0; key < Hud::kQuickKeys; ++key) {
        if (!window.pressed(keys[key]) && scriptedKey_ != key) continue;
        // Hovering a thing in the open bag and pressing the key binds it, which is MU's own
        // gesture (CNewUIMyInventory::UpdateKeyEvent); otherwise the key uses what is bound.
        const int hovered = inventoryOpen_ ? bag_.hovered() : -1;
        if (hovered >= 0 && usable(tables, bag[hovered].item)) {
            quick_[key] = bag[hovered].item;
            // SetItemHotKey plays nothing, and neither does a thing that cannot be bound.
            continue;
        }
        // A thing hovered that will not go on the bar is not used through it either: MuMain's
        // UpdateKeyEvent returns before the use when the pointer is on an item.
        if (hovered >= 0 && !bag[hovered].empty()) continue;
        if (quick_[key] < 0) continue;
        // The strongest of what may stand in for it, which is where MU's descending walk stops
        // first. Quick.Choose.
        int best = -1, strongest = -1;
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            if (bag[slot].empty() || !substitutes(tables, bag[slot].item, quick_[key])) continue;
            const int number = tables.items[size_t(bag[slot].item)].number;
            if (number > strongest) {
                strongest = number;
                best = slot;
            }
        }
        // The ring is struck on the realm's yes and not on the press: a key hit with nothing
        // left to drink must look like nothing happened, because nothing did.
        if (best >= 0 && play.useItem(best)) hud_.strikeQuick(key);
    }
    scriptedKey_ = -1;
    // And what each box shows, handed to the frame.
    for (int key = 0; key < Hud::kQuickKeys; ++key) {
        Hud::Quick q;
        q.item = quick_[key];
        if (q.item >= 0) {
            q.label = tables.items[size_t(q.item)].label;
            for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
                if (!bag[slot].empty() && substitutes(tables, bag[slot].item, q.item)) {
                    q.count += std::max<int>(1, bag[slot].durability);
                }
            }
        }
        hud_.setQuick(key, q);
    }
}

// The skill bar: the four keys, and what the four boxes show.
//
// The press is the whole of the gesture and the realm decides everything about it -- learned,
// cooling, in reach, paid for. What this owes the player is the PICTURE of that decision, which is
// why the box is handed the cooldown as a fraction and in seconds rather than a bool: a skill that
// says nothing while it cools is a key the player thinks is broken.
void Desk::skillKeys(const gfx::Window& window, Play& play, const Pointer& pointer) {
    const sim::Realm& realm = play.realm();
    const content::Tables& tables = *realm.tables();
    const sim::Body& hero = realm.hero();

    // A bar that came out of the save is the player's arrangement whole: every skill he already
    // knew when it was written has had its chance at a key, whether or not it is on one. Only a
    // skill learned AFTER that still takes a free key by itself.
    if (barRestored_) {
        barRestored_ = false;
        for (int i = 0; i < sim::skillCount(); ++i) {
            if (realm.knows(sim::skillAt(i).number)) autoBound_ |= uint64_t(1) << i;
        }
    }

    // Bound on the day it is learned, first free key first -- and ONCE. `autoBound_` is the
    // difference between a convenience and a bar that cannot be changed: without it, a skill the
    // player drags off a key is put back by this loop on the very next frame, which is what it
    // did until the list existed.
    for (int i = 0; i < sim::skillCount(); ++i) {
        const sim::SkillRow& row = sim::skillAt(i);
        if (!realm.knows(row.number)) continue;
        const uint64_t bit = uint64_t(1) << i;
        if ((autoBound_ & bit) != 0) continue;
        // Marked as having had its chance HERE, before anything is bound, and whether or not a
        // key was free for it. A knight knows six skills and the bar holds four, so two of them
        // never find one -- and while they were left unmarked, clearing a key by hand was
        // answered on the very next frame by one of those two dropping into it. The convenience
        // is one offer a skill, not a standing claim on the first key to fall vacant.
        autoBound_ |= bit;
        bool already = false;
        for (int key = 0; key < Hud::kSkillBoxes; ++key) already |= bound_[key] == row.number;
        if (already) continue;
        // A primary goes to the right button, which is where it is used: the wizard's Energy
        // Ball is on his right-click from the day he is made, as it is in MU. Everything else
        // takes the first free key, and the right slot is the player's to fill by dragging.
        if (row.primary() && bound_[Hud::kRightSlot] == 0) {
            bound_[Hud::kRightSlot] = row.number;
            core::logf("window: %s bound to %s", row.name, keyName(Hud::kRightSlot));
            continue;
        }
        for (int key = 0; key < Hud::kSkillKeys; ++key) {
            if (bound_[key] != 0) continue;
            bound_[key] = row.number;
            core::logf("window: %s bound to %s", row.name, keyName(key));
            break;
        }
    }

    // ---- the fan: the list of learned skills, and the drag that fills a key ------------------
    //
    // **A horizontal list above the plate, opened from the gold box.** The user's own words,
    // 2026-09-23 -- *"it was a horizontal list above the HUD, when clicked or hovered on the
    // right-click slot"* -- which is MU2's `client/core/Fan.cs` and `CNewUISkillList` under it.
    // The gold box is the one a right-click casts from in MU, so it is the box the list belongs
    // to; the cells are laid out from it outward, alternating right and left.
    //
    // Opened BOTH ways, because the user remembered both: resting on the gold box opens it while
    // the pointer is there, and a click latches it open so a drag has time to start. MU2 latches
    // only (`Fan.Toggle` off `Hud.HeldToggled`); the hover is this bench's and it is what makes
    // the gesture one movement instead of two.
    const int overBox = hud_.boxAt(pointer.x, pointer.y);
    const bool onGold = overBox == Hud::kGoldBox;
    if (pointer.pressed && onGold) {
        fanLatched_ = !fanLatched_;
        play.ui(Play::Ui::Click);
    }
    // What is in it: everything he has learned, in the table's own order. Asked of the realm
    // every frame rather than kept -- learning is what puts a skill here, and a list that cached
    // them would miss the orb the day it exists. MU2's `Fan.Held` keeps the same rule.
    // A hand for each: the blade an attack needs, the shield the guard needs -- the same two
    // questions `Realm::throwSkill` asks, asked here so a row the realm would refuse is drawn
    // cold in the list exactly as it is on the key.
    const content::Arm* weapon = hero.weapon >= 0 && size_t(hero.weapon) < tables.arms.size()
                                     ? &tables.arms[size_t(hero.weapon)]
                                     : nullptr;
    const content::Arm* shield = hero.shield >= 0 && size_t(hero.shield) < tables.arms.size()
                                     ? &tables.arms[size_t(hero.shield)]
                                     : nullptr;
    // Which family is in each hand, by the realm's own call: a skill is thrown with its kind of
    // weapon or not at all (docs/skills-dk.md §3.1b), so the plate asks `row.suits()` exactly as
    // `Realm::throwSkill` does and cannot hold a different opinion about a dark key.
    const uint32_t inHand = sim::familyOf(weapon);
    const uint32_t onArm = sim::armFamily(hero, shield);
    const auto armedFor = [&](const sim::SkillRow& row) {
        return row.suits(row.onSelf() ? onArm : inHand);
    };
    // **Every refusal the realm would make before the key is even pressed**, asked in
    // `throwSkill`'s own order so that a box drawn cold and a press that does nothing can never
    // disagree. The user, 2026-09-23: the slots need their inactive state when he cannot
    // actually use the skill. The cooldown is NOT one of these -- it has the sweep, and a
    // cooling key must not read as a broken one.
    //
    // **And the card says none of it in words**, on the user's two rules of 2026-09-23. The
    // sentence under the numbers is gone, both halves of it:
    //   * the safe zone, because *"it's obvious"* -- a player standing in Lorencia's square can
    //     see where he is standing.
    //   * the weapon, because the card already carries it. `Weapon` is the first row and it goes
    //     red when the hand is wrong, so a sentence saying the same thing at the foot was the
    //     same fact printed twice.
    // The refusals themselves are unchanged: the keys still go cold for both, which is `ready`
    // below, and a cold key with a red row above it needs no third telling.
    const bool inTown = tables.grid.safe(hero.column(), hero.row());
    const auto ready = [&](const sim::SkillRow& row) {
        // Blood Castle's court is the one safe ground an aura is raised on (Realm::throwSkill).
        const bool court = row.aura() && tables.map == sim::kBloodCastleMap;
        return hero.alive() && (!inTown || court) && armedFor(row) && hero.mana >= row.mana &&
               (!row.mounted || hero.riding);
    };

    fan_.clear();
    for (int i = 0; i < sim::skillCount(); ++i) {
        const sim::SkillRow& row = sim::skillAt(i);
        if (!realm.knows(row.number)) continue;
        Hud::FanCell cell;
        cell.number = row.number;
        cell.name = row.name;
        cell.mana = row.mana;
        for (int key = 0; key < Hud::kSkillBoxes; ++key) {
            if (bound_[key] == row.number) cell.key = key;
        }
        fan_.push_back(cell);
    }
    const bool fanOpen = !fan_.empty() &&
                         (fanLatched_ || onGold || hud_.nearFan(pointer.x, pointer.y) ||
                          carrying_ != 0);
    hud_.setFan(fanOpen, fan_, carrying_);
    // And the card for the entry under the pointer: the same card the keys raise, because it is
    // the same skill. Built for one entry and not six, as the bar's is.
    const int overCell = hud_.fanAt(pointer.x, pointer.y);
    if (overCell >= 0 && size_t(overCell) < fan_.size()) {
        if (const sim::SkillRow* row = sim::skillNumbered(fan_[size_t(overCell)].number)) {
            hud_.setFanSheet(skillSheet(*row, realm));
        }
    }

    // A press picks something up: off a cell of the list, or off a key that already holds one.
    // Which it turns out to be is decided on release, exactly as MU2's `Fan.Release` decides it.
    if (pointer.pressed && carrying_ == 0) {
        const int cell = hud_.fanAt(pointer.x, pointer.y);
        const int key = hud_.skillAt(pointer.x, pointer.y);
        if (cell >= 0 && size_t(cell) < fan_.size()) {
            carrying_ = fan_[size_t(cell)].number;
            carryFrom_ = -1;
        } else if (key >= 0 && bound_[key] != 0) {
            carrying_ = bound_[key];
            carryFrom_ = key;
        }
    }

    if (carrying_ != 0 && pointer.released) {
        const int onto = hud_.skillSlotAt(pointer.x, pointer.y);
        const int32_t carried = carrying_;
        if (onto >= 0) {
            // On a key: bound. A key already holding something is a SWAP and not an overwrite,
            // which is the one rule that makes a full bar rearrangeable without an empty key to
            // stage through -- what was there goes back where the drag started, and a skill
            // dragged out of the list onto a second key MOVES rather than doubling.
            const int32_t displaced = bound_[onto];
            int other = -1;
            for (int key = 0; key < Hud::kSkillBoxes; ++key) {
                if (key != onto && bound_[key] == carried) other = key;
            }
            const bool moved = displaced != carried;
            bound_[onto] = carried;
            if (carryFrom_ >= 0 && carryFrom_ != onto) {
                bound_[carryFrom_] = displaced;
            } else if (other >= 0) {
                bound_[other] = displaced;
            }
            if (moved) {
                core::logf("window: %s on %s", sim::skillNumbered(carried)->name, keyName(onto));
                play.ui(Play::Ui::Took);
                // And the list shuts behind it, as MU2's does: the choice is made.
                fanLatched_ = false;
            }
        } else if (carryFrom_ >= 0) {
            // **Dragged off the bar is taken off the bar**, wherever it lands -- the list, the
            // frame, the grass. The user, 2026-09-23: *"I can't drag a skill out of a
            // quickslot."* It used to clear the key only when the drop landed back on the
            // list, which is tidy and is not what a hand expects: every game that lets you
            // arrange a bar by dragging lets you clear a slot by dragging off it, and nothing
            // is lost either way, since the skill is learned and the list holds every learned
            // skill. A plain CLICK on a key is not this: it lands on the key it came from and
            // falls into the branch above, which leaves the bar as it was.
            bound_[carryFrom_] = 0;
            core::logf("window: %s taken off the bar", sim::skillNumbered(carried)->name);
            play.ui(Play::Ui::Took);
        }
        // A cell dragged out of the LIST and let go on nothing is simply a drag abandoned,
        // which is MU2's third landing.
        carrying_ = 0;
        carryFrom_ = -1;
    }

    // What is standing on him, off the realm: the sim owns the boon and the strip draws it.
    // The guard first and the Ale after it, whichever stands.
    Hud::Boon boons[Hud::kBoons];
    int standing = 0;
    // His pet first and his mount after it, which stand as long as they have life: the Guardian
    // Angel or the Imp in slot 8, Uniria or Dinorant in the mount's, each its Life for the bar
    // under it.
    for (int slot : {sim::kPet, sim::kMount}) {
        const sim::Held& pet = realm.satchel()[slot];
        if (pet.empty() || !realm.tables() || size_t(pet.item) >= realm.tables()->items.size()) {
            continue;
        }
        const content::ItemRow& row = realm.tables()->items[size_t(pet.item)];
        const int most = sim::maximumDurability(row, pet);
        if (row.group == sim::kGroupPets && pet.durability > 0 && most > 0) {
            Hud::Boon& boon = boons[standing++];
            boon.pet = row.number;
            boon.life = pet.durability;
            boon.lifeMost = most;
            boon.kinship = hero.excel.kinship;
            boon.idle = sim::petPower(row).mount && !sim::rideMap(realm.tables()->map);
            boon.share = float(pet.durability) / float(most);
        }
    }
    if (hero.boonSkill != 0 && hero.boonUntil > realm.tick()) {
        const sim::SkillRow* row = sim::skillNumbered(hero.boonSkill);
        const float left = float(hero.boonUntil - realm.tick());
        Hud::Boon& boon = boons[standing++];
        boon.skill = hero.boonSkill;
        boon.seconds = left * 0.05f;  // 20 Hz
        boon.share = row && row->boonTicks > 0 ? left / float(row->boonTicks) : 0.0f;
    }
    // The elf's Greater Damage, which stands beside her guard rather than in its place: its own
    // clock (Body::mightUntil) and its own cell.
    if (hero.mightUntil > realm.tick() && standing < Hud::kBoons) {
        const sim::SkillRow* row = sim::skillNumbered(sim::skill::kGreaterDamage);
        const float left = float(hero.mightUntil - realm.tick());
        Hud::Boon& boon = boons[standing++];
        boon.skill = sim::skill::kGreaterDamage;
        boon.seconds = left * 0.05f;
        boon.share = row && row->mightTicks > 0 ? left / float(row->mightTicks) : 0.0f;
    }
    if (const int64_t left = realm.aleLeft(); left > 0) {
        Hud::Boon& boon = boons[standing++];
        boon.ale = true;
        boon.seconds = float(left) * 0.05f;
        boon.share = float(left) / float(sim::kAleTicks);
    }
    if (const int64_t left = realm.frenzyLeft(); left > 0 && standing < Hud::kBoons) {
        Hud::Boon& boon = boons[standing++];
        boon.frenzy = true;
        boon.stacks = hero.frenzyStacks;
        boon.seconds = float(left) * 0.05f;
        boon.share = float(left) / float(sim::kFrenzyTicks);
    }
    // A potion still pouring in, health then mana: a cell while its instalments are due.
    for (int mana = 0; mana < 2; ++mana) {
        const sim::Realm::Pouring pour = realm.pouring(mana == 1);
        if (pour.left <= 0 || standing >= Hud::kBoons) continue;
        Hud::Boon& boon = boons[standing++];
        boon.potion = mana;
        boon.amount = pour.amount;
        boon.seconds = float(pour.left) * 0.05f;
        boon.share = std::min(1.0f, float(pour.left) / float(sim::Realm::kPourTicks));
    }
    // And a poison on him, last: MU's debuff cell, and the reason to drink an Antidote.
    if (hero.poisonUntil > realm.tick() && standing < Hud::kBoons) {
        Hud::Boon& boon = boons[standing++];
        boon.poison = true;
        const float left = float(hero.poisonUntil - realm.tick());
        boon.seconds = left * 0.05f;
        boon.share = left / float(sim::kHeroPoisonTicks);
    }
    // And iced, the Ice Monster's slow: a debuff cell of its own, its seconds counting down.
    if (hero.chilledUntil > realm.tick() && standing < Hud::kBoons) {
        Hud::Boon& boon = boons[standing++];
        boon.chill = true;
        const float left = float(hero.chilledUntil - realm.tick());
        boon.seconds = left * 0.05f;
        boon.share = left / float(sim::kHeroChillTicks);
    }
    hud_.setBoons(boons, standing);

    const gfx::Window::Key keys[Hud::kSkillKeys] = {
        gfx::Window::Key::Skill1, gfx::Window::Key::Skill2, gfx::Window::Key::Skill3,
        gfx::Window::Key::Skill4, gfx::Window::Key::Skill5};
    for (int key = 0; key < Hud::kSkillKeys; ++key) {
        // **Held, a spell with no cooldown goes on**: the key asks again every frame it is down,
        // and the realm throws it each time he is free (the user, 2026-09-28: "if I hold W and
        // there is no cooldown it has to continue"). A key with a cooldown is a press, as it was.
        const sim::SkillRow* held = sim::skillNumbered(bound_[key]);
        // Not while he is still in a cast: a held key renewed its wish all through Lightning's
        // channel, and the last renewal threw one more after the key was let go (2026-09-30).
        // A fresh press still queues its one throw, as ever.
        const bool again = window.down(keys[key]) && held != nullptr && held->primary() &&
                           !play.realm().casting();
        if (!window.pressed(keys[key]) && !again && scriptedSkill_ != key) continue;
        if (bound_[key] == 0) continue;
        // Aimed at what the pointer is over when it is over something, else at nothing -- the
        // realm falls back to whatever the standing order is fighting, which is the usual case:
        // the knight is already swinging at it.
        play.castSkill(bound_[key], play.pointedAt());
    }
    scriptedSkill_ = -1;
    // What a right-click on a monster throws: the realm is asked with it on the Attack order.
    play.setQuickSkill(bound_[Hud::kRightSlot]);
    // The box rings on the realm's throw, not on the key: a press held until he is in reach
    // rings when he swings, and one refused rings never.
    if (const int32_t cast = play.heroCast()) {
        lastThrown_ = cast;
        for (int key = 0; key < Hud::kSkillBoxes; ++key) {
            if (bound_[key] != cast) continue;
            // Not for a primary: it is thrown twice a second, and a ring on every one is a box
            // that never stops flashing.
            const sim::SkillRow* row = sim::skillNumbered(cast);
            if (row != nullptr && row->primary()) continue;
            hud_.strikeSkill(key);
            core::logf("window: %s thrown off %s", sim::skillNumbered(cast)->name, keyName(key));
        }
    }

    // The card, for the one box the pointer is resting on. Built here and not in the frame,
    // because every number on it is the realm's -- and built for one box, because four cards a
    // frame is four sheets of strings nobody reads.
    const int over = hud_.skillAt(pointer.x, pointer.y);

    for (int key = 0; key < Hud::kSkillBoxes; ++key) {
        Hud::Skill box;
        box.number = bound_[key];
        if (box.number != 0) {
            box.icon = "skill_" + std::to_string(box.number);
            const int64_t left = realm.cooling(box.number);
            const int32_t whole = realm.coolsFor(box.number);
            box.cooling = whole > 0 ? float(left) / float(whole) : 0.0f;
            box.seconds = float(left) * 0.05f;  // 20 Hz
            const sim::SkillRow* row = sim::skillNumbered(box.number);
            // **A spell with no cooldown still has a wait**: the cast he is in, or a swing or a
            // channel still running (`swingsAt`), before it can be thrown again. Wiped over its
            // box like a cooldown, measured against its own clip (`coolsFor` is the clip for a
            // primary), so the box comes back -- and rings -- as he is free to cast it (the user,
            // 2026-09-28: "show the spell reset animation also for spells which don't have
            // cooldowns"). The figure never shows: the clip is under a second.
            // Only on the box of the spell he threw: the gate is the hero's and not the spell's,
            // and worn by every primary it wiped and flashed the whole bar on one cast (the
            // user, 2026-09-30: "dont flash all spells only which one is casted").
            if (row != nullptr && row->primary() && box.number == lastThrown_) {
                const int64_t gate = std::max<int64_t>(0, hero.swingsAt - realm.tick());
                if (gate > 0 && whole > 0) {
                    box.cooling = std::min(1.0f, float(gate) / float(whole));
                    box.seconds = float(gate) * 0.05f;
                }
            }
            // Dimmed for either reason he cannot throw it: the mana is not there, or there is no
            // blade in his hand (Realm::throwSkill refuses both). MU dims a hotkey it will not
            // honour and says nothing else, and the plate decides nothing here -- it asks the
            // same two questions the realm will ask.
            // The same question the list's cells ask, and the realm's own: every refusal it
            // would make before the key is pressed, so the box's cold state and the press that
            // does nothing cannot disagree.
            box.affordable = row == nullptr || ready(*row);

            if (key == over && row != nullptr) {
                hud_.setSkillSheet(key, skillSheet(*row, realm));
            }
        }
        // Back: off its cooldown with the mana for it, on a key it was already on. The second
        // half matters as much as the first -- a skill that cooled while he was dry comes back
        // when the potion does, and that is the moment he can throw it.
        const bool throwable = box.number != 0 && box.cooling <= 0.0f && box.affordable;
        // And a wait that ran straight into the next: cast again on the tick he was free, the box
        // is never seen ready for a whole frame, so a wipe that was nearly done and has started
        // over is the moment it came back.
        const bool rewound = box.number != 0 && readyFor_[key] == box.number &&
                             lastCooling_[key] > 0.0f && lastCooling_[key] < 0.35f &&
                             box.cooling > lastCooling_[key] + 0.3f;
        lastCooling_[key] = box.cooling;
        if (rewound || (throwable && !wasReady_[key] && readyFor_[key] == box.number)) {
            hud_.readySkill(key);
            core::logf("window: %s back on %s", sim::skillNumbered(box.number)->name,
                       keyName(key));
        }
        readyFor_[key] = box.number;
        wasReady_[key] = throwable;
        hud_.setSkill(key, box);
    }
}

// What one skill's card says. The order is the order a player asks the questions in: what is
// this, what does it do, what does it hit for, what does it cost me, and -- if the key is dark --
// why. Every number is read off the realm and off `sim/skills.h`'s own formulas, so the card and
// the blow can never disagree: `force()` is what the damage multiplies by and `coolsFor()` is
// what the cooldown will be set to, the same calls `Realm::throwSkill` makes.
tip::Sheet Desk::skillSheet(const sim::SkillRow& row, const sim::Realm& realm) const {
    const sim::Body& hero = realm.hero();
    tip::Sheet sheet;
    sheet.name = row.name;
    sheet.nameTone = tip::Tone::Blue;
    // Narrower than an item's card. An item wraps lore and a column of options; a skill has one
    // sentence and three numbers, and the item's width left most of the card empty.
    sheet.wide = 232.0f;

    const auto number = [](float value, int places) {
        char text[32];
        std::snprintf(text, sizeof(text), places == 1 ? "%.1f" : "%.2f", double(value));
        return std::string(text);
    };
    const auto line = [](const std::string& label, const std::string& value, tip::Tone tone) {
        tip::Row one;
        one.label = label;
        one.values.push_back({value, tone, false, "", 0});
        return one;
    };

    // What it does, in the row's own line.
    if (row.tells[0] != '\0') {
        tip::Section what;
        tip::Row prose;
        prose.free = row.tells;
        prose.freeTone = tip::Tone::Gray;
        what.rows.push_back(prose);
        sheet.sections.push_back(what);
    }

    // And three numbers, which is the whole of the card.
    //
    // **Only the essentials, on the user's word of 2026-09-23.** What went: the class line, the
    // reach (always his own), the section headings and their marks, and both derivations -- the
    // "x2.00 base, +0.08 from strength" and the "4.0 s base, -6% from agility". The derivations
    // were the teaching bit and they are the first thing to go all the same: what a player acts on
    // is the multiplier his blow HAS and the wait he actually faces, and both of those already
    // carry the stat inside them. The formulas live in docs/skills-dk.md, where they are read
    // once, rather than on a card read fifty times a fight.
    tip::Section facts;
    const int64_t left = realm.cooling(row.number);
    const float seconds = float(realm.coolsFor(row.number)) * 0.05f;

    // **What it is thrown with, and it is the first row on the card.** Added 2026-09-23 with the
    // families themselves (docs/skills-dk.md §3.1b): once a skill can only be thrown with its own
    // kind of weapon, that is the first thing a player needs off the card -- ahead of what it
    // hits for, because it decides whether he can throw it at all and because it is what a
    // weapon in the bag is now judged against.
    //
    // **In MU's own colours for a requirement**: white where the hand meets it, red where it does
    // not, which is exactly what `describe.cpp` does with an item's strength and agility. So a
    // card read with the wrong weapon in hand shows a red line naming the right one, and the
    // sentence under the numbers says it again in words.
    const content::Tables* tables = realm.tables();
    const auto armIn = [&](int32_t at) -> const content::Arm* {
        return tables && at >= 0 && size_t(at) < tables->arms.size() ? &tables->arms[size_t(at)]
                                                                    : nullptr;
    };
    const uint32_t hand = row.onSelf() ? sim::armFamily(hero, armIn(hero.shield))
                                       : sim::familyOf(armIn(hero.weapon));
    // **The families on one line, with commas** (`familiesListed`), the user's of 2026-09-29 --
    // the same line the orb's card prints. They were stacked a line each (2026-09-23) when the
    // words were long: "Two-handed swords and axes" as one value walked over the label. The
    // short words ("2-hand swords") keep the longest clear of it.
    tip::Row weapon;
    weapon.label = "Weapon";
    const tip::Tone met = row.suits(hand) ? tip::Tone::White : tip::Tone::Red;
    weapon.values.push_back({sim::familiesListed(row.families), met, false, "", 0});
    // A spell asks nothing of the hand, so it has no such row. Nor does a mount's skill, which
    // asks for the mount under him instead: white while he rides it, red off it.
    if (!row.wizardry && !row.mounted) facts.rows.push_back(weapon);
    if (row.mounted) {
        facts.rows.push_back(line("Mount", "Horn of Dinorant",
                                  hero.riding ? tip::Tone::White : tip::Tone::Red));
    }
    if (row.wizardry) {
        // The band in his hands and whom it strikes, in the words its scroll's card uses
        // (`spellLines`, game/ui/describe.cpp).
        spellLines(row, realm.wearer(), false, facts.rows);
    } else if (row.onSelf()) {
        // What it takes off a blow, as a share, and for how long -- the two questions a guard
        // is asked. It was "x0.50 for 4.0 s", which left the player to do the sum.
        facts.rows.push_back(
            line("Absorbs",
                 sim::absorbed(sim::boonShare(row, hero.points, hero.shieldDefense)) + " of every blow",
                 tip::Tone::Green));
        // And what it is made of, grey and on one line as an attack's sum is: the points off the
        // shield, the main stat and agility, and the cap they climb towards. Each names only
        // what its formula reads (`guardPoints`, `barrierPoints`): a stat on the line that does
        // not move the number is a lie by listing.
        char sum[96];
        if (row.number == sim::skill::kSoulBarrier) {
            std::snprintf(sum, sizeof(sum), "%d shield, %d ene, %d agi (max %d%%)",
                          hero.shieldDefense, hero.points.energy, hero.points.agility,
                          int(sim::kGuardCap * 100.0f + 0.5f));
        } else {
            std::snprintf(sum, sizeof(sum), "%d shield, %d str, %d agi (max %d%%)",
                          hero.shieldDefense, hero.points.strength, hero.points.agility,
                          int(sim::kGuardCap * 100.0f + 0.5f));
        }
        tip::Row how;
        how.free = sum;
        how.freeTone = tip::Tone::Gray;
        facts.rows.push_back(how);
        facts.rows.push_back(line("Lasts", sim::spoken(float(row.boonTicks) * 0.05f),
                                  tip::Tone::White));
    } else {
        facts.rows.push_back(line("Damage", "x" + number(sim::force(row, hero.points), 2) +
                                                " of a swing",
                                  tip::Tone::Yellow));
        // And the sum that made it, on one grey line: the row's own base plus strength over the
        // skill's divisor. Asked for on 2026-09-23 -- the multiplier says what he hits for and
        // this says WHY, which is the whole argument for spending on strength, and it is one
        // line rather than the two labelled rows it was before.
        char sum[64];
        std::snprintf(sum, sizeof(sum), "%.2f + %d str / %d", double(row.force),
                      hero.points.strength,
                      row.forcePerStrength > 0.0f ? int(1.0f / row.forcePerStrength + 0.5f) : 0);
        tip::Row how;
        how.free = sum;
        how.freeTone = tip::Tone::Gray;
        facts.rows.push_back(how);
    }
    // Tenths under a minute, where a tenth is worth reading; minutes over it, where "300.1 s"
    // was Defense's five-minute wait.
    const auto wait = [&](float s) {
        return s >= 60.0f ? sim::spoken(s) : number(s, 1) + " s";
    };
    if (row.primary()) {
        // No cooldown to state: its pace is its clip.
    } else if (left > 0) {
        facts.rows.push_back(line("Ready in", wait(float(left) * 0.05f), tip::Tone::Red));
    } else {
        facts.rows.push_back(line("Cooldown", wait(seconds), tip::Tone::White));
    }
    const bool paid = hero.mana >= row.mana;
    facts.rows.push_back(line("Mana", std::to_string(row.mana), paid ? tip::Tone::Blue
                                                                     : tip::Tone::Red));
    // And the level it was met at, when there is one. On a card this is history rather than a
    // requirement -- a skill he can read about is one he has already learned -- but it is the
    // one line that says why the bar grew a key this level, and it is the number the orb will
    // ask for when the orbs exist (docs/skills-dk.md §3.3).
    if (row.needLevel > 0) {
        facts.rows.push_back(line("Learned at", "level " + std::to_string(row.needLevel),
                                  tip::Tone::Gray));
    }
    sheet.sections.push_back(facts);

    // **And nothing under the numbers.** Every refusal the card used to spell out is already on
    // it: the wrong hand is the red `Weapon` row, the mana is the red figure, the cooldown is the
    // `Ready in` line and the safe zone is the square he is standing in. See `ready` above.
    return sheet;
}

// Whether a label is set bold: the jewels, BuildGroundItemLabelDescriptor's `boldTextItems`
// (ZzzInventory.cpp:6077), of which the Bless, the Soul and the Chaos are the 0.75 rows.
static bool boldOf(const content::Tables& tables, const sim::Lying& one) {
    // The pets share the jewels' drop group, not their bold name.
    if (one.what.empty()) return false;
    const content::ItemRow& row = tables.items[size_t(one.what.item)];
    return row.jewel() && row.group != sim::kGroupPets;
}

// A ground label's colour: the item card's quality (describe's qualityOf, WoW's ladder since
// 2026-09-29), so the two cannot disagree. BuildGroundItemLabelDescriptor's own ladder coloured
// by the plus -- +3 and +4 orange -- which read as legendary. Zen stays MU's gold.
static uint32_t tintOf(const content::Tables& tables, const sim::Lying& one) {
    if (one.what.empty()) return gfx::rgba(1.0f, 0.8f, 0.1f);
    return tip::colourOf(qualityOf(tables.items[size_t(one.what.item)], one.what));
}

void Desk::labelGround(const Play& play, int width, int height) {
    play.dropsOnScreen(viewProj_, width, height, onScreen_);
    bool same = onScreen_.size() == drawnOnScreen_.size();
    for (size_t i = 0; same && i < onScreen_.size(); ++i) {
        same = onScreen_[i].id == drawnOnScreen_[i].id && onScreen_[i].x == drawnOnScreen_[i].x &&
               onScreen_[i].y == drawnOnScreen_[i].y;
    }
    if (same && groundRebuilds_ > 0) return;
    drawnOnScreen_ = onScreen_;
    ++groundRebuilds_;
    ground_.clear();
    plates_.clear();
    const content::Tables& tables = *play.realm().tables();
    const gfx::Face& face = ground_.face();
    // The tooltip's size: MU's labels are its small type, and the two read as one family.
    const float size = 8.0f * panel::unit();
    struct Label {
        uint32_t id;
        std::string name;
        float set;
        uint32_t tint;
        gfx::Box plate;
    };
    std::vector<Label> labels;
    labels.reserve(onScreen_.size());
    for (const Play::OnScreen& at : onScreen_) {
        const sim::Lying* one = nullptr;
        for (const sim::Lying& l : play.realm().lying()) {
            if (l.id == at.id) one = &l;
        }
        if (!one) continue;
        std::string name;
        if (one->what.empty()) {
            name = panel::commas(one->zen) + " Zen";
        } else {
            // As read at its plus, as the card reads it: the Orb of Summoning is six orbs by its
            // plus, and a +1 one on the ground is the Orb of Goblin, whose plus is its name.
            const content::ItemRow& raw = tables.items[size_t(one->what.item)];
            const content::ItemRow row = sim::asRead(raw, one->what.refinement);
            name = one->what.refinement > 0 && !sim::summoningOrb(raw)
                       ? row.label + " +" + std::to_string(one->what.refinement)
                       : row.label;
            // Ours: excellent said on the label, first after the plus, since its purple alone did
            // not say why (the user, 2026-09-29). MU puts "Excellent " before the name instead.
            if (one->what.excellent != 0) name += " +Excellent";
            // BuildGroundItemLabelDescriptor's tail: the option, then the luck, after the plus.
            if (one->what.option > 0) name += " +Option";
            if (one->what.luck) name += " +Luck";
            // And ours last: the socket, named as the options are (the user, 2026-09-28).
            if (const int n = socketsOf(one->what); n > 0) {
                name += n == 1 ? " +Socket" : " +" + std::to_string(n) + " Sockets";
            }
        }
        // RenderGroundItemLabelTexture: the plate is the text's own box, opaque black, and no
        // padding anywhere in it.
        // g_hFontBold for a jewel, which is a point up here, as a tip's bold line is.
        const float set = boldOf(tables, *one) ? size + panel::unit() : size;
        const float w = face.measure(set, name), h = face.height(set);
        labels.push_back({at.id, std::move(name), set, tintOf(tables, *one),
                          {at.x - w * 0.5f, at.y - h, w, h}});
    }
    // **No label over another** (ours, the user's 2026-09-29, as Diablo and WoW stack a pile's
    // names): the lowest on screen keeps its place, and each one above it that would cross a
    // placed plate climbs to just over it, until it crosses none. Each climb clears one plate for
    // good, so a pile of n settles in n climbs at most.
    // Lowest first, but held: last time's order is where it starts, a new drop goes in by its
    // height, and two trade places only when the upper has come down past the lower by a whole
    // plate -- so a turning camera does not make a name jump a row as two cross.
    const auto foot = [&](size_t i) { return labels[i].plate.y + labels[i].plate.h; };
    std::vector<size_t> order;
    order.reserve(labels.size());
    for (uint32_t id : stacked_) {
        for (size_t i = 0; i < labels.size(); ++i) {
            if (labels[i].id == id) order.push_back(i);
        }
    }
    for (size_t i = 0; i < labels.size(); ++i) {
        if (std::find(stacked_.begin(), stacked_.end(), labels[i].id) != stacked_.end()) continue;
        auto at = order.begin();
        while (at != order.end() && foot(*at) >= foot(i)) ++at;
        order.insert(at, i);
    }
    const float hold = face.height(size);
    for (bool swapped = true; swapped;) {
        swapped = false;
        for (size_t k = 1; k < order.size(); ++k) {
            if (foot(order[k]) > foot(order[k - 1]) + hold) {
                std::swap(order[k], order[k - 1]);
                swapped = true;
            }
        }
    }
    stacked_.clear();
    for (size_t i : order) stacked_.push_back(labels[i].id);
    const float gap = panel::unit();
    const auto crosses = [gap](const gfx::Box& a, const gfx::Box& b) {
        return a.x < b.x + b.w + gap && b.x < a.x + a.w + gap && a.y < b.y + b.h + gap &&
               b.y < a.y + a.h + gap;
    };
    std::vector<size_t> placed;
    placed.reserve(order.size());
    for (size_t i : order) {
        gfx::Box& plate = labels[i].plate;
        for (bool moved = true; moved;) {
            moved = false;
            for (size_t p : placed) {
                // Only ever up: just over p sits on crosses' own edge, and float rounding can
                // still call it crossing -- set to the same y forever, it froze the game on a
                // pile of drops (2026-10-04, crashes/hang-2026-10-04_22-07-25.txt).
                const float over = labels[p].plate.y - plate.h - gap;
                if (crosses(plate, labels[p].plate) && over < plate.y) {
                    plate.y = over;
                    moved = true;
                }
            }
        }
        placed.push_back(i);
    }
    for (const Label& l : labels) {
        ground_.rect(l.plate, gfx::rgba(0.0f, 0.0f, 0.0f, 1.0f));
        plates_.push_back({l.id, l.plate});
        ground_.text(l.plate.x, l.plate.y + face.ascent(l.set), l.set, l.tint, l.name);
    }
}

uint32_t Desk::labelUnder(float x, float y) const {
    for (auto one = plates_.rbegin(); one != plates_.rend(); ++one) {
        if (one->box.has(x, y)) return one->id;
    }
    return 0;
}

void Desk::overhead(float seconds, const Play& play, const float* viewProj, int width,
                    int height) {
    if (!play.isOpen()) {
        vitals_.dismiss();
        tally_.dismiss();
        speech_.dismiss();
        beacon_.dismiss();
        return;
    }
    speech_.update(play, viewProj, width, height);
    // Blood Castle's Statue of Saint is the run's target and not one of its monsters: its bar
    // stands over it while it stands within sight of him, pointed at or not (the user,
    // 2026-10-03: 'statue of spirit is not a monsters, its static which char need to destroy
    // that means it has hp bar and nunber').
    uint32_t shown = play.pointedAt();
    if (shown == 0 && play.realm().tables() &&
        play.realm().tables()->map == sim::kBloodCastleMap) {
        const sim::Realm& realm = play.realm();
        const sim::Body& hero = realm.hero();
        for (const sim::Body& one : realm.bodies()) {
            if (!one.monster() || !one.alive()) continue;
            if (!sim::castleStatue(realm.tables()->kinds[size_t(one.kind)].number)) continue;
            const float dx = one.x - hero.x, dy = one.y - hero.y;
            if (dx * dx + dy * dy <= 12.0f * 12.0f) shown = one.id;
        }
    }
    vitals_.update(seconds, play, shown, play.pointedFolk(), takesPointer_, viewProj, width,
                   height);
    // After the names, so the marker rises by this frame's fade and not the last one's.
    beacon_.update(seconds, play, vitals_.namedFolk(), vitals_.folkShown(vitals_.namedFolk()),
                   viewProj, width, height);
    // The quest on screen, out of the way of a window on the right and of the giver's own.
    tracker_.update(seconds, play,
                    inventoryOpen_ || characterOpen_ || trading_ || banking_ || mixing_ ||
                        questDialog_.up(),
                    viewProj, width, height);
    // The blows' own figures and the gain lane, on the same frame's camera: the figures hang
    // on world points and the lane on the HUD's top edge.
    tally_.update(seconds, play, viewProj, width, height, hud_.plateTop());
}

void Desk::photograph(gfx::Renderer& renderer, double seconds) {
    // The pictures are drawn at the scale the windows are drawn at, so nothing is resampled.
    if (!models_ || !models_->tables() || !renderer.openStages(shaderDir_)) return;
    const float pixelsPerUnit = panel::scale();
    if (inventoryOpen_) bagStagePicture_.render(renderer, pixelsPerUnit, seconds);
    if (trading_ || banking_ || mixing_) {
        shelfStagePicture_.render(renderer, pixelsPerUnit, seconds);
    }
    // The quest giver's rewards on the same stage, which is free whenever he is (no counter).
    else if (questDialog_.up()) {
        shelfStagePicture_.render(renderer, questDialog_.pixelsPerUnit(), seconds);
    }
    // The potion boxes are always on screen, and at rest their stage costs nothing.
    quickStagePicture_.render(renderer, hud_.pixelsPerUnit(), seconds);
    // The tooltip's picture, at the tip's own scale -- the interface pixel, not the windows'.
    tipStagePicture_.render(renderer, panel::unit(), seconds);
}

void Desk::submit(bgfx::ViewId view, int width, int height) {
    interface_.begin(width, height);
    interface_.add(ground_);
    // Over the world's labels and under every window: it is a reading lying on the scene.
    // The quest marker under the names and bars, which are read at the moment of pointing.
    if (beacon_.showing()) interface_.add(beacon_.canvas());
    if (vitals_.showing()) interface_.add(vitals_.canvas());
    // What the guards are saying, over the bars and names and under every window.
    if (speech_.showing()) interface_.add(speech_.canvas());
    // The blows' figures over the bar, because a number is the thing being read at that
    // instant and the bar is the state behind it -- and still under every window.
    if (tally_.showing()) interface_.add(tally_.canvas());
    // The map's name, a reading on the scene as well, and under every window.
    if (arrival_.showing()) interface_.add(arrival_.canvas());
    if (tracker_.showing()) interface_.add(tracker_.canvas());
    if (tracker_.announcing()) interface_.add(tracker_.banner());
    // Go Back! over the HUD's middle: chrome, under every window.
    if (goBack_.showing()) interface_.add(goBack_.canvas());
    // The herald at the top centre: a reading, under every window.
    if (herald_.showing()) interface_.add(herald_.canvas());
    // Chrome like the plate, under every window that might open over its corner.
    if (minimap_.showing()) interface_.add(minimap_.canvas());
    if (travel_.up()) interface_.add(travel_.canvas());
    interface_.add(hud_.canvas());
    if (characterOpen_) interface_.add(card_.canvas());
    if (trading_) interface_.add(shelf_.canvas());
    // The window something is dragged out of goes over the other, so what rides the pointer is
    // never under a window it is carried across -- a vault piece over the bag hid behind it.
    const bool vaultOnTop = banking_ && chest_.dragging();
    if (banking_ && !vaultOnTop) interface_.add(chest_.canvas());
    const bool machineOnTop = mixing_ && mixer_.dragging();
    if (mixing_ && !machineOnTop) interface_.add(mixer_.canvas());
    if (inventoryOpen_) interface_.add(bag_.canvas());
    if (vaultOnTop) interface_.add(chest_.canvas());
    if (machineOnTop) interface_.add(mixer_.canvas());
    // The worn-gear warning hangs left of the windows and so never lies under one; over them,
    // because its hover line is the one part of it that can reach one.
    if (endurance_.showing()) interface_.add(endurance_.canvas());
    // The tips over every window, and under the pointer. Whichever is hovered, it is the one
    // thing on the panel the player is reading at that moment.
    interface_.add(hud_.tipCanvas());
    if (trading_) interface_.add(shelf_.tipCanvas());
    if (banking_) interface_.add(chest_.tipCanvas());
    if (mixing_) interface_.add(mixer_.tipCanvas());
    if (inventoryOpen_) interface_.add(bag_.tipCanvas());
    if (endurance_.showing()) interface_.add(endurance_.tipCanvas());
    // Last of all, over every window too: MU2's own CanvasLayer{Layer=128} -- a pointer is over
    // whatever it is pointing at, and the panel is something you point at as well.
    if (questDialog_.up()) {
        interface_.add(questDialog_.canvas());
        interface_.add(questDialog_.body());
        interface_.add(questDialog_.tipCanvas());
    }
    if (amount_.up()) interface_.add(amount_.canvas());
    // The menu over everything but the pointer: it dims the whole screen, windows and HUD too.
    if (menu_.up()) interface_.add(menu_.canvas());
    if (specimenOpen_) interface_.add(specimen_.canvas());
    interface_.add(cursor_.canvas());
    interface_.submit(view);
}

std::string Desk::line() const {
    char text[160];
    std::snprintf(text, sizeof text, "windows: %u draws, %u vertices, rebuilt hud %llu card %llu bag %llu vitals %llu "
                  "tally %llu",
                  interface_.draws(), interface_.vertices(),
                  (unsigned long long)hud_.rebuilds(), (unsigned long long)card_.rebuilds(),
                  (unsigned long long)bag_.rebuilds(),
                  (unsigned long long)vitals_.rebuilds(),
                  (unsigned long long)tally_.rebuilds());
    return text;
}

}  // namespace mu::game
