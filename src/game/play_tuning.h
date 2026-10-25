// What the drawing of a realm turns on: the clip slots MU numbers its actions by, the timings
// of a death, a spawn and a gait, and the four small helpers that read them. Nothing outside
// game/ may include this.
//
// `play.cpp` was 2073 lines. It is six files now -- the spine (the tick and what it remembers),
// the opening, the noises, the showing, the pointer and the windows' requests -- all
// implementing the one `Play` declared in `play.h`. This was its anonymous namespace, and it
// had to become something all six can see.
//
// Everything here is PRESENTATION and none of it is a rule. That is the whole of play.h's
// contract restated at the level of its constants: a number in this file may change what a
// blow LOOKS like and may never change whether it lands. Anything that decides is in sim/,
// and sim/realm_tuning.h is the page this one is the counterpart of.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "content/tables.h"
#include "game/play.h"

namespace mu::game {

// The sim's own rate. docs/reference/conventions.md, "Time": the tick never reads the frame's delta and
// the frame never decides how many ticks have passed except by this number.
constexpr double kTickSeconds = 1.0 / 20.0;
// PLAYER_ATTACK_DARKHORSE, 11 keys at 0.3 (ZzzCharacter.cpp:1013): how long the horse plays its
// stomp, which the ground rings run for (fx/earthshake.h).
constexpr float kEarthshakeClipSeconds = 11.0f / (0.3f * 25.0f);
// The longest a hero's shot waits for her string (Play::nocking_): her clip cut short -- a
// click away, a death -- still lets the arrow go.
constexpr float kNockHold = 0.33f;
// The longest her blow waits for the arrow carrying it to land (Arrows::landed): the string's
// hold and a long flight.
constexpr float kArrowOwed = 0.8f;
// How many ticks past its flight a spell's hit stays owed (Play::owedHits): a Meteorite's rain
// spreads a second over its fall, and a miss lapses after it.
constexpr int64_t kOwedSlack = 24;
// The most ticks one frame may run. A window that was away -- dragged, or stalled on a
// screenshot's readback -- comes back owing seconds of simulation, and stepping all of it in
// one frame is a stall that makes the next frame owe more. MU2 called this the mirror clock's
// debt and repaid it a little at a time; here the debt is simply forgiven, because a single
// player game has nobody to be out of step with.
constexpr int kMostTicks = 5;
// Ticks owed on a server past which the mirror steps them at once rather than hurrying its
// clock: a second behind (Play::update).
constexpr int kSnapOwed = 20;
// A play frame on a server longer than this is logged (Play::update): about three missed at 60 Hz.
constexpr double kLongFrame = 0.05;
// The join's warm-up under the loading screen (Play::settle): the line counts as steady once a
// ping has come back and, for this long, no frame found more than kSettleOwed ticks waiting;
// and the world is let in after kSettleMost whatever the line is doing.
constexpr double kSettleSteady = 0.5;
constexpr int kSettleOwed = 2;
constexpr double kSettleMost = 6.0;
// **The cushion** (Play::update, sprint 24): ticks held in hand on a
// server so a late one is not a stall. A stall longer than kStallFelt (half a frame at 60 fps;
// shorter is not seen) counts; kStallsToCushion of them inside kStallWindow seconds hold one tick
// more, up to kMostCushion; kCalmToLower seconds with none hold one fewer. While short of it the
// clock runs at kCushionFill. All invention.
// 25 ms since 2026-10-17: at 8 a home line's ordinary jitter took a tick in hand every few minutes,
// and every tick taken or let go moved the hero's lead a step.
constexpr double kStallFelt = 0.025;
// **Coasting** (Play::coast): while the line is late, each walking body is drawn on along its
// route at its own pace for up to kCoastTicks, and when the ticks come, those ticks are stepped
// at once and what the guess got wrong is eased out over kCatchSeconds. A 400 ms Wi-Fi stall once a
// minute froze the world and then hurried it (the user, 2026-10-17: "time to time there happens
// little Delay,Hold-up,Hiccup"). All invention.
// The ease is held under kCatchPace of the body's own pace (or of kCatchFloor tiles a second,
// for a body standing): at 0.15 s unheld, the hero caught half a tile out walked 2.8 times his pace
// for a few frames after every stall of the test's (`--stall 400 5`).
constexpr double kCoastTicks = 8.0;
constexpr float kCatchSeconds = 0.15f;
constexpr float kCatchPace = 0.6f;
constexpr float kCatchFloor = 2.0f;
// The clock's pace while the server is ahead of it by part of a tick (Play::update): another
// player's early tick, ~25 ms, is caught up in about half a second. Invention.
constexpr double kAheadPace = 1.06;
constexpr double kStallWindow = 5.0;
constexpr int kStallsToCushion = 3;
constexpr int kMostCushion = 3;
constexpr double kCalmToLower = 20.0;
constexpr double kCushionFill = 0.9;
// The least time between two ticks taken early for a click, in seconds. See Play::update.
constexpr float kEarlyApart = 0.5f;
// A Teleport's fade, each way: MU takes a tenth of the body's alpha a frame, ten frames
// (ZzzInterface.cpp:2603), which is the realm's eight ticks.
constexpr float kBlinkFadeSeconds = 0.4f;

// How long the character takes to dissolve in when the game has loaded. Invention.
constexpr float kAppearSeconds = 1.1f;
// How long a `--guard` preview holds the ribbons with no boon behind them: two of their laps.
// They were shown this long at a real cast too (2026-09-25), and stand for the guard's whole
// length since 2026-10-21.
constexpr float kGuardShowSeconds = 10.0f;
// A body Evil Spirit strikes spins (the user, 2026-10-02: "when evil spirit hit monster we need
// that monster is rotatinig, this is how i remember it from MuMain"). The spin is MuMain's own,
// PushingCharacter's `Angle[2] += StormTime * 10` a frame with StormTime counted down a frame at
// a time (ZzzCharacter.cpp:3140-3144): ten of MU's 25 frames, 0.4 s and about a turn and a half.
// MuMain as forked sets StormTime nowhere -- it only zeroes it (WSclient.cpp:5797, 5809, 5874) --
// so what started it for which skill is not in the source read: Evil Spirit as its trigger is
// the user's memory. The angle it is left at then eases back to the body's facing over about
// kSpinSettleSeconds, where MuMain keeps it until the body next turns (ours).
constexpr float kSpiritStormTime = 10.0f;
constexpr float kStormFramesPerSecond = 25.0f;
constexpr float kSpinSettleSeconds = 0.25f;

// How long a skill's clip blends in over, against the 0.18 s every other clip uses
// (`kBlendSeconds`, crowd.cpp). **invention**: a skill is a wind-up rather than a jab, and at the
// swing's own blend the body is standing in the pose before the arm has begun to move, which is
// what "the animation looks instant" means. Long enough to read as a body gathering itself and
// short enough that the blow still lands in the clip's own second half.
constexpr float kCastBlend = 0.28f;

// The blade's ribbon on a skill swing (fx/streak.h). Three keys of wind-up before it marks, which
// is the client's own `AnimationFrame >= 3` guard, and a sixth of the blade to the tip, which is
// MU's 20-to-120 read as fractions of the weapon's own measured length.
constexpr float kStreakWindUp = 3.0f;
constexpr float kStreakFrom = 20.0f / 120.0f;
// PLAYER_ATTACK_STRIKE, player.muc's 80: the one Dark Lord clip CreateWeaponBlur streaks
// (ZzzCharacter.cpp:3804-3808).
constexpr int32_t kLordStrikeClip = 80;
// A plain blow with a spear or a scythe: MU's `BlurType 3`, from 100 of 120 -- the head alone.
constexpr float kStreakPoleFrom = 100.0f / 120.0f;
// And the plain blow's light, mapping 0's ladder in `CreateWeaponBlur`: grey, then red from +3,
// blue from +5, orange from +7. A skill's ribbon is white whatever the plus.
constexpr float kStreakLight[4][3] = {
    {0.8f, 0.8f, 0.8f}, {1.0f, 0.2f, 0.2f}, {0.2f, 0.4f, 1.0f}, {1.0f, 0.6f, 0.2f}};

// A breed's name as the command line may spell it: lowered, with everything that is not a
// letter or a digit dropped, so "Skeleton Warrior", "skeletonwarrior" and "Skeleton_Warrior"
// are one word. The cook writes two names for a breed -- the figure ("BudgeDragon01") and the
// label ("Budge Dragon") -- and the log prints the label, so both are accepted.
inline std::string plainName(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (const char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            out.push_back(char(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    return out;
}

// Which breed `name` means, or -1. An exact label or figure wins over a prefix of a figure, so
// that a figure whose name is another's prefix cannot shadow the exact one; the prefix pass is
// what lets `--arena BudgeDragon` find `BudgeDragon01`, which is the figure name the cook
// writes and nobody types.
inline int32_t arenaBreed(const content::Tables& tables, const std::string& name) {
    const std::string want = plainName(name);
    if (want.empty()) return -1;
    for (size_t i = 0; i < tables.kinds.size(); ++i) {
        if (plainName(tables.kinds[i].label) == want) return int32_t(i);
        if (plainName(tables.kinds[i].figure) == want) return int32_t(i);
    }
    for (size_t i = 0; i < tables.kinds.size(); ++i) {
        const std::string figure = plainName(tables.kinds[i].figure);
        if (!figure.empty() && figure.compare(0, want.size(), want) == 0) return int32_t(i);
    }
    return -1;
}

// MONSTER01_DIE: every monster model shares this twelve-slot layout in this one order
// (content/monsters/actions.json), so slot 6 is the death whether the model is a spider or a
// giant, and the cook already marks it `hold` -- it plays once and holds its last frame, a
// corpse pose, rather than looping.
constexpr int kMonsterDieSlot = 6;
// MONSTER01_SHOCK, played on the Lich's meteor quake and nowhere else (sprint 11 step 5).
constexpr int kMonsterShockSlot = 5;
// Nova's two (MuMain's _enum.h): PLAYER_SKILL_HELL_BEGIN held while it gathers and
// PLAYER_SKILL_HELL_START as it goes (WSclient.cpp:4754-4762).
constexpr int kNovaChargeAction = 72;
constexpr int kNovaBurstAction = 73;
// The key of it the burst goes off on: as he lands from the leap, his root at its lowest
// (0.54 m, from 1.47 at key 3.1), measured on our cook of the clip -- eight keys over 1.12 s.
// MU fires MODEL_CIRCLE, StopBuffer(SOUND_NUKE1) and PlayBuffer(SOUND_NUKE2) when Nova's
// AttackTime reaches its limit of 15 (ZzzCharacter.cpp:2846-2851, 4133-4146, 4436-4439): its
// AnimationFrame 14 is past this clip's end, so it is the count from 1, fourteen frames, key 3.5
// here, with him still in the air. **Ours**, the landing, the user's (2026-10-07): 'nova sound
// has to perfectly synced with animation', 'hitting the ground'.
constexpr float kNovaBurstKey = 4.3f;
// MONSTER01_WALK, which MonsterMoveSandSmoke reads.
constexpr int kMonsterWalkSlot = 2;
// How far the quake reaches: MU's `Distance <= 200`, a hundred units to the tile.
constexpr float kShockTiles = 2.0f;
// PLAYER_SHOCK, for a body on the player rig that is not the hero -- the Skeleton Warrior.
// 230, which index.json's `actions` table labels "Come up": player.muc's labels are one slot off
// here, as the salute's are, and the clip under "Shock" (231, 30 keys) is a fall to the ground.
// 230 is the short recoil, the feet planted -- checked on the bench and chosen by the user,
// 2026-09-29.
constexpr int kPlayerShockSlot = 230;
// The HERO keeps none on his Drawn, and that is a decision rather than an omission. MU's quake
// loop excludes the hero outright, so he never flinches for a meteor. His flinch from a blow
// looks this slot up itself: Play::flinch.
// PLAYER_DIE1, which the cook also holds (content/players/rig/actions.json, hold_at_end). The
// hero plays it and lies there until the realm revives him at the gate -- no fade: MU leaves
// the player's body on the ground for the whole wait.
constexpr int kPlayerDieSlot = 232;
// How long a corpse holds its pose before fading, and how long the fade itself takes.
// Invention: MU removes a dead monster from the world the instant OpenMU's own
// DeathAction handler fires, with nothing standing in for either number. This one held
// visibly through its own death clip (typically under a second at a monster's authored
// play speed) plus a beat, then eased out over a fade quick enough that a second monster
// dying nearby is never waiting on it, and short enough beside any monster's own respawn
// timer that nothing here needs to race it.
constexpr float kDeathHold = 1.2f;
constexpr float kDeathFade = 0.6f;
constexpr float kDeathTotal = kDeathHold + kDeathFade;
// How long after the killing blow lands -- the frame the bar reads nought and the fall starts
// -- what the monster left comes out. The user's call (2026-09-22): the drop belongs to the
// health reaching nought, not to the end of the death clip. Invention; a beat so it reads as
// coming out of the blow rather than being there already.
constexpr float kDropDelay = 0.12f;
// A respawn's own fade-in: fast enough to read as "arriving" rather than "loading", and the
// same smoothstep the hero's own door-opening fade uses (kAppearSeconds), just shorter --
// invention, the user asked for it not to pop.
constexpr float kSpawnFadeSeconds = 0.35f;
// The hero's light while an Ale stands: MuMain's AbilityLight for ABILITY_FAST_ATTACK_SPEED,
// `AbilityLight[0] *= 0.9f; AbilityLight[1] *= 0.5f; AbilityLight[2] *= 0.5f`
// (ZzzCharacter.cpp:9320). OpenMU's packet note calls it "shown in a red color".
constexpr float kSousedLight[3] = {0.9f, 0.5f, 0.5f};
// An iced body's light, MU's `eDeBuff_Freeze` BodyLight (ZzzObject.cpp:1126).
constexpr float kIcedLight[3] = {0.3f, 0.5f, 1.0f};
// **Ours.** The ice on an iced body (the user, 2026-10-02: "if mosnter/char is iced add some ice
// effect to the model also"). MU stops at the blue light; this adds the +7 chrome's pass --
// Chrome01's bands crawling over the body, which is how MU draws its cloak (RENDER_CHROME,
// ZzzObject.cpp:10244) -- in a cold blue, so the body reads glazed. What a thing already
// refined to +7 or more wears is left as it is.
constexpr int kIcedPlus = 7;
constexpr float kIcedChrome[3] = {0.35f, 0.6f, 1.0f};
// **Ours.** An Elite (sim/elite.h) wears the +9 refinement pass (Chrome01's bands and Shiny01) in
// gold all the time (the user, 2026-10-21: "chrome has to be golden", "gold effect has to be
// stronger"), its body lit a strong warm gold under it (kEliteLight). Not the excellent shimmer:
// tried for strength, it turned the gold purple on every breed but the red ones. And it stands
// 1.4 times its breed ("elites has to be little bit bigger"; 1.25 before).
constexpr int kElitePlus = 9;
constexpr float kEliteChrome[3] = {1.0f, 0.78f, 0.25f};
constexpr float kEliteLight[3] = {1.9f, 1.5f, 0.75f};
constexpr float kEliteScale = 1.4f;
// A poisoned body's, `eDeBuff_Poison`, and one both poisoned and iced (ZzzObject.cpp:1118-1123).
constexpr float kPoisonedLight[3] = {0.3f, 1.0f, 0.5f};
// **Ours.** A character's, a third of MU's green: his green smoke (Poison::sicken) carries the read
// since 2026-10-12, and under both the user found him 'tintent to much green'.
constexpr float kPoisonedCharLight[3] = {0.77f, 1.0f, 0.83f};
constexpr float kPoisonIcedLight[3] = {0.3f, 1.0f, 0.8f};
// A murderer's light: `if (c->PK >= PVP_MURDERER2) Vector(1.f, 0.1f, 0.1f, c->Light)`
// (ZzzCharacter.cpp:9990-9993), every part he wears lit blood red. CreateMonster sets the
// Lost Tower's Cursed Wizard PVP_MURDERER2 (:13926); nobody else here is one.
constexpr float kMurdererLight[3] = {1.0f, 0.1f, 0.1f};
// show tag "murderer": CursedWizard (its recipe's show.tags).

// How far from the camera a body is drawn at all, in tiles. MU's camera is fixed and close and
// sees about twenty tiles; posing all 290 of Lorencia's bodies every frame would spend the
// crowd's whole account on figures nobody can see. Foundation 7's "ranges per kind", applied to
// the one kind this sprint draws.
constexpr float kDrawRange = 32.0f;

// The three lengths a walk is made of, all three MU2's own (`client/core/Crowd.cs`) and all
// three marked there as not MU's: MU has no fade and no wait and stands a man up on the frame
// he arrives.
//
// Setting off and stopping are not the same change, which is why they are not the same number.
constexpr float kGaiting = 0.1f;    // standing into a walk: MU2's 0.15 slid for its length
constexpr float kHalting = 0.08f;   // a walk into standing: an arrival, already late
constexpr float kCoasting = 0.1f;   // a monster's patience before a still body is a stopped one
// The most a clip may be hurried. MU2 clamps the rate to [0.25, 4]; the floor is not kept here
// because zero is a rate this engine means -- a body covering no ground has its feet stop, and
// a quarter-speed walk under a body that is not moving is the slide the floor was hiding.
constexpr float kFastestClip = 4.0f;

// MU's own action numbers for a swing, by the stance the figure holds its weapon in
// (index.json's `actions` table: 38 "Attack fist", 39 "Attack sword right 1", 43 "Attack two
// hand sword 1", 46 "Attack spear 1", 47 "Attack scythe 1"). A monster's numbers are its OWN
// table and not these -- `monster_actions` 3 is "Attack 1" -- which is the trap figures.h
// names, so the two are looked up separately below and never out of one table.
inline int attackSlotFor(const std::string& stance) {
    if (stance == "sword") return 39;
    if (stance == "two_hand_sword") return 43;
    if (stance == "spear") return 46;
    if (stance == "scythe") return 47;
    if (stance == "bow") return 50;
    if (stance == "crossbow") return 51;
    // Bare hands: MU's 38, Attack fist, is a flailing spin that read as nothing like a blow, so
    // an empty hand swings the one-handed sword's clip (sim/swings.cpp, the same departure).
    return 39;
}

// And the same swing on a horse: MU's PLAYER_ATTACK_RIDE_* (ZzzCharacter.cpp:1112-1153) --
// the one-hand sword's for a fist or a staff, as MU gives them.
inline int rideSlotFor(const std::string& stance) {
    if (stance == "two_hand_sword") return 55;
    if (stance == "spear") return 56;
    if (stance == "scythe") return 57;
    if (stance == "bow") return 58;
    if (stance == "crossbow") return 59;
    return 54;
}

// A weapon held in both hands: on a horse it keeps its own standing grip and blows, seated
// (Figure::seat, Figure::upper), as MU's armed ride stance and swings read one-handed (the user:
// "we need also two hand weapon stance on mount", "not only two hand stance bt also two hand
// attack"). **ours**.
inline bool twoHandedStance(const std::string& stance) {
    return stance == "two_hand_sword" || stance == "spear" || stance == "scythe";
}

// MU's own ride clips, which seat themselves: the stop and run rides, the ride swings, the rider
// skill, Impale's ride thrust (70) and the ride cast. Anything else played on a horse is a
// standing clip and is seated.
// And the Dark Horse's: its stop and run rides, 78 and 79, the ride strike, teleport, sword,
// flash and magic, 82-86, Earthshake 87 and the two horse idles, 88 and 89 (player.muc's
// numbers; the Dark Lord's standing Force and Summon, 80 and 81, are not).
inline bool rideAction(int slot) {
    return slot == 13 || slot == 14 || slot == 36 || slot == 37 || (slot >= 54 && slot <= 59) ||
           slot == 68 || slot == 69 || slot == 70 || slot == 155 || slot == 78 || slot == 79 ||
           (slot >= 82 && slot <= 89);
}

// What a Dark Lord's skill plays on the Dark Horse (13/4): PLAYER_ATTACK_RIDE_TELEPORT 83 for
// Summon (WSclient.cpp:4702-4706), _ATTACK_FLASH 85 for Electric Spike (ClassAttack.cpp:933-937),
// _ATTACK_MAGIC 86 for Critical Damage (WSclient.cpp:4685-4691), and PLAYER_ATTACK_RIDE_STRIKE 82
// for the rest -- Force, Force Wave, Fire Burst, Fire Scream (:4243-4248). player.muc's numbers. And
// PLAYER_ATTACK_DARKHORSE 87 for Earthshake (ClassAttack.cpp:948-952), the horse's own stomp.
inline int lordHorseClip(int skill) {
    switch (skill) {
        case sim::skill::kSummon: return 83;
        case sim::skill::kElectricSpike: return 85;
        case sim::skill::kCriticalDamage: return 86;
        case sim::skill::kEarthshake: return 87;
        default: return 82;
    }
}

// An angle folded into a half turn either side of nothing, so that a body a few degrees the
// other side of due north turns the short way. The sim has its own copy (realm.cpp's `wrapped`)
// and this is deliberately not shared with it: the sim must not grow a dependency on the
// drawing, and an angle is four lines.
inline float wrapped(float angle) {
    constexpr float kTurnabout = 6.28318530718f;
    angle = std::fmod(angle, kTurnabout);
    if (angle > 3.14159265359f) angle -= kTurnabout;
    if (angle < -3.14159265359f) angle += kTurnabout;
    return angle;
}

// What the character is drawn as when nobody dressed him: the cook's own armoured Dark Knight,
// which is what every run before there was a game used. A game hands `heroLook` in instead --
// the naked class body with the cradle's weapon in its hand -- and sprint 9's character select
// is what decides which one that is.
inline constexpr const char* kHeroFigure = "DarkKnight";

// Figures::dress's own `name` for the hero, matching World::play's first dress -- `redress`
// asks for the same key so it replaces that same FigureBody rather than piling up another one.
inline constexpr const char* kHeroDressName = "Hero";

// Hanzo the smith's model: the one townsperson in Lorencia who makes a noise. MU2's
// Scenery.Noise; MixNpc01 and ElfWizard01 are the other two and are Noria's.
inline constexpr const char* kSmithFigure = "Smith01";

// Moss the Gambler, Noria's Jeweler: his pipe, his light and his own second idle (Standing).
inline constexpr const char* kMossFigure = "gambler_moss";

// The one breed in Lorencia that breathes fire and raises dust. MU2's BudgeDragon01.json
// `effects` block, which the cook does not carry; stated here once.
// show tag "budge_breath": BudgeDragon01 (its recipe's show.tags).
// The Great Bahamut, whose Level 1 trails that same dust round it (play_open.cpp).
// show tag "great_bahamut": GreatBahamut01 (its recipe's show.tags).
// And its light: MU's Level 1 body light is -0.4 where Level 0's is +0.2 (Selection.cpp:101-107).
// MU takes the 0.4 off the tile's own light, so the dimmer the land the darker it goes: on
// Atlans's sea floor it is all but black (the user's reference, a near-black fish). As a share
// of what an ordinary monster is drawn by here, this; 0.45 left it brown.
constexpr float kLevelOneLight = 0.25f;
// Its trail's puffs against the dragon's dust: this much larger, and this faint. Ours.
constexpr float kGreatBahamutPuffGrow = 2.2f;
constexpr float kGreatBahamutPuffAlpha = 0.3f;
// And the one that does not fall: MU's SetPlayerDie tests the sub-type of a body on the player
// rig (MODEL_SKELETON1..3) and makes eleven bones of it instead of playing a death
// (ZzzCharacter.cpp:1465-1471).
//
// MU keys that on the MODEL and this keys it on the cooked BODY, which is not the same thing
// and is worth saying out loud: the figure's `name` is "SkeletonWarrior" and its `mesh` is
// "Skeleton01". The dragon's case above gets away with `name` because its two are the same
// word. What this costs is that another breed sharing this model -- MU's Death Cow takes the
// identical eleven-bone branch, and the Stone Golem the same shape with big stones -- would
// need its own row here. Neither stands in Lorencia, and the day one does this becomes a
// field in the cook rather than a name in a list.
// show tag "bursts_apart": SkeletonWarrior (its recipe's show.tags).
// And the Dungeon's two other skeletons, SubType MODEL_SKELETON2 and 3: CharacterDie's test is
// `SubType >= MODEL_SKELETON1 && <= MODEL_SKELETON3`, so all three come apart the same way.
// show tag "bursts_apart": SkeletonArcher (its recipe's show.tags).
// show tag "bursts_apart": EliteSkeleton (its recipe's show.tags).
// MODEL_STONE_GOLEM, which comes apart into stones the same way (Bones::rubble).
// show tag "crumbles": StoneGolem01 (its recipe's show.tags).
// MODEL_DEATH_COW, the Lost Tower's: `o->Live = false`, MODEL_BONE1 and ten MODEL_BONE2
// (ZzzCharacter.cpp:1480-1486), the skeletons' burst on its own model. And RenderEye(o, 22, 23)
// always (:11209-11211), the Elite Bull Fighter's eyes, on the same bone names.
// show tag "death_cow": DeathCow01 (its recipe's show.tags).
// MODEL_CRUST, Icarus's Mega Crust: RenderEye(o, 26, 27, 2.0f) always (ZzzCharacter.cpp:
// 11213-11215), on eye00 and eye01, at twice the sprite's size.
// show tag "crust": MegaCrust01 (its recipe's show.tags).
// show tag "crust": AlphaCrust01 (its recipe's show.tags).
// How much of the way to its tile a Crust's drawn body closes a reference frame (Play's glide).
constexpr float kGlideShare = 0.07f;
constexpr float kGlideLag = 0.35f;  // tiles it may trail the realm by, ours
constexpr float kCrustEyeSize = 2.0f;
// MODEL_ICE_MONSTER, which has no corpse either, but only once its death clip has played: the
// death action ends, EtcStopAnimationSetting calls CreateBlood, and CreateBlood's own case puts
// it out and throws ten MODEL_ICE_SMALL (ZzzCharacter.cpp:3521-3528, ZzzEffectBlurSpark.cpp:449).
// show tag "shatters": IceMonster01 (its recipe's show.tags).
// Every other breed MU gives a BoneHead and no `c->Blood` bleeds instead: two stains by the head
// (CreateBlood's other branch, fx/stains.h, Drawn::bleeds). Show tag "bleeds": every recipe whose
// monster.model_index is a MONSTER_MODEL OpenMonsterModel gives a BoneHead (0-5, 8, 10-13, 19,
// 21-30, 32, 34-36, 39-48, 70-72), less the Ghost, Assassin, Ice Monster and Ice Queen, the
// skeletons (`c->Blood = true`) and the bodies that come apart with no death clip (Stone Golem,
// Death Cow): 55 figures. Our head is the rig's Bip01 Head, not MU's BoneHead number.
// MODEL_BUDGE_DRAGON's hop while it walks (Drawn::hops). Show tag "hops": BudgeDragon01,
// GoldenBudgeDragon01.
// MONSTER_GHOST, drawn seen-through: CreateMonster sets `c->Object.AlphaTarget = 0.4f`
// (ZzzCharacter.cpp:14103), so the whole body is at 40%. It rides the body's own fade (its own
// depth first, then blended, and its shadow dithered by the same number), times its death's.
// show tag "see_through": Ghost01 (its recipe's show.tags).
inline constexpr float kSeeThroughAlpha = 0.4f;
// MODEL_GIANT's own case in the same effect switch, and the whole of it is one call:
//
//     case MODEL_GIANT:
//         MonsterDieSandSmoke(o);
//         break;                                    -- ZzzCharacter.cpp:6178
//
//     void MonsterDieSandSmoke(OBJECT* o) {
//         if (o->CurrentAction == MONSTER01_DIE &&
//             o->AnimationFrame >= 8.f && o->AnimationFrame < 9.f)
//             for (int i = 0; i < 20; i++)
//                 if (rand_fps_check(1))
//                     CreateParticle(BITMAP_SMOKE + 1, <within 32 units>, o->Angle, white, 1);
//     }                                             -- ZzzCharacter.cpp:5552
//
// It is a different SHAPE from the skeleton's burst above, and that is the thing to hold on to:
// the burst happens on the instruction that kills the body, and the sand happens on the death
// clip's own clock, a third of the way into it. So this is read per frame off `keyOf`, like the
// dragon's fire and the smith's hammer, and not from `fall`.
//
// MU's other callers are Tarkan's: the Tantallos (SubType 0 only, so not the Zaikan on its
// body), the Golden Wheel's model (the Iron Wheel) and the plain Beam Knight (ZzzCharacter.cpp:
// 5960-5984, :5945-5946). The Bloody Wolf's and the Mutant's calls are commented out in MuMain.
// show tag "sands" (their recipes' show.tags):
// Giant01, IronWheel01, Tantalos01, BeamKnight01.
constexpr float kSandFrom = 8.0f, kSandTo = 9.0f;
// ONCE, on the frame the death clip crosses key 8, and not a rate. MU's window is one key wide
// and MU advances a key a reference frame, so its twenty attempts at `rand_fps_check(1)` come
// to twenty in total; reading it as the dragon's dust is read -- a rate of twenty a reference
// frame -- turned the death into a sandstorm. Play::sandOnDeath.
//
// TEN rather than MU's twenty, and thrown round a ring of this radius in tiles at the animal's
// own size. Invention, the user's call (2026-09-22): "elegant and nice", "nothing too much".
// Twenty at MU's own size and alpha is a solid tan blob; the count, the ring and the dressing
// in Breath::sand are all part of one answer and are tuned together.
constexpr int kSandPuffs = 10;
constexpr float kSandReach = 0.62f;

// MODEL_BULL_FIGHTER's case in the same switch, which both of Lorencia's bull rows open: the
// plain bull and the Elite share the model, so both snort. Keyed on the cooked row's name, as
// the dragon's is, so each variant needs its own line here.
// show tag "snorts": BullFighter01 (its recipe's show.tags).
// The Dungeon's Poison Bull, the third row out of the same model: it snorts as the other two,
// and CreateMonster registers eDeBuff_Poison on it for good (ZzzCharacter.cpp:14096), which
// RenderObject draws as the poisoned body's green (0.3, 1.0, 0.5) (ZzzObject.cpp:1122) -- a
// standing tint, kPoisonedLight whether or not anything has poisoned it.
// show tag "venomous": PoisonBull01 (its recipe's show.tags).
// The one Power Wave caster MU fans three ways (ZzzCharacter.cpp:5046); the rest throw one.
// show tag "fans": IceQueen01 (its recipe's show.tags).
// And the one of them MU gives Level 1 to, which is what lights RenderEye (fx/eyes.h).
// show tag "elite_bull": EliteBullFighter01 (its recipe's show.tags).
// The Lost Tower's Shadow and Poison Shadow, one model at Level 0 and 1 (fx/shadow_stars.h). MU
// puts a sprite on every joint but the arms' claws, thirty-nine; **ours**, these twelve, the body's
// own, so the aura holds still rather than flickering over every plate and finger (the user,
// 2026-10-01: "still to active it has to be more elegant and subtle").
// show tag "shadow": Shadow01 (its recipe's show.tags).
// The Death Gorgon, the Gorgon at Level 2: embers off random bones and an orange light
// (fx/shadow_stars.h), one ember every this many reference frames: MU throws ten a frame
// (ZzzCharacter.cpp:6064-6071). The user, 2026-10-09: "death gorgon has this red tint and
// overall flame effect around them", and at MU's ten, "flames are too crazy": a quarter of it.
// show tag "death_gorgon": DeathGorgon01 (its recipe's show.tags).
constexpr float kEmberEveryFrames = 0.25f;
// And its red: MU lights a body by the terrain light where it stands, and the Gorgon's own
// AddTerrainLight (1, 0.2, 0) at about 0.65 over the tower's grey floor is about 2.6 : 1.3 : 1
// on the body. Ours, that hue at about its own brightness, on its light as an Ale's is.
constexpr float kDeathGorgonLight[3] = {2.2f, 0.85f, 0.6f};
// The Death Knight: MU's BITMAP_FIRE at bone 2, Bip01 Pelvis, one reference frame in two
// (ZzzCharacter.cpp:6007-6011); ours, the same embers, one in four.
// show tag "death_knight": DeathKnight01 (its recipe's show.tags).
constexpr float kKnightEmberEveryFrames = 4.0f;
// The Devil, whose Lightning blow MU draws as its own: four BITMAP_JOINT_LASER + 1 from its two
// link bones to the hero and fire off them while it swings, with SOUND_EVIL
// (ZzzCharacter.cpp:2295-2313); ours, a beam a hand for this long.
// show tag "devil": Devil01 (its recipe's show.tags).
// The Vepar, whose Energy Ball MU never throws (its AT_SKILL_ENERGYBALL arm breaks for it,
// ZzzCharacter.cpp:5092): its attack is SOUND_EVIL and, while it swings, two BITMAP_BLUR + 1
// joints off each of its link bones (30 and 39, named as the Devil's) to the target, at Scale
// 50 and 10 (:2183-2201). Ours, those two a hand as soft streaks this many metres either side
// of their line, for the Devil's beam time.
// show tag "vepar": Vepar01 (its recipe's show.tags).
constexpr float kVeparBeamHalf[2] = {0.25f, 0.06f};
// The Lizard King, whose Lightning MU draws as six BITMAP_JOINT_THUNDER a frame off its two link
// bones (52 and 65, named as the Devil's) to the target, three a hand at Scale 50 and 10, and no
// bolt from the sky (ZzzCharacter.cpp:2330-2342). Ours, three a hand as joint_thunder streaks
// this many metres either side of their line, for the Devil's beam time.
// show tag "lizard_king": LizardKing01 (its recipe's show.tags).
// The staff it holds, whose head the bolts leave from (fx/staff_fire's head point).
inline constexpr const char* kLizardStaff = "Staff07";
constexpr float kLizardBoltHalf[2] = {0.22f, 0.07f};
// And red, to match the Staff of Resurrection it holds (the user, 2026-10-04: 'make lizard
// lighting match the weapon color'): between the staff fire's head (1, 0.6, 0.4) and its shaft
// lights (1, 0.2, 0.1), fx/staff_fire. Ours; MU's joints take the white sheet's own colour.
constexpr float kLizardBoltColour[3] = {1.0f, 0.32f, 0.18f};
// And on its swing: the bolts start this long before the blow lands (half the swing, where the
// cue shows it) and last this long. Ours.
constexpr float kLizardBoltLead = 0.1f;
constexpr float kLizardBoltSeconds = 0.45f;
// How far the thin bolts' ends wander round the middle of the body they strike, metres: small,
// so all three meet the body (the Devil's 0.35 scattered them past it).
constexpr float kLizardBoltWander = 0.12f;
// The crackle along its staff while it casts: from this far up the shaft (metres in the staff's
// own space; the head is 1.45) to the head, wandering this far off the line. Ours.
constexpr float kLizardShaftFrom = 0.35f;
constexpr float kLizardShaftWander = 0.06f;
// The Hydra, Atlans's boss. MU lets a BITMAP_BOSS_LASER + 1 off its gem (bone 63, Cone01) every
// fifth attack frame and a ring of nine for its Flame of Evil (ZzzCharacter.cpp:1905-1929); both
// left out (the user, 2026-10-04: 'lets keep only lasers from head'), so its swing shows by the
// beams its heads carry (below) and the flare on its gem.
// show tag "hydra": Hydra01 (its recipe's show.tags).
// The Silver Valkyrie, whose bolts are Penetration's arrow (Play::volleyShot). Ours.
// show tag "pierces": SilverValkyrie01 (its recipe's show.tags).
// Its four heads, whose beams strike (sim kSplitBlows): MU's BlendMesh 5 is never drawn (the
// user, 2026-10-04: 'from heads we shoot red lightiing not lasers?'), and each beam is the
// Lizard King's red lightning from a mouth instead -- the bone each beam rides in Monster38 and
// the root of that beam in the model's bind space, metres, read off the mesh. Ours.
// Its heads' bones and mouths are its recipe's show.heads (sprint E4c).
// How long a head's bolt stands. Ours, the Lizard King's.
constexpr float kHydraBoltSeconds = 0.45f;
// Its BlendMesh 5, bbbb03: four red beams from its mouths, which MU lights only in an attack --
// BlendMeshLight up 0.1 a frame in ATTACK1-2 and 0 in anything else (ZzzCharacter.cpp:
// 5989-6001). Ours, never drawn: its heads throw red lightning instead (show.heads).
// The material is its recipe's show.beam_material (sprint E4c).
constexpr float kDevilBeamSeconds = 0.6f;
// MU's four beams wander: each joint is laid at a fresh random angle every frame. Ours, the
// second beam off each hand meets the target this far off its middle, rolled again each frame,
// and an ember off a hand one reference frame in this many (MU: a BITMAP_FIRE a beam a frame).
constexpr float kDevilBeamWander = 0.35f;  // metres
constexpr float kDevilFireEveryFrames = 2.0f;
// The Balrog's Flame of Evil rains MODEL_FIRE within 512 units on every frame its skill lasts
// (ZzzCharacter.cpp:1980-1985, rand_fps_check(1)); ours, the Lich's meteor, one every this
// often for this long -- a storm, under Meteor's sixteen at once. The user, 2026-10-03: the
// four it threw before were less than MU's.
constexpr float kBalrogStormSeconds = 1.6f;  // its attack's 1600 ms
constexpr float kBalrogStormEvery = 0.12f;
// How far into sEvil (3.34 s) another Evil Spirit may start it, in the realm's ticks: half its
// length, so under MU's two voices the two overlap and neither is cut back to its start -- the
// stutter heard as a loop when every cast restarted it -- and a third steals only the last
// tenth of the first.
constexpr int64_t kEvilSoundTicks = 32;
// **Ours** (the user, 2026-10-01: "if monster has some special effects use minimal light
// emiter", "like electricy our fire, it could be also weapon"): a faint light in the colour of
// what a breed burns with, hung on the bone where that is -- its body's spine, or the grip of a
// weapon that is the effect. fx/shadow_stars.h, the nearest two, after the spells' lights.
// Which breeds take one, its bone and its colour, are their recipes' show.aura (sprint
// E4b), each with its citation; Play::fit hangs it.
// show tag "poison_shadow": PoisonShadow01 (its recipe's show.tags).
// MONSTER_ALQUAMOS's stars: g_chStar's first nine bones (ZzzCharacter.cpp:165-168, 8797-8801),
// 10, 18, 37, 38, 51, 52, 58, 59, 66 in Monster51, by name. Its sparks off random bones, three a
// frame (:8805-8812), are left out, ours, by the subtle-aura rule. Its blow throws four
// BITMAP_FLARE sub 7 ribbons round the target (:2140-2150): kStarRibbons a blow, each living
// 30-39 reference frames, SOUND_METEORITE01 once a blow (MU's four plays at LifeTime 30).
// show tag "star_ribbons": Alquamos01 (its recipe's show.tags).
// Its bones are its recipe's show.bones.stars (sprint E4b).
inline constexpr int kStarRibbons = 4;
// MONSTER_QUEEN_RAINER's: a BITMAP_LIGHT at Scale 0.8 on bone 20, Bip01 Head, white
// (ZzzCharacter.cpp:8815-8818; Light is RenderCharacter's (1, 1, 1), :8498), drawn as an
// Alquamos's starlight. MoveCharacterVisual's crackle (:5768-5817): every frame a
// BITMAP_JOINT_THUNDER sub 7, 14 wide, in (0.5, 0.5, 1), along each of these bone pairs --
// the legs, the spine, the arms -- by name in Monster52. Ours: show.arcs of them a
// reference frame, picked at random, as ShadowStars::thunderBeam streaks at kQueenArcDim; all
// fourteen each frame was a lit wire figure. Her blow's twenty BITMAP_BLIZZARD on the target at
// CheckAttackTime(5) (:1681-1695), 4/25 s into it (ShadowStars::blizzard).
// show tag "queen_rainer": QueenRainer01 (its recipe's show.tags).
// Her head's light and her arcs are her recipe's show.bones.stars and show.arcs (sprint E4c).
constexpr float kQueenArcColour[3] = {0.5f, 0.5f, 1.0f};
constexpr float kQueenArcDim = 0.4f;
constexpr float kBlizzardWait = 4.0f / 25.0f;
// MONSTER_DRAKAN's (ZzzCharacter.cpp:8840-8865): BITMAP_LIGHT at 0.8 in (0.1, 0.1, 1) on bones
// 13-26 and 52-58, Monster55's 21 stars bones, as starlights in show.star_tint; and a
// BITMAP_JOINT_THUNDER sub 7, 20 wide, from bone i - 1 to i for i of 14-16 and 23 each frame --
// the show.arcs, ours show.arcs of them a reference frame. Its Attack 2 throws its
// bolt at CheckAttackTime(13) from bone 11, its head (:2123-2138), drawn as a monster's Lightning
// (Play::thunderCasts_, with MU's SOUND_METEORITE01, :1760-1771). Its Flame of
// Evil (sim kBosses) shows nothing more than its blow: MU's boss show is the Gorgon's and the
// Balrog's alone (:1959-1990).
// MONSTER_DEATH_TREE's six horns, Monster104_Horn0-5 on bones 37-40, 44 and 45 (GMAida.cpp:396-401):
// every frame a BITMAP_LIGHT + 1 at 0.4 in (0.4, 0.8-0.89, 0.5) and a BITMAP_SPARK + 1 sub 6
// on each (:978-987), drawn as star bones in its green. Ours: the sprite is the starlight's
// (0.6, a little wider), its colour doubled against kStarlightDim, and the sparks left out.
// show tag "death_tree": DeathTree01 (its recipe's show.tags).
// Its bones are its recipe's show.bones.stars (sprint E4b).
// And its colour its recipe's show.star_tint.
// MONSTER_FOREST_ORC's blow (GMAida.cpp:564-596): on Attack 1 and Attack 2, three CreateBlur
// type 3 (BITMAP_BLUR2), Short, in (0.5, 0.5, 0), sub 23-25, one per claw of its right hand, from
// 20 units along each finger's first bone to 60 along its last: bones 35-37, 39-41 and 43-45. Laid
// on fx/streak.h's ribbons, the spear's sheet, from the first key: MU's case has no wind-up guard.
// show tag "forest_orc": ForestOrc01 (its recipe's show.tags).
// Its claws are its recipe's show.claws: the bone pairs (sprint E4c), and the reach, light and
// last key (from, to, light, until), which the Death Rider's blades share.
// show tag "drakan": Drakan01 (its recipe's show.tags).
// Its stars, their blue and its arcs are its recipe's show.bones.stars, show.star_tint and
// show.arcs (sprint E4c).
// MONSTER_GREAT_DRAKAN, the Drakan's body in black and red: no stars and no arcs, but a
// BITMAP_FIRE at Scale 0.3 on bone 18, stars13, each frame (ZzzCharacter.cpp:8867-8879) -- ours
// as the Death Knight's embers, one every kGreatDrakanEmberEvery reference frames.
// show tag "great_drakan": GreatDrakan01 (its recipe's show.tags).
// Its bones are its recipe's show.bones.ember (sprint E4b).
constexpr float kGreatDrakanEmberEvery = 2.0f;
// MONSTER_PHANTOM_KNIGHT's boss blow: 36 BITMAP_JOINT_SPIRIT sub 1 at Scale 60 from a metre over
// it at CheckAttackTime(14) (ZzzCharacter.cpp:1710-1732). Ours: one Evil Spirit release of eight
// (fx/spirits), the wizard's own joints, at that time -- thirty-six filled the sky.
constexpr float kPhantomSpiritWait = 13.0f / 25.0f;  // (14 - 1) / 25 s
// MONSTER_DARK_PHOENIX, the bird (Monster56) with its rider (Monster57) merged into one body
// (pipeline/merge_rigs.py). Its plain blow's bolt at CheckAttackTime(14) from the bird's head
// (ZzzCharacter.cpp:2108-2121), ours from its middle as a monster's Lightning; its boss blow's
// forty BITMAP_JOINT_SPIRIT at CheckAttackTime(2) and (6) (:1785-1800), ours one Evil Spirit
// release at the first.
// show tag "phoenix": DarkPhoenix01 (its recipe's show.tags).
constexpr float kPhoenixBoltWait = 13.0f / 25.0f;    // (14 - 1) / 25 s
// Its bolt's colour and the light where it bites: the bird's fire, orange-red (the user,
// 2026-10-07: 'lihgting from phoenix still is blue not orangish/redish'). Ours.
constexpr float kPhoenixBoltColour[3] = {0.85f, 0.26f, 0.04f};
constexpr float kPhoenixSpiritWait = 1.0f / 25.0f;   // (2 - 1) / 25 s
// Its shield, shown only (the port's decision 7, A): WebZen toggles it every six seconds
// (gObjSkylandBoss); each time it goes up the bird wears fx/aura's kPhoenixBarrier. The phase
// is the realm's clock, so it needs nothing from the realm.
constexpr int64_t kPhoenixShieldTicks = 6 * 20;
// Its bones are its recipe's show.bones.shade (sprint E4b).
// MODEL_CHAIN_SCORPION, which carries an orange light on its `light_point` bone.
// show tag "scorpion_light": ChainScorpion01 (its recipe's show.tags).
// MONSTER_GORGON's Gorgon Staff, whose star RenderCharacter's linked-weapon switch lights for
// anyone holding it (ZzzCharacter.cpp:10270-10276): BITMAP_SHINY + 1 at Scale 2, 90 units down
// the link bone (knife_gdf, the Gorgon's 30), in (0.4, 0.8, 0.6) * Luminosity.
// show tag "star_staff": Gorgon01 (its recipe's show.tags).
// Its bones are its recipe's show.bones.star (sprint E4b).
// MODEL_ELITE_YETI's breath (ZzzCharacter.cpp:6181-6189): `rand_fps_check(4)` puts one
// BITMAP_SMOKE at bone 22, Box03 under the head, offset zero, with no action gate -- it
// breathes standing, walking, fighting and falling. The Bull Fighter's snort particle.
// show tag "yeti_breath": EliteYeti01 (its recipe's show.tags).
// Its bones are its recipe's show.bones.snort (sprint E4b).
constexpr float kYetiBreathAt[3] = {0.0f, 0.0f, 0.0f};
// MODEL_HUNTER, whose blow is a bolt out of its arquebus. See Play::hunterShot.
// show tag "hunter": Hunter01 (its recipe's show.tags).
// MODEL_ASSASSIN, the one breed struck in silence: the flinch's cry is skipped by type,
// `o->Type != MODEL_ASSASSIN && Models[o->Type].Sounds[2] != -1` (ZzzCharacter.cpp:1411).
// show tag "silent_flinch": Assassin01 (its recipe's show.tags).
// Where on the muzzle: `Vector(0.f, -4.f, 0.f, p)` in bone 24's frame, metres here.
constexpr float kSnortAt[3] = {0.0f, -0.04f, 0.0f};
// The four windows, in the clip's own keys: STOP1 15-20, STOP2 20-25, WALK 2-3 and 5-6.
struct SnortWindow {
    int slot;
    float from, to;
};
constexpr SnortWindow kSnortWindows[] = {{0, 15.0f, 20.0f}, {1, 20.0f, 25.0f},
                                         {2, 2.0f, 3.0f},   {2, 5.0f, 6.0f}};
// RenderEye's `Vector(±5.f, 0.f, 0.f, p)`: out along the left bone and back along the right.
constexpr float kEyeAt[2][3] = {{0.05f, 0.0f, 0.0f}, {-0.05f, 0.0f, 0.0f}};
// Tarkan's eye trails (fx/eye_trails.h): each breed's MoveEye(o, b, Right, Left) bones, by
// name, from MoveCharacterVisual (ZzzCharacter.cpp:5836-5984). The Zaikan and the Death Beam
// Knight are the Tantallos's and the Beam Knight's bodies, so their bones.
// Each breed's pair, right and left, is its recipe's show.eye_trail (sprint E4b).
// MonsterMoveSandSmoke: one BITMAP_SMOKE + 1 within 100 units of the body each reference frame
// it walks (ZzzCharacter.cpp:5571-5582). Its callers: the Mutant, the Bloody Wolf, the Tantallos
// (not the Zaikan), the Golden Wheel and the plain Beam Knight (:5836-5984).
// show tag "walk_sands" (their recipes' show.tags):
// Mutant01, BloodyWolf01, IronWheel01, Tantalos01, BeamKnight01.
constexpr float kWalkSandReach = 100.0f;  // MU's units, either way
// Ours: under MU's full light, as the Great Bahamut's trail was turned down (the user,
// 2026-10-04: 'that smoke is to small and to vissible') -- a puff every walking frame of a
// whole pack is far more dust than the dragon's one in four.
constexpr float kWalkSandAlpha = 0.35f;
// Tarkan's blows that throw an Inferno (fx/inferno.h): CheckAttackTime(1), the attack's first key
// (ZzzCharacter.cpp:1808-1846, :1880-1886). `bombs` is CreateInferno, `mesh` MODEL_SKILL_INFERNO;
// the plain Beam Knight's CreateInferno is commented out in MuMain.
// The plain Beam Knight's hand flames (fx/body_flames.h): MoveHumming from bone 55 to 62 and from
// 70 to 77, a BITMAP_FLAME subtype 1 at Scale 0.2 on each claw (ZzzCharacter.cpp:5925-5946).
// show tag "hand_flame": BeamKnight01 (its recipe's show.tags).
// Its bones are its recipe's show.bones.hand_flame (sprint E4b).
constexpr float kHandFlameScale = 0.2f;
// The Zaikan's foot fires (Breath::footFire): SubType 1's BITMAP_FIRE at bones 6 and 13 each
// reference frame (ZzzCharacter.cpp:5960-5975). Its -1.3 terrain light under it is not carried:
// this renderer only adds light.
// show tag "foot_fire": Zaikan01 (its recipe's show.tags).
// Its bones are its recipe's show.bones.foot_fire (sprint E4b).
// The Death Beam Knight's burning body (fx/body_flames.h): MU's vec_list of 35 bones, by name,
// and its segments as pairs of indices into it (ZzzCharacter.cpp:138-162, :5843-5925). Its
// -1.3 terrain light is not carried either.
// show tag "burning": DeathBeamKnight01 (its recipe's show.tags).
// Its bones are its recipe's show.bones.burning (sprint E4b).
// wingLeft and wingRight (their last four at 0.5), then arm_leg_Left and arm_leg_Right at 0.6.
// And every other frame `body` at 1.3 and a subtype 3 at `head` at 0.5. The segments, body and
// head are its recipe's show.burn (sprint E4c).
// Ours: the burn at this share of MU's light. At MU's own, ~380 one-metre flames added over each
// other under this renderer's bloom were one white blot that hid the knight (arena, 2026-10-06).
constexpr float kBurnLight = 0.15f;
// A monster an Immolate rune set burning (sim kBurnRuneTicks): each reference frame
// kEmberFlames subtype 2 flames on bone segments drawn at random off its own skeleton -- any
// monster's, so no table of names -- at kEmberScale and kEmberLight of MU's light, and its body
// lit kEmberTint, an ember beside the poison's green and the ice's blue. Ours.
constexpr int kEmberFlames = 3;
constexpr float kEmberScale = 0.6f;
constexpr float kEmberLight = 0.35f;
constexpr float kEmberTint[3] = {1.0f, 0.55f, 0.35f};
// A Plague Arrows lane's arrow (sim Happening::plagueLanes): its fire and streak toned a pale
// poison green, softer than the Chaos Nature Bow's (fx/arrow.h kNatureFire). Ours.
constexpr float kPlagueArrowTint[3] = {0.55f, 1.0f, 0.5f};
// A poisoned monster's smoke (fx/poison.h Poison::sicken), beside its green: this many puffs a
// reference frame, each off a bone drawn at random, at this share of its light (the user,
// 2026-10-16: 'the poison effect smoke for monsters is little bit to much', at 0.5 and 1). Ours.
constexpr float kSickPuffs = 0.3f;
constexpr float kSickGlow = 0.7f;
// And a poisoned character's, fewer and fainter (the user, 2026-10-15: 'poison smoke effect from
// poisoned character is little bit to much', then at 0.3 and 0.7 'still little bit to much coming
// from char'). Ours.
constexpr float kSickCharPuffs = 0.15f;
constexpr float kSickCharGlow = 0.55f;
// A bleeding body's drips (sim kBleeders, ours): this many a reference frame for each stack,
// each a drop of the blow's own blood off a bone drawn at random, falling to the ground.
constexpr float kBleedDrips = 0.15f;
// And a monster a bleed rune cut (sim::BleedRune) lit crimson under its drips, as strong as a
// burn's ember and bluer, so the two read apart (the user, 2026-10-12: 'combine some blood effect
// with minimal tint', then at 1, 0.8, 0.8 'we need some special color for bleeding similiar like
// burn'); the hero's bleed keeps none. Ours.
constexpr float kBleedTint[3] = {1.0f, 0.3f, 0.38f};
// Tarkan's bosses' Flame of Evil (sim kBosses): the Zaikan's and the Death Beam Knight's
// AT_SKILL_BOSS blow throws the ring of eighteen staffs at the attack's fourteenth frame
// (fx/staff_ring.h), and the Death Beam Knight a MODEL_SKILL_BLAST within 400 units every frame
// of it -- Cometfall's comet (ZzzCharacter.cpp:1816-1834, :1887-1897). The Tantallos's draws
// nothing more than its blow: its client case gives the ring to the Zaikan alone.
constexpr float kStaffRingWait = 14.0f / 25.0f;
constexpr float kBlastRainReach = 4.0f;    // metres either way: rand() % 800 - 400
// Ours: one every third reference frame. At MU's one a frame two knights' boss blows were some
// forty comets in a second and the fight a white screen (arena, 2026-10-06), as the wizard's
// Inferno blasts were once ('explosions are to crazy').
constexpr float kBlastRainEvery = 3.0f / 25.0f;
constexpr float kBlastRainFall = 0.3f;     // seconds: 300-800 units at 50-100 a frame
// The landing's BITMAP_EXPLOTION. Ours: under Cometfall's 0.72, since eighteen land at once.
constexpr float kStaffBlast = 0.4f;
// The Iron Wheel's three MODEL_ARROW_BOMBs: one at its target and one 20 degrees either side
// (ZzzCharacter.cpp:1859-1878). Ours: drawn as its crossbow's bolt, the sides flying the aimed
// one's distance and striking nothing -- the realm's blow is the one.
// show tag "spreads": IronWheel01 (its recipe's show.tags).
constexpr float kSpreadDegrees = 20.0f;
// `wait` is when in the blow it is thrown, seconds. MU's CheckAttackTime(n) counts AttackTime,
// which starts at 1 with the blow and gains one a reference frame whatever the clip
// (ZzzCharacter.cpp:4133-4138; w_CharacterInfo.h:247-250): (n - 1) / 25 s. 0 is
// CheckAttackTime(1), at once; the Crusts' CheckAttackTime(5) (:1696-1708) is 4/25.
constexpr float attackTime(int n) { return float(n - 1) / 25.0f; }
// Which breeds throw one, and how, are their recipes' show.inferno (sprint E4d): bombs,
// mesh, the attack_frame it waits for, and `first`, on Attack 1 alone.

// Charon's light, RenderCharacter's MODEL_NPC_DEVILSQUARE case (ZzzCharacter.cpp:11249-11268):
// `Vector(3.5f, -12.f, 10.f, p)` on BoneTransform[20], two BITMAP_LIGHTNING+1 sprites at Scale
// 0.3 over a 128-texel sheet, turned by +-WorldTime/50 degrees, lit
// `sinf(WorldTime * 0.002f) * 0.35f + 0.65f` grey; and `rand_fps_check(30)` a wisp.
inline constexpr int kCharonOrbBone = 20;
constexpr float kCharonOrbAt[3] = {0.035f, -0.12f, 0.10f};
constexpr float kCharonOrbHalf = 0.5f * 1.28f * 0.3f;
constexpr float kCharonOrbSpin = 20.0f * 3.14159265f / 180.0f;  // radians a second
constexpr float kCharonWispEvery = 30.0f / 60.0f;                // seconds

// MONSTER01_ATTACK1, and the key its fire stops on: `AnimationFrame <= 4.f`.
constexpr int kBreathSlot = 3;
constexpr float kBreathThrough = 4.0f;
// How far out of the head a spark is born: MU's 32 to 64 units.
constexpr float kBreathNear = 0.32f, kBreathFar = 0.64f;

// Where a foot lands in MU's walk, in the clip's own keys: `AnimationFrame >= 1.5f` and
// `>= 4.5f` in ZzzCharacter.cpp's PlayWalkSound, each latched by its own c->Foot[n] so a mark
// several frames wide sounds once. Two a cycle, which is a pair of legs. MU2's Crowd.FirstFoot.
constexpr float kFirstFoot = 1.5f, kSecondFoot = 4.5f;
// And how far each step strays from the one recording (Sound::vary), so a walk is not the
// same crunch twice a cycle: pitch up to a semitone either way, up to 4 dB softer, and the
// air closed by up to an octave and a half -- from 16 kHz to about 5.7, under the 11 kHz the
// 22 kHz files hold, so it is heard. WoW's way with one sample; MU plays every step identical.
constexpr float kStepSemitones = 1.0f, kStepDropDb = 4.0f, kStepDarken = 1.5f;
// The swim's stroke, wider: a long splash, where a step is a click, and the same one every 0.4 s
// read as a loop under the steps' spread. Ours, with its three takes (sounds.json).
constexpr float kSwimSemitones = 2.0f, kSwimDropDb = 6.0f, kSwimDarken = 1.5f;

// The window of the smith's first action his hammer lands in: `CurrentAction == 0 &&
// AnimationFrame >= 5.f && <= 10.f`, in keys and not seconds. MU2's Scenery.HammerFrom.
constexpr float kHammerFrom = 5.0f, kHammerTo = 10.0f;

// And the part of it the sparks fly in: `AnimationFrame >= 5.f && <= 6.f`, one key, the blow
// itself (ZzzCharacter.cpp:6100). A burst every reference frame of it, so how many a blow
// throws is how long his clip takes over that key. fx/forge.h has the rest.
constexpr float kSparksFrom = 5.0f, kSparksTo = 6.0f;
// `BoneTransform[17]`: Box03, the hammer's head, under Box01 in his left hand.
constexpr int kSparkBone = 17;
// Where his hearth's coals burn, in his own model's metres: the middle of `fire_03`, the
// strip of coals along the top of the forge (Smith01.obj, 62 to 103 across, 66 to 70 up).
// Where the smoke and embers leave from, which are ours (fx/forge.h), and where the light
// goes, which is MU's: `AddTerrainLight(o->Position, (L, 0.4L, 0), 3)` with
// `L = (rand() % 6 + 2) * 0.1`, re-rolled every frame. MU lights the ground under his feet
// and this lights it from the coals, the street lamps' reasoning (StreetLight01.json): a
// light at a man's feet lights the man's feet. Eased at the fires' 3 Hz rather than re-rolled
// a frame, for the reason the fires are (FireLight02.json): a light re-rolled a frame strobes.
constexpr float kHearth[3] = {0.83f, 0.68f, 0.09f};
constexpr float kForgeColour[3] = {1.0f, 0.4f, 0.0f};
constexpr float kForgeLow = 0.2f, kForgeHigh = 0.7f;
constexpr float kForgeReach = 3.0f;
constexpr float kForgeHz = 3.0f, kForgeSmooth = 0.12f;

// Lorencia's grass: the tile texture MU tests for `HeroTile == 0` under a footstep; every
// other floor on the map is soil.
constexpr int kGrassFloor = 0;

// What counts as in the shot for a sound: a sphere this far round a point this high over the
// ground where it was made -- a body's middle, and enough slack that a monster half off the
// edge of the frame is still heard. MU2's Scenery reach was a tile.
constexpr float kHeardHeight = 1.0f;
constexpr float kHeardReach = 1.5f;

// A bonfire's crackle, in metres from the character: 1/d as any placed voice to kFireFull, then
// taken out straight to nothing at kFireReach, where the loop stops. Ours: MU's fires are silent.
constexpr float kFireFull = 8.0f;
constexpr float kFireReach = 16.0f;
// Lorencia's fountain, the same way and closer: water is a small sound, heard across the
// square's middle and not from its edges. Ours: MU's fountain is silent too.
constexpr float kFountainFull = 3.0f;
constexpr float kFountainReach = 9.0f;

// Where a figure's clock stands in its clip's own keys -- MU's AnimationFrame, which is what
// every one of its sound tests reads. A looping clip is cooked with one closing key, so its
// duration spans frames - 1 intervals and this runs 0 to the key count MU authored.
inline float keyOf(const Figure& figure) {
    const FigureBody* body = figure.body();
    if (!body || !body->library || figure.clip() < 0) return -1.0f;
    const content::CookedClip& clip = body->library->clips.clips[size_t(figure.clip())];
    if (clip.duration <= 0.0f || clip.frames < 2) return -1.0f;
    return figure.clock() / clip.duration * float(clip.frames - 1);
}

// How far into its swing a figure's blow lands, as a fraction of the clip: its recipe's
// show.lands key over the clip's keys (keyOf's measure), or `otherwise` -- the half every breed
// without one lands on. The Blue Golem's fist meets on key 5 of 8, where MU throws its shock wave.
inline float landingOf(const Figure& figure, float otherwise) {
    const FigureBody* body = figure.body();
    if (!body || body->landsAt < 0.0f || !body->library || figure.clip() < 0) return otherwise;
    const content::CookedClip& clip = body->library->clips.clips[size_t(figure.clip())];
    if (clip.frames < 2) return otherwise;
    return std::clamp(body->landsAt / float(clip.frames - 1), 0.0f, 1.0f);
}

// The MU action a figure is playing, or -1.
inline int slotOf(const Figure& figure) {
    const FigureBody* body = figure.body();
    if (!body || !body->library || figure.clip() < 0) return -1;
    return body->library->clips.clips[size_t(figure.clip())].slot;
}

}  // namespace mu::game
