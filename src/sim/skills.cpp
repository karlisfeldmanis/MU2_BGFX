#include "sim/skills.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "sim/swings.h"

namespace mu::sim {
namespace {

// The knight's six, in MU's own order, and then the three that fill out the weapon families.
// The mana column is 0.75's on the first six and is corroborated by the client's own
// `Skills.txt` -- 30, 9, 9, 8, 9, 10, two independent readings agreeing.
//
// **The order of this table is the save's**, because `learned` is a bit per INDEX: the six come
// first and forever, and anything new is appended. A row inserted in the middle would hand a
// saved knight a skill he never learned.
//
// The three invented columns, and the reasoning is `docs/skills-dk.md` §3.2:
//   * `force` spreads around Falling Slash's 2.0, which is the class's own multiplier in 0.75.
//     The two area skills are paid in coverage rather than in force, so Cyclone's 1.3 is the
//     weakest key against one monster and the strongest against four.
//   * `forcePerStrength` is 1/K_dmg: 1/1000 on the anchor, gentler on the jab, steeper on the
//     two-handed sweep.
//   * `coolTicks` is at 20 Hz: 60 is three seconds.
// Hellfire's landing key, 6 of 18 (see its row).
constexpr float kHellfireLanding = 6.0f / 18.0f;
// Aqua Beam's throw, key 5 of 12 (see its row).
constexpr float kAquaThrow = 5.0f / 12.0f;

constexpr SkillRow kRows[kSkills] = {
    // Defense 18: a buff for 30 mana (`DefenseEffectInitializer`). Five minutes of it, where
    // 0.75 gives four seconds: the user's call of 2026-09-25, ours and not MU's. What it takes
    // off a blow is no longer this row's flat 0.50 but `guardShare`, off the shield and the
    // four stats; the column keeps 0.75's half as the record of what was replaced. The
    // twelve seconds of cooldown are floored at its own duration and two, so it is never
    // permanent: a guard that lapses is down for two seconds before it can be raised again.
    {skill::kDefense, "Defense", 30, 0.0f, 1.0f, 0.0f, 240, false, Spread::One, 6000, 0.50f,
     "A guard raised behind the shield for five minutes. The better the shield and the "
     "stronger, quicker and keener the knight, the more of every blow it takes.", 187,
     "player_skill_defense", true, arms::kShield, 6},
    // The knock is OFF on all six, and the column is kept rather than removed. 0.75 sets
    // `movesTarget` on the knight's five and it puts the monster on a neighbouring tile at once --
    // which is a one-tile teleport, and the user's rule of 2026-09-22 ("no sudden position changes
    // or teleports") is against exactly that. It comes back the day something can SHOVE a body
    // over a few ticks instead of moving it; until then `knock` stays false and `Realm::shove` is
    // written and unreached.
    //
    // **The families are 0.75's own carriers** (§1.2, `Version075/Items/Weapons.cs:93-132`):
    // whichever weapons granted a skill in the original are the weapons that may throw it here.
    // Not one of the five is a choice made at this desk.
    //
    // Falling Slash: the Morning Star (a mace), the Double Axe and the Tomahawk (one-handed),
    // the Battle Axe and the Nikkea Axe (two-handed). Axes and maces, in either hand -- and the
    // icon MU painted for it is an axe, which is the same fact from the artist's side.
    {skill::kFallingSlash, "Falling Slash", 9, 1.0f, 2.0f, 1.0f / 1000.0f, 80, false, Spread::One,
     0, 1.0f, "An overhead blow brought down on one body -- the heaviest single strike he has.",
     60, "player_skill_sword1", true, arms::kAxes | arms::kMaces, 13},
    // Lunge: the Gladius, and nothing else in 0.75 carried it. A one-handed sword, which is what
    // MU's own description says in as many words -- "used with weapons like Gladius and Katana
    // to execute quick stabs".
    {skill::kLunge, "Lunge", 9, 1.0f, 1.4f, 1.0f / 1400.0f, 60, false, Spread::One, 0, 1.0f,
     "A thrust straight ahead: the cheapest key and the one that comes back soonest.", 61,
     "player_skill_sword2", true, arms::kSword1, 20},
    // Uppercut: the Sword of Assassin, the Falchion and the Serpent Sword. All one-handed.
    {skill::kUppercut, "Uppercut", 8, 1.0f, 1.7f, 1.0f / 1200.0f, 60, false, Spread::One, 0, 1.0f,
     "A rising blow under the guard, between the jab and the overhead in force and in wait.",
     62, "player_skill_sword3", true, arms::kSword1, 12},
    // Cyclone: the Blade (a one-handed sword), the Berdysh and the Great Scythe (polearms). The
    // odd pair is MU's own, and it reads as a spin with something long or something quick.
    {skill::kCyclone, "Cyclone", 9, 1.0f, 1.3f, 1.0f / 1400.0f, 100, false, Spread::Ring, 0, 1.0f,
     "A spin that catches everything within a tile: weakest against one, strongest in a crowd.",
     63, "player_skill_sword4", true, arms::kSword1 | arms::kSpear, 36},
    // Every row is built now. `built` used to mean "and the bar has a key for it", which is why
    // Slash sat here false with its arc written and tested; the list of 2026-09-23 holds every
    // learned skill and the four keys are the player's to fill from it, so the two questions
    // came apart and this one is the simple one again.
    // Slash: the Giant Sword, the Crystal Sword and the Chaos Dragon Axe -- two two-handed
    // swords and a two-handed axe, which is exactly what MU's own description says ("only works
    // with the Giant Sword, Chaos Dragon Axe, or Crystal Sword"). Of those three Lorencia cooks
    // the Giant Sword; the other two are elsewhere and the family is written for them anyway.
    {skill::kSlash, "Slash", 10, 1.0f, 1.8f, 1.0f / 1000.0f, 120, false, Spread::Arc, 0, 1.0f,
     "A wide sweep across the three tiles he faces, thrown with both hands on the haft.", 64,
     "player_skill_sword4", true, arms::kSword2 | arms::kAxe2, 52},

    // ---- past 0.75, and appended so the six keep their indices -------------------------------
    //
    // **Ours, in everything but the name.** Gating the five above on their own carriers leaves a
    // one-handed axe, a mace and a spear with a single key each, which is a dead bar -- so three
    // more rows, each one a knight skill MU itself went on to write (41 in 0.95d, 42 and 43 in
    // Season 6; `skill_eng.bmd` is where the numbers and the names come from). What they DO is
    // this project's, in §3.2's shape: no OpenMU row for any of them exists in 0.75 to be
    // followed, and inventing one and calling it traced would be worse than saying this.
    //
    // Twisting Slash plays MU's own spin, `PLAYER_ATTACK_SKILL_WHEEL` (action 65,
    // SkillCast.cpp:306-312), 13 keys at 0.24 -- slower than a sword skill, as MU's is. Rageful
    // Blow plays MU's own `PLAYER_ATTACK_SKILL_FURY_STRIKE` (action 66, ClassAttack.cpp:986), 11
    // keys at 0.38 (ZzzCharacter.cpp:1008), with SOUND_FURY_STRIKE1 as it starts and the weapon
    // thrown and the ground broken by game/fx/fury.h. Death Stab reuses 0.75's thrust, the
    // nearest blow, and the note is here rather than in the drawing.
    //
    // Twisting Slash is the one skill of the three MU puts no weapon requirement on at all --
    // it is the knight's staple, "whirl his weapon violently around him" -- so it is the one row
    // here that every family may throw, and it is what keeps the mace and the spear from having
    // a bar of two. Its mana is MU's: MuMain's own orb tooltip reads `Twisting Slash Skill
    // (Mana:22)` (docs/mu-scrolls-and-orbs.md §5).
    //
    // **Learned at 60 and not 28, Rageful Blow at 120 and not 44** (the user, 2026-10-02: "lets
    // increase lvl requirments", then "increase ragefull lvl requirtment to 120 and twisting to
    // 60"), with their multipliers raised the same day.
    //
    // **No cooldown** (the user, 2026-10-01: "remove cooldown from twisting slash"), which makes
    // it a primary: paced by its spin alone, held on the key, its mana the only limit.
    //
    // **1.5 and not 1.2** (the user, 2026-10-02: "twisting slash needs multiplier"). Ours: at 1.2
    // it was a plain swing round him, and the bot pressed it 71 times in three hours. **1.7**
    // the same day ("we need to boost twisting slash little bit"), with mana back on every body
    // it wounds (`kTwistManaShare`, recovery.h).
    {skill::kTwistingSlash, "Twisting Slash", 22, 1.0f, 1.7f, 1.0f / 1500.0f, 0, false,
     Spread::Ring, 0, 1.0f,
     "A whirl of whatever he is holding, into everything within a tile. Every weapon can throw "
     "it; none throws it hard.",
     65, "player_skill_sword4", true, arms::kEvery, 60},
    // Rageful Blow: **any weapon**, on the user's word of 2026-09-23. It was written for the
    // heavy hands -- MU's description is a "colossal area attack that unleashes shockwaves ... to
    // crush multiple opponents", which reads as something brought DOWN rather than drawn across --
    // and it is opened to all six families for the same reason Twisting Slash is: MU puts no
    // weapon requirement on either, and a knight of any hand should have a heavy answer as well as
    // a wide one. So the two skills past 0.75 that every family shares are the pair, spin and
    // crush, and every gate in the table below them is traced to a carrier.
    //
    // **The knight's cooldown blow: 3.0 and not 2.1, and its 8.5 s kept** (the user, 2026-10-02:
    // "ragefull blow will be cooldown spell with some multiplier"). Ours: the hardest blow in
    // the table, paid for by the longest wait.
    {skill::kRagefulBlow, "Rageful Blow", 25, 1.0f, 3.0f, 1.0f / 900.0f, 170, false, Spread::Arc,
     0, 1.0f,
     "The weapon driven down into the ground, and what it breaks is the three tiles ahead of "
     "him.",
     66, "rage_blow_1", true, arms::kEvery, 120},
    // Death Stab: the spear's, and MU gates it on the hand too -- `SkillWarrior` refuses it with
    // a staff in the right hand (SkillCast.cpp:157) and the skill has been a spear's in every
    // version that hands it out. The hardest single blow in the table, and the point of carrying
    // a polearm: a spear's answer to Falling Slash, which it may not throw.
    //
    // **Its own clip**, MU's PLAYER_ATTACK_DEATHSTAB (SkillCast.cpp:302-304) at 0.25
    // (ZzzCharacter.cpp:925): player.muc's 71, where Lunge's 61 stood in for it until 2026-10-02
    // (the user: "lets work on Cyclone and Death Stab"). Its thrust lands at half the clip -- the
    // right hand from 77 units behind him to 71 ahead between keys 2 and 3 of 6 -- so the blow
    // lands there as every swing's does. sKnightSkill2 is MU's own for it (ZzzCharacter.cpp:2631),
    // and its streaks, cones and the wound on what it strikes are game/fx/deathstab.h.
    {skill::kDeathStab, "Death Stab", 15, 1.0f, 2.3f, 1.0f / 900.0f, 110, false, Spread::One, 0,
     1.0f, "The point driven through one body at speed. Nothing he has hits one thing harder.",
     71, "player_skill_sword2", true, arms::kSpear, 60},

    // ---- the wizard's, appended after the knight's nine ----------------------------------------
    //
    // Energy Ball, 0.75's row: `CreateSkill(EnergyBall, ..., DamageType.Wizardry, 3, 6,
    // manaConsumption: 1)` (Version095d/SkillsInitializer.cs:60, the same row 0.75 builds) --
    // three damage, six tiles, one mana. Born knowing it (`AddEnergyBallForDarkWizard`), so its
    // level is nought and there is no orb. No cooldown, which is MU's and is also the user's rule
    // of 2026-09-28: on the right-click slot it is the wizard's auto-attack, paced by its clip.
    // The clips are `PLAYER_SKILL_HAND1` and `HAND2`, 147 and 148, one of the two on a coin
    // (`SetPlayerMagic`); the wave is `SOUND_MAGIC`, played beside the bolt's creation
    // (ZzzCharacter.cpp:5142).
    //
    // **Nine tiles and not six** (the user, 2026-09-28: "lets also increase range for fireball and
    // energy ball"). Ours. The bolt lives twenty frames at sixty units, twelve tiles, so it still
    // reaches with room to steer.
    //
    // **One and a half times the band** (the user, 2026-10-02: "basic DW spells also multipliers
    // so its more balanced"). Ours: at 1.0 the bot's wizard dealt a third of the knight's damage
    // a minute until Meteorite arrived (--fights, seed 1: 1,716 against 5,829 at two hours).
    {skill::kEnergyBall, "Energy Ball", 1, 9.0f, 1.5f, 0.0f, 0, false, Spread::One, 0, 1.0f,
     "A bolt of light thrown at one body up to nine tiles off. Its force is his energy and his "
     "staff's.",
     147, "spell_magic", true, arms::kNone, 0, Kin::DarkWizard, true, 3, 148},

    // Soul Barrier 16: the knight's Defense in the wizard's hand, and every column but the mana,
    // the wave and the class is Defense's own so the two classes stand level (the user, 2026-09-28): five minutes, a
    // twelve-second cooldown floored at its own length and two, **thrown on himself only**, and
    // **only behind a shield** -- `kShield`, which the wizard wears in the Small Shield, the
    // Buckler and the Skull Shield. MU casts it on a party member as well (`ClassAttack.cpp`,
    // the `SelectedCharacter` arm); there is no party here and the user ruled it self-only.
    //
    // **And it is drawn as Defense is**, the user's of 2026-09-28: the knight's stance (187) and
    // his green cage, where MU casts it with `SetPlayerMagic`'s two hands and five blue
    // `MODEL_SPEARSKILL` ribbons (ClassAttack.cpp:1200, ZzzCharacter.cpp:4984). What stays MU's
    // own is the wave, `SOUND_SOULBARRIER`, and the mana, 70
    // (`VersionSeasonSix/SkillsInitializer.cs:134`). What it takes off a blow is `barrierShare`,
    // off energy where the knight's is off his body.
    //
    // Not `wizardry`: it throws no blow, and a spell row asks nothing of the hand, which this one
    // must.
    {skill::kSoulBarrier, "Soul Barrier", 70, 0.0f, 1.0f, 0.0f, 240, false, Spread::One, 6000,
     0.50f,
     "A barrier drawn up behind the shield for five minutes. The better the shield and the "
     "keener the wizard, the more of every blow it takes.",
     187, "spell_soul_barrier", true, arms::kShield, 6, Kin::DarkWizard},

    // Fire Ball 4, 0.75's row: `CreateSkill(FireBall, ..., DamageType.Wizardry, 8, 6,
    // manaConsumption: 3, energyRequirement: 40, elementalModifier: Fire)` -- eight damage, six
    // tiles, three mana. Fire is a gate with nothing behind it (`TryApplyElementalEffectsAsync`
    // defines no fire effect), so nothing past the blow is owed. The same two hands as Energy
    // Ball, `SetPlayerMagic`'s 147/148 on a coin, and `SOUND_METEORITE01`, which MU shares with
    // Meteorite as it shares SWORD4 between Cyclone and Slash.
    //
    // **A primary, as Energy Ball is**: no cooldown, paced by its own clip, walked out of like a
    // swing, and a hit pays back a twentieth of the pool (the user, 2026-09-28, "fireball dont
    // have cooldowns same as energy ball"). So it is the wizard's second auto-attack and the
    // natural thing for the right button once it is learned: about one and a half Energy Balls
    // against one body at forty energy (12-22 against 7-14) for three mana against one. Its forty
    // energy is asked by the scroll, where 0.75 asks it (`Realm::useItem`); 0.75 asks it again at
    // every cast, and energy never goes down here, so that second test is not written.
    //
    // It flies at fifty units a reference frame, twelve and a half tiles a second -- slower than
    // the bolt, which is the difference between the two in the air.
    //
    // Nine tiles and not six, with Energy Ball, on the same word; the fireball lives sixty frames.
    //
    // **1.8 times the band**, on the same word as Energy Ball's 1.5. Ours.
    {skill::kFireBall, "Fire Ball", 3, 9.0f, 1.8f, 0.0f, 0, false, Spread::One, 0, 1.0f,
     "A ball of fire thrown at one body up to nine tiles off: harder than an Energy Ball, for "
     "three times the mana.",
     147, "meteorite", true, arms::kNone, 0, Kin::DarkWizard, true, 8, 148, 12.5f},

    // Power Wave 11, 0.75's row: `CreateSkill(PowerWave, ..., DamageType.Wizardry, 14, 6,
    // manaConsumption: 5, energyRequirement: 56)` -- fourteen damage, six tiles, five mana, and no
    // element at all. **Every body in its line**, where 0.75 strikes the one it was thrown at: the
    // curtain sweeps on through and away (its mover never stops on the target), and the user ruled
    // on 2026-09-28 that what it passes through is hit ("it can go through multiple monsters").
    // `Spread::Line`, three quarters of a tile each side, out to the twelve tiles the wave visibly
    // sweeps (`kLineTiles`); it is aimed at a body within the six of its reach. Each body is struck
    // when the wave reaches it, and only the one it was aimed at pays mana back.
    //
    // **A primary like the other two**, the shape the user gave Fire Ball: no cooldown, paced by
    // its clip, a hit paying back. About twice Energy Ball against one body at fifty-six energy
    // (20-31 against 9-18) for five mana. The same two hands; `SOUND_MAGIC`, Energy Ball's wave,
    // which MU plays for both. Sixty units a reference frame, the bolt's fifteen tiles a second.
    //
    // **1.6 times the band**, on the same word: under Fire Ball, since it strikes the whole line.
    {skill::kPowerWave, "Power Wave", 5, 6.0f, 1.6f, 0.0f, 0, false, Spread::Line, 0, 1.0f,
     "A wave of light swept along the ground for twelve tiles, striking everything in its line "
     "harder than an Energy Ball.",
     147, "spell_magic", true, arms::kNone, 0, Kin::DarkWizard, true, 14, 148, 15.0f},

    // Lightning 3: 0.75's row for the numbers that survive -- seventeen damage, fifteen mana,
    // seventy-two energy -- and **this game's first channel** (the user, 2026-09-28): "when it
    // gets cast it has duration, the wizard uses a special animation and lightning finds all the
    // monsters around him and casts lightning to them". **As long as its clip**, 2.08 s -- 42 ticks
    // ("make it shorter, like actual animation length") -- and it strikes **only while his arm is
    // up** in it, from 0.7 s to 1.6 s (ticks 14 to 32, read off the clip frame by frame on the
    // bench): a strike every three ticks, seven in all, and **it goes round**, each at ONE body
    // within four tiles of him, and **no body more than once** (`strikesEach`: a lone monster is
    // struck once, not seven times -- two was still "overpowered on single target" -- and a crowd
    // of up to seven takes a strike each),
    // (`Spread::Ring` at `reach` 4) -- the next one clockwise from the last it struck, so the bolt
    // sweeps round the ring rather than lighting it all at once ("not to all monsters at the same
    // time but like rotation") -- pushing what it leaves standing a step away (`pushes`,
    // `Realm::push`). Ten seconds of cooldown, the user's, before agility's haste. He plays MU's
    // "Skill recovery" (183), one arm thrown up to the sky, looping for the whole of it -- the
    // user's pick of 2026-09-28 off a bench sheet, after 186 held read as frozen and 169 turned
    // out to be a mount's pose -- and cannot walk out of it (the rule of 2026-09-23). 0.75's Lightning is
    // one bolt at one body; what is kept of it is the bolt, the push and the thunder.
    //
    // **Forty mana, not 0.75's fifteen** (the user, 2026-09-28: "lightning has to spend more mana"):
    // a channel that sweeps a ring and pushes it back is worth a third of a young wizard's pool,
    // about what MU asks for Ice (38). Ours.
    //
    // **Each strike at twice the band** (`force` 2), the user's "lightning has to be stronger
    // because it's a cooldown spell": the spells that pay nothing to wait strike at one, and this
    // one waits five seconds. About 60-110 a strike at 120 energy against Fire Ball's 21-42 -- the
    // hardest single blow he has, once a body, into everything round him.
    // **No cooldown and the band's own force since 2026-09-30** -- Lightning, Meteorite, Ice and
    // Poison alike (the user: "do we even need cooldowns for ice, poison, lighting, meteor spells?
    // because animation is pretty long"): standard spells as Energy Ball, Fire Ball and Flame
    // are, each paced by its own clip, and so no longer twice (Meteorite three times) the band --
    // the doubling was the price of the wait, as Flame's was. The notes above and below that
    // speak of five seconds and of twice the band are the rows' history; the numbers are these.
    // **And half the doubling back on 2026-10-01** (the user: "we need to increase DW cooldown
    // spells damage because now we are using longer cast animation, which i really like"): the
    // long clip is a wait of its own, so Lightning, Ice and Poison strike at half again the band
    // and Meteorite at twice it -- half of what the five seconds had bought each. Ours.
    //
    // **A chain, and quicker** (the user, 2026-10-03: "we want that lightning also is a chain
    // spell like pyroblast rune", and its cast sped up with the other three): the first strike
    // from his hand into the body he cast at, each next leaping from the last body struck to the
    // nearest it has not struck within four tiles (Realm::channel), every two ticks. The channel
    // 42 -> 30 ticks and its window 14-32 -> 10-23, kSpellQuicken's 1.4; seven strikes still.
    //
    // **Harder and wider, the wizard's alone** (the user, 2026-10-03: "meteorite, ice, poison,
    // lightning has to be more powerful for DW, only DW can get more tiles"): a third harder, and
    // then "DW should get much more monsters on screen from 1 cast (except if there is wall)":
    // nine tiles round the body it lands on, about the half of the screen MU's camera shows,
    // stopped by walls as before (Realm::seen); Lightning a strike every tick, fourteen, six
    // tiles a leap. The runes that lend other classes a rock, a frost or a bolt keep their own
    // forces and reach (sim/items.h), so this is his. **A tenth down again** the same day (the
    // user: "lets nerf wizard little bit", after the bots had him 186-214 to the others' 175-190
    // at eight hours): Meteorite 2.6 -> 2.3, the other three 2.0 -> 1.8; the reach stays.
    // Eleven bodies at most and not fourteen, so a cast catches fewer (the user, 2026-10-04: '9
    // tile reach DW skillls is little bit to much, nerf it. i think its also for
    // ice,posion,lighting', 'i am talking how much monsters get the spell', then 'and play it
    // same time how it was before'): the window is the fourteen ticks it was, and the chain ends
    // at `kLightningBodies` (skills.h), his arm held up the rest of it. Six tiles a leap.
    {skill::kLightning, "Lightning", 40, 6.0f, 1.8f, 0.0f, 0, false, Spread::Ring, 0, 1.0f,
     "With his arm raised to the sky, lightning leaps from the body he points at into the next "
     "and the next within six tiles, throwing each back a step.",
     183, "spell_thunder", true, arms::kNone, 0, Kin::DarkWizard, true, 17, 0, 15.0f, true, 30,
     1, 10, 23, 1},

    // Meteorite 2: 0.75's row for the damage, `CreateSkill(Meteorite, ..., DamageType.Wizardry,
    // 21, 6, manaConsumption: 12, energyRequirement: 104, elementalModifier: Earth)` -- twenty-one
    // damage, one body, earth a gate with nothing behind it. MuMain drops it where the body
    // stands: `CreateEffect(MODEL_FIRE, to->Position, ...)` and SOUND_METEORITE01 at the let-go
    // (ZzzCharacter.cpp:5008), the Lich's own rock, falling for 0.34 s (`fallTicks` 7).
    //
    // **A rock on every body round it** (`splash`: everything within four tiles of the body he
    // called it on gets its own -- Lightning's reach, so a crowd fighting him is all under it;
    // two left half of four Bull Fighters out, "only 2 but there are 4 monsters"; the user,
    // 2026-09-28), where 0.75 drops one on the one body.
    //
    // **A cooldown spell** (the user, 2026-09-28), and so, as Lightning taught, harder and dearer
    // than the primaries: five seconds before agility's haste, **three times the band** (`force` 3,
    // about 100-170 at 104 energy against Fire Ball's 19-38 -- the heaviest single blow he has),
    // and thirty mana where 0.75 asks twelve. Nine tiles, with the other two he throws at a body.
    // All ours.
    //
    // **Cast in Lightning's pose** (the user, 2026-09-28: "use same casting animation as
    // lighting"): MU's "Skill recovery" (183), the arm thrown up to the sky, played once; the rock
    // is called at the middle of it, with the arm up, and he cannot walk out of it. MU casts it
    // with `SetPlayerMagic`'s two hands, 147/148.
    //
    // **Harder and wider, the wizard's alone** (the user, 2026-10-03: "meteorite, ice, poison,
    // lightning has to be more powerful for DW, only DW can get more tiles"): a third harder, and
    // then "DW should get much more monsters on screen from 1 cast (except if there is wall)":
    // nine tiles round the body it lands on, about the half of the screen MU's camera shows,
    // stopped by walls as before (Realm::seen); Lightning a strike every tick, fourteen, six
    // tiles a leap. The runes that lend other classes a rock, a frost or a bolt keep their own
    // forces and reach (sim/items.h), so this is his. **A tenth down again** the same day (the
    // user: "lets nerf wizard little bit", after the bots had him 186-214 to the others' 175-190
    // at eight hours): Meteorite 2.6 -> 2.3, the other three 2.0 -> 1.8; the reach stays.
    // Six tiles round the body it lands on and not nine, so a cast catches fewer (the user,
    // 2026-10-04: '9 tile reach DW skillls is little bit to much, nerf it', 'i am talking how much
    // monsters get the spell from meteor,ice,etc'), as Ice and Poison. Still nine tiles to cast.
    //
    // **A shower on the ground he clicks** (the user, 2026-10-04: "click on any spot on ground
    // and on that area meteors will drops on some random places on some 4 tile radius", "every
    // time char rises hand meteors drops on that location"): `kShowerRocks` rocks at random
    // within four tiles of the tile, each striking what stands within `kRockBlast` of where it
    // lands, a body once a cast (Realm::shower). Called on a body -- a right-click on a monster,
    // the bots -- it falls round that body. Ours.
    //
    // **Sixty mana and 1.6 the band** (the user, 2026-10-04: "pretty OP spell for dw it should
    // cost more mana"; fifty, then Inferno's two hundred, then "200 to much lets use 60, but
    // decreae multiuplier to 1.6"): the dearest of his throws, and lighter than the 2.3 it was
    // while one rock fell on each body -- a rain now catches a crowd for it.
    {skill::kMeteorite, "Meteorite", 60, 9.0f, 1.6f, 0.0f, 0, false, Spread::One, 0, 1.0f,
     "With his arm raised to the sky he calls a shower of burning rocks down on the ground up "
     "to nine tiles off, falling at random within four tiles of it.",
     183, "meteorite", true, arms::kNone, 0, Kin::DarkWizard, true, 21, 0, 15.0f, false, 0, 0, 0,
     0, 0, 7, 4.0f},

    // Teleport 6: 0.75's row, `CreateSkill(Teleport, ..., manaConsumption: 30, energyRequirement:
    // 88)` -- thirty mana, six tiles, no damage. MuMain's: aimed at the tile under the pointer and
    // refused on a wall (`TerrainWall[...] == 0`, ClassAttack.cpp:1514), MU's "Skill teleport"
    // (152), the body fading out at a tenth a frame (CreateTeleportBegin, ZzzInterface.cpp:2603),
    // put down, and fading back in; the spark and SOUND_MAGIC at both ends.
    //
    // **Ours** (the user, 2026-09-28, "a blink ... on a short cooldown, D3 style"): three seconds
    // of cooldown before agility's haste, where 0.75 has none; a point past six tiles is pulled
    // back along the line to six, and a wall falls back to the nearest open tile toward him,
    // where MU refuses both; and the fight he was in is dropped, as the Town Portal drops it.
    // **Cast with Energy Ball's one hand** (147), not MU's Skill teleport (152): the user,
    // 2026-09-28, "more simple cast animation for teleport".
    {skill::kTeleport, "Teleport", 30, 6.0f, 1.0f, 0.0f, 60, false, Spread::One, 0, 1.0f,
     "He fades and is put down on the ground he points at, up to six tiles off, leaving the "
     "fight where it stood.",
     147, "spell_magic", true, arms::kNone, 0, Kin::DarkWizard, true, 0, 0, 15.0f, false, 0, 0, 0,
     0, 0, 0, 0.0f, true},

    // Ice 7: 0.75's row, `CreateSkill(Ice, ..., DamageType.Wizardry, 10, 6, manaConsumption: 38,
    // energyRequirement: 120, elementalModifier: Ice)` -- ten damage, thirty-eight mana, and its
    // element is the point: `IsIced` for ten seconds and the walk halved (`chillTicks`,
    // `kChillFactor`). MuMain makes the ice where the body stands at the let-go, no flight
    // (`CreateEffect(MODEL_ICE, to->Position, ...)` and five `MODEL_ICE_SMALL`, SOUND_ICE,
    // ZzzCharacter.cpp:4956), on `SetPlayerMagic`'s two hands.
    //
    // **A cooldown spell** (the user, 2026-09-28), so, as Lightning and Meteorite taught, it is
    // wider and harder than 0.75's: **everything within four tiles of the body he aims at** takes
    // its own ice and its own chill (`splash`, Meteorite's rain with no fall -- `flies` is so fast
    // it lands on the let-go), at **twice the band**, on five seconds of cooldown before agility's
    // haste. The mana is 0.75's. Nine tiles, with the other spells he throws at a body. Four and
    // not two, Meteorite's: two iced one of four Bull Fighters ("only 1 of 4 monsters was iced").
    //
    // **Harder and wider, the wizard's alone** (the user, 2026-10-03: "meteorite, ice, poison,
    // lightning has to be more powerful for DW, only DW can get more tiles"): a third harder, and
    // then "DW should get much more monsters on screen from 1 cast (except if there is wall)":
    // nine tiles round the body it lands on, about the half of the screen MU's camera shows,
    // stopped by walls as before (Realm::seen); Lightning a strike every tick, fourteen, six
    // tiles a leap. The runes that lend other classes a rock, a frost or a bolt keep their own
    // forces and reach (sim/items.h), so this is his. **A tenth down again** the same day (the
    // user: "lets nerf wizard little bit", after the bots had him 186-214 to the others' 175-190
    // at eight hours): Meteorite 2.6 -> 2.3, the other three 2.0 -> 1.8; the reach stays.
    {skill::kIce, "Ice", 38, 9.0f, 1.8f, 0.0f, 0, false, Spread::One, 0, 1.0f,
     "Ice bursts on a body up to nine tiles off and on everything within six tiles of it; what "
     "it strikes walks at half speed for ten seconds.",
     147, "spell_ice", true, arms::kNone, 0, Kin::DarkWizard, true, 10, 148, 1000.0f, false, 0, 0,
     0, 0, 0, 0, 6.0f, false, 200},

    // Poison 1: 0.75's row, `CreateSkill(Poison, ..., DamageType.Wizardry, 12, 6, manaConsumption:
    // 42, energyRequirement: 140, elementalModifier: Poison)` -- twelve damage, forty-two mana, and
    // `IsPoisoned` for twenty seconds, a pulse every three (`PoisonMagicEffect`). MuMain lays
    // `MODEL_POISON` and ten smoke puffs where the body stands at the let-go, SOUND_HEART, and draws
    // the body green (ZzzCharacter.cpp:4993, ZzzObject.cpp:1122), on `SetPlayerMagic`'s hands.
    //
    // **A cooldown spell with an area** (the user, 2026-09-28, "also cooldown spell with aoe"), as
    // Ice is: everything within **four tiles** of the body he aims at takes the blow and the
    // poison, at **twice the band**, on **five seconds** of cooldown before agility's haste.
    //
    // **Each pulse is a quarter of the blow that landed** (`Body::poisonDamage`), where 0.75's is
    // 3% of what health is left: at 3% a Bull Fighter lost three a pulse, which is no poison at
    // all. Six pulses, so the poison is half again the blow. It never kills on its own -- 0.75's
    // shape, which only ever takes a share of what is left -- and leaves one health. Ours.
    //
    // **Harder and wider, the wizard's alone** (the user, 2026-10-03: "meteorite, ice, poison,
    // lightning has to be more powerful for DW, only DW can get more tiles"): a third harder, and
    // then "DW should get much more monsters on screen from 1 cast (except if there is wall)":
    // nine tiles round the body it lands on, about the half of the screen MU's camera shows,
    // stopped by walls as before (Realm::seen); Lightning a strike every tick, fourteen, six
    // tiles a leap. The runes that lend other classes a rock, a frost or a bolt keep their own
    // forces and reach (sim/items.h), so this is his. **A tenth down again** the same day (the
    // user: "lets nerf wizard little bit", after the bots had him 186-214 to the others' 175-190
    // at eight hours): Meteorite 2.6 -> 2.3, the other three 2.0 -> 1.8; the reach stays.
    {skill::kPoison, "Poison", 42, 9.0f, 1.8f, 0.0f, 0, false, Spread::One, 0, 1.0f,
     "A cloud of poison bursts on a body up to nine tiles off and on everything within six tiles "
     "of it, and goes on hurting them for twenty seconds.",
     147, "spell_heart", true, arms::kNone, 0, Kin::DarkWizard, true, 12, 148, 1000.0f, false, 0,
     0, 0, 0, 0, 0, 6.0f, false, 0, 400},

    // ---- the Fairy Elf's, appended after the wizard's (sprint 15) ------------------------------
    //
    // Skillshot 24: 0.75's Triple Shot row -- five mana, six tiles, three arrows
    // (`SkillsInitializer.cs:64`, MU2's `Most: 3`) -- taught by the Orb of Skillshot and thrown
    // off any bow or crossbow, the user's of 2026-09-28, where 0.75 grants it only off a bow with
    // the Skill option. No cooldown: on the quick slot it is her auto-attack, as Energy Ball is
    // the wizard's, paced by the bow's own clip (50; the drawing plays 51 with a crossbow). Each
    // arrow is an archery blow at no multiplier -- ClassFairyElf.cs sets no SkillMultiplier --
    // and the fan is its worth: three bodies struck is three blows. Its orb, OrbSkillshot.json,
    // asks nothing.
    //
    // **Nine tiles and not six** (the user, 2026-09-28: "lets increase range in our game for
    // multishot"), ours, as the wizard's Energy Ball and Fire Ball were raised to nine. Her plain
    // shot keeps MuMain's six.
    {.number = skill::kSkillshot, .name = "Skillshot", .mana = 5, .reach = 9.0f, .force = 1.0f,
     .spread = Spread::Fan,
     .tells = "Three arrows loosed in a fan at a body up to nine tiles off, each flying on "
              "through everything in its way. One arrow is spent for every body struck.",
     .clip = 50, .sound = "player_bow", .built = true, .families = arms::kMissiles,
     .needLevel = 0, .kin = Kin::FairyElf, .flies = 17.5f, .arrows = 3},
    // Heal 26: twenty mana, `5 + energy / 5` health at once (HealEffectInitializer). MU casts it
    // on a player; there is no party here, so it is hers -- and her summon's, when step 5 builds
    // one. `PLAYER_SKILL_ELF1` (151) and `SOUND_SKILL_DEFENSE`, as MuMain's ReceiveMagic opens
    // every elf buff (WSclient.cpp:4153-4184). Ours: a three-second cooldown, because 0.75's
    // twenty mana is a heal every clip. Its orb asks 52 energy and no level (Gem02.json).
    {.number = skill::kHeal, .name = "Heal", .mana = 20, .coolTicks = 60,
     .tells = "Health put back at once. The keener the elf, the more.",
     .clip = 151, .sound = "player_skill_defense", .built = true, .families = arms::kNone,
     .needLevel = 0, .kin = Kin::FairyElf, .anyHand = true, .mends = true},
    // Greater Defense 27: **her guard, Defense's own row in the elf's hand** -- thirty mana,
    // five minutes, twelve seconds of cooldown floored at its length and two, learned at six --
    // the user's of 2026-09-28, "available early, the same as the Orb of Defense and the Scroll
    // of Soul Barrier, with the same stats". What it takes off a blow is `wardShare`. No shield:
    // the bow leaves her no hand for one. The mana is 0.75's Greater Defense's own.
    {.number = skill::kGreaterDefense, .name = "Greater Defense", .mana = 30, .coolTicks = 240,
     .boonTicks = 6000, .damageTaken = 0.50f,
     .tells = "A ward raised for five minutes. The quicker and keener the elf, the more of every "
              "blow it takes.",
     .clip = 151, .sound = "player_skill_defense", .built = true, .families = arms::kNone,
     .needLevel = 6, .kin = Kin::FairyElf, .anyHand = true},
    // Greater Damage 28: 0.75's row -- forty mana, `3 + energy / 7` on every blow. Ours: twelve
    // seconds of cooldown floored at its length and two, as the guards are, and **five minutes
    // where 0.75 gives sixty seconds** (GreaterDamageEffectInitializer) -- the user, 2026-10-02:
    // "it has to be same durations as defense", Greater Defense's 6000 ticks. Its orb asks 92
    // energy and no level (Gem04.json).
    {.number = skill::kGreaterDamage, .name = "Greater Damage", .mana = 40, .coolTicks = 240,
     .tells = "Every blow harder by a share of her energy, for five minutes.",
     .clip = 151, .sound = "player_skill_defense", .built = true, .families = arms::kNone,
     .needLevel = 0, .kin = Kin::FairyElf, .anyHand = true, .mightTicks = 6000},

    // Her six summons, 30 to 35, at 0.75's mana (`SkillsInitializer.cs:68-73`): one at a time,
    // raised beside her, and a second cast dismisses the one standing (TargetedSkillDefaultPlugin
    // .cs:121-125). `PLAYER_SKILL_ELF1` and `SOUND_SKILL_DEFENSE`, as ReceiveMagic plays them
    // (WSclient.cpp:4153-4184). All six are `built`: the Goblin and the Stone Golem are
    // Noria's, the Assassin and the Elite Yeti Devias's and the Dark Knight the Dungeon's, every
    // world's table carries every breed, and a summon borrows its figure from any of them. Bali
    // lives on no map, and is cooked with Noria for the rest to borrow (Bali01's summoned_in).
    //
    // **A minute, shared by all six** (the user, 2026-10-02: "give elf summon a 1 min cooldown
    // after summon. its global for all summons"), ours, where 0.75 has none: a cast cools every
    // summon key (Realm::throwSkill), agility does not haste it (`cooldownTicks`), and a key
    // that is cooling still dismisses the one standing.
    {.number = skill::kSummonGoblin, .name = "Summon Goblin", .mana = 40,
     .coolTicks = kSummonCool,
     .tells = "A goblin at her side, that fights what she fights and draws it off her. The "
              "keener the elf, the tougher it is.",
     .clip = 151, .sound = "player_skill_defense", .built = true, .families = arms::kNone,
     .kin = Kin::FairyElf, .anyHand = true, .summons = 26},
    {.number = skill::kSummonGolem, .name = "Summon Stone Golem", .mana = 70,
     .coolTicks = kSummonCool,
     .tells = "A stone golem at her side, slow and hard to break, that holds what it fights. "
              "The keener the elf, the tougher it is.",
     .clip = 151, .sound = "player_skill_defense", .built = true, .families = arms::kNone,
     .kin = Kin::FairyElf, .anyHand = true, .summons = 32},
    {.number = skill::kSummonAssassin, .name = "Summon Assassin", .mana = 110,
     .coolTicks = kSummonCool,
     .tells = "An assassin at her side.", .clip = 151, .sound = "player_skill_defense",
     .built = true, .families = arms::kNone, .kin = Kin::FairyElf, .anyHand = true,
     .summons = 21},
    {.number = skill::kSummonYeti, .name = "Summon Elite Yeti", .mana = 160,
     .coolTicks = kSummonCool,
     .tells = "An elite yeti at her side.", .clip = 151, .sound = "player_skill_defense",
     .built = true, .families = arms::kNone, .kin = Kin::FairyElf, .anyHand = true,
     .summons = 20},
    {.number = skill::kSummonKnight, .name = "Summon Dark Knight", .mana = 200,
     .coolTicks = kSummonCool,
     .tells = "A dark knight at her side.", .clip = 151, .sound = "player_skill_defense",
     .built = true, .families = arms::kNone, .kin = Kin::FairyElf, .anyHand = true,
     .summons = 10},
    {.number = skill::kSummonBali, .name = "Summon Bali", .mana = 250,
     .coolTicks = kSummonCool,
     .tells = "Bali at her side.", .clip = 151, .sound = "player_skill_defense",
     .built = true, .families = arms::kNone, .kin = Kin::FairyElf, .anyHand = true,
     .summons = 150},

    // ---- Flame 5, the wizard's, on the end so no save's learned bit moves ----------------------
    //
    // 0.75's row: twenty-five damage, fifty mana, a hundred and sixty energy, fire
    // (`Version075/SkillsInitializer.cs:45`), taught by the Scroll of Flame (Book05). What it is
    // is MU's: a fire lit on a tile -- `CreateEffect(BITMAP_FLAME, ..., 0)` at the let-go
    // (ZzzCharacter.cpp:4480) -- that asks for its damage twice over its forty frames, every
    // twenty, on everything within 150 units (`Move_BITMAP_FLAME`, MoveHandlers.cpp:1817). So
    // two strikes, `kBurnEvery` apart, a tile and a half round the fire -- MU's 150 units, where
    // OpenMU's `targetAreaDiameter: 2` would be one -- and whoever stands in it at each.
    //
    // **No cooldown**, a standard spell as Energy Ball and Fire Ball are (the user, 2026-09-30),
    // its clip its pace and each strike at the band's own force. Thrown at a body, as Meteorite, Ice and Poison are, from the right button or a key; the
    // fire is lit on that body's tile, not under the pointer. Nine tiles, with the other spells
    // he throws at a body, where 0.75 gives six. Its clip is `SetPlayerMagic`'s two hands, 147
    // and 148 (ClassAttack.cpp:1383), and sFlame is the fire starting, not a blow landing.
    // The numbers past 0.75's are ours.
    {.number = skill::kFlame, .name = "Flame", .mana = 50, .reach = 9.0f, .force = 1.0f,
     .spread = Spread::One,
     .tells = "Sets the ground under a body up to nine tiles off alight, and the fire strikes "
              "everything standing in it twice.",
     .clip = 147, .sound = "spell_flame", .built = true, .families = arms::kNone,
     .needLevel = 0, .kin = Kin::DarkWizard, .wizardry = true, .damage = 25, .clipOther = 148,
     .flies = 1000.0f, .burns = 2, .burnTiles = 1.5f},

    // ---- Evil Spirit 9, the wizard's, after Flame for the same reason ---------------------------
    //
    // 0.75's row: forty-five damage, ninety mana, two hundred and twenty energy, no element
    // (`Version075/SkillsInitializer.cs:51-52`), taught by the Scroll of Evil Spirit (Book09).
    // What it is is MU's: four spirits (eight joints, a wide and a thin each way) let go round
    // the caster with SOUND_EVIL, wandering round him for 49 frames (ZzzCharacter.cpp:4585-4603,
    // ZzzEffectJoint.cpp:3737-3765). What they strike is WebZen's SkillEvil (ObjUseSkill.cpp:
    // 2606-2645): each monster within ten tiles, two in three of them, once, within two seconds
    // -- the realm's `letSpiritsGo`. Its clip is SetPlayerMagic's two hands, as Flame's
    // (ClassAttack.cpp:1357-1362).
    //
    // **No cooldown**, a standard spell as Flame is (the user, 2026-10-01: "so he can use like
    // normal spell without cooldown"). Aimed at a body nine tiles off at most, with his other
    // spells; the spirits go round him, not it. The shield's Evil Spirit rune lets the same go
    // off a miss (sim/items.h).
    //
    // **1.8 the band** (the user, 2026-10-05: "it has to be stronger that meteor"): at 0.75's
    // one a spirit struck ~180 on a 689-energy wizard to a Meteorite rock's ~240 (its 21 at 1.6);
    // at 1.8 a spirit is ~320. The shield's rune lets its spirits go at its own 1.0.
    {.number = skill::kEvilSpirit, .name = "Evil Spirit", .mana = 90, .reach = 9.0f,
     .force = 1.8f, .spread = Spread::One,
     .tells = "Lets evil spirits loose around him, which strike most of the monsters within "
              "ten tiles.",
     .clip = 147, .sound = "spell_evil", .built = true, .families = arms::kNone,
     .needLevel = 0, .kin = Kin::DarkWizard, .wizardry = true, .damage = 45, .clipOther = 148,
     .flies = 1000.0f},

    // ---- Hellfire 10, the wizard's, after Evil Spirit for the same reason -----------------------
    //
    // 0.75's row: a hundred and twenty damage, a hundred and sixty mana, two hundred and sixty
    // energy, fire, an area that strikes by itself (`Version075/SkillsInitializer.cs:53`), taught
    // by the Scroll of Hellfire (Book10). What it strikes is WebZen's SkillHellFire (1.00.93
    // ObjUseSkill.cpp): every monster within four tiles of him (`gObjCalDistance < 4`), once --
    // `Spread::Ring` at `reach` 4, every body `gather` finds round him, each taking his wizardry
    // band. WebZen holds each blow 200 ms (gObjAddAttackProcMsgSendDelay); here it lands on the
    // let-go with the circle. Its clip is MU's own PLAYER_SKILL_HELL (0.5: ClassAttack.cpp:
    // 1202-1210, ZzzCharacter.cpp:945), the leap and the landing with a hand on the ground --
    // action 154 in player.muc, one before the enum's 155 (source/players/rig/actions.json); the
    // landing is half the clip, where the realm lets it go. Its circle and sHellFire are
    // game/fx/hellfire.h.
    //
    // **No cooldown**, a standard spell as Evil Spirit is: a press is one leap, a held key goes
    // on. (A two-second wait was tried on 2026-10-02 for "its like he is jumping twice" and
    // taken out the same day: the second leap was the arena's own hand, which fights with what
    // --arena-learn taught.) The force past 0.75's one is ours.
    //
    // **Let go on the landing**, not at half the clip (the user, 2026-10-02: "animation has to be
    // in sync with spell. when char lands than there is hellfire"): the clip's hips are over three
    // metres up from key 1 to key 5 and down at key 6 of 18 (source/players/rig/player.rig.json,
    // action 154's Bip01 track), so the circle and every blow are a third of the way in.
    //
    // **The wizard's ladder** (the user, 2026-10-05: "hell fire is stroonger than evil spirit,
    // inferno has to be stronger than [hellfire], aqua has to be stronger than inferno"), a blow
    // on a 689-energy wizard: Meteorite ~240 < Twister ~277 < Evil Spirit ~320 < Hellfire 1.3
    // ~356 < Inferno 1.6 ~398 < Aqua Beam 2.0 ~448.
    {.number = skill::kHellfire, .name = "Hellfire", .mana = 160, .reach = 4.0f, .force = 1.3f,
     .spread = Spread::Ring,
     .tells = "Sets the ground round him on fire, striking every monster within four tiles.",
     .clip = 154, .sound = "spell_hellfire", .built = true, .families = arms::kNone,
     .needLevel = 0, .kin = Kin::DarkWizard, .wizardry = true, .damage = 120,
     .flies = 1000.0f, .release = kHellfireLanding},

    // ---- Twister 8, the wizard's, after Hellfire for the same reason ----------------------------
    //
    // 0.75's row: thirty-five damage, sixty mana, a hundred and eighty energy, distance six, wind
    // (`Version075/SkillsInitializer.cs:49-50`), taught by the Scroll of Twister (Book08). What it
    // strikes is MU's client: WebZen's 1.00.93 judges nothing for AT_SKILL_STORM -- it is on
    // CGBeattackRecv's list of skills whose victims the client sends (protocol.cpp:16355), held
    // only to five packets a cast and eight seconds (UseMagicCount, UseMagicTime) -- so the rule is
    // MuMain's: `CreateEffect(MODEL_STORM, o->Position, o->Angle, ...)` at the let-go
    // (ZzzCharacter.cpp:4495-4497), at HIS feet and turned to what he aimed at (ClassAttack.cpp:
    // 1354), walked ten units a frame along that facing (`Direction = (0, -10, 0)`, ZzzEffect.cpp:
    // 2000-2010) -- an eighth of a tile a tick -- and asking `AttackCharacterRange(..., 150.f)`
    // round itself on three of its frames (`kStormFirst`, `kStormEvery`). So three strikes, 1.4,
    // 2.9 and 4.4 tiles out in MU (1.5, 3 and 4.5 here: it walks on the let-go's tick too), on
    // everything within a tile and a half of the storm then; a body
    // that walks with it may be struck by all three, as WebZen lets it. OpenMU's cone and its
    // two hits a target at 0.7 are what 0.75's server does instead, and are not used.
    //
    // **No cooldown**, a standard spell as Flame is. Its clip is `SetPlayerMagic`'s two hands, 147
    // and 148 (ClassAttack.cpp:1361; checked on the bench 2026-10-02: both arms up and thrust
    // forward, one arm up and out), let go at half the clip as MU lets it go on AttackTime's
    // limit. Six tiles, 0.75's own, so a body at the edge is just past the last strike, as in MU.
    // Its storm and sTornado are game/fx/storm.h.
    //
    // **1.65 the band** (the user, 2026-10-05: "twister has to be also stronger than meteorit bit
    // weaker than evil spirits"): ~277 a strike on a 689-energy wizard, between a Meteorite
    // rock's ~240 and a spirit's ~320.
    {.number = skill::kTwister, .name = "Twister", .mana = 60, .reach = 6.0f, .force = 1.65f,
     .spread = Spread::One,
     .tells = "Sends a whirlwind walking out ahead of him, striking everything it passes three "
              "times.",
     .clip = 147, .sound = "spell_storm", .built = true, .families = arms::kNone,
     .needLevel = 0, .kin = Kin::DarkWizard, .wizardry = true, .damage = 35, .clipOther = 148,
     .flies = 1000.0f, .burns = 3, .burnTiles = 1.5f, .walks = 0.125f},

    // ---- Inferno 14, the wizard's, after Twister for the same reason ----------------------------
    //
    // **Not 0.75's**: 0.75 has no Inferno. Its row is 0.95d's -- a hundred damage, two hundred
    // mana, fire, an area that strikes by itself (`Version095d/SkillsInitializer.cs:59`), and
    // WebZen 1.00.93's skill 14 says the same (skill(Kor).txt) -- taught by the Scroll of Inferno
    // (Book14, group 15 number 13). What it strikes is WebZen's: AT_SKILL_INFERNO is handed to
    // the very SkillHellFire that Hellfire is (ObjUseSkill.cpp:880-883), so every monster within
    // four tiles of him, once, his wizardry band -- Hellfire's `Spread::Ring` at `reach` 4.
    //
    // Its clip is MU's own PLAYER_SKILL_INFERNO at 0.6 (ClassAttack.cpp:1212-1221, ZzzCharacter.
    // cpp:944): player.muc's 153, one before the enum's 154, as Hellfire's is (source/players/
    // rig/actions.json; on the bench 2026-10-02 the low crouch with both arms driven out at the
    // ground). Let go at half the clip, the let-go MU makes the ring of blasts and MODEL_SKILL_
    // INFERNO in (ZzzCharacter.cpp:4574-4583). No cooldown, as Hellfire. Its ring and its
    // eExplosion are game/fx/inferno.h. 1.6 the band, above Hellfire (its ladder, above).
    {.number = skill::kInferno, .name = "Inferno", .mana = 200, .reach = 4.0f, .force = 1.6f,
     .spread = Spread::Ring,
     .tells = "A ring of fire bursts round him, striking every monster within four tiles.",
     .clip = 153, .sound = "explosion", .built = true, .families = arms::kNone,
     .needLevel = 0, .kin = Kin::DarkWizard, .wizardry = true, .damage = 100,
     .flies = 1000.0f},

    // ---- Aqua Beam 12, the wizard's, after Inferno for the same reason --------------------------
    //
    // 0.75's row: eighty damage, distance six, a hundred and forty mana, three hundred and
    // forty-five energy, water, an area that strikes by itself, OpenMU's frustum a tile and a
    // half wide and eight long (`Version075/SkillsInitializer.cs:55-56`), taught by the Scroll of
    // Aqua Beam (Book12). What it strikes is MU's client, which WebZen 1.00.93 takes as sent
    // (AT_SKILL_FLASH is on CGBeattackRecv's list, protocol.cpp:16359): the beam's four circles
    // ahead of him, `Spread::Beam` (skills.h), each body once. Water is 0.75's element, and no
    // rune here carries it, so it has none (realm_tuning.h `skillElement`).
    //
    // Its clip is MU's own PLAYER_SKILL_FLASH at 0.4 (ClassAttack.cpp:1365-1376, ZzzCharacter.cpp:
    // 943): player.muc's 152, one before the enum's 153, as Inferno's and Hellfire's are
    // (source/players/rig/actions.json). MU makes the beam with SOUND_FLASH at the let-go
    // (ZzzCharacter.cpp:4551-4554). **Let go as the arm is thrown**, key 5 of 12, not at half the
    // clip (the user, 2026-10-02: "aqua beam cast has to be perfectly synced with cast
    // animation"): the right hand is up over his head to key 5 and out in front of him at key 6,
    // 187 units on in one key (action 152 in source/players/rig/player.rig.json), and let go at
    // half the beam came two frames after the arm in the arena. No cooldown, a standard spell.
    // Its beam and sAquaFlash are game/fx/aqua.h. 2.0 the band, the top of Hellfire's ladder.
    {.number = skill::kAquaBeam, .name = "Aqua Beam", .mana = 140, .reach = 6.0f, .force = 2.0f,
     .spread = Spread::Beam,
     .tells = "A beam of water thrown straight out ahead of him, striking everything along it.",
     .clip = 152, .sound = "spell_flash", .built = true, .families = arms::kNone,
     .needLevel = 0, .kin = Kin::DarkWizard, .wizardry = true, .damage = 80,
     .flies = 1000.0f, .release = kAquaThrow},

    // ---- Penetration 52, the elf's, after Aqua Beam for the same reason -------------------------
    //
    // OpenMU's row (VersionSeasonSix/SkillsInitializer.cs:171-172): seventy damage, distance six,
    // seven mana, level 130, wind, an area that strikes by itself down a frustum 1.1 to 1.2 tiles
    // wide and eight long -- one arrow that flies on through everything in its line. MuMain's:
    // thrown off any bow (`GetEquipedBowType`, ClassAttack.cpp:128-152), one arrow of the bow's own
    // model with MODEL_PIERCING's ribbons wound round it and `Kind = 1`, which passes through what
    // it strikes (ZzzEffect.cpp:1755-1760). Taught by the Orb of Penetration (12, 17), level 130.
    //
    // Here it is Skillshot's fan with one lane, thrown at a body up to eight tiles off and flying
    // sixteen through the screen (`kPierceTiles`, skills.h), each body in it struck once and
    // costing an arrow. **Ours:** half again of her shot where MU adds its seventy to the arrow
    // (about that, on a level-130 elf's band); no cooldown, as MU has none and Skillshot has none;
    // and a Piercing Volley in her bow looses three lanes (`lanesOf`, sim/items.h). Its clip is her
    // bow's 50 or 51, as Skillshot's is; SOUND_PIERCING is MU's, at the draw (ZzzCharacter.cpp:
    // 2774-2779). Wind (realm_fight.cpp `skillElement`), so Tempest raises it.
    {.number = skill::kPenetration, .name = "Penetration", .mana = 7, .reach = 8.0f,
     .force = 1.5f, .spread = Spread::Fan,
     .tells = "An arrow loosed at a body up to eight tiles off, flying on through everything in "
              "its line. One arrow is spent for every body struck.",
     .clip = 50, .sound = "player_piercing", .built = true, .families = arms::kMissiles,
     .needLevel = 130, .kin = Kin::FairyElf, .flies = 17.5f, .arrows = 1},

    // ---- Fire Breath 49, the Dinorant's, after Penetration so no save's learned bit moves -------
    //
    // OpenMU's row (Version095d/SkillsInitializer.cs:80): thirty damage, three tiles, nine mana,
    // level 110, the knight's, physical, one body. WebZen calls it Raid Shoot and its blow
    // `(200 + Energy/10) / 100` of his swing (ObjAttack.cpp:1392-1407) -- which is this table's own
    // 2 + energy/1000, so the force is MU's and the strength term the knight's usual. MuMain plays
    // PLAYER_SKILL_RIDER (68) on him and the dragon's 6, and makes the breath as the blow is let go
    // (game/fx/firebreath.h). Carried by the horn, not learned (zzzitem.cpp:1018-1022): known while
    // it is worn, thrown while it is ridden (`mounted`), with any weapon or none.
    //
    // **Ours:** the four-second cooldown, Falling Slash's -- MU has none, and with none a nine-mana
    // blow at twice his swing would be a primary stronger than Twisting Slash. Its breath crosses
    // the ground at MU's thirty units a frame, 7.5 tiles a second, so the blow lands as it arrives.
    {.number = skill::kFireBreath, .name = "Fire Breath", .mana = 9, .reach = 3.0f,
     .force = 2.0f, .forcePerStrength = 1.0f / 1000.0f, .coolTicks = 80,
     .tells = "The dragon under him breathes at one body up to three tiles off. Only while he "
              "rides the Dinorant.",
     .clip = 68, .sound = "player_skill_sword3", .built = true, .families = arms::kNone,
     .flies = 7.5f, .anyHand = true, .mounted = true},
};

// The energy term is 0.75's own and is kept rather than replaced: a knight who spends on energy
// still gets his tenth of a percent a point, as he does in the original.
constexpr float kForcePerEnergy = 0.001f;

// Two seconds over a boon's duration, so half damage always has a gap in it.
constexpr int32_t kBoonGapTicks = 40;

}  // namespace

// ---- the families (docs/skills-dk.md §3.1b) -------------------------------------------------

uint32_t familyOf(const content::Arm* weapon) {
    if (!weapon) return arms::kNone;
    if (weapon->isShield()) return arms::kShield;
    // A bow, a crossbow and a staff throw none of these: the five 0.75 clips are
    // `PLAYER_ATTACK_SKILL_SWORD1..5` and the streak MU lays on them is a blade's. The missile
    // test comes first because a quiver is in group 4 with the bows.
    if (weapon->missile()) return weapon->bow() ? arms::kBow : arms::kCrossbow;
    const bool both = weapon->twoHanded();
    switch (weapon->group) {
        case 0: return both ? arms::kSword2 : arms::kSword1;
        case 1: return both ? arms::kAxe2 : arms::kAxe1;
        case 2: return both ? arms::kMace2 : arms::kMace1;
        // Every polearm in MU is two-handed, so there is one bit and no branch. A row that says
        // otherwise is a transcription error and would land here as a spear anyway, which is the
        // safe way round: it is what the weapon IS.
        case 3: return arms::kSpear;
        default: return arms::kNone;  // 4 the bows, 5 the staves, and anything uncooked
    }
}

const char* familyName(uint32_t family) {
    switch (family) {
        case arms::kSword1: return "a one-handed sword";
        case arms::kSword2: return "a two-handed sword";
        case arms::kAxe1: return "a one-handed axe";
        case arms::kAxe2: return "a two-handed axe";
        case arms::kMace1: return "a mace";
        case arms::kMace2: return "a two-handed mace";
        case arms::kSpear: return "a spear";
        case arms::kShield: return "a shield";
        case arms::kBow: return "a bow";
        case arms::kCrossbow: return "a crossbow";
        default: return "";
    }
}

int familiesNamed(uint32_t families, const char** out, int room) {
    int found = 0;
    const auto add = [&](const char* word) { if (found < room) out[found++] = word; };
    if (families == arms::kShield) {
        add("A shield");
        return found;
    }
    if ((families & arms::kEvery) == arms::kEvery) {
        add("Any weapon");
        return found;
    }
    // A family whose two hands are both in the mask is said once and without the hand, which is
    // what makes one word "Axes" out of two bits. Each word is a line of its own on the card, so
    // this is a list and not a sentence -- no "and", no commas, nothing to wrap.
    const uint32_t swords = families & arms::kSwords;
    if (swords == arms::kSwords) add("Swords");
    else if (swords == arms::kSword1) add("1-hand swords");
    else if (swords == arms::kSword2) add("2-hand swords");
    const uint32_t axes = families & arms::kAxes;
    if (axes == arms::kAxes) add("Axes");
    else if (axes == arms::kAxe1) add("1-hand axes");
    else if (axes == arms::kAxe2) add("2-hand axes");
    const uint32_t maces = families & arms::kMaces;
    if (maces == arms::kMace2 && (families & arms::kMace1) == 0) add("2-hand maces");
    else if (maces != 0) add("Maces");  // no two-handed mace is cooked; one word for the pair
    if ((families & arms::kSpear) != 0) add("Spears");
    if ((families & arms::kMissiles) == arms::kMissiles) add("Bows and crossbows");
    else if ((families & arms::kBow) != 0) add("Bows");
    else if ((families & arms::kCrossbow) != 0) add("Crossbows");
    return found;
}

std::string familiesListed(uint32_t families) {
    const char* words[8] = {};
    const int found = familiesNamed(families, words, 8);
    std::string listed;
    for (int i = 0; i < found; ++i) listed += (i > 0 ? ", " : "") + std::string(words[i]);
    return listed;
}

int skillCount() { return kSkills; }

const SkillRow& skillAt(int index) {
    return kRows[size_t(std::clamp(index, 0, kSkills - 1))];
}

const SkillRow* skillNumbered(int32_t number) {
    for (const SkillRow& row : kRows) {
        if (row.number == number) return &row;
    }
    return nullptr;
}

int skillIndexOf(int32_t number) {
    for (int i = 0; i < kSkills; ++i) {
        if (kRows[i].number == number) return i;
    }
    return -1;
}

float force(const SkillRow& row, const HeroPoints& points) {
    // A wizard's `SkillMultiplier` is a flat one (ClassDarkWizard.cs:112): his spells take their
    // force from the wizardry band instead, which is where energy already went.
    // A spell's own `force` column only, where 0.75 has none: one on every spell but Lightning,
    // whose two is what a ten-second cooldown buys (the user, 2026-09-28: "lightning has to be
    // stronger because it's a cooldown spell"). Energy and the staff are already in the band.
    if (row.wizardry) return row.force;
    return row.force + float(points.strength) * row.forcePerStrength +
           float(points.energy) * kForcePerEnergy;
}

int32_t castTicks(const content::Tables& tables, Kin kin, int agility, const content::Arm* right,
                  const content::Arm* left, const SkillRow& row, int extra) {
    const content::PlayerAction* clip = tables.action(row.clip);
    if (!clip || clip->keys <= 0) return 0;
    // The attack speed's own term, and it is the ATTACK one rather than the magic one: 60 to 64
    // are `PLAYER_ATTACK_SKILL_SWORD*` and fall on `SetAttackSpeed`'s attack branch, where a
    // wizard's four cast clips read MagicSpeed instead. MU2 got this wrong the other way round
    // once and a staff threw spells a fifth too fast.
    //
    // Except on a self-cast, which the drawing plays at the clip's own pace (`swingPace` 1 in
    // game/play.cpp): there is no blow in it to hurry. Timed with the bonus, the lock that
    // holds him still ran out while the guard was still being raised, and a click walked him
    // out of the middle of it.
    // A spell reads MagicSpeed, which is the wizard's agility over ten (ClassDarkWizard.cs:59)
    // and nothing from the weapon: `PLAYER_SKILL_HAND1..` play at `0.29 + MagicSpeed * 0.004`
    // (ZzzCharacter.cpp:939, the RGZ_FIX arm under 509).
    const float bonus = row.onSelf()     ? 0.0f
                        : row.wizardry ? (magicSpeedStat(kin, agility) + float(extra)) * 0.004f
                                       : (attackSpeedStat(kin, agility, right, left) + float(extra)) * 0.004f;
    const bool quick = row.number == skill::kMeteorite || row.number == skill::kIce ||
                       row.number == skill::kPoison;
    const float rate = (clip->speed + bonus) * 25.0f * (quick ? kSpellQuicken : 1.0f);
    if (rate <= 0.0f) return 0;
    return swingTicks(int(float(clip->keys) / rate * 1000.0f));
}

int32_t authoredCastTicks(const content::Tables& tables, const SkillRow& row) {
    const content::PlayerAction* clip = tables.action(row.clip);
    if (!clip || clip->keys <= 0 || clip->speed <= 0.0f) return 0;
    return swingTicks(int(float(clip->keys) / (clip->speed * 25.0f) * 1000.0f));
}

float magicSpeedStat(Kin kin, int agility) {
    // Only the wizard's class file relates agility to MagicSpeed at a rate this game can reach;
    // nobody else casts a spell here.
    return kin == Kin::DarkWizard ? float(agility) / 10.0f : 0.0f;
}

int32_t floorTicksFor(const SkillRow& row, int32_t clipTicks) {
    if (row.boonTicks > 0) return row.boonTicks + kBoonGapTicks;
    if (row.mightTicks > 0) return row.mightTicks + kBoonGapTicks;
    // A channel's own length, and two seconds: it is never ready again before it has ended.
    if (row.channelTicks > 0) return row.channelTicks + kBoonGapTicks;
    return std::max<int32_t>(1, clipTicks);
}

float guardPoints(const HeroPoints& points, int shieldDefense) {
    return 5.0f * float(std::max(0, shieldDefense)) + 1.1f * float(points.strength) +
           0.5f * float(points.agility);
}

float guardShare(const HeroPoints& points, int shieldDefense) {
    const float p = std::max(0.0f, guardPoints(points, shieldDefense));
    return kGuardCap * p / (p + 150.0f);
}

float barrierPoints(const HeroPoints& points, int shieldDefense) {
    return 5.0f * float(std::max(0, shieldDefense)) + 1.1f * float(points.energy) +
           0.5f * float(points.agility);
}

float barrierShare(const HeroPoints& points, int shieldDefense) {
    const float p = std::max(0.0f, barrierPoints(points, shieldDefense));
    return kGuardCap * p / (p + 150.0f);
}

float wardPoints(const HeroPoints& points) {
    return kWardShieldPoints + 1.1f * float(points.agility) + 0.5f * float(points.energy);
}

float wardShare(const HeroPoints& points) {
    const float p = std::max(0.0f, wardPoints(points));
    return kGuardCap * p / (p + 150.0f);
}

float summonHealthRate(const HeroPoints& points) {
    return 1.0f + float(std::max(0, points.energy)) * kSummonHealthPerEnergy +
           float(std::max(0, points.vitality)) * kSummonHealthPerVitality;
}

float summonForceRate(const HeroPoints& points) {
    return 1.0f + float(std::max(0, points.energy)) * kSummonForcePerEnergy +
           float(std::max(0, points.agility)) * kSummonForcePerAgility;
}

int summonLevel(int breedLevel, int heroLevel, int32_t skill) {
    const int tier = std::clamp(int(skill - skill::kSummonGoblin), 0,
                                int(skill::kSummonBali - skill::kSummonGoblin));
    const float share = kSummonLevelShare + kSummonLevelShareStep * float(tier);
    return breedLevel + int(float(std::max(0, heroLevel)) * share);
}

namespace {

// 0.75's monsters by level, read off every cooked breed (Lorencia to the Lost Tower): health,
// the bottom of the damage band, defence, attack rate, defence rate. Between two rows it is a
// straight line, and past the last it goes on at the last row's slope.
struct Rung {
    float level, column[5];
};
constexpr Rung kLadder[] = {
    {2, {30, 4, 1, 8, 1}},           // Spider
    {10, {165, 26, 10, 44, 10}},     // Beetle Monster
    {20, {600, 75, 25, 100, 25}},    // Worm
    {30, {900, 105, 37, 150, 37}},   // Yeti
    {40, {1600, 130, 60, 200, 47}},  // Hell Spider
    {50, {3500, 155, 85, 250, 73}},  // Poison Shadow
    {60, {5000, 180, 115, 300, 88}}, // Devil
    {64, {6000, 200, 130, 320, 94}}, // Death Gorgon
};

float rung(int column, float level) {
    constexpr size_t kLast = sizeof(kLadder) / sizeof(kLadder[0]) - 1;
    size_t at = 0;
    while (at + 1 < kLast && level > kLadder[at + 1].level) ++at;
    const Rung& low = kLadder[at];
    const Rung& high = kLadder[at + 1];
    const float t = (std::max(level, low.level) - low.level) / (high.level - low.level);
    return low.column[column] + (high.column[column] - low.column[column]) * t;
}

}  // namespace

float summonClimb(Ladder column, int breedLevel, int level) {
    if (level <= breedLevel) return 1.0f;
    const int at = int(column);
    return rung(at, float(level)) / std::max(1.0f, rung(at, float(breedLevel)));
}

int healOf(const HeroPoints& points) { return 5 + std::max(0, points.energy) / 5; }

int mightOf(const HeroPoints& points) { return 3 + std::max(0, points.energy) / 7; }

float boonShare(const SkillRow& row, const HeroPoints& points, int shieldDefense) {
    if (row.number == skill::kGreaterDefense) return wardShare(points);
    return row.number == skill::kSoulBarrier ? barrierShare(points, shieldDefense)
                                             : guardShare(points, shieldDefense);
}

std::string absorbed(float share) {
    return std::to_string(int(std::lround(double(share) * 100.0))) + "%";
}

std::string spoken(float seconds) {
    const int whole = int(std::ceil(std::max(0.0f, seconds)));
    char out[24];
    if (whole >= 60) {
        std::snprintf(out, sizeof out, "%d:%02d", whole / 60, whole % 60);
    } else {
        std::snprintf(out, sizeof out, "%d s", whole);
    }
    return out;
}

int32_t cooldownTicks(const SkillRow& row, int agility, int32_t floorTicks) {
    if (row.coolTicks <= 0) return std::max<int32_t>(0, floorTicks);
    // A summon's minute is a minute: agility hastes the elf's hands, not the summoning.
    if (row.summons > 0) return std::max(floorTicks, row.coolTicks);
    const float haste = float(std::max(0, agility)) / kAgilityPerDoubling;
    const int32_t hasted = int32_t(std::lround(float(row.coolTicks) / (1.0f + haste)));
    return std::max(std::max<int32_t>(1, floorTicks), hasted);
}

}  // namespace mu::sim
