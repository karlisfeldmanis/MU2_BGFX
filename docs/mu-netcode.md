# How original MU Online splits client and server

Research notes for turning MU2_BGFX into a client/server game. Written 2026-10-05.

## Sources, and how far to trust each

| Tag | Source | Where |
|---|---|---|
| **WZ** | WebZen GameServer 1.00.93 (0.97d to S4.6, `#ifdef`-gated) | **Not on this machine.** Read from GitHub `ptr0x-real/Mu-GS-Webzen-MC-10093`, `Source/Server Side/GameServer/` (plus `Source/Base Common Src/include/define.h`). Line numbers below are from the raw files at HEAD on 2026-10-05. The copies I downloaded were temporary and have been deleted. |
| **OMU** | OpenMU (C#, server for 0.75 / 0.95 / S6E3) | `/Users/karlisfeldmanis/Documents/muremaster2/LEGACY/reference/openmu/` — the packet docs are in `docs/Packets/*.md` (one file per packet, 0.75 variants marked `075`) |
| **MM** | MuMain client (S5.2 leak → S6E3, sven-n fork) | `/Users/karlisfeldmanis/Documents/muremaster2/LEGACY/reference/MuMain/src/source/` — mainly `Network/Server/WSclient.cpp` and `Engine/Object/ZzzInterface.cpp` |
| *mem* | My own knowledge, not checked against code | Marked **(from memory, unverified)** |

This machine has no ConnectServer, JoinServer or DataServer source from WebZen. Everything about those processes comes from the GameServer's side of each link (WZ), from OpenMU's equivalents, or from memory.

---

## 1. Server topology

```
 client ──TCP 44405*──> ConnectServer (CS)    server list, then hands out a GS ip:port
   │                         ^ UDP: every GS sends {code, users, max, %} every 1 s
   │
   └──TCP 55901*──> GameServer (GS) x N  ──TCP──> JoinServer (JS)  accounts, auth, "who is online", map-server-move tickets
                                          ──TCP──> DataServer (DS)  characters, inventory, vault (SQL behind it)
                                          ──TCP──> ExDB           guilds and friends (S1+)
                                          ──UDP──> Event/Ranking servers (Chaos Castle, Devil Square ranks)
```
\* The usual default ports (from memory, unverified).

- **ConnectServer.** It is the first and only address the client knows. It sends the server list (`F4 06`), then the chosen server's ip:port (`F4 03`). It holds no game state. Every GS reports its load over UDP, `PMSG_SERVERINFO` with headcode 0x01: ServerCode, UserCount, MaxUserCount and Percent (WZ `Gamemain.cpp:4518-4550`). The report is sent once a second from timer 101 (WZ `GameServer.cpp:1468`). The `ConnectServer.md` doc and `src/ConnectServer/` in OMU cover the CS packet set.
- **JoinServer (login / auth).** The client sends its login (`C3 F1 01`, account and password Xor3-scrambled) to the **GameServer**, never to the JS. The GS relays it as `CSPJoinIdPassRequest`, and the JS answers `0x01 JGPAccountRequest` (WZ `Sprotocol.cpp:113-150`; `wsJServerCli`, `Gamemain.cpp:321`). The JS also handles duplicate logins (`0x08 JGOtherJoin`), kicks (`0x07/0x09`), billing time and map-server-move tickets.
- **DataServer.** This is the only process that touches the DB. The GS asks for the character list (DS `0x01`), create (`0x04`), delete (`0x05`) and full character load (`0x06 JGGetCharacterInfo`). It saves with `0x07` and reads or saves the vault with `0x08/0x09`. Item serials come from `0x52`, and a 1 s live-check from `0x58` (WZ `DSProtocol.cpp` `DataServerProtocolCore`). The GS checks its DS connection every 10 s and reconnects if needed (`MTIME_MIN`, `GameServer.cpp:1566`).
- **GameServer.** One process runs a fixed set of maps. In 0.97d-S2 every GS normally loads **all maps**. A "server" in the list (e.g. "Server 1 - 3") is one GS process with every map. One process holds:
  - `MAX_OBJECT` objects in a single `gObj[]` array: users, monsters, NPCs and summons together.
  - Build variants (WZ `define.h:4393-4421`): `MAX_OBJECT 5650 / MAX_MONSTER 4800 / MAX_USER 250` (pre-Chaos-Castle), `7400/5800/1000` (CC era), or `9000/7400/1000`.
  - Monsters occupy indices `[0, MAX_MONSTER)`, summons come next, and users start at `ALLOC_USEROBJECTSTART`.
  - A server's real limit is the configured `gServerMaxUser`, which it reports to the CS. Values are often a few hundred.
- **Map server groups (S1+, `MAP_SERVER_WORK_20041030`).** `MapServerManager.cpp` reads a table that says which server code serves which map. A server can be `HAVEMAP`, or redirect to `ANYOTHERFSTSVR` / `ANYOTHERRNDSVR` (WZ `MapServerManager.cpp`). Castle Siege and Kanturu use this to run on dedicated processes. A move to a map this GS does not serve goes like this:
  1. GS → JS `GJReqMapSvrMove` (account, char, dest server code, map, x, y). WZ `Sprotocol.cpp:1142`.
  2. JS → GS `JGAnsMapSvrMove` with four random `iJoinAuthCode` ints. The GS **saves the character** (`GJSetCharacterInfo(lpObj, idx, TRUE)`) and sends the client `C1 B1 00` with the destination ip:port and the auth codes (`Sprotocol.cpp:1190-1260`).
  3. The client drops the socket and connects to the new GS. It sends `C3 B1 01 ServerChangeAuthentication` with the account, the char name and the 4 auth codes (OMU `C3-B1-01-...md`). The new GS checks the codes with the JS (`JGAnsMapSvrAuth`, `Sprotocol.cpp:1319`), loads the character from the DS, and places the player. There is no password and no server list.
  
  The DS guards against a double save during the hop: `GJSetCharacterInfo` logs "Inventory Already Saved" (`DSProtocol.cpp:2103`). This is a dupe guard.
- **Ordinary map moves** inside one GS are just a server-side teleport, `C3 1C MapChanged`. The client reloads the terrain. S6 clients then send `C1 F3 12 ClientReadyAfterMapChange` before the server adds them to the map (OMU doc).

## 2. Transport and framing

- **TCP**, one socket per client, IOCP on the GS (`giocp.cpp`). The buffers are `MAX_BUFF_SIZE = 8192*2`. A send larger than that is dropped (`giocp.cpp:1009`). Game messages carry no UDP.
- **Header byte** (OMU `docs/Packets/PacketTypes.md`, WZ `giocp.cpp:550-560`):

| First byte | Length field | Server→Client | Client→Server |
|---|---|---|---|
| `C1` | 1 byte (max 255) | plain | XOR32 |
| `C2` | 2 bytes, big-endian | plain | XOR32 |
| `C3` | 1 byte | SimpleModulus | SimpleModulus + XOR32 |
| `C4` | 2 bytes | SimpleModulus | SimpleModulus + XOR32 |

  The layout is `[C1][len][headcode][subcode?][payload]`. Multi-byte fields are mostly big-endian in this protocol, with some little-endian fields (OMU docs mark each one).
- **XOR32.** A fixed 32-byte key starting `AB 11 CD FE 18 23 C5 A3 ...` (OMU `src/Network/Xor/DefaultKeys.cs`). It chains each byte with the previous byte and is applied from byte 3 (C1/C3) or 4 (C2/C4) onward. In WZ it lives in `CStreamPacketEngine_Server`, used in `giocp.cpp:630-650`.
- **Xor3** (`FC CF AB`) only scrambles the account, password and name strings inside login and the map-server-move auth.
- **SimpleModulus.** A block cipher: 8 plaintext bytes become 11 cipher bytes, with four 4-DWORD key sets (modulus, encrypt, decrypt, xor). Keys load from `Enc1.dat`/`Dec2.dat`-style files (WZ `SimpleModulus.h`; OMU `src/Network/SimpleModulus/`, where the 0.75 variant uses 32→38 byte blocks). The first decrypted byte is a **packet serial counter**, passed to `ProtocolCore(..., Encrypt=1, serial)` (`giocp.cpp:617-665`). The server uses it to reject replayed or injected packets. Login, character info, inventory, gate moves, item operations, trade, magic and similar use C3/C4. The hot packets (walk, hit, chat, viewport) use C1/C2.
- **Opcode scrambling by region.** The four hottest client opcodes (move, position, attack, area-hit) were renumbered per localisation with `PACKET_CHANGE` to break bots (WZ `define.h:4975-5030`). For example, the Korean build has move `D3`, position `DF`, magic-hit `10` and attack `D7`. The base codes are move `0x10`, position `0x11`, attack `0x15`, area-hit `0x1D` (WZ `protocol.cpp` `ProtocolCore` ~line 1040). This is why OMU's S6E3 ENG docs show walk `D4`, hit `11`, area-hit `DB`, while 0.75 uses `10`/`11`/`1D`.
- **Sizes.** Most game packets are small C1 packets (5-60 bytes). Viewport batches, the inventory (`C4 F3 10`) and the shop list (`C2 31`) are the large ones. An item is `ITEM_BUFFER_SIZE` = 5 (0.75), 7 (0.97) or 12 (S3+) bytes on the wire (WZ `define.h:4321-4326`).
- **Keep-alive / anti-speedhack.** The client sends `C3 0E 00 Ping{TickCount, AttackSpeed}` every **20 s** (MM `App/Platform/Windows/Winmain.cpp:151-171,1959`). If the client clock runs faster than the server clock by more than 7 s, the GS disconnects (WZ `protocol.cpp:1974 CGLiveClient`). The checksum challenge `C1 03` / `C3 03 ChecksumResponse` exists to detect a modified `main.exe` (OMU doc).

## 3. Authority: what the server decides

The overall rule: **the server owns every number** (HP, damage, drops, items, zen, exp, stats). The **client owns presentation and some spatial claims** (its path, its claimed start tile, and which targets an area skill "hit"). The server checks those claims more loosely than a modern game would.

### Movement: the client walks first, the server follows and checks loosely
- The client finds the path itself (A*, `PathFinding2`), starts walking at once, and sends `C1 10 Walk{SourceX, SourceY, StepCount:4 | Rotation:4, dirs[] 2-per-byte}`. MAX_ROADPATH is 15, so at most 15 steps per packet (MM `ZzzInterface.cpp:1030-1073 SendCharacterMove`; WZ `define.h:4244`). A walk with 0 steps is "stop / face this way". The client sends it when it starts an attack.
- Server, `PMoveProc` (WZ `protocol.cpp:14086-14560`):
  - It rejects a walk less than **100 ms** after the previous one ("anti-hack"), and walks while stoned, stunned or asleep, or while teleporting.
  - It rebuilds the path from the **client's claimed source** `(sx, sy)` plus the direction table. It checks only that the **first step** is not BLOCK/HOLLOW, and that the path does not cross water (`MAP_ATTR_WATER`).
  - `gObjPositionCheck`: the target must be **within ±14 tiles of the last target** (`m_OldX/m_OldY`) (`user.cpp`, `gObjPositionCheck`). On failure it snaps the player back (`gObjSetPosition` + a 0x18 stand action).
  - It echoes `0x10 ObjectWalked{id, TX, TY, dir}` to the walker **and** to every player whose viewport holds the walker (`VpPlayer2`), sets the stand attribute on the target tile, and then sets **`lpObj->X = sx; lpObj->Y = sy`**. The server's position becomes the client's claimed start.
- The server then **steps the object along the path itself** in `MoveMonsterProc`, which serves players and monsters alike (`user.cpp:836-1010`). One tile per `m_MoveSpeed` of **400 ms**, ×1.3 on diagonals, +300 ms when slowed. A blocked tile stops the path and snaps a player back. This proc runs on a **300 ms** timer in Blood-Castle-era builds (`MTIME_300`, `GameServer.cpp:553,1059`). So the server's X/Y trails the client's drawn hero. Range checks run against that trailing position.
- **Client reconciliation** (MM `WSclient.cpp:1973-2087`). The own hero ignores `0x10` echoes while it is moving (`if (Key == HeroKey) { if (!c->Movement) c->PositionX = TargetX; return; }`), so there is **no rubber-banding on normal walks**. A hard correction is `0x15 ObjectMoved` (`ReceiveMovePosition`), which snaps position, clears the path and stops. Other characters are path-found on the client from their current tile to `TargetX/Y`. The client fills in the steps itself.
- **`0x11` position set** (`RecvPositionSetProc`, `protocol.cpp:14560+`) lets the client set its own X/Y directly. It is meant for knock-backs and skills, and the only check is the same 15-tile `gObjPositionCheck`. OpenMU refuses the S6 equivalent `C1 15 InstantMoveRequest` outright, "because it allows hackers to teleport" (OMU `C1-15-InstantMoveRequest_by-client.md`). OMU's own walk validation in `src/GameLogic/PlayerMovement.cs:75-246` is stricter:
  - the walk start must be ≤ 5 tiles from the server position, or the server resyncs;
  - every step is checked against the walk map, and the path is truncated at the first blocked tile;
  - a speed-hack plugin with a token-bucket attack check (`PlugIns/SpeedHackDetectConfiguration.cs`).

### Melee attack
- The client plays the swing **and** sends `C1 15/11 Hit{TargetId, AttackAnimation, Dir}` at the **start** of the swing (MM `ZzzInterface.cpp:1313`, inside the `MOVEMENT_ATTACK` case after `SetPlayerAttack`).
- Server, `CGAttack` (WZ `protocol.cpp:14664-14860`):
  - target valid and alive;
  - packets **≥ 200 ms apart**;
  - **distance ≤ 3** (`gObjCalDistance`, added by the 2005 anti-hack patch);
  - interval ≥ `m_DetectSpeedHackTime`, derived from attack speed. Breaking this adds a strike, then a "penalty" of ignored attacks, then an optional kick.
  
  It then broadcasts the swing as `0x18 Action` to the viewport and calls `gObjAttack`, which rolls hit/miss, damage, crit, excellent, SD and so on. All of that is server-side.
- The result goes out as `GCDamageSend` → **`0x15` (S6 ENG `0x11`) `ObjectHit{targetId | 0x8000 if hit, damage, DamageKind colour, shieldDamage}`**, and **only to the attacker and the target** (`protocol.cpp:14866-14910`). Bystanders see the swing animation and the target's flinch, not the number. Death is `0x17 ObjectGotKilled{killed, skill, killer}` to the viewport. Exp is `C3 16 ExperienceGained` to the killer.
- The server's roll is immediate. The response usually arrives **before the swing's contact frame**, and MuMain shows the number **on arrival** (`ReceiveAttackDamage` → `CreatePoint` immediately, `WSclient.cpp:3237-3300`). The client applies HP loss to the hero locally from the same packet. There is no delayed "show on hit frame" step in the original client.

### Magic / skills
- Targeted skill: `0x19 TargetedSkill{skillIndex, targetId}`. The client throttles itself to one every 300 ms, with a few exceptions (MM `ZzzInterface.cpp:1106-1117`). The server (`CGMagicAttack`, `protocol.cpp:15185`) checks that the skill is learned, mana and AG, distance and speed. It rolls, broadcasts `0x19 SkillAnimation`, and sends damage as `0x15/0x11` per target.
- **Area skills are split, and the client chooses the targets:**
  1. `0x1E AreaSkill{skill, x, y, rot}`: the server only checks and broadcasts the animation ("no damage is done yet", OMU `C1-1E-AreaSkill075`).
  2. `0x1D AreaSkillHit{skill, x, y, count, targetIds[]}`: **the client decides which monsters were hit** and the server computes the damage.
  
  WZ `CGBeattackRecv` (`protocol.cpp:16156+`) clamps the count to **5 per packet**. The packet must arrive **within 8 s** of the 0x1E, with **at most 4 extra hit packets** per cast (`UseMagicCount`). It also has a serial check and the attack-speed check. It is the classic loose spot: a modified client could name any target in its viewport.
- Teleport (wizard) goes through `C3 1C` with gate 0 and target x/y. The server checks the target tile.

### Items, NPCs, crafting, trade: fully server-owned
- **Drops** are rolled on monster death by the server. They enter the map's item table (`MapC[].m_cItem`, `MAX_MAPITEM`) and are announced to viewports with `C2 20 ItemsDropped`. OMU drops 1 s after death (`AttackableNpcBase.cs:495`).
- **Pickup** `C1/C3 22{itemId}`: `MapClass::ItemGive` requires the same map, an item that is live and not already given, the **player within ±2 tiles** (server position), and **loot ownership** for `gLootingTime` seconds (the killer or his party) (WZ `MapClass.cpp ItemGive`; `MapItem.cpp:77-193`). The result is `C3 22` with the item and slot, or `0xFF`.
- **Drop** `C3 23{x, y, slot}`, **move** `C3 24{fromStore, fromSlot, itemBytes, toStore, toSlot}`. The server moves **its own copy** by slot (`gObjInventoryMoveItem`, `user.cpp:16821`). The item bytes the client sends are echoed, not trusted. The chaos box, trainer and vault slots can only be used while that window is open server-side (`m_IfState`, `protocol.cpp:7172+`). Fail is `24 FF`.
- **NPC shop**: talk `C3 30` → the server opens the window state and sends `C2 31` with the item list. Buy `C3 32{slot}` and sell `C3 33{slot}`: the server prices, places and takes zen. Repair is `C3 34`.
- **Chaos Machine**: items go into the server-side chaos box via `0x24`. Mix `C1 86{type}`: the **server rolls success** and replaces the box's contents. The result is `C1 86 ItemCraftingResult`.
- **Trade** (`0x36-0x3D`): request and answer, items moved into the server's trade box via `0x24`, money `0x3A`, OK `0x3C`, cancel `0x3D`. When both sides confirm, the server swaps, commits, and **saves both characters immediately** (`user.cpp:19540-19554`).
- **Stats**: `C1 F3 06{stat}`. The server checks for a free point and answers with new maxima (`CGLevelUpPointAdd`, `protocol.cpp:3833`). Level-up is server-decided, `C1 F3 05`.

### Known exploits that came from trusting the client
- **Speed hacks.** Client clock acceleration made walking and swings faster. Fixed with the 20 s tick compare (7 s drift kicks), the 100 ms walk gate, attack interval checks against `m_DetectSpeedHackTime` and penalties (WZ code above). Bots and "speed ups" stayed common in private servers.
- **Position and teleport hacks** via `0x11` position set and spoofed `0x10` source coordinates. The 15-tile `gObjPositionCheck` was the only limit. OpenMU drops instant-move entirely.
- **Wall / water walking.** In WZ, `PMoveProc` checks only the first step for BLOCK/HOLLOW. Water and Blood Castle gate checks were added later as patches (`CASTLE_NPC_GATE_WORK_20041206`, `MODIFY_FORCEMOVE_TO_LORENCIA_20060515`).
- **Area-skill target lists**: the client picks up to 5 targets per packet, with no geometry check in early builds.
- **Ranged / melee distance.** No distance check existed before `ADD_ANTI_HACK_01_20051027`.
- **Item duplication** through timing of trade, vault and map-server move against DB saves. There are many `ITEM_DUPLICATE_PREVENT_PATCH_*` flags, a `pTransaction` lock, and "Inventory Already Saved" on server hops.
- **Packet editing to skip UI state**, e.g. moving items into the chaos box without the NPC. Answered by `m_IfState` checks.
- **Modified main.exe**: answered by the `0x03` checksum challenge and later by GameGuard (from memory, unverified).

## 4. Viewport / interest management

- **Range.** WZ `gObjCheckViewport` (`user.cpp:20559`) is a **±15 tile square (31×31)**. Inside that square there is an extra test against a **camera-shaped trapezoid**, `FrustrumX/Y`. It is built from the client camera's far/near widths (far 2400, near 0.19×, far width 1190, near width 550, in world units of 100 per tile) and rotated 45° to match MU's fixed camera (`InitFrustrum`/`CreateFrustrum`, `user.cpp:20500-20540`). Objects behind the camera's footprint are not sent even when close. OpenMU uses a plain configurable radius, `InfoRange = 12` tiles (`Persistence/Initialization/GameConfigurationInitializerBase.cs:44`), with bucketed area-of-interest (`GameLogic/BucketAreaOfInterestManager.cs`).
- **Capacity.** `MAXVIEWPORTOBJECT 75` per player and `MAX_MONVIEWPORTOBJECT 20` per monster (`define.h:4279-4283`). Each object has `VpPlayer[]` (what I see) and `VpPlayer2[]` (who sees me). Broadcasts walk `VpPlayer2`.
- **Rebuild cadence.** **Once per second** for the whole server, on timer 101 (`GameServer.cpp:1133-1150`):
  1. `gObjStateSetCreate` for all objects
  2. `gObjViewportListDestroy` (mark out-of-range entries)
  3. `gObjViewportListCreate` (add new ones)
  4. `gObjViewportListProtocol` (send the batched packets)
  
  So something can take up to ~1 s to appear or vanish after it crosses the edge. That is the familiar MU monster "pop-in".
- **Messages (server → client)**, codes as WZ `user.cpp:24279+` and MM `WSclient.cpp` dispatch:

| Code | Meaning | Notes |
|---|---|---|
| `C2 12` | AddCharactersToScope | id, x, y, appearance (9 bytes in 0.75, more later), effects bits, name, target x/y, rotation (OMU `C2-12-AddCharactersToScope075`) |
| `C2 13` | AddNpcsToScope | monsters and NPCs: id, type, x, y, target x/y, rotation, effects |
| `C2 1F` | AddSummonedMonstersToScope | summons, with owner name |
| `C2 45` | transformed players | S1+ |
| `C1 14` | MapObjectOutOfScope | list of ids; removes players and monsters (`ReceiveDeleteCharacterViewport`) |
| `C2 20` | ItemsDropped / money | id with an "IsFreshDrop" bit (plays the drop animation and sound), x, y, item bytes |
| `C2 21` | ItemDropRemoved | ids; picked up or expired |
| `C1 65/5A/5B` | guild viewport info | |

  An object that **enters while walking** carries its current and target tiles, so the client can start it moving mid-path. A **new drop** sets the fresh-drop bit, and items already lying there do not animate.
- Monsters see players through their own viewport too (20 slots), which drives aggro.

## 5. Timing

WZ timers (`GameServer.cpp:540-561`, handlers `1016-1580`):

| Timer | Period | What runs |
|---|---|---|
| `MTIME_500` | **500 ms** | `MonsterAndMsgProc`: `gObjMonsterProcess` for each monster (old AI: target, attack, decide to move), `gObjMsgProc` for players, frustum refresh, delayed attack messages (`gSMAttackProcMsg`, combos), monster skills (`user.cpp:716-834`) |
| `MTIME_300` | **300 ms** | `MoveMonsterProc`: advances **every object** one path step when 400 ms (×1.3 diagonal) have passed (`user.cpp:836-1010`). Without Blood Castle this was `MTIME_100` at 300 ms, the same thing |
| `MTIME_100` | 100 ms | event schedulers (Blood Castle, Chaos Castle, Siege, Crywolf, Kanturu...) |
| `TIMER_MONSTER_MOVE` | 100 ms | new S1+ `TMonsterAI` movement for AI-scripted monsters only |
| `TIMER_MONSTER_AI` | 1011 ms | `TMonsterAI::MonsterAIProc`. The macro names are swapped in `define`: id 500, period 1011 (`GameServer.cpp:210-213`) |
| 101 / `MTIME_SECOND` | 1000 ms | viewport rebuild, `gObjSetState`, GS info to CS; `gObjSecondProc` (regen ticks, 10-min auto-save, DS live check, PK timers) |
| `MTIME_MIN` | 10 s | reconnect DS / JS if down |
| 100 | 2 s | repaint the server's own window |

- **There is no fixed simulation tick.** It is a set of Win32 `WM_TIMER`s on one window thread. Packets are processed as they arrive on IOCP worker threads, and a hit is resolved **inside the packet handler**.
- Monster speed in the old system is effectively **one tile per ~600 ms** (a 400 ms gate sampled every 300 ms). Monster attack decisions happen on the 500 ms proc, against each monster's AttackSpeed/AttackDelay from Monster.txt. OpenMU instead runs each monster on its own `Timer` at `AttackDelay` (`GameLogic/NPC/BasicMonsterIntelligence.cs:57-59`) and walks it at `MoveDelay` per tile (`NPC/Monster.cs:354-360`).
- **Client prediction of the local hero.** The client is fully predictive for walking, starting at once and ignoring server echoes. It is **not** predictive for outcomes: HP, exp, items and kills all wait for the server. It plays its own swing and cast animations without waiting. Damage numbers, flinches and deaths appear when `0x15/0x11` and `0x17` arrive. The hero's HP bar is reduced locally from the same packet (`ReceiveAttackDamage`, `key == HeroKey`).
- **Player death and respawn**: `0x17` on death. After a few seconds the server sends `C3 F3 04 RespawnAfterDeath{map, x, y, dir, hp, mp, exp, zen}`, and the client reloads at the town spawn (OMU `C1-F3-04-RespawnAfterDeath075`; MM `ReceiveRevival`). The ~3 s delay is from memory, unverified.

## 6. Persistence

- **When the GS saves a character** (`GJSetCharacterInfo` → DS `0x07 SDHP_DBCHAR_INFOSAVE`, WZ `DSProtocol.cpp:2094-2200`):
  - **every 10 minutes** of play (`gObjSecondProc`, `user.cpp:22907` "10분에 한번씩 자동 저장");
  - **on logout / disconnect** (`user.cpp:6257`), and for everyone at shutdown;
  - **after a completed trade**, both players (`user.cpp:19553-19554`);
  - **before a map-server move** (`Sprotocol.cpp:1234`);
  - before a character transfer.
  
  The vault (warehouse) is saved separately when it is closed. The guild, friends and mail live in ExDB.
- **What the save holds:** account, name, class, level, LevelUpPoint, exp, NextExp, zen, Str/Dex/Vit/Ene, life, maxlife, mana, maxmana (×10 fixed-point), CtlCode (ban/GM), **inventory blob**, **skill list blob** (`MagicByteConvert`), map, x, y, dir, PK count, level and time, quest bytes.
- **The inventory is one binary blob**: `ItemByteConvert10/16` writes every slot (equipment + inventory, later + personal shop) as fixed records:
  - 0.75 / 0.97: 7 → 10 bytes per item; S3+: 16 bytes. Fields are index, level/skill/luck/option bits, durability, a 4-byte serial, excellent bits and ancient bits.
  - Sizes: `MAX_DBINVENTORY` 760 (76×10), 1080 or 1728 (108×16) bytes, depending on version (`define.h:4298-4314`).
  - The DB column is a varbinary. Empty slots are `FF` bytes (from memory, unverified).
  - The vault is a similar 120-slot blob.
  - Item **serials** come from the DS (`0x52`) and exist so dupes can be traced.
- Between saves, the authoritative state lives **only in GS memory**. A GS crash loses up to 10 minutes. OpenMU instead saves through EF Core: on trade, drop and close-NPC, and **every 1 minute** by default (`GameLogic/PlugIns/PeriodicSaveProgressPlugInConfiguration.cs:16`). It stores items as rows, not a blob.

## 7. Minimal message set for a playable game

Codes are the **base / 0.75-0.97 codes**. In brackets: the S6E3 ENG code where it differs, from the OMU docs. ↑ means client→server, ↓ means server→client.

| Purpose | Message |
|---|---|
| Hello on connect | ↓ `C1 F1 00` GameServerEntered (your object id, version) |
| Login | ↑ `C3 F1 01` Login (Xor3 user/pass, tick, version, serial) · ↓ `C1 F1 01` LoginResponse |
| Logout | ↑ `C3 F1 02` · ↓ `C3 F1 02` |
| Character list | ↑ `C1 F3 00` · ↓ `C1 F3 00` CharacterList (name, level, class, appearance) |
| Create / delete | ↑ `C1 F3 01` / `C1 F3 02` · ↓ results |
| Join map (select) | ↑ `C1 F3 03` SelectCharacter · ↓ `C3 F3 03` CharacterInformation (map, x, y, stats, exp, zen, hp, mp) · ↓ `C4 F3 10` CharacterInventory · ↓ `C1 F3 11` SkillListUpdate |
| Keep-alive | ↑ `C3 0E 00` Ping (every 20 s) |
| Walk | ↑ `C1 10` [D4] WalkRequest · ↓ `C1 10` [D4] ObjectWalked (self + viewport) |
| Snap / teleport | ↓ `C1 15` ObjectMoved (server-forced position) |
| Animation / emote | ↑ `C1 18` · ↓ `C1 18` ObjectAnimation (also the swing broadcast) |
| Melee | ↑ `C1 15` [11] HitRequest{target, anim, dir} · ↓ `C1 15` [11] ObjectHit{target(+hit flag), dmg, kind, sd} to attacker + target |
| Targeted skill | ↑ `C1 19` TargetedSkill · ↓ `C1 19` SkillAnimation (viewport) + `ObjectHit` |
| Area skill | ↑ `C1 1E` AreaSkill{x, y, rot} · ↓ `C1 1E` AreaSkillAnimation · ↑ `C1 1D` [DB] AreaSkillHit{targets ≤5} · ↓ `ObjectHit` each |
| Death | ↓ `C1 17` ObjectGotKilled{killed, skill, killer} |
| Exp | ↓ `C3 16` ExperienceGained{killed, exp, lastDmg} |
| Respawn | ↓ `C3 F3 04` RespawnAfterDeath |
| HP/MP | ↓ `C1 26 FF` CurrentHealth(+SD) · `C1 26 FE` Maximum · `C1 27 FF/FE` mana/AG |
| Level up | ↓ `C1 F3 05` CharacterLevelUpdate (level, points, max hp/mp...) |
| Stat point | ↑ `C1 F3 06` IncreaseCharacterStatPoint{stat} · ↓ `C1 F3 06` response |
| Viewport in | ↓ `C2 12` players · `C2 13` monsters/NPCs · `C2 1F` summons · `C2 20` items/money on ground |
| Viewport out | ↓ `C1 14` objects out of scope · `C2 21` ground items removed |
| Pick up | ↑ `C1 22` [C3 22] PickupItemRequest · ↓ `C3 22` ItemAddedToInventory / failed · ↓ `C3 22 FE` InventoryMoneyUpdate |
| Drop | ↑ `C3 23` DropItemRequest · ↓ `C1 23` ItemDropResponse |
| Inventory move / equip | ↑ `C3 24` ItemMoveRequest · ↓ `C3 24` ItemMoved / `24 FF` failed · ↓ `C1 25` AppearanceChanged (viewport) |
| Use item (potion) | ↑ `C1 26` [C3 26] ConsumeItemRequest · ↓ `C1 28` ItemRemoved / `C1 2A` durability |
| NPC | ↑ `C3 30` TalkToNpc · ↓ `C3 30` NpcWindowResponse + `C2 31` StoreItemList · ↑ `C1 31` close · ↑ `C3 32` buy / `C3 33` sell / `C3 34` repair |
| Chat | ↑ `C1 00` PublicChat · ↓ `C1 00` ChatMessage · ↑↓ `C1 02` whisper · ↓ `C1 0D` ServerMessage (notice) |
| Map move (gate) | ↑ `C3 1C` EnterGateRequest{gate} · ↓ `C3 1C` MapChanged{isMapChange, map, x, y, rot} · ↑ `C1 F3 12` ClientReady (S6) |
| Map move (menu / warp) | ↑ `C1 8E 02` WarpCommandRequest (S1+) |
| Server hop (optional) | ↓ `C1 B1 00` ip/port/auth · ↑ `C3 B1 01` ServerChangeAuthentication |
| ConnectServer (optional) | ↑ `C1 F4 06` list · ↓ `C2 F4 06` · ↑ `C1 F4 03` · ↓ `C1 F4 03` ip:port |

Packet layouts are in `LEGACY/reference/openmu/docs/Packets/<header>-<code>-<Name>_by-<side>.md`, which has byte tables. The MuMain dispatch is in `WSclient.cpp` around lines 12700-13700.

## Takeaways for MU2_BGFX (my reading, not source)

- The original split is "server simulates, client presents". The client predicts only its own walk path. Hits, drops and items all wait one round trip, and the numbers simply appear when the packet lands. That suits MU2_BGFX's existing realm/showing split: the realm is the server, and the showing hangs off arriving events.
- Keep the useful parts: the claimed-source walk path (cheap, and smooth on bad links), batched viewport enter/leave, and a server-side item state that the client only mirrors.
- Change the parts that were exploits:
  - check every walk step against the walk map and a speed budget (the OMU approach);
  - **choose area-skill targets on the server**, not from a client list;
  - check melee/skill range against the server's position;
  - never accept a client-set position (`0x11`/`0x15`).
- WZ's 1 s viewport rebuild and 300/500 ms timers come from 2003 hardware. A fixed server tick (e.g. 20-25 Hz, matching MU's 25 fps frame unit) with incremental interest updates will feel better and remove the pop-in.
