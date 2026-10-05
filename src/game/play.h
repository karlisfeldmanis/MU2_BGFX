// The window's view onto a realm: the tick on its own accumulator, a figure for every body the
// sim has, and the pointer that turns a click into a request.
//
// The whole of the rule this file exists to keep is that **it reads the sim and never
// second-guesses it**. Nothing here decides whether a blow lands, where something may stand or
// who is fighting whom; it asks where a body is, draws it there, and hands clicks back the
// other way. What it does own is presentation: the smoothing between two ticks, which clip a
// figure plays, and which way it is turned.
//
// It is deliberately NOT `game/crowd.cpp`'s Crowd. A Crowd stands figures where it chooses and
// idles them; these stand where the sim says and walk when the sim says they are walking. The
// two are different jobs and the second one is the game.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/ground.h"
#include "content/tables.h"
#include "game/fx/aura.h"
#include "game/fx/warp.h"
#include "game/fx/bones.h"
#include "game/fx/breath.h"
#include "game/fx/eyes.h"
#include "game/fx/staff_fire.h"
#include "game/fx/wing_motes.h"
#include "game/fx/shadow_stars.h"
#include "game/fx/snort.h"
#include "game/fx/dust.h"
#include "game/fx/forge.h"
#include "game/fx/bolt.h"
#include "game/fx/wave.h"
#include "game/fx/arrow.h"
#include "game/fx/blink.h"
#include "game/fx/ice.h"
#include "game/world/trap_show.h"
#include "game/fx/poison.h"
#include "game/fx/flame.h"
#include "game/fx/spirits.h"
#include "game/fx/firework.h"
#include "game/fx/thunder.h"
#include "game/fx/wheel.h"
#include "game/fx/fury.h"
#include "game/fx/hellfire.h"
#include "game/fx/storm.h"
#include "game/fx/inferno.h"
#include "game/fx/aqua.h"
#include "game/fx/deathstab.h"
#include "game/fx/firebreath.h"
#include "game/fx/meteor.h"
#include "game/fx/gleam.h"
#include "game/fx/streak.h"
#include "game/crowd.h"
#include "game/pets.h"
#include "game/wings.h"
#include "game/figures.h"
#include "game/fx/marker.h"
#include "game/fx/showing.h"
#include "game/sound.h"
#include "gfx/renderer.h"
#include "sim/audit.h"
#include "sim/realm.h"

namespace mu::game {

class Lamps;
class Ornaments;

class Play {
public:
    // A fight's Zen is said once, at its end: when this long has gone by with no pile taken, the
    // lane posts the sum (ui/tally.cpp) and the coins ring once (Play::takeZen) -- the user,
    // 2026-10-03, "play zen sound when combat is over and total zen is calculated". 0.05, from
    // 1.5, 0.6 and 0.2: "basically almost instantly after the combat", then "can we get
    // faster?". The wait no longer has to cover a spell's spread of falls, since it holds while
    // any kill's Zen is still owed (zenOwed), so all it is for is a few frames' grace.
    static constexpr float kZenQuietSeconds = 0.05f;
    // The arena (--arena), set BEFORE open() or not at all. It is not a second kind of realm
    // and not a bench: all it does is rewrite the map's nest table to one nest of one breed
    // beside where the hero is being put down, and the realm then raises that table exactly as
    // it raises the map's own -- same seeded placement, same rules, same events. Suppressing
    // the map's other spawns is therefore the same mechanism `--crowd N` uses on the still
    // crowd: fewer of the map's own monsters, chosen before anything is raised, rather than a
    // second switch that hides them afterwards.
    struct Arena {
        std::string breed;  // the figure or the label, as the cook writes them; see core/args.h
        int count = 1;
        // A skill the arena's hero is taught when he is raised, by MU's number (0 for none), and the
        // arena's hand attacks with it (Play::fight). For filming.
        int32_t learn = 0;
        // `--arena-undying`: the hero is never felled, so a long fight can be watched.
        bool undying = false;
        // `--peaceful`: no nests at all, breed or none -- a map to walk and run in with nothing
        // to rouse. For looking at locomotion.
        bool peaceful = false;
        // Where the fight happens, and why this tile. Lorencia is the only cooked world, and
        // this is the brightest of the flat, empty, non-safe patches on it -- the grass east of
        // the town, above the spider field. Chosen by reading four of the map's own files
        // together rather than by eye, and every one of the four ruled something out:
        //   * attributes.png -- the whole 13 by 13 patch, tiles 165..177 by 60..72, is the
        //     word 0. Nothing blocks, and no tile carries MU's SafeZone bit, which is not a
        //     nicety: Realm::press refuses to strike anything standing on a safe tile, so an
        //     arena inside the town square is a fight that never starts.
        //   * height.png -- 15 mm of relief across the whole patch, which is one height byte.
        //     A figure on a slope reads as leaning and the blood and the numbers sit wrong.
        //   * lorencia.json's 2845 placements -- the nearest is a Tree10 fourteen and a half
        //     tiles away, so nothing leans into the frame and nothing casts into it.
        //   * light.png -- MU's baked terrain light averages 209 of 255 here against 135 on the
        //     equally flat and empty patch north of the town, which was where this stood first
        //     and photographed as a fight in a brown twilight. The baked light multiplies the
        //     ground's albedo before any lighting (docs/conventions.md), so it decides the
        //     exposure of the whole shot and no lighting sheet will win it back.
        static constexpr int kColumn = 171;
        static constexpr int kRow = 66;
        // Half the side of the box the breed is scattered in, in tiles. Three puts every one
        // of them within a Budge Dragon's view range of the hero, so they rouse on the first
        // tick and the fight starts without the camera being walked anywhere.
        static constexpr int kSpread = 3;
    };
    void setArena(const Arena& arena) { arena_ = arena; }

    // `column` and `row` are where the character is put down; the realm moves him to the
    // nearest standable tile. False when the world has no cooked tables -- which is not fatal
    // to the run, only to playing it.
    // `heroLook` is the body the character is drawn in -- the naked class body with whatever
    // the game has put in his hands, built by Figures::dress. Null falls back to the cook's
    // own armoured Dark Knight, which is what every run before the game had.
    // `bare` is the naked class body's own name (Figures::dress's `base`), kept so a later
    // equip or unequip can dress the hero again in the same body it started in. Left empty,
    // as it is where nothing calls `open` with one, `redress` has nothing to re-dress with
    // and does nothing -- the hero keeps whatever he was handed at the door.
    bool open(const std::string& assetDir, const std::string& world, const content::Ground* ground,
              Figures* figures, uint64_t seed, int kin, int level, int column, int row,
              const std::string& weapon = "", const std::string& shield = "",
              const FigureBody* heroLook = nullptr, const std::string& bare = "");
    void shutdown();

    bool isOpen() const { return realm_.tables() != nullptr; }

    // Steps the sim as many whole ticks as the frame's own seconds have earned, on the sim's
    // own accumulator, and never more than a handful at once. Presentation is smoothed between
    // the two ticks either side of where the clock stands.
    void update(double seconds);

    // The pointer, and what a click does with it. The ray is cast against the land's own
    // height field rather than against a flat plane: the town is up to two metres of relief
    // and a flat-plane pick is a tile or two out wherever the ground is not level.
    void point(const gfx::Camera& camera, const float* view, const float* proj, float pixelX,
               float pixelY, int width, int height);
    void leftClick();   // walk to the tile under the pointer, or fight what is standing on it
    // The right button: a monster under it is attacked with the quick slot's skill, thrown
    // whenever it can be and the weapon swung when it cannot; anywhere else it stops him.
    void rightClick();
    // And held down after the press: a skill aimed at the ground goes on being cast there, the
    // next as soon as his arm comes down from the last (the user, 2026-10-04: "if i hold right
    // click it has to cast again"), and moved onto another monster it sets him on that one
    // ("allow me to hold right click and chancge monsters").
    void rightHeld();
    // What the right button's quick slot holds, by MU's skill number, 0 for nothing. The desk's
    // and handed down each frame, as the bar is the interface's and the order is the realm's.
    void setQuickSkill(int32_t skill) { quickSkill_ = skill; }
    // The arena's hand on the left button instead: `fight` then carries no skill.
    void setArenaLeft(bool left) { arenaLeft_ = left; }
    int32_t quickSkill() const { return quickSkill_; }
    // The same Attack request a click on a body raises, by id and with no pointer: the arena's
    // hand. It goes through `Realm::ask` like every other order and decides nothing itself.
    // Refused, silently, for a body that is not there or is already dead.
    void fight(uint32_t id);

    // `hover` collects the SAME drawables -- same transform, same palette row -- for whichever
    // body is `pointedAt()` or whichever townsperson is `pointedFolk()`, so the outline ring
    // can draw them a second time without a second pose. Null skips the collecting.
    //
    // `flashed` does the same for each monster flashing red because it has just turned on
    // him, and `flashes` says which run of it is whose and how bright: the newest
    // kFlashRings, so each has a ring of its own (gfx::kOutlineRings less the hover's).
    struct Flash {
        size_t from = 0, to = 0;  // into `flashed`
        float strength = 0.0f;    // the ring's opacity this frame, 0 to 1
    };
    static constexpr int kFlashRings = gfx::kOutlineRings - 1;
    void gather(gfx::Renderer& renderer, const float* viewProj, std::vector<gfx::Drawable>& out,
                std::vector<gfx::Drawable>* casters, std::vector<gfx::Drawable>* hover = nullptr,
                std::vector<gfx::Drawable>* flashed = nullptr,
                std::vector<Flash>* flashes = nullptr);

