# WebZen audit (2026-09-30)

Our sim checked against WebZen's own GameServer 1.00.93 (0.97d to Season 4.6,
github ptr0x-real/Mu-GS-Webzen-MC-10093), base 0.97d behaviour unless a flag is named.
Trust the C++; the repo's `_Server Files/Data` is a private-server repack (Elion 2012,
300x exp, custom shops) -- Monster.txt is mostly sound, the shops and MoveLevel are not.
WZ paths are under `Source/Server Side/GameServer/`. Lines marked INVENTION in our code
are the user's calls and are not listed as faults.

## The top list, most important first

| # | Update | Why it matters | Kind |
|---|---|---|---|
| 1 | Resistances: swap ice/poison, block with r/(r+1) on the raw number | Poison bounces off Devias monsters 67-83% in WZ, Ice lands; ours blocks both ~1% | bug |
| 2 | Damage rolls include the maximum | Every blow, both sides, 5-10% low at low level | bug |
| 3 | SD does not soak monster blows | Ours takes 90% of every monster hit on SD: ~2x survival. WZ: PvP only, 0.97d had none | decide |
| 4 | Life back per kill, and sitting heals | WZ: +monster level HP 2 s after each kill; sitting 3% HP+MP / 5 s anywhere. Ours: neither | decide |
| 5 | Drop option rolls | Luck 4% (ours 25), option ~8% (ours 25), skill 6%; excellent always has its skill, 2nd excellent option ~21% (ours 0.1%) | fix |
| 6 | Drop level window 15, + up to (L-itemL)/3 capped by MaxItemLevel | Ours 12 levels and +3 at most; WZ drops reach +5/+6 | fix |
| 7 | Jewel prices | Bless 9M, Soul 6M, Chaos 810k, Life 45M to buy, a third to sell; ours ~1,700 | bug |
| 8 | Monster sight is round, strict `<` | Ours square: 1.75x the aggro area at view 5, far diagonal pulls | fix |
| 9 | Chase and respawn | WZ chase 30 AI steps (~12 s), lost only past 15 tiles, never walks home; respawn regen+1 s (6 s, Ice Queen 11 s) at a random tile of its box, idle 5 s | decide (leash is INVENTION) |
| 10 | Per-monster drop rates | Item chance 10/ItemRate (5-8%), zen 10/MoneyRate (67-92%) of kills without an item; ours flat 10% / 50% | fix |
| 11 | Weapon wear | WZ ~1 point per 50-150 hits on tougher monsters; ours 1 per 10,000 | fix |
| 12 | Ice slows attacks too, not refreshed while on; poison lands hit or miss, 7 pulses from 2 s | Ice +800 ms per action; ours only halves walking | fix |
| 13 | Spawn counts | Noria 1005 vs WZ 477, Devias 560 vs ~110 (75 Ice Queens vs 14); Lorencia Lich/Giant/Skeleton 20/15/15 vs 45; WZ's small fixed clusters missing | decide |

Done 2026-09-30: #2 (rules.cpp, both the swing and the spell) and #7 (market.cpp, with the
Rune's row at the Jewel of Creation's 36M). #1 landed with the Ice Monster's chill, which
brought the resistance table in.
Also done 2026-09-30: #5 and #6 (Realm::leave, items.h; the excellent draw is 1/2000 of the
user's item chance), #8 for the monsters' eyes and arms only (realm_tuning.h `apart`; the
swing keeps the larger axis as well, or a walker halts half a tile short), #10 for Zen only
(kDropRates; the 10% item chance is the user's), #11 (wear.h weaponWear; the pendant keeps
OpenMU's) and #12 (realm_fight.cpp; the Ice timer is not paused under poison as WebZen's is).
The Poison and Ice hunts in sim_test moved off seed 7, where the round sight left them short.

The user's picks, 2026-09-30: #3 WebZen's (the shield meets a player's blow only, so in this
single-player game it soaks nothing; the skill hunts in sim_test keep their unspent heroes alive
with Realm::wholeAgain, since without it they died mid-hunt), #4 both (life per kill, rest on a
perch), #9 no leash and WebZen's respawn (the hit's chase is WebZen's ten steps, then eyesight;
nothing past fifteen tiles), #13 WebZen's counts -- taken back the same day: the user found
OpenMU's placement better, and kept only the respawn on a tile drawn anew from the nest.

