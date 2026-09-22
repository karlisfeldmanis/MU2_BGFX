<!--
Read against MuMain's own source on 2026-09-22, for the tooltip the user chose that day (the
card in game/ui/tip.h). It is here so that a line this game has no rules for yet -- luck, the
excellent options, a set block -- has a known shape and a known colour when its rules arrive,
and so that nobody has to read RenderItemInfo twice.

Paths below are in LEGACY/reference/, which is not part of this project's own tree.
-->

# MU Online item tooltip — complete line catalogue (from MuMain C++ + OpenMU)

Sources
- `MuMain` = `/Users/karlisfeldmanis/Documents/muremaster2/LEGACY/reference/MuMain/src/source`
  (this tree is a Season 6 / MuDream-era client; it renders *every* line type MU ever shipped).
- Strings: the old `GlobalText[n]` table now lives in
  `/Users/karlisfeldmanis/Documents/muremaster2/LEGACY/reference/MuMain/src/Localization/Game.en.resx`
  (`<comment>legacy_id=N</comment>` carries the old index; the C++ uses `I18N::Game::<PascalCaseSlug>`).
  Every quoted string below is the literal English `<value>` from that file; `legacy_id` is given as `GT n`.
  `%%` in the resx is a literal `%` after formatting.
- Item names themselves come from `bin/Data/Local/Eng/item_eng.bmd` (`ITEM_ATTRIBUTE::Name`).
- 0.75 evidence: `/Users/karlisfeldmanis/Documents/muremaster2/LEGACY/reference/openmu/src/Persistence/Initialization/Version075/…`

Main entry points
- `RenderItemInfo(sx, sy, ITEM*, bool Sell, int Inventype, bool bItemTextListBoxUse)` — ZzzInventory.cpp:2113 (the tooltip).
- `RenderRepairInfo(...)` — ZzzInventory.cpp:5665 (the repair-mode tooltip).
- `giPetManager::RenderPetItemInfo(...)` — GameLogic/Pets/GIPetManager.cpp:595 (Dark Horse / Dark Raven; a *replacement* tooltip, ZzzInventory.cpp:2160 returns early).
- Callers / anchor point: UI/NewUI/Inventory/NewUIInventoryCtrl.cpp:1509-1543, UI/NewUI/Inventory/NewUIMyInventory.cpp:1637-1655, UI/Legacy/UIControls.cpp:6063 (chat/trade "item text list box", bottom-centred variant).
- Shared line buffer: `wchar_t TextList[50][100]; int TextListColor[50]; int TextBold[50];` — ZzzInventory.cpp:201-204.
  **Hard limits: 50 lines, 100 wide chars per line** (pet tooltip re-states this: GIPetManager.cpp:45-47).

---

## 1. Frame & layout rules

Renderer: `RenderTipTextList(sx, sy, TextNum, Tab, iSort, iRenderPoint, bUseBG)` — ZzzInventory.cpp:305-473.

| Rule | Value | Where |
|---|---|---|
| Background | black, `glColor4f(0,0,0,0.8)` filled rect | :379-380 |
| Border | 1 px solid black (`alpha 1.0`), drawn on all four sides 1 px outside the fill | :373-377 |
| Width | `max(text width of all lines)` (in 640×480 virtual units) `+ 4` px padding; if `Tab>0` the width is forced to `Tab/rate*2` (used by the Item-Info window and the set-option popup) | :329-348 |
| Horizontal placement | tooltip is **centred on `sx`**: `x = sx - width/2`, then clamped to `[0, screen-width-width-1]` | :349-355 |
| Vertical placement | default `y = sy`; with `STRP_BOTTOMCENTER` the box is placed *above* the point (`y = sy - height`) — used by the chat/trade item link box (ZzzInventory.cpp:5659) | :359-369 |
| Anchor from the inventory | `sx` = centre of the item's cells, `sy` = top of the item's cells (+½ cell if the item is 1 row high), then `sy += INVENTORY_SCALE (20)`, and if the box would pass `y=420` it is shifted up | NewUIInventoryCtrl.cpp:1513-1520; ZzzInventory.cpp:5640-5648 (`iScreenHeight = 420`) |
| Line advance | `lineHeight = fontHeight * 1.1` | :468 |
| Blank line | a line whose text is `"\n"` renders nothing and advances **half** a line; a line that is a single space advances a full line | :396-400, :332-342 |
| Text alignment | default `RT3_SORT_CENTER` — **every line is centre-aligned** inside the box (`RenderText(..., fWidth-2, 0, iSort)`) | :465; UIControls.cpp:2874-2886 |
| Font | Tahoma (config-overridable), size `FontHeight-1` where `FontHeight = ceil(12 + (WindowHeight-480)/200)` → 12 px @480p, ~14 px @1080p | App/Platform/Windows/Winmain.cpp:1313-1330, 1387-1392 |
| Bold | per line via `TextBold[i]` → `g_hFontBold` (FW_SEMIBOLD, same size). Bold lines: the item name, shop price, the 380/harmony option lines, the staff "Wizardry Dmg % rise", boots/gloves speed lines, pet-attack line, set names | :318-325, :386-393 |
| Colour application | the glyph bitmap is white and is modulated by `glColor3f` — so the `glColor3f` triples below are the on-screen RGB | UIControls.cpp:2729 + RenderTipTextList:404-442 |
| Highlight bands | `TEXT_COLOR_DARKRED/DARKBLUE/DARKYELLOW/GREEN_BLUE` set a **background colour band the full width of the tooltip behind that one line** (`SetBgColor` → filled rect of the text box) | :443-459; UIControls.cpp:2925-2933 |

