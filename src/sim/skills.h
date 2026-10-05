// The skills a character can throw: what one costs, what it does to the blow, and how long it
// is before it can be thrown again.
//
// `docs/skills-dk.md` is the argument; this is the table and the two formulas. Three layers, and
// which layer a number comes from is written beside it:
//
//   * **0.75, traced.** The mana, the clip and the knock are OpenMU's
//     `Version075/SkillsInitializer.cs:58-63`, and the multiplier's own base is
//     `ClassDarkKnight.cs:74`/`:112` -- `SkillMultiplier = 2 + energy/1000`, applied to every hit
//     that has a skill (`AttackableExtensions.cs:226-247`). A knight's skill is about twice his
//     swing, which MU2's own docs had wrong until 2026-09-22.
//   * **Ours, and marked `invention`.** The cooldown, because 0.75 has none; strength on the
//     multiplier, because the user asked for the wizard's shape with the knight's stat; and the
//     reach, because nothing here closes a gap.
//   * **Not MU's range.** Every skill is thrown at the knight's own reach. `movesToTarget` is
//     not implemented at all -- see `docs/skills-dk.md` §3.1a.
#pragma once

#include <cstdint>
#include <string>

#include "content/tables.h"
#include "sim/rules.h"

namespace mu::sim {

// MU's own skill numbers, so nothing here carries a magic 19.
namespace skill {
constexpr int32_t kNone = 0;
constexpr int32_t kDefense = 18;
constexpr int32_t kFallingSlash = 19;
constexpr int32_t kLunge = 20;
constexpr int32_t kUppercut = 21;
constexpr int32_t kCyclone = 22;
constexpr int32_t kSlash = 23;
// The three past 0.75, and their numbers and names are MU's own: `skill_eng.bmd`, the client's
// string table, reads 41 Twisting Slash, 42 Rageful Blow, 43 Death Stab (the decode is
// `docs/mu-scrolls-and-orbs.md` §7). 41 is 0.95d's and the other two are Season 6's -- they are
// here because §3.1b gates a skill on the weapon family and 0.75's five leave three families
// with one key each. What each one DOES here is ours; only the name, the number and the icon
// are MU's.
constexpr int32_t kTwistingSlash = 41;
constexpr int32_t kRagefulBlow = 42;
constexpr int32_t kDeathStab = 43;
// The wizard's first spell, and the only one he is born knowing: OpenMU's
// `AddEnergyBallForDarkWizard` puts it in a new wizard's list at creation, and 0.75 sells no
// scroll for it (MU2/docs/spells.md).
constexpr int32_t kEnergyBall = 17;
// And his guard, the knight's Defense in the wizard's hand: MU's `AT_SKILL_SOUL_BARRIER`, taught
// by the Scroll of Soul Barrier (group 15 number 15). Season 6 in OpenMU's tree and not 0.75; it
// is here because the user asked for the two classes to stand level behind a shield
// (docs/skills-dw.md, 2026-09-28).
constexpr int32_t kSoulBarrier = 16;
// His first spell that has to be earned: MU's `AT_SKILL_FIREBALL`, off the Scroll of Fire Ball
// (group 15 number 3, `Book04`), which asks forty energy to read. 0.75's own row.
constexpr int32_t kFireBall = 4;
// And the third he buys, `AT_SKILL_POWERWAVE`, off the Scroll of Power Wave (group 15 number 10,
// `Book11`) at fifty-six energy.
constexpr int32_t kPowerWave = 11;
// And the fourth, `AT_SKILL_THUNDER`, off the Scroll of Lighting (OpenMU's spelling; group 15
// number 2, `Book03`) at seventy-two energy -- the one whose element does something: it pushes.
constexpr int32_t kLightning = 3;
// And the fifth, `AT_SKILL_METEO`, off the Scroll of Meteorite (group 15 number 1, `Book02`) at a
// hundred and four energy: a rock called down out of the sky onto one body, on a cooldown.
constexpr int32_t kMeteorite = 2;
// And `AT_SKILL_TELEPORT`, off the Scroll of Teleport (group 15 number 5, `Book06`) at eighty-eight
// energy: no blow at all, a blink to the ground he points at.
constexpr int32_t kTeleport = 6;
// And `AT_SKILL_ICE`, off the Scroll of Ice (group 15 number 6, `Book07`) at a hundred and twenty
// energy: little damage, and what it strikes walks at half speed.
constexpr int32_t kIce = 7;
// And `AT_SKILL_POISON`, off the Scroll of Poison (group 15 number 0, `Book01`) at a hundred and
// forty energy: a blow, and then a poison that goes on hurting.
constexpr int32_t kPoison = 1;
// And `AT_SKILL_FLAME`, off the Scroll of Flame (group 15 number 4, `Book05`) at a hundred and
// sixty energy: a fire lit on the body's tile that burns twice.
constexpr int32_t kFlame = 5;
// `AT_SKILL_EVIL_SPIRIT`, off the Scroll of Evil Spirit (group 15 number 8) at two hundred and
// twenty energy: spirits round him, striking most of what is within ten tiles once. The shield's Evil Spirit rune lets the
// same go off a miss (sim/items.h kSpiritChance).
constexpr int32_t kEvilSpirit = 9;
// `AT_SKILL_HELL_FIRE`, off the Scroll of Hellfire (group 15 number 9) at two hundred and sixty
// energy: the ground on fire round him, striking every monster within four tiles once.
constexpr int32_t kHellfire = 10;
// `AT_SKILL_STORM`, off the Scroll of Twister (group 15 number 7, `Book08`) at a hundred and
// eighty energy: a whirlwind sent walking out ahead of him, striking what it passes three times.
constexpr int32_t kTwister = 8;
// `AT_SKILL_INFERNO`, off the Scroll of Inferno (group 15 number 13, `Book14`) -- 0.95d's and
// not 0.75's: eight blasts in a ring round him, striking every monster within four tiles once.
constexpr int32_t kInferno = 14;
// `AT_SKILL_FLASH`, off the Scroll of Aqua Beam (group 15 number 11, `Book12`) at three hundred
// and forty-five energy: a beam of water thrown straight out ahead of him.
constexpr int32_t kAquaBeam = 12;
// **The Fairy Elf's** (sprint 15), at 0.75's own numbers: Triple Shot 24, Heal 26, Greater
// Defense 27, Greater Damage 28 (`Version075/SkillsInitializer.cs:64-67`). 24 is called
// "Skillshot" here and taught by an orb, the user's of 2026-09-28; 0.75 grants it only off a bow
// with the Skill option.
constexpr int32_t kSkillshot = 24;
constexpr int32_t kHeal = 26;
constexpr int32_t kGreaterDefense = 27;
constexpr int32_t kGreaterDamage = 28;
// And her six summons, `AT_SKILL_SUMMON` to `+5`: 0.75's `SummonSkillToMonsterMapping`
// (TargetedSkillDefaultPlugin.cs:22-31) -- the Goblin 26, the Stone Golem 32, the Assassin 21,
// the Elite Yeti 20, the Dark Knight 10 and Bali 150.
constexpr int32_t kSummonGoblin = 30;
constexpr int32_t kSummonGolem = 31;
constexpr int32_t kSummonAssassin = 32;
constexpr int32_t kSummonYeti = 33;
constexpr int32_t kSummonKnight = 34;
constexpr int32_t kSummonBali = 35;
// `AT_SKILL_PENETRATION`, off the Orb of Penetration (group 12 number 17): 0.95d's and not
// 0.75's (Version095d is where OpenMU first has it; VersionSeasonSix/SkillsInitializer.cs:171
// carries the row). An arrow that flies on through everything in its line.
constexpr int32_t kPenetration = 52;
// `AT_SKILL_RIDER`, Fire Breath: the Horn of Dinorant's and not learned. 0.95d's and not 0.75's
// (Version095d/SkillsInitializer.cs:80): thirty damage, three tiles, nine mana, level 110, the
// knight's; MuMain prints it on the horn as his own skill (ZzzInventory.cpp:5174). Known while
// the horn is worn and its life lasts (Realm::knows), thrown only while it is ridden.
constexpr int32_t kFireBreath = 49;
}  // namespace skill

// What an iced body's walking is multiplied by: OpenMU's `IcedMovementSpeedFactor`, 0.5, which
// MuMain agrees with twice over (`Speed *= 0.5f`, ZzzCharacter.cpp:6353).
constexpr float kChillFactor = 0.5f;

// A poison's pulse: OpenMU's `PoisonMagicEffect` ticks every three seconds (sixty ticks here).
// The first comes two seconds on, as WebZen's count of twenty pulses on every third
// (user.cpp:25695-25699, 1.00.93): at 2, 5, 8 ... 20 seconds, seven in all, not six from 3.
constexpr int32_t kPoisonEvery = 60;
constexpr int32_t kPoisonFirst = 40;
// What an iced monster's swing waits on top of its own: WebZen's DelayActionTime of 800 ms,
// added to every action it takes while iced (ObjBaseAttack.cpp:772, gObjMonster.cpp:1521).
constexpr int32_t kChillSwingTicks = 16;

// A Flame's fire strikes every twenty reference frames -- MU's client asks
// `AttackCharacterRange` when `(int)LifeTime % 20 == 0` over a forty-frame life
// (MoveHandlers.cpp:1817) -- so twice, 0.8 s apart, which is sixteen ticks. OpenMU caps the
// hits at two (`maximumHitsPerTarget`) and puts them 500 ms apart; the picture's pace is MU's.
constexpr int32_t kBurnEvery = 16;
// Twister's storm strikes when `(int)LifeTime % 15 == 0` (Move_MODEL_STORM, MoveHandlers.cpp:
// 3423-3425) over a fifty-nine-frame life. MoveEffect runs the mover before it takes the frame
// off and kills the effect at nought (ZzzEffect.cpp:8573-8576), so the mover sees 59 to 1 and
// strikes at 45, 30 and 15 -- three times, fourteen, twenty-nine and forty-four frames in. At 20
// Hz that is the first eleven ticks after the let-go and each next twelve on (15 frames = 0.6 s).
constexpr int32_t kStormFirst = 11;
constexpr int32_t kStormEvery = 12;

// ---- the weapon families (docs/skills-dk.md §3.1b) ------------------------------------------
//
// **A skill belongs to a kind of weapon, and is thrown with that kind or not at all.** The
// user's rule, 2026-09-23. It is not an invention so much as 0.75's own arrangement made
// explicit: in the original a knight had a skill only while he held the weapon that carried it
// (§1.2), so Falling Slash WAS an axe's blow and Slash WAS the two-hander's. Learning made a
// skill permanent and quietly threw that away; this column puts it back, and MuMain gates its
// own later skills exactly this way -- `SkillWarrior` in `GameLogic/Combat/SkillCast.cpp:145`
// refuses Impale without a spear and Spiral Slash without a sword before it spends any mana.
//
// A bit for each hand a weapon can be, and MU's own item groups are what decides which: 0
// swords, 1 axes, 2 maces, 3 the polearms. One-handed and two-handed are told apart by
// `Arm::twoHanded()`, which OpenMU keeps as the footprint's width -- a Battle Axe is two cells
// across and a Double Axe is one, and they are different weapons to swing.
namespace arms {
constexpr uint32_t kNone = 0;
constexpr uint32_t kSword1 = 1u << 0;
constexpr uint32_t kSword2 = 1u << 1;
constexpr uint32_t kAxe1 = 1u << 2;
constexpr uint32_t kAxe2 = 1u << 3;
constexpr uint32_t kMace1 = 1u << 4;
constexpr uint32_t kMace2 = 1u << 5;
// Every polearm in MU is two-handed, so the spear has one bit rather than two: Spear, Dragon
// Lance, Berdysh and Great Scythe are all two cells across.
constexpr uint32_t kSpear = 1u << 6;
// Defense's own hand, and the reason a shield is in this enumeration at all: it is the one
// "weapon family" a buff asks for, so one column answers both questions instead of two.
constexpr uint32_t kShield = 1u << 7;
// The elf's two (sprint 15): a bow and a crossbow, which throw Skillshot and nothing a knight
// throws.
constexpr uint32_t kBow = 1u << 8;
constexpr uint32_t kCrossbow = 1u << 9;
constexpr uint32_t kMissiles = kBow | kCrossbow;

constexpr uint32_t kSwords = kSword1 | kSword2;
constexpr uint32_t kAxes = kAxe1 | kAxe2;
constexpr uint32_t kMaces = kMace1 | kMace2;
constexpr uint32_t kOneHand = kSword1 | kAxe1 | kMace1;
constexpr uint32_t kTwoHand = kSword2 | kAxe2 | kMace2 | kSpear;
// Everything a knight can swing a skill with. Bows, crossbows, staves and empty hands are not
// in it, which is the rule the first pass wrote as "a blade in his hand".
constexpr uint32_t kEvery = kSwords | kAxes | kMaces | kSpear;
}  // namespace arms

// Which family a weapon is, or `arms::kNone` for a hand that throws no skill at all -- empty,
// a bow, a crossbow, a staff, a shield in the right hand. A shield is `kShield` and is found
// by the same call, because the left hand asks the same question.
uint32_t familyOf(const content::Arm* weapon);

// What that family is called, for the card and for the refusal: "a one-handed sword", "an axe".
const char* familyName(uint32_t family);

// Every family a skill may be thrown with, as short words -- "Axes", "Maces"; "2-hand swords",
// "2-hand axes"; "Any weapon". Filled into `out` in the table's own order and the count comes
// back. The cards print them on one line with commas (`familiesListed`), the user's of
// 2026-09-29 ("use commas axes, 1-hand sword"); the short words are what keep the longest,
// Slash's, clear of its label.
int familiesNamed(uint32_t families, const char** out, int room);
// The same, joined: "Axes, Maces".
std::string familiesListed(uint32_t families);

// Whom a cast lands on. One target is all that is built; the two area shapes are written down
// because the cooldown state and the request are the same for them and choosing the shape later
// should not mean changing either. Ring is Cyclone -- everything within a tile of the caster --
// and Arc is Slash's three tiles off his facing.
// Line is Power Wave's: every body within half a tile of the line from him toward what it was
// thrown at, out to its reach -- the curtain sweeps through them all (the user, 2026-09-28).
// Fan is Skillshot's: `arrows` lines out from him, one straight at the body he aims at and the
// rest `kFanDegrees` apart either side, each striking every body within `kLineHalfWidth` of it
// from her feet to the row's reach -- MU2's `Realm.Passes`, off MuMain's Triple Shot, whose
// arrows fly on through what they strike (`Kind = 1`). MU2 left the first 0.6 tiles out, which
// let a monster pressed against her take nothing.
enum class Spread : uint8_t { One, Ring, Arc, Line, Fan, Beam };
// How many bodies one Lightning chain strikes at most: eleven, not the fourteen its window
// would reach (the user, 2026-10-04). The window and the arm keep their time. Realm::channel.
constexpr int kLightningBodies = 11;
constexpr float kFanDegrees = 15.0f;

// Half the Line's width, in tiles: the curtain is 0.91 m across, so a body whose middle is within
// three quarters of a tile of the line is in its way.
constexpr float kLineHalfWidth = 0.75f;
// And how far the line runs, in tiles: the whole of the wave's visible sweep -- twenty frames at
// sixty units, twelve tiles -- where the spell's own reach (six) is only how far off the body it
// is aimed at may stand. The user, 2026-09-28: "we need to increase range for that spell because
// it goes far"; first cut stopped at six, where MU starts fading it.
constexpr float kLineTiles = 12.0f;

// **Aqua Beam's** `Spread::Beam`: MU's client asks `AttackCharacterRange(..., 150.f)` once, on the
// beam's birth, at four points stepped 150 units along his facing from his hand, which
// `CalcAddPosition(o, -20, -90, 100)` puts 90 units ahead of him (ZzzEffect.cpp:1018-1029,
// ZzzCharacter.cpp:4551-4554). So four circles of a tile and a half round 2.4, 3.9, 5.4 and 6.9
// tiles out, and whatever stands in any of them is struck. WebZen takes the client's lists as
// sent (CGBeattackRecv), so a body inside two circles was in two packets and struck twice; here
// it is struck once, as OpenMU's frustum strikes it -- ours, and marked.
constexpr float kBeamStart = 0.9f;
constexpr float kBeamStep = 1.5f;
constexpr int kBeamPoints = 4;
constexpr float kBeamRadius = 1.5f;
constexpr float kBeamTiles = kBeamStart + kBeamStep * float(kBeamPoints) + kBeamRadius;

struct SkillRow {
    int32_t number = 0;
    const char* name = "";
    // What it costs. 0.75's own column, and the client's `Skills.txt` agrees on all six.
    int32_t mana = 0;
    // How far it reaches, in tiles. OURS: 0.75 gave each skill its own range and then added two
    // on top, because the skills were gap-closers thrown from a step or two out. Nothing closes
    // a gap here, so the reach is the knight's own and the column exists to be read rather than
    // to vary.
    float reach = 1.0f;
    // The multiplier on the blow: `force + strength * forcePerStrength + energy * 0.001`.
    // `force` is 0.75's own 2 on Falling Slash and is spread around it on the other four;
    // `forcePerStrength` is 1/K_dmg and is ours.
    float force = 1.0f;
    float forcePerStrength = 0.0f;
    // The cooldown before haste, in ticks. INVENTION, all of it: 0.75 passes `cooldownMinutes`
    // on nothing, and the client's `SkillAttribute[].Delay` is a Season 6 field that is zero for
    // all six of these.
    int32_t coolTicks = 0;
    // Whether the blow shoves what it hits one tile. 0.75's `movesTarget`, kept on the three
    // single-target skills and dropped on the two area ones -- scattering a crowd is the
    // opposite of what a crowd skill is for.
    bool knock = false;
    Spread spread = Spread::One;
    // A buff's own duration in ticks and what it multiplies incoming damage by, flattened onto
    // the skill as MU2's `Rows.Skill` flattens them: 0.75 has no effect two skills share.
    int32_t boonTicks = 0;
    float damageTaken = 1.0f;
    // One line of what it does, for the tooltip. Written from the row itself -- what it hits,
    // what it is for -- in the voice `describe.cpp` uses for an item's own line.
    const char* tells = "";
    // The player library's action, which is both what the figure plays and -- because in MU a
    // swing rate IS the length of the clip it swings with -- where the cooldown's floor comes
    // from. `WSclient.cpp:4280`: SKILL_SWORD1 + (skill - FallingSlash).
    int32_t clip = 0;
    // The wave, by the key `sounds.json` carries. Cyclone and Slash share SWORD4, which is MU's
    // own reuse and not a slip here.
    const char* sound = "";
    // Whether this project has built it yet. The other five are in the table so that the bar,
    // the save's learned mask and the log all have their final shape from the first one, and so
    // that the next session adds a row's behaviour rather than a row.
    bool built = false;
    // **Which hands may throw it**, as a mask of `arms::` bits -- the column §3.1b is about.
    // For the five attacks 0.75 carried it is traced rather than chosen: the families are the
    // weapons that granted that skill in the original (`Version075/Items/Weapons.cs:93-132`,
    // and §1.2's table), so Falling Slash is the axes' and the maces' because the Morning Star,
    // the Double Axe, the Tomahawk, the Battle Axe and the Nikkea Axe were what carried it.
    // Defense is `kShield`, which is `Armors.cs:40`. The three past 0.75 are ours.
    uint32_t families = arms::kEvery;
    // **What the orb asks of him before he may read it**, in levels, and it is here to be READ
    // rather than enforced: the requirement that stops a young knight learning Slash is the orb's
    // own (`ItemRow::needLevel` and `teachesLevel`, checked in `Realm::useItem`), because a
    // requirement belongs to the thing you pick up. This column carries the same number so the
    // card can print "Learned at level 52" without going looking for an item, and so the two can
    // be checked against each other. §3.3's ladder is where both come from.
    int32_t needLevel = 0;
    // ---- the wizard's columns, appended so the knight's rows above need not name them -------
    // Who may throw it at all. A knight's rows are the knight's and a spell is the wizard's; an
    // orb or a scroll asks the same question of the class before it teaches.
    Kin kin = Kin::DarkKnight;
    // A spell and not a blow: its damage is the WIZARDRY sum (`sim::cast`, off energy and the
    // staff) and not the swing's, it asks nothing of the hand -- a staff grants nothing in 0.75
    // (`Weapons.cs:152-159`, `skillNumber` 0 on all eight) and a spell is thrown bare-handed as
    // well -- and its clip runs at MagicSpeed rather than AttackSpeed (`SetAttackSpeed`, the
    // `PLAYER_SKILL_HAND1..` loop at ZzzCharacter.cpp:939).
    bool wizardry = false;
    // The spell's own `AttackDamage`, which the wizardry band adds (`GetSkillDmg`).
    int32_t damage = 0;
    // A second clip the drawing picks between on a coin: `PLAYER_SKILL_HAND1 + rand() % 2`
    // (ZzzCharacter.cpp:1339). Both are the same length, so the sim reads `clip` alone.
    int32_t clipOther = 0;
    // How fast a thrown spell crosses the ground, in tiles a second: Energy Ball's sixty units a
    // reference frame is fifteen, Fire Ball's fifty is twelve and a half (`Direction` in each
    // one's `CreateEffect` arm). The realm times the landing off it and the drawing flies at it,
    // so the two cannot part.
    float flies = 15.0f;
    // Whether a blow that lands and does not kill pushes the body a tile away from the caster
    // (`Realm::push`). 0.75's Lightning element: `TryApplyElementalEffectsAsync` moves the target
    // one tile (`MoveRandomlyAsync`), after the blow and never on a body the blow killed.
    bool pushes = false;
    // **A channel**: how many ticks it runs once cast, and how often it strikes while it does.
    // Nought for everything but Lightning (the user, 2026-09-28: "the first cast duration spell").
    // While it runs he stands in its clip, held, and every `pulseTicks` it strikes everything in
    // its shape; nothing else is thrown until it ends (`Realm::channel`).
    int32_t channelTicks = 0;
    int32_t pulseTicks = 0;
    // And the window it strikes in, in ticks from the cast: from `strikeFrom` to `strikeUntil`
    // (the arm up in its clip, "when the hand is up only then start channeling"); the rest of the
    // channel is the wind-up and the arm coming down.
    int32_t strikeFrom = 0, strikeUntil = 0;
    // And how many of its strikes one body may take in a cast: the channel is for a crowd, and
    // seven strikes into a lone monster was a one-shot ("when there is a single monster the DW
    // casts all lightning to one monster and basically one-shots him", 2026-09-28; two was still
    // "overpowered on single target", so it is one). 0 is no cap.
    int32_t strikesEach = 0;
    // **A fall and not a flight**: how many ticks it takes to land wherever the body stands, at
    // any distance -- Meteorite's rock drops out of the sky onto the target (`Meteor::fallSeconds`,
    // 0.34 s, seven ticks) rather than crossing the gap from his hand. 0 flies at `flies`.
    int32_t fallTicks = 0;
    // **A rock on every body round the one it is called on** (the user, 2026-09-28: "meteor did
    // not landed on multiple monsters around", then "only one meteor was flying"): every body
    // within this many tiles of the aimed one at the let-go gets its own, each landing its own
    // blow; only the aimed body pays back. 0 is the one body.
    float splash = 0.0f;
    // **A blink**: thrown at the ground under the pointer rather than at a body, and what it does
    // is put him there (`Realm::blink`), up to `reach` tiles off. Teleport's alone.
    bool blinks = false;
    // **A chill**: how many ticks what it strikes walks at `kChillFactor`. Ice's, 0.75's ten
    // seconds (`IsIced`). 0 for none.
    int32_t chillTicks = 0;
    // **A poison**: how many ticks what it strikes goes on being hurt, a pulse every
    // `kPoisonEvery`. Poison's, 0.75's twenty seconds. 0 for none.
    int32_t poisonTicks = 0;
    // ---- the elf's columns (sprint 15), appended so no row above need name them -------------
    // Thrown with anything or nothing in hand: her buffs, which neither a bow nor a shield
    // decides.
    bool anyHand = false;
    // **Heal**: health put back at once, `5 + energy / 5` (OpenMU's HealEffectInitializer).
    bool mends = false;
    // **Greater Damage**: `3 + energy / 7` added to every blow after the defence, for this long
    // (GreaterDamageEffectInitializer, sixty seconds; AttackableExtensions.cs:185). 0 for none.
    int32_t mightTicks = 0;
    // **Skillshot**: how many arrows the fan looses. Each body struck costs one. Penetration's
    // one is a fan of a single lane; a Piercing Volley makes it more (`lanesOf`).
    int32_t arrows = 0;
    // **A summon**: the monster number it raises, 0 for none (the map above).
    int32_t summons = 0;
    // ---- Flame's, appended after the elf's ------------------------------------------------
    // **A fire on the ground**: lit at the let-go on the tile of the body it was thrown at, it
    // strikes this many times, `kBurnEvery` apart, everything within `burnTiles` of the tile's
    // centre -- whoever stands in it then, not whoever stood in it when it was lit. 0 for none.
    int32_t burns = 0;
    float burnTiles = 0.0f;
    // ---- Hellfire's, appended after Flame's -----------------------------------------------
    // **Where in its clip it is let go**, as a share of the clip: half for everything else --
    // the arm at the bottom of the swing, `Showing::kLandingPoint` -- and Hellfire's landing,
    // the key his hips come down on (the user, 2026-10-02: "when char lands than there is
    // hellfire").
    float release = 0.5f;
    // ---- Twister's, appended after Hellfire's ---------------------------------------------
    // **A fire that walks**: tiles a tick it moves from his feet along where he aimed, striking
    // on `kStormFirst`/`kStormEvery` rather than lit under the body. 0 for a fire that stays put.
    float walks = 0.0f;
    // ---- Fire Breath's, appended after Twister's ------------------------------------------
    // **The mount's and not his**: known while a Horn of Dinorant with life left is worn
    // (`Realm::knows`, no learned bit and nothing in the save), thrown only while it is ridden
    // (`Body::riding`, so never on a safe tile), with any weapon or none
    // -- it is the dragon that breathes.
    bool mounted = false;
    // **Whether a press aims it at the ground under the pointer** rather than at a body: the
    // shapes that have a direction -- Aqua Beam's beam, Power Wave's line, the knight's arcs and
    // Twister's walking storm. Thrown from the keys or the right button, he turns to where the
    // mouse is and casts that way, with or without a monster there (the user, 2026-10-02: "if we
    // cast skill from quick bar or right click slot it cast to direction where hover mouse is
    // hovered"), as MU's client casts them at its own angle into empty air. The rings round him
    // have no direction and the single throws need a body to fly at.
    bool aimsAtPointer() const {
        return spread == Spread::Beam || spread == Spread::Line || spread == Spread::Arc ||
               walks > 0.0f || showers();
    }
    // **A shower**: rocks out of the sky on random ground within `splash` of where it is called
    // -- Meteorite's since 2026-10-04 (the user: "click on any spot on ground and on that area
    // meteors will drops on some random places on some 4 tile radius", "every time char rises
    // hand meteors drops on that location"). Ice and Poison keep the splash with no fall: a
    // body each round the aimed one (Realm::rain). Realm::shower.
    bool showers() const { return splash > 0.0f && fallTicks > 0; }
    // **Cast with no monster to cast at** (the user, 2026-10-05: "evil spirit,twister,hellfire is
    // aoe spell allow to cast it without monster"): the spirits go round him, the storm walks off
    // his facing, the ring burns round him, empty or not -- mana and clip spent on air, which
    // 0.75's "no target, no skill" refused. Ours.
    bool castsBare() const {
        return number == skill::kEvilSpirit || number == skill::kTwister ||
               number == skill::kHellfire;
    }
    // Whether it is cast on the caster and takes no target.
    bool onSelf() const { return boonTicks > 0 || mends || mightTicks > 0 || summons > 0; }
    // Whether it is an aura: a self-cast that is not a summon. Blood Castle's court lets these
    // be raised on its safe tiles, to go into the fight with them (Realm::throwSkill).
    bool aura() const { return boonTicks > 0 || mends || mightTicks > 0; }
    // **A primary: no cooldown, cast over and over.** The wizard's Energy Ball on the quick
    // slot is his auto-attack (the user, 2026-09-28), paced by its own clip and nothing else,
    // and like a swing it can be walked out of and a hit pays mana back.
    bool primary() const { return coolTicks <= 0 && !onSelf(); }
    // Whether it flies to what it is thrown at, rather than being struck at arm's length.
    bool thrown() const { return reach > 1.5f && channelTicks == 0; }
    bool channelled() const { return channelTicks > 0; }
    // Whether this hand may throw it. One test, asked by the realm before it spends anything
    // and by the plate before it draws the key lit -- they must not be able to disagree.
    bool suits(uint32_t family) const {
        return wizardry || anyHand || (family != arms::kNone && (families & family) != 0);
    }
};

// How many skills the sim has room for: the knight's six of 0.75, the three that fill out the
// families past it, and the wizard's Energy Ball, Soul Barrier, Fire Ball, Power Wave and
// Lightning, Meteorite, Teleport, Ice and Poison -- and Flame, Evil Spirit, Hellfire, Twister,
// Inferno and Aqua Beam, on the end past the elf's. The learned mask is sixty-four bits since
// Inferno, the thirty-third (`Body::learned`, and the save writes it whole); past sixty-four it needs widening
// again, which the static_assert below says. Also the width of a body's cooldown array -- and
// the learned mask is by INDEX, so a new row goes on the END of the table or an old save
// gives a knight somebody else's skill.
constexpr int kSkills = 36;
static_assert(kSkills <= 64, "the learned mask (Body::learned) is sixty-four bits");

// How many bodies one area skill may catch. Nine tiles are within a spin's reach and nothing
// stands two deep on one, so this is roomy on purpose -- it is a bound so that a cast allocates
// nothing, not a rule about crowds.
// 24 and not 16 since the wizard's spells reach the screen (2026-10-03, nine tiles).
constexpr int kVictims = 24;

// A shower's rocks (Realm::shower): how many fall each time his arm goes up, and how far round
// where each lands it strikes. Six rocks of two tiles over a disc of four cover much of it and
// leave a gap or two, which is the randomness the user asked for; the first rock always lands
// within a tile of the spot he clicked. All ours, 2026-10-04: eight at first, six since the user
// asked 'nerf meteor amount of meteors little bit' -- with Arcane Echo's second shower a long
// fight was a sky of rocks, and fewer rocks also hit fewer bodies at a shower's edge.
constexpr int kShowerRocks = 6;
constexpr float kRockBlast = 2.0f;
constexpr float kFirstRockOff = 1.0f;
// **A rain, big rocks and small** (the user, 2026-10-04: "has to fele more like meteor rain,
// some balls are bigger some are smaller", "bigger do more damage smalleer not so much"): each
// rock rolls a weight in this band, which scales its blow, its blast's reach and its drawing
// (said in hundredths in the `Loosed`'s `c`), and they come down over this many ticks rather
// than on one. A body under several takes the heaviest. Ours.
//
// **Wider, and mostly small** (the user, 2026-10-04: "ad more variations of rock sizes"): 0.4 to
// 2.0, drawn as the square of an even roll, so most of a rain is pebbles and a boulder is rare
// (a third under 0.6, one in ten over 1.6; 0.93 on average). A rock's blast grows with the
// root of its weight, 1.3 to 2.8 tiles, so a boulder does not cover the whole shower.
constexpr float kLightestRock = 0.4f, kHeaviestRock = 2.0f;
constexpr int32_t kShowerSpreadTicks = 12;

// Half the Arc's spread, in radians: 67.5 degrees either side of where he is facing, which is
// exactly the three compass eighths of "ahead and the two diagonals beside it". Written as an
// angle rather than as three tiles because a body stands at a fractional position and a tile
// test would drop a monster straddling the line between two of them.
constexpr float kArcHalfAngle = 1.17809725f;

int skillCount();
const SkillRow& skillAt(int index);
// The row for MU's number, or null. And its index in the table, or -1, which is what a learned
// bit and a cooldown are keyed on -- not the skill number, so the arrays stay six wide.
const SkillRow* skillNumbered(int32_t number);
int skillIndexOf(int32_t number);

// ---- the two formulas (docs/skills-dk.md §3.2) --------------------------------------------

// 250 weighted points would double the casts; 300 is the tuned number and is the one knob that
// decides WHEN a build runs out of cooldown. See the anchor table in the doc.
constexpr float kAgilityPerDoubling = 300.0f;

// The multiplier on a skill's blow. 0.75's `2 + energy/1000` with strength added at the row's
// own rate, which is the whole of the user's rule: strength is force, as energy is for a wizard.
float force(const SkillRow& row, const HeroPoints& points);

// How long the clip itself takes, in ticks, at this character's attack speed -- `keys /
// ((authored + attackSpeed * 0.004) * 25)`, the same chain `sim/swings.cpp` walks for a swing.
// Zero when the cooked tables do not carry the action, in which case the caller keeps the base.
// `extra` is speed standing on him beside the stat -- a Frenzy rune's -- added to whichever of
// AttackSpeed or MagicSpeed the clip reads.
int32_t castTicks(const content::Tables& tables, Kin kin, int agility, const content::Arm* right,
                  const content::Arm* left, const SkillRow& row, int extra = 0);

// **The wizard's cooldown spells cast quicker** (the user, 2026-10-03: "we need to increase DW
// meteor, ice, poison, lighting attack speed for animation"): Meteorite's, Ice's and Poison's
// clips run this much faster than MagicSpeed alone makes them, and the realm holds him for the
// shorter clip, which the drawing fits the clip to. Lightning's channel is written shorter on
// its row by the same factor. Ours.
constexpr float kSpellQuicken = 1.4f;

// How long the clip takes at its authored pace, in ticks -- what the drawing plays a knight's
// skill with a cooldown at (game/play.cpp, `swingPace` 1 on a cast); a primary is fitted to
// castTicks instead. Zero as castTicks is.
int32_t authoredCastTicks(const content::Tables& tables, const SkillRow& row);

// A wizard's MagicSpeed: what his spells' clips are quickened by, as AttackSpeed quickens a swing.
float magicSpeedStat(Kin kin, int agility);

// The cooldown, in ticks: `base / (1 + agility/300)`, never shorter than `floorTicks`.
//
// A FLOOR AND NOT A ZERO, which is WoW's answer and the reason nothing here divides by zero: the
// wall is the animation, and a cooldown under it means the key is ready before the knight has
// finished swinging -- which is what "no cooldown" means in play. For a buff the floor is its own
// duration plus two seconds instead, so Defense can never be permanent half damage.
int32_t cooldownTicks(const SkillRow& row, int agility, int32_t floorTicks);

// What the floor is for this row: the clip's length for an attack, the boon's duration and two
// seconds for a buff.
int32_t floorTicksFor(const SkillRow& row, int32_t clipTicks);

// How much of every blow Defense takes away, 0 to kGuardCap, off the shield he holds, his
// strength and his agility. INVENTION: the user's call of 2026-09-25 put all four stats in it,
// and on 2026-09-28 it became the mirror of the wizard's Soul Barrier with STRENGTH as the main
// stat, as energy is the wizard's -- it had weighed strength least (0.4, against 1.0 agility
// and 1.2 energy), so a knight spending on strength fell behind a wizard spending on energy.
// 0.75 gives a flat half. Guard points are 5 a point of the shield's defence (its plus and its
// wear counted), 1.1 a point of strength and 0.5 of agility; the share is
// kGuardCap * points / (points + 150), so it climbs fast early and flattens, and never reaches
// the cap. A new knight's is about 16%, a level-23 knight's who spent on strength about 34%.
// Read at the cast and held for the guard's whole length (`Body::boonDamageTaken`), which
// multiplies what gets past armour and the floor (`rules.cpp`).
constexpr float kGuardCap = 0.60f;
float guardPoints(const HeroPoints& points, int shieldDefense);
float guardShare(const HeroPoints& points, int shieldDefense);

// **Soul Barrier's share, on the same curve and under the same cap**: the knight's formula
// with energy in strength's place. INVENTION, the user's call of 2026-09-28 -- "almost
// identical at the beginning of the game, but the wizard's is energy and the knight's is
// strength". Barrier points are 5 a point of the shield's defence, 1.1 a point of energy and
// 0.5 of agility, so a new wizard and a new knight behind the same shield come within half a
// point of each other (16.5% against 16.3% behind a Buckler +1, 14.3% against 14.0% behind a
// Small Shield), and each spending on his main stat stays level with the other after. MU's own `10 + agility/50 + energy/200` percent
// (SkillTooltipModel.cpp:248) is a tenth of this at the start and is not followed.
float barrierPoints(const HeroPoints& points, int shieldDefense);
float barrierShare(const HeroPoints& points, int shieldDefense);

// **The elf's guard, Greater Defense, on the same curve and under the same cap** -- the user's
// of 2026-09-28: learned as early as Defense and Soul Barrier and standing level with them. Ours
// in everything but the name (0.75's Greater Defense is `2 + energy / 8` defence for a minute).
// Her main stat in the knight's strength's place is AGILITY, which is her damage as strength is
// his; energy takes agility's half-weight. She cannot carry a shield beside a bow, so a flat 15
// points stands where theirs is -- a Buckler +1's, which is what the knight's own 16.3% is
// measured behind -- and no shield is asked. A new elf's is 14.4%, beside theirs.
constexpr float kWardShieldPoints = 15.0f;
float wardPoints(const HeroPoints& points);
float wardShare(const HeroPoints& points);
// **A summon scales with her energy** -- the user's, 2026-09-28: "if an elf player decided to go
// full energy elf, that summon is actually doing good damage and can hold aggro". Ours: 0.75
// summons the breed's row as it stands (PlayerSummon.CreateAsync), which a levelled elf outgrows.
// **And with her level and her other stats** -- the user, 2026-10-02: "elf summons did not
// scale with elf stats and lvls, we need to fix that". Ours, in two steps:
//   * it stands at the breed's level plus a share of hers (`summonLevel`), and the breed's row
//     is carried up 0.75's own monster ladder to that level (`summonClimb`, the cooked breeds'
//     health, damage, defence and rates by level). **The better summon takes the bigger share**
//     (the user, the same day: "if elf gets better summon, he of course is better with
//     scaling", "it would be weird if goblin is same power as late game summon"): a tenth of
//     her level for the Goblin, a tenth more each summon up, six tenths for Bali. Beside a
//     level-60 elf the Goblin fights at 9 and the Golem at 30; at 150, at 18 and 48, so the
//     gap between them widens as she levels. A fifth for the Goblin and half for every breed
//     were each tried first and were too heavy (the user, on a level-102 elf's Goblin rolling
//     565: "scaling is to heavy");
//   * then her points: health times `1 + energy / 100 + vitality / 200`, and damage, defence and
//     both rates times `1 + energy / 200 + agility / 800` -- energy her summoner's stat, a tank
//     first and a weapon second; vitality lends half of energy's, agility (her bow's) a quarter.
// A new elf's Goblin is the breed's own. A level-102 elf of 351 agility and 110 energy has a
// level-13 Goblin of 645 health and a 50-71 bite, or a level-38 Golem of 3 000 and 142-155;
// at 300 energy the Golem has 5 500 and 181-198 (kSummonDamageShare's, 0.6 of what they were). One knob each, here.
// The minute every summon key cools for after one is raised (skills.cpp, the summons' rows).
constexpr int32_t kSummonCool = 1200;
constexpr float kSummonHealthPerEnergy = 1.0f / 100.0f;
constexpr float kSummonHealthPerVitality = 1.0f / 200.0f;
constexpr float kSummonForcePerEnergy = 1.0f / 200.0f;
constexpr float kSummonForcePerAgility = 1.0f / 800.0f;
constexpr float kSummonLevelShare = 0.1f;      // the Goblin's share of her level
constexpr float kSummonLevelShareStep = 0.1f;  // and each summon up takes this much more
// What a summon's bite keeps of all that (the user, 2026-10-03: "nerf summons damage").
constexpr float kSummonDamageShare = 0.6f;
float summonHealthRate(const HeroPoints& points);
float summonForceRate(const HeroPoints& points);
// `skill` is the summon's own (kSummonGoblin ... kSummonBali), which sets its share.
int summonLevel(int breedLevel, int heroLevel, int32_t skill);
// What one of the ladder's columns grows by from the breed's level to `level`: 1 at its own.
enum class Ladder : uint8_t { Health, Damage, Defense, AttackRate, DefenseRate };
float summonClimb(Ladder column, int breedLevel, int level);
// How far from her it hunts, and how far it strays before it walks back: OpenMU's
// SummonedMonsterIntelligence -- eight tiles round the owner, two tiles idle, five fighting.
constexpr int kSummonHunt = 8;
constexpr int kSummonTether = 2;
constexpr int kSummonTetherFighting = 5;
// Ours, the user's of 2026-10-02 ("when char is using mount wich is fast summon cant keep up",
// "summon teleport him self closer to char if he wall behind to much"). Walking back to her it
// keeps her pace, ridden or running, and a little over it so the gap closes rather than holds;
// and past `kSummonBlink` tiles from her, or with no road back to her, it steps out of the air
// behind her (Realm::blinkSummon). Past the hunt's eight would let it lose its quarry first.
// And then ('we need better system when summon falls behind, he has to teleport to char when
// char is out of reach'): six tiles, and a summon walking back that has not moved half a tile
// in kSummonStuckTicks -- caught on a corner, a fence or a crowd -- steps through too.
constexpr float kSummonCatchUp = 1.15f;
constexpr int kSummonBlink = 6;
constexpr float kSummonStuckGain = 0.5f;  // tiles
constexpr int kSummonStuckTicks = 30;     // a second and a half

// **How many lanes a fan looses**: the row's own arrows, and Penetration's three while a
// Piercing Volley is in her hands (`volleys`, Excellence::volleys; sim/items.h). One answer
// for the realm that strikes the lanes and the drawing that flies them.
constexpr int kVolleyLanes = 3;
// **How far a Penetration arrow flies**, in tiles: through the screen and out of it (the user,
// 2026-10-04: 'penetration has to fly through screen'), striking everything on the way, where
// the body it is loosed at may stand no more than its row's reach off. ours: MU's frustum is 8.
constexpr float kPierceTiles = 16.0f;
inline float laneTiles(const SkillRow& row) {
    return row.number == skill::kPenetration ? kPierceTiles : row.reach;
}
inline int lanesOf(const SkillRow& row, int volleys) {
    return row.number == skill::kPenetration && volleys > 0 ? kVolleyLanes : row.arrows;
}

// Heal's health and Greater Damage's bonus, off her energy.
int healOf(const HeroPoints& points);
int mightOf(const HeroPoints& points);

// Whichever of the two a self-cast row is, so the realm, the card, the scroll and the strip ask
// one question and cannot say different numbers for the same buff.
float boonShare(const SkillRow& row, const HeroPoints& points, int shieldDefense);

// A buff's words, for every card that describes one, so the skill key, the orb and the strip
// cannot say it three ways. `absorbed` is a share of a blow as a player reads it, "30%".
// `spoken` is a length of time: "5:00" from a minute up, "4 s" under it.
std::string absorbed(float share);
std::string spoken(float seconds);

}  // namespace mu::sim
