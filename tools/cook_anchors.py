"""The placements cook's world anchors: MU's hidden boxes that throw something, by world.

Moved out of tools/cook.py on 2026-10-11 to pay for Aida's waterfall spray under the size ratchet
(docs/order-plan.md). cook.py imports these: which hidden models become world anchors and of
which kind (content::EmitterKind), the light each kind throws, and the hidden models a world
holds back or never draws.
"""
import os
import sys

# pipeline/world_manifest.py: each world's own anchors are its manifest's `pipeline` knobs
# (anchor_kinds, brazier_bowls, shown_later, hidden_models; sprint D's last slice), each with the
# comment that stood here as its note. The comments below say what each kind is.
sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "pipeline"))
import world_manifest  # noqa: E402

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
ANCHOR_KINDS_BY_WORLD = world_manifest.by_world("anchor_kinds", dict)
# The anchor kinds whose particles grow with the box: the box's Scale rides in the emitter's
# `high`, its reach left 0 so Lamps lights nothing.
SPRAY_KINDS = (18, 19, 20, 21, 22)
# A spray kind's `high` carries its box's Scale, so it cannot carry a light's level too. One that
# should light is given a second anchor beside it of a light-only kind: Aida's small fires, MU's
# type 67, which lights nothing in MU -- ours (the user, 2026-10-11: 'fire has to make emiter').
LIGHT_TWINS = {21: 23}
BRAZIER_BOWLS = world_manifest.by_world("brazier_bowls", tuple)
# World 74's meshes MU never draws: GMEmpireGuardian4::MoveObject sets HiddenMesh = -2 on types
# 79 to 86 and 129 to 132 (models Object80.. and Object130..133). Type 129's cloud anchor is
# Object130, whose green `angeflo_r` glow slabs stood on the wall tops until this was read.
#: Hidden placements a world shows later, cooked with flag 8 so the town holds them back until
#: the game clears it: Blood Castle's lowered drawbridge, Object10's deck and Object11's chains,
#: which MuMain shows as Object37 lands (ActionObject, ZzzObject.cpp:107-123; game/world/
#: drawbridge.h).
SHOWN_LATER = world_manifest.by_world("shown_later", set)

HIDDEN_BY_WORLD = world_manifest.by_world("hidden_models", set)
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
                **{k: ((1.0, 1.0, 1.0), 0.0, 0.0, 0.0, 0.0, 0.0) for k in range(12, 23)},
                # The small fires' own light (LIGHT_TWINS): CreateFire(0)'s, kind 1's colour and
                # flicker, over three tiles, a torch rather than a bonfire.
                23: ((1.0, 0.6, 0.4), 0.6, 1.1, 3.0, 3.0, 0.12)}
