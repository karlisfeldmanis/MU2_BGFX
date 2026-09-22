"""Writes the list of what has been built, and which of it is a character.

    python3 pipeline/index.py source workshop

The viewer cannot work this out for itself. All it can see is .glb files under build/, and
a character is not a file — the Dark Knight is five of them that happen to belong together,
and nothing about `ArmorMale10.glb` says whether it is a piece of somebody or an item
sitting on a shelf.

That distinction is only in the asset files, so it is written down here, once, into
build/index.json. The viewer reads it and gets two tabs out of it: things you wear, and
things you hold.

Paths are relative to the build root rather than absolute, because the exported
application is meant to be moved around and a path recorded from this machine would not
survive the trip.
"""

import json
import math
import shutil
import sqlite3
import struct
import wave
import sys
from pathlib import Path

import numpy as np
from PIL import Image

#: The two bits of MU's attribute grid that stop a walker. See terrain.py, which writes it.
NO_MOVE = 0x0004
NO_GROUND = 0x0008

#: The types a character walks onto and poses against, whose tiles this pass must leave open.
#:
#: Named here rather than imported, because terrain.py is a script run on its own and index.py
#: does not otherwise depend on it. The list is the same one, and terrain.OPERABLE_BY_MAP
#: carries the argument for it in full.
#:
#: Keyed by the server's map number, because a type number means a different object in every
#: world: 8 is Noria's sitting stump and Lorencia's ninth tree, and stamping either map's
#: seats open on the other's grid would open tiles MU closed.
OPERABLE_BY_MAP = {
    0: {6, 133, 145, 146},
    3: {8, 38},
}

#: What the walker actually compares, which is a threshold and not a bit test: a tile is open
#: when its word, read as a number, is below CHARACTER. Three flags describe a tile rather
#: than obstruct it and are cleared first. The rule is MU's own — see shared/Route.cs, which
#: is the thing this has to agree with, and docs/movement.md for why it is a threshold.
CHARACTER = 0x0002
ACTION = 0x0020
HEIGHT = 0x0040
CAMERA_UP = 0x0080

