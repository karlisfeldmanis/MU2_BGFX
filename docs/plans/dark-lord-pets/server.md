# Dark Lord pets: the server rules (Dark Raven 13/5, Dark Horse 13/4, Pet Trainer 226)

## Sources and how to read the citations

- **WZ** = the WebZen GameServer 1.00.93 clone at `scratchpad/webzen`. Paths are shortened:
  - `GS/` = `Source/Server Side/GameServer/`
  - `BC/` = `Source/Base Common Src/` (where `zzzitem.cpp`/`.h` and `include/define.h` live)
- The build is `GAME_VERSION G_V_S2_2` (`BC/include/define.h:48`).
  - `DARKLORD_WORK` is on (`GS/define2.h:3298`, `define.h:4049` area).
  - `MODIFY_DARKSPIRIT_TAGET_ATTACK_01_20040906` is on (`define.h:3388`).
  - `MODIFY_EX_DAMAGE_DARKSPRIT_BUGFIX_20080826` is **commented out** (`define.h:1195`), so the raven has no excellent or double damage, and the old 30% critical code runs.
- Late flags (2008–2009) are named wherever they change a rule. The 0.97/0.99 base is the `#ifndef`/`#else` side.
- Line numbers are from the cp949 files. They match the `iconv` copies line for line.
- This GS tree also carries private-server edits: `g_GlobalConfig`, `Balancer`, `ResetSystem`, `DropTable`. They are flagged where they touch pets.
- **OM** = OpenMU at `LEGACY/reference/openmu/src`.

---

## 1. Dark Raven (CDarkSpirit, `GS/DarkSpirit.cpp`)

### Stats
They are recomputed in `CDarkSpirit::Set` (`DarkSpirit.cpp:640-689`) on every `gObjCalCharacter` (`GS/ObjCalCharacter.cpp:1287`). Here L = pet level and Cmd = `Leadership + AddLeadership`.

| stat | formula | cite |
|---|---|---|
| min damage | `180 + 15*L + Cmd/8` | DarkSpirit.cpp:666 |
| max damage | `200 + 15*L + Cmd/4` | :667 |
| attack speed | `20 + L*4/5 + Cmd/50` (integer) | :669 |
| attack rate | `1000 + L + L/15` | :671 |

- `Set` only works for item 13/5 with durability > 0 and a Dark Lord owner (:643, :656, :659).
- **Scepter rise** (`GetAttackDamage`, :1720-1744): if the right hand holds a scepter 2/8–2/12 (2/13, 2/14, 2/15, 2/17, 2/18 under later flags):
  - `pct = m_Magic/2`, then `pct -= pct*m_CurrentDurabilityState`.
  - `dmg += dmg*pct/100`.
  - `m_Magic` is the item.txt MagicPower column, plus `3*level`, plus the +10..+13 bonus, plus the excellent bonus (`BC/Common/zzzitem.cpp:752-778`).
  - `m_CurrentDurabilityState` is 0 / 0.2 / 0.3 / 0.5 / 1.0 as the scepter wears (`zzzitem.cpp:418-426`).
- **Final damage**: `dmg - targetDefense` (:1751).
  - Defense comes from `GetTargetDefense` (`GS/ObjBaseAttack.cpp:1276`): target `m_Defense + m_SkillDefense`, minus the defense-reduce debuffs.

### Timing
- `Run()` is called from `MonsterAndMsgProc` (`GS/user.cpp:752`), which fires on the 500 ms timer (`GS/GameServer.cpp:550, 1052`).
- Next attack time is `now + 1500 - speed*10` ms (`DarkSpirit.cpp:125-128`).
- **The effective interval is that value rounded up to a multiple of 500 ms.** Examples:
  - L1, Cmd 300: speed 26, 1240 ms, so it attacks every 1.5 s.
  - L50, Cmd 1000: speed 80, 700 ms, so it attacks every 1.0 s.
