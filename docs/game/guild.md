# The guild

Started 2026-10-13 as research and a plan. The user: "research how guild making works, what we
need to design on UI part on artifacts, how guild invite works, basically everything related
guild, make some plan of implementation". The UI proposal page is linked under *Proposal*. Nothing
here is decided until the user picks; the defaults marked **(rec.)** are what we would build.

Sources, by authority: **GS** = WebZen GameServer 1.00.93 (0.97 rules read from the non-`#ifdef`
branches; github ptr0x-real/Mu-GS-Webzen-MC-10093, cp949), **MM** = MuMain
(LEGACY/reference/MuMain/src/source), **OM** = OpenMU (LEGACY/reference/openmu).

## MU's rules (0.75 to 0.97d)

### Making a guild

- **The NPC**: Guild Master, NPC 241, in **Devias at (215,45)** (GS MonsterSetBase.txt:2546; OM
  Version075/Maps/Devias.cs:53). Talking to him (GS NpcTalk.cpp:832) checks: not in a guild (msg
  1140), **level 100 or more** (Gamemain.cpp:3435 default; user.cpp:8647). The "all worn items +7"
  rule is commented out. **No Zen cost** in any source.
- **Flow**: 0x54 "Do you wish to be the guild master?" -> yes -> 0x55 opens the editor -> the
  client sends name (8 bytes) + mark (32 bytes) -> uniqueness checked server-side -> 0x56 result
  (0 name taken, 1 OK, 2 under 4 characters, 3 already in a guild, 4 space, 5 symbol, 6 reserved;
  MM WSclient.cpp:7485-7505). 0x57 cancels.
- **Name**: 4-8 characters, no spaces or symbols, unique (MM NewUIGuildMakeWindow.cpp:21-27,
  366-372). GS lets 3 through (protocol.cpp:12230); OM checks only uniqueness. We take MM's.
- **Mark**: 8x8 pixels, 4 bits each, 32 bytes; 16 colours, index 0 transparent (MM
  ZzzInventory.cpp:11183-11234): transparent, black, grey, white, red, orange, yellow, lime,
  green, spring, cyan, azure, blue, violet, magenta, rose. The editor won't take an empty mark.
- **Editor window** (NewUIGuildMakeWindow.cpp, 190x429): step 1 the question and a Create Guild
  button; step 2 name box, 8x8 grid of 15 px cells, a 2x8 palette, left-drag paints, right-drag
  clears; step 3 a 48 px preview with the name, Back and Next.

### Joining

- **The applicant asks the master**, not the other way round. A guildless player types `/guild`
  in front of a master: with a target, within 1 tile; with none, the first player within 1 tile
  **facing him** (MM ZzzInterface.cpp:2221-2270). The S6 right-click menu skips the distance
  (NewUICommandWindow.cpp:511-527). The server never checks distance.
- **Server checks** (GS protocol.cpp:11670-11851): target online; neither busy; applicant **level
  6+**; applicant guildless; master's request option on; target is the master; guild not at war;
  guild not full.
- **The master** gets an OK/Cancel box: "<name>: you have received an offer to join a guild".
  **No timeout** (the auto-cancel was cut in 2002, user.cpp:10116-10155). OM keeps one pending
  request per master; a second gets "busy".
- **Cap**: master's level / 10, master counted, at most 40 (GS :11820, GuildClass.h:16). The Dark
  Lord's +Leadership/10 (to 80) is S1. OM never enforces a cap.

### Leaving, kicking, disbanding

One packet, 0x53 (name + the account's personal-ID code; GS protocol.cpp:12015-12156): the master
naming himself **disbands** (a running war ends first); the master naming another **kicks**; a
member naming himself **leaves**. No cooldown, no fee. On disband, 0x5D strips the mark from
everyone in view.

### Chat and notice

`@text` guild chat to every online member; `@>text` the **notice**, master only, 60 characters,
two lines in the guild window (GS protocol.cpp:2575-2700, define.h:4232). Guild lines are black on
green in the log. OM has no `@>`.

### What others see

- The **mark is worn on the armour**: a 5x7-unit (5x7 cm) plane at (20, -5, -10) off bone 26,
  `Bip01 R UpperArm` -- a patch on the right upper arm, not the back (MuMain ZzzCharacter.cpp
  RenderGuild :6568-6606; corrected 2026-10-13 on reading it) -- hidden in Chaos Castle
  and while cloaked.
- `[Guild]` shows over the name on hover or speech: your own guild yellow-green, others lavender
  on navy. The name keeps its PK colour; a master gets no colour of his own.
- The 0.75 client knew a player's guild id, name and mark only; no rank byte (OM
  C2-5B-AssignCharacterToGuild075). "Master" labels are S1+.