Text colours (`Core/Globals/_define.h:238-251`, RGB from ZzzInventory.cpp:404-442):

| Name | id | RGB (0-255) | Band behind the line |
|---|---|---|---|
| `TEXT_COLOR_WHITE` | 0 | 255,255,255 | – |
| `TEXT_COLOR_BLUE` | 1 | 128,179,255 (0.5,0.7,1.0) | – |
| `TEXT_COLOR_RED` | 2 | 255,51,26 (1.0,0.2,0.1) | – |
| `TEXT_COLOR_YELLOW` | 3 | 255,204,26 (1.0,0.8,0.1) | – |
| `TEXT_COLOR_GREEN` | 4 | 26,255,128 (0.1,1.0,0.5) | – |
| `TEXT_COLOR_DARKRED` | 5 | white text | 160,0,0 |
| `TEXT_COLOR_PURPLE` | 6 | 255,26,255 (1.0,0.1,1.0) | – |
| `TEXT_COLOR_DARKBLUE` | 7 | white text | 0,0,160 |
| `TEXT_COLOR_DARKYELLOW` | 8 | white text | 160,102,0 |
| `TEXT_COLOR_GREEN_BLUE` | 9 | text forced to 0,255,0 | 60,60,200 |
| `TEXT_COLOR_GRAY` | 10 | 102,102,102 (0.4) | – |
| `TEXT_COLOR_REDPURPLE` | 11 | 204,128,204 (0.8,0.5,0.8) | – |
| `TEXT_COLOR_VIOLET` | 12 | 179,102,255 (0.7,0.4,1.0) | – |
| `TEXT_COLOR_ORANGE` | 13 | 230,107,10 (0.9,0.42,0.04) | – |

Designer notes: the tooltip is a flat centred stack of single-line strings — no columns, no icons, no wrapping (multi-sentence lore is hand-split into several lines in the source, e.g. ZzzInventory.cpp:3659-3667). The only "structure" is blank half-lines and the coloured full-width bands. Sections are separated by a `"\n"` line.

---

## 2. Ordered master list of line types

Order below is the emission order in `RenderItemInfo`. `ZI:n` = `Engine/Object/ZzzInventory.cpp:n`.

### 2.0 Economy header (printed *before* the name)
| # | Line | Colour | When | Version | Where |
|---|---|---|---|---|---|
| 0a | blank | – | always (first line of every tooltip) | all | ZI:2164 |
| 0b | `Purchasing price: 1,250,000(1,200,000)` — `"Purchasing price: %s(%s)"` (tax-included, base) | item-name colour, then blank line | NPC shop window open **and** this is the shop's stock (`Sell=true`), item not sell-banned | GT 1620 → S1+ (tax); plain `"Purchase Price: %s"` GT 62 is the 0.75-era form | ZI:2250-2262 |
| 0c | `Selling Price: 350,000` — `"Selling Price: %s"` GT 63 | item-name colour | NPC shop open, own inventory item (`Sell=false`) | 0.75 | ZI:2265-2266 |
| 0d | `Selling Price: 3,000,000` (personal shop) | **bold**; red ≥10,000,000, yellow ≥1,000,000, green ≥100,000, else white | personal-shop tooltips | S1+ (personal shop) | ZI:2283-2297 |
| 0e | `You are short of Zen.` GT 423 | RED bold | buying in a personal shop with too little Zen | S1+ | ZI:2306 |
| 0f | `Right click for price setting` GT 1101 | RED bold | own personal-shop item with no price set | S1+ | ZI:2315 |

### 2.1 Identity
| # | Line | Colour | When | Version | Where |
|---|---|---|---|---|---|
| 1 | **Item name**, bold. Forms: `Name`; `Name +9`; `Excellent Name +9`; `<SetName> Name +11` (ancient); jewels print `Jewel of Bless +3` (stack count) ; special-case names (Fruits `ENG Fruit`, Transformation Ring `<Monster> Transformation Ring`, Dark Horse `Spirit of Dark horse`, …) | see §3 | always | name+level: 0.75; `Excellent ` prefix: 0.95d+; set-name prefix: ancient era (S1+) | ZI:2322-2673 (generic branch ZI:2645-2671; `GetSetItemName` GameLogic/Items/CSItemOption.cpp:216) |
| 2 | blank | – | always | all | ZI:2674 |

