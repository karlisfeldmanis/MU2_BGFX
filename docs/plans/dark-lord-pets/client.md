# Dark Lord pets: what the client does (MuMain S6, checked against Main 1.03.35)

Paths:
- `MM` = `LEGACY/reference/MuMain/src/source`
- `M103` = `MU2_BGFX/third_party/Main 1.03.35/MU/[11.07.12] MU_Chs_1.03.35/source` (its files are cp949; use `grep -a`)
- `DATA` = `LEGACY/reference/MuMain/src/bin/Data`

Strings are English from `MuMain/src/Localization/Game.en.resx`, with the legacy Text.bmd id in [brackets]. 1.03.35 uses the same ids through `GlobalText[n]`.

All per-tick numbers are for the 25 Hz reference. MuMain multiplies them by `FPS_ANIMATION_FACTOR`; 1.03.35 uses plain `++` and `rand()%N==0`.

Comparing 1.03.35 with MuMain: the pet logic is the same in the parts that matter. Every constant and formula in CSPetSystem, GIPetManager and npcBreeder matches, and so do the raven's mesh and level rules and the trainer message boxes. The exceptions are listed in §8.

---

## 1. Dark Raven (item 13/5, `MODEL_DARK_SPIRIT`)

### Models and textures
- **The flying pet** is `Data/Skill/darkspirit.bmd` (`MM/Engine/Object/ZzzOpenData.cpp:4226`, textures from `Skill\` at :4364).
  - BMD v0xC, 5 meshes, 77 bones, 4 actions.
  - mesh0 `dkthreebody.jpg`, mesh1 `dkswing.jpg`, mesh2 `dkonebody.jpg`, mesh3 `dksbody.tga`, mesh4 `foot.JPG`. mesh4 is never drawn.
  - Actions (keys): 0 = 6 keys (glide), 1 = 4 (fast flap), 2 = 4 with LockPositions (perched), 3 = 4 (attack dive).
  - Bone 6 is `Bip01 Head`. The flight sparks pick a random bone 0..65.
  - The level-40 glow texture `Skill\dkthreebody_r.jpg` loads as `BITMAP_MONSTER_SKIN+2` (`ZzzOpenData.cpp:4227`).
- **The inventory/drop item** is `Data/Item/spiritbill.bmd` (`ZzzOpenData.cpp:721`). It has 2 meshes: `dkshelmet.jpg` and `dksbody.tga`. Its textures are opened from both `Skill\` and `Item\` (:1299-1300).
  - Item render (`MM/Engine/Object/ZzzObject.cpp:7136`): the body, then mesh0 again as RENDER_BRIGHT|RENDER_CHROME with BodyLight (0.3, 0.8, 1.0).
  - It is **never drawn in the hand**. The weapon loop skips `MODEL_DARK_RAVEN_ITEM` (`ZzzCharacter.cpp:10093`), and for viewport players group 13/5 in the left-hand slot creates the pet instead of a weapon (`ZzzCharacter.cpp:12858`).
- The equip slot is the left hand / shield, `EQUIPMENT_WEAPON_LEFT` (`ZzzInfomation.cpp:1081`). Max durability (= pet life) is fixed at 255 (`ZzzInventory.cpp:1623`).

### Spawn and lifetime
- The pet is created on equip: `NewUIMyInventory.cpp:140`, `NewUIInventoryCtrl.cpp:1610`, `GIPetManager.cpp:177-196`, and viewport joins at `ZzzCharacter.cpp:12858`.
- It is **never created in Chaos Castle** (`GIPetManager.cpp:181`). It is deleted on unequip (`NewUIMyInventory.cpp:168`).
- `CreatePetPointer` (`MM/GameLogic/Pets/CSPetSystem.cpp:64-199`) sets:
  - Scale **0.7**, Light (0.5, 0.5, 0.5), Velocity 1, Gravity 13, no shadow flag.
  - Bounding box (-60,-60,0)..(50,50,150).
  - The spawn sits 300 above the terrain at the owner's tile (:300).
- Animation speed is **0.4 keys/tick** for every raven action (other pets use 0.1) (`CSPetSystem.cpp:201-215`).

### Level look (`ZzzObject.cpp:981-1023`; identical in `M103/ZzzObject.cpp:1073`)
`o->WeaponLevel` is set to the pet level every frame (`CSPetSystem.cpp:631`).

| Level | What is drawn |
|---|---|
| any | mesh3 (`dksbody.tga`), RENDER_TEXTURE |
| < 20 | mesh2 `dkonebody` |
| 20–39 | mesh1 `dkswing` |
| ≥ 40 | mesh0 `dkthreebody`; again as CHROME\|BRIGHT with BodyLight (0.3, 0.6, 1.0); then mesh3 BRIGHT with texture `dkthreebody_R`, BlendMeshLight = `sin(WorldTime*0.001)` (period ≈ 6.28 s; the negative half shows nothing) |

- After the body, a **projected body shadow** is drawn at terrain height. It is black with alpha 1.0, or 0.2 in Atlans. It is skipped in Heaven, Hellas and the Kanturu Maya scene (:1004-1022).
- **Quirk (both clients):** `giPetManager::RenderPet` always passes in the **local hero's** raven info (`GIPetManager.cpp:217-220`; `M103/GIPetManager.cpp:146-150`). So every raven on screen, other players' too, shows the local player's raven level. The client has no per-player raven level.

### AI states (`_enum.h:3549`)
PET_FLYING=0, PET_FLY=1, PET_ESCAPE=2, PET_STAND=3, PET_STAND_START=4, PET_ATTACK=5, PET_ATTACK_MAGIC=6.

Action used when an animation ends (`CSPetSystem.cpp:390-403`):
- FLY → action 0
- FLYING → action 1
- STAND → action 2
- STAND_START → action 1
- ATTACK / ESCAPE → action 3
- anything else → action 0

### Following and orbit (FLY/FLYING, `CSPetSystem.cpp:421-505`)
- **Target point:** the owner's position. With "Attack target" active (`m_bActionStart`), it is the target's position instead (:408-419). FlyRange = 150.
- **Every tick:** one particle `BITMAP_SPARK+1` (`Effect/Spark03.jpg`), subtype 5, scale 0.8, light (0.3, 0.4, 0.7), at a random bone 0..65. It is skipped while the owner is cloaked (:429-437).
  - Spark sub 5: life 10, drifts 2–3 u/tick along its local −X axis after the yaw. Each tick scale ×0.9 and light ÷1.03 (`ZzzEffectParticle.cpp:2148-2153` create, 6652 move).
- **Steering:** while the horizontal distance is ≥ 150, yaw turns toward the target by rand 5..19 °/tick (:440-444). Each tick the raven moves by `Direction` rotated by its angles; forward is local −Y.
- **Height:** goal = target z + **250**, or **350** if the owner has a Dark Horse equipped or this is the login scene (:451-457).
  - Below the goal: pitch −2 °/tick (clamped at −15), and a speed re-roll chance of 1/10.
  - More than goal+100: pitch +2 (clamped at +15), re-roll chance 1/20.
  - Otherwise the re-roll chance is 1/28 (:459-474).
- **Speed re-roll:**
  - Far (≥ 150): speed = −(12.8 .. 19.1).
  - Near: speed = −(3.2 .. 3.9), and yaw += rand 0..59°. This makes the lazy orbit and wobble.
  - Then speed = (speed + old Direction.y) / 2. Direction becomes (0, speed, rand −3.2 .. 3.1) (:476-492).
- **State from speed:** Direction.y < −12 gives PET_FLYING (fast flap, action 1); otherwise PET_FLY (glide, action 0) (:494-504).
- **Snap back:** if the raven is more than 200 below the owner, or more than 640 away horizontally (dist² > 409600), LifeTime +1 per tick. When LifeTime > 90 ticks (3.6 s), it teleports to the owner's position + 250 z (:597-609).
- **Off screen:** Velocity = 3 and AI = FLYING, unless it is in STAND_START (:645-652).

### Town vs field (perch)
- **In a safe zone** (owner `SafeZone`) the raven goes to PET_STAND_START, with Velocity 3 if it is more than 50 away (:369-384).
  - STAND_START: it homes on the owner's **bone 42** (`hand_bofdgne01`, a child of `Bip01 L Hand`) with `MoveHumming`. The turn rate equals the velocity; velocity +1 per tick. Below 50 it becomes STAND (:563-583).
  - STAND: its position is locked to the owner's **bone 37** (`Bip01 L Hand`) at local offset (−10, 0, 10). Its angle is the owner's angle with yaw −120°. It plays action 2 (:554-562).
- **The owner** switches to `PLAYER_DARKLORD_STAND` / `PLAYER_DARKLORD_WALK` (arm raised for the bird) while in a safe zone with a raven, except in Chaos Castle (`ZzzCharacter.cpp:278-281, 563-566`).
- **Leaving the safe zone:** STAND or STAND_START becomes PET_FLYING (:385-388).
- **Cloaking:** when the owner or the target is cloaked, an attacking raven is sent back to Normal, with a Normal pet command sent to the server (:328-367). The raven is then drawn with PetState 10, the transparent pass (`GIPetManager.cpp:211-214`).

### Attacks (server packet 0xA8 → `SetAttack`, `CSPetSystem.cpp:245-286`)
`m_bySkillType` 0 = PET_ATTACK (rush), 1 = PET_ATTACK_MAGIC. AttackTime +1 per tick, capped at 15 (:584-591).

**Rush (PET_ATTACK):**
- Setup: Velocity = 20..29, Gravity 0.5, then play `DSpirit_Rush.wav`.
- Each tick (:506-531):
  - It moves forward by Velocity.
  - The aim point is the target + 50 z. It turns with `MoveHumming` at Velocity °/tick.
  - Velocity += Gravity, and Gravity += 0.2.
- When the distance is < 20, or LifeTime > 20 ticks: PET_ESCAPE, with pitch −45.
- ESCAPE: Velocity −1 per tick until it is more than 250 from the owner, then FLYING (:532-540). Both states play action 3.
- FX (`CSPetSystem.cpp:729-757`):
  - **AttackTime 0..2:** 10 joints per tick of `BITMAP_LIGHT` (`Effect/flare01.jpg`), sub 1, scale 20..59.
    - Joint sub 1: life 10, 20 tails, light (1, 0.8, 0.6), pitch 30, random yaw, speed 1..5 accelerating 0.5 per sub-step. Light ÷1.2 per tick (`ZzzEffectJoint.cpp:2429-2440`, 6460-6478).
    - The result is a golden streak burst.
  - **AttackTime == 1:** one `MODEL_DARKLORD_SKILL` effect, subtype 3, light (1, 0.8, 0.6), angle (45, rand −90..89, 0).
    - This is `Skill/DarkLordSkill.bmd`, a single quad with texture `KingS_R.jpg`. LifeTime 10, Scale 0.2, anim speed 0.1, rendered with o->Light (`ZzzEffect.cpp:3449-3474`, `ZzzObject.cpp:975`).
  - **AttackTime > 3 and odd, with the raven more than 100 above the owner:** `MODEL_AIR_FORCE` subtype 0 at the head (bone 6), offset (50, 0, 0).
    - This is `Skill/airforce.bmd`, a sphere with texture `SpiE_R.jpg`. Life 15, scale 0.6 + 0.2 per tick, BlendMeshLight ÷1.5 per tick (`ZzzEffect.cpp:3176`, `Behaviors/MoveHandlers.cpp:5328-5342`). It reads as a sonic-boom puff.

**Magic (PET_ATTACK_MAGIC):**
- Plays `DSpirit_Missile.wav`.
- The raven hovers and turns its yaw toward the target at 40 °/tick (:543-553).
- **AttackTime < 15 (charge, 0.6 s):** every tick, a particle `BITMAP_LIGHT` sub 6 (scale 1.3, light (1, 0.6, 0.4)) on every other non-dummy bone, starting at 0 or 1 and running to 65.
  - Sub 6: life 10..19, scale 0.5..1×1.3, rises 2.5 per tick with jitter, scale and light ×0.95 per tick (`ZzzEffectParticle.cpp:3177-3182`, 8014-8030).
- **AttackTime 15 (release):** `RenderMeshEffect(mesh 1, BITMAP_LIGHT, 1)`. Every 22nd triangle of mesh1 (96 tris → 5 tris × 3 verts) spawns a joint `BITMAP_JOINT_SPIRIT` (`Effect/JointSpirit01.jpg`), sub 13, scale 20, aimed at the target (`ZzzBMD.cpp:2184-2200`).
  - Joint sub 13: alpha blend, Velocity 40, life 20, 10 tails, light (1, 0.6, 0.4) (`ZzzEffectJoint.cpp:812-822`).
  - It homes on the target ±70 xy jitter, +50 z. The extra lifetime tick-down starts once it is within 50.
  - Each tick it draws sprites `BITMAP_LIGHT` and `BITMAP_SHINY+1` at scale 2×(life/10)+2. Light ÷1.2 per tick in the last 10 ticks (`ZzzEffectJoint.cpp:3830-3885`).
  - Then the raven returns to FLYING, and AttackTime resets (:776-792).
- **Nothing pet-specific is drawn on hit.** Damage numbers come from the normal damage packet.

### Level up / down (packet 0xA9 with InvType 254 / 255, `GIPetManager.cpp:475-482`)
- Level up (`CSPetSystem.cpp:656-667`): 5 × `CreateJoint(BITMAP_FLARE, pos, pos, 0, sub 0, raven, scale 40, PKKey 2)`.
- Level down (:669-679): 15 of the same.
- `BITMAP_FLARE` is `Effect/Flare.jpg`. Sub 0 with scale above 10: life 50, 20 tails, light copied from the raven (0.5 grey). It spirals at radius 40 around the spawn point at angle (rand(−250..250) + life)/2 rad, and rises 2.0..4.5 per tick (`ZzzEffectJoint.cpp:1669-1690`, 5367-5395).
- There is **no level-up text** anywhere in the client.

### Sounds (`ZzzOpenData.cpp:4876-4878`)
- `Sound/DSpirit_Missile.wav` (4 buffers): played when the magic attack starts.
- `Sound/DSpirit_Rush.wav` (3): played when the rush starts. Both are non-positional `PlayBuffer`.
- `Sound/DSpirit_Shout.wav` (1): positional on the raven. Plays at random, about 1 in 6000 ticks (≈ 4 min): `rand_fps_check(100)&&rand_fps_check(60)` (`CSPetSystem.cpp:592`); in 1.03.35, `rand()%100==0 && MoveSceneFrame%60==0`.

---

## 2. Dark Horse (13/4): what changes with level
- **Riding render** (`ZzzObject.cpp:783`, `GOBoid.cpp:323`): it does **not** change with level. It is hidden (alpha 0) in safe zones.
- **Level up** (0xA9 with InvType 254 sets `Hero->Object.ExtState = 1`, `GIPetManager.cpp:487-490`): in `GOBoid.cpp:469-488`, 4 directions × 3 joints of `BITMAP_FLARE` sub 0, scale 40, PKKey 2, at radius 50 around the horse. That makes 12 rising spiral flares. ExtState 2 (level down) draws nothing.
- **Item models:**
  - Horn `Data/Item/DarkHorseHorn.bmd` (`dkhead2.JPG`, `dkheadwing.JPG`; `ZzzOpenData.cpp:719`, textures from `Skill\`+`Item\` :1296-1297). Render: body, then BRIGHT|CHROME with (0.8, 0.4, 0.1) (`ZzzObject.cpp:7130`).
  - Spirit `Data/Item/DarkHorseSoul.bmd` (`change.jpg`) is item 13/31; see §5.
- **Stats from level** (`GIPetManager.cpp:534-539`):
  - DmgMin = Str/10 + Cmd/10 + Lvl×5; DmgMax = Min × 1.5.
  - AtkSpd = 20 + Lvl×4/5 + Cmd/50.
  - Rate = 1000 + Lvl×16.
  - Exp to next = (10 + L)·L³·100, with L = level + 1.
- **Defence option** written into the item (`SetPetItemConvert`, :544-570): `AT_SET_OPTION_IMPROVE_DEFENCE` = 5 + Dex/20 + Lvl×2. It shows as "Increase defensive skill +%d" [959].
- **Sounds:** `Sound/pHorseStep1..3.wav`, a random step sound while running (`ZzzOpenData.cpp:4863-4865`, `ZzzCharacter.cpp:766`).

---

## 3. Pet info window (`MM/UI/NewUI/Character/NewUIPetInfoWindow.cpp`)
- **Opening:** the **"Pet" [1217] button on the Character window** at (x+87, y+392), 36×29, image `newui_chainfo_btn_pet.tga` (`NewUICharacterInfoWindow.cpp:122-124, 204-207`). It is shown for every class.
  - There is no hotkey.
  - It opens in column 2, beside the Character window, and hides Inventory and Quest (`NewUISystem.cpp:224, 764-780`).
  - Size **190×429** (`NewUIPetInfoWindow.h:53-54`).
- **Frame:** `newui_msgbox_back.jpg`, then `newui_item_back04.tga` top (64), `newui_item_back02-L/-R.tga` sides (21 wide), and `newui_item_back03.tga` bottom (45) (:126-130).
  - Title "Pet" [1217] in bold, centred at (x+60, y+13), width 70.
  - Close button `newui_exit_00.tga` at (13, 392), 36×29, tooltip "Close" [1002].
- **Tabs:** radio group `newui_guild_tab04.tga` at (12, 48), 56×22 each: "Dark horse" [1187] and "Dark Raven" [1214]. The horse tab is the default (:20, 64-71).
- **No pet:** red (`0xFF0000FF` ABGR) bold text at (15, 100), box 160×30.
  - Horse tab: "No %s." [1233] with "Dark horse".
  - Raven tab: "No pet" [1169].
- **Group boxes** (`RenderGroupBox`, :280-303): title area 60×20 at black 0.9, body at black 0.6. Corners and edges use `newui_item_table01(L/R)`, `02(L/R)` and `03(Up/Dw/L/R).tga`.
- **Raven tab** (:221-278):
  - "Dark Raven" in white, centred at (60, 25).
  - **Box 1** at (12, 75), 168×121:
    - Yellow bold "Level: %d" [200] at (+2, +8).
    - White "Life: %d/%d" [358] (life/255) at (+10, +28).
    - Bar back `newui_pet_lifebar01.jpg` at (+7, +40) 151×12; fill `newui_pet_lifebar02.jpg` at (+9, +42), width life×147/255, height 8.
    - "Exp: %d/%d" [357] at +59.
    - "Dmg(rate): %d~%d (%d)" [203] at +72.
    - "Attack speed: %d" [64] at +85.
    - "Charisma Requirement: %d" [698] = 185 + Lvl×15, at +98.
  - **Box 2** at (12, 196), 168×195:
    - Yellow bold "Commands" [1218].
    - Four `newui_skillbox.jpg` 32×38 slots at y +28/+68/+108/+148.
    - Icons from `newui_command.jpg` (19×27 at u = 1/21/41/61 px).
    - Labels at (+52, y+17): "Basic action" [1219], "Random automatic attack" [1220], "Attack with owner" [1221], "Attack target" [1222].
  - **Damage shown** = pet min/max × (1 + right-hand weapon `MagicPower`/100), i.e. the sceptre's "pet attack %" (`CalcDamage`, :347-363).
- **Horse tab** (:185-219): box 1 only (168×105), with Level, Life bar, Exp, Dmg(rate) and Attack speed. The damage shown = pet dmg + Earthshake skill damage + Earthshake master boost (:331-345).
- **Raven formulas** (`GIPetManager.cpp:517-532`), with Cmd = Charisma + AddCharisma:
  - DmgMin = 180 + Lvl×15 + Cmd/8; DmgMax = 200 + Lvl×15 + Cmd/4.
  - AtkSpd = 20 + Lvl×4/5 + Cmd/50.
  - Rate = 1000 + Lvl×16.
  - Exp to next = (10 + L)·L³·100, with L = Lvl + 1.
  - The command needed is 185 + Lvl×15.

### Raven command buttons
- **Command ids:** `AT_PET_COMMAND_DEFAULT=120, RANDOM=121, OWNER=122, TARGET=123` (`_enum.h:3347`).
  - A Dark Lord gets all four as skills (`WSclient.cpp:1411-1417`).
  - Names come from [1219+cmd]; tooltip descriptions [1223..1226]: "Follow around the character." / "Attack any monsters around the character." / "Attack the monster together with the character." / "Attack the monster selected by the character." (`GIPetManager.cpp:342-376`). The tooltip shows the blue bold name, two blank lines, then the description.
- **Where they appear:** a row of 4 slots 32×38 at (353, 352), drawn under the open skill list (`m_bSkillList`) when a pet exists (`NewUIMainFrameWindow.cpp:1652-1680, 2073-2097`).
  - The current command uses `IMAGE_SKILLBOX_USE`.
  - Icons come from `newui_command.jpg`, cell (cmd%8, cmd/8) at 20×28.
  - **Shift+1..4** selects a command (:1759-1769).
  - Hotkeys are allowed, but only while a pet exists (:1851).
- **Sending:**
  - Clicking a slot only sets `Hero->CurrentSkill`. The command is **sent on right-click** (`ZzzInterface.cpp:2697` → `GIPetManager.cpp:287-324`).
  - Default, Random and Owner: `SendPetCommandRequest(petType, mode, 0xFFFF)`.
  - **Target:** sent only if `CheckAttack()` passes and `SelectedCharacter` (the character under the cursor) is a monster or player. That target's Key is sent. Otherwise nothing is sent, but the right-click is still consumed.
  - The pet commands are disabled in Chaos Castle (`NewUIMainFrameWindow.cpp:2267-2275`).
- **Packets** (1.03.35 `wsclientinline.h:4040-4066`):
  - C→S `C1 A7 [petType][cmd][keyH][keyL]`, with petType 0 = raven, 1 = horse.
  - C→S `C1 A9 [petType][invType][slot]`: the pet info request. It is sent on equip, on tooltip hover (debounced to 1 s), and when you click the trainer.
  - S→C `A7 [petType][cmd][keyH][keyL]` → `SetCommand`. Target mode locks `m_PetTarget` to that Key (`WSclient.cpp:10654`, `CSPetSystem.cpp:223-243`).
  - S→C `A8 [petType][skill 0/1][ownerKeyH/L][targetKeyH/L]` → attack.
  - S→C `A9 [petType][invType][pos][level][int exp][life]`. invType 254 = level up and 255 = level down; both play the flares (`WSclient.cpp:10674-10687`, `GIPetManager.cpp:458-502`).
  - StorageType values (`PacketFunctions_ClientToServer_Enums.h:23`): 0 Inventory, 1 Vault, 2 TradeOwn, 3 TradeOther, 4 Crafting, 5 PersonalShop, 6 NpcShop, 254 pet slot.
  - MuHelper only uses modes 0..2 (`MuHelper.cpp:399-407`).

---

## 4. HUD (`MM/UI/NewUI/Inventory/NewUIItemEnduranceInfo.cpp`)
- The stack starts at (2, 26) (`NewUISystem.cpp:403`). Frames are 57×23 (`newui_Pet_Back.tga`), with a bar 49×3 (`newui_Pet_HpBar.jpg`) at (+4, +16). Frames are 24 apart (`.h:36-39`, :737-738).
- Backing: black at alpha 0.7, or (0.2, 0, 0, 0.7) for a warning. The name is white and centred at the top (+5) (`RenderHPUI`, :335-379).
- **Order** (`RenderLeft`, :261-294): the helper/mount first ("Dark horse" [1187], life = helper durability/255, :408-451), then **"Dark Raven" [1214]** for Dark Lords with a raven (life = left-hand durability/255, :496-508).
- The `PJH_FIX_SPRIT` pink tint for "not enough command" is compiled out in both clients (commented out at `M103/Winmain_New.h:70`).
- **The old top-right raven indicator** (`RenderCmdType`, `CSPetSystem.cpp:681-720`: name, a 50×2 life bar, and the command icon from `BITMAP_SKILL_INTERFACE+2`) has **no callers** in MuMain. It is the legacy UI.
- **No level-up message text** exists. The only feedback is the flares (§1, §2) and the updated numbers.

---

## 5. Item tooltips
**Raven and horse** (`GIPetManager.cpp:595-769`, reached from `ZzzInventory.cpp:2138-2162` whenever the item is not being sold).
There is no item-name line; the first line is the pet name. Colours: BLUE = (0.5, 0.7, 1.0), RED = (1, 0.2, 0.1), DARKRED = white text on a (160, 0, 0) background (`ZzzInventory.cpp:404-445`).

Lines in order:
1. Only in the NPC shop: "Selling Price: %s" [63] in white bold. Price = (level × 1,000,000 for the raven, × 2,000,000 for the horse) / 3, rounded down to 100s (:572-593, 633-642). In a personal shop, the price colour depends on size, plus "You are short of Zen." [423] / "Right click for price setting" [1101].
2. "Dark Raven" [1214] or "Dark horse" [1187], **blue bold**.
3. Two blank half-lines.
4. "Exp : %u/%u" [201], white.
5. "Level : %d" ([368] + " : %d"), white.
6. Raven only:
   - "Dmg(rate): %d~%d (%d)" [203]. These are the raw pet values, without the sceptre bonus.
   - "Attack speed: %d" [64].
7. "Life: %d" [70].
8. Requirement line:
   - Horse: "Minimum Level Requirement: %d" [76] = 218 + Lvl×2.
   - Raven: "Charisma Requirement: %d" [698] = 185 + Lvl×15.
   - White if met; otherwise red plus a red "(lacking %d)" [74].
9. A blank line, then "Can be equipped by %s" [61] with "Dark Lord" [24]: white for a Dark Lord, DARKRED otherwise.
10. Each special option in blue. For the horse these are "Earth shake skill (mana:%d)" [1189] and "Increase defensive skill +%d" [959] (5 + Dex/20 + Lvl×2).
11. Horse only, blue: "Absorb %d%% additional damage" [744] = (30 + Lvl)/2, and "Increase %d possible attack distance" [1188] with 2.

- The raven can only be equipped if Cmd ≥ 185 + Lvl×15 (`ZzzInfomation.cpp:2274-2281`, `NewUIMyInventory.cpp:332`).

**Spirit (13/31)** uses the normal tooltip:
- Name (`ZzzInventory.cpp:2513-2519`): "Spirit of Dark horse" (level 0) / "Spirit of Dark Raven" (level 1), built as `"%ls of %ls"` from the Item.bmd name. The mix-list name is "Dark horse Spirit" / "Dark Raven Spirit" (:1736-1742).
- Description in white (:4375-4382): "Used in Dark Horse resurrection" [1215] / "Used in Dark Raven resurrection" [1216].
- Model: `DarkHorseSoul.bmd`. Level 0 draws mesh0 BRIGHT; level 1 draws it BRIGHT tinted (0.3, 0.8, 1.0) (`ZzzObject.cpp:9760-9775`).
- The item names in this `Item.bmd` are Japanese (`ダークホースの蹄`, `ダークスピリットの爪`, `魂`). The English "Dark Horse Horn" / "Dark Raven" / "Spirit" are not in this copy.

---

## 6. Pet Trainer NPC (monster 226)
- **Model:** `Data/NPC/Breeder.bmd`, textures from `NPC\` (`ZzzOpenData.cpp:1940-1942`). It is created at `ZzzCharacter.cpp:14296-14299` with the hard-coded ID `"조련사 NPC"` ("Trainer NPC"), and no special scale, effect or sound.
  - BMD: 6 meshes, 36 bones, 2 actions (9 and 7 keys).
  - Textures: `birdnpc.JPG`, `dksbody.tga`, `dkswing.jpg`, `foot.JPG`, `bnpc02.tga`, `bnpc01.tga`. This is the man with a raven on his arm; meshes 1–3 reuse the raven's textures.
  - Behaviour is generic NPC: idle actions and a random head look about every 32 ticks (`ZzzCharacter.cpp:5620-5627`).
- **Placement comes from the server.** OpenMU S6: Lorencia (122, 110) facing **SouthEast** (`openmu/.../Maps/Lorencia.cs:36`), and Crywolf (135, 47) facing SouthWest.
- **Click:**
  - The client sends Talk.
  - For 226 it also sends pet info requests for an equipped horse (helper slot) and raven (left hand) (`ZzzInterface.cpp:1675-1684`).
  - The server answers Talk with Value **7** → `CTrainerMenuMsgBox` (`WSclient.cpp:6359-6361`).

**Menu box** (`NewUICustomMessageBox.cpp:5931-6119`):
- Size 230 × (67 + 7×15 + 50) = 230×222, at y = 60, centred.
- Text: bold "Trainer" [1227] centred at +10. Then, at 18 px line spacing from y+25: "Hi" [1202] and "%s what is your command?" [1203] with the hero name.
- Buttons, 128×29 (`MSGBOX_BTN_EMPTY` + 20):
  - At y+85: "Restore life (durability)" [1204] → opens the Recover box.
  - At y+120: "Resurrect spirit" [1205] → mix window with `MIXTYPE_TRAINER`.
  - Small "Close" [1002] (64×29) at the bottom → sends Close NPC.
  - All play `SOUND_CLICK01`.

**Recover box** (:6121-6318), same frame:
- Text: "Trainer"; then "Select the pet to recover life" [1228].
- Buttons: "Dark horse" [1187] at y+65 and "Dark Raven" [1214] at y+115.
- Under each button, in colour (206, 192, 146): a cost line from `npcBreeder::CalcRecoveryZen` (`npcBreeder.cpp:24-96`). It shows one of:
  - "Pet is not equipped." [1229]
  - "Life has been recovered" [1230] (cost 0 or full)
  - "%s zen is lacking to recover life." [1231] (the shortfall)
  - "%s zen is required to recover life." [1232]
- Pressing a pet button: `C1 34 [slot][(BYTE)gold]` (repair; the second byte is the truncated zen value, used as the self-repair flag), then Close NPC (`npcBreeder.cpp:98-117`).
- Reply `ReceiveRepair` plays `SOUND_REPAIR` (`WSclient.cpp:6787-6799`).

**Recovery cost** (use 1.03.35, `M103/ZzzInventory.cpp:1452-1560`):
- V = min(level × 1,000,000 for the raven / 2,000,000 for the horse, 400,000,000).
- cost = 3·√V·V^¼·(1 − dur/255) + 1, then ×2 if durability is 0.
- At the trainer, the cost is used as is. Round down to 100s if ≥ 1000, else to 10s if ≥ 100.
- **MuMain deviates:** it uses 3.5 instead of 3, and at the trainer, with neither the shop nor the inventory open, it returns 0, so the box always says "Life has been recovered" (`MM/Engine/Object/ZzzInventory.cpp:1222-1294`). It also has a `memcpy` that copies only half of a wide string (`npcBreeder.cpp:89`).

**Resurrect mix window** (`MIXTYPE_TRAINER`, `NewUIMixInventory.cpp`, `MixMgr.cpp`):
- Title: "Resurrect spirit" [1205] (:365-367).
- "Tax rate: %d%% (changed in real-time)" [1623] (:412-418).
- Success text: "Resurrection Success rate: %d%%" [584] with [1212] (:478-480).
- Zen: "Required zen: %s(%s)" [1622], tax-included first, then base (:500-521).
- "Assembly prediction: %s" [2334].
- Source lines, coloured red (missing), (210, 230, 255) (partial) or (255, 255, 48) (ok) (:528-560).
- Mix button tooltip "Resurrection" [1212] (:619-620).
- When the items don't fit: "Item inappropriate for Resurrection" [1213] (`MixMgr.cpp:549-551`).
- Closing sends Close NPC (:159-161).
- Failure: "Resurrection failed." [1208], with `SOUND_MIX01` + `SOUND_BREAK01`, via the 0x31 subcode 5 reply (`WSclient.cpp:1695-1701`). "Resurrection successful." [1209] is commented out.

**Recipes**, decoded from `DATA/Local/mix.bmd` (type 4 = TRAINER; BuxConvert, 656-byte records):

| Displayed name | Id | Sources | Zen (type A) | Rate | Talisman of Luck |
|---|---|---|---|---|---|
| "Dark Raven Resurrection" [1214]+[1212] | 14 | Spirit 13/31 **+1** ×1, Jewel of Bless 14/13 ×2, Jewel of Soul 14/14 ×2, Jewel of Creation 14/22 ×1, Jewel of Chaos 12/15 ×1 | 1,000,000 | 60% (max-rate token) | allowed ('A'); chaos charm not allowed ('U') |
| "Dark horse Resurrection" [1187]+[1212] | 13 | Spirit 13/31 **+0** ×1, Bless ×5, Soul ×5, Creation ×1, Chaos ×1 | 5,000,000 | 60% | same |

- The rate is capped at the recipe's 60, then charm bonus is added, capped at 100 (`MixMgr.cpp:836-851`).
- Source lines read like "Dark Raven Spirit 1Quantity" (`GetSourceName`, `MixMgr.cpp:648-745`).

---

## 7. Sounds
- **Raven:** `DSpirit_Missile.wav` (magic attack), `DSpirit_Rush.wav` (rush), `DSpirit_Shout.wav` (random cry). See §1.
- **Horse:** `pHorseStep1-3.wav` while running.
- **Trainer UI:** `SOUND_CLICK01` on every button. Repair uses `SOUND_REPAIR`; a resurrect result uses `SOUND_MIX01` / `SOUND_BREAK01`.
- The trainer NPC itself has no sound.

---

## 8. Differences, 1.03.35 vs MuMain
- **Repair cost:** coefficient 3.0 vs 3.5; MuMain returns 0 at the trainer (see §6). 1.03.35 is the correct one.
- **Raven level on screen:** 1.03.35 only sets the level when `PBG_FIX_DARKPET_RENDER` is on (it is defined: `M103/Defined_Global.h:892`). The result is the same as MuMain, including the "local hero's level" quirk.
- **Shout timing:** written differently (`MoveSceneFrame%60`), same rarity.
- **Everything else:** the per-tick constants, formulas and text ids are identical.

---

## 9. Asset files in `DATA` (bytes)

| File | Bytes |
|---|---|
| Skill/darkspirit.bmd | 131883 |
| Skill/dkthreebody.OZJ | 18708 |
| Skill/dkthreebody_R.OZJ | 5576 |
| Skill/dkswing.OZJ | 13490 |
| Skill/dkonebody.OZJ | 14709 |
| Skill/dksbody.OZT | 65584 |
| Skill/foot.OZJ | 10178 |
| Skill/dkshelmet.OZJ | 16360 |
| Skill/DarkLordSkill.bmd | 394 |
| Skill/KingS_R.OZJ | 21230 |
| Skill/airforce.bmd | 5618 |
| Skill/SpiE_R.OZJ | 6239 |
| Skill/DarkHorse.bmd (ridden) | 330601 |
| Skill/dkhead2.OZJ | 9931 |
| Skill/dkhead2_gold.OZJ | 9931 |
| Skill/dkheadwing.OZJ | 9991 |
| Item/spiritbill.bmd | 9970 |
| Item/DarkHorseHorn.bmd | 3172 |
| Item/DarkHorseSoul.bmd | 4146 |
| Item/change.OZJ | 8711 |
| NPC/Breeder.bmd | 127216 |
| NPC/birdnpc.OZJ | 40228 |
| NPC/dksbody.OZT | 65584 |
| NPC/dkswing.OZJ | 12144 |
| NPC/foot.OZJ | 9815 |
| NPC/bnpc01.OZT | 32816 |
| NPC/bnpc02.OZT | 4144 |
| NPC/dkshelmet.OZJ | 10301 |
| Sound/DSpirit_Missile.wav | 80040 |
| Sound/DSpirit_Rush.wav | 51450 |
| Sound/DSpirit_Shout.wav | 110458 |
| Sound/pHorseStep1.wav | 23502 |
| Sound/pHorseStep2.wav | 27526 |
| Sound/pHorseStep3.wav | 30962 |
| Interface/newui_command.OZJ | 24139 |
| Interface/newui_skillbox.OZJ | 14434 |
| Interface/Newui_Pet_Lifebar01.OZJ | 14601 |
| Interface/Newui_Pet_Lifebar02.OZJ | 14391 |
| Interface/newui_Pet_Back.OZT | 5292 |
| Interface/newui_Pet_HpBar.OZJ | 13406 |
| Interface/newui_chainfo_btn_pet.OZT | 8400 |
| Interface/newui_guild_tab04.OZT | 9904 |
| Interface/newui_msgbox_back.OZJ | 113250 |
| Local/mix.bmd | 65000 |

- The FX bitmaps `Effect/flare01.jpg`, `Spark03.jpg`, `JointSpirit01.jpg` and `Flare.jpg` are shared engine sprites (`ZzzOpenData.cpp:5195-5297`).
- The helper scripts used here (`bmd.py`, `mix.py`, `i18n.py`) are in the same scratchpad folder.