- Damage lands **600 ms after** the 0xA8 animation packet, through `gObjAddAttackProcMsgSendDelay(..., MSG_ATTACKPROC_DARKSPRITATTACK, target, 600, crit, actionType)` (:547-551). The message is handled at `user.cpp:7408-7414`, which calls `CDarkSpirit::Attack`.

### `Run()` gates (:94-128)
The raven does nothing if any of these hold:
- there is no master, or the master is not a Dark Lord;
- there is no pet item, or its durability is ≤ 0 (a "dead" raven);
- **the owner stands on a safe-zone tile** (:109-114);
- **the owner's Cmd is below `m_RequireLeaderShip`** (:116-123). This is under `MODIFY_SETITEM_LEADERSHIP_BUGFIX`, which is on for ≥ 0.99B (`define.h:1012-1015`). In 0.97 only the equip check stopped it.

The Cmd requirement is `185 + 15*L` (`zzzitem.cpp:506-524`). It is also checked when the item is put on (`gObjIsItemPut`, `user.cpp:15953`).

### Hit chance
`MissCheck` (:1756-1835) is used against monsters, and against players when SD is off.
- Blocking = target `m_SuccessfulBlocking`. The cap `g_GlobalConfig.m_iMaxMissRate` at :1766 is a private-server edit; ignore it.
- If `attackRate < blocking`, the attack is an "all miss": only 5% of hits land, and those deal 30% damage (:1769-1773, :1801-1816, :936-944; `MODIFY_SUCCESSBLOCKING_ALLMISS` is on).
- Otherwise it misses when `rand()%attackRate < blocking` (:1821-1833).
- **PvP with SD on** uses `MissCheckPvP` (:1838-2078), which runs on the *owner's* numbers:
  - DL attack rate: `lvl*3 + Dex*4`.
  - Target defense rate by class: DK and DL `lvl*2 + Dex/2`, Elf `/10`, DW and MG `/4`.
  - Hit % = `100 * AR/(AR+DR) * g_fSuccessAttackRateOption * Lvl/(Lvl+tLvl)`.
  - −5 if the target is 100+ levels higher. The −10 and −15 branches can never run because the 100 check comes first.

### Critical / excellent
- Critical means max damage (`min+sub`, :1710-1713) and the message type `DT_CRITICAL` (:859).
- There is **no excellent and no double damage** in this build (that is the commented-out 2008 flag).

### The four modes
`ePetItem_Mode`, `DarkSpirit.h:20-26`: 0 Normal, 1 Random, 2 WithMaster, 3 Target.