### 2.2 Item-kind preamble (quest / consumable / event blurb) — one of ~90 mutually exclusive branches, ZI:2676-3935
Representative kinds (all one-liners, mostly BLUE or WHITE, some with a trailing blank):
- `Quest Item` GT 730 (WHITE) + `Cannot store in vault.` GT 731 (RED) + `Cannot be traded.` GT 732 (RED) — quest items, ZI:2753-2764, 3491-3502.
- `Enables entrance into Devil Square.` GT 2259 + `You will be assigned to a stage according to your level.` GT 2270 (BLUE) — event tickets, ZI:2786-2797.
- `Number of items: 5` GT 69 (BLUE) — stacked consumables, ZI:2909.
- `Restores HP by 100%% immediately.` GT 2500 / `Restores Mana by 100%% immediately.` GT 2501 — ZI:2916.
- `Increases Attack Speed by 7` GT 2503 … `Increases Mana by %d` GT 2508 + `You may continue to use the strengthener power.` GT 2502 — buff potions, ZI:2925-2933.
- `Reset point: 120` GT 2511 + `Resets the status.` GT 2510 + blank + `It can be used with item removed.` GT 1908 (DARKRED band) — reset items, ZI:2947-2961.
- `Increases experience gained.` GT 2256 / `…and item drop rate.` GT 2257 / `Prevents experiences to be gained.` GT 2258 + `Warp Command Window available.` GT 2297 (BLUE) + `Not applicable to` GT 2567 / `Master Level Characters.` GT 2568 (YELLOW) — seals, ZI:2856-2905.
- Pet blurbs: `Increases Attack Power and Wizardry by 40%%` GT 2575 (Demon), `Alleviates monster's damage by 30%%` GT 2577 + `Increases Maximum Life by 50` GT 2578 (Spirit of Guardian), `Surrounding Zens are automatically collected.` GT 2600 (Rudolph/Skeleton), `Auto-collects zen around you.` GT 2746 + `EXP rate 50%% increase` GT 2747 + `Increase Defensive Skill +50` GT 2748 (Panda), `Zen increase 50%%` GT 2744 (Unicorn) — ZI:3122-3202.
- `Cannot Repair` GT 926 (RED) — Moonstone Pendant, Wizard's Ring, some cash items — ZI:3205, 3531, 3542, 3591.
- `Can be dropped after level 40` GT 924 (WHITE) + `Cannot store in vault.` + `Cannot be traded.` + `Cannot be sold.` GT 733 (all RED) — bound items, ZI:3551-3566.
- Lore blocks (WHITE, hand-split lines), e.g. `It's a sign infused with traces of dimensions.` GT 2773 / `Collect five and the signs will automatically` GT 2774 / `transform into a Mirror of Dimensions.` GT 2775, then blank, `3 / 5` GT 1181, `You need 2 more to create a Mirror of Dimensions.` GT 2776 (YELLOW) — ZI:3659-3679; Secromicon lore ZI:3692-3747.
- Wing-charm luck lines: `Increases your luck to create wing of your wish.` GT 2717 + per-wing `Increases your luck to create Wings of Satan.` GT 2718… + `Talisman of Wings of Satan` GT 2732+ — ZI:3602-3655.
- `7 Days until Expiration` GT 3127 (YELLOW bold) — ZI:3912.
- `usable 3times` GT 2260 — ZI:3901, 4688, 4767.
Version: almost all of this block is S3-S6 cash/event content (GT ≥ 2200). The 0.75-relevant members are the plain `Number of items: %d`, the jewel/quest descriptions in §2.5, and `Cannot be sold/traded`.

### 2.3 Core stats (ZI:3952-4085) — the heart of the tooltip
| # | Line | Colour | When | Version | Where |
|---|---|---|---|---|---|
| 3 | `One-Handed Damage: 20 ~ 28` GT 40 / `Two-Handed Damage: 60 ~ 72` GT 41 / `Wizardry Damage: 40 ~ 45` GT 42 (index `40 + p->TwoHand`; skill books & scrolls always use Wizardry) | YELLOW if a Jewel-of-Harmony strengthen bonus is included; else BLUE if the item is excellent; else WHITE | `ip->DamageMin != 0` | 0.75 | ZI:3972-4019 |
| 4 | `Defense: 34` GT 65 | YELLOW with harmony bonus, BLUE if excellent armour, else WHITE | `ip->Defense` | 0.75 | ZI:4040-4050 |
| 5 | `Spell resistance: 12` GT 66 | WHITE | `ip->MagicDefense` | 0.75 (unsure whether displayed pre-S2) | ZI:4057 |
| 6 | `Defense rate: 22` GT 67 | BLUE if excellent, else WHITE | shields etc. (`p->SuccessfulBlocking`) | 0.75 | ZI:4064 |
| 7 | `Attack speed: 35` GT 64 | WHITE | `p->WeaponSpeed` | 0.75 | ZI:4074 |
| 8 | `Moving speed: 10` GT 68 | WHITE | `p->WalkSpeed` (mounts) | S1+ | ZI:4081 |
| 9 | `%s Resistance: 3` GT 72 with element name (`Ice` GT 48, `Poison` 49, `Lightning` 50, `Fire` 51, `Earth` 52, `Wind` 53, `Water` 54); value = `Level+1` | WHITE | rings/pendants with `p->Resistance[i]` | 0.75 (Ice/Poison/Lightning/Fire); Earth/Wind/Water later | ZI:4807-4815 |
| 10 | `Durability: [51/66]` GT 71 | WHITE | equipment with durability, non-period | 0.75 | ZI:4756 (max via `CalcMaxDurability` ZI:1572-1649: +1/+2/+3/+4/+5/+6/+7/+8 per level band, +20 ancient, +15 excellent, 255 lucky) |
| 10b | `Durability: [3]` GT 95 (single value) — transformation rings; `Life: 120` GT 70 — pets & mounts; `Number of items: 12` GT 69 — potions/arrows/bolts; `3 / 20` GT 1181 — collectibles | WHITE | see branch list | GT 69/70/71 are 0.75-era; GT 95/1181 later | ZI:4600-4776 |
| 11 | Arrows/bolts +N: `Increase 3%% of Damage` GT 577 + `Additional Dmg +1` GT 88 | BLUE | `ITEM_BOLT/ARROWS` with level | S1+ | ZI:4790-4804 |