#: Art the viewer draws itself rather than loading as a model, copied into the build.
#:
#: The move marker is not an item and has no .glb. It is one of MU's own textures drawn on
#: a quad, the way the client draws it, so there is nothing for the item pipeline to do to
#: it — no unwrap, no bake, no material. What it does need is to be *in* the build, because
#: the viewer finds build/ and has no idea where assets/ is.
EFFECTS = {
    # The damage number's digits, which are art and not a font.
    #
    # Data/Interface/FontTest.OZT, 256x32: ten 16-pixel digits along the top and the word
    # `Miss` beneath them. MU draws a damage number as a row of textured quads out of this
    # sheet -- RenderNumberPoints in ZzzEffectPoint.cpp -- so the number needs no text
    # system at all, and a miss is a separate sprite rather than a zero.
    #
    # Listed here because nothing else was reaching it: the interface art this table carries
    # is what the client draws itself, and the numbers had never been asked for. Without it
    # MU2_BGFX's sprint 6 would have had to invent digits or borrow a typeface, which for a
    # thing MU ships as a painting would be a guess dressed as a port.
    "damage_digits": "interface/damage_digits.png",

    "move_marker": "effects/movetarget/cursorpin01.png",

    # The two ground rings the move marker turns, and the one that breathes.
    #
    # BITMAP_SUMMON_IMPACT, which is Data/Effect/empact01.jpg, and it is the part of the
    # marker anybody actually remembers: a runic ring, drawn twice at 0.8 and 0.96 tiles and
    # turned in opposite directions - `RenderTerrainAlphaBitmap(BITMAP_SUMMON_IMPACT, x, y,
    # Scale, Scale, vLight, o->HeadAngle[1])` and again at `Scale * 1.2f` with HeadAngle[2].
    # It arrives through BITMAP_MAGIC subtype 11, which is created by the pin on the frame the
    # pin is: the name says magic and the texture it draws is not magic_ground at all.
    #
    # cursorpin02 is BITMAP_TARGET_POSITION_EFFECT2, the ring that opens and closes between
    # 1.8 and 0.8 tiles for the whole thirty frames. Both are textures on the terrain rather
    # than geometry, which is why they are here beside the marker's own sheet and not with the
    # models.
    "move_rings": "effects/movetarget/empact01.png",
    "move_pulse": "effects/movetarget/cursorpin02.png",

    # MU's own three pointers, for the crowd.
    #
    # The client draws all of these whole at 24x24 from a 2px offset - ZzzInterface.cpp's
    # RenderCursor, BITMAP_CURSOR for the normal one, +2 for the attack and +3 for the hand
    # that picks something up - so the tip is the hot spot and there is no animation on any of
    # them. CursorTalk is the one that steps through quadrants on a six-frame cycle, and it is
    # not one of these.
    #
    # RenderCursor tests them in its own order and the get cursor comes *before* the attack
    # one: SelectedItem != -1 is above SelectedCharacter, so a drop lying under a monster
    # shows the hand and not the sword. See Pointer.
    "cursor": "interface/cursor.png",
    "cursor_attack": "interface/cursor_attack.png",
    "cursor_get": "interface/cursor_get.png",

    # And the fourth, over a townsperson. The one that animates: BITMAP_CURSOR + 4 is a
    # two-by-two sheet and RenderCursor steps its quadrants on a six-frame cycle at
    # WorldTime * 0.01 - ten steps a second - which is why it is 64 square where the other
    # three are 32. See Pointer for the walk through the quadrants, which is not 0-1-2-3.
    "cursor_talk": "interface/cursor_talk.png",

    # And the two over a thing you can strike a pose against.
    #
    # BITMAP_CURSOR + 6 is Interface/CursorLeanAgainst.tga and + 7 is CursorSitDown.tga, and
    # RenderCursor picks between them by what the hovered object *is* rather than by what it
    # does: the lean cursor is shown for Lorencia's pose box, Dungeon's 60, Devias' 91 and
    # Noria's 38, and the sit cursor for every other operable. Noria's 38 is the odd one -
    # it plays the healing action and still shows the lean pointer, which is the client's
    # own list and not a derivation. See Poses.
    "cursor_lean": "interface/cursor_lean.png",
    "cursor_sit": "interface/cursor_sit.png",

    # The three right-hand windows - inventory, character, shop - as MuDream skins them.
    #
    # The layout stays CNewUIMyInventory's, CNewUICharacterInfoWindow's and CNewUINPCShop's,
    # to the pixel: 190x429, the equipment slots at SetEquipmentSlotInfo's own sizes, a bag
    # cell 21x21 drawn on a 20 pitch. Only the paint is MuDream's, which is what MuDream
    # itself did to the bottom frame - see the LegendHUD block below. The art comes off the
    # GFx sheets in the same folder as the TopMenu the HUD's side buttons are cut from, and
    # it is written at twice MU's size so it lands on Panel.Scale's screen one pixel for one
    # rather than being resampled up from a 640x480 original. See Panel.cs and Bag.cs.
    "bag_back": "interface/win_back.png",
    "bag_crest": "interface/win_crest.png",
    "bag_plate": "interface/win_plate.png",
    "bag_cell": "interface/win_cell.png",
    "bag_zen": "interface/win_zen.png",
    "bag_field": "interface/win_field.png",
    "bag_close": "interface/win_close.png",
    "bag_plus": "interface/win_plus.png",

    # The ghosts drawn in empty worn slots - the boot outline in the boot slot - which are
    # most of what makes the equipment panel readable. m_EquipmentSlots[i].dwBgImage.
    "bag_slot_pet": "interface/win_slot_pet.png",
    "bag_slot_helm": "interface/win_slot_helm.png",
    "bag_slot_wings": "interface/win_slot_wings.png",
    "bag_slot_weapon_left": "interface/win_slot_weapon_left.png",
    "bag_slot_weapon_right": "interface/win_slot_weapon_right.png",
    "bag_slot_armour": "interface/win_slot_armour.png",
    "bag_slot_pants": "interface/win_slot_pants.png",
    "bag_slot_gloves": "interface/win_slot_gloves.png",
    "bag_slot_boots": "interface/win_slot_boots.png",
    "bag_slot_amulet": "interface/win_slot_amulet.png",
    "bag_slot_ring": "interface/win_slot_ring.png",

    # MU's system menu, the strip Escape brings up. CNewUIWindowMenu stitches it from three
    # pieces over the same message-box background the panels use: a 112x45 cap, as many
    # 112x15 middles as the entry count asks for, and a cap at the bottom, with an 82x2 rule
    # between entries and a pair of 6x9 arrows marking the one under the pointer. See Menu.cs.
    "menu_top": "interface/newui_commamd01.png",
    "menu_middle": "interface/newui_commamd02.png",
    "menu_bottom": "interface/newui_commamd03.png",
    "menu_rule": "interface/newui_commamd_line.png",
    "menu_arrow_left": "interface/newui_arrowl.png",
    "menu_arrow_right": "interface/newui_arrowr.png",

    # MU's four buttons along the foot - close, repair, personal store, expand - are gone
    # with the row that held them. Three of them opened nothing in this world and were drawn
    # only so that dropping the behaviour in later would move nothing, which is a defensible
    # thing to do with a painted panel and not with a lit one: three dead controls under a
    # live one read as a broken row. The fourth was a second close beside the head's X. The
    # foot is the Zen strip now - see Bag.MoneyStrip.

    # The frame along the bottom of the screen: CNewUIMainFrameWindow as MuDream.online
    # skins it, its LegendHUD sheets decoded whole by hud_cut.py. One base plate with the
    # eleven slots, their keys and the two diamond sockets painted in; a sheet of sixty
    # gem frames for each socket; a bar and its empty twin for shield and ability; the
    # experience rail and its fill; and the thirteen menu icons, rest and lit.
    "hud_base": "interface/hud_base.png",
    "hud_gem_life": "interface/hud_gem_life.png",
    "hud_gem_mana": "interface/hud_gem_mana.png",
    "hud_bar_shield": "interface/hud_bar_shield.png",
    "hud_bar_shield_empty": "interface/hud_bar_shield_empty.png",
    "hud_bar_ability": "interface/hud_bar_ability.png",
    "hud_bar_ability_empty": "interface/hud_bar_ability_empty.png",
    "hud_level_track": "interface/hud_level_track.png",
    "hud_level_fill": "interface/hud_level_fill.png",
    "hud_slot_selected": "interface/hud_slot_selected.png",
    "hud_slot_hover": "interface/hud_slot_hover.png",

    # The open skill list's cell, and the same cell lit for the skill in hand. MuMain's own
    # newui_skillbox and newui_skillbox2, which LegendHUD has no equivalent of because it
    # paints the frame's boxes into the base plate - see hud_cut.py.
    "hud_skill_box": "interface/hud_skill_box.png",
    "hud_skill_box_chosen": "interface/hud_skill_box_chosen.png",

    # The four side buttons: a disc cut off MuDream's TopMenu sheet with its centre
    # cleared, and MuMain's own icons on it, two states each - rest above, lit below.
    "hud_disc": "interface/hud_disc.png",
    "hud_button_menu": "interface/hud_button_menu.png",
    "hud_button_chat": "interface/hud_button_chat.png",
    "hud_button_inventory": "interface/hud_button_inventory.png",
    "hud_button_character": "interface/hud_button_character.png",

    # The chat window: the log's grab bar and scrollbar above, the bar of ten buttons
    # below, and the lit state of each button to lay over the one the bar paints dark.
    # MuMain's own newui_chat set, which MuDream ships unrepainted - see hud_cut.py, and
    # Log.cs for where each goes.
    "chat_back": "interface/chat_back.png",
    "chat_grip": "interface/chat_grip.png",
    "chat_scroll_top": "interface/chat_scroll_top.png",
    "chat_scroll_middle": "interface/chat_scroll_middle.png",
    "chat_scroll_bottom": "interface/chat_scroll_bottom.png",
    "chat_thumb": "interface/chat_thumb.png",
    "chat_on_normal": "interface/chat_on_normal.png",
    "chat_on_party": "interface/chat_on_party.png",
    "chat_on_guild": "interface/chat_on_guild.png",
    "chat_on_gens": "interface/chat_on_gens.png",
    "chat_on_whisper": "interface/chat_on_whisper.png",
    "chat_on_system": "interface/chat_on_system.png",
    "chat_on_log": "interface/chat_on_log.png",
    "chat_on_frame": "interface/chat_on_frame.png",
    "chat_btn_size": "interface/chat_btn_size.png",
    "chat_btn_alpha": "interface/chat_btn_alpha.png",

    # The character screen's eight, MuMain's OpenCharacterSceneData set by way of MuDream —
    # see lobby_cut.py for what each is and its size, and Lobby.cs for where each is drawn.
    "lobby_strip": "interface/lobby_strip.png",
    "lobby_class": "interface/lobby_class.png",
    "lobby_create": "interface/lobby_create.png",
    "lobby_menu": "interface/lobby_menu.png",
    "lobby_connect": "interface/lobby_connect.png",
    "lobby_delete": "interface/lobby_delete.png",
    "lobby_balloon": "interface/lobby_balloon.png",
    "lobby_deco": "interface/lobby_deco.png",
    "lobby_msg_back": "interface/lobby_msg_back.png",
    "lobby_msg_field": "interface/lobby_msg_field.png",
    "lobby_ok": "interface/lobby_ok.png",
    "lobby_cancel": "interface/lobby_cancel.png",
    # The picked figure's three, which are not interface art: the runic disc under it and
    # the two particles thrown off it. See lobby_cut.py's EFFECTS.
    "lobby_aurora": "effects/lobby/gmmzine.png",
    "lobby_blob": "effects/lobby/chasellight.png",
    "lobby_spark": "effects/lobby/impack03.png",

    # The fire a brazier gives off, and its smoke.
    #
    # Twenty-five of Lorencia's placements are Light01 to Light03, which the client marks
    # HiddenMesh and never draws: MoveObject calls CreateFire on them and hides the anchor in
    # the same breath, so what a player sees there is flame and never the thing carrying it.
    # These are the two sheets it spawns, copied in from MuMain like everything else here —
    # Fire01 is four 64-pixel frames in one 256 strip and smoke02 is a single 64 square.
    "fire": "effects/fire/fire01.png",
    "smoke": "effects/fire/smoke02.png",

    # BITMAP_SMOKE itself, which is not the dust's sheet: smoke01.jpg is a grey wisp on
    # black with no alpha, and it is what a Bull Fighter snorts. See Snort.
    "smoke01": "effects/fire/smoke01.png",

    # What a meteor leaves where it lands. See Meteor.
    #
    # BITMAP_EXPLOTION, which ZzzOpenData loads from Effect/Explotion01.jpg: a 256 square
    # holding a 4x4 grid of sixteen frames of fireball, of which the client ever shows ten -
    # `o->Frame = (20 - LifeTime) / 2` over a life of twenty. Drawn at the sheet's own full
    # width, which is 256 units and so two and a half metres of fire.
    #
    # A JPEG with no alpha, cut out of an additive pass by its own black, as the flame and
    # streak sheets are. Not explotion01mono.jpg, which is BITMAP_EXPLOTION_MONO and a
    # different picture for a different set of skills.
    #
    # The trail's own embers are BITMAP_FIRE and want no entry of their own: that is the
    # brazier's four-frame strip, already here as "fire", and the meteor lays one of those
    # down behind it every reference frame.
    "explosion": "effects/meteor/explotion01.png",

    # And what the wizard's Flame burns with. See client/core/Flame.cs.
    #
    # BITMAP_FLAME, which ZzzOpenData loads from Effect/Flame01.jpg with GL_CLAMP_TO_EDGE - a
    # 64 square of orange plume on black, and the clamp is the point: the same sheet is
    # stretched over two tiles of terrain as the spell's scorch, where a wrap would tile it
    # four times over.
    #
    # One sheet doing two jobs, which is MU's own arrangement rather than a saving here: the
    # effect throws six particles of this a frame and paints the same picture on the ground
    # under them every frame, through RenderTerrainAlphaBitmap.
    "flame": "effects/flame/flame01.png",

    # What an elite monster looks at you with. See Eyes.
    #
    # BITMAP_SHINY + 3, which ZzzOpenData loads from Effect/eye01.jpg, and it is the whole
    # answer to a question that has been guessed at twice: the Elite Bull Fighter is not
    # tinted, and nothing about it is recoloured. It is the plain model with two of these
    # drawn over its eye bones, and the red is *in the sheet* - a 32x16 almond of red on
    # black, mean chroma 87 in the channel and under one in the other two. RenderEye passes
    # a grey light, so the sheet is the only colour in the effect.
    #
    # A JPEG with no alpha, cut out of an additive pass by its own black the way the streak
    # and flare sheets are.
    "eye": "effects/eyes/eye01.png",

    # BITMAP_LIGHT, which ZzzOpenData loads from Effect/flare01.jpg: a soft white star on
    # black, 64 square. Not Flare.OZJ, which is the Energy Ball's. The Chain Scorpion carries
    # one on the tip of its tail in its own colour - see Entry.Glints and ChainScorpion01.
    "light": "effects/light/flare01.png",

    # What comes off something that has just been hit. See Wounds.
    #
    # The client throws blood at every landed blow and strikes sparks only off a sword skill,
    # which is not a distinction the art makes for it — these are three sheets out of Effect/
    # and nothing in them says which weapon found the target.
    #
    # blood.tga is BITMAP_BLOOD + 1, a 128 square holding four 64-pixel quadrants of splash
    # whose alpha fades from 160 in the first to 58 in the fourth, so the sheet does the
    # thinning itself. Spark02 is BITMAP_SPARK and is four pixels square — a mote, drawn a
    # couple of centimetres wide. Spark03 is BITMAP_SPARK + 1, the 32-pixel pop at the moment
    # of contact.
    #
    # Not blood01.tga, which is BITMAP_BLOOD and a different thing: the decal CreateBlood lays
    # at the head bone of something that has died, rather than anything a live monster sheds.
    "blood": "effects/hit/blood.png",
    "spark": "effects/hit/spark02.png",
    "spark_flash": "effects/hit/spark03.png",

    # The glint a thing lying on the ground throws off. See Drops.
    #
    # BITMAP_SHINY, which ZzzOpenData loads from Effect/Shiny01.jpg, and it is the sheet the
    # whole effect is: a sixteen-pixel four-point star, a white core with a horizontal and a
    # vertical spike running out to the border, on black. CreateShiny draws two of them at
    # one place and turns the second, so the star has to be a star — a shape whose spikes
    # sweep as it rotates — rather than the round mote a sparkle is usually painted as.
    #
    # A JPEG with no alpha, cut out of an additive pass by its own black like the flame,
    # streak and flare sheets. Sixteen pixels is also its width in the world: RenderSprite is
    # handed Bitmaps[Type].Width * Scale, so a shiny at its largest is a sixth of a tile.
    #
    # Drops drew this on spark02 before it had this sheet, which is BITMAP_SPARK and four
    # pixels of round mote. A mote at the size of a star is a smudge, and it was the reason
    # the sparkle read as dust off the item rather than as light off it.
    "shiny": "effects/drop/shiny01.png",

    # What a +7 and a +9 are drawn with. See Shine.
    #
    # BITMAP_CHROME, which ZzzOpenData loads from Effect/Chrome01.jpg with GL_REPEAT: a 64
    # square of soft diagonal bands, mostly dark - mean 57 of 255 - with a few bright streaks,
    # which is what an additive pass wants: black adds nothing and the streaks slide over the
    # metal as the wave scrolls the UVs. The +9 metal pass samples the shiny above, clamped,
    # off the same normals; MU binds BITMAP_SHINY for RENDER_METAL and nothing else.
    "chrome": "effects/refine/chrome01.png",

    # The bolt a Dark Wizard's Energy Ball throws. See Bolt.
    #
    # BITMAP_ENERGY, which ZzzOpenData loads from Effect/Thunder01.jpg, and the client's own
    # comment beside the particle case calls it "thunder energy". A 64 square: a white core
    # in a blue-violet halo with four faint spikes off it, on black - so it is a JPEG cut out
    # of an additive pass by its own black, like the flame, streak and flare sheets.
    #
    # One sheet does the whole effect. The bolt in flight is this, the trail it lays behind
    # it is another of this dimmed by the remaining lifetime, and the pop where it arrives is
    # spark03 - already here as "spark_flash", because it is the same 32-pixel contact flash
    # a sword throws. Nothing else needs cutting for it.
    "energy": "effects/energy/thunder01.png",

    # And the ribbon Lightning is drawn on, which is a strip and not a square. See Thunder.
    #
    # BITMAP_JOINT_THUNDER, Effect/JointThunder01.OZJ, 256x32: a crackling white-violet bolt
    # painted along the length of the sheet, on black, so it cuts itself out of an additive
    # pass like every other joint sheet.
    #
    # The zigzag is therefore in two places at once and both are needed. The geometry has its
    # own — MoveJoint turns the head back toward the target and then throws
    # (rand() % 1024 - 512) / Scale degrees of pitch and yaw at it every step, which is +-10
    # degrees on the wide joint and +-51 on the thin one — and the painting has the fine
    # crackle the geometry is far too coarse to make. RenderJoints then scrolls the U by
    # (WorldTime % 1000) / 1000 along the ribbon, with the second face of the cross offset by
    # twice that, so the painted bolt runs while the drawn one wanders. A ribbon built without
    # the scroll is a static wire and reads as one.
    "joint_thunder": "effects/thunder/joint_thunder01.png",

    # The streak a melee weapon leaves behind a basic swing. See Trails.
    #
    # CreateWeaponBlur picks between these by weapon group and by action, and the choice is
    # the one place MU really does treat melee weapons differently: a sword streaks on its
    # ordinary swings, a spear streaks on its own texture, and an axe or a mace streaks on
    # nothing until it is running a skill.
    #
    # blur01 is BITMAP_BLUR, a 32x64 strip and the plain sword's; motion_blur is BITMAP_BLUR+1
    # at the same size, for the two-handed third swing; blur02 is BITMAP_BLUR2 at 64x128, and
    # is what a spear or a scythe lays down. All three are JPEGs over black, which is how they
    # cut themselves out of an additive pass.
    "trail": "effects/trail/blur01.png",
    "trail_motion": "effects/trail/motion_blur.png",

    # And the one a skill swing lays, which is neither of the two above.
    #
    # CreateWeaponBlur tests PLAYER_ATTACK_SKILL_SWORD1..5 *before* it looks at what is in the
    # hand, and gives them BlurMapping 2 - so a skill streaks whatever the weapon is, and does
    # it on a sheet of its own. CreateBlur resolves the mapping as `BITMAP_BLUR + Type`, and
    # BITMAP_BLUR + 2 is Effect/motion_blur_r.jpg: a 32x64 strip like the other two and drawn
    # white, because the refinement colours only apply to mapping 0.
    "trail_skill": "effects/trail/motion_blur_r.png",
    "trail_spear": "effects/trail/blur02.png",

    # What a level looks like. See Aura.
    #
    # WSclient.cpp answers 0xF3 0x05 with fifteen CreateJoint(BITMAP_FLARE) and one
    # CreateEffect(BITMAP_MAGIC + 1), which is these two sheets and nothing else — the flare
    # is what orbits and rises, the circle is what opens on the ground under it.
    #
    # Flare.OZJ is a 64 square and is not flare01.OZJ, which is a different painting used for
    # torches and item shine; BITMAP_FLARE is the plain one. Magic_Ground2.OZJ is a 128 square
    # of radial spokes. Both are JPEGs over black, so both cut themselves out of an additive
    # pass the way the streak sheets do.
    "flare": "effects/levelup/flare.png",
    "magic_ground": "effects/levelup/magic_ground.png",

    # The elf's summon, which borrows magic_ground above for its ground decal - BITMAP_MAGIC + 1
    # at sub-type 3, tinted orange - and adds three sheets of its own. JointEnergy01.OZJ is
    # BITMAP_JOINT_HEALING, the streaks that home in on her, and is 8 by 4: a gradient and not
    # a painting. Shiny02.OZJ is the flash each streak ends in, 32 by 64. lightning2.OZJ is
    # BITMAP_LIGHTNING + 1, the burst that stands over the creature as it arrives. All JPEGs
    # over black, all additive. See client/core/Summoning.cs and docs/summoning.md.
    "joint_energy": "effects/summon/joint_energy01.png",
    "shiny_02": "effects/summon/shiny02.png",
    "lightning_2": "effects/summon/lightning2.png",

    # And what a barrier looks like, which is a different sheet and not a tint of the one
    # above. CreateJoint's MODEL_SPEARSKILL subtypes 0, 4 and 9 all set
    # `o->TexType = BITMAP_FLARE_BLUE`, which ZzzOpenData loads from Effect/flareBlue.jpg -
    # a 64 square, blue-white where Flare.jpg is gold. The green those ribbons actually read
    # as is this sheet under the joint's own Light of (0.4, 0.8, 0.2); tinting the gold
    # flare green instead gives a muddy olive, which is the failure the note on Flaring
    # describes from the other direction.
    #
    # Filed under defense/ because that is what this project throws it for. It is MU's
    # barrier art and the elf's Greater Defense is where it is used in the original; the
    # knight's Defense drawing anything at all is this bench's, and Aura.Defense says so.
    "flare_blue": "effects/defense/flare_blue.png",

    # Lorencia's leaves, which live in the world's own directory rather than under Effect.
    #
    # A 16-pixel square with 98% of it cut away — the leaf is a dozen opaque texels and the
    # rest is nothing, which is why it is a cut-out rather than a blend. Not a fire effect
    # and grouped with them anyway: what this table is for is art the viewer draws itself,
    # with no model and nothing for the item pipeline to do to it.
    "leaf": "effects/leaf/leaf01.png",

    # MU's own grass, which is a picture rather than a model.
    #
    # Three 256-wide sheets, each holding four 64-pixel columns of blades. The client stands
    # one quad on every grass tile and takes the next column along for each, which is how a
    # field of forty thousand identical quads never looks like one. See Turf.
    #
    # The .tga beside the .OZJ of the same name, and the difference matters: TileGrass01.OZJ
    # is the ground texture the tile is painted with, TileGrass01.tga is the grass standing on
    # it. They share a name and are not the same picture.
    "grass_0": "effects/grass/TileGrass01.png",
    "grass_1": "effects/grass/TileGrass02.png",
    "grass_2": "effects/grass/TileGrass03.png",

    # And per world, because the blades are the world's art and not the engine's.
    #
    # The three above are Lorencia's, decoded before there was a second map and named as
    # though grass were one thing. It is not: World4 ships its own TileGrass01.OZT, 80% cut
    # away against Lorencia's 77% and a different plant, and it ships no TileGrass02.OZT at
    # all - which is consistent, because Noria paints slot 1 nowhere. Left growing on the
    # shared keys, the elf wood would have come up wearing the town's lawn.
    #
    # The slot in the key is the *tile slot*, not an index into a list: the client's lookup is
    # BITMAP_MAPGRASS + TerrainMappingLayer1, so slot 0 grows TileGrass01.tga and slot 2 grows
    # TileGrass03.tga, and a slot with no entry grows nothing. See Turf, and World.Sow.
    "grass_lorencia_0": "effects/grass/lorencia_TileGrass01.png",
    "grass_lorencia_1": "effects/grass/lorencia_TileGrass02.png",
    "grass_lorencia_2": "effects/grass/lorencia_TileGrass03.png",

    "grass_noria_0": "effects/grass/noria_TileGrass01.png",
    "grass_noria_2": "effects/grass/noria_TileGrass03.png",

    # The meadow's wild plants, which are not MU's: seed heads, weeds and flowers painted by
    # meadow.py and scattered through every world's green grass. See Turf.Meadow.
    "grass_wild": "effects/grass/wild.png",
}

#: Colour grades, built by grade.py and copied into the build for the viewer to find.
GRADES = "grades"


def assets(root: Path) -> list[tuple[Path, dict]]:
    """Every asset file under the tree, with what it declares.

    Judged by what the file says it is rather than where it sits — the same rule the
    listing uses. assets/ also holds the shared material library, the profiles and a rig
    beside each part, and none of those are things that can be looked at.
    """
    found = []

    for path in sorted(root.rglob("*.json")):
        if path.name.endswith(".rig.json"):
            continue

        try:
            document = json.loads(path.read_text())
        except (OSError, json.JSONDecodeError):
            continue

        if "item" in document or "parts" in document:
            found.append((path, document))

    return found


def actions_library(rig: Path, assets: Path, build: Path) -> Path:
    """Where a rig's clips are built to, given where the rig itself is.

    Beside the rig, in the build tree that mirrors the asset tree, which is the same rule
    every other derived file follows: `assets/players/rig/player.rig.json` becomes
    `build/players/rig/player.actions.res`, and Girl01's becomes
    `build/npc/body/Girl01.actions.res`.

    Stated here rather than in each caller because there are three of them and they are in
    three languages — mu2.sh builds the file, this index names it, and the viewer opens it.
    A rule that drifts between them is a viewer looking for something the build wrote one
    directory over.
    """
    relative = rig.resolve().relative_to(assets.resolve())

    return build / relative.parent / f"{relative.name.removesuffix('.rig.json')}.actions.res"


