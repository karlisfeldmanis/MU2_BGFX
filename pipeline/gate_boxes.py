"""The gates' boxes the index keeps open, by the server's map number.

Moved out of pipeline/index.py on 2026-10-11 to pay for Aida's waterfall sheets under the size
ratchet (docs/order-plan.md). index.py imports it for the solid-stamping pass.
"""

#: The gates' boxes, by the server's map number: OpenMU Version075 Gates.cs, enter and exit
#: both, the same numbers src/sim/gates.cpp walks by. A gate is a tile MU left open to be
#: walked onto, so the solid-stamping pass must not close one -- Lorencia's west gate to Devias,
#: (5, 38) to (6, 41), sits between boulders and a stone wall whose declared footprints covered
#: all of it, and a hero walking west stopped a tile short of Devias.
GATE_BOXES_BY_MAP = {
    0: [(5, 38, 6, 41), (7, 38, 8, 41), (213, 246, 217, 247), (213, 244, 217, 245)],
    # The Dungeon's sixteen, Gates.cs:119-125 and 186-192: the way out and its landing, and the
    # three floors' stairs, each an enter box beside its exit box.
    1: [(107, 247, 110, 247), (108, 248, 109, 248), (239, 149, 239, 150), (240, 149, 241, 151),
        (231, 126, 234, 127), (232, 127, 233, 128), (2, 17, 2, 18), (3, 16, 6, 17),
        (5, 34, 6, 34), (5, 32, 7, 33), (3, 83, 4, 86), (2, 84, 2, 85),
        (29, 125, 30, 126), (29, 127, 30, 127)],
    2: [(244, 34, 245, 37), (242, 34, 243, 37)],
    3: [(148, 3, 155, 4), (148, 5, 155, 6),
        # The way to Atlans and its landing, Gates.cs:135 and 205. docs/atlans-port.md.
        (242, 240, 245, 243), (240, 240, 241, 243),
        # The door to Aida and its landing back, Season Six Gates.cs:534 and 170.
        # docs/aida-port.md.
        (220, 30, 226, 30), (220, 31, 226, 34)],
    # Atlans's way back to Noria and its landing, Gates.cs:149 and 206. docs/atlans-port.md.
    7: [(9, 9, 11, 12), (14, 12, 15, 13),
        # The door to Tarkan and its landing back, Season Six Gates.cs:524 and 194 (WebZen's
        # gate.txt 53 and 56). docs/tarkan-port.md.
        (14, 225, 15, 230), (16, 225, 17, 230)],
    # Tarkan's way back to Atlans, its landing from it and its spawn gate 57, the safe hall
    # (WebZen's gate.txt 55, 54, 57; Season Six Gates.cs:525, 200, 198). docs/tarkan-port.md.
    8: [(246, 40, 247, 44), (248, 40, 251, 44), (187, 54, 203, 69)],
    # The Lost Tower's door to Icarus on floor 7's south end and its landing back (WebZen's
    # gate.txt 62 and 65; Season Six Gates.cs:526, 181). Its stairs are not here yet: they were
    # walked open before this row. docs/icarus-port.md.
    4: [(17, 250, 19, 250), (17, 249, 19, 249)],
    # Icarus's way back and its arrival (gate.txt 64 and 63; Gates.cs:527, 213).
    10: [(14, 12, 16, 12), (14, 13, 16, 13)],
    # Aida's way back to Noria, its landing from it, its spawn gate 119 (the safe hall) and
    # the Aida 2 landing 140 (Season Six Gates.cs:535, 267, 265, 266). docs/aida-port.md.
    33: [(74, 9, 74, 13), (76, 9, 78, 16), (82, 8, 87, 14), (186, 173, 190, 177)],
}
