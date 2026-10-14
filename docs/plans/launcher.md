# The launcher and the accounts

2026-10-14. The user: "now we have to create windows launcher together with account", picking
"Small C++ app, game's UI" and "Install + updates"; "macos later".

**The idea:** a player downloads one small program from the website, `MU2Launcher.exe`. He signs
in with the email and password he registered on the site (web/, live at https://37.27.158.226).
The launcher installs the game from the box, keeps it up to date, and starts `MU2.exe` signed in.
His characters belong to his account, not to his machine.

The look is the "MU2 Launcher" canvas (claude.ai/artifact/AvN3tu56TjFCX1SbYvnPmT, Launcher page):
960×540, the knight, the MU Classic logo, the game's own buttons (src/game/ui/controls.cpp).

## What exists

- **Accounts:** web/server.py on the box. `POST /api/login {email, password}` answers a token of
  64 hex digits, good for 90 days. `GET 127.0.0.1:8080/internal/session?token=T` answers
  `{account: id}`, on the box only.
- **The game's account today** is a machine key: `game::accountKey()` (src/game/roster.cpp) makes
  32 hex digits once into `%APPDATA%\MU2\account.key`, and the lobby sends it in `net::Account`.
  `mu2_server` takes any key of 16 to 64 letters and digits (accounts.cpp `keyShape`) and keeps
  characters under it.
- **The Windows client:** `MU2.exe` and 17 packs, `./mu dist windows` (docs/plans/windows-client.md).

## The steps

**L1. Accounts in the game.** The launcher's token goes where the machine key goes now.
- The client sends `MU2_SESSION` (an environment variable the launcher sets) as the Account's key
  when it is there, and its machine key when it is not (dev runs, bots, the Mac until L5).
- `mu2_server --accounts http://127.0.0.1:8080`: a key of 64 hex digits is a token. The server
  asks the web service in the background (a job polled each pass, like the snapshot's packing),
  never on the loop, and keeps the player's characters under `acct<id>`.
- A token the web service does not know gets a Roster refused `SignIn` (protocol 31), and the
  lobby says "Sign in again in the launcher." Today any refusal is a silent close that reads as
  "Cannot reach the server."
- **Done when:** on a private port, a token from a local web service opens the lobby with that
  account's characters; a made-up token is refused with the sign-in line; a machine key still
  works; `sim_test` unchanged.
- **Done 2026-10-14** (server/src/signin.cpp). On a private port against a local web service: a
  token opens the character screen as `account000000001`; the same token again is served from the
  ten-minute memory without asking; a made-up one gets `Refused::SignIn`; a machine key still
  works; a key that starts `account` is turned away, so no one can name himself into an account; a
  character made under the token enters Lorencia with the token in its Hello. server_test 64/64.
  The box's service file takes `--accounts http://127.0.0.1:8080`; **not deployed**: protocol 31
  goes to the box with the first client that speaks it.

**L2. Releases on the box.** `./mu release windows` puts `dist/windows/MU2/` on the box under
`/srv/mu2-web/game/windows/`, every file named by its SHA-256, and writes `manifest.json` last:
the version, the protocol, and each file's path, size and hash. Only new files travel. Caddy
serves them, with ranges, so a broken download resumes.
- **Done 2026-10-14** (tools/mu/release.py). The first release is the client dist last made,
  354bb0d9, protocol 30 like the live server: 19 files, 1.53 GB, at
  https://37.27.158.226/game/windows/manifest.json. Sent at about 23 MB/s; a second run sent
  nothing and took 4 s. Read back over HTTPS: MU2.exe and a pack, sizes and SHA-256 right; the
  files immutable with ranges, the manifest `no-cache`.

**L3. The launcher.** `src/launcher/`, the executable `mu2_launcher` (`MU2Launcher.exe`), built
by mingw like `MU2.exe`, linking `mu_gfx` and the few UI sources it draws with.
- The screens: Sign in, Install, Downloading, Ready. Edit account and Log out on the signed-in ones.
- HTTPS by WinHTTP, Windows' own. SHA-256 and a small JSON writer in core/.
- The token kept with DPAPI (`CryptProtectData`) in `%APPDATA%\MU2\session`, so another user on
  the machine cannot read it.
- The game in `%LOCALAPPDATA%\Programs\MU2` by default, no administrator asked; Change picks another.
- Update: fetch the manifest, hash what is there, fetch what differs into a temporary name,
  replace when whole. Play stays off until every file matches.
- Play: `CreateProcessW` on `MU2.exe` with `MU2_SESSION` set; the launcher minimises.
- Its own update: a newer launcher in the manifest is fetched beside it, swapped in on the next start.
- **The engine is done (2026-10-14)**, src/launcher/ with no screens yet: `mu2_launcher --check`
  signs in, keeps the token, reads the manifest, finds what is stale, fetches it and checks it.
  - On the Mac against the box: a whole install, 19 files and 1.53 GB in 34 s, every hash right;
    a pack with one flipped byte and another left half-fetched as a `.part` were the only two found
    stale and both came down right; a third run found nothing.
  - `MU2Launcher.exe` under Wine: WinHTTP signs in, the token is sealed by DPAPI (the file is a
    sealed blob, not the token), two files fetched into `C:\MU2check` and recorded.
  - **Wine here has no TLS** (no GnuTLS for its schannel): every HTTPS call fails with WinHTTP
    12157, so the Wine check runs over plain HTTP against a local web/server.py. Real Windows does
    TLS itself; TLS 1.2 and 1.3 are asked for outright.
- **The screens are drawn (2026-10-14)**, src/launcher/ui.cpp: one 960×540 window with no system
  frame (its own close and minimise; dragged by its top strip), the game's canvas and the game's
  slab looks copied from controls.cpp, so it links mu_gfx and none of the game. Sign in (email,
  password, stay signed in, the website's refusal on a red box), then one screen that checks,
  installs or updates (a bar, MB/s, time left, pause), and plays. Its two shaders, three fonts,
  the wallpaper and the logo are inside the exe (tools/cmake/embed.py), served to core::readFile.
  `--shot FILE --screen NAME` draws one screen in a hidden window: checked on the Mac (Metal) and
  as MU2Launcher.exe under Wine (Vulkan), 22 MB.
- **Live on protocol 31 (2026-10-14, the user: "deploy now").** The Windows release and the Mac
  app built from 3965dc81, the server deployed with `--accounts` (7466 sim checks, 22 characters
  kept). Under Wine the launcher reaches the box through tools/wine_https_relay.py, Wine here
  having no TLS: `wine MU2Launcher.exe --site http://127.0.0.1:8096`.
- **The full cycle, by the user under Wine (2026-10-14):** signed in on the live site, the whole
  release fetched (MU2.exe and 17 packs), Play: the server took the token as account 1, he made
  "windowskar", played Lorencia, left, and came back to the character screen with 1 character
  under `account000000001`. Log out and sign in again both worked.
- **Before it was tried by hand:** typing, clicking, a real sign-in and install through the window
  (the engine under it was checked headless). Next: the user tries it; then signing the exe,
  and L4.

**L4. The website offers the launcher.** The Windows button downloads `MU2Launcher.exe`, a few MB,
in place of the 1.5 GB zip.

- **Done 2026-10-14** (b5e2fb85): MU2Launcher.exe, stripped to 5.6 MB, at
  https://37.27.158.226/downloads/MU2Launcher.exe; the site's Windows button links it and shows
  "Windows 10 or 11, 64-bit · 5 MB". A new launcher goes up the same way: build, strip
  (`x86_64-w64-mingw32-strip`), rsync to /srv/mu2-web/downloads/.

**L5. The Mac launcher.** The same program, as a Mac app (the user: "we need also launcher for
macos, with install and everything").
- `./mu dist launcher` makes dist/launcher/MU2Launcher.app -- the binary, glfw in its Frameworks
  pointed at, Info.plist, signed ad hoc -- and MU2.dmg of it beside the Applications link to drag
  onto. The DMG goes to /srv/mu2-web/downloads/MU2.dmg and lights the site's Mac button, whose
  note reads "first launch: allow in Privacy & Security": on macOS 15 and later the right-click
  door is shut and that pane's "Open Anyway" is the only one -- all until there is a Developer
  ID to sign and notarise with, after which there is no door at all.
- The double-click installer file, MU2Install.command (Finder opens it in Terminal, whose
  "downloaded from the Internet" question has an Open button), works on older macOS and is
  served in /downloads/ -- **but macOS 27 refuses quarantined scripts outright**: a simulated
  browser download was refused by LaunchServices with error -128 and never ran. The page's Mac
  button therefore keeps the DMG with the Open Anyway instruction, and the Terminal one-liner
  (install-macos.sh) stays as the no-dialog route. After this, the only way to a clean
  double-click on current macOS is the Developer ID.
- `./mu release macos` puts dist/macos/MU2.app on the box under /game/macos/ exactly as the
  Windows client goes under /game/windows/ (release.py walks the bundle, exe
  MU2.app/Contents/MacOS/MU2). _CodeSignature is left out: macOS refuses to hard-link its files,
  the launcher's fetches need no signature, and an update could never replace it.
- The launcher itself needed little: its engine takes the platform from Place already. What was
  Mac's own: the install folder is ~/Applications, so MU2.app lands where a Mac player looks
  (defaultInstall); the probe is Metal (gpu_mac.mm -- Apple's own card or Radeon High, Intel
  integrated Medium, no driver date to be old); the machine report's cpu, ram and os come from
  sysctl. The token is still the owner-only file rather than the Keychain: the one piece of this
  step left.
- **Done 2026-10-14.** `release macos`: 3965dc81's app, 20 files (1.4 GB -- the packs are few and
  big) up under /game/macos/manifest.json; MU2.dmg (2.7 MB) up and named by /api/downloads; the
  packaged app's own --check against the live site probed "Apple M1 Pro, High graphics" and sent
  its machine report. A Mac install through the window -- sign in, MU2.app into ~/Applications,
  Play -- is the user's to make.

**L6. The machine's graphics.** The launcher reads what the machine has to draw with -- which
adapter the game will run on, the driver behind it and whether Vulkan is there -- sets a first
run's graphics to what that machine can draw, and says so when the driver is the thing in the way
(the user: "we also need that launcher understand which drivers is missing so game can run at
full capacity of pc with integrated graphic card"). src/launcher/gpu.h, gpu_win.cpp.
- The probe: DXGI's adapters, the one with video memory of its own preferred and the software
  adapter never (src/main.cpp exports the symbols that make Windows start a two-card machine on
  the real card); that adapter's display-class driver key for the driver's version and date
  (SetupAPI, matched by the adapter's own name); and vulkan-1.dll, the loader gfx/window.cpp's
  renderer opens. Intel (0x8086), NVIDIA (0x10de) and AMD (0x1002) each carry their download page.
- What it chooses: no Vulkan or no hardware adapter is Low; 2 GB or more of video memory is High
  and less (an integrated card sharing the machine's) Medium; a driver three years old or more
  costs one step, since a card on an old driver runs below what it is. On the first run only --
  when %APPDATA%\MU2\options.txt is not there yet -- the preset's graphics keys are written in
  the keys core/args.cpp's readOptions knows (engine.cpp firstRunGraphics), and nothing else:
  the run's own switches for the display, the cap and the volume stay. The player's options.txt
  is never touched again; his rows in the game's Options write it back whole.
- On the screens: the adapter and the driver along the top of Home, "Graphics set to N for this
  PC" under Up to date on the run that wrote it, and a red line with an "Update driver" link when
  the driver has no Vulkan or is three years old -- to the vendor's page (ui.cpp openUrl).
- `mu2_launcher --check` says what was probed and whether the graphics were set or kept.
- **Done 2026-10-14.** Built on the Mac and cross-compiled by mingw (`MU2Launcher.exe`, out/
  build-launcher-win). Under Wine DXVK's own DXGI answers E_FAIL (80004005), so the probe falls
  back to the display class -- which there names the host card: `gpu ok Apple M1 Pro, driver
  35.0.10.1000 of 2026-10-14, vulkan yes, Medium graphics`; the first run wrote options.txt
  ("first run set to Medium"), the second kept it ("kept, options.txt is there"). A REG_SZ
  DriverDate arrives month-day-year ("10-14-2026") and is brought to yyyy-mm-dd (dateOf). The
  Mac's own check says "no probe on this system" and sets nothing. The screens, as
  `--shot --screen ready`: the adapter and driver along the top, "Graphics set to High for this
  PC" under Up to date, and with a driver three years old the red line with "Update driver" and
  the preset held back a step to Medium. **Not yet run:** the DXGI path itself, which wants a
  real Windows machine (`MU2Launcher.exe --check` there).

**L7. The machine, told to the site.** Every run the launcher says what machine it opened on --
which card, which driver behind it, and what runs around it -- so the fleet the updates must fit
is read off data rather than guessed (the user: "when i launch launcher on other pc we send
information about that pc back to server"). engine.cpp reportMachine, web/server.py.
- `POST /api/machine`: `gpu, vendor, driver, date, vulkan, old, software, memory` (the probe's
  own words), `graphics` and `tuned` (the preset and whether this run set it), and `cpu, ram, os`
  -- the CPU's name from the processor key, total memory in MB, Windows as RtlGetVersion says it.
  **Nothing an account could be found by:** no email, no token, nothing about who signed in.
  Fire and forget: sent from the check job once a run, before the manifest is read; a report that
  does not land is a log line and nothing else; `--check` prints `machine ...`. The Mac sends
  none until L5 gives it a probe.
- The site keeps one JSON line per report in machines.jsonl beside the database (web/README.md):
  fields named and sized by MACHINE_KEYS, anything else dropped, 60 an hour per address
  (MACHINE_LIMIT), each line fingerprinted by card+cpu+ram+os so one machine counts once.
  `GET /internal/machines` (loopback only; Caddy answers 404 outside) counts the file: launches,
  machines, cards, drivers, presets and the last ten reports -- the answer to "which drivers are
  the players on, and what should the next update meet".
- **Done 2026-10-14.** Local end-to-end against `web/server.py` on 127.0.0.1:8096: a Wine
  MU2Launcher.exe --check sent `Apple M1 Pro, cpu "VirtualApple @ 2.50GHz", os 10.0.19043,
  ram 32767, graphics Medium` (`machine ok sent to the site`); a `junk` field sent by curl was
  dropped; /internal/machines counted both reports by fingerprint; the Mac's check said
  "no probe to send".
- **Deployed 2026-10-14.** web/deploy.sh put server.py on the box (mu2-web restarted, local 200)
  and the stripped exe -- 5,686,784 bytes -- at /srv/mu2-web/downloads/MU2Launcher.exe, which
  `/api/downloads` already names; a live POST /api/machine answered {ok} over the site's HTTPS
  and `/internal/machines` on the box counted it (the test line then taken out of the file).
  So the launcher on another PC reports the moment it opens. **Still open:** the launcher's own
  update (L3's line about a newer launcher in the manifest) is not built -- the manifest carries
  no launcher, so a machine holding an older MU2Launcher.exe only gets this one by downloading it
  again from the site.

**Later:** signing (Authenticode) before strangers download it (windows-client.md W7); the server
refusing machine keys once every player has the launcher; Edit account (change password) on the
website.

## Traps to expect

- The token must never be in a log line or a command line: the environment, not `--session`.
- A Windows exe that is running cannot be overwritten, but it can be renamed: the launcher's own
  update renames itself out of the way first.
- Protocol 31 must reach the box and the players together: the deploy of L1's server waits until a
  client that speaks 31 is out.