    // Where the camera should look, in tiles: the character, smoothed as he is drawn.
    void focus(float* column, float* row) const;
    // Where body `id` is drawn this frame, in tiles, as focus() is for him; false if it is not.
    bool shownAt(uint32_t id, float* column, float* row) const;

    const sim::Realm& realm() const { return realm_; }
    // The line under the map's name, "Level 2-40": its whole spawn table, taken before the
    // breeds not yet cooked are held back, so a world still waiting on its figures says what it
    // will hold. Empty for a world that spawns nothing.
    const std::string& zoneLevels() const { return zoneLevels_; }
    // The windows' requests, which the realm decides. A window never changes what it shows by
    // itself: it asks here and redraws from the realm afterwards (sprint 7, "a mirror").
    // One point into strength (0), agility (1), vitality (2) or energy (3), refused where
    // none is in hand -- OpenMU's IncreaseStatsAction, one point a press.
    bool spendPoint(int stat);
    // A drag from one slot to another, and a right-click on a carried thing. Both the realm's
    // to refuse; see sim/items.h for the gates.
    bool moveItem(int from, int to);
    bool useItem(int slot);
    // A jewel let go over a thing it goes on (sim::refinable). The realm rolls and spends it;
    // this is heard and re-dressed. See Realm::refine.
    bool refine(int jewelSlot, int targetSlot);
    // A drag let go over the world: the thing is thrown on the ground at his feet, where the
    // same Pick order that takes a kill's drop takes it back. The realm's to refuse, and the
    // figure is re-dressed when what was thrown came off him.
    bool discard(int slot);
    // A skill key pressed: throw this skill at whatever the fight is on, or at `at` when the
    // window knows a target. Not an order and it does not cancel one -- the realm spends the next
    // swing on it and the knight goes on fighting (docs/skills-dk.md §3.1a). The realm refuses
    // silently, so this returns nothing: the box's own sweep is what tells the player it is
    // cooling, and the plate reads that from the realm like everything else.
    void castSkill(int32_t skill, uint32_t at = 0);
    // Re-dresses the hero over the realm's own idea of what his hands and his back hold, so
    // the figure never shows a weapon the bag no longer does. Called after anything that can
    // change a worn slot; a no-op where `open` was given no `bare` to dress over.
    void redress();
    // The quiver's item name, off whichever hand holds ammunition, or empty; and the one the
    // figure was last dressed in, so a shot that empties or refills the hand redresses her.
    std::string quiverName() const;
    std::string dressedQuiver_;
    int mixAnswer_ = -1;
    std::string mixWords_;
    // Puts things in his bag by the asset's name, for a scripted run: `--give Potion02:3`.
    // `count` is a stack's size for a potion and ignored for anything else.
    // `extras` is `+N` for a plus, `L` for luck and `O` then a digit for the option: +3LO2.
    bool give(const std::string& name, int count, const std::string& extras = "");
    // --lay's: a thing laid on the ground beside him, heard landing. See Realm::lay.
    bool lay(const std::string& name);
    // The open counter's requests: a purchase by shelf slot, a sale by bag slot, and walking
    // away. Each the realm's to refuse.
    bool buy(int shelfSlot);
    bool sell(int bagSlot);
    // The shelf's undo: the newest sale taken back at what it fetched (Realm::buyBack).
    bool buyBack();
    // A mending counter's two: one thing by its slot, worn or in the bag, and everything.
    // Heard as MU's SOUND_REPAIR when the realm takes the Zen.
    bool repair(int slot);
    bool repairAll();
    void closeTrade() { realm_.closeTrade(); }
    // The vault, as the realm keeps it: each a request answered yes or no, logged and heard
    // as the bag's own moves are.
    bool deposit(int bagSlot, int cell);
    bool withdraw(int cell, int bagSlot);
    bool rearrange(int from, int to);
    bool depositZen(int64_t zen);
    bool withdrawZen(int64_t zen);
    void closeVault() { realm_.closeVault(); }
    void restoreVault(const sim::Vault& saved) { realm_.restoreVault(saved); }
    // The Chaos Machine (sim/machine.h), as the realm keeps it: the vault's three moves, and the
    // mix, heard as MU hears its answer -- eMix with eGem for a success, with eBreak for a
    // failure (ReceiveMixExtended, ReceiveTradeInventoryExtended).
    bool putIn(int bagSlot, int cell);
    bool takeOut(int cell, int bagSlot);
    bool shuffle(int from, int to);
    bool mix(sim::Service service, int socket);
    void closeMachine() { realm_.closeMachine(); }
    void restoreMachine(const sim::Machine& saved) { realm_.restoreMachine(saved); }
    // The last mix's answer while it stands: 1 made, 0 failed, -1 none since the box was last
    // filled or closed. The window's line in place of the recipe.
    int mixAnswer() const { return realm_.mixing() >= 0 ? mixAnswer_ : -1; }
    // And what it was, in words: the service's success or failure line as it was judged.
    const std::string& mixWords() const { return mixWords_; }
    // A quest giver's dialog (sim/quests.h): accept, hand in with a choice, walk away. The wall
    // clock a repeating quest waits on is handed to the realm each frame (Realm::setWallClock).
    bool acceptQuest(int quest);
    bool completeQuest(int quest, int choice);
    void closeQuest() { realm_.closeQuest(); }
    // The travel list (M, game/ui/travel.h): the realm checks the row and takes the Zen, and
    // the map change is the mode's, as a gate's is (`takeTravel`, app/modes/play_mode.cpp).
    bool travel(int index);
    // The row paid for since the mode last asked, once, or -1.
    int takeTravel() {
        const int row = travelled_;
        travelled_ = -1;
        return row;
    }
    void setWallClock(int64_t unixSeconds) { realm_.setWallClock(unixSeconds); }
    void openCastleDoor() { realm_.openCastleDoor(); }
    void freeCastle() { realm_.freeCastle(); }
    void dropCastleBridge(int seconds) { realm_.dropCastleBridge(seconds); }
    void setCastle(int castle) { realm_.setCastle(castle); }
    // The Messenger's page's two answers (QuestDialog::kGate).
    bool enterCastle(int castle) { return realm_.enterCastle(castle); }
    void closeGate() { realm_.closeGate(); }
    // The Archangel's page's answers (QuestDialog::kArchangel).
    bool handInStaff() { return realm_.handInStaff(); }
    bool claimCastle() { return realm_.claimCastle(); }
    void closeAngel() { realm_.closeAngel(); }
    // Zen, for a scripted run (`--zen`), and a walk to a townsperson by name (`--talk`): the
    // same Talk request a click on him raises.
    void earn(long long zen) { realm_.earn(zen); }
    bool talkTo(const std::string& name);
    // ---- what he gained this frame (sprint 12) --------------------------------------------
    //
    // Experience, Zen and a potion are HIS and belong to no body on the map, so they are kept
    // apart from the blows and drawn in their own lane over the HUD -- the design page of
    // 2026-09-23, concept B. The list holds what this frame's ticks said and is cleared at the
    // top of every update, so whoever draws it runs after Play and reads it once. Nobody
    // reading it is a run with no window, which is the headless case and costs nothing.
    // `Died` is the one of these that is not a gain: it is here because the LANE is where it
    // is said, and a second channel from the realm to the same three square inches of screen
    // would be two things to keep in step for no gain.
    struct Gain {
        enum class Kind : uint8_t { Experience, Zen, Health, Mana, Died };
        Kind kind = Kind::Experience;
        int64_t value = 0;
    };
    const std::vector<Gain>& gains() const { return gains_; }
    // The skill the hero threw on this frame's ticks, by its number, or 0: the realm's yes to a
    // key, which a press alone is not -- the press is held until he is in reach, and refused
    // while the skill cools. What the skill box rings on.
    int32_t heroCast() const { return heroCast_; }

    const sim::Findings& findings() const { return findings_; }
    int pointedColumn() const { return pointedColumn_; }
    int pointedRow() const { return pointedRow_; }
    uint32_t pointedAt() const { return pointedAt_; }
    // The townsperson under the pointer, as an index into the tables' folk, or -1.
    int pointedFolk() const { return pointedFolk_; }
    // The thing on the ground under the pointer, by its id, or 0.
    uint32_t pointedLying() const { return pointedLying_; }
    // The pointer is on this drop's NAME, which outranks anything the ray found: the plate is
    // drawn over the world, so what the eye is on is the name and not the monster behind it
    // (the user, 2026-09-28). Called after `point`, with the desk's `labelUnder`; 0 does nothing.
    void pointAtLabel(uint32_t lying);
    // Something to sit on or lean against under the pointer, as an index into the tables'
    // perches, or -1. Tested last, after bodies, drops and townsfolk.
    int pointedPerch() const { return pointedPerch_; }
    // The same Perch request a click on one raises, by index and with no pointer: `--perch`.
    bool perch(int index);