### 2.4 Requirements (ZI:4818-5006) — always in this order
| # | Line | Colour | When | Version | Where |
|---|---|---|---|---|---|
| 12 | `Minimum Level Requirement: 74` GT 76 | WHITE if met; **RED if not, immediately followed by a second RED line** `(lacking 12)` GT 74 | `ip->RequireLevel` | 0.75 | ZI:4820-4836 |
| 13 | `Strength Requirement: 180` GT 73 (+ RED `(lacking N)`) | YELLOW when a harmony/socket requirement reduction applies, else WHITE / RED | `ip->RequireStrength` | 0.75 | ZI:4873-4902 |
| 14 | `Agility Requirement: 120` GT 75 (+ RED `(lacking N)`) | as above | `ip->RequireDexterity` | 0.75 | ZI:4904-4932 |
| 15 | `Stamina Requirement: 90` GT 1930 (+ `(lacking N)`) | WHITE / RED | `ip->RequireVitality` | S3+ (unsure) | ZI:4935-4956 |
| 16 | `Energy Requirement: 200` GT 77 (+ `(lacking N)`) | WHITE / RED | `ip->RequireEnergy` | 0.75 | ZI:4959-4981 |
| 17 | `Charisma Requirement: 300` GT 698 (+ `(lacking N)`) | WHITE / RED | `ip->RequireCharisma` (Dark Lord) | 0.97d+ | ZI:4984-5005 |

"Requirement unmet" is therefore **two lines**: the requirement itself turns RED and a `(lacking N)` RED line is inserted under it. Lucky items skip all stat requirements (`bRequireStat=false`, ZI:4842).

### 2.5 Class restrictions (`RequireClass`, ZI:627-802, called from ZI:5008-5011)
- Emits a leading blank line, then one line per allowed class: `Can be equipped by %s` GT 61, e.g. `Can be equipped by Blade Knight`.
- Class names by required step: Dark Wizard/Soul Master/Grand Master, Dark Knight/Blade Knight/Blade Master, Elf/Muse Elf/High Elf, Magic Gladiator/Duel Master, Dark Lord/Lord Emperor, Summoner/Bloody Summoner/Dimension Master, Rage Fighter/Fist Master.
- Colour: WHITE when the viewing character's class *and* class-step qualify; otherwise `TEXT_COLOR_DARKRED` → **white text on a dark-red band** (ZI:658-665).
- Skipped entirely when all classes are allowed (ZI:648-649) and for a long list of consumables (`IsRequireClassRenderItem`, ZI:557-625).
- Version: 0.75 (three classes then).

### 2.6 Level-5+ / weapon intrinsic bonuses (ZI:5013-5056) — each preceded by a blank line, **bold BLUE**
| # | Line | When | Version | Where |
|---|---|---|---|---|
| 18 | `Increases moving speed` GT 78 | boots +5 or higher | 0.75 | ZI:5018 |
| 19 | `Swimming speed increase` GT 93 | gloves +5 or higher | 0.75 | ZI:5029 |
| 20 | `Wizardry Dmg 53%% rise` GT 79 (books use `Curse Spell increment %d%%` GT 1691) | staves / rune-type blades (`ip->MagicPower`) | 0.75 (staff); curse variant S3+ | ZI:5041-5048 |
| 21 | `Increase pet attack as 40%%` GT 1234 | scepters (Dark Lord) | 0.97d+ | ZI:5054 |

### 2.7 Options block
A blank line is inserted before the options when the item has any (`ip->SpecialNum>0`, ZI:5058-5067). Then, in order:

