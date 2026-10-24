# The Helper

MU Helper's auto-hunt, made simple and free. Asked for by the user on 2026-10-20. The approved
design is window A and the minimap's H on claude.ai/artifact/DaBoq8ccx79xmoqc2j5YuX. The hand is
`game/helper.h`, the window `game/ui/helper_window.h`, and the button `Minimap::drawHelper`.

## Using it

- **H**, or a click on the **H** on the minimap's rim (upper right, over Auto-play's button),
  starts it where he stands. Pressing either again stops it. The very first press opens the
  window instead, so the settings are seen before it runs.
- **Shift+H**, or a right click on the minimap's H, opens the window. Its foot holds Start, or
  while it helps the time, the kills, the things picked up and Stop.
- A click of his own on the world stops it, as it stops Auto-play. The Helper and Auto-play never
  run together: starting one stops the other.
- It does not start on a safe tile.
- H used to be a second key for hiding the HUD. F11 alone does that now.

## What it does

Every quarter second it works in MU Helper's order (`MuHelper.cpp`'s `Work`):

1. **Support.** Each skill on the bar set to *keep up* (an aura or a might) is recast when it
   lapses, *when hurt* (Heal) is cast under his health line, and *keep one out* (a summon) is
   recast when the summon is gone. Each is cast on himself, and only when it is ready, paid for
   and suited to his arm.
2. **Pickup.** It picks up what lies within its range of the start, plus 2 tiles: the window's
   ticks (Jewels, Runes, Legendary, Excellent, +Socket, All). It only takes what fits his bag,
   and never while something is fighting him. Zen goes to the purse on the kill, so it is not
   offered.

   **Pick up by name**, under the ticks, is MU Helper's *Add Extra Item* list (`MuHelper.cpp`'s
   `bPickExtraItems`), asked for on Discord on 2026-10-23. Up to 12 names of up to 20 letters
   each, typed in the field and added with Add or Enter. A click on a name takes it off. Anything
   lying whose name holds one of them, in any case, is picked whatever the ticks say, Boxes off
   included: "chaos" takes the Jewel of Chaos, "kundun" every Box of Kundun. While the field has
   the keyboard, H and the other keys are not the game's. Escape or a click elsewhere lets it go.
3. **Back.** With nothing in reach and *Walk back* on, he walks back to where it started.
4. **Fight.** It fights whatever is fighting him first, then the one it had, then the nearest
   monster within range of the start (Near 4, Mid 7, Far 11 tiles). The right button's skill (or
   the first attack on his keys, as Auto-play picks it) is the swing. Each attack key is thrown as
   its use says: *when ready*, or only with *2+* or *3+* monsters within 3 tiles of the one he
   fights.

All the while (MuHelper.cpp's `RepairEquipments`, MU Helper's *Repair Item*): with **Repair my
gear** on, the default, each worn piece at half its durability or under is mended by his own hand
where he stands, at the bag's self-repair price (two and a half times Hanzo's), whenever the purse
holds it. He does not stop fighting for it. It works from level 50, as the bag's hammer does; under
that the switch is veiled and reads "level 50". The Angel and the Imp do not mend and are passed
over. A piece he cannot pay for is left as it is; the Helper does not stop for it.

Each skill's kind decides the uses its row offers (`helperKind`, `helperUses`). The first is the
default.

| Kind | Uses |
|---|---|
| right button | always |
| one target | when ready, off |
| hits a crowd (ring, arc, line, fan, beam, splash, volley) | 3+ near, 2+ near, when ready, off |
| buff (boon or might) | keep up, off |
| heals | when hurt, off |
| summon | keep one out, off |
| moves him (blink, charge, mount), held key (channel) | never used |

A skill his weapon cannot throw reads "not with this weapon" and is passed over. An attack is
asked of the hand that swings, a boon of the arm, as the HUD's keys are (`SkillRow::suits`).

The window's **Drink below** meters are his auto-drink lines (`Play::setAutoDrink`). These are
the same lines the bag's bottle sets, and the realm drinks under them as it always does.

## When it stops by itself

- He dies.
- He stands on a safe tile.
- The bag has no free cell.
- The healing potions he set out with run out. A young hero who carries none may still be
  helped.
- An archer has no shots left.
- Another map: a new mode starts without it.

Each stop says why on the notice line.

## Kept

The settings are kept per character beside his save, as `Name.helper`: range, walk back, picks, the names to pick up, the repair,
whether the window has been seen, and each skill's use by MU's skill number.

## The pickup log

The window's third page, **Log** (Discord, 2026-10-23, Crylos: "a helper pick-up log ... with
quantities ... timestamp and remembers the last active helper, so i can access it even if i DC";
the user's design, claude.ai/artifact/BbDLeKxZrc15tAyTdF115X). What the run took into his bag and
purse, as the realm says it came (`What::Picked` for the hero, through `Play::takePicked`), so a
thing another player took first is never counted; his own picks while it helps count too, and so
does "N picked up" at the foot. The Zen is summed at the top. **Totals** is one row a kind, its
pieces summed, the rarest first; **Timeline** each thing with its hour, newest first, and the Zen
between two things as one gold line. Newer and Older page a long list.

Kept beside his save as `Name.helperlog`, a line written as each thing comes (`began`, `place`,
`item`, `zen`, `end`), so a run cut off is still there when he is played again. While it is off the
page reads **Last run** with how it ended: the stop's own reason, "Disconnected", "You left the
map", or "cut off, the game closed" when no `end` line was written. Clear lets it go; the next start
begins it afresh, and a followed quest's travel carries it on to the next map.

## Ours, not WebZen's

WebZen's helper (the 2011 update note) asked level 80, charged Zen every five minutes with a
gauge that climbed to stage 5, and its state was the server's (`C1 BF 51`). This one is the
client's own hand, like Auto-play, and costs nothing (the user: "helper has to be free").

## Tests

`./mu run --play --world lorencia --class 2 --level 60 --teach 20,21,22,41,18,19 --give
Sword02:1:W,Shield01:1:W --walk-to 200,125 --helper 25 --windows helper --fixed-dt 50 --frames
4800 --shot 1600 --offscreen --mute`

The `helper:` log lines show it starting, raising Defense, throwing Lunge and Uppercut at one
target and Cyclone only with 3 about, and walking back between fights. An unarmed hero's keys
are all "not with this weapon", as the HUD's are.