def kind_of(relative: Path) -> str:
    """What sort of thing a built .glb is, read off where the pipeline put it.

    The viewer needs this to offer a weapon where a weapon goes and a shield where a shield
    goes, and there is nothing in a .glb that says which it is. The build tree mirrors the
    asset tree, so the directory already knows — `items/weapons/Sword01` is a weapon and
    `players/body/HelmClass02` is a piece of a bare body — and reading it here is cheaper
    and harder to get wrong than asking every asset file to repeat it.
    """
    parts = relative.parts

    if parts and parts[0] == "players":
        return "body"

    # Scenery is filed by the map it belongs to rather than by what sort of thing it is -
    # world/lorencia/Stone01 - because that is how MU ships it and a boulder is not a
    # category of object the way a weapon is. Read literally, the rule below would call its
    # kind "lorencia" and the viewer would start offering to put a town in somebody's hand.
    if parts and parts[0] == "world":
        return "scenery"

    return parts[1].rstrip("s") if len(parts) > 1 else "object"


#: Above this a material is drawn as metal, and keeps the sun's full specular.
METAL_AT = 0.2


def is_metal(name: str | None, root: Path) -> bool:
    """Whether the library calls this material a metal."""
    if not name:
        return False

    definition = root / "materials" / f"{name}.json"

    if not definition.exists():
        return False

    try:
        return float(json.loads(definition.read_text()).get("metallic", 0.0)) > METAL_AT
    except (OSError, json.JSONDecodeError):
        return False


def glb_materials(path: Path | None) -> list[str]:
    """The material names inside a built .glb."""
    if path is None:
        return []

    try:
        data = path.read_bytes()
    except OSError:
        return []

    if len(data) < 20 or data[:4] != b"glTF":
        return []

    length = int.from_bytes(data[12:16], "little")

    try:
        document = json.loads(data[20:20 + length])
    except (json.JSONDecodeError, UnicodeDecodeError):
        return []

    return [str(one.get("name") or "") for one in document.get("materials", [])]


def metal_slots(document: dict, root: Path, glb: "Path | None" = None) -> list[str]:
    """The names in one asset's .glb whose material is a metal.

    Names, and not slot names, which is the correction this carries. The renderer matches
    what is in this list against the material name inside the built file, and only one of
    the three ways export_gltf names a material is the slot: tiled scenery keeps its slots
    (House02 ships "drum" and "steel"), a baked model split by its islands is named after
    the *material* of each (Axe01 ships "steel" and "wood"), and a baked model whose islands
    are all one material is not split at all and ships a single part called "mu2".

    Reading only the first of those was a silent hole, and it was the whole of a bug: every
    monster and every item in the build is baked, so a Giant in full plate carried the list
    ["armor_up2", ...] and the file it names its materials "mu2". Nothing matched, and the
    moment the crowd started damping its dielectrics that armour would have gone flat. The
    built file is right there and cannot disagree with itself, so it is what is read.
    """
    named = document.get("sheet_materials") or {}
    fallback = document.get("default_material")
    regions = document.get("sheet_regions") or {}
    found = []

    for slot in (document.get("sheets") or {}):
        # A slot counts as metal if any *part* of its sheet is. The viewer damps dielectric
        # specular per material name and cannot damp half a primitive, so a cannon whose
        # barrel is wrought iron inside a sheet declared timber would have its one metal
        # dulled along with the crates around it. Erring toward metal is the safe side here:
        # the ORM still says metallic 0 everywhere outside the region, and what this list
        # decides is only whether the damping is applied at all.
        if any(is_metal(one.get("material"), root) for one in regions.get(slot, [])):
            found.append(slot)
            continue

        if is_metal(named.get(slot, fallback), root):
            found.append(slot)

    # And the two baked namings, which are the ones the slot list cannot reach.
    #
    # Taken off the built file rather than worked out again from the asset: export_gltf
    # decides between them on a rule with three branches and a "fewer than two distinct
    # names means do not split at all" clause, and a second copy of that rule here is a
    # second copy that can drift. What is in the file is what the renderer will ask about.
    inside = glb_materials(glb)

    if not inside:
        return found

    # An island's material is the part's name, so the name answers for itself. "hidden" and
    # "nocked" are export_gltf's own labels rather than materials and are never metal — the
    # first is blanked outright and the second is an arrow.
    for name in inside:
        if name != "mu2" and name not in found and is_metal(name, root):
            found.append(name)

    # And the unsplit model, which is one part called "mu2" holding the whole thing. It is
    # only ever written when every island agreed on one material, so there is nothing to
    # weigh: the model is metal exactly when that material is.
    #
    # Only when it is the *whole* list, which is the trap here. export_gltf builds one base
    # material called "mu2" before it makes the per-part copies and leaves it in the file
    # whether or not a primitive ends up pointing at it — so a split model carries the name
    # too, referenced by nothing. Reading it as "the whole model" there put "mu2" beside
    # "brass" and "plate_steel" on nine items and would have exempted every one of their
    # dielectrics. A model that really is one part has one name and nothing else.
    if inside == ["mu2"]:
        whole = {one.get("material") for one in (document.get("islands") or [])}
        whole.discard(None)

        if not whole:
            whole = set(named.values()) | ({fallback} if fallback else set())

        if whole and all(is_metal(one, root) for one in whole):
            found.append("mu2")

    return [one for one in found if one in inside]


def built(build: Path, name: str, beside: Path | None = None,
          root: Path | None = None) -> Path | None:
    """Where a name's .glb ended up, if it was built at all.

    `beside` is the asset asking, and it decides between two builds of one name. The sweep
    below is over the whole tree and returns whichever the filesystem hands back first, which
    was unambiguous for as long as a name meant one model. It stopped being so with the second
    numbered world: the character scene and Noria both run Object01 upward, they share four
    names, and the sweep gave both assets the same .glb — so the index carried Object06 twice,
    both pointing at Noria's bush, and the character scene lost four of its eighteen entries
    to models from another town.

    The build tree mirrors the asset tree, so the asset's own folder is the answer: an asset
    at assets/world/charscene/Object06.json is built to build/world/charscene/Object06. Tried
    first, and the sweep is kept behind it for everything that legitimately looks elsewhere —
    a held weapon named by a character, a `mesh` shared between twelve scrolls.
    """
    if beside is not None and root is not None:
        mirrored = (build / beside.parent.relative_to(root) / name / f"{name}.glb")

        if mirrored.exists():
            return mirrored.relative_to(build)

    for candidate in build.rglob(f"{name}.glb"):
        return candidate.relative_to(build)

    return None


def rig_of(root: Path, path: Path, document: dict) -> Path:
    """The rig that answers for an asset's animation, which is the model's and not the row's.

    Beside the asset for everything with a model of its own, and beside the *shared* one for
    an asset that names another's with `mesh`. The Elite Bull Fighter is why: it is Monster01
    a second time - the same 45 bones and the same seven clips at a different scale, with a
    polearm instead of an axe and its crest left showing - so it ships no .obj and no .rig.json,
    and every question about its clips is a question about BullFighter01's.

    Getting this wrong is quiet rather than loud. A missing rig means no key counts, and no
    key counts means monster_effects falls back to "the whole clip": the Elite would have
    snorted continuously through both idles and its walk instead of on the four windows the
    client opens, which is an effect that plays and is wrong rather than one that is absent.
    """
    shared = document.get("mesh")

    if not shared:
        return path.parent / f"{path.stem}.rig.json"

    for candidate in root.rglob(f"{shared}.rig.json"):
        return candidate

    return path.parent / f"{shared}.rig.json"



def fixed(declared: dict, corrections: Path) -> dict:
    """Applies the map's own correction list: single placements, and whole-model rules."""
    try:
        written = json.loads(corrections.read_text())
    except (OSError, json.JSONDecodeError):
        return declared

    listed = written.get("fixes", [])
    drops = written.get("drops", [])
    sheets = written.get("grass_sheets", {})

    # Tiles the map leaves open under something solid, named one at a time.
    #
    # Carried through rather than applied here, because this function edits the placement
    # list and the answer belongs in the attribute grid. See stamp_blocked, which puts it
    # there, and placements.json, where each one says what was measured.
    declared["blocked"] = [
        tile
        for entry in written.get("blocked", [])
        for tile in entry.get("tiles", [])
    ]

    # And the shapes that are blocked wherever they stand, rather than a tile at a time.
    # A model with an entry here has its footprint stamped at every one of its placements;
    # everything else is left exactly as MU painted it. See solid_tiles.
    solids = written.get("solids") or {}

    if solids.get("models"):
        declared["solids"] = {
            "coverage": solids.get("coverage", 0.10),
            "models": solids["models"],
        }

    # Which grass billboard grows on which ground. A lookup the client does by index and gets
    # wrong in Lorencia — see the note in placements.json.
    paired = {k: int(v) for k, v in sheets.items() if k.isdigit()}

    if paired:
        declared["grass_sheets"] = paired
        print(f"  grass      {len(paired)} tile slot(s) paired to a sheet by hand")

    # The townspeople, added to the placement list rather than carried beside it.
    #
    # An NPC standing in a town is an animated model at a position with a rotation, which is
    # exactly what the world already knows how to raise — and it is the only thing it knows
    # how to raise. Putting them in the list means they batch, cull, catch MU's baked light
    # and run their idle through Scenery without any of that being written twice.
    #
    # Their heights come off the terrain here rather than from the table, because the table
    # has none: the server stores a tile and lets the client drop them on the ground.
    for spawn in written.get("spawns", []):
        model = spawn.get("model")
        tile = spawn.get("tile")

        if not model or not tile:
            continue

        ground = tiles_of(corrections.parent, declared)
        heights = heightmap_of(corrections.parent, declared)

        if heights is None:
            print(f"  spawn      no heightmap; {model} not placed")
            continue

        size = declared.get("size", 256)
        tx = int(tile[0]) % size
        ty = int(tile[1]) % size

        # MU's eight named directions, turned by the offset the old port measured against a
        # landmark rather than derived. See placements.json.
        turn = ((int(spawn.get("facing", 3)) - 3) * 45.0) % 360.0

        placement = {
            "type": -1,
            "model": model,
            "at": [
                (tx + 0.5) * 100.0,
                (ty + 0.5) * 100.0,
                float(heights[ty, tx]) * declared.get("height_factor", 1.5)
                + float(spawn.get("lift", 0.0)),
            ],
            "angle": [0.0, 0.0, turn],
            "scale": 1.0,
        }

        # And a lift off the ground, for the one character in the game that does not stand on
        # it. Noria's Elf Lala is a winged fairy and the client hovers her a metre and forty
        # up rather than animating it — `Position[2] = RequestTerrainHeight(...) + 140.f`. It
        # rides on the spawn because a table of tiles and facings has nowhere else to put it,
        # and it is nought for everybody else. See placements.json.

        # The one clip this one holds, where the spawn says. A guard's stance is a fact
        # about his tile rather than about his model - inside a safe zone SetPlayerStop
        # never reaches his weapon - so it rides on the placement. See placements.json.
        if (idle := spawn.get("idle")):
            placement["idle"] = idle

        declared.setdefault("objects", []).append(placement)

        print(f"  spawn      {spawn.get('who', model)} at {tx},{ty} facing {turn:.0f} deg")

    if not listed and not drops:
        return declared

    placed = declared.get("objects", [])
    applied = 0

    # The rules first, because a rule can remove the very placement a fix names and the
    # stale-entry warning below should say so rather than silently doing nothing.
    for rule in drops:
        want = rule.get("models")
        allowed = set(rule.get("only_on") or [])

        if not want or not allowed:
            continue

        floors = tiles_of(corrections.parent, declared)

        if floors is None:
            print("  drop       no tile map to check against; rule skipped")
            continue

        size = declared.get("size", 256)
        kept = []
        removed = 0

        for one in placed:
            if not (one.get("model") or "").startswith(want):
                kept.append(one)
                continue

            tx = int(one["at"][0] / 100.0) & (size - 1)
            ty = int(one["at"][1] / 100.0) & (size - 1)
            floor = declared.get("tile_slots", {}).get(str(int(floors[ty, tx])), "")

            if floor in allowed:
                kept.append(one)
            else:
                removed += 1

        placed = kept
        declared["objects"] = placed

        if removed:
            print(f"  drop       {removed} {want}* placement(s) not on "
                  f"{', '.join(sorted(allowed))}")

    for fix in listed:
        want = fix.get("model")
        near = fix.get("near")

        if not want or not near:
            continue

        # By model and position rather than by index, because an index into a list nobody
        # here writes is a number that means nothing and moves the moment the extractor does.
        closest = None
        best = 0.5

        for one in placed:
            if one.get("model") != want:
                continue

            gap = max(abs(one["at"][0] / 100.0 - near[0]),
                      abs(one["at"][1] / 100.0 - near[1]))

            if gap < best:
                best, closest = gap, one

        if closest is None:
            print(f"  fix        no {want} within half a tile of "
                  f"{near[0]}, {near[1]} — the correction is stale")
            continue

        if "height" in fix:
            closest["at"][2] = float(fix["height"]) * 100.0
            applied += 1

    if applied:
        print(f"  fix        {applied} placement(s) corrected against the map's own list")

    return declared


def heightmap_of(beside: Path, declared: dict):
    """The map's heightmap, for standing somebody on the ground."""
    name = declared.get("height")

    if not name or not (beside / name).exists():
        return None

    try:
        from PIL import Image
        import numpy as np
    except ImportError:
        return None

    return np.asarray(Image.open(beside / name).convert("L"))


def tiles_of(beside: Path, declared: dict):
    """The map's tile-texture grid, for a rule that cares what a placement stands on."""
    name = declared.get("tiles")

    if not name or not (beside / name).exists():
        return None

    try:
        from PIL import Image
        import numpy as np
    except ImportError:
        return None

    return np.asarray(Image.open(beside / name).convert("RGB"))[..., 0]