**(a) 380 / "additional" JoH-era option** — ZI:5070-5088, text from `GameLogic/Items/ItemAddOptioninfo.cpp:57-101`, `TEXT_COLOR_REDPURPLE`, **bold**, wrapped in blank lines:
`Attack sucess rate increase +20` GT 2184, `Additional Damage +20` GT 2185, `Defense success rate increase +20` GT 2186, `Defensive skill +20` GT 2187, `Max. HP increase +100` GT 2188, `Max. SD increase +100` GT 2189, `SD auto recovery` GT 2190, `SD recovery rate increase +10%%` GT 2191. (Season 3+.)

**(b) Jewel of Harmony option** — ZI:5099-5151. One bold line `"<HarmonyOptionName> +12"` (or `+12%` for defense option 7), name from `Data/Local/Eng/JewelOfHarmonyOption_eng.bmd`; **YELLOW when the item's level is high enough to activate it, GRAY when not** (ZI:5122-5123). Error fallback: `Reinforcement option error : 2 7 3` GT 2204 + `Send screenshots with the report.` GT 2205, DARKRED band. (Season 3+.)

**(c) Ancient bonus stat** — `CSItemOption::RenderDefaultOptionText`, CSItemOption.cpp:647-663, BLUE:
`Increase strength +5` GT 950 / `Increase agility +5` 951 / `Increase energy +5` 952 / `Increase stamina +5` 953 (value = `AncientBonusOption * 5`), plus `Increase attribute damage` GT 1165 for elemental rings/pendants. (Ancient era, S1+.)

**(d) `Special[]` loop** — ZI:5156-5200, every line `TEXT_COLOR_BLUE`, text built by `GetSpecialOptionText` (ZI:1889-2111). Emission order inside the array is fixed by `SetItemAttributes` (Engine/Object/ZzzInfomation.cpp:1101-1455): wing-excellent options → **skill** → **luck** → **additional option (+4/+8/+12…)** → **excellent options**.

| Group | Lines | Version |
|---|---|---|
| Skill | `Defend skill (Mana:30)` GT 80, `Falling Slash skill (Mana:…)` 81, `Lunge skill` 82, `Uppercut skill` 83, `Cyclone Cutting skill` 84, `Slashing skill` 85, `Triple Shot skill` 86, `Power Slash Skill` 98, `Raid Skill` 745 (+ DARKRED note `Knight specific skill` GT 179), `Long spear skill` 1210, `Force wave skill` 1186, `Earth shake skill` 1189 (+ DARKRED `Dark Lord exclusive skill` GT 1201), `Plasma storm skill` 1928, `Killing Blow` 3153, `Beast Uppercut` 3154 | GT 80-86: 0.75; the rest per class era |
| Luck | **two lines**: `Luck (success rate of Jewel of Soul +25%%)` GT 87 and `Luck (critical damage rate +5%%)` GT 94 | 0.75 |
| Additional option (+4 per level) | `Additional Dmg +12` GT 88 (weapons), `Additional Wizardry Dmg +12` GT 89 (staves), `Additional Curse Spell +12` GT 1697 (books), `Additional defense rate +15` GT 90 (shields), `Additional defense +12` GT 91 (armour), `Automatic HP recovery 3%%` GT 92 (jewelry/wings), `Max mana increased by 3%%` GT 1087 (Ring of Magic), `Max AG increased by 3%%` GT 1088 (Pendant of Ability) | 0.75 (the first six); ring/pendant variants 0.97d+ |
| Excellent — armour/shield/rings | `Increase Max HP +4%%` GT 622, `Increase Max Mana +4%%` 623, `Damage Decrease +4%%` 624, `Reflect Damage +5%%` 625, `Defense success rate +10%%` 626, `Increases acquisition rate of Zen after hunting monsters +30%%` 627 | 0.95d+ |
| Excellent — weapons/staves/pendants | `Excellent Damage rate +10%%` GT 628, `Increase Damage +level/20` 629, `Increase Damage +2%%` 630, `Increase Wizardry Dmg +level/20` 631, `Increase Wizardry Dmg +2%%` 632, `Increase Attacking(Wizardry)speed +7` 633, `Increases acquisition rate of Life after hunting monsters +life/8` 634, `…Mana after hunting monsters +Mana/8` 635 | 0.95d+ |
| Excellent — 2nd/3rd-level wings & capes | `HP +75 increased` GT 740, `Mana +75 increased` 741, `Ignor opponent's defensive power by 3%%` 742, `Max AG +50 increased` 743, `Absorb 5%% additional damage` 744, `Increase command +35` 954, `Return's the enemy's attack power in 5%%` 1673, `Complete recovery of life in 5%% rate` 1674, `Complete recover of Mana in 5%% rate` 1675 | S1+ / S3+ |
| Misc option texts reachable from the same table | `Increase %d%% of Damage` GT 577, `Parrying 10%% increased` 746, `Increase defensive skill +%d` 959, `Increase strength/agility/stamina/energy +%d` 950-953, `Increase critical damage +%d` 965, `Increase excellent damage +%d` 967 | varies |
| Rune-blade extra | on Rune/Dark Reign/Explosion/Sword Dancer/Imperial blades an extra `Additional Wizardry Dmg +%d` GT 89 line mirrors the damage option | S6 |