    // Where a monster's health bar sits on screen: the pixel a third of a tile over the top
    // of its body as drawn this frame, MU2's Crowd.Crown. False when the body has never been
    // placed or the point is behind the camera.
    bool crownOf(uint32_t id, const float* viewProj, int width, int height, float* x,
                 float* y) const;
    // And the same over a townsperson, by the tables' folk index: where the name goes.
    bool folkCrownOf(int folk, const float* viewProj, int width, int height, float* x,
                     float* y) const;
    // The body a townsperson is in the realm -- a guard's -- or 0 for one who stands still.
    uint32_t wardenBody(int folk) const;
    // The townsfolk with a quest to give, by the tables' folk index: the ones the quest marker
    // floats over. Marlon, today -- MuMain's MONSTER_MARLON, the one NPC its interface opens the
    // quest dialogue for. Whether he still has one to give is the quest's to say when it lands.
    static constexpr int32_t kQuestGiver = 229;
    const std::vector<int>& questGivers() const { return questGivers_; }

    // What the guards are saying: who, the line, and how long it has been up, in seconds. Put
    // up by a `Shouted`, newest last, one line a speaker (a new one replaces his last), and
    // taken down after `kSaidSeconds`. The words are chosen here, off the monster it is about.
    struct Said {
        uint32_t who = 0;
        std::string line;
        float age = 0.0f;
        int folk = -1;  // said by a townsperson who stands as no body (Lumen at her bar)
    };
    static constexpr float kSaidSeconds = 4.0f;
    const std::vector<Said>& said() const { return said_; }
    // A body's health as the DRAWING has shown it: the realm's, with every blow still waiting
    // for its landing cue added back, and nought once it is dead. See Showing::owed.
    int32_t shownHealth(uint32_t id) const;
    // Alive as the DRAWING has it: the realm's alive, or dead on the tick with the fall still
    // waiting for the killing blow to land. What the health bar reads, so it is not taken
    // away a swing before the blow that emptied it.
    bool shownAlive(uint32_t id) const;
    // Seconds until the realm raises him at the gate, off the tick clock as it stands this frame;
    // negative while he is alive. The step that raises him is the one whose tick reaches
    // `risesAt`, and the next step is `kTickSeconds - accumulator_` away.
    float heroRisesIn() const;
    // The enter gate he went through (sim::What::Gated) and the tile he comes out on in the
    // map it leads to, or 0 while he has gone through none. Held until the world closes: the
    // map change is the mode's (app/modes/play_mode.cpp).
    int32_t gated(int* column, int* row) const {
        *column = gatedColumn_;
        *row = gatedRow_;
        return gated_;
    }
    // Everybody standing in the field: the feet of every body that is placed, in view and
    // alive as drawn, as (x, ground height, z, 0), at most `most` of them, the hero first.
    // Returns how many were written. For the grass, which parts round whoever walks in it;
    // a corpse parts nothing, since it lies on the sward rather than standing in it.
    int walkers(float* out, int most) const;
    // The hero as a save keeps him, and laid back on a hero just raised -- see Realm::restore.
    // restore() also dresses the figure in what the record wears.
    sim::HeroRecord record() const { return realm_.record(); }
    void restore(const sim::HeroRecord& saved);
    // The drops on the ground in the realm that the drawing is still holding back, because
    // the monster that dropped them has not finished falling. Litter skips them, and so does
    // the pointer.
    const std::vector<uint32_t>& heldDrops() const { return heldIds_; }
    // Whether a killed body's Zen is still held for its fall: the fight's sum waits on it.
    bool zenOwed() const { return zenOwed_; }

    // Where each thing on the ground is on screen this frame, for its label: the id, and the
    // pixel a little above where it lies. Only those in front of the camera.
    struct OnScreen {
        uint32_t id = 0;
        float x = 0.0f, y = 0.0f;
    };
    void dropsOnScreen(const float* viewProj, int width, int height,
                       std::vector<OnScreen>& out) const;
    // Which drops have landed and lie still, from Litter::settled: only those are labelled.
    void setSettledDrops(const std::vector<uint32_t>& ids) { settled_.assign(ids.begin(), ids.end()); }
    int64_t ticks() const { return realm_.tick(); }
    double tickMs() const { return tickMs_; }
    // What the last line of the log said, so the run can be read without a HUD. Sprint 6 draws
    // these; sprint 5 writes them down.
    const std::string& lastLine() const { return lastLine_; }

