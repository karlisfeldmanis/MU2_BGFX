# Armory prototype

The first working extractors and the clickable design, kept as found (2026-10-24) so the real
`tools/armory/` build can be grown from them. Paths inside still point at the session
scratchpad they were written in; they are reference, not the build.

- `gen_*.py`, `pack_models.py`: the extractors (see ARMORY_HANDOFF_PROMPT.md for what each makes).
- `sysb/dump.cpp`, `cardx/cards.cpp` + `cardx/build.sh`: dump programs that link the game's own code.
- `card_style.*`, `card_grain.png`, `cards_ref/`, `card_compare.png`: the game's item card, measured.
- `harness.js`, `check.js`: the page logic run headlessly against the JSON.
- `design/Main.dc.html`: the design canvas (also at claude.ai/artifact/Gre39R3PX3sVpDv6R5JZAE).