- The **guild window** (legacy CUIGuildListBox, UIControls.cpp:1152-1290): the master's row first
  on a blue band with the mark; members light blue; offline grey; each online member's server as
  a "(n)" chip. The master has an X on every row (kick; his own row disbands), a member only on
  his own (leave). Guildless, it says "Type /guild in front of the guild master you want to join".

### Guild war and Battle Soccer (needs PvP)

- `/war <guild>` from a master; the other master accepts. Refused if either master is a murderer
  (PK 6+), either guild is at war, or the other master is offline or busy.
- Every online member of both guilds is at war (GS; OM only the master's party). A kill is 1
  point, the enemy master 2, **first to 20** wins, no time limit. A master leaving or disbanding
  loses. The reward is +1 guild score; item transfer is disabled in GS.
- War kills are not PK, drop nothing, cost no experience; the dead respawn within 18 tiles.
  During a war you can't hit your own guild, outsiders can't hit the warring, monsters take no
  damage.
- Outside a war a guild gives **no PvP protection**.
- Battle Soccer: `/soccer`; only the masters' parties go to the Arena (map 6), red and blue. A
  goal is 20, kills as war, first to 100.

### Later seasons (not in scope)

Assistant and Battle Master ranks, guild types, alliances (`/union`, `@@`), rivals, the S3
three-tab window, Castle Siege, Gens.

## What we have already (2026-10-13)

The scaffolding is half there:

- **Wire**: `Channel::Guild = 2` (src/net/wire.h:160). Kinds end at 18 (`kLastKind = BrokerView`);
  protocol `kVersion = 28` (:107).
- **Server chat**: answers "You are not in a guild." (server/src/chat.cpp:101-104).
- **Client chat**: `kGuildInk 0x93d67e`, `/g` and `/guild`, a Guild tab behind `inGuild_` which
  is never set (src/game/ui/chat.cpp:32, :438-445, :756-770).
- **Speech**: a guild bubble style (`Play::kSaidGuild`, speech.cpp:167-183).
- **Nameplates and target frame**: a guild line under the name (`kTagGuild`), used today only
  for `<Bot>` (vitals.cpp:456-477, target_frame.cpp:291-297).
- **Card window**: shows "[guild]" when set (card.cpp:308), never set.
- **The NPC**: `kGuildMaster = 241` stands in Devias (src/sim/realm.h:208) and only greets
  (realm_folk.cpp:332; play.cpp:2795-2801 "not ready").
- **No PvP at all** (realm_fight.cpp:183), so war has nothing to hook into.
- **The party is the model**: server/src/party.cpp (askParty, partyTick each second, `g_invites`
  60 s), wire PartyAsk/PartyState, the chat's invite card (chat.cpp:1121-1170), netbot `--parties`.
  The card is found by matching chat text; a guild should have its own field instead.
- **Free**: the G key; a row in the player menu (player_menu.cpp:21-44).

## The user's picks (2026-10-13)

- **Guild window: B, online first**: two-line member cards (name and map, class and level), Online and All tabs, 8 to a page, the × to kick or leave, Disband and Invite in the foot.
- **Nameplate: A**: the mark and the guild's name on the line under the name (today's `<Bot>` line), always shown, in the name's own green.
- **Invites use the party's card** (`Chat::buildInvite`): kicker *Guild* in the guild ink, Accept and Decline, 60 s; a wire field instead of matching chat text.
- The page follows the Sanctuary design system (the trade page's CSS).

## Choices still open

| question | MU | **(rec.)** |
|---|---|---|
| who starts a join | applicant asks the master, face to face | **both**: the master invites by name (`/ginvite NAME`, menu row, like the party), and a guildless player may ask a master (menu row "Ask to join", MU's `/guild`) |
| answering | OK/Cancel box, no timeout | **the chat's invite card**, Accept/Decline, 60 s like the party |
| create requirement | level 100, free | **level 100, free** (MU) |
| where | Devias Guild Master only | **Devias only** (MU); a Lorencia one is ours if wanted |
| member cap | master level / 10, max 40 | **MU's** |
| ranks | master only | **master only** for now; an Assistant later |
| leave / kick / disband | personal-ID code | **a confirm in the window**; disband asks for the guild's name typed |
| mark | worn on the right upper arm, `[Guild]` on hover | **on the nameplate's guild line** (mark + name, always) first; the arm patch later |
| notice | `@>`, 60 characters | **MU's**, also editable in the window |
| bots | n/a | bots **accept** a guild invite like a party one; a "Wanderers" bot guild on the server |
| guild war | `/war`, first to 20 | **parked** until PvP exists |

## Plan

Each step is one commit, tested on a private server first ([[party-review-run]] recipe). All
server steps ship to Hetzner as one build with the client and netbot (protocol change).

1. **G1 The server's guild** (protocol 29). store.cpp: `guilds(id, name UNIQUE COLLATE NOCASE,
   master_token, mark BLOB(32), notice, score, made)` and `guild_members(token PK, guild,
   joined)`. host.h: `Guild`, `g_guilds`, `g_guildOf` (by token, so it survives map changes, as
   the party does), `g_guildAsks`. Wire: `GuildAsk{what, name, text, mark}` with what = Create,
   Invite, Ask, Accept, Decline, Leave, Kick, Disband, Notice; `GuildState{name, mark, notice,
   members[name, kin, level, world, online]}` to members on change, not each second;
   `GuildMark{guild, name, mark}` once per guild to each client that sees one. `net::Who` gains
   a guild id. Server checks are MU's (above). Chat's Guild channel: `@`/`/g` to online members.
   `sim_test` untouched: the realm knows nothing of guilds.
2. **G2 Making one** at the Guild Master: `Command::Window::Guild`, the realm opens it when the
   level is 100+ and the speaker is guildless (else his MU line). Desk window `guild_make`: the
   three steps with the mark editor. Shown in the studio on Devias at (215,45).
3. **G3 The guild window** (G): header with mark, name, members n/cap, notice; the member roll;
   Leave / Kick / Disband with confirms; guildless state with the hint and a Create hint pointing
   to Devias.
4. **G4 Being seen**: an 8x8 mark atlas (one 128x128 texture, 256 marks of 8x8, filled on
   GuildMark), the nameplate's guild line "[mark] Name" for everyone, own guild in guild green;
   target frame and Card; the chat's Guild tab on; player menu rows "Invite to guild" / "Ask to
   join".
5. **G5 Bots**: netbot answers a guild invite card; the Board makes one bot guild on the server.
6. **Later**: the mark on the right upper arm (bone 26); guild war once PvP is decided.

## Built (2026-10-13)

The user: "implement". Everything in the plan but the arm patch and war, with the table's
defaults above taken as decided. Protocol 29.

- **The server** (server/src/guild.cpp, guild_store.cpp; host.h's guild section). `g_guilds` by
  id, `g_guildOf` by character token; kept whole in characters.db's `guilds` and
  `guild_members` on every change and each minute for when members were last seen
  (Store::keepGuild), read back at start (loadGuilds). MU's rules: found at level 100, by the
  Guild Master (within kGuildMasterReach of his tile, `Realm::folkTile`), a name of 4 to 8
  letters or digits unique in any case, a mark not blank, free; join at level 6; a cap of the
  master's level / 10, 40 at most; a master disbands rather than leaves, the name typed. Invites
  and asks stand 60 s and say so when they run out. Every refusal is a Guild-channel line.
- **The wire**: GuildAsk (Found, Invite, Ask, Accept, Decline, Leave, Remove, Disband, Notice),
  GuildState to members on change and every 5 s, GuildMark (name, mark, master) once a line,
  GuildCall for the card, Who's guild per player.
- **The chat**: the Guild tab live; `@text` and `/g` to the guild, `@>text` the notice,
  `/ginvite NAME`, `/gjoin MASTER`, `/gleave`, `/gnotice TEXT`. The party's invite card carries a
  guild's call: kicker *Guild*, the mark, the guild's name in its green, a quiet line (members,
  master, map; or the asker's class, level, party and map), Accept and Decline, a bar down its
  60 s (Chat::guildCalled). Answered by GuildAsk, never by matching text.
- **The windows** (game/ui/guild.*): GuildMake, opened by the Guild Master's greeting on a server
  (Play::takeGuildMaster); GuildWindow on G, in the first free column from the right. Both
  through Desk; their fields hold the keyboard as the Broker's do.
- **Seen**: the nameplate's guild line (vitals.cpp) is the mark and the name for every player in a
  guild, `<Bot>` for a bot in none; the target frame's line the same; the Card's [guild]. The
  player menu's "Invite to guild" (a master, on one in no guild) and "Ask to join guild" (one in
  none, on a guild's master).
- **Bots**: netbot accepts a guild invite while in no guild, party or not (since 2026-10-13, `--guilds` plays the whole guild: below).
- **Checked**: server_test's guild (found, refuse, invite across maps, accept, notice, a restart
  reading it back, remove, ask, leave, disband, the mark on the wire): 50 checks, 0 failed.
  sim_test 7462, 0 failed. And live on a private port (`mu2_server --kit 150:Zen+5000000`, two
  netbots, scripted clients): founded Vanguard at the Guild Master, invited both bots by name
  from the window, they joined from Lorencia; a second client's ask and the master's invite each
  showed the card; disbanded with the name typed and founded Ember.

## The bots' guilds (2026-10-13)

The user: "test with fresh bot server with 100 bots", "idea is that in guild bots has to act
different their goal is to be stronger as group", "tardes has to happpen anywhere". Picks: bots
found their own (no head start: level 100 at the Guild Master like anyone); masters invite and
the guildless ask; members hunt together, share gear, share jewels and Zen, and the strong carry
the weak; masters set the notice; members leave, are removed, guilds disband; recruiting for what
the guild lacks; a guild power score; no rivalry yet. `netbot --guilds` (tools/netbot/guilds.h):

- **One folder for every netbot** (`--dir`/guild/): each bot's line (NAME.who, every 3 s), each
  guild's plan by its master's netbot (NAME.plan: size, cap, roles lacking, power, parties), and
  the gifts' offers and claims. A guild spans netbots as the server's does.
- **Founding**: a guildless bot of level 100+, not a trader, while fewer than one guild per ten
  bots stands and no guild with room lacks his role, goes to Devias (Bot::order), to the Guild
  Master, and founds one of twenty names with a mirrored two-colour mark. One founder at a time
  per netbot. Given up after 40 game minutes.
- **Roles**: a tank (knight, melee gladiator), a healer (energy elf), damage (wizard, magic
  gladiator, agility elf). A guild wants three tenths tanks and two tenths healers of its cap.
- **Recruiting**: every 5 game minutes the master invites the best guildless bot -- a role the
  guild lacks first, then strength against the guild's middle (Bot::strength). Asked in, he takes
  one his guild lacks or one as strong as its middle. Full, he removes the weakest for a candidate
  1.5 times as strong (not one whose role would fall short), once every two game hours.
- **The guildless** ask, every 30 game minutes, the guild that most lacks their role. Invites are
  always taken.
- **Parties**: the master plans the online members (no traders) into parties of five: the
  strongest left leads, a tank and a healer within 40 levels join him, then the weakest within 40
  -- carried. Each is a Board group (Board::guildPlan), kept and farming the most experience an
  hour they stand together as the elite's are; a member on another netbot follows its leader as he
  would a player. Guild members are out of the elite, duos and quest parties.
- **The notice**: "Hunt with LEADERS. Spares to guildmates.", or what the guild is recruiting.
- **Gifts**: each member offers his spares (Bot::spares, what he would sell) and the Zen over 3M +
  20k a level; each guildmate tries each on himself (Bot::worth) and claims what helps him, and
  Zen while under 200k + 15k a level. A giver on the same map as the claimant walks to him, asks a
  trade, puts the things and the Zen in and accepts; the taker stands, says yes and accepts. Any
  map but an event's. Done inside each bot's tick (NetBot::giftStep, takeStep): at --speed 100 a
  trade's ask stands 0.2 s of the wall.
- **Churn**: a member of a guild of two or fewer for six game hours leaves for one lacking his
  role; a master of two or fewer for six game hours disbands while another has room.
- **The report**: `tools/netbot/guild_report.py sample RUN` / `report RUN`: what the guilds did, the
  guilds at the end, and levels and strength gained an hour in a guild and out, by level band.

Not yet: the mark on the right upper arm (rendered in Blender at our camera on 2026-10-13: about
5 pixels at 1080 lines, a red speck); guild war and Battle Soccer (no PvP). Deploying is
separate: client, server and netbot go together (protocol 29).

## Proposal

UI proposal page: https://claude.ai/artifact/2qCQPGfwWcWmi9XCRvSVvq (create window with a working mark editor, guild window A/B, invite cards, nameplate).