    // What a blow looks like. Opened by the caller rather than by Play::open, because the
    // sheets need a device and Play is given an asset directory and no Textures -- and
    // threading one through world.cpp for two texture loads would be a worse trade than
    // saying here that it is opened second. A Showing that never opened draws nothing and
    // the fight is otherwise unaffected.
    Showing& showing() { return showing_; }
    const Showing& showing() const { return showing_; }
    // The character comes in: he is not there for `delay` seconds and then dissolves in, with
    // a warm edge, over kAppearSeconds. Called once the preloader has faded the frame up;
    // never called, he is simply there, which is what every review run wants.
    void appear(float delay) {
        appearing_ = true;
        appearAt_ = -delay;
    }
    // Where a click sent him. Opened by the caller for the same reason as the showing.
    Marker& marker() { return marker_; }
    void gatherMarker(gfx::Effects& effects) const {
        if (ground_) marker_.gather(effects, *ground_);
    }
    // What a level looks like, and sounds like. Opened by the caller for the same reason as
    // the showing.
    Aura& aura() { return aura_; }
    // Where a Town Portal Scroll lands him, opened by the caller for the same reason.
    Warp& warp() { return warp_; }
    void gatherWarp(gfx::Effects& effects) const {
        if (ground_) warp_.gather(effects, *ground_);
    }
    // Whether he has warped since this was last asked, and forgets it: what the windows shut
    // on, as MuMain's ReceiveTeleport shuts every one (`g_pNewUISystem->HideAll()`).
    // A Town Portal read where the map has no safe zone (the Dungeon): Lorencia is owed, and
    // the map change is the mode's, as a gate's is. Once.
    // And Blood Castle's run over and its rest out (sim::CastleRun::sentOut): Devias.
    bool takeHome() {
        const bool was = homeOwed_ || realm_.castleRun().sentOut;
        homeOwed_ = false;
        return was;
    }
    // Where the last Town Portal Scroll was read -- the tile and the way he faced -- once, or
    // false: what Go Back! takes him back to (app/modes/play_mode.cpp). A scroll is refused in a
    // safe zone, so this is always the field.
    bool takePortalFrom(int* column, int* row, float* facing) {
        if (portalFrom_[0] < 0) return false;
        *column = portalFrom_[0];
        *row = portalFrom_[1];
        *facing = portalFacing_;
        portalFrom_[0] = portalFrom_[1] = -1;
        return true;
    }
    // Go Back!: put down on a tile of this same map as a Town Portal lands, facing `facing` --
    // the realm's setHeroDown, and the landing drawn and heard as a warp's (`warped`).
    void goBack(int column, int row, float facing);
    // The performance sweep's (app/sweep.h): the realm's setHeroDown and nothing drawn or heard,
    // so the warp's ring and sound are not in the frames being measured.
    void setDown(int column, int row) { realm_.setHeroDown(column, row, 0, 100); }
    // A warp's landing heard and seen where he stands, and nothing else: sMagic and the ring, for
    // a map come into by magic -- a Tab trip, a Town Portal to another map, Go Back! -- where the
    // world was raised around him rather than him set down in it.
    void landed();
    bool takeWarp() {
        const bool was = warpOwed_;
        warpOwed_ = false;
        return was;
    }
    // The guard: thrown when Defense lands, and kept on the body every frame after.
    void guardRise(float seconds);
    void guardStep();
    // The realm tick a `showGuard` preview holds the cage until; zero when none is running.
    int64_t guardPreview_ = 0;
    Sound& sound() { return sound_; }
    // What a Budge Dragon gives off, opened by the caller for the same reason as the showing.
    Breath& breath() { return breath_; }
    // What a skeleton leaves, opened by the caller for the same reason as breath.
    Bones& bones() { return bones_; }
    void gatherBones(std::vector<gfx::Drawable>& out) const { bones_.gather(out); }
    // The Lich's meteorite: opened by the caller for the same reason as breath.
    Meteor& meteor() { return meteor_; }
    // The wizard's Energy Ball: let go on `Loosed`, flown until its `Hit` arrives. fx/bolt.h.
    Bolt& bolt() { return bolt_; }
    // The wizard's Power Wave, opened beside the bolt. fx/wave.h.
    Wave& wave() { return wave_; }
    Arrows& arrows() { return arrows_; }
    // And his Lightning. fx/thunder.h.
    Thunder& thunder() { return thunder_; }
    // And his Teleport's sparks. fx/blink.h.
    Blink& blink() { return blink_; }
    // And his Ice. fx/ice.h.
    Ice& ice() { return ice_; }
    // The Dungeon's traps, drawn (game/world/trap_show.h).
    TrapShow& trapShow() { return trapShow_; }
    // And his Poison. fx/poison.h.
    Poison& poison() { return poison_; }
    // And his Flame. fx/flame.h.
    Flame& flame() { return flame_; }
    // And Evil Spirit's spirits round him, the spell's and the shield rune's. fx/spirits.h.
    Spirits& spirits() { return spirits_; }
    // And a thrown Firecracker's firework over the tile it opened on. fx/firework.h.
    Firework& firework() { return firework_; }
    // The knight's Twisting Slash: his weapon flung round him. fx/wheel.h.
    Wheel& wheel() { return wheel_; }
    // The knight's Rageful Blow: his weapon thrown down and the ground broken. fx/fury.h.
    Fury& fury() { return fury_; }
    // The wizard's Hellfire: the sigil and the wall of fire at his feet. fx/hellfire.h.
    Hellfire& hellfire() { return hellfire_; }
    // The wizard's Twister: the whirlwind walked out ahead of him. fx/storm.h.
    Storm& storm() { return storm_; }
    // The wizard's Inferno: the ring of blasts round him. fx/inferno.h.
    Inferno& inferno() { return inferno_; }
    // The wizard's Aqua Beam: the line of water out ahead of him. fx/aqua.h.
    Aqua& aqua() { return aqua_; }
    // The knight's Death Stab: its streaks, cones and the wound it leaves. fx/deathstab.h.
    DeathStab& deathStab() { return deathStab_; }
    // The Dinorant's Fire Breath: its sprays, its haze and its burst. fx/firebreath.h.
    FireBreath& fireBreath() { return fireBreath_; }
    void gatherBolt(gfx::Effects& effects, const float eye[3]) const { bolt_.gather(effects, eye); }
    // The bolt bench (`--bolt-every`): one thrown from where he stands at a point `tiles` east,
    // drawing only -- the realm is not asked and nothing is hit. What the trail and the arrival
    // are tuned on.
    // `acrossX, acrossZ` is the flat direction it is thrown in, screen-right by the caller.
    void benchBolt(float tiles, float acrossX, float acrossZ, int32_t skill);
    // And before it, a step that way, so he stands facing the throw: the bench asks the realm
    // for an ordinary walk of one tile, and the body turns as it always does.
    void benchFace(float acrossX, float acrossZ);
    // `--walk-to`: one walk to a tile, as a click on the ground there would ask for.
    void walkTo(int column, int row);
    void gatherMeteor(gfx::Effects& effects, const float eye[3]) const { meteor_.gather(effects, eye); }
    // The blade's ribbon behind a skill swing. Fed in `show`, off the pose the frame has already
    // computed -- see fx/streak.h, which is MU's own `CreateWeaponBlur` rung for a skill.
    Streak& streak() { return streak_; }
    void gatherStreak(gfx::Effects& effects) const { streak_.gather(effects); }
    // A refined hero's gear as a light source: fx/gleam.h. Fed in `show`.
    Gleam& gleam() { return gleam_; }
    // Hanzo's forge: the sparks off his anvil and his hearth's smoke. Opened by the caller for
    // the same reason as breath; fed in `smithy`.
    Forge& forge() { return forge_; }
    // A Bull Fighter's snort and an Elite's eyes: opened by the caller for the same reason as
    // breath; fed in `snort`.
    Snort& snorts() { return snort_; }
    Dust& dust() { return dust_; }
    Eyes& eyes() { return eyes_; }
    StaffFire& staffFire() { return staffFire_; }
    WingMotes& wingMotes() { return wingMotes_; }
    // The Shadows' stars: opened by the caller as the eyes are; fed in `shade`.
    ShadowStars& shadowStars() { return shadowStars_; }
    // The townsfolk's RenderLight sprites, in `sheet` (the showing's `light`, MU's BITMAP_LIGHT):
    // `(1, 0.6, 0.4) * (sin(WorldTime * 0.002) * 0.3 + 0.7)`, one per glowing person.
    void setFolkLight(bgfx::TextureHandle sheet) { folkLight_ = sheet; }
    // The showing's `shiny_02`, MU's BITMAP_SHINY + 1: the Gorgon Staff's star.
    void setStarSheet(bgfx::TextureHandle sheet) { starSheet_ = sheet; }
    // Charon's orb and its wisps: the showing's `lightning_2` and `joint_energy`.
    void setFolkOrb(bgfx::TextureHandle orb, bgfx::TextureHandle wisp) {
        orbSheet_ = orb;
        wispSheet_ = wisp;
    }
    void gatherFolkLights(gfx::Effects& effects) const;
    void gatherForge(gfx::Effects& effects, const float eye[3], const float near[3],
                     float daylight) const {
        forge_.gather(effects, eye, near, daylight);
    }
    // And the light off each smith's coals, into the lamps' static set: he never moves, so it
    // belongs in the grid with the braziers rather than among the lights that travel. Called
    // once, after the lamps open and before they are handed to the renderer.
    void lightForges(Lamps& lamps) const;
    // Opens the sound and loads what this realm can say: the level-up, and every breed's
    // attack, death and wandering cries, found once per body as its clips are. Opened by the
    // caller after the showing, whose table the events are read from. Not fatal.
    void openSound(const std::string& assetDir, bool muted);
    // Where the ears are this frame: on the ground under the character as he is drawn,
    // turned by the camera's heading. MU's Update3DPositions, with the voices that follow a
    // body moved to where it is drawn now.
    // `indoors` is whether the character's tile is under a roof (World::indoors), which is
    // what switches the wind off -- the same read that lifts the roofs, so the two agree.
    void hear(const gfx::Camera& camera, bool indoors);
    // The world's own water and fire, after hear(): the nearest bonfire's crackle and the
    // nearest fountain's drip, each one loop levelled and panned from that place, on within
    // its reach of the character and off past it. Ours. `lamps` is null with the lamps off.
    // `inside` says whether a point in metres is under a roof (World::indoors): a fire burning
    // indoors is heard only by a character indoors too, and never through the wall (the user,
    // 2026-09-29).
    using Inside = bool (*)(void* context, float x, float z);
    void hearWorld(const Lamps* lamps, const Ornaments& ornaments, Inside inside = nullptr,
                   void* context = nullptr);
    // The interface's own noises, raised by the windows: a button acknowledging the finger
    // (SOUND_CLICK01), a request the realm said no to (iButtonError), and a thing going into
    // a slot -- MU has no equip or bind sound of its own and plays SOUND_GET_ITEM01 for both.
    // Opened is SOUND_INTERFACE01, which a merchant's counter opens on beside the click.
    enum class Ui { Click, Refused, Took, Opened };
    void ui(Ui which);
    void gatherAura(gfx::Effects& effects, const float eye[3]) const {
        if (ground_) aura_.gather(effects, *ground_, eye);
    }
    // Throws the level-up on the hero where he is drawn now. What a `Levelled` does once the
    // blow that earned it has landed, and what `--rise` does for a review run.
    void rise();
    // And the orb's: the ribbons and the swoosh together, thrown by `useItem` when what was
    // read taught something, and by `--learn` for a review run.
    void learned();
    // And the guard's cage for `--guard` -- the knight's Defense and the wizard's Soul Barrier
    // wear the same one -- held for the two seconds a real cast shows it, whatever the realm says.
    // Only the drawing, like `--learn`: no boon is raised and no mana is spent.
    void showGuard();
    // And a Town Portal Scroll's arrival: the hero faded in from nothing where the realm put
    // him, the warp thrown under him, and the windows told (`takeWarp`).
    void warped();

private:
    // One body as it is drawn: the figure, and where it was at the last two ticks so a frame
    // between them can be interpolated.
    struct Drawn {
        Figure figure;
        uint32_t id = 0;
        float wasX = 0.0f, wasY = 0.0f;
        float nowX = 0.0f, nowY = 0.0f;
        // And which way it was pointing at those same two ticks. The facing is interpolated for
        // the same reason the position is, and it matters MORE: the sim turns a body up to 45
        // degrees in one tick (900 degrees a second at 20 Hz), so a facing read raw off the sim
        // snaps through a quarter turn every 50 ms while the position glides. At 180 fps that
        // is one jump every nine frames, and it is the stutter the first person to play this
        // reported.
        float wasFacing = 0.0f, nowFacing = 0.0f;
        // Where it was drawn when a click cut a tick short; see Play::update.
        float caughtX = 0.0f, caughtY = 0.0f, caughtFacing = 0.0f;
        float yaw = 0.0f;
        // The top of the body where follow() last stood it, in world metres, and whether it
        // has been stood anywhere yet. What the health bar is hung from; a corpse keeps the
        // last one, since follow() stops placing it.
        float crown[3] = {0.0f, 0.0f, 0.0f};
        bool placed = false;
        bool visible = false;
        int attackClip = -1;     // Attack 1, this body's first swing, found once at open
        int attackClip2 = -1;    // Attack 2, the second swing; -1 for breeds that have none
        // A knight with a weapon in each hand swings both, right 1, left 1, right 2, left 2 by
        // the swing counter (sim::attackActions, MuMain ZzzCharacter.cpp:1166-1173). -1 when he
        // holds fewer than two, and then attackClip is his swing as before.
        int dualClips[4] = {-1, -1, -1, -1};
        int deathClip = -1;      // MONSTER01_DIE, found once at open the same way
        // How much of it is there at its fullest: MU's AlphaTarget, 1 but for the Ghost's 0.4.
        float seeThrough = 1.0f;
        // The Poison Bull's standing eDeBuff_Poison: drawn in the poisoned green always.
        bool venomous = false;
        // A murderer, lit red always (kMurdererLight): the Cursed Wizard.
        bool murderer = false;
        // A Shadow's joints, which wear its stars every frame it is drawn (fx/shadow_stars.h), and
        // whether it is the Poison Shadow; empty on everything else.
        std::vector<int> shadeBones;
        bool shadePoison = false;
        // A Death Gorgon: its bones throw embers and it lights orange, rather than wear stars.
        bool embers = false;
        float emberOwed = 0.0f;
        // Its faint light (kAuraLights): the bone it hangs on, -1 for none, and its colour.
        int auraBone = -1;
        float auraColour[3] = {0.0f, 0.0f, 0.0f};
        // The one bone its embers rise off (the Death Knight's pelvis), -1 for a random one.
        int emberBone = -1;
        float emberEvery = 0.0f;   // reference frames between embers
        // The Devil's two hands, its beams' ends; -1 on everything else.
        int handBones[2] = {-1, -1};
        // What its beams are: the Devil's laser, the Vepar's soft blur (kVeparFigure, two a
        // hand straight to the target) or the Lizard King's lightning (kLizardKingFigure, three
        // a hand wandering round it). The last two throw no embers. Or the Hydra (kHydraFigure),
        // which throws none and wears MU's flare on its gem.
        enum class Beams : uint8_t { Laser, Blur, Thunder, Horn };
        // The Hydra's BlendMesh, its four breath beams (kHydraBeamMaterial): never drawn, its
        // heads' red lightning in their place; -1 on everything else.
        int beamMaterial = -1;
        // Its four heads' bones (kHydraHeads), -1 when missing, and the head the next bolt
        // leaves: one after another round the four.
        int headBones[4] = {-1, -1, -1, -1};
        int nextHead = 0;
        Beams beams = Beams::Laser;
        // MU's SwordCount, incremented on each swing. `swordCount % 3 == 0` plays Attack 1,
        // the rest Attack 2 — ZzzCharacter.cpp:1269-1276. The drawing's own counter, not the
        // sim's: it draws from no seeded state.
        uint32_t swordCount = 0;
        // The skill a Cast said this tick, and the clip it plays: set when the cast arrives and
        // read by the Hit that follows it on the same tick, so the blow is drawn with the skill's
        // own animation instead of the weapon's. MU does the same thing the other way round --
        // `UseSkillWarrior` sets the hero's action before the request even goes out -- and MU2
        // found that taking the weapon's clip over the top of a cast was what killed its sparks.
        int32_t castSkill = 0;
        int castClip = -1;
        // The clip a buff -- a cast on himself -- was started with, while it is the one playing:
        // on a horse only its arms are drawn, over the ride's seat (play_show). -1 for none.
        int selfClip = -1;
        // Seconds of a cast's own animation still owed. A SWING is cancelled by a step -- see
        // play_show, where that rule and its measurement live -- and a SKILL is not: the realm
        // holds the character still for the whole clip (Realm::throwSkill takes the longer of
        // the swing and the clip), so a skill cut off by the walk that starts after it is the
        // drawing contradicting the rules. It was also what made a skill read as instant.
        float casting = 0.0f;
        // A blow of this body's is in the air: its clip was started by a `Swung` and the `Hit`
        // that follows lands at the moment the arm is already coming down, so the cue's fuse is
        // nought rather than half a swing. False for a monster, which still resolves its blow on
        // the tick it decides it and is drawn exactly as it always was.
        bool landing = false;
        // A monster's own sound events, as Sound handles, found once at openSound: its breed's
        // `_attack`, `_die` and `_move` by MU2's naming (the label lowered, no spaces). -1 for
        // the character and for a breed with nothing cooked, which is silence.
        int cryAttack = -1, cryDie = -1, cryMove = -1;
        // MONSTER01_SHOCK (slot 5): on the meteor's quake, silently, and on one landed blow in
        // two, crying its attack pair -- MU has no shock sound of its own: a model's
        // `Sounds[]` are idle/move, the attack pair and the death, and the cooked `_shock`
        // event for a breed is its attack pair under a second name. -1 for breeds that lack
        // the clip.
        int shockClip = -1;
        // A Budge Dragon: its head bone, which the fire comes out of, and what of a reference
        // frame's spark and a fourth of one's puff is owed. See Play::exhale.
        // A body that does not fall but comes apart: MU's SetPlayerDie makes eleven bones of
        // a skeleton and stops drawing the model on the same instruction. Found once, by the
        // figure's name, as the dragon's own case is -- MU keys it on the MODEL, not on the
        // breed, and two of its monsters share this body.
        bool bursts = false;
        bool crumbles = false;  // and into stones, not bones: the Stone Golem (Bones::rubble)
        bool breathes = false;
        // A Giant, whose death throws up sand: MU's MonsterDieSandSmoke, keyed on the MODEL as
        // the two above are. `sandOwed` is what of a reference frame's twenty puffs is left to
        // throw, and it is reset the moment the death clip leaves the window rather than
        // carried -- see Play::sandOnDeath.
        bool sands = false;
        // Latched the frame its death clip crosses key 8, and cleared if it ever stands again.
        // MU throws twenty puffs once, not twenty a frame -- see Play::sandOnDeath.
        bool sanded = false;
        // An Ice Monster: at the end of its death clip it is put out and bursts into ten ice
        // shards (CreateBlood). `shattered` latches it, cleared if it stands again.
        bool shatters = false, shattered = false;
        int headBone = -1;
        float fireOwed = 0.0f, dustOwed = 0.0f;
        // A Bull Fighter, either variant: smok_bone, which it snorts out of, and what of half
        // a reference frame's puff is owed. The Elite also has its two eye bones, MU's 22 and
        // 23; -1 on everything else. See Play::snort.
        int snortBone = -1;
        int eyeBones[2] = {-1, -1};
        float snortOwed = 0.0f;
        // An Elite Yeti's breath: the same puff out of Box03, in every action, one in four
        // reference frames rather than one in two within the bull's windows.
        bool snortAlways = false;
        // The Chain Scorpion's BITMAP_LIGHT at bone 7, `light_point` (ZzzCharacter.cpp:6151);
        // -1 on everything else. Drawn in gatherFolkLights.
        int lightBone = -1;
        // The Gorgon's staff star, on its knife_gdf; -1 on everything else. gatherFolkLights.
        int starBone = -1;
        // Negative while alive. Set to 0 the tick `Died` happens and counted up from there, so
        // the corpse holds its last pose and fades instead of vanishing on the tick it falls --
        // see kDeathHold and kDeathFade in play.cpp.
        float deadFor = -1.0f;
        int32_t health = 0;  // the body's health before the blows of the tick being read
        // Dead on the tick and not yet fallen on screen: the blow that killed it is still
        // being swung, and the fall waits for that blow's landing cue. See Play::fallWhenLanded.
        bool fallOwed = false;
        // Counted up from 0 the tick `Rose` happens, so a respawn eases in rather than popping
        // into being; left far above kSpawnFadeSeconds otherwise, which reads as "done fading".
        float spawnFade = 1e9f;
        float swinging = 0.0f;   // seconds of it left to play before idle or walk take over
        // Seconds his right hand stays empty: Rageful Blow's weapon is in the air while its clip
        // is under its fourth key (ZzzCharacter.cpp:10079). fx/fury.h.
        float handEmpty = 0.0f;
        float swingPace = 1.0f;  // how much faster than authored the swing clip must run
        // Seconds of a flinch left to play, held as a swing is -- without it the idle took the
        // shock clip back on the next frame. A swing or a step ends it. See Play::flinch.
        float shocked = 0.0f;
        // Evil Spirit's spin (kSpiritStormTime): MuMain's StormTime, in its 25 fps frames, and
        // the turn it has put on the body over its facing, in radians.
        float stormTime = 0.0f;
        float spin = 0.0f;
        // Seconds his weapon stays slung on his back: a guard's salute is given with the hand
        // that holds it, and with the crossbow in it the salute was him aiming at Marlon.
        float stowed = 0.0f;
        // The walk. `groundSpeed` is what the last tick actually covered, in metres a second,
        // and is the numerator of the clip's rate; `still` is how long it has covered nothing,
        // which is what decides whether a stop is a stop or a stumble; `walkPhase` is where
        // the cycle was when the walk was last left, so it can be resumed rather than restarted.
        float groundSpeed = 0.0f;
        float still = 0.0f;
        float walkPhase = 0.0f;
        float clipRate = 1.0f;   // what this figure's clip runs at this frame
        // Counted up every time this body starts a swing. A landing cue carries the token of
        // the swing it belongs to, and a cue whose token no longer matches drops itself --
        // which is what "gated on the clip still being the swing" means. A step cancels a
        // swing here, so a cue really does get dropped in ordinary play.
        uint32_t swingToken = 0;
        // What threw the swing this token belongs to: the `Swung`'s own skill number, or 0 for
        // the weapon. Kept here because a player's blow is said in two halves -- the swing on
        // one tick and the damage on another -- and the figure that goes up at the landing has
        // to know which step of the ramp it is. An area skill says one `Swung` and a `Hit` per
        // body, so one field answers for all of them.
        int32_t swingSkill = 0;
        // The swing whose release has sounded its hit: a Meteorite says `Loosed` once per body
        // it falls on and a channel once per strike, and MU plays the hit once, at the release.
        uint32_t heardToken = 0;
        // A spell's hit, owed by its release and paid where it first strikes (Play::update): the
        // tick each is due to land by, 0 for none. Four, because the next bolt can be let go
        // before the last one lands; a miss lapses rather than paying for the next.
        int64_t owedHits[4] = {};
    };

