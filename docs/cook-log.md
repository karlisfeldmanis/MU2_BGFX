# Cook log

One line per item cooked with `tools/cook_one.py`: when, what, and whether its shots passed
a look. An item is not done when it is cooked; it is done when this says `passed`. Mark it
with `tools/cook_one.py --pass NAME "note"` or `--fail NAME "note"`.

| item | kind | cooked | look | note |
|---|---|---|---|---|
| Plate | set | 2026-09-21 21:02 | passed | rebuilt after the flip fix; steel with bright ridges, feather cut, fire on the plates at dusk and night. Darker at noon than its first pass because the stage's lighting moved since (ambient 0.95 to 0.8, midtone contrast and sharpen at the present), not the armour. First sheet broke mid-run when another session rebuilt the shaders; re-shot |
| Bronze | set | 2026-09-21 18:44 | passed | all five parts now rebuilt after the flip fix (helm, pants, gloves, boots joined the cuirass); look unchanged from the user's pass |
| Brass | set | 2026-09-21 17:25 | passed | gold again with plate_steel's grey pull off (desaturation 1.0, roughness 0.46, as Bronze); olive in the hollows as MU painted it; rebuilt after the bake flip fix |
| Scale | set | 2026-09-21 17:36 | passed | teal-green steel, aged not wet at roughness 0.55; scales and the chest face read; helm's green face plate is MU's paint; lighter than the sheet because the lift puts steel at its f0; rebuilt after the flip fix |
| Sphinx | set | 2026-09-21 17:47 | passed | aged gold over black as the recipe asks: gold stays gold at desaturation 0.88 (hue 37, sat ~0.3), not neon; nemes, collar and limb bands read as one set; rebuilt after the flip fix, which had it worst |
| Sword01 | arm | 2026-09-21 18:22 | passed | steel 0.2: sky sheen and a highlight along the wave at noon, warm at dusk; bronze-ish guard is MU's paint; shown without its bearer |
| Pad | set | 2026-09-21 17:58 | passed | wizard's own silver head shows under the winged gilt band (keeps_head, 6 parts); olive-and-gold cloth jerkin stays dielectric, brass gloves/boots/pant plates read gilt; flat gold wings are MU's cards; rebuilt after the flip fix |
| Vine | set | 2026-09-21 18:15 | passed | elf's own blonde head under the vine circlet (keeps_head); dark warm leather with the vine raised in the same brown; skin reads as skin, hip blades as bone; rebuilt after the flip fix |
| Sword02 | arm | 2026-09-21 19:13 | passed | steel 0.4 on this flat blade: grey steel with MU's paint legible, soft sheen at noon; no longer black |
| Leather | set | 2026-09-21 18:56 | passed | dark olive-black hide with MU's painted pale ridges, dielectric at 0.75; face and bare arms in skin; rebuilt after the flip fix |
| Sword03 | arm | 2026-09-21 18:59 | passed | thin blade bright steel at noon, cup guard catches a highlight into dusk, wrap grip reads |
| Sword04 | arm | 2026-10-06 10:25 | awaiting |  |
| Sword05 | arm | 2026-09-21 19:05 | passed | curved blade bright steel with its edge band, banded grip; reads at night |
| Bone | set | 2026-09-21 19:07 | passed | pale grey bone over dark hide, dielectric at 0.58 so the mid-tones carry between MU's painted highlights; skull mask and crest read; rebuilt after the flip fix |
| Sword06 | arm | 2026-09-21 19:09 | passed | royal blue is MU's own paint (sword07.png); winged guard and green-banded grip read; bright at noon |
| Sword08 | arm | 2026-09-21 19:14 | passed | steel 0.4: grey-blue steel with the painted wear visible; no longer black |
| Sword09 | arm | 2026-09-21 19:17 | passed | dark scaled blade is MU's paint, silver edge and scales read at steel 0.3; gold grip no longer blows out at brass 0.6. Guard claws render grey where MU paints them gold: only the grip is brass |
| Axe01 | arm | 2026-09-21 19:26 | passed | head dark forged iron at steel 0.55 (flat side-on face), haft wood reads, end cap wrought iron 0.7 no longer whites out; figures' copy recooked too |
| Axe02 | arm | 2026-09-21 19:30 | passed | iron head with a highlight on the spike at steel 0.55, grip end wrought iron 0.7 matte; shot with the bonfire at the frame's edge |
| Axe03 | arm | 2026-09-21 19:39 | passed | studio sheet: haft wrought iron 0.6 no longer whites out; gold grip 0.6; head shines to the fire side, dark to the far side (accepted) |
| Axe04 | arm | 2026-09-21 19:41 | passed | studio sheet: red haft and banded grip read at every angle, no blowouts; head dark away from the light (accepted) |
| Mace01 | arm | 2026-09-21 19:43 | passed | studio sheet: spiked head steel from most angles, a sun glint at noon and the fire's at dusk, wooden haft reads |
| Mace02 | arm | 2026-09-21 19:44 | passed | studio sheet: spiked ball bright steel at every angle, the fire's streak on the collar at dusk 90 deg; no rig in MU's data, rigid |
| Spear03 | arm | 2026-09-21 19:53 | passed | studio sheet: gold vamplate flared white at noon at brass 0.44; brass 0.6 spreads it; shaft reads gold toward the light, dark away (accepted). The 'mu2' in the report is the glb's slot name, the islands are brass |
| Bow01 | arm | 2026-09-21 19:56 | passed | studio sheet (row 1 is now the moonlit default): red-brown wood limbs, pale fletching and grip band read, no blowouts under dusk's sun; string and arrow in bind pose (held-item clip owed) |
| Bow02 | arm | 2026-09-21 19:58 | passed | studio sheet: red wood limbs with dark recurve tips, red grip wrap and pale fletching read in the moonlit default and dusk's sun, no blowouts; 'nocked' is export_gltf's arrow part, wood; bind pose (clip owed) |
| CrossBow01 | arm | 2026-09-21 20:00 | passed | studio sheet: orange-red stock, steel limbs and bolt, iron string frame read; the flat steel butt plate goes pale sky-grey from behind but does not bloom (physical, accepted); bind pose (clip owed) |
| Arrows01 | arm | 2026-09-21 20:06 | passed | studio sheet: MU's crossed-card case reads as warm wood; the bolts are MU's dark grey (91,90,90) on thin cards and stay dark -- steel 0.45 was tried and changed nothing, reverted |
| Arrows02 | arm | 2026-09-21 20:07 | passed | studio sheet: red-brown leather quiver, cream fletching and tan shafts at the mouth read in the moonlit default and dusk; no metal, no faults |
| Staff01 | arm | 2026-09-21 20:13 | passed | studio sheet: crossbar and horns bloomed into glowing bars under dusk's sun at brass 0.45; brass 0.6 keeps the gold and MU's dark mottling, blown pixels 164 -> 0 |
| Staff02 | arm | 2026-09-21 20:10 | passed | studio sheet: pale feather-and-bone staff, feathered head reads at every angle; dielectric, no blowouts |
| Staff03 | arm | 2026-09-21 20:17 | passed | studio sheet: round gold shaft and hook streaked with bloom under dusk's sun at brass 0.44; brass 0.6 keeps a gold line down the shaft without the flare |
| Staff04 | arm | 2026-09-21 20:16 | passed | studio sheet: MU's blue glass shaft and fins read at every angle, small brass tips catch the fire without blowing out |
| Shield01 | arm | 2026-09-21 20:28 | passed | studio sheet: boss spike was plank and glowed chalk-white; it is steel 0.4 now and shades facet by facet. Wood face orange-brown with MU's seams; the back is dark facing away from sun and fire (the camera orbits; accepted) |
| Shield10 | arm | 2026-09-21 20:26 | passed | studio sheet: dark ornate plate steel as MU paints it (sheet mean 60), border and boss read at noon and dusk; back dark away from the light (accepted) |
| Shield02 | arm | 2026-09-21 20:34 | passed | studio sheet: the three horns bloomed white as plate_steel cones; steel 0.4 shades them along their length with a small tip glint; face dark steel with MU's pale centre |
| Shield03 | arm | 2026-09-21 20:32 | passed | studio sheet: red rampant lion on gold reads at every front angle, painted leather, no metal; back dark away from sun and fire (ambient only, measured ~(20,11,6), not a hole) |
| Shield05 | arm | 2026-09-21 20:36 | passed | studio sheet: hammered plate steel with the skull boss and scalloped rim, a soft rim glint, no blowouts; back ambient-dark (accepted) |
| Shield06 | arm | 2026-09-21 20:38 | passed | studio sheet: gold dragon on blue reads from the front, the strapped back (its own sheet) reads with a warm rim; dielectric, no blowouts |
| Shield07 | arm | 2026-09-21 20:40 | passed | studio sheet: pale bone horns and the ribbed skull read from front and back, MU's banded paint; dielectric bone, no blowouts |
| Shield08 | arm | 2026-09-21 20:44 | passed | studio sheet: the twelve spikes were a column of white bloom as plate_steel cones; steel 0.4 shades them blue-grey with a glint or two; green-mottled face is MU's paint |
| Shield09 | arm | 2026-09-21 20:45 | passed | studio sheet: MU's blue face with yellow-green knotwork reads at every front angle, painted leather, navy back; no metal, no faults |
| Shield11 | arm | 2026-09-21 20:47 | passed | studio sheet: ornate plate steel face and raised rim read at noon and dusk, no blowouts; back ambient-dark (accepted) |
| Shield12 | arm | 2026-09-21 20:51 | passed | studio sheet: the gilt serpent head blew out at 225-315 deg at brass 0.44; at 0.6 it reads orange-gold with its scales; dark carved face and pale hide are MU's paint |
| Shield13 | arm | 2026-09-21 20:53 | passed | studio sheet: the gilt lion's face blew out at brass 0.44; at 0.6 its features read against MU's verdigris green |
| Waterspout01 | world | 2026-09-21 21:45 | passed | the fall now scrolls: two shots 90 frames apart show the ripple pattern moved down the pool; the engine log confirms ston02 scrolls 1.00/s, traced to MoveObject's BlendMeshTexCoordV |
| House04 | world | 2026-09-21 21:51 | passed | the round window's glow now scrolls (tile_space01, 1.00/s), same mechanism as the waterspout |
| House05 | world | 2026-09-21 21:51 | passed | the window's glow now scrolls (ston02, 1.00/s) |
| Bonfire01 | world | 2026-09-21 21:52 | passed | unaffected control: still just the brightness flicker, fire_02 has no scroll rate and none was added |
| Candle01 | world | 2026-09-21 21:52 | passed | unaffected control: brass gets its own material now (0.87 metal), candle2's flicker untouched, no scroll rate on this object |
| Tree01 | world | 2026-09-22 11:09 | passed | rebuilt on today's pipeline (clip recooked with it): canopy renders (70,57,37) against tree_a's own brown-olive (57,51,28), bark with its grain map reads down the trunk; edge-on cards streak as MU's do; above the fire's reach at night it is as dark as the ground round it |
| Tree02 | world | 2026-09-22 11:11 | passed | rebuilt on today's pipeline (clip recooked): big spreading oak, bark branches read through a ring of Tree_a cards -- the open crown from above is MU's mesh; brown-olive leaves as painted, roots lit by the fire at dusk and night |
| Tree11 | world | 2026-09-22 11:20 | passed | rebuilt on today's pipeline (clip recooked): dark conifer; MU paints tree_06 near-black (26,22,5) and the canopy renders (40,32,22) at noon -- faithful, not a lighting hole; reads as a silhouette at dusk and against the fire at night |
| Tree12 | world | 2026-09-22 11:21 | passed | rebuilt on today's pipeline (clip recooked): blossom tree, tree_04 is MU's pale lilac (163,147,164); canopy (133,116,108) at noon, pink at dusk, a faint moonlit violet (13,11,23) at night where the ground is (3,3,1) -- pale paint under the moon's sky, as Tree06's bark; bark trunk reads |
| Tree13 | world | 2026-09-22 11:22 | passed | rebuilt on today's pipeline (clip recooked): autumn tree, tree_05's orange-brown leaves read at noon and deep rust at dusk; the warm paint takes little of the moon's blue, so at night only the fire-lit trunk shows |
| SteelWall01 | world | 2026-09-25 11:53 | passed | one dark iron: black posts and bars, only the spear tips catch the sun at noon and the fire at night; no chrome rail (the f0 0.12 note holds); rebuilt and synced, no texture changed |
| SteelWall02 | world | 2026-09-25 11:53 | passed | the flower-bed run: black iron bars with pale tips, reads as one iron with SteelWall01 at noon, dusk and by the fire |
| SteelWall03 | world | 2026-09-25 11:53 | passed | the closing section: same black iron and pale tips as 01 and 02, no faults |
| SteelDoor01 | world | 2026-09-25 11:53 | passed | failed first: the scrollwork came out cream-white at noon and dusk though MU paints it dark brown (mean 28,21,13). steel_barred_door now takes the rails' f0 0.12 and a tenth of its paint; the gate reads as one black wrought-iron silhouette with the posts. Needed sync_one before the cook saw the rebuild |
| HouseEtc03 | world | 2026-09-25 11:58 | passed | failed first: the cage's cut-out bars came out pale grey-tan and the gate arch cream beside the black SteelWall runs. Both sheets now take f0 0.12 and a tenth of their paint; black iron at noon and dusk, the fire-side tip lit at night |
| Hound01 | figure | 2026-09-22 09:04 | passed | rebuilt after the flip fix: blue painted armour plates with MU's pale streaks, fur underside, pale claws and horn; brass head plate is the mesh MU hides, so it cannot blow out; reads at noon, dusk and by the fire at night |
| BudgeDragon01 | figure | 2026-09-22 09:10 | passed | rebuilt after the flip fix: red scaled hide and dark wing membranes read at noon, dusk and by the fire; no metal, paint as MU drew it |
| BullFighter01 | figure | 2026-09-22 09:11 | passed | rebuilt after the flip fix: brown fur with pale horns and hooves; the Elite shares this mesh and keeps its crest; no metal, no blowouts |
| Giant01 | figure | 2026-09-22 09:13 | passed | rebuilt after the flip fix: painted plate armour, metallic 0 by the recipe's own override (MU painted the highlights in); reads at every time of day |
| Lich01 | figure | 2026-09-22 09:14 | passed | rebuilt after the flip fix: black robe with red trim, pale skull mask and hands; cloth, bone and skin read apart |
| Spider01 | figure | 2026-09-22 09:15 | passed | rebuilt after the flip fix: dark carapace with MU's pale patch, banded legs read against the grass; no metal |
| Skeleton01 | figure | 2026-09-22 09:16 | passed | rebuilt after the flip fix: the Skeleton Warrior's body; bones read dark-ivory as MU paints them, at noon, dusk and night |
| Axe07 | arm | 2026-09-22 09:23 | passed | the Bull Fighter's axe: steel 0.55 as Axe01/02 -- at 0.2 the spike threw a white dot even at night; now the double head shows its engraving, no blowouts |
| Spear08 | arm | 2026-09-22 09:18 | passed | the Elite Bull Fighter's Berdysh: steel head and plate_steel shaft read grey along their length, no blowouts |
| Sword07 | arm | 2026-09-22 09:26 | passed | the Skeleton Warrior's gladius: steel 0.55 -- black at 0.2, dark grey at Sword02's 0.4; now grey steel with MU's painted wear, warm at dusk |
| Grass01 | world | 2026-09-22 10:57 | passed | rebuilt on today's pipeline (last built 2026-08-30): MU's low leafy ground cover, flat as its .bmd; dark teal-green leaves as tree_08 paints them read sage against the yellow turf at noon, warm at dusk, lit by the fire at night; foliage 0.9, no sheen |
| Grass02 | world | 2026-09-22 10:58 | passed | rebuilt on today's pipeline: same tree_08 ground cover as Grass01 in a sparser spread; dark teal-green leaves read against the turf at noon and dusk, by the fire at night |
| Grass03 | world | 2026-09-22 10:59 | passed | rebuilt on today's pipeline: four upright tufts on tree_01/tree_02, green blades with pale tips read at noon, keep their shape at dusk and in the fire's light; foliage 0.9, no sheen |
| Grass04 | world | 2026-09-22 11:00 | passed | rebuilt on today's pipeline: four tufts on tree_01/tree_02 as Grass03 in another spread; blades and pale tips read at noon, dusk and by the fire |
| Grass05 | world | 2026-09-22 11:03 | passed | rebuilt on today's pipeline: broad-leaf fern on tree_09; leaves render hue 60-90 (81,89,35), MU's olive (sheet hue 82) -- they read teal only beside the orange turf, measured by hue band; reads at noon, dusk and by the fire |
| Grass06 | world | 2026-09-22 11:04 | passed | rebuilt on today's pipeline: a smaller tree_09 fern as Grass05; olive leaves read at noon and dusk, by the fire at night |
| Grass07 | world | 2026-09-22 11:05 | passed | rebuilt on today's pipeline: a ring of red-capped mushrooms with pale spots and white stems; reads at noon, dusk, and by the fire at night |
| Grass08 | world | 2026-09-22 11:06 | passed | rebuilt on today's pipeline: three solid red-capped mushrooms on the mushroom sheet; drawn opaque (no dark-keyed cutout declared, unlike Grass07) and no black border shows -- the mesh covers only caps and stems |
| Tree03 | world | 2026-09-22 11:13 | passed | rebuilt on today's pipeline: bare dead tree, dark bark with its grain map reads along the limbs at noon and dusk, the trunk by the fire at night; no leaves in MU's mesh |
| Tree04 | world | 2026-09-22 11:14 | passed | rebuilt on today's pipeline: twisted two-trunk dead tree on tree_03 bark; limbs read at noon and dusk, trunk by the fire at night |
| Tree05 | world | 2026-09-22 11:15 | passed | rebuilt on today's pipeline: thin dead sapling on tree_03 bark, 3.5 m; reads at noon and dusk, dim beside the fire at night |
| Tree06 | world | 2026-09-22 11:16 | passed | rebuilt on today's pipeline: tall pale dead trunk with stubs; tree_02.jpg is MU's pale birch bark (176,170,165) and renders (188,172,161) at noon -- paint, not a blowout; stubs catch the dusk sun |
| Tree07 | world | 2026-09-22 11:17 | passed | rebuilt on today's pipeline: tree stump, tree_03 bark sides and tree_04 timber top with the rings read; lit by the fire at dusk and night |
| Tree08 | world | 2026-09-22 11:17 | passed | rebuilt on today's pipeline: fallen log on tree_01 bark, grain reads along it at noon and dusk, the fire picks out its length at night |
| Tree09 | world | 2026-09-22 11:18 | passed | rebuilt on today's pipeline: reed/sapling bundles on tree_07 (pale straw, 104,92,61 on the sheet); pale blades and tan stems read at noon and dusk, by the fire at night |
| Tree10 | world | 2026-09-22 11:19 | passed | rebuilt on today's pipeline: a wider field of tree_07 bundles as Tree09; straw stems and pale blades read at noon and dusk, by the fire at night |
| Stone01 | world | 2026-09-22 11:24 | passed | rebuilt on today's pipeline: MU's long boulder, ston01 grey rock warmed by the stage's sun like the stone building behind; its UVs wrap the tiling face 2.4x along the length, so the strata streak plank-like -- MU's mapping, not a wrong sheet; cast rock normal reads, the fire catches its edge at night |
| Stone02 | world | 2026-09-22 11:26 | passed | rebuilt on today's pipeline: upright boulder on ston01 with ston02 tufts at its foot; cracked grey faces read with the cast rock normal, cool where they face the sky at noon, warm at dusk, the fire's side lit at night |
| Stone03 | world | 2026-09-22 11:29 | passed | rebuilt on today's pipeline: faceted boulder on ston01, cracked grey rock with the cast normal's relief; reads at noon and dusk, lit on the fire's side at night |
| Stone04 | world | 2026-09-22 11:30 | passed | rebuilt on today's pipeline: three small stones on ston01, pale grey on the turf at noon and dusk, the fire's light on them at night |
| Stone05 | world | 2026-09-22 11:31 | passed | rebuilt on today's pipeline: a ring of stones on ston01, pale grey with the rock relief; reads at noon and dusk, fire-lit at night |
| Straw01 | world | 2026-09-25 11:42 | passed | tan straw bundles with their bindings read at noon, warm on the fire side at dusk, dark away from it at night; the softened comb does not crawl on the sheet. Judged on the uncommitted cutout_soften + uv_heal recipe |
| NewFace01 | bust | 2026-09-28 23:37 | awaiting |  |
| NewFace02 | bust | 2026-09-28 23:37 | awaiting |  |
| NewFace03 | bust | 2026-09-28 23:37 | awaiting |  |
| Object01 | world | 2026-10-03 22:22 | passed | blue-grey sea-worn boulders, pitted stone reads, coral at their feet (41,116 and 130,17) |
| Object02 | world | 2026-10-03 22:27 | passed | blue-grey sea-worn boulders, pitted stone reads, coral at their feet (41,116 and 130,17) |
| Object03 | world | 2026-10-03 22:32 | passed | blue-grey sea-worn boulders, pitted stone reads, coral at their feet (41,116 and 130,17) |
| Object04 | world | 2026-10-03 22:32 | passed | MU's small red and pale starfish lying on the sand by the basin's rocks (16,18 and 22,163) |
| Object05 | world | 2026-10-03 22:33 | passed | MU's small red and pale starfish lying on the sand by the basin's rocks (16,18 and 22,163) |
| Object06 | world | 2026-10-03 21:51 | passed | MU's cut-out coral and sea lettuce on clean edges, no dark fringe; fields read in muted --play at 184,24 and 152,152 |
| Object07 | world | 2026-10-03 21:53 | passed | MU's cut-out coral and sea lettuce on clean edges, no dark fringe; fields read in muted --play at 184,24 and 152,152 |
| Object08 | world | 2026-10-03 23:45 | passed | MU's giant octopus coiled round the wrecks at 56,56 and 14,212, its tentacles moving on its own clip; red skin reads dark blue-grey under the teal light |
| Object09 | world | 2026-10-03 23:47 | passed | MU's giant octopus coiled round the wrecks at 56,56 and 14,212, its tentacles moving on its own clip; red skin reads dark blue-grey under the teal light |
| Object10 | world | 2026-10-03 23:49 | passed | MU's giant octopus coiled round the wrecks at 56,56 and 14,212, its tentacles moving on its own clip; red skin reads dark blue-grey under the teal light |
| Object11 | world | 2026-10-03 23:51 | passed | MU's giant octopus coiled round the wrecks at 56,56 and 14,212, its tentacles moving on its own clip; red skin reads dark blue-grey under the teal light |
| Object12 | world | 2026-10-03 22:46 | passed | the wreck graveyard reads in the south-west court: ribs, planked hull sections, stove-in boats, leaning masts; planking under the court's dim teal light (24,224, 32,232, 15,214, 56,74) |
| Object13 | world | 2026-10-03 22:48 | passed | the wreck graveyard reads in the south-west court: ribs, planked hull sections, stove-in boats, leaning masts; planking under the court's dim teal light (24,224, 32,232, 15,214, 56,74) |
| Object14 | world | 2026-10-03 22:50 | passed | the wreck graveyard reads in the south-west court: ribs, planked hull sections, stove-in boats, leaning masts; planking under the court's dim teal light (24,224, 32,232, 15,214, 56,74) |
| Object15 | world | 2026-10-03 22:52 | passed | the wreck graveyard reads in the south-west court: ribs, planked hull sections, stove-in boats, leaning masts; planking under the court's dim teal light (24,224, 32,232, 15,214, 56,74) |
| Object16 | world | 2026-10-03 22:55 | passed | the wreck graveyard reads in the south-west court: ribs, planked hull sections, stove-in boats, leaning masts; planking under the court's dim teal light (24,224, 32,232, 15,214, 56,74) |
| Object17 | world | 2026-10-03 23:03 | passed | loose planks, broken frames and spars strewn over the sand and ridges at 168,168, 168,184 and 136,200; the brown planking reads jade green under the teal light -- asked of the user |
| Object18 | world | 2026-10-03 23:05 | passed | loose planks, broken frames and spars strewn over the sand and ridges at 168,168, 168,184 and 136,200; the brown planking reads jade green under the teal light -- asked of the user |
| Object19 | world | 2026-10-03 23:08 | passed | loose planks, broken frames and spars strewn over the sand and ridges at 168,168, 168,184 and 136,200; the brown planking reads jade green under the teal light -- asked of the user |
| Object20 | world | 2026-10-03 23:12 | passed | loose planks, broken frames and spars strewn over the sand and ridges at 168,168, 168,184 and 136,200; the brown planking reads jade green under the teal light -- asked of the user |
| Object21 | world | 2026-10-03 23:20 | passed | dark crags rising out of the floor at 62,212, mostly buried as MU stands them |
| Object22 | world | 2026-10-02 21:44 | awaiting |  |
| Object23 | world | 2026-10-02 22:30 | awaiting |  |
| Object24 | world | 2026-10-03 21:10 | awaiting |  |
| Object25 | world | 2026-10-03 23:28 | passed | broad green kelp blades in beds along the ridges, swaying on MU's 31-key clip, clean cut-out (198,90 and 138,114) |
| Object26 | world | 2026-10-03 23:28 | passed | broad green kelp blades in beds along the ridges, swaying on MU's 31-key clip, clean cut-out (198,90 and 138,114) |
| Object27 | world | 2026-10-03 21:55 | passed | MU's cut-out coral and sea lettuce on clean edges, no dark fringe; fields read in muted --play at 184,24 and 152,152 |
| Object28 | world | 2026-10-03 23:36 | passed | long weed, kelp and coral clumps swaying on MU's 31-key clip, clean cut-outs |
| Object29 | world | 2026-10-03 23:38 | passed | long weed, kelp and coral clumps swaying on MU's 31-key clip, clean cut-outs |
| Object30 | world | 2026-10-03 22:00 | passed | pale encrusted boulders over their weed beds, rock reads at 40,12 |
| Object31 | world | 2026-10-03 22:05 | passed | pale encrusted boulders over their weed beds, rock reads at 40,12 |
| Object32 | world | 2026-10-03 23:38 | passed | dark olive weed tufts clustered round the glyph stones at 40,56, swaying on MU's 15-key clip |
| Object33 | world | 2026-10-03 20:43 | awaiting |  |
| Object34 | world | 2026-10-03 23:38 | passed | dark olive weed tufts clustered round the glyph stones at 40,56, swaying on MU's 15-key clip |
| Object35 | world | 2026-10-03 20:48 | awaiting |  |
| Object36 | world | 2026-10-03 23:21 | passed | MU's skeleton remains among the wreck timbers at 28,218 |
| Object37 | world | 2026-10-03 23:22 | passed | MU's skeleton remains among the wreck timbers at 28,218 |
| Object38 | world | 2026-10-03 23:23 | passed | the drifting skeleton plays its own 60-key clip at 31,199 (pose differs 24 s apart) |
| Object40 | world | 2026-09-30 19:08 | awaiting |  |
| Object41 | world | 2026-10-03 21:21 | awaiting |  |
| Object42 | world | 2026-09-29 21:33 | awaiting |  |
| Object43 | world | 2026-09-29 21:28 | awaiting |  |
| Silk | set | 2026-09-28 21:05 | passed | purple quilted silk with gold trim and the visored hood, MU's own colours; skin islands right; nothing blown at noon, dusk or night |
| Wind | set | 2026-09-28 21:10 | passed | deep blue quilted set with the winged helm, MU's own; skin islands right; holds at all three times |
| Bow03 | arm | 2026-09-28 21:13 | passed | green elven limbs and binding, MU's own; arrow nocked |
| Bow04 | arm | 2026-09-28 21:14 | passed | the painted Battle Bow, MU's own colours; arrow nocked |
| Bow05 | arm | 2026-09-28 21:15 | passed | orange spined Tiger Bow, MU's own; arrow nocked |
| CrossBow02 | arm | 2026-09-28 21:16 | passed | reads silver at noon: the sheet's prod is MU's blue-grey band (80,97,118), not gold paint -- 'Golden' is the name, and the steel is physical |
| Shield04 | arm | 2026-09-28 21:18 | passed | pale feathered wings round a brass boss, MU's own; the wing edges are cut, not blended as MU blends them -- a closer look owed in the studio |
| Spirit | set | 2026-09-28 21:25 | passed | dark green plate with pale fins, MU's own; bare arms skin; one boot highlight flashes white at dusk, a highlight and not a material |
| Guardian | set | 2026-09-28 22:10 | passed | recooked with separate_meshes: the 44 arm triangles are skin at metal 0, the plate still plate_steel; MU's silver plate with gold trim |
| Object74 | world | 2026-09-29 20:38 | awaiting |  |
| Object59 | world | 2026-09-29 20:38 | awaiting |  |
| Object86 | world | 2026-09-29 20:38 | awaiting |  |
| Object70 | world | 2026-09-29 20:38 | awaiting |  |
| Object85 | world | 2026-09-29 20:39 | awaiting |  |
| Object100 | world | 2026-09-29 20:40 | awaiting |  |
| Object78 | world | 2026-09-29 20:41 | awaiting |  |
| Object71 | world | 2026-09-29 20:41 | awaiting |  |
| Object60 | world | 2026-09-29 20:41 | awaiting |  |
| Object79 | world | 2026-09-29 20:43 | awaiting |  |
| Object94 | world | 2026-09-29 20:43 | awaiting |  |
| Double Poleaxe | arm | 2026-09-29 21:14 | passed | steel 0.45, pole plate 0.5: silver crescents and green ornament read, no black head, no flare on the shaft |
| Halberd | arm | 2026-09-29 21:15 | passed | steel 0.45: silver blades, bronze-gold socket (brass); the round-shaft flare at 0.21 is gone |
| Flail | arm | 2026-09-29 21:38 | passed | frozen at action 1 (MU's rest), chain cards cut into 19 ring segments, bake_share 0.05 so the head keeps the map: handle, diamond grip, chain and spiked ball all read. Its swing clip is not played -- held items are rigid |
| Elven Axe | arm | 2026-09-29 21:39 | passed | wood haft reads warm; the crescent head is a one-sided card seen near edge-on in the studio's flat pose, plate_steel after steel stayed black; to be judged in the hand |
| Giant Trident | arm | 2026-09-29 21:18 | passed | steel 0.45 prongs, plate 0.5 pole: silver trident at noon and dusk, no flare |
| Sword of Salamander | arm | 2026-09-29 21:18 | passed | steel 0.45: white blade and gold guard from the lit side; on edge its shaded face mirrors the ground -- the pose, as the Serpent Sword lying flat shows |
| Object77 | world | 2026-09-29 20:57 | awaiting |  |
| Object82 | world | 2026-09-29 20:57 | awaiting |  |
| Object99 | world | 2026-09-29 20:58 | awaiting |  |
| Object57 | world | 2026-09-29 20:59 | awaiting |  |
| Object69 | world | 2026-09-29 20:59 | awaiting |  |
| Object39 | world | 2026-10-03 20:49 | awaiting |  |
| Object75 | world | 2026-09-29 21:00 | awaiting |  |
| Object83 | world | 2026-09-29 21:00 | awaiting |  |
| Object62 | world | 2026-09-29 21:01 | awaiting |  |
| Object64 | world | 2026-09-29 21:22 | awaiting |  |
| Object84 | world | 2026-09-29 21:22 | awaiting |  |
| Object63 | world | 2026-09-29 21:23 | awaiting |  |
| Object80 | world | 2026-09-29 21:24 | awaiting |  |
| Object96 | world | 2026-09-29 21:26 | awaiting |  |
| Object72 | world | 2026-09-29 21:28 | awaiting |  |
| Object98 | world | 2026-09-29 21:28 | awaiting |  |
| Object93 | world | 2026-09-29 21:29 | awaiting |  |
| Object45 | world | 2026-09-29 21:29 | awaiting |  |
| Object55 | world | 2026-09-29 21:29 | awaiting |  |
| Object97 | world | 2026-09-29 21:31 | awaiting |  |
| Object73 | world | 2026-09-29 21:31 | awaiting |  |
| Object76 | world | 2026-09-29 21:32 | awaiting |  |
| Object81 | world | 2026-09-29 21:32 | awaiting |  |
| Object91 | world | 2026-09-29 21:34 | awaiting |  |
| Object88 | world | 2026-09-29 21:35 | awaiting |  |
| Object67 | world | 2026-09-29 21:46 | awaiting |  |
| Object87 | world | 2026-09-29 21:47 | awaiting |  |
| Object89 | world | 2026-09-29 21:47 | awaiting |  |
| Object47 | world | 2026-09-29 21:48 | awaiting |  |
| Object54 | world | 2026-09-29 21:48 | awaiting |  |
| Object56 | world | 2026-09-29 21:48 | awaiting |  |
| Object51 | world | 2026-09-29 21:49 | awaiting |  |
| Object52 | world | 2026-09-30 19:12 | awaiting |  |
| Object50 | world | 2026-09-29 21:50 | awaiting |  |
| Object68 | world | 2026-09-29 21:50 | awaiting |  |
| Object44 | world | 2026-09-29 21:52 | awaiting |  |
| Object46 | world | 2026-09-29 21:52 | awaiting |  |
| Object95 | world | 2026-09-29 21:52 | awaiting |  |
| Object66 | world | 2026-09-29 21:53 | awaiting |  |
| Object48 | world | 2026-09-29 21:53 | awaiting |  |
| Object49 | world | 2026-09-29 21:53 | awaiting |  |
| Object53 | world | 2026-09-29 21:54 | awaiting |  |
| Object58 | world | 2026-09-29 21:54 | awaiting |  |
| Object65 | world | 2026-09-29 21:55 | awaiting |  |
| Legendary Sword | arm | 2026-09-29 22:17 | passed | steel 0.45: twin serrated white blade and purple guard from the lit side; on edge its shaded face goes dark, the pose as with the Salamander |
| Larkan Axe | arm | 2026-09-29 22:17 | passed | brass head reads as MU's ornate gold double axe, dark grip and haft; silver bit reads warm (shares the brass island) |
| Serpent Spear | arm | 2026-09-29 22:18 | passed | silver blades, gold serpent collars (brass), plate pole at 0.5: reads at noon from every angle |
| Gorgon Staff | arm | 2026-09-29 22:19 | passed | bone skull, dark plate frame with gold trim, tan shaft; two-handed as 0.75 |
| Blade | arm | 2026-09-29 23:04 | awaiting |  |
| Serpent Shield | arm | 2026-09-29 23:03 | passed | fangs glow as MU's two thin yellow fangs at BlendMeshLight alone (were orange bars at the world's glow_strength 2.0); ivory scrolls and salmon tail tip are MU's paint (raw-sheet render) |
| Bronze Shield | arm | 2026-09-29 23:05 | awaiting |  |
| Light Spear | arm | 2026-09-29 23:07 | awaiting |  |
| Double Blade | arm | 2026-09-29 23:08 | awaiting |  |
| Legendary Shield | arm | 2026-09-29 23:11 | awaiting |  |
| Dragon | set | 2026-09-30 09:17 | passed | full set: red lacquer and bone-gold helm, scaled legs, clawed gauntlets and greaves; helm at the body's first override (0.42/0.70/0.55) since its horns mirrored the fire as glowing spikes at the body's 0.50/0.60/0.42; whole, no floating parts |
| Short Bow | arm | 2026-09-29 23:42 | awaiting |  |
| Bow | arm | 2026-09-29 23:42 | awaiting |  |
| Elven Bow | arm | 2026-09-29 23:42 | awaiting |  |
| Battle Bow | arm | 2026-09-29 23:42 | awaiting |  |
| Tiger Bow | arm | 2026-09-29 23:42 | awaiting |  |
| Legendary | set | 2026-09-30 09:06 | passed | full set: blue enamel under gold filigree on helm, mantle, tasset, cuffs and greaves; face and fingers skin (glove islands 2-3), toe caps brass 0.6; whole, no floating parts, no see-through |
| Crossbow | arm | 2026-09-29 23:42 | awaiting |  |
| Golden Crossbow | arm | 2026-09-29 23:44 | awaiting |  |
| Arquebus | arm | 2026-09-29 23:45 | awaiting |  |
| Light Crossbow | arm | 2026-09-29 23:45 | passed | played on the shot: string and prod draw through action 51 on the CrossbowGuard bench, key 0 identical to the rigid rest |
| Sword16 | arm | 2026-09-30 00:07 | passed | broad grey steel blade with dark grip, whole and solid at noon/dusk/night, no blowout |
| Spear09 | arm | 2026-09-30 00:10 | passed | dark blue-steel crescent blade, thin pole and spike tip read whole; small highlight on the collar only, no blowout |
| Worm01 | figure | 2026-09-30 09:22 | passed | pale shaggy hide, red maw and fangs read at noon, dusk and night; fur, no sheen |
| Assassin01 | figure | 2026-09-30 09:37 | passed | black robes with silver trim, a Katache in each hand with red grips; blades steel without blowout, readable at night |
| Staff06 | arm | 2026-09-30 09:57 | passed | fixed by dropping Staff06.rig.json (static 4-bone rig borrowed player.rig.json and 'Mesh01' matched the player's hand bone, tearing 40 verts off); re-shot at 2.0 and 3.2 m: cyan orb head, blue-gold wings at the foot, knot mid-shaft, MU's added shield11_a shimmer as the shaft between -- continuous, no gaps |
| Sword15 | arm | 2026-09-30 09:45 | passed | steel 0.45 blade with sword15_2's blue lightning added and breathing over it, ornate guard and grip whole; no blowout |
| Hommerd01 | figure | 2026-09-30 09:47 | passed | blue painted plate with silver trim, crested helm, dark greaves; metallic 0 keeps it paint, not chrome; axe and shield judged in the arena |
| Sword13 | arm | 2026-09-30 09:50 | passed | silver blade with cut-out serrated teeth along both edges, brass 0.6 winged guard and pommel; whole, teeth solid not rectangles |
| Spear10 | arm | 2026-09-30 09:53 | passed | silver-red bill with its veins cut through as MU alpha-tests af.tga, red haft (plate, desaturation 1.0) and black spikes; whole, no floating parts |
| EliteYeti01 | figure | 2026-09-30 09:54 | passed | pale grey shaggy pelt with dark marks, steel-blue spikes on crown and shoulders; fur and chitin, no sheen |
| Bow06 | arm | 2026-09-30 10:01 | passed | silver steel limbs with gold scroll paint, nocked arrow and string drawn, cut-out string cards solid; whole at noon/dusk/night |
| IceMonster01 | figure | 2026-09-30 10:12 | passed | MU's additive ice body with its V scroll, unchanged; half-strength shadow (ours) outlines it on Devias's snow -- approved by the user in game |
| CrossBow05 | arm | 2026-09-30 10:12 | passed | serpent-scaled limbs and stock in brass 0.6 with dark scale paint, bolt on the rail and string drawn mid-clip; whole, no blowout |
| CrossBow06 | arm | 2026-09-30 10:20 | passed | drawn as MuMain draws it: ZzzObject.cpp:5243-5246 gives MODEL_BLUEWING_CROSSBOW BlendMesh -2 with BlendMeshLight sin*0.3+0.7, and ZzzBMD.cpp:1509 draws every mesh added when BlendMesh <= -2, so the whole crossbow is a breathing additive ghost; jk00 ships as a glow, stock and wings whole, pale steel-blue and see-through. jk01's coincident shell is welded away |
| IceQueen01 | figure | 2026-09-30 14:10 | awaiting |  |
| BloodCastle02 | figure | 2026-09-30 12:05 | passed | plate drawn twice as MU does (opaque + added), shines pale blue with the white wings, as the user's reference |
| DevilNpc01 | figure | 2026-09-30 12:18 | passed | robe and gilt plate as painted; the orb in his hands and its wisps drawn in code |
| Bat01 | world | 2026-09-30 18:21 | awaiting |  |
| Rat01 | world | 2026-09-30 18:21 | awaiting |  |
| Saw01 | world | 2026-09-30 19:28 | awaiting |  |
| Larva01 | figure | 2026-09-30 19:39 | passed | dark umber grub hide with its ring folds and a red maw at noon, dusk and night; skin, no sheen; fang rows bone (cut from snake02); whole at 0.6 |
| DungeonStone01 | world | 2026-09-30 19:48 | awaiting |  |
| Ghost01 | figure | 2026-09-30 19:50 | passed | grey hooded shroud on long clawed arms, matte cloth throughout (the feathered hem is welded into the shroud's island); whole at noon, dusk and night; MU's 0.4 AlphaTarget declared, not yet drawn |
| Axe09 | arm | 2026-09-30 20:13 | passed | silver crescent head with its engraved serpent scroll and painted dark edge, steel 0.45; thin plate haft and leather wraps whole; no blowout at noon, dusk, night |
| Cyclops01 | figure | 2026-09-30 20:30 | passed | dark hide and tan leg wraps, worn grey pauldrons with horns at plate_steel 0.6 (0.35 blew out white), bone skull buckle; whole at noon, dusk, night |
| HellSpider01 | figure | 2026-09-30 20:41 | passed | dark shell legs with gold bands and the gold-ridged torso, chitin with a soft sheen, no glare; whole at noon, dusk and night |
| DarkKnight01 | figure | 2026-09-30 20:59 | passed | black enamel plate with raised gold filigree (painted_steel), crescent crest, brown cloth cape hanging in play (juts only in the stage pose); gold bright but no blowout at noon, dusk, night |
| Gorgon01 | figure | 2026-09-30 21:21 | passed | black plate with bronze trim and studs (painted_steel), bone skull face, green added eyes in the sockets; no blowout at noon, dusk, night |
| DeviasTrader01 | figure | 2026-10-01 23:06 | awaiting | Thompson; judged only on a Blender render of the glb (no stage shot, not placed): white-bearded old man in a fur-trimmed cap and brown leather with steel cups, a steel blade held flat over his timber whetstone wheel; textured, not inside out |
| kalnpc | figure | 2026-10-01 23:13 | awaiting | Oracle Layla; judged only on a Blender render of the glb (no stage shot, not placed): hooded woman in a white robe with red vine paint sitting on a carved sandstone crate of red, blue and violet bottles; textured, not inside out; the sandstone reads coarse |
| NpcSenatus | figure | 2026-10-01 23:40 | awaiting | Senior (223), in Layla's place; judged only on a Blender render of the glb (no stage shot, not placed): white-bearded old man, deep blue hooded robe with a gold mantle, gold belt and gold leaf at the hems, a pale panel down the front, a thin steel staff with a gilt head in his right hand; textured, not inside out. MU's scale 1.1 is the placement's |
| tersia | figure | 2026-10-02 00:20 | awaiting | Tersia (566), in Senatus's place; judged only on a Blender render of the glb (no stage shot, not placed): long purple hair, dark purple-black leather armour with copper trim and a jagged cut-out skirt, armoured greaves and boots, an open parchment in one hand and a quill in the other; textured, not inside out, cut-outs whole. MU's scale 0.93 is the placement's; her idles play at the flat 0.25, not MU's 0.35/0.3; the chrome overlay on her armour is not drawn |
| Mace04 | arm | 2026-10-02 16:55 | awaiting |  |
| Sword11 | arm | 2026-10-02 17:17 | awaiting |  |
| Mace05 | arm | 2026-10-02 17:16 | awaiting |  |
| Chaos Dragon Axe | arm | 2026-10-02 21:30 | passed | studio 8x3: silver-red blades read at noon and dusk, the ft72 flame breathes red at the edges; the far face goes dark in shade as the Legendary Sword's does, no blowouts |
| Chaos Nature Bow | arm | 2026-10-02 21:36 | passed | studio 8x3: the whole bow an additive green ghost on bow77, as MU's BlendMesh -2 draws it; silver fittings bright but not blown, holds at night |
| Chaos Lightning Staff | arm | 2026-10-02 21:42 | passed | studio 8x3: deep blue lacquered head and spikes, gold rings; the u2u2 streaks flicker blue round the head and foot; no blowouts |
| Crow01 | world | 2026-10-02 21:56 | awaiting |  |
| Orc01 | figure | 2026-10-03 21:34 | awaiting |  |
| Bali01 | figure | 2026-10-03 11:24 | passed | summoned in a muted Lorencia arena, borrowed from figures_noria.json (Bali spawns nowhere; summoned_in noria): teal muscled hide in skin, the loin plate, bracers and horn bases in brass without blowout, the cyan mane cut out; about twice the elf's height at MU's 0.12 |
| Staff11 | arm | 2026-10-03 12:44 | awaiting |  |
| SaintStatue01 | figure | 2026-10-03 13:18 | awaiting |  |
| OrcArcher01 | figure | 2026-10-03 21:40 | awaiting |  |
| DarkSkull01 | figure | 2026-10-03 21:43 | awaiting |  |
| RedSkeleton01 | figure | 2026-10-03 21:46 | awaiting |  |
| Sword20 | arm | 2026-10-03 22:15 | awaiting |  |
| Bow19 | arm | 2026-10-03 22:19 | awaiting |  |
| Shield14 | arm | 2026-10-03 22:36 | awaiting |  |
| Mace06 | arm | 2026-10-03 22:42 | awaiting |  |
| CrossBow07 | arm | 2026-10-03 22:49 | awaiting |  |
| Staff07 | arm | 2026-10-03 23:15 | awaiting |  |
| Fish02 | world | 2026-10-04 00:24 | passed | the school reads at 184,24: small cichlids shoaling past the corals, tethered round the hero |
| Bow18 | arm | 2026-10-05 10:28 | awaiting |  |
| Wing02 | figure | 2026-10-05 19:42 | awaiting |  |
| Wing03 | figure | 2026-10-05 19:42 | awaiting |  |
| Sword17 | arm | 2026-10-06 00:13 | awaiting |  |
| Staff09 | arm | 2026-10-06 00:17 | awaiting |  |
| CrossBow17 | arm | 2026-10-06 00:22 | awaiting |  |
| Spear11 | arm | 2026-10-06 00:28 | awaiting |  |
| Shield16 | arm | 2026-10-06 00:39 | awaiting |  |
| Shield17 | arm | 2026-10-06 00:44 | awaiting |  |
| Black Dragon | set | 2026-10-06 07:01 | awaiting |  |
| Mace08 | arm | 2026-10-06 10:08 | awaiting |  |
| Staff10 | arm | 2026-10-06 09:53 | awaiting |  |
| CrossBow20 | arm | 2026-10-06 09:56 | awaiting |  |
| Mace03 | arm | 2026-10-06 10:27 | awaiting |  |
| IronWheel01 | figure | 2026-10-06 11:42 | awaiting |  |
| Tantalos01 | figure | 2026-10-06 11:44 | awaiting |  |
| Zaikan01 | figure | 2026-10-06 11:06 | passed | ember figure, all three meshes added, half shadow |
| BeamKnight01 | figure | 2026-10-06 11:07 | passed | dark bat knight, mane cut out |
| DeathBeamKnight01 | figure | 2026-10-06 11:07 | passed | pale ghost of the Beam Knight, all added |
| Mutant01 | figure | 2026-10-06 11:46 | awaiting |  |
| BloodyWolf01 | figure | 2026-10-06 11:07 | passed | hide, cloth and mane on their own materials |
| GoldenDragon01 | figure | 2026-10-06 18:28 | awaiting |  |
| Alquamos01 | figure | 2026-10-06 20:26 | passed | MU's translucent cyan star-dragon, all added with its star specks; reads at noon, dusk and night and over Icarus's navy |
| MegaCrust01 | figure | 2026-10-06 21:10 | passed | blue-grey cast plate reads as painted armour at noon, dusk and night; the flame banner glows in its wing frame; Thunder Blade in hand |
| QueenRainer01 | figure | 2026-10-06 21:45 | passed | MU's translucent violet winged woman, every mesh added, her star-field gown and banded body read at noon, dusk and night; no shadow |
| Drakan01 | figure | 2026-10-06 21:58 | passed | black scaled serpent with blue plates, red eyes and gold jaw, shot from 10 m; dark at night without MU's chrome passes, lit in Icarus by its blue stars |
| AlphaCrust01 | figure | 2026-10-06 22:17 | passed | the Crust in MU's teal BodyLight, its flames green; +9 Thunder Blade in hand; reads at noon, dusk and night |
| PhantomKnight01 | figure | 2026-10-06 23:28 | passed | dark spiked winged knight, its runes a faint violet (0.35) after a first cut at 0.75 read as loud stripes; Dark Breaker in hand |
| GreatDrakan01 | figure | 2026-10-06 23:40 | passed | the Drakan in MU's ExtraMon light, black with red plates, shot from 10 m; nearly gone at stage night, as MU's, its head fire marking it |