Smaller, one line each:
- Luck crit +4% per item, not 5% (zzzitem.cpp:3023).
- A staff adds half its damage to melee, not all (ObjCalCharacter.cpp:496).
- Excellent spell is max*1.2 - defense, not (max - defense)*1.2 (ObjAttack.cpp:3744).
- Level-up drops leftover exp: one level per kill (user.cpp:7989).
- Repair 0.97d: broken x0.4, self-repair +5%, on the full buy price (zzzitem.cpp:4988, protocol.cpp:8493).
- Refine keeps wear in proportion; ours heals fully (user.cpp:28522).
- Attack rates: Hound 39, Budge Dragon 18, Elite Bull Fighter 50, Lich 62 (Monster.txt).
- Chaos only from monster level 13-66, weighted 3/7 of jewels; ~3x rarer in ours (MonsterItemMng.cpp:430).
- Jewel drops need jewel level strictly below the monster's (zzzitem.cpp:5897).
- Zen: 1 in 400 kills drops four extra piles (gObjMonster.cpp:4838).
- Missing: Noria->Atlans gate 45; Lost Tower gate is level 80; Devias guards are Berdysh (249).
- Out-rated attacker: 0.97d always misses; 1.00.93 5% at x0.3; ours 3% at x0.3.

## Where each lives

| # | WZ | Ours |
|---|---|---|
| 1 | user.cpp:8710-8711, MonsterAttr.cpp:233-236, public.h RESISTANCE_COLD 0 | realm_fight.cpp:527-538, realm_tuning.h:281-292 |
| 2 | ObjAttack.cpp:3117, 3331, 3694 | rules.cpp:42, 79 |
| 3 | ObjAttack.cpp:2262-2281 (ADD_SHIELD_POINT_01_20060403) | realm_fight.cpp:53-59, recovery.h:37-39 |
| 4 | user.cpp:13665-13669, :14243; sitting :23246-23380 | realm_fight.cpp:773-784, recovery.h:29-35, realm_items.cpp:651-672 |
| 5 | gObjMonster.cpp:4690-4714, 3125-3142 | items.h:306-326 |
| 6 | zzzitem.cpp:5962-5974, MonsterItemMng.cpp:626 | realm_items.cpp:720, 788, 794 |
| 7 | zzzitem.cpp:1789-1801 | market.cpp:217-252 |
| 8 | user.cpp:7042-7053, gObjMonster.cpp:899-903 | realm_tuning.h:135-141, realm_move.cpp:284, 480 |
| 9 | gObjMonster.cpp:2809-2820, 527, 620; user.cpp:21336; gObjMonster.cpp:185, 283 | realm_move.cpp:415-419, 463-466; realm_tuning.h:55-56; realm_fight.cpp:736, 886-905 |
| 10 | gObjMonster.cpp:4515-4567, 4762; Monster.txt ItemRate/MoneyRate | realm_items.cpp:719 |
| 11 | zzzitem.cpp:3831-3870 | wear.h:31-35 |
| 12 | ObjBaseAttack.cpp:670-680, 770-777; ObjAttack.cpp:578; gObjMonster.cpp:1521 | realm_fight.cpp:39-44, 97-117, 540-560; skills.h:99 |
| 13 | MonsterSetBase.txt (Lorencia 60-65, 2512-2520; Noria 81-83, 2554-2561; Devias 87-96, 214-270) | source/mu.db monster_spawns |

## What already matches

Stat-to-damage per class, wizardry band, defense and defense rate, PvM attack rate and
hit chance, crit/excellent (10%), HP/MP growth, 5 points a level, exp table and the
level-difference rule, mana regen, Heal and Greater Damage, poison/ice lengths, every
monster's level, HP, damage, defense, ranges and speeds, the gates and safe zones of
Lorencia/Noria/Devias, Bless 100% and Soul 50/75%, the price and sell formulas.

## Not built here, for later

Jewel of Life 50% (a failure resets the option). Chaos Machine +10 50% / +11 45%,
+20 with luck, cap 75%, 2M / 4M Zen, a failure takes the box. Chaos weapon: value/20000
success, weapon out at +0-4 (MixSystem.cpp:399-1948). AG per class (NEW_FORSKYLAND2).