    Drawn* drawnOf(uint32_t id);
    // The hero's four alternating swings into `drawn.dualClips` while he holds two weapons.
    void dualSwings(Drawn& drawn, const FigureBody* look) const;
    // Where a spell leaves a caster thrown at `to`: the middle of his chest, a little toward it.
    // False when there is no figure to measure, and `out` is then his feet.
    bool castFrom(const Drawn& caster, const float to[3], float out[3]) const;
    // And an archer's arrow at `to`, from MU's muzzle, in the model her weapon throws.
    void shootArrow(const Drawn& shooter, const float to[3], uint32_t whom, float seconds = 0.0f,
                    bool pierce = false);
    // A Hunter's blow drawn as MU draws it: CreateArrows off its MODEL_ARQUEBUS, which throws
    // MODEL_ARROW_SAW (ZzzCharacter.cpp:4831, ZzzEffectMagicSkill.cpp:225). The rules resolve
    // the blow at range on the tick and are not told; this only draws the bolt it would be.
    // And a guard's the same way when her figure holds a bow or a crossbow: Noria's watch and
    // Lorencia's Crossbow Guard (the user's, 2026-09-29: "defend city with arrows").
    void volleyShot(uint32_t shooter, uint32_t target);
    // Its bolts waiting for the release key: MU looses a monster's shot when its AttackTime
    // reaches 15 frames (ZzzCharacter.cpp:4140, g_iLimitAttackTime), and the blow is shown
    // where the bolt lands -- its cue is rushed by Arrows::landed, as a meteor's impact rushes.
    struct Volley {
        uint32_t shooter = 0, target = 0;
        float wait = 0.0f;  // seconds to the release
    };
    std::vector<Volley> volleys_;
    // The hero's shots, held from the realm's `Loosed` until the string in her hands goes
    // (Figure::released, MU2's release fuse): the arrow leaves the weapon with the string and
    // flies what is left of the realm's flight, so it still lands on the tick it lands. A fan's
    // arrows fly at their own speed toward the points they were aimed at.
    struct Nocking {
        uint32_t shooter = 0, whom = 0;
        float to[3] = {0.0f, 0.0f, 0.0f};  // a fan arrow's far point; a shot reads its body
        float air = 0.0f;                  // seconds of the realm's flight, 0 for a fan's
        float waited = 0.0f;
        // Hers to sound: the string, eBow or eCrossbow, started its onset ahead of the release
        // so it is heard as the arrow leaves (MU2's Crowd.Shot). One of a fan's three.
        bool sound = false;
        // Penetration's: wound in MODEL_PIERCING's bands (fx/arrow.h), with its SOUND_FLASH.
        bool pierce = false;
    };
    std::vector<Nocking> nocking_;
    // An Ice Monster's blow casts Ice on its target (OpenMU's AttackSkill 7, shown on every
    // swing): MU's ReceiveMagic starts AttackTime at 1 and at 15 reference frames the skill arm
    // drops MODEL_ICE and five shards on the target and plays SOUND_ICE
    // (WSclient.cpp:4196-4219, ZzzCharacter.cpp:4140, :4956-4970). `wait` is to that frame.
    struct IceCast {
        uint32_t caster = 0, target = 0;
        float wait = 0.0f;
        // Seconds before it starts at all: a Lizard King's bolts wait for its swing (laserCasts_).
        float delay = 0.0f;
        // Which of a Hydra's heads the bolt leaves (kHydraHeads), and whether its sound has gone.
        int head = 0;
        bool heard = false;
        // Whether its lightning has left its wisp of smoke on what it struck (ShadowStars::wisp).
        bool smoked = false;
    };
    std::vector<IceCast> iceCasts_;
    // An Ice Queen's Power Wave (OpenMU's AttackSkill 11): at the same fifteenth reference frame
    // MU's skill arm throws three MODEL_MAGIC2 off her feet at the target, the middle one
    // straight and the other two ten degrees either side, and plays SOUND_MAGIC once
    // (ZzzCharacter.cpp:5045-5055). The realm's blow is her one target; the side waves are show.
    std::vector<IceCast> waveCasts_;
    // A Thunder Lich's Lightning (OpenMU's AttackSkill 3): the hero's own thunder, from its chest
    // to the target at the same fifteenth frame, with SOUND_THUNDER01 (WSclient.cpp:4186-4190).
    std::vector<IceCast> thunderCasts_;
    // The Devil's swing: its beams from both hands to the hero while `wait` lasts (seconds).
    std::vector<IceCast> laserCasts_;
    // A Balrog's meteor storm round where it stood: one more meteor every kBalrogStormEvery
    // while `left` lasts (seconds).
    // A Meteorite's rock still waiting in the sky (Realm::rain spreads the rain): its fall starts
    // on the body when `wait` (seconds) is up, at the body's drawn place then, or at (x, z) if
    // it is gone.
    struct RockDue {
        float wait = 0.0f;
        uint32_t who = 0, whom = 0;
        float x = 0.0f, z = 0.0f;
        float weight = 1.0f;  // a shower's rock's size (sim::kLightestRock..kHeaviestRock)
    };
    std::vector<RockDue> rocksDue_;
    struct MeteorStorm {
        float x = 0.0f, z = 0.0f, left = 0.0f, next = 0.0f;
    };
    std::vector<MeteorStorm> storms_;
    // Whether a body's blow is drawn as a missile, and in which model: the Hunter's saw bolt,
    // or a guard's arrow or bolt by what she holds.
    bool shoots(uint32_t id, Arrows::Model* model);
    Arena arena_;
    // One line for one happening, in an arena run only, with the TICK on it -- because a run is
    // read afterwards and not watched, and under `--fixed-dt 16.667` a tick is exactly three
    // frames, so the tick is what a shot's frame number is worked out from. The same spirit and
    // the same shape as the `meteor: tick N` and `bones: tick N` lines beside it; those two stay
    // where they are, since an effect knows things a happening does not.
    void announce(const sim::Happening& happening);
    // The breed's own name for those lines, or "the hero".
    std::string nameOf(uint32_t id) const;
    void remember();  // the tick's positions become "was", the sim's become "now"
    // Clips, yaw and where each figure stands, at the smoothed position. `seconds` is the
    // frame's own, which the coast and the stop are measured in.
    void follow(float seconds);
    // Starts a body's death clip, its hold and its fade.
    void fall(Drawn& dead);
    // Stands a body the realm has just raised: at its tile at once, in its idle at once.
    void stand(Drawn& risen);
    // A figure put on a drawn body with its clips and bones found (play_open.cpp): every body at
    // open, and her summon's again whenever she raises one (sprint 15).
    void fit(Drawn& one, const sim::Body& body, const FigureBody* look);
    // Starts every owed fall whose killing blow is no longer waiting to be shown.
    void fallWhenLanded();
    // SetPlayerShock on a blow that did damage: a monster flinches one time in two, and so does
    // the hero, who is halted and has a click to move refused until the clip has played.
    void flinch(Drawn& struck, bool isHero);
    // Plays a body's shock clip and holds it for as long as the clip lasts.
    static void shock(Drawn& one, int clip);
    // Lets go of every held drop a beat after its dropper's killing blow lands, and rewrites
    // heldIds_.
    void releaseDrops();
    // The kill's Zen, shown taken: the figure to the lane, which sums a fight's, and the coins'
    // wait begun again -- they ring once the fight's piles stop (zenQuiet_).
    void takeZen(int64_t zen);
    float zenQuiet_ = 0.0f;  // seconds to the coins, 0 for none owed
    struct HeldDrop {
        uint32_t drop = 0;
        uint32_t dropper = 0;
        int64_t zen = 0;  // nonzero: no drop, the kill's Zen, heard and shown as he takes it
    };
    std::vector<HeldDrop> held_;
    std::vector<uint32_t> heldIds_;
    bool zenOwed_ = false;
    std::vector<uint32_t> settled_;

