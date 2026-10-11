"""The placements cook's world anchors: MU's hidden boxes that throw something, by world.

Moved out of tools/cook.py on 2026-10-11 to pay for Aida's waterfall spray under the size ratchet
(docs/order-plan.md). cook.py imports these: which hidden models become world anchors and of
which kind (content::EmitterKind), the light each kind throws, and the hidden models a world
holds back or never draws.
"""

# MU's hidden anchors in Lorencia: MoveObject's WD_0LORENCIA calls CreateFire(0|1|2, o, 0,0,0)
# on MODEL_LIGHT01..03 and hides the holder (ZzzObject.cpp). Kind 1 is a fire, 4 a smoke.
ANCHOR_KINDS = {"Light01": 1, "Light02": 4, "Light03": 4}
# And world 74's, the character screen's, which are placements with no mesh rather than hidden
# ones: GMEmpireGuardian4 hides types 79 and 132 (HiddenMesh = -2) and hangs a fire off 79 --
# a light sprite and flame particles, RenderObjectVisual case 79 -- and smoke off 132 (case 132).
# Type 129's blue cloud is not carried: the user keeps that sky black.
# The Lost Tower's 95 Flame vents (Object25, hidden): MoveObject's WD_4LOSTTOWER arm lights
# the wizard's Flame on one a frame in 64 (ZzzObject.cpp:4020-4024). Kind 5, a vent: Lamps
# rolls it and Play burns it (game/world/lamps.h).
# Blood Castle's nine Object38 (type 37, no .bmd): MU's emitters, which roll BITMAP_ADV_SMOKE
# and BITMAP_CLOUD up from the court, the bridge, the gap and the castle every other frame on a
# four-tick cadence (ZzzObject.cpp:3227-3242). Kind 4, the chimney's rising smoke.
ANCHOR_KINDS_BY_WORLD = {"charscene": {"Object80": 1, "Object133": 4},
                         "losttower": {"Object25": 5},
                         "bloodcastle": {"Object38": 4},
                         # Atlans's 845 bubble vents, Object23 (type 22, hidden): MoveObject's
                         # WD_7ATLANSE arm throws a BITMAP_BUBBLE a frame for half of every
                         # four seconds (ZzzObject.cpp:4055-4063). Kind 6, a bubble vent: no
                         # light; game/world/bubbles.h rolls them.
                         "atlans": {"Object23": 6},
                         # Tarkan's hidden emitters (RenderObjectVisual's WD_8TARKAN arm,
                         # ZzzObject.cpp:2994-3065), drawn by game/world/desert_vents.h: 7 the
                         # falling sand of Object71, 8 the steam vents of Object77, 9 the sand
                         # geysers of Object84, 10 the cyan glow sprites of Object64. None lights.
                         # 11 Object61's standing dust clouds (SubType 6, which never dies).
                         # And Object05, whose .bmd the client does not ship, but whose MoveObject
                         # arm still adds its white pulsing terrain light (ZzzObject.cpp:4095-4106):
                         # a lamp, 0.
                         "tarkan": {"Object71": 7, "Object77": 8, "Object84": 9, "Object64": 10,
                                    "Object61": 11, "Object05": 0},
                         # Icarus's cloud banks, Object01-06 (types 0-5, hidden): each throws
                         # 20 or 10 standing BITMAP_CLOUD puffs (ZzzObject.cpp:3090-3169), drawn
                         # by game/world/sky_clouds.h. Kind 12 + the type. None lights.
                         "icarus": {"Object01": 12, "Object02": 13, "Object03": 14,
                                    "Object04": 15, "Object05": 16, "Object06": 17},
                         # Aida's waterfall spray boxes (RenderAidaObjectVisual types 56-58,
                         # GMAida.cpp:166-183), drawn by game/world/waterfall_spray.h: 18 the
                         # falling drops of Object57, 19 the specks bursting off Object58, 20
                         # the mist of Object59. None lights; each carries the box's Scale,
                         # which MU adds to every particle's (SPRAY_KINDS).
                         "aida": {"Object57": 18, "Object58": 19, "Object59": 20,
                                  # and Object68's small fires on rocks and ruins (type 67,
                                  # GMAida.cpp:239-242), game/world/small_fires.h,
                                  # and Object71's skull ghosts (type 70, :243-252),
                                  # game/world/skull_ghosts.h.
                                  "Object68": 21, "Object71": 22}}
