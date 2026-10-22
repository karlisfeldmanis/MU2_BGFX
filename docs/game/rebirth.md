# Rebirth

The reset. Ours: MU 0.75 has none. The rules were settled with the user on 2026-10-22.

## The rules (sim/rebirth.h)

- **Where:** Lugard, the Fallen Knight (MuMain's MONSTER_LUGARD, NPC 540), in Devias at 207,45.
  A Talk opens his window (`Realm::rebirthing`); any order shuts it.
- **Asks:** level 400 (`kRebirthLevel`, the top) and 10,000,000 Zen times the rebirth's number:
  10M for the first, 20M for the second, and so on (`rebirthZen`). No cap.
- **Takes:** the body's levels. Level 1, no experience.
- **Keeps:** the four stats, the points in hand, the skills, the talents.
- **Accumulative:** every level on the way back up gives its stat points again. Talent points
  count every life's levels (`talentPoints(kin, level, rebirths)`), so the first rebirth shows
  79 -> 80 for a knight at 400.
- **Gear:** what he wears and could not wear at level 1 goes into the bag, as Reset Points does.
  A bag without room refuses the rebirth whole and nothing changes.
- **Says:** `What::Reborn` (a: the count, b: pieces to the bag), to everyone.

## The wire

- `Command::Kind::Rebirth`, answered with the new count or -1 (`refusal()` says why);
  `Command::Window::Rebirth` for Close.
- `Body::rebirths` and `HeroRecord::rebirths`, in a character's bytes from kept layout 13.
- Protocol 47. Client and servers release together.

## The server (server/src/world.cpp)

On `Reborn`: the character is written at once, as a trade is, and the server tells Global
(`announceRebirth`, net::rebirthLine): "[1. Global] Ken has been reborn (Rebirth I)." in gold.

## The client

- Lugard's window (`game/ui/rebirth_window.h`): the canvas's layout A, at the quest dialog's
  place. Be reborn asks once more ("Kneel before Lugard? It cannot be undone.").
- On `Reborn`: the rite where he stands (`fx/rite.h`, `Play::startRite`), for him or for any player
  seen; the message (`ui/reborn.h`) as he rises, his own only.
- Bench: `--rite F` plays the show alone; a save with `"level": 400` and the Zen, a click on Lugard
  and on Be reborn twice runs the real thing offscreen.

## Not yet

- Lugard's voiced lines (content/voice/ref/pending_pages.json).
- The count elsewhere: the character window, the ranking, over his head.