Then a blank line (ZI:5202).

### 2.8 Wing / cape / mount intrinsic option block (printed with the core stats, ZI:4270-4374)
WHITE lines, in this order: `Increase 24%% of Damage` GT 577, `Absorb 24%% of Damage` GT 578, `Increase speed` GT 579. Values scale with wing tier and level (`12+L*2`, `32+L`, `39+L*2`, …). Dinorant: `Increase 15%% of Damage` + `Absorb 10%% of Damage`. Fenrir (ZI:5222-5284): skill line, then `Increase final damage 10%%` GT 1860 / `Absorb final damage 10%%` GT 1861 / `Added %d of Life` 1867 / `…of Mana` 1868 / `Added %d Attack` 1869 / `Added %d Wizardry` 1870, `Golden Fenrir` 1871 + `Exclusive edition only given to MU Heroes` 1872 (GREEN), blank, `Can summon the Fenrir when equipped.` 1920 (YELLOW), `Skills will improve through upgrading.` 1929.
Version: 1st-level wings (`Wings of Elf/Heaven/Satan`) exist in 0.75 (OpenMU `Version075/Items/Wings.cs:44-46`) and already print damage/absorb/speed; the higher tiers are S1-S6.

### 2.9 Description / lore, jewels, consumables (ZI:4218-4569) — WHITE unless noted
`It is used to increase your item level up to 6` GT 572 (Bless), `…up to 7,8,9` GT 573 (Soul), `It is used to combine Chaos items` GT 574 (Chaos), `Increases item option by 1 level` GT 621 (Life), `Used to create fruits that increase stats` GT 619 (Creation), `Create and improve items for siege` GT 1289 (Guardian), `Jewel with impurities` GT 2208 (Gemstone), `Jewel for item reinforcement` GT 2209 (Harmony), `Grant actual power to reinforced item.` GT 2210 (refine stones), `Used to upgrade wings` GT 748 (Loch's feather), `Used when creating a Cloak of Invisibility` GT 816, `Used when entering Blood Castle` GT 814, `It is used to combine items for a Devil Square Invitation` GT 637, `Warp to the corresponding area after 3 seconds` GT 157 (town-portal scroll levels 1-8), `Throw it and you may receive some Zen or items` GT 571 (boxes), `Increases 1~3 stat points` GT 636 + `Possible to decrease stat 1~9 point` GT 1910 + DARKRED `It can be used with item removed.` GT 1908 (fruits), `3 Jewel of Bless is combined` GT 1819 + `Can be used after dismantling` GT 1820 (jewel stacks), Chaos-Castle/Kalima level tables (ZI:4510-4544, World/GameMaps/GMHellas.cpp:204-262; the matching row for your level gets the DARKYELLOW band; `Right click to enter.` GT 1157 in DARKBLUE).
Version: the jewel lines (572/573/574/636/637/814/816) are 0.75-era; the rest follow their content.

Built, 2026-09-22. Four of these are rows this game has — GT 572, 573 and 574 on the three
jewels, GT 157 on the Town Portal Scroll — and all four are now printed, MU's English carried
letter for letter in the asset's own `tells` field (`source/items/misc/*.json` → `index.json` →
the `.mur` version 8 → `ItemRow::tells` → the card's first section). The two rows MU leaves
blank here, the **Ale** (group 14 number 9) and the **Antidote** (group 14 number 8), carry a
line of ours instead: MU prints nothing for either, so the sentences are written
from what the rule actually is — OpenMU's `AlcoholEffectInitializer` (+20 attack speed for 80
seconds) and `AntidoteConsumeHandlerPlugIn` (removes magic effect `0x37`, the poison) — and
each asset's `tells_from` says so. None of the six does anything yet: `Realm::useItem` takes
only the rows that heal or restore.