# The anchor kinds whose particles grow with the box: the box's Scale rides in the emitter's
# `high`, its reach left 0 so Lamps lights nothing.
SPRAY_KINDS = (18, 19, 20, 21, 22)
BRAZIER_BOWLS = {"charscene": ("Object15",)}
# World 74's meshes MU never draws: GMEmpireGuardian4::MoveObject sets HiddenMesh = -2 on types
# 79 to 86 and 129 to 132 (models Object80.. and Object130..133). Type 129's cloud anchor is
# Object130, whose green `angeflo_r` glow slabs stood on the wall tops until this was read.
#: Hidden placements a world shows later, cooked with flag 8 so the town holds them back until
#: the game clears it: Blood Castle's lowered drawbridge, Object10's deck and Object11's chains,
#: which MuMain shows as Object37 lands (ActionObject, ZzzObject.cpp:107-123; game/world/
#: drawbridge.h).
SHOWN_LATER = {"bloodcastle": {"Object10", "Object11"}}

HIDDEN_BY_WORLD = {"charscene": {"Object80", "Object81", "Object83", "Object84", "Object85",
                                 "Object86", "Object87", "Object130", "Object131", "Object132",
                                 "Object133"}}
# What each throws: CreateFire(0)'s own (ZzzEffectFireLeave.cpp:61) -- L = rand[0.6, 1.1),
# colour (L, 0.6L, 0.4L), range 4 -- with index.json's flicker for every other fire. Smoke
# throws no light. Colour, low, high, reach, hz, smoothing.
ANCHOR_LIGHT = {# A lamp: Tarkan's Object05, white, sin(WorldTime*0.002)*0.35+0.65 over three tiles,
                # carried as a slow wander between 0.3 and 1, as Object08's.
                0: ((1.0, 1.0, 1.0), 0.3, 1.0, 3.0, 0.32, 1.0),
                1: ((1.0, 0.6, 0.4), 0.6, 1.1, 4.0, 3.0, 0.12),
                4: ((1.0, 1.0, 1.0), 0.0, 0.0, 0.0, 0.0, 0.0),
                # A vent's light is the Flame's, (1, 0.4, 0) over three tiles (MoveHandlers.cpp:
                # 1815-1816), and dark until it burns: Lamps drives its level.
                5: ((1.0, 0.4, 0.0), 0.0, 0.0, 3.0, 0.0, 0.0),
                # A bubble vent throws no light.
                6: ((1.0, 1.0, 1.0), 0.0, 0.0, 0.0, 0.0, 0.0),
                # Nor do Tarkan's sand, steam, geysers and glow sprites.
                7: ((1.0, 1.0, 1.0), 0.0, 0.0, 0.0, 0.0, 0.0),
                8: ((1.0, 1.0, 1.0), 0.0, 0.0, 0.0, 0.0, 0.0),
                9: ((1.0, 1.0, 1.0), 0.0, 0.0, 0.0, 0.0, 0.0),
                10: ((1.0, 1.0, 1.0), 0.0, 0.0, 0.0, 0.0, 0.0),
                11: ((1.0, 1.0, 1.0), 0.0, 0.0, 0.0, 0.0, 0.0),
                # Nor Icarus's cloud banks, nor Aida's spray, small fires and skull ghosts
                # (MU's types 67 and 70 light nothing: no AddTerrainLight in their arms).
                **{k: ((1.0, 1.0, 1.0), 0.0, 0.0, 0.0, 0.0, 0.0) for k in range(12, 23)}}