| mode | how the target is picked | what attack it does |
|---|---|---|
| 0 Normal | `ModeNormal` is empty (:150-153), so it does nothing. | — |
| 1 Random | Scans `VpPlayer2[]` (the owner's viewport) for **monsters only**: alive, Class not 100–110 (traps), not a summon (`m_RecallMon < 0`), same map, distance `< 5` from the **owner** (`MAX_DARKSPIRIT_ATTACK_RANGE`, `DarkSpirit.h:15`). It picks one at random each tick (:186-262). The duel and guild-war branches inside cannot fire because the outer `if` already demands `type == MONSTER`; in practice they only let in summons of a guild-war enemy. | 30% (`rand()%10<3`, :180-183): **direct hit**, max damage, full damage. 70%: **RangeAttack** (below). |
| 2 WithMaster | `m_iTargetIndex`. It is set by `gObjAttack` on **every** owner attack against anything, monster or player (`GS/ObjAttack.cpp:430`). The target must be on the same map, alive, and within distance `< 7` of the owner (:279-323). | The old code sets `criticaldamage = 0` inside the 30% roll (:297-300; a WebZen bug), so this mode **always** does RangeAttack. |
| 3 Target | The index comes from the client in 0xA7 (any object < MAX_OBJECT, :603-610). Same map, alive, `< 7` from the owner (:332-398). | Under the 2004-09 flag, always a **direct dash hit** (`SkillType 0`, actionType 1, so no /1.5). In `SendAttackMsg` the crit is re-rolled at 30% (:528-538), so 30% of hits deal max damage. |

**RangeAttack** (:401-492):
- It hits the main target, then walks the owner's viewport.
- Valid extra targets are non-summon monsters, or guild-war-enemy summons, standing **within distance < 4 of the main target**.
- The first extra target found is always hit. Each later one has a 1/3 chance.
- It stops after 4 extra hits (`HitCount > 3`).
- The main target and the extras are all sent `criticaldamage 0`, so each takes **damage / 1.5** (:927-929).

**Target loss and mode resets:**
- When a target dies, `gObjLifeCheck` calls `ReSetTarget` (`user.cpp:13904`). A dead target is also dropped at :321-322 and :394-397.
- If the mode was 3 it drops back to **Normal** (:582-587) and sends 0xA7.
- The mode is reset to Normal on: login (`user.cpp:4218`), respawn (`user.cpp:22006`), map move (`user.cpp:26001`), gate move (`user.cpp:26845`), and duel end (`user.cpp:14393`, `NewPVP.cpp:728`).
- **When the owner has no target:** mode 2 idles (`m_iTargetIndex == -1`, :279) and mode 3 cannot be set without a valid index. The target is not cleared when it walks out of range; the raven just doesn't attack until it is back in range.

### Packets (`GS/protocol.h:3214-3282`; handlers in `protocol.cpp:1580-1586`, `22093-22112`)
- `C1 A7` client to server: `PetType(0=raven) Command(0..3) NumberH NumberL`. It goes to `ChangeCommand` and then `SetMode`. There is no ownership or equip check beyond the Dark Lord class and `m_iMasterIndex != -1`.
- `C1 A7` server to client: the same layout, echoing the mode and target. It is sent only when the mode changes, or always for mode 3 (:599-624).
- `C1 A8` server to client, sent to self and viewport (:500-525): `PetType 0`, `SkillType` (0 = dash / direct, 1 = range ball), `NumberH/L` (the owner), `TargetH/L`.
  - SkillType is `!criticaldamage`: crit hits show as dash, normal hits as range.
  - The splash targets get no A8 of their own (`crit 2` skips the packet, :505).

### Other raven rules
- **PvP:** yes, in modes 2 and 3, against players the owner attacked or picked.
  - The normal `PkCheck` applies (`ObjBaseAttack.cpp:246+`), plus `gObjAttackQ` and `CheckAttackArea` (:798, :815). The target on a safe tile is immune (`ObjBaseAttack.cpp:153-160`).
  - SD is split by `GetShieldDamage` (:2121-2234).
  - A hit on a player sets self-defense like a normal attack (:1386-1541).
  - Random mode never picks players.
- **Safe zones:** the raven never acts while the owner is on a safe tile (:111). Players on safe tiles can't be hit.
- **Kill credit and exp:** `Attack(lpObj = owner, ...)` calls `gObjLifeCheck(target, owner, ...)` (:1617). The **owner** gets the kill, the exp, the drop rights and the aggro (`LastAttackerID`, `IncAgro`, :1358-1369). The raven is not a separate object.
- Other damage modifiers apply as normal: the target's damage-minus, Blocking (halves), Soul Barrier, angel, wings, dinorant and **dark horse** reduction (:948-1146).
  - There is a WebZen bug at :1099: a Dinorant on the *target* drains the *raven owner's* HP by 1.
  - Damage has a floor of `ownerLevel/10` (:957-962).

---

## 2. Pet level and experience (both pets)

- **Level cap 50:** `MAX_DARK_SPIRIT_LEVEL` / `MAX_DARK_HORSE_LEVEL` (`BC/Common/zzzitem.h:348-349`).
- **Exp table** (`zzzitem.h:354-368`):
  - `T[0] = T[1] = 0`, and `T[L] = (10+L)*L^3*100` for L ≥ 2.
  - A pet at level L levels up when `exp ≥ T[L+1]`.
  - The table is identical for both pets.
  - Values: T2 = 9,600; T3 = 35,100; T5 = 187,500; T10 = 2,000,000; T20 = 24,000,000; T30 = 108,000,000; T40 = 320,000,000; T50 = 750,000,000; T51 = 809,171,100.
- **Gain** (`gObjSetExpPetItem`, `GS/user.cpp:32505-32648`): it is called from `gObjLevelUp` (`user.cpp:7966-7967`), so it covers **every** exp gain of the Dark Lord: solo kills, party share, event rewards, and after the PC-bang/bonus additions.
  - `exp *= DarkSpiritAddExperience` (commonserver.cfg, default `"1"`, `GS/Gamemain.cpp:3662-3663`).
  - Horse **and** raven equipped: each gets **10%** of the exp (`exp*10/100`, :32519; the Korean comment says "5% each").
  - Only the horse, or only the raven: **20%** (:32583, :32616).
  - The pet must be in its slot: horse = helper slot 8, raven = left hand slot 1.
- `AddPetItemExp` (`zzzitem.cpp:3212-3296`):
  - Durability 0 gains no exp. A pet whose info isn't loaded gains none. A pet below level 1 is set to 1 / 0.
  - At max level it gains nothing.
  - It loops `while exp ≥ T[L+1]: L++`. The loop has no inner cap at 50 (an overshoot past 50 is theoretically possible).
  - On level-up it re-runs `Convert` and `PetValue`.
- **On level-up** (:32521-32541):
  - If the owner can no longer wear the pet (`gObjIsItemPut`), it calls `PetItemLevelDown(addexp)`: the gain is undone and the level is walked back (`zzzitem.cpp:3385-3447`). The owner gets message `lMsg 1245` (raven: "cannot control the Dark Spirit, cannot gain exp") or `1246` (horse).
    - For the raven that means the owner's Cmd is below `185 + 15*newL`.
    - For the horse it means the owner's level is below `218 + 2*newL`. So the horse level is capped at `(charLevel - 218)/2`.
  - Otherwise it logs, then `SendLevelmsg(..., InvenType 254)` sends a `C1 A9` level-up packet (`DarkSpirit.cpp:2098-2116`). There is no other notice.
- **Storage:**
  - Pet level and exp are **not in the item bytes**. They are kept per item serial in the DataServer pet table:
    - request on login: `0x56` (`GS/DSProtocol.cpp:3903-3984`), with the reply handled at :3987-4053;
    - save: `0x57` (:4055-4141).
  - In memory they are `BYTE`-ranged `m_PetItem_Level` and `int m_PetItem_Exp`.
  - The raw `pMsg.Level` field is a BYTE and `Exp` is an int (`protocol.h:3275-3276`).
  - `SetPetItemInfo` clamps exp into `[T[L], T[L+1])` (`zzzitem.cpp:3165-3210`).
  - The default level is 1 (`zzzitem.cpp:95`).
- **Pet info request and answer** (`protocol.cpp:22114-22306`):
  - Client sends `C1 A9 {PetType(0 raven / 1 horse), InvenType, nPos}`.
  - InvenType: 0 inventory, 1 warehouse, 2 own trade, 3 the other side's trade, 4 chaos/trainer box, 5 the personal shop you are viewing.
  - Server answers `C1 A9 {PetType, InvenType, nPos, Level BYTE, Exp int, [Life BYTE]}`.
  - `Life` is only there under `MODIFY_DARKLORD_SEND_INFO_PROTOCOL_EXTEND_20080626` (S4.5+; **off** in this S2_2 build, `define.h:1398-1401`).
  - Pushes from the server use InvenType 254 (level up) and 255 (level down) (`protocol.h:3272`).

---

## 3. Durability ("Life"): max 255 for both

Max durability is 255 (item.txt Dur column; `PetItemSerialCreateSend` fills it from `ItemGetDurability`, `DSProtocol.cpp:2961-2964`).

**Raven** (`DarkSpirit.cpp:968-1017`):
- On every **raven hit that lands** (after the miss check, defense, /1.5 and floor): `DurabilitySmall += dmg*2/100` (integer, so hits under 50 damage wear nothing).
- At `DurabilitySmall ≥ 800` it resets to 0 and Life drops by 1. That is about one Life per 40,000 raven damage dealt.
- The owner taking hits does **not** wear the raven.

**Horse** (`gObjSpriteDamage`, `user.cpp:10632-10688`; called from `CObjAttack::Attack`, `ObjAttack.cpp:1042`):
- On every **hit the rider takes**: `DurabilitySmall += (WORD)(dmg*2/10/10 + 1)`, which is about `dmg/50 + 1`.
- At `> 1500` it resets and Life drops by 1. That is at least 1,500 hits per Life point.
- Raven hits on a horse rider skip `gObjSpriteDamage`, so they don't wear the horse.
- Under the S4 flag `UPDATE_MASTER_LEVEL_4RANK` both thresholds get `+iMpsPetDurDownSpeed`.

**At Life 0** (raven :979-993, horse :10653-10666):
- The item **stays**. It is "dead": it doesn't attack, give defense or absorb damage, and gains no exp.
  - The checks are `Set` (:659) and `Run` (:106) for the raven, and `gObjDarkHorse` (`ObjAttack.cpp:3805-3820`) and `ObjCalCharacter.cpp:1085` for the horse.
- `DecPetItemExp(10)` (`zzzitem.cpp:3298-3383`): `exp -= 10% * (T[L+1] - T[L])`, i.e. 10% of the *current level's span*.
  - If exp falls below `T[L]`, the pet drops **one level**. The raven then runs `Set()` and `SendLevelmsg(..., 255)`.
  - If exp goes below 0, the pet resets to level 1 / exp 0.
- Then `GCItemDurSend` is sent.
- The horse log at :10656/10664 reads the *raven's* exp (a copy-paste bug, logs only).

**Repair cost** (`GetNeedMoneyItemDurRepaire`, `protocol.cpp:8415-8543`):
- `base = m_BuyMoney`. For pets this is the full value, not /3 like other items (:8442-8451).
  - Pet value comes from `PetValue` (`zzzitem.cpp:3452-3479`): horse `L*2,000,000`, raven `L*1,000,000`, rounded to 100.
- Cap `base` at 400,000,000 (:8453-8458), then round it.
- `cost = 3*sqrt(base)*sqrt(sqrt(base)) * (1 - life/255) + 1` (:8465-8470).
- If life is 0: **×2** for pets (:8472-8482).
- Repair from the inventory (`RequestPos 1`, needs character level ≥ 50, :8728):
  - this build: **+200%** (`UPDATE_INVEN_REPAIREMONEY_INC_20081112`, `define.h:1132`);
  - 0.97/0.99 base: +5% (:8493).
- Then round: ≥1000 to 100s, >10 to 10s (:8525-8536).
- Repair sets Life to 255, re-runs `Convert` and `PetValue`, and sends the durability (:8662-8691).
- Example: an L10 horse at Life 0: base 20M → sqrt 4472 × 66.9 × 3 ≈ 897,000 → ×2 ≈ 1.79M zen.

**NPC repair rules:**
- In the 0.97/0.99 base (`#ifndef UPDATE_DARKROAD_PET_ITEMREPAIR_20090109`):
  - `ItemDurRepaire` **refuses** 13/4 and 13/5 unless the trainer window (`I_DARK_TRAINER`) is open (`protocol.cpp:8556-8567`).
  - **Repair-all skips both pets** (:8791-8798).
- The 2009 flag (on here, `define.h:1020-1023`) lets pets repair anywhere, including from the inventory.
- 13/0–13/3 (angel, imp, uniria, dinorant) are never repairable (:8570-8571).
- So "Restore life" at the trainer is just the ordinary `C1 34` repair request made while the trainer box is open.

---

## 4. Dark Horse

- **Level requirement:** `218 + 2*L` (`zzzitem.cpp:578-583`; item.txt base 218).
- **Defense:** `+ (5 + totalAgi/20 + 2*L)` while Life > 0 (`ObjCalCharacter.cpp:1081-1091`; `Dexterity = Dexterity + AddDexterity`, :319).
  - `Convert` also adds `m_Level*2` for the item's +level (`zzzitem.cpp:878-884`), but that is always 0 for pets.
- **Damage absorb:** with a horse at Life > 0, every incoming normal hit is multiplied by `(100 - (30+L)/2)/100`. That absorbs 15% at L1 and 40% at L50 (integer: L2 and L3 both 16%).
  - Each such hit also costs the rider **1 HP** (`ObjAttack.cpp:1344-1368`; same at `DarkSpirit.cpp:1119-1146`).
  - The commented-out old formula was 10 + Cmd/100.
- **Earthshake (skill 62):**
  - The horse always carries the skill: `Option1 = 1`, `m_Special = AT_SKILL_DARKHORSE_ATTACK` (`zzzitem.cpp:1025-1031`).
  - Bonus damage `Str/10 + Cmd/5 + 10*L`, added to min and max, only while 13/4 is in the helper slot. It checks the type only, not Life (`CObjAttack::GetAttackDamage`, `ObjAttack.cpp:3221-3240`, line 3233).
  - Area: objects within distance < 5, up to about 15 hits (`ObjUseSkill.cpp:3779-3870`).
- **Speed:** not modelled by the GS. It is client-side only. Only OM gives it a movement-speed stat.
- **Horse level also changes:** the sell and repair price. Nothing else.
- **Other horse rules:**
  - The horse counts as a mount for Icarus and for unequipping wings (S4 flag, `user.cpp:16901-16920`, `26309`).
  - A Dark Lord on a horse who is registering the crown is not pushed back (`user.cpp:7477-7486`, `7720`).
  - The appearance bit is `CharSet[12] |= 1` (`user.cpp:20007`).

---

## 5. Pet Trainer (NPC 226) and breeding

- **NPC 226:** `NPC_DARKSPRIT_TRAINER` (`GS/NpcTalk.h:40`). The talk handler is `NpcDarkSpiritTrainer` (`NpcTalk.cpp:1781-1830`):
  - It needs `bCanChaosBox`, and refuses while you run a personal shop.
  - It sets `m_IfState.type = I_DARK_TRAINER` and sends `C1 30` with result **0x07** (the trainer window; OM `NpcWindow.PetTrainer = 7`).
  - It sends the chaos tax rate (`GCAnsCsMapSvrTaxInfo`).
  - The box is a chaos box variant (`ITEMMOVE_DARK_TRAINER`). Only these items may go in (`user.cpp:17094-17118`): 12/15 Chaos, 14/13 Bless, 14/14 Soul, 14/22 Creation, 13/31 Spirit, 14/53 Charm (2007 flag), and the S4 charge items.
- **Spawns:** none are hard-coded in the C++.
  - Repack `MonsterSetBase.txt:7`: `226 0 0 122 110 3`, i.e. Lorencia 122,110, dir 3. This is the only 226 line.
  - OM: Lorencia 122,110 SouthEast (`Persistence/Initialization/VersionSeasonSix/Maps/Lorencia.cs:36`), and Crywolf 135,47 (`CrywolfFortress.cs:47`; Crywolf is a later map).
- **"Restore life":** the repair in section 3.
- **"Resurrect spirit" / breeding:** the two mixes. They arrive as the ordinary chaos-mix button with type **13** (horse) or **14** (raven) (`define.h:5130-5131`; dispatched at `protocol.cpp:13639-13646`).

| mix | exact box contents (nothing else: `iInvalidItemCount`, flag on at `define.h:3268`) | zen (+ castle chaos tax) | rate | cite |
|---|---|---|---|---|
| Dark Horse | 1 Spirit **13/31 level 0** + 5 Bless + 5 Soul + 1 Chaos + 1 Creation | 5,000,000 | 60% + charm (≤ 10 charms, more is refused with 0xF0) | MixSystem.cpp:3051-3250 |
| Dark Raven | 1 Spirit **13/31 level 1** + 2 Bless + 2 Soul + 1 Chaos + 1 Creation | 1,000,000 | 60% + charm | MixSystem.cpp:3253-3451 |

- **Success:** `PetItemSerialCreateSend(..., 254, item 13/4 or 13/5, itemlevel 0, dur 0 → 255, ...)` (:3218, :3419).
  - The pet starts at **pet level 1, exp 0** (the CItem default; the DataServer creates the row).
  - It also rolls options:
    - `Option1` (skill) at `6 + rate/5` %;
    - luck at 1/5;
    - add-option: pick 1 of 3; then +3 at 4%, +2 at 10% or +1 at 20% (:3193-3216).
  - The horse's skill bit is forced on by `Convert` anyway.
- **Failure:** the **whole box is lost**, including the spirit (`ChaosBoxInit`, :3235, :3436). The zen is already taken.
- There are no other trainer mixes in this GS.

---

## 6. Spirits (13/31)

- **One item row**, item.txt: `31 -1 0 1 1 1 0 0 "Spirit" ...`, 1×1, **drop flag 0**, level 0, DL column 1 (repack item.txt and item(Kor).txt; row tables OK).
  - **Level 0** = Spirit of Dark Horse, **level 1** = Spirit of Dark Raven.
- **Price** (`zzzitem.cpp:2253-2266`, tail at :2678-2680):
  - buy: horse spirit 30,000,000, raven spirit 15,000,000;
  - sell: one third, so 10,000,000 / 5,000,000.
- **Drops:**
  - The GS C++ has **no hard-coded spirit drop**: no `13,31` anywhere outside the mix and the box filter.
  - The normal monster drop needs `MondownFlag`, which is 0.
  - So in WebZen it comes from data. In this repack that is the private `DropTable.cfg:35-40`, at rate 150/10000 = 1.5%:
    - map 10, monster 75 (Gigas Drakan, lvl 108);
    - Kalima 5 (lvl 105–117);
    - Kalima 6 (lvl 83–121).
  - **Don't use those as official.**
  - OM: 0.1% from any monster with level ≥ 102 (horse) or ≥ 96 (raven) (`Pets.cs:206-224`). That matches the commonly cited official rule and is my pick.
- **Stacking:** none. The spirit is a plain 1×1 item.

---

## 7. Prices of the pets

`PetValue`, `zzzitem.cpp:3452-3479`:
- Buy value: horse `L × 2,000,000`, raven `L × 1,000,000`.
- **Sell = buy / 3** (`BUGFIX_SELL_PRICE_OF_DARKLOAD_PETS_20050310`, `define.h:3079`), rounded. Before 2005-03 the sell price equalled the buy price.
- The sell handler calls `PetValue` (`protocol.cpp:8289-8296`). No durability scaling applies.
- Neither pet is sold in any shop.

---

## 8. What the server sends (item card)

- The standard item bytes (`ItemByteConvert`, `zzzitem.cpp:4402+`):
  - index;
  - byte 1: level bits = item +level, **always 0** for pets; skill bit (horse forced 1); luck; option;
  - **byte 2 = Life (durability)**;
  - excellent and set bits as rolled. They carry no pet level or exp.
- The client gets level and exp by asking `C1 A9` (section 2). The server pushes A9 on level up (254) and down (255).
- In ≥ S4.5 the A9 reply also carries Life.
- Everything else on the card is computed **client-side** from the level, the owner's stats and the pet's type: raven damage, speed, attack rate, Cmd requirement; horse level requirement, defense, absorb.
- Login fills level and exp from the DataServer (`user.cpp:2921`, `DSProtocol.cpp:2698`).

---

## 9. WebZen vs OpenMU: where they differ (WebZen is right in every case unless noted)

| topic | WebZen | OpenMU | note |
|---|---|---|---|
| range | random mode `< 5` from the owner; modes 2/3 `< 7` | 7 for all modes (`GameLogic/Pet/RavenCommandManager.cs:21`, 298) | |
| attack mix | random: 30% direct crit at max damage, 70% range (main + ≤ 4 extras within < 4 of the target, 1/3 each after the first, all ÷1.5); with-owner: always range; target: always direct, 30% crit | 30% range (main + ≤ 2 within 3, 3 hits cycling), 70% single, the same in every mode (:109, :139-157) | OM's split is inverted |
| interval | `1500 - 10*speed` on a 500 ms tick, plus 600 ms damage delay | continuous, min 100 ms (:45) | |
| target filter | monsters only for random; Class 100–110 traps and summons excluded | guards excluded (:296) | |
| raven wear | per raven hit, `dmg*2/100` per 800 | per *owner* hit, as weapon durability (`GameLogic/Player.cs:2027-2045`) | OM wrong |
| horse wear | `dmg/50 + 1` per 1500 per hit taken | `dmg/100000` (`GameConfigurationInitializerBase.cs:62`, `Player.cs:1987-2023`) | OM's horse almost never wears |
| death penalty | −10% of the level span, can drop one level | `exp*0.9`, floored at the level start, no level loss (`Player.cs:1993-1996`) | |
| exp share | 20%, or 10% each; × config | the same (`GameLogic/PlugIns/PetExperiencePlugIn.cs:28-47`) | agree |
| level-up check | after the gain, if the owner can't wear it the level is reverted, with message 1245/1246 | raven stops levelling at Cmd < req; horse unchecked (:85-86) | |
| exp table | `T[L]=(10+L)L³·100`, `T[1]=0` | the same formula, but `T[1]=1100` (`Pets.cs:30`; `GameLogic/Pet/PetLevelHelper.cs`) | trivial |
| damage, speed, rate | as in section 1 | the same constants (`Persistence/Initialization/CharacterClasses/ClassDarkLord.cs:84-138`); rate `16/15*L` vs `L + L/15` | agree up to rounding |
| scepter rise | `m_Magic/2` %, reduced by scepter wear | `ScepterRise/100` (`ClassDarkLord.cs:84`) | |
| horse absorb | `(30+L)/2` % (integer), 1 HP per hit | `15% + 0.5%·L` (float), no HP cost (`Pets.cs:294`) | |
| horse defense | `5 + Agi/20 + 2L` | the same (`Pets.cs:295`) | agree |
| Earthshake | `+Str/10 + Cmd/5 + 10L` | `+10·HorseLevel` (`SkillsInitializer.cs:615`) plus OM's general formula | |
| repair | pets only at the trainer, repair-all skips both (base); ×2 when dead; base = full value | ×2 when dead and full value, agree; repair-all skips only the helper slot, so the **raven is repaired by repair-all**; the pet slot needs any NPC (`GameLogic/PlayerActions/Items/ItemRepairAction.cs:24`, :73; `GameLogic/ItemPriceCalculator.cs:359-371`) | |
| prices | `L×2M` / `L×1M`, sell /3; spirits 30M / 15M | the same (`ItemPriceCalculator.cs:192`, :446-454) | agree |
| horse mix | spirit must be **level 0** | any spirit level (`ChaosMixes.cs:816-820`) | OM bug |
| mix result | pet level 1; rolls skill/luck/option bits | pet level 1 (raven `RandomMin/MaxLevel 1`, :905-906) | |
| spirit drops | none in the C++; repack data is private | 0.1% at monster level ≥ 102 / ≥ 96 | use OM |
| trainer spawns | Lorencia 122,110 (repack) | Lorencia 122,110 plus Crywolf 135,47 | Crywolf belongs to a later version |
| safe zone | owner on a safe tile means no raven action | the same (`RavenCommandManager.cs:104`, :183, :216) | agree |