### 2.10 Special / set / socket (end of the tooltip)
| # | Block | Colour | Where |
|---|---|---|---|
| 22 | Trailing item-specific notes: `Unable to Equip with a Different Transformation Ring` GT 3088 (RED), `Fireworks will appear once thrown in the field.` GT 2244, `(in use)` GT 3143, figurine/relic/charm blocks with `Increase critical damage +10` GT 965, `Maximum HP increase +50` GT 3132 … + `Right click on your inventory to use.` GT 3124 | BLUE/RED | ZI:5204-5585 |
| 23 | Period item: `Expired Item` GT 3266 (RED) **or** `Expiration Day` GT 3265 (ORANGE) + `2026-09-22  18:30` (BLUE) | – | ZI:5587-5607 |
| 24 | **Set option block** (`CSItemOption::RenderSetOptionListInItem`, CSItemOption.cpp:1058-1105): blank, `Set Item option info` (`"Set"` GT 1089 + `"Item option info"` GT 159) in YELLOW, two blanks, then one line per set option, then two blanks. Option lines (`getExplainText`, CSItemOption.cpp:383-453): `Increase strength +30` GT 950, `Increase agility +30` 951, `Increase energy +30` 952, `Increase stamina +30` 953, `Increase command +30` 954, `Increase min. damage +20` 955, `Increase max. damage +20` 956, `Increase damage +20` 957, `Increase damage success rate +20` 958, `Increase defensive skill +20` 959, `Increase max. life +50` 960, `Increase max. mana +50` 961, `Increase max. AG +50` 962, `Increase AG increase rate +5` 963, `Increase critical damage rate 10%%` 964, `Increase critical damage +20` 965, `Increase excellent damage rate 10%%` 966, `Increase excellent damage +20` 967, `Increase skill attacking rate +20` 968, `Double damage rate 10%%` 969, `Ignore enemies defensive skill 5%%` 970, mastery lines `%s Increase damage strength/30` 971-975, elemental `Ice attribute skill increase damage +20` 976-982, `Increase damage when using two handed weapons +20%%` 983, `Increase defensive skill when using shield weapons 20%%` 984. **Colour = state**: GRAY when the item is not equipped or the option is inactive, RED when it is a mastery option your class cannot use, YELLOW for full-set options, GREEN for extended options, BLUE for ordinary ones (CSItemOption.cpp:1119-1133). | | CSItemOption.cpp:1058-1144 |
| 25 | **Socket block** (`CSocketItemMgr::AttachToolTipForSocketItem`, Network/Server/SocketSystem.cpp:218-273): blank, `Socket Item option info` (GT 2650 + 159) in PURPLE, blank, then one line per socket `Socket 1: Fire(<option name> +30)` GT 2655 — BLUE when filled, GRAY `No item application` GT 2652 when empty; element names GT 2640-2646 (`Fire/Water/Ice/Wind/Lightning/Earth/Unique`). If a set bonus is present: blank, `Bonus socket option` GT 2656 (PURPLE), blank, `<option name> +20` (BLUE). Seed-sphere items get their own block (`Element: Fire` GT 2653, `Level: 3` GT 2654) — SocketSystem.cpp:277-380. | | Season 4+ |

### 2.11 Pet tooltip (Dark Horse / Dark Raven) — GIPetManager.cpp:595-770
Replaces the whole tooltip: optional shop price header, bold BLUE name (`Dark horse` GT 1187 / `Dark Raven` GT 1214), two blanks, `Exp : 1200/4000` GT 201, `Level : 7` GT 368, (raven only) `Dmg(rate): 120~150 (85)` GT 203 and `Attack speed: 35` GT 64, `Life: 200` GT 70, then `Minimum Level Requirement: 232` GT 76 (horse) or `Charisma Requirement: 290` GT 698 (raven) with the same RED + `(lacking N)` rule, blank, `Can be equipped by Dark Lord` GT 61 (WHITE for a DL, DARKRED band otherwise), the `Special[]` loop in BLUE, and for the horse `Absorb 18%% additional damage` GT 744 + `Increase 2 possible attack distance` GT 1188.

### 2.12 Repair tooltip — `RenderRepairInfo`, ZI:5665-5959
blank, `Repairing cost: 12,500` GT 238 (bold, in the item-name colour), blank, item name (bold, name colour), blank, `Durability: [40/66]` GT 71, blank. Many item kinds return early (nothing repairable).

### 2.13 Item-info ("explanation") window — a different panel, ZI:809-903 + UI/NewUI/Inventory/NewUIItemExplanationWindow.cpp:87-400
Uses the same `RenderTipTextList` frame with a forced width, printing `Item info` GT 160 (bold BLUE), the base item name (bold WHITE), then a **per-level table** for +0…+15 with the columns `LV`, `ATK Dmg`, `WIZ Dmg`, `Curse`, `Attack`, `DEF`, `DEF Rate`, `STR`, `AGI`, `ENG`, `Command`, `STA`, `Req LV` (`RenderHelpCategory`, ZI:850-903 — headers in BLUE, rows WHITE when equippable at that level, RED when not), ending with the `Can be equipped by …` list. Worth stealing for a modern "stats at each level" panel.

---

## 3. Item-name colour rules

`RenderItemInfo` ZI:2166-2248 (repair variant ZI:5786-5846), first match wins:

| Priority | Condition | Colour |
|---|---|---|
| 1 | Jewels (Bless, Soul, Chaos, Guardian, Life, Creation), compiled jewel stacks, quest drops (Flame/Horn/Feather/Eye), Devil's Eye/Key/Invitation, Scroll of Archangel, Blood Bone, Gemstone/Harmony/refine stones, Lost Map, Fruits, Loch's feather, Wizard's Ring, Archangel weapons, several event items | YELLOW |
| 2 | Divine (archangel) weapons | PURPLE |
| 3 | `AncientDiscriminator > 0` (ancient set item) | GREEN_BLUE — **green text on a blue band** |
| 4 | socket item | VIOLET |
| 5 | excellent (`SpecialNum>0 && ExcellentFlags>0`) | GREEN |
| 6 | `Level >= 7` (i.e. +7 … +15) | YELLOW |
| 7 | has any option/skill/luck (`SpecialNum > 0`) | BLUE |
| 8 | otherwise | WHITE |