    content::Tables tables_;
    std::string zoneLevels_;
    sim::Realm realm_;
    sim::Findings findings_;
    const content::Ground* ground_ = nullptr;
    Figures* figures_ = nullptr;
    // His pet, drawn: the Guardian Angel about him or the Imp on his shoulder (game/pets.h).
    Pets pets_;
    // And his wing on his back (game/wings.h), slot 7.
    WingLook wing_;
    std::string bare_;  // the naked class body redress() dresses back over; see open()
    // Whether the world's air is MU's wind loop. Not Noria's since 2026-09-29: its jungle bed
    // (game/world/weather.h) is the whole of its air, the user's word -- "dont use lorencia
    // wind in noria, we have our new ambient sound which is perfect".
    bool windy_ = true;
    // The Dungeon's and the Lost Tower's: their air is aDungeon or aTower, which rides the
    // wind's slot (Play::openSound).
    bool dungeonAir_ = false;
    bool towerAir_ = false;
    bool castleAir_ = false;  // Blood Castle: its run bed (heard_.castleBed)
    // Atlans's: aWater, on the wind's slot too, the whole map and in the open.
    bool waterAir_ = false;
    // Under the sea: off its safe zone a player swims (FigureBody::swimWalkClip), MU's Fly
    // stance for Atlans (ZzzCharacter.cpp:298-301).
    bool underwater_ = false;
    // Whether MU plays the grass step on a grass floor here: Lorencia and Noria alone
    // (PlayWalkSound), so the Dungeon's and the tower's slot 0 is stone underfoot.
    bool grassy_ = true;
    bool snowy_ = false;  // Devias: his steps are snow outdoors (PlayWalkSound)

