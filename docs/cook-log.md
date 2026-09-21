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
| Sword04 | arm | 2026-09-21 19:01 | passed | polished blade bright at noon, warm at dusk, still reads at night; red wrap and tassel |
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
| Tree01 | world | 2026-09-21 23:20 | passed | scale-dependent sway now plays in the real town: canopy shape measurably differs between frame 0 and frame 145 with the camera held still (mean abs diff 4.2, max 233 over its crop); rate traced to ZzzObject.cpp's 1/Scale*0.4 |
| Tree02 | world | 2026-09-21 23:21 | passed | same scale-dependent rule as Tree01, same trace |
| Tree11 | world | 2026-09-21 23:21 | passed | plays its own clip at 1.0 (its natural bake); MU's ZzzObject.cpp comments out the scale-dependent case for this object type (10), so it falls to the flat 0.16 default -- but nothing traces what velocity its own bake assumed, so this is the honest default and not a verified rate |
| Tree12 | world | 2026-09-21 23:21 | passed | same as Tree11: plays its own clip at 1.0, untraced against MU's flat 0.16 |
| Tree13 | world | 2026-09-21 23:21 | passed | same as Tree11: plays its own clip at 1.0, untraced against MU's flat 0.16 |