Wings/capes bypass rules 3-5 and use only the level/option test (ZI:2222-2242), so an excellent wing's name is still yellow/blue. Seeds and spheres are VIOLET (ZI:2633). The name line is always **bold**, and the same colour is reused for the shop price line (ZI:2269).

Name text assembly (ZI:2645-2671): `[<SetName> ]` (ancient) + `[Excellent ]` + `Name` + `[ +Level]`. Level is only appended when > 0.

---

## 4. Which lines are 0.75-era

Strong evidence (OpenMU `Version075`: only `ItemOptionTypes.Option` and `ItemOptionTypes.Luck` exist — `Version075/GameConfigurationInitializer.cs:28-35`; `MaximumOptionLevel => 4` i.e. +4/+8/+12/+16 — `Version075/Items/Constants.cs:20`; jewelry option max level 3 — `Version075/Items/Jewelery.cs:32`; weapons carry `Luck` + a damage option and optionally a `Skill` — `Items/WeaponInitializerBase`-style code at `Version075/Items/Weapons.cs:280-313`; wings carry one option + Luck — `Version075/Items/Wings.cs:44-79`). Excellent options first appear in `Version095d/GameConfigurationInitializer.cs:33-35`; Harmony, Guardian, ancient sets and sockets only in `VersionSeasonSix/GameConfigurationInitializer.cs:64-84`.

0.75 tooltip line set:
1. Item name (`Name`, `Name +N`) — white / blue (has option) / yellow (+7 and up). No excellent, ancient or socket colours.
2. `One-Handed Damage: a ~ b` / `Two-Handed Damage` / `Wizardry Damage`.
3. `Defense: n`, `Defense rate: n`, `Spell resistance: n` (unsure), `Attack speed: n`.
4. `Durability: [n/m]`, `Number of items: n` (potions, arrows, bolts), `Life: n` (Guardian Angel / Imp / Horn of Uniria).
5. `<Element> Resistance: n` on Ring of Ice/Poison, Pendant of Lighting/Fire.
6. `Minimum Level Requirement`, `Strength Requirement`, `Agility Requirement`, `Energy Requirement` — each with the RED + `(lacking N)` treatment.
7. `Can be equipped by <class>` (Dark Wizard / Dark Knight / Elf only), dark-red band when it does not fit you.
8. Skill line `… skill (Mana:n)` (GT 80-86).
9. Luck's two lines (GT 87 + GT 94).
10. Additional option: `Additional Dmg/Wizardry Dmg/defense/defense rate +4…+16`, `Automatic HP recovery n%` on jewelry.
11. Wing block: `Increase n% of Damage`, `Absorb n% of Damage`, `Increase speed`.
12. Boots/gloves +5: `Increases moving speed`, `Swimming speed increase`; staff `Wizardry Dmg n% rise`.
13. Jewel/consumable descriptions: Bless GT 572, Soul GT 573, Chaos GT 574, `Used when entering Blood Castle` 814, `Used when creating a Cloak of Invisibility` 816, `It is used to combine items for a Devil Square Invitation` 637, box `Throw it and you may receive some Zen or items` 571.
14. Shop lines: `Purchase Price: %s` GT 62 / `Selling Price: %s` GT 63, and `Repairing cost: %s` GT 238 in repair mode.
15. `Cannot be sold.` / `Cannot be traded.` / `Cannot store in vault.` / `Quest Item` (low GT ids 730-733 — present early, exact 0.75 usage unsure).

Not 0.75 (add space for them, but they are later): excellent options (0.95d+), ancient set name + set-option block and the ancient bonus stat (S1+), Charisma/Stamina requirements (0.97d / S3), 380 options and Jewel of Harmony (S2-S3), sockets and seed spheres (S4), period/expiry lines, personal-shop pricing, pet (Dark Horse/Raven) stat block, master-level notes, all cash-shop and event blurbs.

---

## 5. Gotchas a modern layout should know
- The array is cleared only partially: `ZeroMemory(TextListColor, 20*sizeof(int))` but 30 text slots and 50 colour slots exist (ZI:2132-2136) — stale colours can leak past line 20 in the original.
- `RenderTipTextList` stops at the first empty string (ZI:312-316), so any branch that forgets to fill a slot truncates the tooltip.
- Several branches decrement `TextNum` to overwrite the previous line (e.g. ZI:4000, 5429, 5445) — the original "layout" is really a mutable line buffer.
- There is no wrapping and no measured max width: a long lore line simply makes the whole box wider (up to the screen clamp).
- `Data/Local/Eng/ItemTooltip_eng.bmd`, `ItemTooltipText_eng.bmd`, `ItemLevelTooltip_eng.bmd` exist in the data tree but are **not read by this source tree** — the tooltip is built entirely in code from `item_eng.bmd` names + the string table.