    Showing showing_;
    Marker marker_;
    Aura aura_;
    Warp warp_;
    bool warpOwed_ = false;
    bool homeOwed_ = false;
    int portalFrom_[2] = {-1, -1};  // takePortalFrom's tile, -1 for none
    float portalFacing_ = 0.0f;
    Sound sound_;
    Breath breath_;
    Bones bones_;
    Meteor meteor_;
    Bolt bolt_;
    Wave wave_;
    Arrows arrows_;
    Thunder thunder_;
    int64_t lastThunderTick_ = -1;  // the tick a channel's pulse last sounded on
    // The tick Evil Spirit's sEvil last began on: it plays out before another begins.
    int64_t lastEvilTick_ = -1000;
    Blink blink_;
    Ice ice_;
    TrapShow trapShow_;
    Poison poison_;
    Flame flame_;
    Spirits spirits_;
    Firework firework_;
    Wheel wheel_;
    Fury fury_;
    Hellfire hellfire_;
    Storm storm_;
    Inferno inferno_;
    Aqua aqua_;
    DeathStab deathStab_;
    FireBreath fireBreath_;
    // A Teleport's fade on the hero: seconds since he began to fade out, or since he was put
    // down and began to fade back in; -1 for neither. MU's tenth of alpha a frame, both ways.
    float blinkOut_ = -1.0f, blinkIn_ = -1.0f;
    int32_t gated_ = 0;
    int gatedColumn_ = 0, gatedRow_ = 0;
    int travelled_ = -1;
    int32_t quickSkill_ = 0;
    // The monster the right button last set him on, so a held button moved onto another one
    // sets him on that (rightHeld); 0 when the last press was not on a monster.
    uint32_t rightTarget_ = 0;
    // The realm tick the held right button last asked its attack again (Play::rightHeld).
    int64_t rightAskedTick_ = 0;
    bool arenaLeft_ = false;
    // The drawing's coin for a spell's two hands, `PLAYER_SKILL_HAND1 + rand() % 2`: its own,
    // so watching a wizard cast never moves the sim's seeded stream.
    uint32_t handDice_ = 0x2545f491u;
    Streak streak_;
    Gleam gleam_;
    Forge forge_;
    Snort snort_;
    // And a ridden horse's dust (fx/dust.h), owed in reference frames as the snorts are.
    Dust dust_;
    float dustOwed_ = 0.0f;
    uint32_t dustSeed_ = 0x3c6ef372u;
    Eyes eyes_;
    StaffFire staffFire_;  // the held Staff of Resurrection's spark and shaft lights
    WingMotes wingMotes_;  // the motes off every worn wing's tips
    ShadowStars shadowStars_;
    // The Shadows' stars, off the same posed frame as the eyes. fx/shadow_stars.h.
    void shade(float seconds);
    // The Budge Dragons' fire and dust, after the clips have been advanced this frame.
    void exhale(float seconds);
    // The Bull Fighters' snorts and the Elite's eyes, off the same posed frame.
    void snort(float seconds);
    // Hanzo's sparks and his hearth's smoke, read off his clip as hammer() reads its ring.
    void smithy(float seconds);
    // Twisting Slash's wheel, thrown with the weapon in his right hand. fx/wheel.h.
    void throwWheel(const Drawn& swinger, const sim::Body* body);
    // Rageful Blow's weapon and the ground it breaks. fx/fury.h.
    void throwFury(Drawn& swinger, const sim::Body* body);
    // The Giant's death sand, thrown between keys 8 and 9 of its death clip. Read per frame off
    // the clip's own clock, so it starts a third of the way down the fall and stops itself.
    void sandOnDeath();
    // The Lich's meteors: impacts this frame, and the shock that follows each one.
    std::vector<Meteor::Impact> meteorImpacts_;
    // The wandering cry's own dice: the drawing's, so that hearing a spider never moves the
    // sim's seeded stream.
    uint32_t wanderDice_ = 0x6d2b79f5u;
    // The flinch's coin, `rand_fps_check(2)` on a blow that lands: the drawing's too.
    uint32_t flinchDice_ = 0x9e3779b9u;
    // The events that are not a breed's, as Sound handles, found once at openSound.
    struct Heard {
        int swing = -1, swingLong = -1, bow = -1, crossbow = -1;  // the character's swing
        float bowOnset = 0.0f, crossbowOnset = 0.0f;  // seconds to the string's attack
        int hit = -1;                                            // melee_hit, any blow let go
        int missile = -1;                                        // missile_hit, an arrow's
        int die = -1;                                            // pMaleDie, the knight's fall
        int dieFemale = -1;                                      // pFemaleScream2, the elf's
        int deathBell = -1;                                      // the user's bell, his fall
        int shock = -1, shockFemale = -1;                        // his flinch's scream, and hers
        int grass = -1, soil = -1;                               // his footsteps
        int hoof = -1;  // and his horse's, on the run ride (mount_hoof)
        int swim = -1;  // and his stroke, swimming in Atlans (player_step_swim)
        int wind = -1;                                           // Lorencia's air
        int castleBed = -1;  // Blood Castle's run bed, iBloodCastle (world_bloodcastle)
        int fire = -1;                                           // a bonfire's crackle
        int fountain = -1;                                       // the fountain's water
        int hammer = -1;                                         // Hanzo at his anvil
        int itemDrop = -1, moneyDrop = -1, jewel = -1;  // a thing landing; a jewel's own ring
        int firework = -1;  // a Firecracker's rocket bursting, SOUND_XMAS_FIRECRACKER
        int take = -1;                                  // pGetItem: a pickup, an equip, a bind
        int drink = -1, apple = -1;                     // a potion going down
        int orb = -1;                                   // an orb read, and the skill kept
        int warp = -1;                                  // sMagic: a Town Portal landing
        int grate = -1, trapFlame = -1;                 // the traps' aGrate and sFlame
        int click = -1, refused = -1, opened = -1;      // the windows
        int repair = -1;                                // SOUND_REPAIR: a counter mended
        int mix = -1, mixBreak = -1;                    // eMix and eBreak: the Chaos Machine
        int meteorite = -1, explosion = -1;               // the Lich's throw and its landing
        int evil = -1, hellfire = -1;  // the Devil's sEvil and the Balrog's sHellFire
        int boltThunder = -1;          // eThunder, Lightning's own, on a Hydra's and a Lizard King's bolts
        int rage2 = -1, rage3 = -1;    // Rageful Blow's streaks and its cracks
        int iceCast = -1;                                 // spell_ice, on an Ice Monster's cast
        // The knight's skills, one wave each -- and Cyclone and Slash share SWORD4, which is
        // MU's own reuse. Indexed by the skill table's own index, as the cooldowns are.
        int skill[sim::kSkills] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    } heard_;
    // The sound a player's swing makes, from what is in his hands. -1 bare-handed.
    int swingSound(const sim::Body& body) const;
    // One of hearWorld's loops: heard from `at`, or off when `at` is null or past `reach`.
    void hearFrom(int event, const float* at, float full, float reach);
    // The hero's footsteps and the smith's hammer, after the clips have been advanced this
    // frame, since both are read off where a clip's clock stands.
    void steps();
    // A drop coming into view -- its dropper down, or the hero's own discard -- makes its noise
    // where it lies. Called from releaseDrops for each one let go.
    void landed(uint32_t drop);
    // Every placed sound goes through here, and is heard only if the camera holds where it is.
    void emit(int event, float x, float z, uint32_t following = 0);
    float shot_[16] = {};
    bool shotKnown_ = false;
    void hammer();
    // The townsfolk MU gives a sound of their own, played at them on MU's roll and never over
    // itself: MODEL_MIX_NPC's npc_mix on rand_fps_check(64), MODEL_ELF_WIZARD's npc_harp on
    // rand_fps_check(256) (ZzzCharacter.cpp:6081). Hanzo's hammer is hammer(), on his blow.
    void chatter(float seconds);
    // Whether each of the hero's feet has been heard on the walk cycle now playing, and whether
    // he was walking last frame. MU's c->Foot[0] and [1]; see steps().
    bool leftFoot_ = false, rightFoot_ = false, striding_ = false;
    float stepKey_ = 0.0f;  // the walk's key on the last frame, so a wrap can be seen
    int stepClip_ = -1;     // and which walk it was
    // A level the realm has given and the drawing has not shown: it waits, as MU2's did, for
    // the blow that killed `levelOn_` to land, so the flares do not go up half a swing before
    // the monster that earned them is hit. 0 is no one, and shows at once. Counted, not a flag:
    // a quest paying three levels rises three times, one after another, `levelWait_` apart; a
    // kill paying three owes one.
    int levelsOwed_ = 0;
    uint32_t levelOn_ = 0;
    float levelWait_ = 0.0f;
    // Seconds between two rises: a rise's flares live 2 s (kRising's 50 ticks), and so does the
    // loud part of plevelup.wav (3 s long, -12 dB to 1.8 s, -19 at 2). The sound has one voice
    // and a replay cuts it, so at 1.2 s every level chopped the last one's ring off at full
    // level; at 2 it is cut on its tail. Ours.
    static constexpr float kLevelApart = 2.0f;
    // Whether the next Walked the hero says came from a click, and so puts the marker down.
    // Cleared by the first tick that runs after the ask, since that tick is the one the realm
    // takes the order on. One click is one walk: holding the button does not drag the walk
    // after the pointer (the user's call, 2026-09-21), so there is no held re-aim here.
    bool mark_ = false;
    // A click was made this frame: run the next tick now instead of waiting up to 50 ms for
    // it. See Play::update.
    bool stepNow_ = false;
    bool appearing_ = false;  // the character's fade-in is running; see appear()
    float appearAt_ = 0.0f;   // seconds into it, negative while it waits
    float sinceEarly_ = 1.0f;  // seconds since a click last took a tick early
    // The cues that came due this frame. A member and not a local so that it keeps its
    // capacity: a fight must not allocate to show itself.
    std::vector<Cue> due_;
    // And what he gained on this frame's ticks -- see gains(). A member for the same reason.
    std::vector<Gain> gains_;
    // A potion's worth, drunk between frames and handed to the next frame's gains.
    int32_t drankHealth_ = 0, drankMana_ = 0;
    // What a Firecracker opened into, given when its firework is done: Zen paid -- coins and
    // the lane's sum -- or its item landed, held out of sight and reach until then (heldIds_).
    // `wait` starts at its last burst (Play::discard). Ours, the user, 2026-10-04: "play zen
    // sound and notificaiton of getting zen after firefraxrer animation and sound", then "if
    // there is item, it has to drop after firefracter aniamtion".
    struct CrackerOwed {
        uint32_t tag = 0;
        int64_t zen = 0;
        uint32_t drop = 0;   // the lying item, 0 for Zen
        float wait = -1.0f;  // seconds; below zero until the last burst
    };
    std::vector<CrackerOwed> crackerOwed_;
    // From the last burst to the giving: the barrage's tail, 1.78 s to about 2.4 s into it.
    static constexpr float kCrackerAfter = 0.6f;
    int32_t heroCast_ = 0;  // see heroCast()
    int32_t heroCasting_ = 0;  // the hero's last cast, held until the next one