def solid_tiles(declared: dict) -> list:
    """The tiles a declared footprint stands on, at every placement of that model.

    The other half of the same problem the hand list solves, for the case a hand list cannot
    reach. MU's grid under-marks the big scenery one boulder at a time and it under-marks
    Lorencia's railings a hundred and eighty times, in a different orientation each time --
    a fence is placed at any angle the map likes, and writing out the tiles under each one by
    hand would be six hundred coordinates that say nothing about why any of them is blocked.

    So the *shape* is declared, once, per model, and the placement list says where the shape
    is. Each entry in placements.json names a box measured off the model's own geometry below
    body height, in MU units about the model's origin, and says what was measured. That is
    still declaration and not detection: nothing here reads a mesh, and a model with no entry
    is untouched.

    A tile is taken when the placed box covers at least ``coverage`` of it, which the
    declaration names rather than this code hiding. Not when the box's centre is in it, which
    was tried first and is wrong for exactly the thing this exists for: Lorencia's kerbs are
    26 units thick against a tile of 100, and one at 136.77 to 137.00 lies wholly inside tile
    136 without going near its centre -- so the kerb was declared, stamped nothing, and a
    character walked through it as before.

    Nor when the box merely *touches* a tile, which is the rule that was tried and reverted
    long before that: a shape ending on a tile boundary took the tile beyond it, and a
    character standing there had every step refused and could not move. A share of the tile
    has no such edge -- a hairline clip is a hairline share and is not enough.

    A tenth is the share, and it is chosen against the kerbs because they are the thinnest
    thing a share can measure: 26 units across a whole tile is 26%, comfortably over, and one
    lying exactly on a tile line is 13% either side and takes both, which reads as a kerb a
    tile thick rather than as one you can walk through. Anything chunkier is far over the line
    in every tile it is really in.

    **A box thinner than the share is measured by its length instead.** Lorencia's iron
    railings are 6 units thick, and a share cannot see them: a railing running straight
    through the middle of a tile covers 6% of it, under the tenth, so every railing in the
    town stamped nothing and a character walked through the flower beds. The fault is in the
    unit and not in the threshold -- a shape narrower than the share can never reach it, in
    any tile, however squarely it crosses. So for those the overlap is divided by the box's
    own thickness, which turns an area back into the length of the crossing, and the same
    tenth then asks the same question of it: does this shape cross a tenth of a tile's width.
    A railing through the middle of a tile scores 1.0 and is in; one clipping a corner scores
    the sliver it clips and is not. Nothing else in Lorencia is under a tenth of a tile thick,
    so nothing else is measured this way.
    """
    solids = declared.get("solids") or {}
    models = solids.get("models") or {}
    objects = declared.get("objects") or []

    if not models or not objects:
        return []

    size = int(declared.get("size") or 256)
    per_tile = float(declared.get("units_per_tile") or 100.0)
    coverage = float(solids.get("coverage") or 0.10)
    taken = set()

    for placement in objects:
        declaration = models.get(placement.get("model"))

        if not declaration:
            continue

        # One box, or several. Several is how a shape with a way through it is said: the town
        # gate is a pier, an archway, and a pier, and it is written as the two piers. A single
        # box cannot say that, and a single box over the gate walls the town in.
        boxes = declaration if isinstance(declaration[0], list) else [declaration]

        scale = float(placement.get("scale") or 1.0)
        angle = math.radians(float(placement.get("angle", [0, 0, 0])[2]))
        turn, lean = math.cos(angle), math.sin(angle)
        at_x, at_y = float(placement["at"][0]), float(placement["at"][1])

        for box in boxes:
            x0, x1, y0, y1 = (float(edge) for edge in box)

            # How thin it is, in tiles, which decides what a "share" means for it. A shape
            # narrower than the share can never cover that much of any tile, so measuring one
            # by area is asking a question whose answer is always no. See the note above.
            thickness = min(x1 - x0, y1 - y0) * scale / per_tile

            # The box's four corners where it actually stands, in tiles.
            corners = []

            for local_x, local_y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
                across = local_x * scale
                down = local_y * scale
                corners.append((
                    (at_x + (across * turn) - (down * lean)) / per_tile,
                    (at_y + (across * lean) + (down * turn)) / per_tile,
                ))

            first = math.floor(min(x for x, _ in corners))
            last = math.floor(max(x for x, _ in corners))
            top = math.floor(min(y for _, y in corners))
            bottom = math.floor(max(y for _, y in corners))

            for row in range(top, bottom + 1):
                for column in range(first, last + 1):
                    share = overlap(corners, column, row)

                    # An area over a thickness is a length: how much of a tile's width the
                    # crossing spans. Only for the shapes a share cannot see.
                    if 0.0 < thickness < coverage:
                        share /= thickness

                    if share >= coverage:
                        taken.add((column % size, row % size))

    return [list(tile) for tile in sorted(taken)]


def overlap(corners: list, column: int, row: int) -> float:
    """How much of one tile a convex quad covers, as a share of the tile.

    Sutherland-Hodgman against the tile's four sides, which is exact for a convex shape and
    is the whole of what a rotated box is. Written out rather than reached for in a package
    because it is twenty lines and the pipeline has no geometry dependency to add it to.
    """
    polygon = list(corners)

    for inside, edge in (
        (lambda p: p[0] >= column, "left"),
        (lambda p: p[0] <= column + 1, "right"),
        (lambda p: p[1] >= row, "top"),
        (lambda p: p[1] <= row + 1, "bottom"),
    ):
        if not polygon:
            return 0.0

        clipped = []
        previous = polygon[-1]

        for point in polygon:
            if inside(point):
                if not inside(previous):
                    clipped.append(cut(previous, point, edge, column, row))

                clipped.append(point)
            elif inside(previous):
                clipped.append(cut(previous, point, edge, column, row))

            previous = point

        polygon = clipped

    if len(polygon) < 3:
        return 0.0

    # The shoelace, which is the area of any simple polygon and is signed, so the sign is
    # dropped rather than the winding being cared about.
    area = 0.0

    for at in range(len(polygon)):
        x0, y0 = polygon[at]
        x1, y1 = polygon[(at + 1) % len(polygon)]
        area += (x0 * y1) - (x1 * y0)

    return abs(area) / 2.0


def cut(start: tuple, end: tuple, edge: str, column: int, row: int) -> tuple:
    """Where a segment crosses one side of a tile."""
    (x0, y0), (x1, y1) = start, end

    if edge in ("left", "right"):
        at = column if edge == "left" else column + 1
        along = 0.0 if x1 == x0 else (at - x0) / (x1 - x0)
        return (at, y0 + ((y1 - y0) * along))

    at = row if edge == "top" else row + 1
    along = 0.0 if y1 == y0 else (at - y0) / (y1 - y0)
    return (x0 + ((x1 - x0) * along), at)


def sealed(was, now) -> None:
    """Says what the stamping cut off, which is the failure this cannot be shipped with.

    A box is a poor description of anything with a hole in it, and the hole is invisible in
    every number except this one. StoneMuWall01 is the town gate -- four of them, one in the
    middle of each side of Lorencia's wall -- and its box is six tiles by six. Declared, it
    stamped 168 tiles, took the town's walkable share from 72.6% to 72.4%, and walled the
    town in: the tiles you could reach on foot from the square fell from 47538 to 745. Every
    other measurement looked fine.

    So the shape of the ground is checked rather than its area. What is printed is the tiles
    that were reachable and are not any more, and every pocket over a couple of tiles is
    worth looking at: a fenced flower bed sealing is the point, and a building or a road
    sealing is a model whose box has a doorway in it.
    """
    # Into a signed type first: the grid is uint16 and numpy will not take the complement of
    # a mask against it.
    describes = np.int32(ACTION | HEIGHT | CAMERA_UP)
    open_before = (was.astype(np.int32) & ~describes) < CHARACTER
    open_after = (now.astype(np.int32) & ~describes) < CHARACTER

    if open_before.sum() == open_after.sum():
        return

    def islands(grid):
        seen = np.zeros(grid.shape, dtype=bool)
        found = []

        for row in range(grid.shape[0]):
            for column in range(grid.shape[1]):
                if not grid[row, column] or seen[row, column]:
                    continue

                queue = [(row, column)]
                seen[row, column] = True
                island = []

                while queue:
                    y, x = queue.pop()
                    island.append((x, y))

                    for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        ny, nx = y + dy, x + dx

                        if (0 <= ny < grid.shape[0] and 0 <= nx < grid.shape[1]
                                and grid[ny, nx] and not seen[ny, nx]):
                            seen[ny, nx] = True
                            queue.append((ny, nx))

                found.append(island)

        found.sort(key=len, reverse=True)
        return found

    before, after = islands(open_before), islands(open_after)

    if not before or not after:
        return

    reachable = set(before[0])
    cut = [island for island in after[1:] if any(tile in reachable for tile in island)]

    print(f"  walkable   {open_after.sum() / open_after.size * 100:.2f}% of the map, "
          f"{len(after[0])} tiles of it reachable on foot (was {len(before[0])})")

    for island in cut[:6]:
        across = sum(x for x, _ in island) // len(island)
        down = sum(y for _, y in island) // len(island)
        print(f"  sealed     {len(island)} tile(s) around ({across},{down})")


