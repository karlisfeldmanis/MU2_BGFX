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

**L2. Releases on the box.** `./mu release windows` puts `dist/windows/MU2/` on the box under
`/srv/mu2-web/game/windows/`, every file named by its SHA-256, and writes `manifest.json` last:
the version, the protocol, and each file's path, size and hash. Only new files travel. Caddy
serves them, with ranges, so a broken download resumes.

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

**L4. The website offers the launcher.** The Windows button downloads `MU2Launcher.exe`, a few MB,
in place of the 1.5 GB zip.

**L5. The Mac launcher.** The same program: NSURLSession for HTTPS, the Keychain for the token,
`~/Applications/MU2` for the game. Later, as the user said.

**Later:** signing (Authenticode) before strangers download it (windows-client.md W7); the server
refusing machine keys once every player has the launcher; Edit account (change password) on the
website.

## Traps to expect

- The token must never be in a log line or a command line: the environment, not `--session`.
- A Windows exe that is running cannot be overwritten, but it can be renamed: the launcher's own
  update renames itself out of the way first.
- Protocol 31 must reach the box and the players together: the deploy of L1's server waits until a
  client that speaks 31 is out.