    std::vector<Drawn> drawn_;
    // The town's people: those the table names a figure for, where the tables say, facing
    // where MU faced them, and those it leaves to the town's placements, as those stand them.
    struct Standing {
        Figure figure;
        int folk = -1;
        // Whether it takes turns among its clips, MuMain's way: see Play::fidget. A guard does
        // not -- he wears the player's 283 and MU stands him in one stop action for good.
        bool cycles = false;
        // Hanzo, whose hammer is heard; and whether this blow has rung yet (see hammer()).
        bool smith = false;
        bool rung = false;
        // What his forge is owed: sparks in reference frames of the blow, and the hearth's
        // smoke and embers; and whether the blow is in its spark key. See smithy().
        bool striking = false;
        float sparksOwed = 0.0f;
        float hearthOwed[2] = {0.0f, 0.0f};
        float lastClock = 0.0f;
        uint32_t dice = 1;
        // Who this is (the figure's name), and the sound MU has him make on a roll: the Chaos
        // Goblin's mixing and the elf wizard's harp. -1 for the quiet ones. See chatter().
        std::string who;
        int voice = -1;
        float every = 0.0f;  // seconds a roll comes up, on average
        float busy = 0.0f;   // seconds the last one still sounds
        // MU's RenderLight on a bone, or -1: the Chaos Goblin's BITMAP_LIGHT at bone 32, Scale
        // 1.5 (ZzzCharacter.cpp:11243). See gatherFolkLights.
        int glowBone = -1;
        float glowScale = 1.0f;
        // Charon's light in his hand, MU's MODEL_NPC_DEVILSQUARE case (ZzzCharacter.cpp:11249):
        // the bone it sits on, or -1, and what of half a second's wisp is owed. See orbs().
        int orbBone = -1;
        float wispOwed = 0.0f;
    };
    // One of Charon's BITMAP_JOINT_ENERGY wisps (CreateJoint subtype 6), in MU's units and
    // reference frames: it rises for twenty frames and then homes into the light it was
    // thrown round, and dies inside 35 units of it.
    struct Wisp {
        float position[3];
        float target[3];
        float tail[8][3];
        int tails = 0;
        float age = 0.0f;       // reference frames lived
        float velocity = 3.0f;  // units a reference frame, once it turns home
    };
    std::vector<Wisp> wisps_;
    float wispStep_ = 0.0f;  // what of a reference frame the wisps are owed
    uint32_t wispDice_ = 0x9e3779b9u;
    bgfx::TextureHandle orbSheet_ = BGFX_INVALID_HANDLE;   // lightning_2, BITMAP_LIGHTNING+1
    bgfx::TextureHandle wispSheet_ = BGFX_INVALID_HANDLE;  // joint_energy, BITMAP_JOINT_ENERGY
    void orbs(float seconds);
    // Starts a townsperson who cycles: its own dice, a clip by the rule, and a clock put
    // somewhere in it so that two of a kind are not in step.
    void settle(Standing& one);
    // The next clip for one that has just finished its last: three times in four the first,
    // which is the resting one, and otherwise one of the others. MuMain's
    // `if (rand() % 16 < 12) SetAction(o, 0); else SetAction(o, rand() % 2 + 1);`, by way of
    // MU2's Scenery.Next, which generalised it past two alternates.
    static int fidget(Standing& one);
    std::vector<Standing> folk_;
    bgfx::TextureHandle folkLight_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle starSheet_ = BGFX_INVALID_HANDLE;
    float folkClock_ = 0.0f;  // seconds, WorldTime's own, for the lights' breathing
    // MoveCharacterVisual's own Luminosity, `(rand() % 8 + 2) * 0.1` a frame, rolled at 25 Hz
    // here as the lanterns' is: the scorpion's flicker.
    float monsterLuminosity_ = 0.5f;
    float monsterRollWait_ = 0.0f;
    uint32_t monsterRoll_ = 0x5C0B710u;
    std::vector<int> questGivers_;
    int pointedFolk_ = -1;
    std::vector<Said> said_;
    // A guard's `Shouted` put into words and up over his head. See Play::said.
    void speak(const sim::Happening& happening);
    uint32_t pointedLying_ = 0;
    int pointedPerch_ = -1;
    std::vector<float> scratch_;
    double accumulator_ = 0.0;
    double tickMs_ = 0.0;
    float through_ = 0.0f;  // how far between the last tick and the next, 0 to 1

    int pointedColumn_ = -1, pointedRow_ = -1;
    uint32_t pointedAt_ = 0;  // the body under the pointer, or 0
    std::string lastLine_;

    // The red flash round a monster the frame it turns on him: its quarry becoming the hero,
    // whether it saw him, was struck by him or left a summon for him. Read off the realm each
    // frame and never told to it -- the realm decides, this only notices. INVENTION, the
    // user's (2026-09-30): MU marks no aggro at all.
    struct Aggro {
        uint32_t id = 0;
        float seconds = 0.0f;  // since it turned
    };
    static constexpr float kFlashSeconds = 0.7f;  // two blinks, the second a little fainter
    std::vector<Aggro> aggro_;
    std::vector<uint32_t> hunting_, huntingNow_;  // on him last frame, and this one
    void watchAggro(float seconds);
    float flashOf(uint32_t id) const;  // its ring's strength now, 0 for none
};

}  // namespace mu::game
