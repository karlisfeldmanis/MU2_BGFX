#include "game/ui/desk.h"

#include "sim/realm_tuning.h"

#include <ctime>
#include <cstdio>

#include "core/log.h"
#include "game/ui/controls.h"
#include "game/ui/describe.h"
#include "game/ui/tip.h"
#include "game/quest_auto.h"
#include "game/world/maps.h"
#include "game/world/world_show.h"
#include "sim/maps.h"
#include "sim/quests.h"

namespace mu::game {

// Where he stands as a player reads it: the named place, or the world's own name where it has
// none (placeName gives back the world's key, "lorencia", there).
static std::string shownPlace(const std::string& world, int column, int row) {
    const std::string place = placeName(world, column, row);
    return place == world ? worldShow(world).name : place;
}

// A carried thing's footprint in cells, one by one for nothing: what a window it is let go over
// needs to put its top-left half of it back of the pointer (panel::landing).
static void footprint(const Play& play, const sim::Held& what, int* width, int* height) {
    *width = *height = 1;
    const content::Tables* tables = play.realm().tables();
    if (!tables || what.empty() || size_t(what.item) >= tables->items.size()) return;
    *width = tables->items[size_t(what.item)].width;
    *height = tables->items[size_t(what.item)].height;
}

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

// The worn slot a right-click puts a carried thing in, or -1 for a thing that is not worn: where
// placeOf puts it, or the second ring's slot or a knight's left hand when that is the one empty.
// Whatever stands in the slot comes down in its place (sim::move).
static int wornSlotFor(const Play& play, const sim::Held& what) {
    const content::Tables* tables = play.realm().tables();
    if (!tables || what.empty() || size_t(what.item) >= tables->items.size()) return -1;
    const content::ItemRow& row = tables->items[size_t(what.item)];
    const int place = sim::placeOf(row);
    if (place < 0) return -1;
    const sim::Satchel& bag = play.realm().satchel();
    const sim::Kin kin = play.realm().hero().kin;
    for (const int other : {int(sim::kRingLeft), int(sim::kWeaponLeft)}) {
        if (other != place && !bag[place].empty() && bag[other].empty() && sim::placesIn(row, kin, other)) {
            return other;
        }
    }
    return place;
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
    broker_.useTipStage(&tipStagePicture_);
    deal_.useTipStage(&tipStagePicture_);
    mixer_.useTipStage(&tipStagePicture_);
    questDialog_.useTipStage(&tipStagePicture_);
    questLog_.useTipStage(&tipStagePicture_);
    chat_.useTipStage(&tipStagePicture_);
    inspect_.useTipStage(&tipStagePicture_);
    inspect_.open(interface_, &arts_);
    card_.open(interface_, &arts_);
    bag_.open(interface_, &arts_);
    shelf_.open(interface_, &arts_);
    chest_.open(interface_, &arts_);
    broker_.open(interface_);
    guild_.open(interface_, &arts_);
    guildMake_.open(interface_, &arts_);
    deal_.open(interface_, &arts_);
    mixer_.open(interface_, &arts_);
    jeweler_.open(interface_, &arts_);
    amount_.open(interface_, &arts_);
    report_.open(interface_);
    whatsNew_.open(interface_, &arts_);
    questDialog_.open(interface_);
    questLog_.open(interface_, assetDir);
    helperWindow_.open(interface_, &arts_);
    talents_.open(interface_, &arts_);
    tracker_.open(interface_);
    travel_.open(interface_, assetDir, &arts_);
    goBack_.open(interface_);
    lineChip_.open(interface_);
    squareResult_.open(interface_);
    herald_.open(interface_);
    minimap_.open(interface_);
    chat_.open(interface_);
    partyBar_.open(interface_);
    targetFrame_.open(interface_);
    playerMenu_.open(interface_);
    menu_.open(interface_);
    endurance_.open(interface_, &arts_);
    cursor_.open(interface_, &arts_);
    vitals_.open(interface_, &arts_);
    bossBar_.open(interface_);
    speech_.open(interface_);
    beacon_.open(interface_);
    tally_.open(interface_);
    arrival_.open(interface_);
    // The Sanctuary controls' faces and stone, which every window's frame is drawn with.
    controls::open();
    specimen_.open(interface_);
    interface_.adopt(ground_);
    // Baked at 48 as the controls' faces are, larger than a label is ever drawn, and minified.
    if (groundFace_.bake(gfx::fontPath("Amarante-Regular.ttf"), 48.0f, 512, 4, 1, 0)) {
        groundTexture_ = gfx::uploadFace(groundFace_, "ground labels");
        groundFace_.dropPixels();
    } else {
        core::logError("interface: the ground label face did not bake; drops keep the body face");
    }
    return true;
}

void Desk::shutdown() {
    questDialog_.close();
    questLog_.close();
    helperWindow_.close();
    talents_.close();
    tracker_.close();
    travel_.close();
    goBack_.close();
    lineChip_.close();
    squareResult_.close();
    herald_.close();
    minimap_.close();
    specimen_.close();
    controls::close();
    panel::closeTitleFace();
    bagStagePicture_.shutdown();
    shelfStagePicture_.shutdown();
    quickStagePicture_.shutdown();
    tipStagePicture_.shutdown();
    inspectStagePicture_.shutdown();
    inspectFigure_.shutdown();
    for (Portrait& face : classFaces_) face.shutdown();
    arrival_.shutdown();
    tally_.shutdown();
    beacon_.close();
    if (bgfx::isValid(groundTexture_)) bgfx::destroy(groundTexture_);
    groundTexture_ = BGFX_INVALID_HANDLE;
    groundFace_ = gfx::Face{};
    interface_.shutdown();
}

// The first press opens the window, so the Helper is seen before it is used; after that a press
// starts it or stops it, and the window is the minimap button's right click (or Shift+H).
void Desk::helperPressed() {
    if (!helperSettings_.seen) {
        openHelper();
        return;
    }
    helperAsk_ = helping_ ? 0 : 1;
}

void Desk::openHelper() {
    if (!helperSettings_.seen) {
        helperSettings_.seen = true;
        helperChanged_ = true;
    }
    questLog_.hide();
    helperWindow_.show();
    core::logf("window: helper up");
}

// **Talents hides the others** (the user, 2026-10-20): it is 1260 wide and stands over every
// column, so the bag, the character window, the quest log, the Helper and the guild go down while
// it is up, and what went down comes back as it shuts. An inspected player is let go, as a counter
// lets him go.
void Desk::openTalents(Play& play) {
    (void)play;
    hidden_ = {inventoryOpen_, characterOpen_, questLog_.up(), helperWindow_.up(), guild_.up()};
    inventoryOpen_ = characterOpen_ = false;
    questLog_.hide();
    helperWindow_.hide();
    guild_.hide();
    travel_.hide();
    closeInspect();
    fanLatched_ = false;
    talents_.show();
    core::logf("window: talents up");
}

void Desk::closeTalents(bool bringBack) {
    if (!talents_.up()) return;
    talents_.hide();
    if (bringBack) {
        inventoryOpen_ = hidden_.bag;
        characterOpen_ = hidden_.card;
        if (hidden_.quests) questLog_.show(autoPlaying_ >= 0 ? autoPlaying_ : tracker_.following());
        if (hidden_.helper) helperWindow_.show();
        if (hidden_.guild) guild_.show();
    }
    hidden_ = {};
    core::logf("window: talents down%s", bringBack ? "" : ", the others left shut");
}

void Desk::expect(uint32_t ticket, Then then, int key) {
    if (ticket != 0) waiting_.push_back({ticket, then, key});
}

bool Desk::overWindows(float x, float y) const {
    return amount_.up() || report_.up() || whatsNew_.up() || menu_.up() || hud_.covers(x, y) || guild_.covers(x, y) ||
           guildMake_.covers(x, y) || chat_.covers(x, y) || (!minimap_.full() && partyBar_.covers(x, y)) ||
           playerMenu_.covers(x, y) || targetFrame_.covers(x, y) || minimap_.covers(x, y) ||
           questDialog_.covers(x, y) || questLog_.covers(x, y) || helperWindow_.covers(x, y) || travel_.covers(x, y) || goBack_.covers(x, y) ||
           talents_.covers(x, y) ||
           (!watching_ && lineChip_.covers(x, y)) ||
           herald_.covers(x, y) || squareResult_.covers(x, y) ||
           (specimenOpen_ && specimen_.covers(x, y)) ||
           (characterOpen_ && card_.covers(x, y)) || inspect_.covers(x, y) ||
           (inventoryOpen_ && (bag_.covers(x, y) || bag_.dragging())) ||
           (trading_ && shelf_.covers(x, y)) ||
           (banking_ && (chest_.covers(x, y) || chest_.dragging())) ||
           (brokering_ && broker_.covers(x, y)) ||
           (mixing_ && (mixer_.covers(x, y) || mixer_.dragging())) ||
           (jeweling_ && jeweler_.covers(x, y)) ||
           (dealing_ && (deal_.covers(x, y) || deal_.dragging()));
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
    // Asked before any window answers the press, while what the player clicked on is still up.
    const bool pressedOnWindow = pointer.pressed && overWindows(pointer.x, pointer.y);
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
    // C, or the bar's button. A counter -- the shop, the vault, the machine, a trade, the market --
    // stands beside the bag in the first column, and the character window would push the bag into
    // the second, over the counter, where a click meant for the bag picked from the vault (bug
    // report #13). A counter shuts the character window as it opens; while it is up, C is the
    // interface's no.
    const auto toggleCard = [&]() {
        if (!characterOpen_ && (trading_ || banking_ || brokering_ || mixing_ || jeweling_ || dealing_)) {
            refused();
            return;
        }
        // Over Talents, C is the character window alone: Talents goes, and what it hid stays shut.
        closeTalents(false);
        characterOpen_ = !characterOpen_;
        click();
    };
    // V, the bar's button or the bag's X. A counter stands beside the bag and goes with it, as
    // Escape shuts both: left up alone, the shop took clicks the player meant for the world (bug
    // report #15: "Suggestion to be closed together with the inventory to avoid misclicks").
    const auto toggleBag = [&]() {
        closeTalents(false);  // as C does over Talents
        inventoryOpen_ = !inventoryOpen_;
        if (!inventoryOpen_) {
            if (trading_) play.closeTrade();
            if (banking_) play.closeVault();
            if (brokering_) play.closeBroker();
            if (mixing_) play.closeMachine();
            if (jeweling_) play.closeJeweler();
            if (dealing_) play.closeDeal();
        }
        click();
    };
    // Whether the bag slot holds a bound thing, so a trade's no can say why.
    const auto carriesBound = [&](int slot) { return play.isOpen() && slot >= 0 && play.realm().satchel()[slot].bound; };
    // The answers to what this window asked, heard now (Play::answers).
    for (const Play::Answer& one : play.answers()) {
        const auto at = std::find_if(waiting_.begin(), waiting_.end(),
                                     [&](const Waiting& w) { return w.ticket == one.ticket; });
        if (at == waiting_.end()) continue;
        const Then then = at->then;
        const int struck = at->key;
        waiting_.erase(at);
        switch (then) {
            case Then::TookOrRefused: one.ok() ? took() : refused(); break;
            case Then::Bound: if (one.ok()) took(); else { refused(); tally_.bound(); } break;
            case Then::Reset:
                if (one.ok()) {
                    took();
                } else {
                    refused();
                    if (one.value == int32_t(sim::Realm::Reset::NoRoom)) tally_.noRoomToReset();
                }
                break;
            case Then::Refused: if (!one.ok()) refused(); break;
            case Then::Spark: one.ok() ? mixer_.spark() : refused(); break;
            // The unlock's sword cut only for a point that opened something (the plan's "The
            // unlock"): his ranks already hold the answer, so what it opened is read off them.
            case Then::Talent:
                if (!one.ok()) {
                    refused();
                } else if (struck >= 1000 && TalentWindow::unlocks(play.realm().hero().talents, (struck - 1000) / 16,
                                                                   (struck - 1000) % 16, one.value)) {
                    // The whoosh and the cut, timed to the light (TalentWindow::unlocked): the cut
                    // at once on the talent's own first point, else on the whoosh's peak.
                    const int c = (struck - 1000) / 16, n = (struck - 1000) % 16;
                    const bool own = int(play.realm().hero().talents[size_t(c)][size_t(n)]) == one.value;
                    talents_.unlocked(own);
                    core::logf("window: talent opened something: the whoosh, the cut %s", own ? "at once" : "on its peak");
                } else if (struck < 1000) {
                    play.ui(Play::Ui::Soft);  // a point given back: the lighter click
                } else {
                    click();
                }
                break;
            case Then::Jewel:
                jeweler_.answered(one.ok(), one.value, play.realm().refusal());
                if (!one.ok()) refused();
                break;
            case Then::ZenBox:
                if (one.ok()) {
                    amount_.hide();
                    click();
                } else {
                    amount_.refuse();
                    refused();
                }
                break;
            case Then::Strike: if (one.ok()) hud_.strikeQuick(struck); break;
            // The dialog shuts on a yes; Play has heard a no.
            case Then::CloseQuest:
                if (one.ok()) play.closeQuest();
                else questShutting_ = false;
                break;
            case Then::Click: one.ok() ? click() : refused(); break;
            case Then::CloseAngel:
                if (one.ok()) {
                    play.closeAngel();
                    click();
                } else {
                    refused();
                }
                break;
            case Then::Travel:
                if (one.ok()) {
                    travel_.hide();
                    click();
                }
                break;
            case Then::Quiet: break;
        }
    }
    // A play that closed (a map change) answers nothing more.
    if (!play.isOpen()) waiting_.clear();
    // **The number box, before anything else, and it takes everything.** It is modal, as MU's
    // message boxes are: the pointer's presses and the keys are its own while it is up, so the
    // windows under it do not hear a click on its OK and a 1 typed into it is not the first
    // potion drunk. `typing` is taken before the box answers, so the frame Return closes it on
    // does not hand the same keys on either. The cursor is still drawn where the pointer is.
    const bool typing = amount_.up() || report_.up() || whatsNew_.up() || broker_.typing() || guild_.typing() || guildMake_.typing();
    // The chat's keyboard, taken before the box spends the script's: it types only while it is
    // open and no box is up, and its Enter and Escape are then its own.
    const bool chatTyping = chat_.typing() && !typing;
    const std::string typedNow = window.typed() + scriptTyped_;
    const bool enterNow = window.entered() || scriptEnter_;
    const bool escapeNow = window.escaped() || scriptEscape_;
    const Pointer real = pointer;
    // Escape this frame, taken before the box below spends the script's: the box has it first
    // while it is up, and only otherwise is it the menu's. A scripted one always is, so a review
    // run can open the menu; a real one only when the game holds Escape (PlayMode::open).
    // And a quest giver's window, which is answered as the box is and takes Escape before the menu.
    const bool questing = play.isOpen() && (play.realm().questing() >= 0 || journal_ >= 0);
    const bool escape =
        !typing && !chatTyping && !questing && (scriptEscape_ || (holdEscape_ && window.escaped()));
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
        } else if (result.offered) {
            // His Zen in a trade, which replaces the last; the realm refuses more than he carries.
            expect(play.dealZen(result.amount), Then::ZenBox);
        } else if (result.amount > 0 && amount_.purpose() == Amount::Purpose::Split) {
            if (result.amount > amount_.most()) {
                amount_.refuse();
                refused();
            } else {
                if (play.isOpen()) takePart(play, int(result.amount));
                amount_.hide();
                click();
            }
        } else if (result.amount > 0) {
            // CZenReceiptMsgBoxLayout::ProcessOk: sent, clicked and shut -- or, short, said so.
            const bool deposit = amount_.purpose() == Amount::Purpose::Deposit;
            expect(deposit ? play.depositZen(result.amount) : play.withdrawZen(result.amount),
                   Then::ZenBox);
        }
    }
    // The bug report, modal as the box is, with the keys taken before the box spent the script's.
    {
        Report::Result result;
        report_.update(seconds, float(window.width()), float(window.height()), pointer,
                       report_.up() ? typedNow : std::string(), report_.up() ? window.backspaces() : 0,
                       report_.up() && enterNow, report_.up() && escapeNow, &result);
        if (result.clicked) click();
        if (result.cancel) core::logf("window: bug report down");
        if (result.send) {
            reportAsk_ = BugAsk{};
            reportAsk_.text = report_.words();
            reportAsk_.world = worldShow(worldName_).name;
            if (play.isOpen()) {
                const sim::Body& hero = play.realm().hero();
                reportAsk_.world = shownPlace(worldName_, hero.column(), hero.row());
                reportAsk_.x = hero.column();
                reportAsk_.y = hero.row();
                reportAsk_.level = hero.level;
                reportAsk_.kind = sim::className(int(hero.kin), hero.second);
                if (!play.playerName(hero.id, &reportAsk_.character, nullptr)) reportAsk_.character = heroName_;
            }
            reportReady_ = true;
            core::logf("window: bug report sent (%zu characters)", reportAsk_.text.size());
        }
    }
    // What's New, modal as the report is: Escape, Skip or its last Next put it down, and then its
    // edition is seen (PlayMode keeps it in options.txt).
    {
        WhatsNew::Result result;
        whatsNew_.update(seconds, float(window.width()), float(window.height()), pointer,
                         whatsNew_.up() && escapeNow, &result);
        if (result.clicked) click();
        if (result.closed) {
            whatsNewSeen_ = true;
            core::logf("window: what's new down");
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
            // One waiting on his level is never opened, only listed: no page, no voice.
            if (picked_ >= 0 && realm.questUnderLevel(picked_)) picked_ = -1;
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
        // Auto-play answers the giver itself (game/quest_auto.h): his window stays down, and
        // the quest log up.
        if (autoPlaying_ >= 0 && realm.questing() >= 0 && !gating && !angeling) quest = -1;
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
            // A hand-in another takes back is in her words, read in her own voice: Lirien's.
            const bool theirs = page == 2 && sim::questElsewhere(sim::questAt(quest));
            if (theirs) who = sim::questAt(quest).receiverVoice;
            if (who && *who) {
                voiced = std::string("voice/") + who + "/" + who + "_" + kPage[page] + ".wav";
            }
        }
        // The answer turns the page (offer to under way, hand-in to resting) a moment before
        // the close lands: he goes on with what he was saying and starts nothing new (the
        // user, 2026-10-17: 'quest audio cuted for minisecond').
        if (quest < 0) questShutting_ = false;
        if (questShutting_ && !voiced.empty()) voiced = voiced_;
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
            expect(play.handInStaff(), Then::Click);
        } else if (result.claim) {
            expect(play.claimCastle(), Then::CloseAngel);
        } else if (result.enter) {
            expect(result.square ? play.enterSquare(result.castle) : play.enterCastle(result.castle),
                   Then::Click);
        } else if (result.accept) {
            expect(play.acceptQuest(quest), Then::CloseQuest);
            questShutting_ = true;
        } else if (result.complete) {
            expect(play.completeQuest(quest, result.choice, result.path), Then::CloseQuest);
            questShutting_ = true;
        }
    }
    // A Town Portal Scroll read shuts the bag and the character window, silently: ReceiveTeleport's
    // `g_pNewUISystem->HideAll()`. The realm has already closed the counter and the vault. And the
    // same handler ends `if (Data->Flag) g_pUIMapName->ShowMapName()`, which a warp has set even
    // into the map it left, so the town's name comes up at once, as it does on the way in.
    if (play.takeWarp()) {
        closeTalents(false);
        inventoryOpen_ = characterOpen_ = false;
        closeInspect();
        fanLatched_ = false;
        arrival_.announce(hero ? placeName(worldName_, hero->column(), hero->row()) : worldName_,
                          0.0f, play.zoneLevels());
    }
    // **Escape, in MU's order and then the menu.** A box takes it first (above). With any window
    // open it shuts them all, as CNewUIManager's Escape closes the open windows before anything
    // else; only with nothing open does it raise the menu. And up, the menu has it: back a page,
    // or down.
    const bool windowsOpen = inventoryOpen_ || characterOpen_ || trading_ || banking_ || brokering_ || mixing_ || jeweling_ ||
                             dealing_ || travel_.up() || guild_.up() || guildMake_.up() || inspecting() ||
                             questLog_.up() || helperWindow_.up() || talents_.up();
    {
        std::string place = hero ? placeName(worldName_, hero->column(), hero->row()) : worldName_;
        if (hero) {
            place += (place.empty() ? "" : "  \xC2\xB7  ") + std::to_string(hero->column()) +
                     ", " + std::to_string(hero->row());
        }
        // The server's line gone (Play::cut): the game holds and the Disconnected page is up
        // over it, with every other window shut, and stays up -- Escape does not take it down.
        if (play.isOpen() && play.cut() && !menu_.lost()) {
            closeTalents(false);
            inventoryOpen_ = characterOpen_ = false;
            travel_.hide();
            amount_.hide();
            mending_ = false;
            fanLatched_ = false;
            const std::string why = play.cutWhy();
            menu_.lose(why);
            core::logf("window: disconnected -- %s", why.empty() ? "no reason kept" : why.c_str());
        }
        const bool wasUp = menu_.up();
        if (!wasUp && escape) {
            if (mending_) {
                // Repair mode is the first rung: Escape puts the hammer down -- the cursor and
                // the foot's button both -- and leaves the bag or the counter open. The user:
                // 'if player press escape he cancels the repair icon and button state'.
                mending_ = false;
                core::logf("window: repair mode off");
            } else if (playerMenu_.up()) {
                playerMenu_.hide();
            } else if (windowsOpen) {
                closeTalents(false);  // Escape shuts every window, what Talents hid among them
                inventoryOpen_ = characterOpen_ = false;
                closeInspect();
                questLog_.hide();
                helperWindow_.hide();
                if (trading_) play.closeTrade();
                if (banking_) play.closeVault();
                if (brokering_) play.closeBroker();
                if (mixing_) play.closeMachine();
                if (jeweling_) play.closeJeweler();
                if (dealing_) play.closeDeal();
                travel_.hide();
                guild_.hide();
                guildMake_.hide();
                fanLatched_ = false;
            } else if (!play.targetName().empty()) {
                // With nothing open, Escape lets his target go before it raises the menu, as
                // WoW's clears the target first.
                play.untarget();
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
        if (asked.whatsNew) {
            whatsNew_.show();
            core::logf("window: what's new up from the menu");
        }
        if (asked.switched) {
            switchAsked_ = true;
            core::logf("window: switch character asked from the menu");
        }
        if (asked.reconnect && rejoinHere_) {
            rejoinAsked_ = true;
            core::logf("window: reconnect asked: at once, where he stands");
        } else if (asked.reconnect) {
            switchAsked_ = true;
            core::logf("window: reconnect asked: back to the character screen");
        }
    }
    // Modal, as the box is: the menu has the pointer and the keys while it is up, and the frame
    // it went down on hands neither on, so the click that shut it does not walk him.
    const bool menuHeld = menu_.up() || escape;
    if (typing || menuHeld) {
        pointer.pressed = pointer.released = pointer.rightPressed = pointer.held = false;
        pointer.x = pointer.y = -1.0f;
    }
    const bool keysHeld = typing || menuHeld || chatTyping;

    if (!keysHeld && (window.pressed(gfx::Window::Key::Inventory) || scriptBag_)) toggleBag();
    scriptBag_ = false;
    if (!keysHeld && (window.pressed(gfx::Window::Key::Character) || scriptCharacter_)) toggleCard();
    scriptCharacter_ = false;
    // G, the guild window (docs/game/guild.md): on a server; off one there is no guild to show.
    if (!keysHeld && (window.pressed(gfx::Window::Key::Guild) || scriptGuild_) && play.isOpen()) {
        guild_.toggle();
        click();
    }
    scriptGuild_ = false;
    // L, the quest journal: the tracked quest's page -- the one under way, else the one resting,
    // else the first on offer -- and L again shuts it. With the bag up L is the bag's repair.
    // Ours: MU 0.75 has no quests, and its L is only the repair.
    const bool journalAsked = scriptJournal_;
    scriptJournal_ = false;
    // The HUD's quest button asks the same, with the bag up too (Hud's kQuestButton).
    const bool journalPressed = questButton_;
    questButton_ = false;
    if (!keysHeld && play.isOpen() && play.realm().questing() < 0 &&
        (journalPressed || questLogAsked_ ||
         (!inventoryOpen_ && (window.pressed(gfx::Window::Key::Repair) || journalAsked)))) {
        // The quest log (game/ui/quest_log.h), on the quest Auto-play has, else the one the
        // tracker follows -- the quest he is fighting for on this map.
        if (questLog_.up() && !questLogAsked_) {
            questLog_.hide();
        } else {
            helperWindow_.hide();
            questLog_.show(autoPlaying_ >= 0 ? autoPlaying_ : tracker_.following());
            core::logf("window: quest log up");
        }
        questLogAsked_ = false;
        click();
    }
    // The log, while it is up and no giver's window has the left: a row chosen, Auto-play or Stop
    // pressed, the close.
    if (questLog_.up() && play.isOpen()) {
        const sim::Realm& realm = play.realm();
        if ((realm.questing() >= 0 && autoPlaying_ < 0) || realm.gating() >= 0 || realm.angeling() >= 0) {
            questLog_.hide();
        } else {
            QuestLog::Result result;
            const bool shelfBusy = trading_ || banking_ || brokering_ || mixing_ || jeweling_ || dealing_;
            questLog_.update(seconds, play, autoPlaying_, float(window.width()), float(window.height()),
                             typing ? Pointer{} : pointer, typing ? 0.0f : window.scroll(),
                             shelfBusy ? nullptr : shelfStage_, &result);
            if (result.clicked) click();
            if (result.close) {
                questLog_.hide();
                click();
            }
            if (result.autoPlay >= 0) {
                autoAsk_ = result.autoPlay;
                tracker_.pin(result.autoPlay);
                click();
            }
            if (result.stop) {
                autoAsk_ = -1;
                click();
            }
        }
    }

    // H, the Helper (game/helper.h): its window the first time and with Shift, else started or
    // stopped. The window, while it is up: its settings, the auto-drink lines, Start and Stop.
    if (!keysHeld && play.isOpen() && (window.pressed(gfx::Window::Key::Helper) || helperAsked_)) {
        if (helperAsked_ || window.shift()) {
            if (helperWindow_.up() && !helperAsked_) helperWindow_.hide();
            else openHelper();
        } else {
            helperPressed();
        }
        helperAsked_ = false;
        click();
    }
    if (helperWindow_.up() && play.isOpen()) {
        HelperWindow::Result result;
        int32_t bar[Hud::kSkillBoxes];
        for (int k = 0; k < Hud::kSkillBoxes; ++k) bar[k] = bound_[k];
        helperWindow_.update(seconds, play, helperSettings_, bar, helping_, helperStats_, float(window.width()),
                             float(window.height()), typing ? Pointer{} : pointer, &result);
        if (result.clicked) click();
        if (result.changed) helperChanged_ = true;
        if (result.close) helperWindow_.hide();
        if (result.start) helperAsk_ = 1;
        if (result.stop) helperAsk_ = 0;
        if (result.health >= 0 || result.mana >= 0) {
            play.setAutoDrink(result.health, result.mana);
            core::logf("window: auto-drink %d%% health, %d%% mana", result.health, result.mana);
        }
    }

    // A, Talents (game/ui/talent_window.h): MU's own master-tree key since Season 4. Not over a
    // counter, which the realm holds open beside the bag: there A is the interface's no, as C is.
    talents_.watch(seconds, hero);
    for (TalentWindow::Cue cue; talents_.takeSound(&cue);) {
        play.ui(cue == TalentWindow::Cue::Whoosh ? Play::Ui::Whoosh : Play::Ui::Talent);
    }
    {
        const bool asked = scriptTalents_ || (talentsAsked_ && play.isOpen());
        scriptTalents_ = talentsAsked_ = false;
        if (play.isOpen() && ((!keysHeld && window.pressed(gfx::Window::Key::Talents)) || asked)) {
            if (talents_.up()) {
                closeTalents(true);
                click();
            } else if (trading_ || banking_ || brokering_ || mixing_ || jeweling_ || dealing_) {
                refused();
            } else {
                openTalents(play);
                click();
            }
        }
    }
    // A counter opened under it (a merchant spoken to): Talents makes way, and the bag the
    // counter opened stays.
    if (talents_.up() && (trading_ || banking_ || brokering_ || mixing_ || jeweling_ || dealing_)) closeTalents(false);
    if (talents_.up() && play.isOpen()) {
        TalentWindow::Result result;
        talents_.update(play, float(window.width()), float(window.height()), hud_.plateTop(),
                        typing ? Pointer{} : pointer, &result);
        if (result.clicked) click();
        if (result.close) {
            closeTalents(true);
            click();
        }
        // The point asked; its sound is the answer's (Then::Talent), so a refused + never plays
        // the unlock.
        if (result.column >= 0) {
            expect(play.talent(result.take, result.column, result.node, 1), Then::Talent,
                   (result.take ? 1000 : 0) + result.column * 16 + result.node);
        }
        // Reset is free. Its confirming press is silent, and the answer is heard once: MU's
        // SOUND_GET_ITEM01 for a tree given back whole, as Reset points' yes is, else the error.
        if (result.reset) expect(play.resetTalents(), Then::TookOrRefused);
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
        // The press is the list's whatever the answer: it was made on the list.
        if (go >= 0) {
            expect(play.travel(go), Then::Travel);
            travelPressed = true;
        }
    }
    // Go Back!: clicked, it clicks, and the mode takes him back (app/modes/play_mode.cpp), where
    // the warp's own sMagic plays as he lands. Not while a box or the menu has the screen.
    {
        const bool held = amount_.up() || report_.up() || whatsNew_.up() || menu_.up();
        if (goBack_.update(seconds, goBackShown_ && play.isOpen(), goBackLeft_, goBackClosed_,
                           goBackWhere_, held ? Pointer{} : pointer, window.width(),
                           window.height(), hud_.plateTop())) {
            goBackAsked_ = true;
            click();
        }
    }
    // The line's chip in the screen's top right corner, as the old counter was, the minimap under
    // it and its list over the minimap while open. Under a box or the menu it shows and hears
    // nothing new.
    if (!watching_) {
        const bool held = amount_.up() || report_.up() || whatsNew_.up() || menu_.up();
        const float u = tip::unit();
        lineChip_.update(seconds, line_, held ? Pointer{} : pointer, window.width(), window.height(),
                         float(window.width()) - 10.0f * u, 4.0f * u);
        if (lineChip_.takeClicked()) click();
        // The minimap steps down under it, and the tracker under the minimap with it.
        minimap_.setTop(lineChip_.showing() ? lineChip_.chip().bottom() + 6.0f * u : 0.0f);
    }
    // Devil Square's result, from the end of its time until he is paid: the realm's ranks, the
    // names as the server (or the bench) says them, his own his save's. Claim is asked of the
    // realm, which pays and sends him to Noria; its cross shuts it for this run.
    {
        bool shown = false;
        int square = 1, left = 0;
        squareRows_.clear();
        if (play.isOpen()) {
            const sim::Realm& realm = play.realm();
            const sim::SquareRun& run = realm.squareRun();
            if (run.phase != sim::SquarePhase::Ended) {
                squareResult_.reset();
            } else if (!realm.squarePaid()) {
                realm.squareResults(&squareResults_);
                for (const sim::Realm::SquareResult& one : squareResults_) {
                    SquareResult::Row row{one.rank, "", one.points, one.experience, one.zen, one.mine};
                    if (!play.playerName(one.id, &row.name, nullptr)) {
                        row.name = one.mine && !heroName_.empty() ? heroName_ : one.mine ? "You" : "Player";
                    }
                    squareRows_.push_back(std::move(row));
                }
                shown = true;
                square = run.square;
                left = realm.squareSecondsLeft();
            }
        }
        const bool held = amount_.up() || report_.up() || whatsNew_.up() || menu_.up();
        const bool wasUp = squareResult_.up();
        if (squareResult_.update(seconds, shown, square, squareRows_, left, held ? Pointer{} : pointer,
                                 float(window.width()), float(window.height()))) {
            expect(play.claimSquare(), Then::Click);
        }
        if (squareResult_.up() != wasUp) {
            if (squareResult_.up()) play.ui(Play::Ui::Opened);
            else if (squareResult_.dismissed()) click();
        }
    }
    // The herald: the event coming for his level, at its three moments. Its cross clicks.
    if (herald_.update(seconds, play, amount_.up() || report_.up() || whatsNew_.up() || menu_.up() ? Pointer{} : pointer,
                       window.width(), window.height())) {
        click();
    }

    bool toggleInventory = false, toggleCharacter = false, toggleMenu = false;
    bool toggleQuest = false, toggleChat = false, toggleGuild = false, toggleTalents = false;
    hud_.setChatOpen(chat_.typing());
    hud_.setGuildOpen(guild_.up());
    hud_.setTalents(play.isOpen() ? play.realm().talentsInHand() : 0, talents_.up());
    hud_.update(seconds, float(window.width()), float(window.height()), pointer, inventoryOpen_,
                characterOpen_, &toggleInventory, &toggleCharacter, &toggleMenu, questLog_.up(),
                &toggleQuest, &toggleChat, &toggleGuild, &toggleTalents);
    // The HUD's Talents button asks what A does, read by A's own answer on the next frame.
    if (toggleTalents) talentsAsked_ = true;
    // The HUD's guild button asks what G does, before the guild window's own frame below.
    if (toggleGuild && play.isOpen()) {
        guild_.toggle();
        click();
    }
    // The chat: Enter opens it when nothing else has the keyboard, and then it has it.
    if (!watching_ && play.isOpen()) {
        const float u = tip::unit();
        const bool free = !typing && !menuHeld && !questing;
        chat_.avoid(hud_.blocks());
        chat_.update(seconds, float(window.width()), float(window.height()), hud_.plateTop() - 18.0f * u,
                     hud_.buffsLeft() - 16.0f * u, pointer,
                     chat_.over(pointer.x, pointer.y) ? window.scroll() : 0.0f,
                     chatTyping ? typedNow : std::string(), chatTyping ? window.backspaces() : 0,
                     (chatTyping || free) && enterNow, chatTyping && escapeNow, toggleChat, play,
                     chatTyping && window.tabbed(), chatTyping ? window.arrows() : 0);
        // While the Helper helps, what its window says of invites and trades is answered for him
        // (HelperSettings::invites, noTrades): the card shows it answered.
        if (helping_) {
            if (helperSettings_.noTrades) chat_.answerOpen(play, true, false);
            if (helperSettings_.invites != HelperSettings::kInviteAsk) {
                chat_.answerOpen(play, false, helperSettings_.invites == HelperSettings::kInviteJoin);
            }
        }
        if (toggleChat) click();
        // The player menu first: a press it takes (a row, or shutting it) is no one else's.
        menuTook_ = playerMenu_.update(play, pointer);
        Pointer under = pointer;
        if (menuTook_ || playerMenu_.covers(pointer.x, pointer.y) || (travel_.up() && travel_.covers(pointer.x, pointer.y)))
            under.pressed = under.rightPressed = false;
        // The party's faces the next gather takes: whoever of them is on this map, him too.
        {
            faceIds_.clear();
            for (const auto& member : play.party().members) {
                if (member.here != 0) faceIds_.push_back(member.here);
            }
            play.wantFaces(faceIds_);
        }
        partyBar_.update(seconds, play, minimap_.full() ? Pointer{} : under, partyFaces_, partyFacesTaken_);
        targetFrame_.update(seconds, play, under, arts_, float(window.width()), hud_.plateTop() - 26.0f * u,
                            portrait_.picture(), portrait_.taken());
        // A right click on a capsule, on the target frame or on a name in the chat: his menu.
        // Never from the world.
        {
            std::string whom;
            float x = 0.0f, y = 0.0f;
            if (partyBar_.takeMenuAsk(&whom, &x, &y) || targetFrame_.takeMenuAsk(&whom, &x, &y) ||
                chat_.takeMenuAsk(&whom, &x, &y)) {
                playerMenu_.show(play, whom, x, y, float(window.width()), float(window.height()));
            }
        }
        // The menu asked for a whisper: the chat opens on his tab.
        if (const std::string whom = playerMenu_.whisper(); !whom.empty()) chat_.whisperTo(whom);
        // And to inspect him: his window in the character window's column.
        if (const std::string whom = playerMenu_.inspect(); !whom.empty()) openInspect(play, whom);
        // A guild's call: its card in the middle (Chat::guildCalled).
        net::GuildCall call;
        while (play.takeGuildCall(call)) {
            if (chat_.guildCalled(call)) play.ui(Play::Ui::Invited);
        }
    }
    // The guild's windows (game/ui/guild.h): G's, and the Guild Master's when he is spoken to on a
    // server. Each keeps the pointer while a field of it types, as the Broker's does.
    if (play.isOpen()) {
        if (play.takeGuildMaster()) {
            guildMake_.show();
            play.ui(Play::Ui::Opened);
        }
        const std::string said = chat_.guildSaid(6.0f);
        GuildRequests asked;
        GuildKeys keys;
        if (guildMake_.typing()) keys = {typedNow, window.backspaces(), enterNow, escapeNow};
        guildMake_.update(seconds, float(window.width()), float(window.height()), 2, play,
                          guildMake_.typing() && !amount_.up() ? real : pointer, keys, said, &asked);
        keys = {};
        if (guild_.typing()) keys = {typedNow, window.backspaces(), enterNow, escapeNow};
        guild_.update(seconds, float(window.width()), float(window.height()), guildColumn(), play,
                      guild_.typing() && !amount_.up() ? real : pointer, keys, said, &asked);
        for (const net::GuildAsk& one : asked.asks) play.askGuild(one);
        if (!asked.whisper.empty()) chat_.whisperTo(asked.whisper);
        if (!asked.party.empty()) play.askParty({net::PartyWhat::Invite, asked.party});
        if (!asked.trade.empty()) play.dealAsk(play.playerNamed(asked.trade));
        if (asked.clicked) click();
    }
    // Read by the journal's L on the next frame, which runs ahead of the HUD.
    if (toggleQuest) questButton_ = true;
    if (toggleMenu) {
        menu_.show();
        core::logf("window: menu up");
        click();
    }
    if (toggleInventory) toggleBag();
    if (toggleCharacter) toggleCard();

    // The character window, while it is up. What it asks for is answered here, by the realm,
    // and the window sees the answer on its next frame.
    if (characterOpen_) {
        int spend = -1, count = 1;
        bool reset = false, close = false;
        std::string name;
        if (!hero || !play.playerName(hero->id, &name, nullptr)) name = heroName_;
        card_.setGuild(play.guild().name);
        const int page = card_.tab();
        card_.update(float(window.width()), float(window.height()), hero, &play.realm(), name,
                     pointer, &spend, &count, &reset, &close);
        // A tab changing its page clicks, as every button does (the user, 2026-10-16).
        if (card_.tab() != page) click();
        // The stat button clicks whether or not the point lands: CNewUICharacterInfoWindow
        // sends the request and plays SOUND_CLICK01 on the next line without waiting.
        if (spend >= 0) {
            play.spendPoint(spend, count);
            click();
        }
        // The reset, confirmed: a bag that cannot take what he could no longer wear is said in
        // the lane (the user, 2026-10-20: "we need to show error that can reset points because
        // there is no room in inventory").
        if (reset) expect(play.resetPoints(), Then::Reset);
        // The exit button clicks as the C key does. INVENTION, the user's (2026-09-30):
        // CNewUICharacterInfoWindow's m_BtnExit has no PlayBuffer, and a silent X read as broken.
        if (close) {
            characterOpen_ = false;
            click();
        }
    }

    // **The inspect window** (game/ui/inspect.h), while it is up: out of the mirror, so he must be
    // on this map. The character window takes the column back (C), a counter, the vault, the
    // market, the machine or a trade shut it, and so does his leaving.
    if (scriptInspect_ && !inspecting() && hero) {
        for (int i = 0; i < play.realm().playersHere(); ++i) {
            const sim::Body& one = play.realm().playerAt(i);
            std::string his;
            if (one.gone || one.id == hero->id || !inspectable(play.realm(), one.id) ||
                !play.playerName(one.id, &his, nullptr) || his.empty()) {
                continue;
            }
            openInspect(play, his);
            scriptInspect_ = false;
            break;
        }
    }
    if (inspecting()) {
        const uint32_t id = play.playerNamed(inspectName_);
        const sim::Body* who = id != 0 ? play.realm().find(id) : nullptr;
        const sim::Satchel* worn = id != 0 ? play.realm().satchelOf(id) : nullptr;
        if (!who || !worn || !inspectable(play.realm(), id) || characterOpen_ || trading_ || banking_ ||
            brokering_ || mixing_ || dealing_) {
            closeInspect();
        } else {
            inspectId_ = id;
            const net::GuildMark* guild = play.playerGuild(id);
            InspectRequests asked;
            inspect_.update(float(window.width()), float(window.height()), 1, play.realm(), *who, *worn,
                            inspectName_, guild ? guild->name : std::string(),
                            inspectFigure_.taken() ? &inspectFigure_.picture() : nullptr, pointer,
                            &inspectStagePicture_, &asked);
            if (asked.close) {
                click();
                closeInspect();
            }
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
            expect(play.repairAll(), Then::Refused);
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
        if (buy >= 0) expect(play.buy(buy), Then::Refused);
        if (mend.toggle) {
            mending_ = !mending_;
            click();
        }
        if (mend.all) expect(play.repairAll(), Then::Refused);
        if (undo) expect(play.buyBack(), Then::Refused);
        if (close) {
            play.closeTrade();
            click();
        }
    }
    bag_.setMending(mending_);

    // Shift and a press on a stack in the bag, the vault or the box: how many, asked in the number
    // box, and the press heard by nothing else. Not while the chat types, where Shift-click links.
    const bool shiftNow = window.shift() || scriptShift_;
    scriptShift_ = false;
    if (pointer.pressed && shiftNow && !chat_.typing() && splitAsked(play)) {
        pointer.pressed = false;
        click();
    }

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
    if (!banking && amount_.purpose() != Amount::Purpose::Offer && amount_.purpose() != Amount::Purpose::Split) amount_.hide();
    if (!banking && banking_ && bagForVault_) {
        inventoryOpen_ = false;
        bagForVault_ = false;
    }
    banking_ = banking;
    if (banking_) {
        ChestRequests asked;
        // The bag's drag as it stood last frame: the bag updates after the vault.
        const bool fromBag = inventoryOpen_ && bag_.dragging();
        // A worn piece too, straight off him (Realm::deposit, 2026-10-05).
        chest_.carrying(fromBag ? &play.realm().satchel()[bag_.dragged()] : nullptr, fromBag);
        chest_.update(float(window.width()), float(window.height()), 2, play.realm(), pointer,
                      shelfStage_, &asked);
        if (asked.moveFrom >= 0) {
            expect(play.rearrange(asked.moveFrom, asked.moveTo, asked.pieces), Then::TookOrRefused);
        }
        // A right-click: into the first room in the bag (Realm::withdraw pours it).
        if (asked.back >= 0) expect(play.withdraw(asked.back, -1), Then::TookOrRefused);
        // Let go outside: into the bag cell under the pointer; over the world, thrown on the
        // ground at his feet as the bag throws (the user, 2026-10-09: "allow to drop items to
        // ground from warehouse" -- MU refuses it). Over any window -- the bag's frame, the bar, the
        // chat -- it stays on the pointer, as the bag's does.
        if (asked.outside >= 0) {
            int w, h;
            footprint(play, play.realm().vault()[asked.outside], &w, &h);
            const int slot = inventoryOpen_ ? bag_.slotUnder(asked.outsideX, asked.outsideY, w, h) : -1;
            const bool overWindow = overWindows(asked.outsideX, asked.outsideY);
            if (slot >= 0) expect(play.withdraw(asked.outside, slot, asked.pieces), Then::TookOrRefused);
            else if (!overWindow) expect(play.discardFromVault(asked.outside, asked.pieces), Then::Refused);
            else chest_.split(asked.outside, asked.pieces);
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
    } else {
        chest_.cancel();
    }

    // The Broker's market (game/ui/broker.h), as the vault is: the bag beside it, the character
    // window shut. It asks the server and plays its own sounds; the desk hands it the keyboard
    // while a box of it types, and opens the chat on a seller it whispers. Updated shut too, so
    // an answer late to a closed window is spent.
    const bool brokering = play.isOpen() && play.realm().brokering() >= 0;
    if (brokering && !brokering_) {
        characterOpen_ = false;
        bagForBroker_ = !inventoryOpen_;
        inventoryOpen_ = true;
    }
    if (!brokering && brokering_ && bagForBroker_) inventoryOpen_ = bagForBroker_ = false;
    brokering_ = brokering;
    if (play.isOpen()) {
        Broker::Keys keys;
        if (broker_.typing()) keys = {typedNow, window.backspaces(), enterNow, escapeNow};
        // Its own box typing, it keeps the pointer: a price typed and then List pressed.
        const gfx::Art* faces[8] = {};
        for (int i = 0; i < 8; ++i) faces[i] = classFaces_[i].taken() ? &classFaces_[i].picture() : nullptr;
        faces[int(sim::Kin::MagicGladiator) * 2 + 1] = faces[int(sim::Kin::MagicGladiator) * 2];
        broker_.classFaces(faces);
        broker_.update(seconds, float(window.width()), float(window.height()), play,
                       broker_.typing() && !amount_.up() ? real : pointer, keys, shelfStage_);
        std::string whom;
        if (broker_.takeWhisper(&whom)) chat_.whisperTo(whom);
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
                        fromBag && !play.realm().mixed());
        // Three columns wide at the vault's place, as the Broker is: its right edge where the
        // bag's column begins.
        mixer_.update(seconds, float(window.width()), float(window.height()), 4, play.realm(),
                      play.mixAnswer(), play.mixWords(), pointer, shelfStage_, &asked);
        if (asked.moveFrom >= 0) {
            expect(play.shuffle(asked.moveFrom, asked.moveTo, asked.pieces), Then::TookOrRefused);
        }
        // Let go outside: into the bag cell under the pointer, and nowhere else; anywhere else
        // it stays on the pointer.
        if (asked.outside >= 0) {
            int w, h;
            footprint(play, play.realm().machine()[asked.outside], &w, &h);
            const int slot = inventoryOpen_ ? bag_.slotUnder(asked.outsideX, asked.outsideY, w, h) : -1;
            if (slot >= 0) expect(play.takeOut(asked.outside, slot, asked.pieces), Then::TookOrRefused);
            else mixer_.split(asked.outside, asked.pieces);
        }
        if (asked.back >= 0) {
            expect(play.takeOut(asked.back, -1), Then::TookOrRefused);
        }
        if (asked.click) click();
        // The service row stepped: the journal's and the map's page sound.
        if (asked.turned) play.sound().play(play.sound().load("quest_page_turn", false));
        // Take out: everything in the box back to the bag, as many right-clicks.
        if (asked.takeAll) {
            // Answered one by one; the last is heard for them all.
            uint32_t last = 0;
            for (int cell = 0; cell < sim::kMachineCells; ++cell) {
                if (play.realm().machine()[cell].empty()) continue;
                if (last != 0) expect(last, Then::Quiet);
                last = play.takeOut(cell, -1);
            }
            if (last != 0) expect(last, Then::TookOrRefused);
            else refused();
        }
        // Fill from bag: what the realm's plan picks, put in as many drags would, each to the
        // first cell it fits.
        if (asked.fill) {
            const sim::Planned planned = play.realm().planned(mixer_.service(), mixer_.socket(), false);
            uint32_t last = 0;
            for (int i = 0; i < planned.count; ++i) {
                if (last != 0) expect(last, Then::Quiet);
                last = play.putIn(planned.slots[i], -1, planned.pieces[i]);
            }
            if (last != 0) expect(last, Then::TookOrRefused);
            else refused();
        }
        // CMixCheckMsgBoxLayout's OK: the click, and the realm's answer is heard by Play.
        if (asked.mix) {
            click();
            expect(play.mix(mixer_.service(), mixer_.socket()), Then::Spark);
        }
        if (asked.close) {
            play.closeMachine();
            click();
        }
    } else {
        mixer_.cancel();
    }

    // Moss the Gambler, the Jeweler (game/ui/jeweler.h), as the machine is: the bag opens beside
    // him, and walking away shuts him in the realm. Nothing is held in his window.
    const bool jeweling = play.isOpen() && play.realm().jeweling() >= 0;
    if (jeweling && !jeweling_) {
        click();
        if (play.isOpen()) play.ui(Play::Ui::Opened);
        characterOpen_ = false;
        bagForJeweler_ = !inventoryOpen_;
        inventoryOpen_ = true;
    }
    if (!jeweling && jeweling_ && bagForJeweler_) {
        inventoryOpen_ = false;
        bagForJeweler_ = false;
    }
    jeweling_ = jeweling;
    if (jeweling_) {
        JewelerRequests asked;
        jeweler_.update(float(window.width()), float(window.height()), 2, play.realm(), pointer,
                        shelfStage_, &asked);
        if (asked.click) click();
        if (asked.turned) play.sound().play(play.sound().load("quest_page_turn", false));
        if (asked.recutSlot >= 0) expect(play.jewelRecut(asked.recutSlot, asked.recutLine), Then::Jewel);
        if (asked.salvageSlot >= 0) expect(play.jewelSalvage(asked.salvageSlot), Then::Jewel);
        if (asked.combine[0] >= 0) {
            expect(play.jewelCombine(asked.combine[0], asked.combine[1], asked.combine[2]), Then::Jewel);
        }
        if (asked.close) {
            play.closeJeweler();
            click();
        }
    } else {
        jeweler_.cancel();
    }

    // A trade with another player, as the vault is: it opens the bag beside it and closes the
    // character window; the realm shuts it when either walks off, and the windows follow.
    if (play.isOpen()) {
        play.approachDeal();
        dealNews(play);
    }
    const bool dealing = play.isOpen() && play.realm().dealing() != 0;
    if (dealing && !dealing_) {
        click();
        if (play.isOpen()) play.ui(Play::Ui::Opened);
        characterOpen_ = false;
        bagForDeal_ = !inventoryOpen_;
        inventoryOpen_ = true;
    }
    if (!dealing && amount_.purpose() == Amount::Purpose::Offer) amount_.hide();
    if (!dealing && dealing_ && bagForDeal_) {
        inventoryOpen_ = false;
        bagForDeal_ = false;
    }
    dealing_ = dealing;
    if (dealing_) {
        DealRequests asked;
        const bool fromBag = inventoryOpen_ && bag_.dragging();
        deal_.carrying(fromBag ? &play.realm().satchel()[bag_.dragged()] : nullptr);
        std::string name;
        play.playerName(play.realm().dealing(), &name, nullptr);
        deal_.update(seconds, float(window.width()), float(window.height()), 2, play.realm(), name, pointer,
                     shelfStage_, &asked);
        if (asked.take >= 0) expect(play.dealTake(asked.take, -1), Then::TookOrRefused);
        // Let go outside: into the bag cell under the pointer, and nowhere else; anywhere else
        // it stays on the pointer.
        if (asked.outside >= 0) {
            int w, h;
            footprint(play, play.realm().dealOffer(false)[asked.outside], &w, &h);
            const int slot = inventoryOpen_ ? bag_.slotUnder(asked.outsideX, asked.outsideY, w, h) : -1;
            if (slot >= 0) expect(play.dealTake(asked.outside, slot), Then::TookOrRefused);
            else deal_.hold(asked.outside);
        }
        if (asked.zen) {
            amount_.show(Amount::Purpose::Offer, play.realm().dealZenOf(false));
            click();
        }
        if (asked.accept >= 0) expect(play.dealAccept(asked.accept == 1), Then::Click);
        if (asked.close) {
            play.closeDeal();
            click();
        }
    } else {
        deal_.update(seconds, float(window.width()), float(window.height()), 2, play.realm(), std::string(), pointer,
                     nullptr, nullptr);
    }

    // The bag, in the right-hand column or beside the character window when that is up.
    if (inventoryOpen_ && play.isOpen()) {
        BagRequests asked;
        bag_.carrying(banking_ && chest_.dragging()   ? &play.realm().vault()[chest_.dragged()]
                      : mixing_ && mixer_.dragging() ? &play.realm().machine()[mixer_.dragged()]
                                                     : nullptr);
        // Shift and a click on a thing while the chat types: its link into the line, and the bag
        // not told of the press (WoW's gesture; the user, 2026-10-11).
        Pointer bagPointer = pointer;
        if (chat_.typing() && window.shift() && pointer.pressed) {
            const int hovered = bag_.hovered();
            const sim::Satchel& carried = play.realm().satchel();
            if (hovered >= 0 && !carried[hovered].empty()) {
                chat_.link(play.realm().tables()->items[size_t(carried[hovered].item)], carried[hovered]);
                bagPointer.pressed = false;
            }
        }
        // What lies on the Jeweler's page is shown there, not in the bag, until taken out.
        bag_.away(jeweling_ ? jeweler_.away() : std::bitset<sim::kSlots>{});
        bag_.update(float(window.width()), float(window.height()), characterOpen_ || inspecting() ? 2 : 1,
                    play.realm(), bagPointer, bagStage_, &asked);
        if (asked.toggleDock) click();
        // A line set on the dock is a request; the dock redraws when the realm takes it.
        if (asked.autoHealth >= 0) {
            play.setAutoDrink(asked.autoHealth, asked.autoMana);
            click();
        }
        // A move is ReceiveEquipmentItem, which ends its success branch on SOUND_GET_ITEM01 --
        // MU's equip sound is the pickup's -- and a use refused is iButtonError. A use that goes
        // through is heard as the potion going down, off the realm's Drank.
        if (asked.moveFrom >= 0) {
            expect(play.moveItem(asked.moveFrom, asked.moveTo, asked.pieces), Then::TookOrRefused);
        }
        // A jewel on a thing: Play rings both of its sounds, so a yes needs nothing here.
        if (asked.refineJewel >= 0) expect(play.refine(asked.refineJewel, asked.refineTarget, asked.pieces), Then::Refused);
        // At the machine a right-click puts the thing in the box instead
        // (ProcessMyInvenItemAutoMove); a worn one, which the box refuses, is still used.
        if (asked.use >= 0 && jeweling_ && sim::baggable(asked.use)) {
            // At the Jeweler a right-click puts a ring or pendant on his page, and nothing else.
            if (jeweler_.offer(asked.use, play.realm())) click();
            else refused();
        } else if (asked.use >= 0 && mixing_ && sim::baggable(asked.use)) {
            expect(play.putIn(asked.use, -1, piecesForBox(play, asked.use, 0)), Then::TookOrRefused);
        } else if (asked.use >= 0 && brokering_ && sim::baggable(asked.use)) {
            broker_.offer(asked.use);  // at the Broker a right-click puts it on the Sell tab
            click();
        } else if (asked.use >= 0 && dealing_ && sim::baggable(asked.use)) {
            // In a trade a right-click puts it up, as at the machine.
            expect(play.dealPut(asked.use, -1), carriesBound(asked.use) ? Then::Bound : Then::TookOrRefused);
        } else if (asked.use >= 0 && banking_ && sim::baggable(asked.use)) {
            // At the vault a right-click puts it away, in the first room it fits.
            expect(play.deposit(asked.use, -1), Then::TookOrRefused);
        } else if (asked.use >= 0 && trading_ && sim::baggable(asked.use)) {
            // At a merchant a right-click sells it, as a drop on the shelf does (the user,
            // 2026-10-17: "allow to sell item with right click if vendor is opened").
            expect(play.sell(asked.use), Then::Refused);
        } else if (const int worn = asked.use >= 0 && sim::baggable(asked.use)
                                        ? wornSlotFor(play, play.realm().satchel()[asked.use])
                                        : -1;
                   worn >= 0) {
            // Anywhere else a right-click on gear wears it, as a drag onto its slot does.
            expect(play.moveItem(asked.use, worn), Then::TookOrRefused);
        } else if (asked.use >= 0 && sim::wearable(asked.use)) {
            // A right-click on a worn piece takes it off, into the first room in the bag; a full
            // bag is the interface's no (bug report #7, 2026-10-18: "fast equip/unequip").
            const sim::Held& what = play.realm().satchel()[asked.use];
            const content::Tables* tables = play.realm().tables();
            const int room = tables && !what.empty() && size_t(what.item) < tables->items.size()
                                 ? play.realm().satchel().free(*tables, tables->items[size_t(what.item)].width,
                                                               tables->items[size_t(what.item)].height)
                                 : -1;
            if (room >= 0) expect(play.moveItem(asked.use, room), Then::TookOrRefused);
            else refused();
        } else if (asked.use >= 0) {
            expect(play.useItem(asked.use), Then::Refused);
        }
        // A click in repair mode: mended where it lies, heard as SOUND_REPAIR by Play, and a
        // refusal -- whole already, not repairable, not the Zen -- is the interface's no.
        if (asked.repair >= 0) expect(play.repair(asked.repair), Then::Refused);
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
        int carriedW = 1, carriedH = 1;
        if (asked.outside >= 0) footprint(play, play.realm().satchel()[asked.outside], &carriedW, &carriedH);
        if (asked.outside >= 0 && trading_ && shelf_.covers(asked.outsideX, asked.outsideY)) {
            expect(play.sell(asked.outside, asked.pieces), Then::Refused);
        } else if (asked.outside >= 0 && banking_ &&
                   chest_.covers(asked.outsideX, asked.outsideY)) {
            // Into the vault cell under the pointer; over the foot or the head, the first cell
            // it fits in.
            expect(play.deposit(asked.outside, chest_.cellUnder(asked.outsideX, asked.outsideY, carriedW, carriedH),
                                asked.pieces),
                   Then::TookOrRefused);
        } else if (asked.outside >= 0 && brokering_ && broker_.covers(asked.outsideX, asked.outsideY)) {
            broker_.offer(asked.outside);  // dropped on the market: chosen to sell
            click();
        } else if (asked.outside >= 0 && dealing_ && deal_.covers(asked.outsideX, asked.outsideY)) {
            // Into his own box, at the cell under the pointer or the first it fits.
            expect(play.dealPut(asked.outside, deal_.cellUnder(asked.outsideX, asked.outsideY, carriedW, carriedH),
                                asked.pieces),
                   carriesBound(asked.outside) ? Then::Bound : Then::TookOrRefused);
        } else if (asked.outside >= 0 && mixing_ &&
                   mixer_.covers(asked.outsideX, asked.outsideY)) {
            expect(play.putIn(asked.outside, mixer_.cellUnder(asked.outsideX, asked.outsideY, carriedW, carriedH),
                              piecesForBox(play, asked.outside, asked.pieces)),
                   Then::TookOrRefused);
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
        } else if (asked.outside >= 0 && overWindows(asked.outsideX, asked.outsideY)) {
            // Over any other window -- the character's, the chat, the minimap, the bar off its
            // potion boxes -- it stays on the pointer, as MU's picked item does: let go on a
            // window, a thing went on the ground behind it.
            bag_.split(asked.outside, asked.pieces);
        } else if (asked.outside >= 0) {
            // The one gesture in the interface that gives something away: a click on the
            // ground with the thing picked, MU's IsPress off every window. A refusal -- a dead
            // man's drop -- is the interface's no, and the window puts the item back by
            // redrawing from a satchel that never changed.
            expect(play.discard(asked.outside, asked.pieces), Then::Refused);
        }
        // Clicks as the I and V keys do. INVENTION, the user's (2026-09-30): CNewUIMyInventory's
        // exit button is silent in MU.
        if (asked.close) toggleBag();
    } else {
        bag_.cancel();
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
    minimap_.setReporting(report_.up());
    minimap_.setAutoPlaying(autoPlaying_);
    minimap_.setHelping(helping_);
    minimap_.update(seconds, play, typing || menuHeld || lineChip_.covers(pointer.x, pointer.y) ? Pointer{} : pointer,
                    typing || menuHeld ? 0.0f : window.scroll(), window.width(), window.height());
    // An Elite near: its bell on each pulse of its mark (Minimap::takeBell).
    if (minimap_.takeBell() && play.isOpen()) play.ui(Play::Ui::Bell);
    // Its H, as H is; and its right click, the window.
    if (minimap_.takeHelperToggle() && play.isOpen()) {
        helperPressed();
        click();
    }
    if (minimap_.takeHelperWindow() && play.isOpen()) {
        openHelper();
        click();
    }
    // Its Auto-play button stops it, as Stop on the log does.
    if (minimap_.takeAutoStop() && autoPlaying_ >= 0) autoAsk_ = -1;
    // Its bug button raises the report, with what goes with it said: where and who.
    if (minimap_.takeReport() && !report_.up() && !amount_.up() && play.isOpen()) {
        const sim::Body& hero = play.realm().hero();
        std::string name;
        if (!play.playerName(hero.id, &name, nullptr)) name = heroName_;
        std::string about = shownPlace(worldName_, hero.column(), hero.row()) + "  \xC2\xB7  " +
                            std::to_string(hero.column()) + ", " + std::to_string(hero.row()) + "  \xC2\xB7  ";
        if (!name.empty()) about += name + ", ";
        about += std::string(sim::className(int(hero.kin), hero.second)) + ", level " + std::to_string(hero.level);
        report_.show(about);
        click();
        core::logf("window: bug report up");
    }
    // A press that began over a window stays the window's until the button comes up, even when
    // the press shut the window: the Guild Master's "Not now" hid its box in this frame's update,
    // and the same press, asked again below with the box gone, walked him to where it had been
    // (the user, 2026-10-16: "we need that never click to move triggers on any UI window button").
    if (pressedOnWindow) pressOnWindow_ = true;
    else if (!pointer.held && !pointer.pressed) pressOnWindow_ = false;
    takesPointer_ = typing || menuHeld || menuTook_ || travelPressed || carrying_ != 0 ||
                    liftedQuick_ >= 0 || pressOnWindow_ || overWindows(pointer.x, pointer.y);

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

void Desk::script(float x, float y, bool press, bool release, bool right, bool shift) {
    scripted_ = true;
    scriptShift_ = press && shift;
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
        if (best >= 0) expect(play.useItem(best), Then::Strike, key);
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
    // weapon or not at all (docs/game/skills-dk.md §3.1b), so the plate asks `row.suits()` exactly as
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
        // Nor a summon where none is raised: Icarus (Realm::conjure, sim::MapRow::summons).
        const bool raised = row.summons <= 0 || sim::summonsHere(row, uint32_t(tables.map));
        return hero.alive() && (!inTown || court) && armedFor(row) && hero.mana >= sim::manaCostOf(hero, row) &&
               (!row.mounted || hero.riding) && raised;
    };

    fan_.clear();
    for (int i = 0; i < sim::skillCount(); ++i) {
        const sim::SkillRow& row = sim::skillAt(i);
        if (!realm.knows(row.number)) continue;
        Hud::FanCell cell;
        cell.number = row.number;
        cell.name = row.name;
        cell.mana = sim::manaCostOf(hero, row);  // his talents' share off it, as the realm charges
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
            // The Angel's and the Imp's bar is its day (sim::timedPet): minutes left of 1440.
            if (sim::timedPet(row)) {
                const int64_t until = sim::petUntil(pet);
                const int64_t left =
                    until == 0 ? sim::kPetDaySeconds : std::max<int64_t>(0, until - int64_t(std::time(nullptr)));
                boon.lifeMost = int(sim::kPetDaySeconds / 60);
                boon.life = int((left + 59) / 60);
                boon.share = float(boon.life) / float(boon.lifeMost);
            }
        }
    }
    // Her summon while it stands (Realm::summoned): hidden in a safe zone it is not standing.
    if (const sim::Body* summon = realm.summoned();
        summon && summon->alive() && !summon->gone && summon->maxHealth > 0 && standing < Hud::kBoons) {
        Hud::Boon& boon = boons[standing++];
        boon.summon = true;
        boon.skill = summon->summonedBy;
        boon.life = summon->health;
        boon.lifeMost = summon->maxHealth;
        boon.share = float(summon->health) / float(summon->maxHealth);
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
    // **His talents' states** (docs/plans/talents.md; the cells of memory talent-buff-icons), after
    // Frenzy's: each while it stands, its clock against the length his talent gives it (Mercy's,
    // which another's heal may lay, against the table's 5 s when he has none of his own).
    {
        const int64_t now = realm.tick();
        const auto lengthOf = [&](sim::Special special, int fallback) {
            const sim::TalentEffect* found = nullptr;
            sim::talentSpecial(hero.kin, hero.talents, special, sim::TalentAsk{}, &found);
            return float((found && found->seconds > 0 ? found->seconds : fallback) * sim::kTalentTicksPerSecond);
        };
        const auto timed = [&](int64_t until, int talent, float amount, sim::Special special, int fallback) {
            if (until <= now || standing >= Hud::kBoons) return;
            Hud::Boon& boon = boons[standing++];
            boon.talent = talent;
            boon.amount = int(std::lround(amount));
            const float left = float(until - now);
            boon.seconds = left * 0.05f;
            boon.share = std::min(1.0f, left / std::max(1.0f, lengthOf(special, fallback)));
        };
        timed(hero.procUntil, hero.procWizardry ? Hud::kBattleMage : Hud::kBattleFury, hero.procAmount,
              sim::Special::DamageProc, 5);
        timed(hero.killSpeedUntil, Hud::kBloodrush, float(hero.killSpeed), sim::Special::KillSpeed, 4);
        timed(hero.ironWillUntil, Hud::kIronWill, hero.ironWill, sim::Special::IronWill, 4);
        timed(hero.mercyUntil, Hud::kMercy, hero.mercy, sim::Special::Mercy, 5);
        // The two with no clock: Riposte while armed, Last Stand while he is under 30% life.
        const auto standingFor = [&](int talent, sim::Special special) {
            if (standing >= Hud::kBoons) return;
            Hud::Boon& boon = boons[standing++];
            boon.talent = talent;
            boon.amount = int(std::lround(sim::talentSpecial(hero.kin, hero.talents, special, sim::TalentAsk{})));
            boon.share = 1.0f;
        };
        if (hero.riposte) standingFor(Hud::kRiposte, sim::Special::Riposte);
        if (hero.alive() && hero.maxHealth > 0 && hero.health * 10 < hero.maxHealth * 3 &&
            sim::talentSpecial(hero.kin, hero.talents, sim::Special::LastStand, sim::TalentAsk{}) > 0.0f) {
            standingFor(Hud::kLastStand, sim::Special::LastStand);
        }
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
    // And the Golden Dragon's Dragonfire, its stacks in the corner (sim/raid.h).
    if (hero.dragonfireUntil > realm.tick() && hero.dragonfire > 0 && standing < Hud::kBoons) {
        Hud::Boon& boon = boons[standing++];
        boon.dragonfire = true;
        boon.stacks = hero.dragonfire;
        const float left = float(hero.dragonfireUntil - realm.tick());
        boon.seconds = left * 0.05f;
        boon.share = left / float(sim::kDragonfireTicks);
    }
    // And a bleeder's claws (sim kBleeders), its stacks in the middle as Dragonfire's.
    if (hero.bleedUntil > realm.tick() && hero.bleed > 0 && standing < Hud::kBoons) {
        Hud::Boon& boon = boons[standing++];
        boon.bleed = true;
        boon.stacks = hero.bleed;
        const float left = float(hero.bleedUntil - realm.tick());
        boon.seconds = left * 0.05f;
        boon.share = left / float(sim::kBleedTicks);
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
    // **Nova let go with its key** (the user, 2026-10-07: 'Hold, let go'): while the realm holds a
    // charge, the first frame no key bound to it is down lets it go (Realm::letGo).
    if (const int32_t held = play.realm().chargeSkill(); held != 0) {
        bool down = false;
        for (int key = 0; key < Hud::kSkillKeys; ++key) {
            if (bound_[key] == held && (window.down(keys[key]) || scriptedHold_ == key)) down = true;
        }
        if (!down) play.letGo();
    }
    scriptedHold_ = -1;
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
    // carry the stat inside them. The formulas live in docs/game/skills-dk.md, where they are read
    // once, rather than on a card read fifty times a fight.
    tip::Section facts;
    const int64_t left = realm.cooling(row.number);
    const float seconds = float(realm.coolsFor(row.number)) * 0.05f;

    // **What it is thrown with, and it is the first row on the card.** Added 2026-09-23 with the
    // families themselves (docs/game/skills-dk.md §3.1b): once a skill can only be thrown with its own
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
    // Nor one any hand throws -- a buff or a summon -- whose list is empty.
    if (!row.wizardry && !row.mounted && !(row.anyHand && row.onSelf()) &&
        !sim::familiesListed(row.families).empty()) {
        facts.rows.push_back(weapon);
    }
    if (row.mounted) {
        facts.rows.push_back(line("Mount", "Horn of Dinorant",
                                  hero.riding ? tip::Tone::White : tip::Tone::Red));
    }
    // **Her walking summons, not over Icarus** (Realm::conjure refuses them there; the user,
    // 2026-10-20: "dont allow walking pets for elf in icarus, basically we need to show that its
    // not usable on tooltip. we will have flying summons later"): a red row where it is refused,
    // the key cold beside it (`ready`).
    if (row.summons > 0 && tables && !sim::summonsHere(row, uint32_t(tables->map))) {
        facts.rows.push_back(line("Here", "Not usable in " + quest_world::mapName(tables->map),
                                  tip::Tone::Red));
    }
    if (row.wizardry) {
        // The band in his hands and whom it strikes, in the words its scroll's card uses
        // (`spellLines`, game/ui/describe.cpp).
        spellLines(row, realm.wearer(), false, facts.rows);
    } else if (row.onSelf()) {
        // A guard, a buff, a heal or a summon, in the lines its orb prints (`selfLines`,
        // game/ui/describe.cpp). Greater Damage and the summons were read as guards until the
        // user's of 2026-10-08 ("elf greater damage wrong tooltip info", "summon orb tooltip
        // info is bugs, it show some kind of absorb").
        selfLines(row, tables, realm.wearer(), false, facts.rows);
    } else {
        facts.rows.push_back(line("Damage", "x" + number(sim::force(row, hero.totalPoints()), 2) +
                                                " of a swing",
                                  tip::Tone::Yellow));
        // And the sum that made it, on one grey line: the row's own base plus strength over the
        // skill's divisor. Asked for on 2026-09-23 -- the multiplier says what he hits for and
        // this says WHY, which is the whole argument for spending on strength, and it is one
        // line rather than the two labelled rows it was before.
        char sum[64];
        std::snprintf(sum, sizeof(sum), "%.2f + %d str / %d", double(row.force),
                      hero.totalPoints().strength,
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
    const int32_t cost = sim::manaCostOf(hero, row);  // as the realm charges it, talents and all
    const bool paid = hero.mana >= cost;
    facts.rows.push_back(line("Mana", std::to_string(cost), paid ? tip::Tone::Blue
                                                                     : tip::Tone::Red));
    // And the level it was met at, when there is one. On a card this is history rather than a
    // requirement -- a skill he can read about is one he has already learned -- but it is the
    // one line that says why the bar grew a key this level, and it is the number the orb will
    // ask for when the orbs exist (docs/game/skills-dk.md §3.3).
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
// In a Blood Castle the Archangel's weapon the statue lets fall is Epic purple (the user,
// 2026-10-21: "arch quest item has to be epic color on BC when destroy the statue").
static uint32_t tintOf(const content::Tables& tables, const sim::Lying& one, bool castle) {
    if (one.what.empty()) return gfx::rgba(1.0f, 0.8f, 0.1f);
    if (castle && sim::archangelQuestItem(tables.items[size_t(one.what.item)], one.what)) {
        return tip::colourOf(tip::Tone::Epic);
    }
    return tip::colourOf(qualityOf(tables.items[size_t(one.what.item)], one.what));
}

void Desk::labelGround(const Play& play, int width, int height) {
    play.dropsOnScreen(viewProj_, width, height, onScreen_);
    const uint32_t lit = play.pointedLying();
    bool same = onScreen_.size() == drawnOnScreen_.size() && lit == groundLit_;
    for (size_t i = 0; same && i < onScreen_.size(); ++i) {
        same = onScreen_[i].id == drawnOnScreen_[i].id && onScreen_[i].x == drawnOnScreen_[i].x &&
               onScreen_[i].y == drawnOnScreen_[i].y;
    }
    if (same && groundRebuilds_ > 0) return;
    drawnOnScreen_ = onScreen_;
    groundLit_ = lit;
    ++groundRebuilds_;
    ground_.clear();
    plates_.clear();
    const content::Tables& tables = *play.realm().tables();
    // **Diablo IV's ground labels** (the user's reference, 2026-10-06: 'analyse item labels, and
    // make similiar design'): a mixed-case serif in the quality's colour with its own shadow, on
    // a see-through dark strip padded past the words and fading out at both ends, lit grey under
    // the pointer, and a pile's names settling round it rather than in one tower. The name's
    // grammar is ours and unchanged. MU's own was the text's box in opaque black.
    // The face is Amarante, a medieval roman with a gothic hand in its capitals (the user, same
    // day: 'font needs to be more gothic and little bit smaller label', then of Cinzel, all
    // capitals, 'this font hard reads', and 'amarante' off a sheet of nineteen faces drawn over
    // Lorencia's ground). Alegreya, Cinzel and Grenze before it, the same day.
    const bool gothic = groundFace_.ready() && bgfx::isValid(groundTexture_);
    const bgfx::TextureHandle texture = groundTexture_;
    const gfx::Face& face = gothic ? groundFace_ : ground_.face();
    const float u = panel::unit();
    // Under Alegreya's 9.5, as the user asked for a smaller label.
    const float size = 8.5f * u;
    const float track = 0.0f;  // Amarante sets its letters apart already
    // The strip's air: each end as wide as its fade, so the dark is whole where the words start.
    const float padX = 0.8f * size, padY = 0.2f * size;
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
                       : sim::itemLabel(row, one->what);
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
        // g_hFontBold for a jewel, which is a point up here, as a tip's bold line is.
        const float set = boldOf(tables, *one) ? size + u : size;
        const float w = face.measure(set, name) + track * float(name.size()) + padX * 2.0f;
        const float h = face.height(set) + padY * 2.0f;
        // Another player's drop in grey: his alone to pick up (sim::Lying::owner). Ours.
        const uint32_t tint = play.realm().mayTake(*one) ? tintOf(tables, *one, play.realm().event() == sim::EventKind::Castle)
                                                         : gfx::rgba(0.5f, 0.5f, 0.5f);
        labels.push_back({at.id, std::move(name), set, tint,
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
    // Diablo's strips nearly touch: a pile reads as one list.
    const float gap = u * 0.5f;
    const auto crosses = [gap](const gfx::Box& a, const gfx::Box& b) {
        return a.x < b.x + b.w + gap && b.x < a.x + a.w + gap && a.y < b.y + b.h + gap &&
               b.y < a.y + a.h + gap;
    };
    // **Nearest free place, either side** (Diablo's pile, which spreads above and below its
    // drops): each name tries its own place, then just over or just under every plate it would
    // share a column with, and takes the nearest that crosses none -- under counted half again
    // as far, so a pile still leans upward off its things. Over the highest plate in its column
    // is always free, and there is no loop to freeze on rounding (crashes/hang-2026-10-04).
    std::vector<size_t> placed;
    placed.reserve(order.size());
    std::vector<float> tries;
    for (size_t i : order) {
        gfx::Box& plate = labels[i].plate;
        const float home = plate.y;
        tries.assign(1, home);
        for (size_t p : placed) {
            const gfx::Box& b = labels[p].plate;
            if (plate.x < b.x + b.w + gap && b.x < plate.x + plate.w + gap) {
                tries.push_back(b.y - plate.h - gap * 1.01f);
                tries.push_back(b.y + b.h + gap * 1.01f);
            }
        }
        const auto far = [home](float y) { return y > home ? (y - home) * 1.5f : home - y; };
        std::stable_sort(tries.begin(), tries.end(),
                         [&](float a, float b) { return far(a) < far(b); });
        for (float y : tries) {
            plate.y = y;
            bool free = true;
            for (size_t p : placed) free = free && !crosses(plate, labels[p].plate);
            if (free) break;
        }
        placed.push_back(i);
    }
    // The strip: Diablo's dark glass, its ends faded to nothing across the padding. 0.7 and no
    // thinner: over a legendary's column at 0.62 a green name went into the green.
    const uint32_t body = gfx::rgba(0.0f, 0.0f, 0.0f, 0.7f);
    const uint32_t litBody = gfx::rgba(0.24f, 0.22f, 0.20f, 0.86f);
    const float drop = std::max(1.0f, u);
    // A minimal outline (the user, 2026-10-06: 'also use some minimal outline for label and
    // hover effect'): a hairline along the strip's top and bottom in a bronze a little over the
    // card's ring -- the ring's own went into the strip -- faded out across the ends as the
    // strip is, so the ends stay open. Under the pointer the hairlines take the name's own
    // colour and the strip lifts to warm grey.
    const float hair = std::max(1.0f, u * 0.5f);
    const uint32_t rim = gfx::rgba(0.58f, 0.47f, 0.35f, 0.8f);
    const auto band = [&](const gfx::Box& b, float y, float tall, uint32_t ink) {
        const uint32_t none = ink & 0x00FFFFFFu;
        ground_.shade({b.x, y, padX, tall}, none, ink, ink, none);
        ground_.rect({b.x + padX, y, b.w - padX * 2.0f, tall}, ink);
        ground_.shade({b.x + b.w - padX, y, padX, tall}, ink, none, none, ink);
    };
    for (const Label& l : labels) {
        const gfx::Box& b = l.plate;
        const bool on = l.id == lit;
        band(b, b.y, b.h, on ? litBody : body);
        const uint32_t edge = on ? l.tint : rim;
        band(b, b.y, hair, edge);
        band(b, b.y + b.h - hair, hair, edge);
        plates_.push_back({l.id, b});
        const float x = b.x + padX, baseline = b.y + padY + face.ascent(l.set);
        if (gothic) {
            ground_.lettered(face, texture, x + drop, baseline + drop, l.set, track,
                             tip::ink::kDrop, l.name);
            ground_.lettered(face, texture, x, baseline, l.set, track, l.tint, l.name);
        } else {
            ground_.shadowed(x, baseline, l.set, l.tint, tip::ink::kDrop, drop, l.name);
        }
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
    bossBar_.update(seconds, play, viewProj, width, height);
    // After the names, so the marker rises by this frame's fade and not the last one's.
    beacon_.update(seconds, play, vitals_.namedFolk(), vitals_.folkShown(vitals_.namedFolk()),
                   viewProj, width, height);
    // The quest on screen, out of the way of a window on the right and of the giver's own.
    tracker_.setUnder(minimap_.under());
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
    if (inspecting()) inspectStagePicture_.render(renderer, pixelsPerUnit, seconds);
    if (trading_ || banking_ || brokering_ || mixing_ || jeweling_ || dealing_) {
        shelfStagePicture_.render(renderer, pixelsPerUnit, seconds);
    }
    // The quest giver's rewards on the same stage, which is free whenever he is (no counter).
    else if (questDialog_.up()) {
        shelfStagePicture_.render(renderer, questDialog_.pixelsPerUnit(), seconds);
    }
    // And the quest log's, which stands where the giver's window does and never with it.
    else if (questLog_.up()) {
        shelfStagePicture_.render(renderer, questLog_.pixelsPerUnit(), seconds);
    }
    // The potion boxes are always on screen, and at rest their stage costs nothing.
    quickStagePicture_.render(renderer, hud_.pixelsPerUnit(), seconds);
    // The tooltip's picture, at the tip's own scale -- the interface pixel, not the windows'.
    tipStagePicture_.render(renderer, panel::unit(), seconds);
}

void Desk::portrait(gfx::Renderer& renderer, Play& play) {
    if (play.targetChanged()) portrait_.forget();
    // The inspected player whole, on a view of his own (ViewStageFigure), so the faces below go on.
    // His doll (Play::poseDoll), not his body in the world: weapons on his back, as in a safe zone.
    // Taken every frame while he hovers on wings, else only when he is turned or his gear changes.
    if (inspecting()) {
        const float turn = inspect_.turn();
        if (play.dollStale() || play.dollMoving() || turn != inspectTurned_ || !inspectFigure_.taken()) {
            if (!renderer.openStages(shaderDir_)) return;
            float head[3] = {}, scale = 1.0f;
            if (play.poseDoll(renderer, turn, inspectBody_, head, &scale)) {
                // A fixed lens (yaw 0): he turns in front of it.
                inspectFigure_.render(renderer, inspectBody_, head, 0.0f, scale);
                inspectTurned_ = turn;
            }
        }
    }
    // A still: taken once, and again only when what he wears changes. One a frame, the target's
    // first: every face is drawn on the one portrait stage.
    if (targetFrame_.showing() && !play.targetDrawables().empty() && !portrait_.same(play.targetDrawables())) {
        if (!renderer.openStages(shaderDir_)) return;
        portrait_.render(renderer, play.targetDrawables(), play.targetHead(), play.targetYaw(), play.targetScale());
        return;
    }
    // His party's, the same way (the user, 2026-10-11: "same idea with circle thumb so we have
    // same logic"); a member gone from the party lets his go.
    const net::PartyState& party = play.party();
    for (auto it = partyFaces_.begin(); it != partyFaces_.end();) {
        bool kept = false;
        for (const auto& member : party.members) kept = kept || member.name == it->first;
        if (kept) {
            ++it;
        } else {
            it->second.shutdown();
            it = partyFaces_.erase(it);
        }
    }
    for (const Play::FaceShot& face : play.faces()) {
        if (face.drawables.empty()) continue;
        const net::PartyState::Member* member = nullptr;
        for (const auto& one : party.members) {
            if (one.here == face.id) member = &one;
        }
        if (!member) continue;
        Portrait& picture = partyFaces_[member->name];
        if (picture.same(face.drawables)) continue;
        if (!renderer.openStages(shaderDir_)) return;
        picture.render(renderer, face.drawables, face.head, face.yaw, face.scale);
        ++partyFacesTaken_;
        return;
    }
    // The classes' faces for the market's card (the user, 2026-10-19: "show class picture on
    // market"), each once, while the Broker is open. The gladiator has no second.
    if (brokering_) {
        for (int i = 0; i < sim::kKinCount * 2; ++i) {
            const sim::Kin kin = sim::Kin(i / 2);
            const bool second = (i % 2) != 0;
            if (classFaces_[i].taken() || (second && kin == sim::Kin::MagicGladiator)) continue;
            if (!renderer.openStages(shaderDir_)) return;
            float head[3] = {}, scale = 1.0f;
            if (!play.poseClass(renderer, kin, second, classBody_, head, &scale)) continue;
            classFaces_[i].render(renderer, classBody_, head, 0.0f, scale);
            return;
        }
    }
}

void Desk::submit(bgfx::ViewId view, int width, int height) {
    interface_.begin(width, height);
    interface_.add(ground_);
    // Over the world's labels and under every window: it is a reading lying on the scene.
    // The quest marker under the names and bars, which are read at the moment of pointing.
    if (beacon_.showing()) interface_.add(beacon_.canvas());
    if (vitals_.showing()) interface_.add(vitals_.canvas());
    if (bossBar_.showing()) interface_.add(bossBar_.canvas());
    // What the guards are saying, over the bars and names and under every window.
    if (speech_.showing()) interface_.add(speech_.canvas());
    // The blows' figures over the bar, because a number is the thing being read at that
    // instant and the bar is the state behind it -- and still under every window.
    if (tally_.showing()) interface_.add(tally_.canvas());
    // The map's name, a reading on the scene as well, and under every window.
    if (arrival_.showing()) interface_.add(arrival_.canvas());
    if (tracker_.showing() && !watching_) interface_.add(tracker_.canvas());
    if (tracker_.announcing() && !watching_) interface_.add(tracker_.banner());
    // Go Back! over the HUD's middle: chrome, under every window.
    if (goBack_.showing() && !watching_) interface_.add(goBack_.canvas());
    // The herald at the top centre: a reading, under every window.
    if (herald_.showing()) interface_.add(herald_.canvas());
    // Chrome like the plate, under every window that might open over its corner.
    if (minimap_.showing()) interface_.add(minimap_.canvas());
    // Watched: the scene's readings above and the menu and pointer below, and nothing of his.
    if (watching_) {
        if (travel_.up()) interface_.add(travel_.canvas());
        if (menu_.up()) interface_.add(menu_.canvas());
        interface_.add(cursor_.canvas());
        interface_.submit(view);
        return;
    }
    // The party steps aside while M lays the whole map over the screen.
    if (partyBar_.showing() && !watching_ && !minimap_.full()) interface_.add(partyBar_.canvas());
    if (targetFrame_.showing()) interface_.add(targetFrame_.canvas());
    // The Warp list (M) over the party's capsules, which it opens across (bug #20: "party window
    // is being drawn over Warp Window").
    if (travel_.up()) interface_.add(travel_.canvas());
    // The chat, at the bottom left; the plate is drawn over it.
    if (chat_.showing()) {
        interface_.add(chat_.back());
        interface_.add(chat_.tabs());
        interface_.add(chat_.lines());
        if (chat_.setting()) interface_.add(chat_.settings());
    }
    interface_.add(hud_.canvas());
    interface_.add(hud_.glowCanvas());  // the ember round its Talents button
    // The line's chip in the corner over the minimap, its list over whatever lies under it.
    if (lineChip_.showing()) interface_.add(lineChip_.canvas());
    // A party invite, centred over the plate where a fight's Zen is said.
    if (chat_.inviting()) interface_.add(chat_.invite());
    if (characterOpen_) {
        interface_.add(card_.canvas());
    }
    if (inspecting()) interface_.add(inspect_.canvas());
    if (questLog_.up()) {
        interface_.add(questLog_.canvas());
        interface_.add(questLog_.body());
        interface_.add(questLog_.card());
    }
    if (helperWindow_.up()) interface_.add(helperWindow_.canvas());
    if (trading_) interface_.add(shelf_.canvas());
    // The window something is dragged out of goes over the other, so what rides the pointer is
    // never under a window it is carried across -- a vault piece over the bag hid behind it.
    const bool vaultOnTop = banking_ && chest_.dragging();
    if (banking_ && !vaultOnTop) interface_.add(chest_.canvas());
    if (brokering_) interface_.add(broker_.canvas());
    const bool machineOnTop = mixing_ && mixer_.dragging();
    if (mixing_ && !machineOnTop) interface_.add(mixer_.canvas());
    if (jeweling_) interface_.add(jeweler_.canvas());
    const bool dealOnTop = dealing_ && deal_.dragging();
    if (dealing_ && !dealOnTop) interface_.add(deal_.canvas());
    if (inventoryOpen_) interface_.add(bag_.canvas());
    if (guildMake_.up()) interface_.add(guildMake_.canvas());
    if (guild_.up()) interface_.add(guild_.canvas());
    if (dealOnTop) interface_.add(deal_.canvas());
    if (vaultOnTop) interface_.add(chest_.canvas());
    if (machineOnTop) interface_.add(mixer_.canvas());
    if (talents_.up()) {
        interface_.add(talents_.canvas());
        interface_.add(talents_.glow());
    }
    // The worn-gear warning hangs left of the windows and so never lies under one; over them,
    // because its hover line is the one part of it that can reach one.
    if (endurance_.showing()) interface_.add(endurance_.canvas());
    // The tips over every window, and under the pointer. Whichever is hovered, it is the one
    // thing on the panel the player is reading at that moment.
    interface_.add(hud_.tipCanvas());
    if (trading_) interface_.add(shelf_.tipCanvas());
    if (banking_) interface_.add(chest_.tipCanvas());
    if (brokering_) interface_.add(broker_.tipCanvas());
    if (dealing_) interface_.add(deal_.tipCanvas());
    if (mixing_) interface_.add(mixer_.tipCanvas());
    if (inventoryOpen_) interface_.add(bag_.tipCanvas());
    if (inspecting()) interface_.add(inspect_.tipCanvas());
    if (questLog_.up()) interface_.add(questLog_.tipCanvas());
    if (talents_.up()) interface_.add(talents_.card());
    if (endurance_.showing()) interface_.add(endurance_.tipCanvas());
    if (chat_.tipping()) interface_.add(chat_.tipCanvas());
    // Last of all, over every window too: MU2's own CanvasLayer{Layer=128} -- a pointer is over
    // whatever it is pointing at, and the panel is something you point at as well.
    if (questDialog_.up()) {
        interface_.add(questDialog_.canvas());
        interface_.add(questDialog_.body());
        interface_.add(questDialog_.tipCanvas());
    }
    if (squareResult_.up() && !watching_) interface_.add(squareResult_.canvas());
    if (amount_.up()) interface_.add(amount_.canvas());
    if (report_.up()) interface_.add(report_.canvas());
    if (whatsNew_.up()) interface_.add(whatsNew_.canvas());
    // A player's right-click menu over the windows it may open beside.
    if (playerMenu_.up()) interface_.add(playerMenu_.canvas());
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

// The guild window's column: the first free from the right -- beside the bag when it is up, and
// beside the character window too when both are.
int Desk::guildColumn() const {
    const bool first = characterOpen_ || inspecting();
    if (inventoryOpen_ && first) return 3;
    if (inventoryOpen_ || first) return 2;
    return 1;
}

void Desk::openInspect(Play& play, const std::string& name) {
    const uint32_t id = play.playerNamed(name);
    if (!inspectable(play.realm(), id)) return;
    if (name != inspectName_) {
        inspectFigure_.forget();
        inspect_.hide();
    }
    inspectName_ = name;
    inspectId_ = id;
    // His doll is photographed for the window (Play::wantDoll); the target frame shows whom it is about.
    play.wantDoll(id);
    inspect_.resetTurn();
    play.target(name, id);
    characterOpen_ = false;
    play.ui(Play::Ui::Opened);
    core::logf("window: inspecting %s (#%u)", name.c_str(), id);
}

void Desk::closeInspect() {
    if (!inspecting()) return;
    core::logf("window: inspect of %s shut", inspectName_.c_str());
    inspectName_.clear();
    inspectId_ = 0;
    inspect_.hide();
}

}  // namespace mu::game