def stamp_blocked(declared: dict, grid: Path) -> int:
    """Marks the hand-listed tiles NoMove, in the build's copy of the attribute grid.

    MU's grid is painted by hand and it under-marks the big scenery: a boulder four tiles
    across carries a two-tile block under it, and a character walks into the overhang and
    stands inside the stone. The client has the same hole and so did the old port, which
    fixed it the same way this does — LEGACY's WorldViewer names the tavern bar's rectangle
    outright, because the bar is furniture the map forgot to mark.

    Listed rather than derived, and that is the whole of the design. Deriving the footprint
    from the mesh was tried and reverted: a disc of the measured radius takes tiles whose
    centres land exactly on its boundary, and a character standing on the edge of one of
    those has every step refused and cannot move. A list has no arithmetic in it to be
    wrong at the edges, it can be read, and each entry in placements.json says what was
    measured and why the map disagrees with itself.

    Written into the build's copy and never into the one under assets/, which stays MU's
    own measurement — so this is idempotent, every run starting from the file the client
    shipped.
    """
    tiles = list(declared.get("blocked") or [])
    tiles += solid_tiles(declared)

    # Except where MU left a tile open so a character could stand on it and sit down.
    #
    # This pass exists to close tiles the map under-marked; it must not close one the map
    # deliberately left open. A bench, a log or a lean box is walked *onto* — the client gates
    # the click on the placement's own tile and then routes there — so a footprint laid over
    # one is a seat that answers the pointer, walks you over, and does nothing. See
    # terrain.OPERABLE_BY_MAP, which is why this is a list of types and not a guess.
    grid_size = int(declared.get("size") or 256)
    per_tile = float(declared.get("units_per_tile") or 100.0)

    seats = {
        (int(one["at"][0] // per_tile) % grid_size, int(one["at"][1] // per_tile) % grid_size)
        for one in (declared.get("objects") or [])
        if one.get("type") in OPERABLE_BY_MAP.get(
            int(declared.get("map_number", 1)) - 1, set()) and len(one.get("at") or []) >= 2
    }

    if seats:
        tiles = [tile for tile in tiles
                 if (int(tile[0]) % grid_size, int(tile[1]) % grid_size) not in seats]

    if not tiles:
        return 0

    image = Image.open(grid).convert("RGB")
    pixels = np.asarray(image).astype(np.uint16)
    flags = pixels[:, :, 0] | (pixels[:, :, 1] << 8)
    was = flags.copy()
    size = flags.shape[0]

    stamped = 0

    for tile in tiles:
        x, y = int(tile[0]) % size, int(tile[1]) % size

        if not flags[y, x] & NO_MOVE:
            flags[y, x] |= NO_MOVE
            stamped += 1

    if stamped == 0:
        return 0

    sealed(was, flags)

    blocked = (flags & (NO_MOVE | NO_GROUND)) != 0

    Image.fromarray(np.stack([
        (flags & 0xFF).astype(np.uint8),
        (flags >> 8).astype(np.uint8),
        np.where(blocked, 0, 255).astype(np.uint8),
    ], axis=-1), "RGB").save(grid)

    return stamped


def worlds(root: Path, build: Path) -> list[dict]:
    """Every map that has been extracted and had its ground built.

    The world's own document and its heightmap are copied into the build beside the ground,
    for the reason the effects are: the viewer finds build/ and has no idea where assets/ is.
    The heightmap goes because the ground is a mesh and a mesh is a bad thing to ask "how high
    is it at this point" of — that question gets asked every frame while somebody walks.
    """
    found = []

    for document in sorted((root / "world").glob("*/*.json")):
        try:
            declared = json.loads(document.read_text())
        except (OSError, json.JSONDecodeError):
            continue

        if "objects" not in declared or "height" not in declared:
            continue

        name = declared.get("world", document.stem)
        ground = build / "world" / name / f"{name}_ground.glb"

        if not ground.exists():
            print(f"  world      {name} is extracted but its ground is not built")
            continue

        # Corrected on the way through, where the map is wrong about itself.
        #
        # MU's placement list is extracted and never edited, which is the right default and
        # stays the default: the extraction is a measurement, and a measurement corrected in
        # place is one nobody can check again. So the fixes live in their own file beside the
        # assets, each saying what it saw, and are applied here — the file on disk keeps what
        # the client shipped and the build carries what is drawn.
        #
        # See placements.json for the test an entry has to pass: the data has to disagree
        # with itself rather than with taste. A candle half a metre above the table it stands
        # on is a table and a candle that cannot both be right.
        declared = fixed(declared, document.parent / "placements.json")

        # Beside the ground, which is where the viewer will be looking.
        target = ground.parent
        (target / document.name).write_text(json.dumps(declared, indent=1) + "\n")
        shutil.copyfile(document.parent / declared["height"], target / declared["height"])

        # What each tile permits, which is the other grid a walker needs. Absent on a map
        # extracted before the attribute pass existed, and the viewer treats that as "walk
        # anywhere" rather than as "walk nowhere".
        attributes = declared.get("attributes")
        if attributes and (document.parent / attributes).exists():
            shutil.copyfile(document.parent / attributes, target / attributes)

            # And the tiles the map forgot to mark under something solid. The copy is
            # stamped and the file under assets/ is left alone, so every run starts from
            # the grid the client shipped. See stamp_blocked.
            marked = stamp_blocked(declared, target / attributes)

            if marked:
                print(f"  blocked    {marked} tile(s) marked by hand, from placements.json")

        # And which tile texture each square wears, which is how the client decides the
        # player is indoors: it reads the texture under him and compares it against one
        # number per world. See World.Indoors.
        tiles = declared.get("tiles")
        if tiles and (document.parent / tiles).exists():
            shutil.copyfile(document.parent / tiles, target / tiles)

        # And MU's baked light, which the ground already carries in its vertex colours and
        # nothing standing on the ground did. That was the whole reason a fence beside a
        # lit wall read brighter than the wall: the town's own light stopped at the grass.
        # Shipped as the grid so that an object can be given the light of the tile it
        # stands on, which is what the client does.
        light = declared.get("light")
        if light and (document.parent / light).exists():
            shutil.copyfile(document.parent / light, target / light)


        found.append({
            "name": name,
            "label": name.capitalize(),

            # OpenMU's map number, which is also MU's own internal one: Lorencia is 0. The
            # data folder counts from one - Lorencia is World1, and that is what terrain.py
            # wrote down as map_number - so the one comes off here, once, and everything
            # downstream agrees with the spawn rows, the gates and a character's map column.
            "number": int(declared.get("map_number", 1)) - 1,
            "ground": str(ground.relative_to(build)),
            "data": str((target / document.name).relative_to(build)),
            "height": str((target / declared["height"]).relative_to(build)),
            "attributes": (str((target / attributes).relative_to(build))
                           if attributes and (target / attributes).exists() else ""),
            "tiles": (str((target / tiles).relative_to(build))
                      if tiles and (target / tiles).exists() else ""),
            "light": (str((target / light).relative_to(build))
                      if light and (target / light).exists() else ""),

            # What each ground surface is drawn with - sheet, normal, roughness - which
            # ground.py writes beside the ground and World reads beside it. Named here so
            # that what ships is what the index names; the client still finds it by
            # convention, as it did.
            "surfaces": (str((target / "ground_surfaces.json").relative_to(build))
                         if (target / "ground_surfaces.json").exists() else ""),
            "size": declared.get("size", 256),
            "units_per_tile": declared.get("units_per_tile", 100.0),
            "height_factor": declared.get("height_factor", 1.5),
            "objects": len(declared.get("objects", [])),
        })

    # In MU's own order, which matters because the first map is the default one.
    #
    # The loop above walks assets/world in name order, and that was fine while Lorencia was
    # the only map in it. The second map is the character scene, whose folder sorts *before*
    # lorencia — so a client asking for no map in particular would have quietly moved the
    # whole game onto a set with 158 walkable tiles. Sorted by the number instead, Lorencia
    # is 0 and first because it is MU's first town, not because of how it is spelled.
    found.sort(key=lambda one: one["number"])

    return found


#: The columns of monster_kinds a fight actually reads, in the order Breed declares them.
COMBAT_COLUMNS = (
    "level", "health", "minimum_damage", "maximum_damage", "defense",
    "move_range", "attack_range", "view_range", "move_delay", "attack_delay",
    "attack_rate", "defense_rate", "respawn_seconds",

    # And the skill it attacks with, which is the one nullable column in the table.
    #
    # NULL for every monster in Lorencia but the Lich, whose row carries 2 - OpenMU's
    # SkillNumber.Meteorite, which is also MuMain's AT_SKILL_METEO, so the number crosses
    # without translation. Read as 0 below, because a Breed is a record of numbers and
    # "no skill" and "skill 0" are the same statement: AT_SKILL_UNDEFINED is 0.
    "attack_skill",
)


def monster_effects(document: dict, rig: Path) -> list[dict]:
    """What a monster puts in the air, with MU's key numbers turned into clip fractions.

    The client gates these on its own animation counter: the Budge Dragon breathes fire
    while `AnimationFrame <= 4` of attack 1, which is a count of keys and means nothing to
    a viewer holding a clip measured in seconds. The conversion needs the number of keys in
    that action, and the rig beside the asset is the only file that knows it -- so it is
    done here, once, rather than by shipping a key count to the engine and converting it
    there every frame.

    A fraction rather than a time, deliberately. Seconds would be right only for as long as
    the clip is exactly as long as it is now, and the length of a monster's clip is the one
    number in this pipeline that is *expected* to move: it is play_speed out of
    assets/monsters/actions.json, which is a shared table anybody may correct. Four sevenths
    of the attack stays four sevenths of the attack.
    """
    declared = document.get("effects")
    if not declared:
        return []

    keys = {}

    if rig.exists():
        keys = {
            int(one["action"]): int(one["keys"])
            for one in json.loads(rig.read_text()).get("animations", [])
            if one.get("keys")
        }

    resolved = []

    def fraction(action, key, fallback):
        count = keys.get(int(action))

        if not count:
            print(f"  index      {rig.stem}: no key count for action {action}; "
                  f"'{one.get('kind')}' will play for the whole clip")
            return fallback

        return round(float(key) / count, 4)

    for one in declared:
        effect = {k: v for k, v in one.items()
                  if not k.endswith("_key") and k != "windows"}

        # Several stretches of several clips, for an effect the client opens and closes
        # more than once. The Bull Fighter snorts on keys 15 to 20 of its first idle, 20 to
        # 25 of its second and twice in its walk - four windows across three actions - and
        # one action with one through_key cannot say that. Each window becomes its own
        # entry with the same kind and bone, so the engine gates each on its own clip and
        # the emitter is one node found by name as it is for everything else; see
        # Crowd.Figure.Puffing, which runs every window's test and emits on any that passes.
        if (windows := one.get("windows")):
            effect["windows"] = [
                {
                    "clip": f"action{int(w['action'])}",
                    "from": fraction(w["action"], w.get("from_key", 0), 0.0),
                    "through": fraction(w["action"], w.get("to_key", 0), 1.0),
                }
                for w in windows
            ]
            resolved.append(effect)
            continue

        if (action := one.get("action")) is not None:
            # The clip's name in the built .glb, which is what the viewer selects by.
            effect["clip"] = f"action{int(action)}"

            if (through := one.get("through_key")) is not None:
                count = keys.get(int(action))

                if not count:
                    print(f"  index      {rig.stem}: no key count for action {action}; "
                          f"'{one.get('kind')}' will play for the whole clip")
                    effect["through"] = 1.0
                else:
                    effect["through"] = round(float(through) / count, 4)

        resolved.append(effect)

    return resolved


#: MU's units to metres, which is export_gltf's own number. See MU_UNITS_PER_METRE there.
MU_UNITS_PER_METRE = 100.0


def strides(rig: Path) -> dict:
    """How far each locked action's root travels over one cycle, in metres, by action.

    The one fact the built .glb cannot answer, and it is thrown away on purpose: MU bakes
    the stride into the root bone and its own client ignores it, so export_gltf pins the
    root's horizontal translation to the first key and the clip supplies only the legs. See
    the locked-action note there for why honouring both is a character walking out of his
    own body.

    Pinned, the distance is still the animator's statement of how far this body covers per
    cycle -- 2.43 m for the player's walk, 3.89 m for the Giant's -- and that is exactly
    what is needed to play the clip at the speed the ground is passing underneath. Without
    it the only way to pace a walk is to assume a cycle is two steps, which is right for a
    man and says nothing about a spider. See Crowd.Figure.Pacing.

    Only the locked actions, because only they travel: an action animated on the spot has a
    root that stays put, and a zero here would mean "measured, and it does not move" where
    the absence means "nothing to say". Both read the same downstream, so anything at all
    below a millimetre is left out.
    """
    if not rig.exists():
        return {}

    measured = {}

    for one in json.loads(rig.read_text()).get("animations", []):
        if not one.get("locked") or one.get("action") is None:
            continue

        root = next((t for t in one.get("tracks", []) if t.get("bone") == 0), None)
        positions = root.get("t") if root else None

        if not positions or len(positions) < 2:
            continue

        # MU is Z-up, so X and Y are the ground plane -- the same pair export_gltf pins.
        # Z is the body's rise and fall and is not travel.
        first, last = positions[0], positions[-1]
        travel = math.dist(first[:2], last[:2]) / MU_UNITS_PER_METRE

        if travel >= 0.001:
            measured[str(int(one["action"]))] = round(travel, 4)

    return measured


def combat_rows(project: Path) -> dict:
    """Every monster's combat row from the server's store, by monster number.

    Read here rather than by the viewer because the viewer is given build/ and nothing else,
    and because putting a SQLite driver inside a Godot project to read thirteen integers
    would be a native dependency in an exported application for no reason. mu.db stays the
    one place these numbers are written down; this copies them to where the client can see
    them, which is what a game does with its monster table anyway.

    A missing database is not an error. Most of what this pipeline builds has nothing to do
    with combat, and refusing to write an index because the server's store is not there
    would make the whole viewer depend on it.
    """
    store = project / "mu.db"

    if not store.exists():
        return {}

    with sqlite3.connect(f"file:{store}?mode=ro", uri=True) as db:
        db.row_factory = sqlite3.Row

        try:
            rows = db.execute(
                f"SELECT number, name, {', '.join(COMBAT_COLUMNS)} FROM monster_kinds"
            ).fetchall()
        except sqlite3.DatabaseError as bad:
            print(f"  index      could not read monster_kinds ({bad}); no combat rows")
            return {}

    return {
        row["number"]: {
            "name": row["name"],

            # `or 0` rather than the value, for attack_skill's sake: it is the one column
            # that may be NULL, and a null in the index reaches the reader as a JSON null
            # where every other number is a number. See COMBAT_COLUMNS.
            **{c: (row[c] or 0) for c in COMBAT_COLUMNS},
        }
        for row in rows
    }


def spawn_rows(project: Path) -> dict:
    """Where each monster actually stands on the map, by monster number.

    MU does not scatter its monsters: monster_spawns is a list of boxes with a count, and a
    Spider belongs in x 180-226, y 90-244 of Lorencia because that is where the original
    server puts forty-five of them. The crowd used to invent a nest around the middle of a
    flat pad, which is a correct simulation of nothing in particular - a spider that has
    never stood where spiders stand cannot be judged against the game.

    Several boxes to a monster is normal and all of them are carried. The Budge Dragon has
    two, and picking one of them here would be the pipeline deciding which half of a spawn
    table matters.

    Read here for the reason combat_rows gives: the viewer is handed build/ and nothing else,
    and mu.db stays the one place these numbers are written down.
    """
    store = project / "mu.db"

    if not store.exists():
        return {}

    with sqlite3.connect(f"file:{store}?mode=ro", uri=True) as db:
        db.row_factory = sqlite3.Row

        try:
            rows = db.execute(
                "SELECT id, number, map, x1, x2, y1, y2, count FROM monster_spawns ORDER BY id"
            ).fetchall()
        except sqlite3.DatabaseError as bad:
            print(f"  index      could not read monster_spawns ({bad}); no spawn boxes")
            return {}

    found: dict = {}

    # The map goes with the box, because a box is a place and a place is on a map. A Spider
    # that has rows on two maps is one breed with two boxes, and whoever raises a map takes
    # the boxes whose number is that map's - see Content.Read and Viewer.Journey.
    for row in rows:
        found.setdefault(row["number"], []).append({
            # The row's own id, so a box keeps its name when a monster is renamed or another
            # is built, and so the realm raises the boxes in the table's order whoever reads
            # them - which is what makes a seeded run on the server and on the client the
            # same run.
            "id": row["id"],
            "map": row["map"],
            "x1": row["x1"], "x2": row["x2"],
            "y1": row["y1"], "y2": row["y2"],
            "count": row["count"],
        })

    return found


def gate_rows(project: Path) -> dict:
    """Where a character is put down on each map, by OpenMU's map number.

    OpenMU's Gates.cs, kept in mu.db as the gates table for the reason the spawn boxes are:
    it is the one place these numbers are written down. Only the spawn gate is carried for
    now - the box a new character starts in and a dead one stands up in - because it is the
    only gate anything reads. The rest of the table is for when a map has a door to another.
    """
    store = project / "mu.db"

    if not store.exists():
        return {}

    with sqlite3.connect(f"file:{store}?mode=ro", uri=True) as db:
        db.row_factory = sqlite3.Row

        try:
            rows = db.execute(
                "SELECT map, x1, y1, x2, y2 FROM gates WHERE spawn = 1 ORDER BY id"
            ).fetchall()
        except sqlite3.DatabaseError as bad:
            print(f"  index      could not read gates ({bad}); no spawn gates")
            return {}

    return {
        row["map"]: {"x1": row["x1"], "y1": row["y1"], "x2": row["x2"], "y2": row["y2"]}
        for row in rows
    }


def onset(path: Path) -> float:
    """How long after a wave file starts before it is actually heard, in seconds.

    A sound scheduled to land on a moment lands on its *first sample*, which is not what an
    ear hears. eCrossbow.wav is 167 ms long and does not reach half its peak until 94 ms in:
    played on the frame the bolt leaves, the shot is drawn on time and heard a tenth of a
    second late, which is exactly long enough to notice and not long enough to explain.

    So the attack is measured - the first sample above half the file's peak - and whatever
    schedules a sound against a picture can start it that much earlier. Half the peak rather
    than the first non-zero sample, because the quiet ramp in front of a thwack is not the
    thwack: this file's first audible sample is at 5 ms and means nothing.

    Standard library only. A .wav is a header and some samples, and adding a dependency to
    read one would be a strange thing to do to a pipeline that decodes MU's textures by hand.
    """
    try:
        with wave.open(str(path)) as clip:
            frames, rate = clip.getnframes(), clip.getframerate()
            channels, width = clip.getnchannels(), clip.getsampwidth()

            if frames == 0 or rate == 0 or width not in (1, 2):
                return 0.0

            raw = clip.readframes(frames)
    except (OSError, wave.Error):
        return 0.0

    if width == 2:
        values = struct.unpack(f"<{frames * channels}h", raw[:frames * channels * 2])
        loud = [abs(values[i * channels]) for i in range(frames)]
    else:
        loud = [abs(raw[i * channels] - 128) for i in range(frames)]

    peak = max(loud, default=0)

    if peak == 0:
        return 0.0

    for i, value in enumerate(loud):
        if value > peak * 0.5:
            return i / rate

    return 0.0


def sounds(root: Path, build: Path) -> dict:
    """MU's wave files, copied into the build, and which one each event plays.

    A .wav is not art that can be upscaled, so unlike everything else here there is nothing
    for the pipeline to do to one: the files are copied unchanged and what travels with them
    is the table that says which event plays which. That table is assets/sounds/sounds.json,
    which is where the client's own rule was written down - see its notes for how MU picks.

    Kept out of EFFECTS deliberately, though it would have fitted. An effect is one file the
    viewer draws; a sound event is a *set* of files one of which is chosen at random, and
    flattening that into one path each would have thrown away the choosing, which is the part
    that stops a fight from ticking.
    """
    manifest = root / "sounds" / "sounds.json"

    if not manifest.exists():
        return {}

    try:
        events = json.loads(manifest.read_text()).get("events", {})
    except (OSError, json.JSONDecodeError) as bad:
        print(f"  sound      could not read {manifest} ({bad})")
        return {}

    written, missing = {}, 0
    attacks = {}
    gains = {}

    for event, one in sorted(events.items()):
        paths, leads = [], []

        for name in one.get("files", []):
            source = root / "sounds" / name

            if not source.exists():
                missing += 1
                continue

            target = build / "sounds" / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
            paths.append(f"sounds/{name}")
            leads.append(onset(source))

        if paths:
            written[event] = paths

            # The longest of them, where an event has several files to choose between. A
            # sound scheduled against a picture is scheduled before it is known which file
            # the roll will pick, and being early by the difference is a great deal less
            # noticeable than being late. See onset.
            if (lead := max(leads, default=0.0)) > 0.001:
                attacks[event] = round(lead, 4)

            # And how much louder or quieter than the file this event is mixed.
            #
            # Carried only where an event asks for it, which is where MU's own files are not
            # comparable to each other. pDropItem.wav is mono, 22 kHz and peaks at 0.44
            # against pDropMoney.wav's stereo 44 kHz at 0.93, so played straight, a sword
            # landing beside you is inaudible while a coin heap is not. See sounds.json,
            # which carries the measurement beside the number.
            if (gain := one.get("gain_db")):
                gains[event] = float(gain)

    files = len({p for paths in written.values() for p in paths})
    slow = sorted(attacks.items(), key=lambda pair: -pair[1])[:3]
    print(
        f"  sound      {len(written)} event(s) over {files} file(s)"
        + (f", {missing} named but not present" if missing else "")
        + (f", {len(gains)} remixed" if gains else "")
        + (", slowest attacks " + ", ".join(f"{e} {t * 1000:.0f}ms" for e, t in slow)
           if slow else ""))

    return {"events": written, "onsets": attacks, "gains": gains}


def missiles(root: Path, build: Path) -> dict:
    """Effect models, copied into the build with the numbers they are drawn by.

    Four folders, and it used to be one. The things that fly were the first of these and
    gave the format its shape; the click-to-move pin is the second and is the same kind of
    thing wearing a different name - a handful of triangles of additively blended light with
    no lighting on it, no bake and no material. Whether it moves is not a property the asset
    has.

    The third is the bones a skeleton comes apart into, and it stretches the shape by exactly
    one field. A bone is MU's own model out of Data/Skill like the rest, travels as the .obj
    muextract wrote like the rest, and has no bake and no material - but it is *solid*, and it
    is thrown upward from a point rather than forward from a muzzle, so it carries a `lift`
    where an arrow carries a muzzle. What draws it is not this pass's business either way; see
    client/core/Bones.cs, which lights it where Missiles.Paint would not.

    The fourth is the meteor a Lich throws, with the two stones it scatters. It stretches the
    shape by two more fields of that same kind and by nothing else - `sideways` and its
    spread, because a rock is born up *and* off to one side of the point it will land on
    where a bone is only thrown up. Its own folder rather than beside the bones, because the
    stones are its and the sheets are its: what an effect model needs from this pass is its
    geometry, its sheets and the numbers it is created by, and those come in sets.
    """
    return {
        **_models(root, build, "missiles"),
        **_models(root, build, "movetarget"),
        **_models(root, build, "bones"),
        **_models(root, build, "bigstone"),
        **_models(root, build, "meteor"),
        **_models(root, build, "wave"),
        **_models(root, build, "ice"),
        **_models(root, build, "poison"),
        **_models(root, build, "storm"),
        **_models(root, build, "summon"),
    }


def _models(root: Path, build: Path, folder_name: str) -> dict:
    """One folder of them.

    A missile is neither an item nor an effect texture, which is why it is neither of the
    two passes above. MU files it under Data/Skill rather than Data/Item and for a good
    reason: the quiver on a character's back and the bolt crossing the clearing are two
    different models, and nothing turns one into the other. What is in the air is four
    triangles of additively blended light with no lighting on it, no bake, no material and
    no place in the item pipeline at all - the same argument EFFECTS makes for the move
    marker, one step up, because this one has geometry.

    So the geometry travels as MU2 keeps geometry: the .obj muextract wrote, copied whole.
    The viewer reads it into a mesh at load. That is the format this project already
    declares as its own - see tools/MuExtract - and a four-triangle streak is not worth a
    detour through Blender to arrive as a .glb with a PBR material it would then have to be
    stripped of.
    """
    folder = root / "effects" / folder_name

    if not folder.exists():
        return {}

    flying = {}

    for path in sorted(folder.glob("*.json")):
        try:
            one = json.loads(path.read_text())
        except (OSError, json.JSONDecodeError) as bad:
            print(f"  model      could not read {path.name} ({bad})")
            continue

        mesh, parts = one.get("mesh"), one.get("parts") or []

        if not mesh or not parts:
            continue

        # And the poses, where the model is one whose rest is not its picture.
        #
        # An effect model in MU is geometry, and for most of these that is the whole story:
        # Fire01's single action only makes its flame writhe, so the bind pose is the meteor
        # as it is drawn. Two of them are not like that, for two different reasons.
        #
        # Ice01 is thirteen bones and a six-key action of pure translation - the shards fly
        # inward and lock - and its rest pose is the *first* of those keys, which is the block
        # unformed. A bind-pose export of it draws a shape the game never shows.
        #
        # Poison01's rest *is* its picture, and it carries poses anyway: its eleven keys are
        # played through at three tenths of a key a frame across the whole of its life, so
        # what one pose of it would miss is not the shape but the movement. See that asset's
        # poses_from note, which is where the difference between the two is argued.
        #
        # So the asset may name a list of poses instead of leaning on one, each baked by
        # `muextract export-obj --key=N`. `mesh` stays and names the pose a reader of one
        # should draw - the settled key for the ice, the first for the poison - so anything
        # that reads only `mesh` still draws the right picture; `poses` is what a stepper
        # reads, whether it steps them or blends between them.
        #
        # Six meshes of 528 vertices rather than a skinned rig, because the animation plays
        # once at a fixed speed and freezes on its last key - what a rig would buy is the
        # ability to blend something that never blends. See client/core/Ice.cs.
        poses = [str(name) for name in (one.get("poses") or [])]

        # One entry per mesh MU's model is made of, in the .obj's own group order.
        #
        # A missile is not always one surface: the arrow a bow throws is a solid shaft and a
        # blended tail, twenty-four triangles and nine, and the client picks between them by
        # index - `o->BlendMesh = 1` is the second mesh and not the model. So each group
        # carries its own sheet and its own blend, and a single-mesh missile is the same
        # shape of thing with one entry in the list.
        carried = {}
        wanted = [mesh] + poses + [part.get("sheet") for part in parts]

        for name in wanted:
            source = folder / (name or "")

            if not name or not source.exists():
                print(f"  model      {path.stem} names {name}, which is not there")
                break

            target = build / "effects" / folder_name / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
            carried[name] = f"effects/{folder_name}/{name}"
        else:
            flying[path.stem] = {
                "mesh": carried[mesh],

                # Absent rather than empty where the model has one pose, so the index says
                # which kind of thing it is rather than carrying a field of nothing on
                # every arrow in the game.
                **({"poses": [carried[name] for name in poses]} if poses else {}),

                "parts": [
                    {
                        "group": part.get("group", ""),
                        "sheet": carried[part["sheet"]],
                        "blend": part.get("blend", "additive"),
                    }
                    for part in parts
                ],
                "scale": float(one.get("scale", 1.0)),
                "frames": float(one.get("frames", 30.0)),
                "muzzle": [float(v) for v in one.get("muzzle", [0.0, 0.0, 0.0])],

                # How far above the point it is born, for the debris. MU's own
                # `o->Position[2] += ...` at the throw, in MU units. Zero for anything
                # that leaves from a muzzle instead.
                "lift": float(one.get("lift", 0.0)),

                # And how far to one side of it, with the spread on that.
                #
                # Only the meteor has these, and it needs both halves of what `lift` says
                # on its own for a bone: `o->Position[0] += 130.f + rand() % 32` beside the
                # `o->Position[2] += 400.f`. What they describe is where the rock appears
                # against where it will land, which is a different thing from where a bone
                # is thrown from - see the Missile record, and Fire01.json for the whole of
                # why an offset that looks like a miss is not one.
                "sideways": float(one.get("sideways", 0.0)),
                "sideways_spread": float(one.get("sideways_spread", 0.0)),
            }

    if flying:
        print(f"  missile    {len(flying)}: {', '.join(sorted(flying))}")

    return flying



def monster_entry(entry: dict, document: dict, root: Path, build: Path, combat: dict,
                  spawns: dict, rigged: Path) -> dict:
    """One monster's row of the index, out of the asset that declares it.

    Its own function because two different kinds of asset reach it. Nearly every monster is
    one model on its own rig and arrives from the object branch, which is where a single
    .glb with its own clips is handled. A handful are not: MU builds them the way it builds
    a character - `CreateCharacter(Key, MODEL_PLAYER, ...)`, a body in `Object.SubType`, a
    scale and two weapons - and the Skeleton Warrior is Lorencia's. That one declares parts
    and a stance like a guard and a `monster` block like a spider, and it needs both halves.

    So what makes an asset a monster is the `monster` block and not where its file sits or
    what shape its model is. The only difference between the two callers is the entry handed
    in: one carries a `glb` and the other the list of parts under the same key, because the
    reader on the other side takes either. See Index.ReadEntries.
    """
    # And its combat row, if the server's table has one for it.
    #
    # Joined on object_type, which is the monster number the server sends - the
    # same number MuMonsters.cs resolves to a model index. Carried into the index
    # so the viewer's Monsters tab can run the real simulation against the real
    # numbers without a database in the process: see MU2/shared/Rows.cs.
    #
    # Absent rather than defaulted when there is no row. A monster simulated
    # against invented stats is worse than one that cannot be simulated, because
    # only the second of those tells you the row is missing.
    beast = document.get("monster") or {}
    number = document.get("object_type")
    stats = combat.get(number) if number is not None else None

    # What it carries, and where. A monster is one rigged body rather than five
    # worn pieces, so unlike a character it names the bone itself: the client
    # keeps a LinkBone per model in CreateCharacter's weapon switch, and the
    # Bull Fighter's is 42, which its rig calls left_bone. Allowed to be missing
    # exactly as a character's is - a bull without its axe is still a bull.
    hands = {}

    for hand in ("right_hand", "left_hand"):
        if (held := document.get(hand)) and (one := built(build, held)):
            hands[hand] = str(one)

            if (bone := document.get(f"{hand}_bone")):
                hands[f"{hand}_bone"] = str(bone)

    # And a mesh the client hides on this variant. c->Object.HiddenMesh is one
    # index per object, -1 for none; the plain Bull Fighter hides mesh 0, its
    # crest, and the Elite wearing the same model does not.
    hidden = document.get("hidden_mesh")

    # Its own built file, for the metal list, which is read out of the .glb's material names.
    #
    # Both shapes of monster, and the character-shaped one hands "glb" a list of its five
    # parts rather than a path. The first is the one asked: a Skeleton Warrior's metal is his
    # armour's, and the parts are looked up whole on the client side through Catalogue.MetalOf
    # — this list is what a monster with a model of its own needs.
    model = entry.get("glb") or entry.get("parts")

    if isinstance(model, list):
        model = next(iter(model), None)

    return entry | {
        "model_index": int(beast.get("model_index", -1)),
        "scale": float(beast.get("scale", 1.0)),
        **hands,
        **({"hidden_mesh": int(hidden)} if hidden is not None else {}),
        **({"number": number} if number is not None else {}),
        **({"combat": stats} if stats else {}),
        **({"spawns": spawns[number]} if number in spawns else {}),
        **({"metal": found} if (found := metal_slots(
            document, root, build / model if model else None)) else {}),

        # What it puts in the air, and when. See monster_effects.
        **({"effects": puffs} if (puffs := monster_effects(document, rigged))
           else {}),

        # And what it lights, which is not something it puts in the air.
        #
        # MU keeps the two apart and so does this. A puff is created by the
        # per-frame effect switch in MoveCharacterVisual and lives its own life
        # once thrown; an eye is two sprites RenderCharacter re-creates every
        # frame it draws the monster, at a brightness off the world clock, with
        # no clip, no window and no lifetime. Carried whole rather than resolved
        # against the rig for exactly that reason: there is no key count in it.
        **({"eyes": eyes} if (eyes := document.get("eyes")) else {}),

        # And the sprites held on a bone that are not eyes - the Chain Scorpion's tail
        # light - carried whole for the same reason.
        **({"sprites": sprites} if (sprites := document.get("sprites")) else {}),

        # A darkened material, for the one monster here that has one. Not read
        # off MU - see tint_from in the asset that carries it - so this passes
        # whatever the asset states without deriving anything from the client.
        **({"tint": tint} if (tint := document.get("tint")) else {}),

        # How it stands and swings, for the ones built on the player's rig.
        #
        # A monster with a model of its own needs none of this: MU authors it to the twelve
        # fixed slots and slot 2 is the walk whatever the animal is. One built as a character
        # has the player's 283 instead, and which of them it uses is decided by what is in
        # its hand exactly as it is for a man - so the stance travels and the crowd runs the
        # client's own ladder against it. Absent for everything else, which is nearly
        # everything. See Crowd.Moves.Armed.
        **({"stance": stance} if (stance := document.get("stance")) else {}),

        # And how it dies, for the one that does not.
        #
        # A skeleton is switched off rather than animated - CharacterDie replaces it with
        # eleven loose bones before it ever reaches SetAction - so the asset names that
        # instead of a clip nothing would play. See SkeletonWarrior.json's death_from.
        **({"death": death} if (death := document.get("death")) else {}),
    }


def main() -> None:
    if len(sys.argv) < 3:
        print("usage: python3 index.py <assets dir> <build dir>", file=sys.stderr)
        raise SystemExit(2)

    root, build = Path(sys.argv[1]), Path(sys.argv[2])
    characters, objects, monsters = [], [], []

    # mu.db sits inside source/ itself, beside the recipes it is read with.
    combat = combat_rows(root.resolve())
    spawns = spawn_rows(root.resolve())

    for path, document in assets(root):
        name = path.stem
        label = document.get("mu_name", "")

        if parts := document.get("parts"):
            # A character is only worth listing when all of it is there. Half a Dark
            # Knight in a tab labelled "character" is a worse answer than an absent one.
            paths = [built(build, part) for part in parts]
            if all(paths):
                entry = {
                    "name": name,
                    "label": label,
                    "parts": [str(p) for p in paths],
                }

                # What he is holding, which is not what he is wearing. A held item is a
                # separate rigid model hung off a bone, so it is named separately and is
                # allowed to be missing: a character whose sword has not been built yet
                # is still a character, where a character missing a boot is not.
                for hand in ("right_hand", "left_hand"):
                    if (held := document.get(hand)) and (one := built(build, held)):
                        entry[hand] = str(one)

                # The one clip he stands in, for the ones that stand in one.
                #
                # A townsperson has no idle here and wants none: he carries two or three
                # actions of his own and the client re-rolls between them every time a cycle
                # wraps, which is what the scenery path already does. A player-model NPC is
                # the opposite case — the plate parts carry all 283 of player.bmd's clips and
                # SetPlayerStop picks exactly one of them and never picks again. Naming it is
                # what keeps the town from watching a guard sit down.
                if (idle := document.get("idle")):
                    entry["idle"] = idle

                # Whether anybody can *be* this one.
                #
                # A townsperson is assembled exactly like a player — a base rig with pieces
                # skinned to it — so she is a character by construction, and the tabs that let
                # you walk around as somebody were built by copying the character list whole.
                # That handed the world to Lumen the Barmaid, because npc/ sorts before
                # players/ and the walking figure is whoever is first.
                #
                # So the list says who is playable rather than the tab guessing. Every
                # character is still registered as something a map can stand up.
                entry["playable"] = document.get("playable", "npc" not in str(path.parent))

                # Whether she stands and walks as a woman.
                #
                # Two clips and nothing else. The client keeps this as a question about the
                # class rather than about the model — CCharacterManager::IsFemale(c->Class) —
                # and it decides which bare idle and walk are played and which voice screams.
                # PLAYER_STOP_FEMALE is 2 against the male 1, PLAYER_WALK_FEMALE is 16 against
                # 15, and everything with a weapon in it is shared: the crossbow stance is 9
                # and 22 whoever stands in it.
                #
                # Carried only when the asset says so, so a character that never mentions it
                # is a man by omission — which is the client's own default and keeps the
                # entry the shape it has always been for the ones already built.
                if document.get("female"):
                    entry["female"] = True

                # Which of the three classes this body is, where it is one of them.
                #
                # Absent on a guard and a barmaid, which is right: an NPC assembled out of
                # player parts is not a Dark Knight, and giving one a class would put a set
                # of levelling rates on somebody who never fights. The three bare bodies say
                # so, and the fight reads the class for the points a level one starts with
                # and for what a weapon will let them hold. See Rates.For.
                if (trade := document.get("trade")):
                    entry["trade"] = trade

                # And the ones that are not townspeople at all.
                #
                # A `monster` block on something with parts is MU's other way of making a
                # monster: CreateCharacter on MODEL_PLAYER with a body in Object.SubType, a
                # scale and weapons, which is the Skeleton Warrior. It is assembled like a
                # guard and it fights like a spider, so it is listed where the fight looks
                # for it - and the reader on the other side takes a list of parts under the
                # `glb` key as readily as a single path, which is what makes one row shape
                # serve both. See monster_entry.
                #
                # Not listed as a character as well. The Characters tab is who you can be
                # and who stands in the town, and a Skeleton Warrior is neither: listed
                # there it would be a body somebody could walk the town as, holding a
                # Gladius, with no death animation. Its own `playable: false` is now
                # redundant and is kept, because the asset should still say what it is.
                if document.get("monster") is not None:
                    monsters.append(monster_entry(
                        {"name": name, "label": label, "glb": entry["parts"],
                         **{k: v for k, v in entry.items()
                            if k not in ("name", "label", "parts")}},
                        document, root, build, combat, spawns,
                        rig_of(root, path, document)))
                    continue

                characters.append(entry)

            continue

        # Whose model this entry draws, which is usually its own.
        #
        # An asset may name another's with `mesh`, and the twelve spell scrolls are why:
        # Book01.bmd through Book12.bmd are byte-identical in MU - one md5 across the lot -
        # so group 15 is twelve rows on one model, and building eleven more copies of it
        # would put eleven identical atlases in the tree pretending to be different art.
        #
        # It is resolved here and nowhere else in this file, so everything below sees an
        # ordinary entry: its own name, its own label, its own row, and a glb that happens
        # to be shared. mu2.sh reads the same field to know it has no .obj to build.
        if (one := built(
                build, document.get("mesh", name), beside=path, root=root)) is not None:
            entry = {
                "name": name,
                "label": label,
                "glb": str(one),
                "kind": kind_of(one),
            }

            # Which map's scenery this is, where it is scenery at all.
            #
            # MU names most worlds' objects Object01 upward - only Lorencia runs named blocks
            # - so the moment a second such world is built the names collide: the character
            # scene's Object06 is a ruined arch and Noria's is a bush, and a lookup by bare
            # name hands whichever was read last to both maps. The world is in the path
            # already; naming it here is what lets a map ask for its own. See Index.Built and
            # World.Local.
            if entry["kind"] == "scenery":
                entry["world"] = one.parts[1]

            # What this asset changed about a library material for itself, verbatim.
            #
            # build_maps reads `material_overrides` and bakes the result into the ORM, and
            # until now that was the end of it: the baked number disagreed with the library
            # and nothing downstream could tell a deliberate override from a bad bake. The
            # Budge Dragon's wings are the case - leather restored to 0.74, which the library
            # took to 0.62 for the player's boots - and an audit that cannot read this
            # reports the restoration as the fault.
            #
            # Carried whole rather than resolved, because the merge is build_maps' own and
            # one level deep; a reader applies it the same way. Written here, above the
            # branch that sends a monster off to monster_entry, because a monster is as
            # likely to hold one as a house is - and this is the animal it was found on.
            if (tweaks := document.get("material_overrides")):
                entry["material_overrides"] = tweaks

            # How many keys MU authored each of this model's actions with.
            #
            # The one number the built file cannot answer. Godot's glTF importer resamples an
            # animation to a fixed rate on the way in, so a clip MU wrote in eleven keys
            # arrives with fifty - and every timing the client states is written in its own:
            # the blacksmith's hammer between frames 5 and 10 of an eleven-key swing is the
            # second half of the cycle, and measured against the imported clip it came out
            # the first fifth. The same mistake, and the same fix, as the player's actions —
            # see action_keys at the bottom of this file.
            #
            # Read straight off the asset's rig, which is where monster_effects already goes
            # for the same reason: it turns a through_key into a fraction at build time
            # because this is the only place the authored count is known. The model's rig
            # rather than the row's - see rig_of, and the Elite Bull Fighter, which has none
            # of its own because it has no model of its own.
            rigged = rig_of(root, path, document)

            if rigged.exists():
                counted = {
                    str(each["action"]): int(each["keys"])
                    for each in json.loads(rigged.read_text()).get("animations", [])
                    if each.get("keys") and each.get("action") is not None
                }

                if counted:
                    entry["action_keys"] = counted

                # And how far each travelling action carries the body, which is the other
                # number the built file cannot answer and for the opposite reason: the keys
                # are resampled away, the stride is deliberately thrown away. See strides.
                if (travelled := strides(rigged)):
                    entry["action_travel"] = travelled

            # A monster is one file like an object and is not one, so it is routed away here
            # before the object fields are considered.
            #
            # What it shares with an object is only its shape on disk: a single .glb with its
            # own skeleton and its own clips inside it, rather than the five parts a character
            # is assembled from. Everything else is different. An object is scenery — it has
            # one action if it has any, and the Objects tab plays it and offers no choice —
            # where a monster is authored to MU's twelve fixed slots and choosing between them
            # is the whole reason to look at one. And a monster carries two numbers no object
            # has: the client's model index, which is the key into the shared action table,
            # and the scale the client draws it at.
            #
            # Sorted by name so the tab's order is the tree's order and does not depend on
            # which asset was built most recently.
            #
            # It carries the metal list too, and for the same reason an object does: the
            # viewer takes the dielectric specular off everything *not* named here, and it
            # cannot tell which is which by looking. See the note below. A monster is mostly
            # not metal — a spider is a shell all over — but a Bull Fighter's axe is steel,
            # and a list left off is not the same as a list that is empty.
            if (beast := document.get("monster")) is not None:
                monsters.append(
                    monster_entry(entry, document, root, build, combat, spawns, rigged))
                continue

            # Which of its sheets are metal, by the name the .glb will call the material.
            #
            # For the renderer, which otherwise cannot tell. A glTF with an ORM map sets
            # metallicFactor to 1.0 and puts the real value in the map's blue channel, so the
            # engine's material reports metallic 1.0 for a plank wall and the only way to
            # learn otherwise is to read the texture back. The pipeline already knows - it
            # resolved the slot to a material file to build that map - and four of the
            # library's twenty-three are metal. Saying so costs a list of names.
            #
            # What it buys: everything not on the list can have its dielectric specular taken
            # down. MU's art has its highlights painted in and Fresnel makes any dielectric a
            # mirror at a grazing angle, so weathered timber came out looking varnished.
            if metal := metal_slots(document, root, build / one):
                entry["metal"] = metal


            # How a character stands while holding it. MU keeps this in the client rather
            # than in the .bmd — a sword and a two-handed sword are the same kind of file
            # and a different kind of stance — so the asset says it and it travels here.
            if (stance := document.get("stance")):
                entry["stance"] = stance

            # What it puts in the air, for the ones that put something there.
            #
            # A table in the client and not a property of the ammunition: CreateArrow
            # switches on the weapon's own model, so a Light Crossbow throws
            # MODEL_ARROW_LASER where the plain Crossbow throws MODEL_ARROW_STEEL and a bow
            # throws MODEL_ARROW - the same bolt in the quiver either way. Named on the
            # weapon because that is where MU decides it.
            if (fires := document.get("fires")):
                entry["fires"] = fires

            # And where in its own animation it lets go, for a weapon that has one.
            #
            # A fraction of the clip rather than a key, because what reads it plays the clip
            # in seconds. MU never runs these - see the animation_note on CrossBow04 - so
            # this is the one number in the missile path the client does not supply, and it
            # is read off the keys rather than chosen: see release_from on the asset.
            if (release := document.get("release")):
                entry["release"] = float(release)

            # And the bone the missile leaves from, for a weapon whose rig marks one.
            #
            # See muzzle_bone_from on the asset: MU's own offset is measured from the
            # character and lands in front of the chest, which is close enough for a bolt
            # nobody is looking at and wrong the moment you look at the weapon.
            if (muzzle := document.get("muzzle_bone")):
                entry["muzzle_bone"] = muzzle

            # And the bone behind it on the same rail, which is what gives the direction the
            # bolt leaves along. Two points make a line and the muzzle bone is a chain root.
            if (axis := document.get("muzzle_axis")):
                entry["muzzle_axis"] = axis

            # And how far off that bone the missile actually sits, in MU units. A bolt lies
            # in the groove on top of the rail and the bone marking the rail is on the
            # centreline, sixteen units under it.
            if (nudge := document.get("muzzle_offset")):
                entry["muzzle_offset"] = [float(v) for v in nudge]

            # Whether it leaves a hand free. Separate from the stance because MuWeapons.cs
            # keeps it separate: a wand has a stance of its own and one hand on it, a bow has
            # a stance of its own and both, so neither implies the other.
            if document.get("two_handed"):
                entry["two_handed"] = True

            # The item's own row - what a fight and a kill read. Only those columns are
            # carried, which is the same cut shared/Rows.cs makes: the asset holds the whole
            # of OpenMU's line with its provenance beside it, and the index holds the
            # numbers the simulation asks for. Carrying the rest would put the durability
            # curve and the option pool into a file the viewer parses every start.
            #
            # The drop level is here now and was not before. It is what decides whether a
            # monster is rich enough to leave a thing at all, so a loot pool built from the
            # index cannot be built without it - see Loot.Affordable. It is two integers per
            # item and it is the whole of MU's rule about what falls where.
            if (stats := document.get("stats")):
                entry["stats"] = {
                    field: stats[field]
                    for field in (
                        "group", "number", "drop_level",
                        "minimum_damage", "maximum_damage", "attack_speed",
                        # The footprint, which is a fact about the item and not about the
                        # window that draws it: an item occupies a rectangle of the
                        # inventory grid - a Small Axe one across by three down, a shield
                        # two by two - and both the bag's drawing and the server's "is
                        # there room" walk read it. Carried always rather than only where
                        # it is not one, because every row has one and a missing footprint
                        # is not a smaller item, it is an unknown one. See Satchel.Covered.
                        "width", "height",
                        # Armour's one number, from the same OpenMU row the damage came
                        # from. Read by Wear - see shared/Rows.cs - and summed into
                        # Player.ArmourDefense, which until this was fed by nothing.
                        "defense",
                        # A shield's block column, added whole to the defence rate
                        # (ArmorInitializerBase.CreateShield). Absent on everything else.
                        "defense_rate",
                    )
                    if field in stats
                }

                # Whether a monster ever leaves it, which is not implied by the drop level.
                #
                # A quiver of arrows has a drop level of nought and would head the pool of
                # anything that dropped at all; the original sells them and drops none, and
                # this is the flag that says so. Carried only where it is false, because an
                # absent field meaning "yes" is what the rest of this index does and an
                # item that nothing can leave is the rarer statement.
                if stats.get("drops_from_monsters") is False:
                    entry["stats"]["drops_from_monsters"] = False

                # Whether the row is in OpenMU's jewel drop group, which is a list of three
                # and not a rule about a group number - the Jewel of Chaos is group 12 with
                # the orbs and the wings, the other two are group 14 with the potions, and a
                # test on the group would take the Orb of Summoning with them. Carried only
                # where it is true, which is three rows in the whole table.
                #
                # It is a separate question from drops_from_monsters above, and the two were
                # confused for a while: that flag keeps a jewel out of the 30% random-item
                # pool and does nothing about this 0.1% group. See Prize.Jewel.
                if stats.get("jewel"):
                    entry["stats"]["jewel"] = True

                # The ceiling on the monster level that may leave it. One row in the 0.75
                # table sets one - the Jewel of Chaos, at 66 - so like the two above it is
                # carried only where it is there, and absent means no ceiling. See
                # Prize.MaximumDropLevel and Loot.Reaches.
                if stats.get("maximum_drop_level") is not None:
                    entry["stats"]["maximum_drop_level"] = stats["maximum_drop_level"]

                # And two columns that are only meaningful on some rows.
                #
                # Durability is wear on a sword and nothing reads it; on the two ammunition
                # rows it is the count of shots in the quiver, which the fight spends one at
                # a time. Carried only where it is that - see Arm.Durability.
                if stats.get("is_ammunition") and stats.get("durability"):
                    entry["stats"]["durability"] = stats["durability"]

                # And the wizard's damage, on the four rows that have one.
                #
                # OpenMU's magicPower: every blade and bow in the 0.75 table passes 0 and the
                # staves pass 6, 20, 34 and 46. Carried only where it is not zero, which is
                # the same shape as the durability above - an absent field means none, and
                # that is the rarer statement to write down. Nothing reads it yet; it is here
                # so that the day spells arrive is not the day the staff table is transcribed
                # a second time. See Arm.MagicPower.
                if stats.get("magic_power"):
                    entry["stats"]["magic_power"] = stats["magic_power"]

                # And the skill the item grants while it is held, on the rows that grant one.
                #
                # This is not decoration: in 0.75 it is the *only* way a character comes to
                # have a skill. There is no tree and no trainer, and the twelve scrolls are
                # the Dark Wizard's alone - so a Dark Knight has Lunge because he is holding
                # a Gladius, and stops having it when he puts the Gladius down. OpenMU says
                # the same thing in one line: Weapons.cs ends `if (skillNumber > 0) { ... }`
                # and hangs the skill off the item definition. See docs/skills.md.
                #
                # Carried only where it is not zero, which is the shape magic_power above
                # uses and the shape the number itself has: AT_SKILL_UNDEFINED is 0, so "no
                # skill" and "skill 0" are the same statement - the argument mu.db's
                # attack_skill column already settles for monsters.
                if stats.get("skill"):
                    entry["stats"]["skill"] = stats["skill"]

                # Who may hold it, which is the one requirement points cannot buy. A level
                # can be granted and strength can be granted; being an elf cannot, and every
                # bow and crossbow in the game is elf-only. See Crowd.Begin, which uses this
                # to decide who stands up to fight rather than to refuse the weapon.
                if (classes := stats.get("classes")):
                    entry["stats"]["classes"] = list(classes)

                # The requirements ride along even though the fight does not read them.
                # Whether a character can lift the thing is the first question anybody asks
                # of a weapon row, and a level one Dark Knight has 28 strength against the
                # Small Axe's 50 - so the answer is usually no, and it is much better said
                # than quietly assumed. See tests/weapons.py.
                if (requires := stats.get("requires")):
                    entry["stats"]["requires"] = {
                        field: value for field, value in requires.items() if value
                    }

            # What a scroll teaches, which is a block of its own and not a statistic.
            #
            # Deliberately not folded in beside stats.skill above, though the two are the same
            # english word. A weapon's `skill` is a bare number and means "while this is in your
            # hand"; a scroll's is a record - the number, what it asks of your energy, what it
            # costs - and means "once, and then the scroll is gone". Putting a block where a
            # number is read would make the key mean two shapes, and the first reader to take
            # the wrong one would get a silent nought.
            #
            # This block already sat in the twelve Book assets, written when the group was
            # built and read by nothing: Book01's own note says it is recorded "so that the day
            # skills arrive is not the day this table is transcribed a second time". This is
            # that day. See Prize.Teaches, which is where it lands.
            if (teaches := document.get("skill")):
                entry["skill"] = {
                    "number": int(teaches["number"]),
                    "level_requirement": int(teaches.get("level_requirement", 0)),
                    "energy_requirement": int(teaches.get("energy_requirement", 0)),
                }

            # Whether this is one of the pieces that gets out of the way when the player is
            # under it. The client keeps a list of types per world rather than a flag per
            # model; the flag is the same fact said where the asset can carry it.
            if document.get("roof_fade"):
                entry["roof_fade"] = True

            # Whether it is ground cover: grass, flowers, the leafy clutter a map is carpeted
            # in. Said by its sheets rather than by a list of names, because Noria's models
            # are numbers and a list would be forty guesses: every sheet foliage, or a glow
            # hung on foliage. Bark, timber or stone anywhere on it and it is a tree or a
            # thing, and keeps its shadow. The client adds the height test. See World.Cover.
            kinds = set((document.get("sheet_materials") or {}).values())

            # An asset can say so outright either way, for the foliage that is a thing — a
            # straw bale is cut-out grass and still stands in the way of the sun.
            covered = document.get("cover")

            if covered is None:
                covered = bool(document.get("world")) and "foliage" in kinds \
                    and kinds <= {"foliage", "lamplight"}

            if covered:
                entry["cover"] = True

            # Whether the character's own head is still drawn under this helm.
            #
            # MU keeps a BODYPART_HEAD beside BODYPART_HELM and normally sets it to -1: a
            # helm replaces the head. Six helms are named in SetCharacterScale's condition
            # and get the bare class head drawn underneath instead, because they are open -
            # a circlet, a coif, a band - and carry no face of their own. Without it the
            # Pad Helm floats over an empty neck. See the asset's keeps_head_from.
            if document.get("keeps_head"):
                entry["keeps_head"] = True

            # How fast its own action plays, where the client's rate is wrong for it.
            #
            # MU plays these at one speed and loops them without a pause, which is right for
            # a swaying tree and wrong for anything whose action is a *gesture*: a merchant's
            # animal shifting its weight forty times a minute reads as a tic. Slowing it is
            # the fix rather than pausing it — see Scenery, where stopping a single-clip
            # model holds whatever pose its last key is and freezes the animal mid-stride.
            #
            # Two numbers, the slowest and fastest, so two of a kind drift out of step.
            if (speed := document.get("animation_speed")):
                entry["animation_speed"] = [float(speed[0]), float(speed[1])]

            # Whether MU takes its action away from it entirely.
            #
            # `o->Velocity = 0.f` in ZzzObject.cpp's per-world switch, against the 0.16 that
            # CreateObject hands every object. It is not a slow speed and it is not the
            # absent field above, which already means "its own rate": it is the statement
            # that a model's keyframes are not an idle and are never to run. Lorencia's
            # treasure chest is the one - seven keys of a lid opening and shutting, which
            # looped forever until this said not to.
            if document.get("still"):
                entry["still"] = True

            # Drawn in a cell the other way up: the model's +Z at the bottom rather than the
            # top. The client fits an item to its cell by measuring it and standing the
            # longest axis positive-end up, which is right for a sword, whose tip is at +Z.
            # MU does not measure; RenderObjectScreen keeps an angle per model, and for the
            # bolt and the arrows it is a half turn about X short of a sword's - the quiver's
            # bolts stand at its -Z end and are shown heads-up. No measurement can tell a
            # case with bolts in it from a sword with a hilt, so this is the one bit of
            # that table carried over. See upended_from on Arrows01.
            if document.get("upended"):
                entry["upended"] = True

            # And the other bit of that table: a thing shown as it was authored, its own Y
            # up, with no measuring at all. The jewels are octahedra a hair wider than they
            # are tall - the Soul is 0.224 across and 0.219 high - so the longest-axis rule
            # stood the Bless on its point and the Soul on its side, and which of two near-equal
            # numbers wins is not a fact about the item. See upright_from on Jewel02.
            if document.get("upright"):
                entry["upright"] = True

            # How its self-luminous submesh moves, if it has one that does.
            #
            # A lit window, a bonfire's glow, a fountain's falling water: the client animates
            # these by writing one number to one material rather than through the model's
            # skeleton, so they are the one kind of animation this pipeline can carry today.
            # Everything else MU animates is frames in a .bmd and this builds from .obj.
            if (glow := document.get("glow")):
                entry["glow"] = glow

            # The lights it throws, which are not the same thing as the lights drawn on it.
            # MU keeps those apart — a glow submesh is rendered, a lamp is registered with
            # the terrain — and so does this: the glow is a material and this is a list.
            if (emitters := document.get("emitters")):
                entry["emitters"] = emitters

            objects.append(entry)

    # MU's own names for the actions travel with the index, so the viewer reads one file
    # and never has to know where assets/ is. A .bmd stores actions as a bare array with
    # no names in it, so without this a clip can only be called by its number — and there
    # are 284 of them.
    names, speeds = {}, {}
    catalogue = root / "players" / "rig" / "actions.json"
    if catalogue.exists():
        listed = json.loads(catalogue.read_text()).get("actions", {})
        names = {key: value["name"] for key, value in listed.items()}

        # And the rate each was exported at, which the viewer needs to scale it.
        #
        # The clip in the .glb is at MU's *base* play speed and nothing else, because that is
        # all a shared file can carry: the client's actual rate is `base + AttackSpeed *
        # 0.004`, and AttackSpeed is a property of the character swinging rather than of the
        # animation. A nimble Dark Knight swings visibly faster than a clumsy one in MU, and
        # the only way to reproduce that from one clip is to play it faster.
        #
        # So the base travels with the index and whoever plays the clip divides by it to
        # recover the multiplier. Without this the viewer would have to hard-code 0.6 for the
        # fist, which is the number that was wrong here in the first place.
        speeds = {
            key: value["play_speed"]
            for key, value in listed.items()
            if "play_speed" in value
        }

    # And how many keys MU authored each action with, which is not how many the clip has.
    #
    # This is the one number that cannot be recovered from the built file. MU's own
    # AnimationFrame counter runs 0 to keys-1 over a cycle, and every timing the client
    # states is written in it: the footsteps at 1.5 and 4.5, the weapon streak from 3, the
    # blacksmith's hammer between 5 and 10. Godot's glTF importer resamples an animation to
    # a fixed rate on the way in - a seven-key walk arrives with twenty-five - so asking the
    # imported clip how many keys it has answers a different question, and answering it put
    # both of a walk's footsteps in the first fifth of the cycle.
    #
    # So the authored count travels with the index, straight off the rig, and whatever wants
    # MU's frames converts against this rather than against what it can see.
    keys = {}
    rig = root / "players" / "rig" / "player.rig.json"
    if rig.exists():
        for one in json.loads(rig.read_text()).get("animations", []):
            if "action" in one and "keys" in one:
                keys[str(one["action"])] = one["keys"]

    # And how far each of the player's walks and runs carries him, for the same kind of
    # reason and for one more body than it sounds like: the Skeleton Warrior is a monster
    # built on the player's rig, so the distance its legs cover per cycle is in this table
    # and not in a monster's. See strides, and Crowd.Figure.Pacing.
    travel = strides(rig)

    # And the monsters' twelve, which are a different set of names for the same kind of
    # number and must not be confused with the player's.
    #
    # They travel separately because they are a separate namespace, not an extension of one.
    # Action 4 is PLAYER_STOP_SWORD on a character and MONSTER01_ATTACK2 on a spider, and
    # both files call the clip "action4" — so a single merged table would have the viewer
    # label a spider's second swing "Stop sword" and look, at a glance, like it was working.
    #
    # The hold list rides with them for the same reason it is in the asset: the client marks
    # the monster death as the one action that stops on its last key rather than wrapping,
    # and the viewer loops everything by default because glTF cannot say otherwise.
    monster_names, monster_holds = {}, []
    beasts = root / "monsters" / "actions.json"
    if beasts.exists():
        table = json.loads(beasts.read_text())
        monster_names = {
            key: value["name"] for key, value in table.get("actions", {}).items()
        }
        monster_holds = [int(one) for one in table.get("hold_at_end", [])]

    build.mkdir(parents=True, exist_ok=True)

    # The grades, which are built rather than authored: grade.py bakes each declaration into
    # a lookup cube and the viewer hands it straight to the renderer.
    graded = {}
    for cube in sorted((build / GRADES).glob("*.png")) if (build / GRADES).exists() else []:
        graded[cube.stem] = str(cube.relative_to(build))

    effects = {}
    # The skill icons, one file a skill, cut by skill_icons.py under the skill's own number:
    # skill_4 is Fire Ball because 4 is Fire Ball everywhere else in this project. Listed
    # off the directory rather than named one by one, because the number is the name.
    icons = {
        f"skill_{path.stem.split('_')[1]}": str(path.relative_to(root))
        for path in sorted((root / "interface" / "skills").glob("skill_*.png"))
    }

    # And the status icons the same way, under the state's name: buff_poison and its three
    # siblings, cut by buff_icons.py.
    icons.update({
        path.stem: str(path.relative_to(root))
        for path in sorted((root / "interface" / "buffs").glob("buff_*.png"))
    })

    for key, relative in {**EFFECTS, **icons}.items():
        source = root / relative
        if not source.exists():
            continue

        target = build / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        effects[key] = relative

    listed = worlds(root, build)

    # What the server needs of a map that is not in the map's own document: where a
    # character is put down, from the gates table, and which grade the world is looked at
    # through, by the grade sharing the world's name. A world with neither is still listed;
    # the viewer walks it, and the server refuses to raise it and says which is missing.
    gates = gate_rows(root.resolve())

    for world in listed:
        if (safe := gates.get(world["number"])) is not None:
            world["gates"] = {"safe": safe}

        world["grade"] = world["name"] if world["name"] in graded else ""

    heard = sounds(root, build)
    flying = missiles(root, build)

    # The materials library, flattened for the viewer.
    #
    # Every number in it was argued for in its own file and those arguments are the point of
    # the library — but they are of no use to a select, which needs a name and three
    # numbers. Carried through the index rather than read off the assets folder at run time,
    # because a built viewer is given build/ and nothing else, and a control that only works
    # when the source tree happens to be beside it is a control that works on this machine.
    stuff = []

    for path in sorted((root / "materials").glob("*.json")):
        try:
            one = json.loads(path.read_text())
        except (OSError, json.JSONDecodeError):
            continue

        stuff.append({
            "name": one.get("name", path.stem),
            "summary": one.get("summary", ""),
            "metallic": one.get("metallic", 0.0),
            "roughness": one.get("roughness", 0.6),

            # The grain's depth, which is the closest the library has to what the panel
            # calls normal depth. Not the same thing — the library's depth shapes a normal
            # map the pipeline generates, and the panel scales whatever normal is already
            # on the part — but it is the same judgement about how much relief a substance
            # should read with, and it is the number the asset would end up carrying.
            "relief": (one.get("grain") or {}).get("depth", 1.0),
        })

    # The whole monster table, built or not. The monsters list above is what the client can
    # draw, and a server that read it would raise only the breeds somebody had got round to
    # modelling - Lorencia's Bull Fighters vanished from the simulation the day the server
    # stopped reading mu.db, because there is no Bull Fighter model yet. A missing model is a
    # gap on the client; it is not a change to the game. See Content.Read, which takes this.
    breeds = [
        {"number": number, "combat": stats, "spawns": spawns.get(number, [])}
        for number, stats in sorted(combat.items())
    ]

    (build / "index.json").write_text(
        json.dumps(
            {
                "characters": characters,
                "objects": objects,
                "monsters": sorted(monsters, key=lambda one: one["name"]),
                "breeds": breeds,
                "actions": names,
                "action_speeds": speeds,
                "action_keys": keys,
                "action_travel": travel,
                "monster_actions": monster_names,
                "monster_holds": monster_holds,
                "effects": effects,
                "missiles": flying,
                # .get rather than [], because sounds() returns an empty dict when there is
                # no manifest at all and a build with no sound in it should still write an
                # index. It was subscripting, which turned a missing sounds.json into a
                # traceback halfway through the write.
                "sounds": heard.get("events", {}),
                "sound_onsets": heard.get("onsets", {}),
                "sound_gains": heard.get("gains", {}),
                "grades": graded,
                "worlds": listed,
                "materials": stuff,
            },
            indent=2) + "\n")

    print(
        f"  index      {len(characters)} character(s), {len(objects)} object(s), "
        f"{len(monsters)} monster(s), "
        f"{len(names)} action names, {len(effects)} effect(s), "
        f"{len(graded)} grade(s), {len(listed)} world(s), {len(stuff)} material(s)"
        f" -> {build / 'index.json'}")


# Guarded so mu2.sh can ask this file where a rig's actions belong without writing an
# index to find out. See actions_library: the build and the index have to agree on that
# path, and the only way to guarantee they do is for both to read it from here.
if __name__ == "__main__":
    main()
