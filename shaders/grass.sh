// One card of grass, built from nothing.
//
// **MU2's Turf, with the placement rebuilt.** MU does not scatter blades: it cuts cards out of
// a painted sheet -- four 64-pixel columns of a tuft, `assets/effects/grass/<world>_TileGrassNN.png`
// -- and stands them on the land. MU2's Godot client did the same and scattered them, and that
// is the method here, because a painted column is eight or ten blades of grass for the fill of
// ONE quad. A geometric blade buys one blade for one strip; this was measured both ways in this
// engine and the card wins on everything the camera can see. docs/grass.md.
//
// What is new is everything round the card. MU's client stands one quad on each grass tile's
// own edge and calls it done; MU2 scattered them at a flat density with a jitter. Here a card's
// place, size, lean, arch, roll, colour and how it answers the wind are each their own draw
// with their own reason, and they are pulled together by two clump fields so that a field reads
// as tufts and patches rather than as confetti. The whole field is a pure function of
// (patch, card index): nothing is stored, streamed or uploaded.
//
// Included by vs_grass.sc and by nothing else. The prepass and the shade pass share that ONE
// vertex shader on purpose: the shade pass tests depth EQUAL against what the prepass laid
// down, so the two must agree to the last bit, and the only way to be sure of that is for them
// to be the same compiled code. Both of them alpha-test against the same sheet at the same
// threshold, for the same reason.
#ifndef MU2_GRASS_SH
#define MU2_GRASS_SH

#include "common.sh"

uniform vec4 u_grassCard;   // x: height m  y: width over height  z: lean  w: how far a card may be widened
uniform vec4 u_grassWind;   // xy: the wind's direction  z: its strength  w: time in seconds
uniform vec4 u_grassRoot;   // rgb: what the sheet is tinted towards at the root  w: the AO at the root
uniform vec4 u_grassTip;    // rgb: and at the tip  w: roughness
uniform vec4 u_grassVary;   // x: cards a patch  y: the stratification's side  z: the rank share  w: how dry a dry tuft goes
uniform vec4 u_grassSheet;  // x: columns  y: the alpha the cutout tests  z: a bias on the mip level, negative is sharper  w: unused
uniform vec4 u_grassSize;   // xy: THIS sheet's size in texels  z: a scale on the patch's density  w: how far the colour grade goes
uniform vec4 u_grassReach;  // x: metres from the eye past which no card stands  y: the band before it, over which a card shrinks away  z: where the thinning begins  w: where it has taken all it takes
uniform vec4 u_grassWalkers[8]; // xyz: somebody's feet, world space  w: how far round them the sward is parted (0 is an empty slot)

// --- the hash ----------------------------------------------------------------------------
//
// Dave Hoskins', and the change away from the familiar sin one is not a preference.
//
// The first build used `fract(sin(n * 12.9898) * 43758.5453)` off a seed the CPU fused as
// `column * 37 + row * 131 + index * 17.13`. That is broken twice over at this scale:
//
//   * **The seed is enormous.** On a 256-tile map it reaches forty thousand, and forty thousand
//     times 12.9898 is five hundred thousand -- where consecutive float32 values are further
//     apart than a twentieth of a radian. sin() is being sampled on a coarse lattice, so the
//     "hash" returns a handful of banded values instead of a spread. The randomness repeats,
//     visibly, and it repeats in bands across the map.
//   * **A seed fused by adding multiples collides.** 37 a column against 17.13 a card means
//     tile (c+11, r) card 0 lands within half a unit of tile (c, r) card 24 -- and a near-equal
//     input to a smooth function gives a near-equal answer. Whole cards are copies of cards a
//     few tiles away, on a lattice.
//
// This takes its inputs SMALL and SEPARATE -- the tile, the card index, a salt -- and mixes them
// with fract and a dot product. Neither failure applies, and there is no sin anywhere. It is
// still exactly reproducible, which is the one property it must have.
float grassHash(vec3 p)
{
	vec3 q = fract(p * 0.1031);
	q += dot(q, q.yzx + 33.33);
	return fract((q.x + q.y) * q.z);
}

vec2 grassHash2(vec3 p)
{
	vec3 q = fract(p * vec3(0.1031, 0.1030, 0.0973));
	q += dot(q, q.yxz + 33.33);
	return fract((q.xx + q.yz) * q.zy);
}

// What a card knows about itself. Filled once and read by the position, the normal and the uv,
// so the three cannot disagree about which card this is.
struct Card
{
	vec3 base;      // where it stands, world space
	vec3 top;       // where its top edge ends up, relative to base
	vec3 control;   // the Bezier's middle point, relative to base
	vec3 across;    // the width runs along this
	vec3 ground;    // the land's own up where it stands, before the card's own turn
	float width;    // metres
	float column;   // which 64-pixel column of the sheet it wears
	float tint;     // 0..1, its own brightness against its neighbours
	float hue;      // 0..1, which of Turf's four tints it wears (the top quarter wears none)
	float warm;     // 0..1, its own warmth or coolness, continuous
	float vigour;   // 0..1, the coarse field: how well the grass is doing here
	float bunch;    // 0..1, the fine field: how deep in a bunch it stands
};

// A smooth field over the world, for the drifts: value noise on a lattice `metres` wide,
// smoothstepped between corners, which is Turf's Patchy. A floor() field -- which is what the
// clump fields were first -- is a checkerboard of cells with a hard change at every edge, and
// on a colour that edge is a line across the sward. This has none.
float grassField(vec2 p, float metres, float salt)
{
	vec2 f = p / metres;
	vec2 i = floor(f);
	vec2 t = f - i;
	t = t * t * (3.0 - 2.0 * t);
	float a = grassHash(vec3(i, salt));
	float b = grassHash(vec3(i + vec2(1.0, 0.0), salt));
	float c = grassHash(vec3(i + vec2(0.0, 1.0), salt));
	float d = grassHash(vec3(i + vec2(1.0, 1.0), salt));
	return mix(mix(a, b, t.x), mix(c, d, t.x), t.y);
}

// The patch instance, unpacked:
//   i_data0 = (x, z of the patch's -x -z corner, height at (col, row+1), height at (col+1, row+1))
//   i_data1 = (height at (col, row), height at (col+1, row), density 0..1, unused)
//   i_data2 = (MU's baked light rgb, unused)
//   i_data3 = the paving at the same four corners as the heights, in the same order
//
// The two height pairs are the v = 0 and v = 1 edges of the tile, where v runs along +z. See
// docs/conventions.md: column is +x, row is -z, so the tile's corner is (column, -(row + 1)).
Card grassCard(vec4 d0, vec4 d1, vec4 d3, float index)
{
	// The meadow draw and the sward draw share this function and differ in a few places
	// below; the flag is the sheet's own, set by the renderer per draw.
	bool meadow = u_grassSheet.w > 0.5;
	Card c;
	// A plant is TWO cards crossed, as Turf's were: a lone card turned edge-on to the camera is
	// a line, where a blade among fifty is not missed. The meadow's index range is twice its
	// card count, and the second half is the same plants again turned a quarter round -- the
	// same hash, so the same place, size, kind and lean, and only the facing differs.
	float cross = 0.0;
	if (meadow)
	{
		cross = floor(index / u_grassVary.x);
		index -= cross * u_grassVary.x;
	}
	// The patch's identity is its own tile, in metres, which d0.xy already is -- small numbers
	// straight into the hash, rather than a big one the CPU fused. See the note on the hash.
	vec3 id = vec3(d0.x, -d0.y, index);

	// Where in the square metre it stands. Stratified rather than free: a free hash clumps and
	// leaves bald patches, and at this count the bald patches are what the eye finds. A
	// jittered grid has neither the regularity of a grid nor the holes of a hash.
	float side = u_grassVary.y;
	float row = floor(index / side);
	float col = index - row * side;
	vec2 cell = (vec2(col, row) + grassHash2(id + 1.7)) / side;

	float u = cell.x;
	float v = cell.y;
	float y = mix(mix(d0.z, d0.w, u), mix(d1.x, d1.y, u), v);
	c.base = vec3(d0.x + u, y, d0.y + v);

	// The land's own slope under the card, off the same four heights the position came from.
	// A card standing on a bank has to lean with the bank, or a hillside grows vertical grass
	// out of a slope and the field looks pasted on.
	float dhdu = mix(d0.w - d0.z, d1.y - d1.x, v);
	float dhdv = mix(d1.x - d0.z, d1.y - d0.w, u);
	c.ground = normalize(vec3(-dhdu, 1.0, -dhdv));

	// How far this card is from the EYE. Everything that changes with distance below -- the
	// thinning, the widening that pays it back, and the far edge where the field ends -- is
	// read off this one number, and off the eye rather than the focus on purpose: MU's camera
	// is rigid to the player, so a distance from the eye is a place on the SCREEN. A band
	// measured that way sits still in the frame as the player walks, and nothing ever crosses
	// it. Measured from the focus (which is what this did first) every band was a ring round
	// the player that moved with him, and the field's far edge was an arc a third of the way
	// down the frame with cards standing up out of the turf along it.
	float distance = length(u_camPos.xyz - c.base);

	// Thinned by shrinking whole cards away, never by fading them: there is no TAA here to
	// hold a half-transparent card still, and a dissolve on painted grass crawls.
	//
	// It is a RAMP and not a step, and that is not a nicety. The density falls with distance
	// and the camera moves; on a step, a card whose hash sits near the threshold switches on
	// and off between one frame and the next as the player walks. A field of those twinkles,
	// and that twinkle is what was reported as the grass shuttering. Over a band the same card
	// grows and shrinks instead, which is nothing the eye reports. The band is wide -- a sixth
	// of the density's range -- so that with the thinning spread over fifteen metres a card
	// takes two or three metres of walking to grow, which at a walk is a second.
	float keep = grassHash(id + 2.3);

	// The tufts. Two clump fields at two scales, because a meadow has two: a coarse one that
	// says how well the grass is doing here -- sun, water, what has walked over it -- and a
	// finer one that gathers cards into the bunches grass actually grows in. They are read off
	// the WORLD and not off the card, so neighbours agree and a tuft does not stop at the
	// patch's edge. Smooth, so that nothing they drive -- height, colour, the plants -- has an
	// edge of its own; the bunch field is a hard cell on purpose, because a bunch IS an edge.
	float vigour = grassField(c.base.xz, 4.5, 7.0);
	float bunch = grassHash(vec3(floor(c.base.xz * 1.6), 19.0));
	c.vigour = vigour;
	c.bunch = bunch;

	// The density this draw asks for. For the sward it is the patch's, thinned with distance,
	// which the widening below pays back: coverage held, card count down, and no painted blade
	// allowed under a pixel wide at the far edge. For the meadow it is Turf's: a rate of
	// plants a tile -- `grass_meadow` in u_grassSize.z is that rate over the cards on offer --
	// varied by a drift six metres across so the plants come in patches rather than one a tile
	// everywhere. Not thinned with distance: a seed head is one plant, not a sward, and a
	// missing one at the top of the frame is a missing plant.
	float far = saturate((distance - u_grassReach.z) / max(u_grassReach.w - u_grassReach.z, 0.1));
	float density = d1.z * u_grassSize.z;
	if (meadow)
	{
		density *= 0.3 + 1.4 * grassField(c.base.xz, 6.0, 41.0);
	}
	else
	{
		density *= 1.0 - 0.65 * far;
	}
	// The +1 puts the ramp's top AT the density rather than a sixth above it, so a patch at
	// full density keeps every card; without it the sixth of cards whose hash sits over 0.83
	// were being shrunk away from a sward that was asked for whole. The meadow's ramp is
	// narrow: its density is a few hundredths, and a sixth on top of that would be five
	// plants a metre.
	float alive = saturate((density - keep) * (meadow ? 40.0 : 6.0) + 1.0);

	// The paving. MU's overlay alpha at the tile's four corners, bilineared at the card's own
	// foot exactly as fs_ground bilinears it to draw the road, so the grass stops where the
	// cobbles start. A ramp, so the edge of the road is a sward getting shorter and thinner
	// into it over the fade MU painted, rather than a line of grass along the kerb.
	float paved = mix(mix(d3.x, d3.y, u), mix(d3.z, d3.w, u), v);
	alive *= 1.0 - smoothstep(0.12, 0.5, paved);

	// And the field's end, as a height and never an alpha. Over the last metres before the
	// reach a card shrinks into the turf, so the far edge of the field is a sward getting
	// shorter into the painted grass tile under it rather than a line of cards. At MU's 8 m
	// the reach sits past the far corners of the frame, so on flat ground the edge is never
	// in the picture at all; where a bank lifts the far ground into view, it is a fade.
	alive *= saturate((u_grassReach.x - distance) / max(u_grassReach.y, 0.1));

	// Which column of the sheet. MU rolls the column by the terrain ROW so the four tufts do
	// not line up into stripes across the map; a hash per card does the same job without a
	// table, and does it in two axes rather than one.
	//
	// The meadow chooses by Turf's weights for Lorencia -- seed heads and weeds mostly, and
	// the three flowers (cells 5, 6, 7) only where a drift seven metres across says the
	// field is in flower, so the flowers come in patches as they do in a meadow.
	if (meadow)
	{
		float blooming = smoothstep(0.35, 0.8, grassField(c.base.xz, 7.0, 53.0)) * 2.0;
		float w[8];
		w[0] = 3.0; w[1] = 2.0; w[2] = 3.0; w[3] = 2.0; w[4] = 2.0;
		w[5] = 0.8 * blooming; w[6] = 0.8 * blooming; w[7] = 0.3 * blooming;
		float total = w[0] + w[1] + w[2] + w[3] + w[4] + w[5] + w[6] + w[7];
		float roll = grassHash(id + 29.7) * total;
		float chosen = 7.0;
		float sum = 0.0;
		for (int i = 0; i < 7; ++i)
		{
			sum += w[i];
			if (roll < sum) { chosen = min(chosen, float(i)); }
		}
		c.column = chosen;
	}
	else
	{
		c.column = floor(grassHash(id + 29.7) * u_grassSheet.x);
	}

	// The height, out of four draws that each do a different job:
	//   the card's own        a sward is not level; no two tufts beside each other match
	//   the bunch's           bunches stand a little over or under their neighbours
	//   the wide field's      and whole patches are lusher or thinner than the rest
	//   the rank minority     a few stand well over the sward and seed
	// Only the last is a step, and it is what makes a field read as a field rather than as a
	// mown lawn: without it the whole sward has one top and looks trimmed.
	// A WIDE spread, on purpose: this is the random length the field is asked for, and a
	// narrow one reads as one plant at one size however many of them there are.
	// A plant's own spread is narrower (Turf's 0.8 to 1.2): a daisy is a daisy's size.
	float own = meadow ? 0.8 + grassHash(id + 7.7) * 0.4 : 0.46 + grassHash(id + 7.7) * 1.02;
	float rank = step(1.0 - u_grassVary.z, grassHash(id + 13.9));
	float scale = own * (0.84 + bunch * 0.32) * (0.84 + vigour * 0.36);
	scale *= mix(1.0, 1.85, rank);
	float height = u_grassCard.x * scale * alive;

	c.tint = grassHash(id + 11.3);
	c.hue = grassHash(id + 37.1);
	c.warm = grassHash(id + 41.9);

	// How stiff it is. A stiff tuft stands up and barely answers the wind; a floppy one leans
	// further, arches harder and whips. ONE draw, read by three things below, so the tuft that
	// leans a long way is also the one that arches and the one that moves -- which is what
	// makes the variation read as one plant rather than three unrelated wobbles.
	float stiff = 0.30 + grassHash(id + 19.3) * 0.70;
	stiff = mix(stiff, 0.86, rank);   // a rank tuft is stalks: stiffer, and narrower below

	// Which way it faces. Mostly its own, pulled towards its bunch's, so a tuft leans together.
	// Mostly its own. Pulled towards its bunch's, but not far: a tuft that leans together
	// is a tuft, and a tuft whose cards are all parallel is a fence.
	float yaw = (grassHash(id + 3.1) * 0.74 + bunch * 0.26) * 6.2831853 + cross * 1.5707963;
	vec2 facing = vec2(cos(yaw), sin(yaw));

	// The wind, in two bands over one direction: a slow sway the whole field shares and a
	// faster flutter each card keeps its own phase in. The slow band is advected along the
	// wind so a gust travels ACROSS the field rather than the field pulsing in place, which is
	// the difference between wind and a breathing carpet.
	float travel = dot(c.base.xz, u_grassWind.xy);
	float slow = sin(u_grassWind.w * 1.1 - travel * 0.45);
	float fast = sin(u_grassWind.w * 3.9 - travel * 1.7 + grassHash(id + 5.1) * 6.2831853);
	float gust = slow * 0.72 + fast * 0.28 * (1.45 - stiff);

	// The lean is the card's OWN, and the wind does not touch it.
	//
	// This used to add the wind into the lean vector and then normalise the sum, which turns a
	// gust into a rotation: the direction a card leans swings round towards the wind and back,
	// and a field of that is a field of spinning tufts rather than a field swaying. MU does not
	// do that. MU's own client adds a wind value straight onto the top vertex's position --
	// `TerrainVertex[0][1] += TerrainGrassWind[..]` -- and MU2's Turf kept it. It is a
	// displacement along one axis, which is what a sway is.
	//
	// So the card leans where its own habit and its own stiffness put it, always, and the wind
	// is added afterwards as a push on the top.
	vec2 ownLean = facing * u_grassCard.z * (1.24 - stiff * 0.48);
	float leanLength = length(ownLean);
	float reachFraction = min(leanLength, 0.93);
	vec2 leanDir = leanLength > 1e-5 ? ownLean / leanLength : facing;

	// Rotated, not stretched: the card keeps its height as it leans, so its own habit does not
	// grow the field. reach^2 + rise^2 = height^2.
	float reach = height * reachFraction;
	float rise = height * sqrt(max(0.0, 1.0 - reachFraction * reachFraction));
	c.top = vec3(leanDir.x * reach, rise, leanDir.y * reach);
	// The middle control point is what gives a tuft its arch: high and barely out stands and
	// then turns over at the top; lower and further out curves the whole way. Stiffness picks
	// between them, so a stalk stands and a floppy tuft bows.
	float controlRise = mix(0.46, 0.76, stiff);
	float controlReach = mix(0.34, 0.10, stiff);
	c.control = vec3(leanDir.x * reach * controlReach, rise * controlRise,
	                 leanDir.y * reach * controlReach);

	// The sway: one push along the wind's own direction, on the top, carried down the card by
	// the arch. It never turns the card and it never changes which way the card faces -- both
	// of those are the card's own and are settled above. A floppy card swings further than a
	// stalk, which is the one thing stiffness still says here.
	vec2 sway = u_grassWind.xy * u_grassWind.z * gust * height * (1.35 - stiff * 0.6);
	c.top.xz += sway;
	// A third of it at the middle, so the card BENDS into the gust rather than shearing over
	// as one rigid piece. A blade bends; a fence panel shears.
	c.control.xz += sway * 0.34;

	// The walkers: the hero and whatever of the crowd is standing in the field. Grass they
	// stand in is pushed out from under them and laid nearly flat, and it springs back as
	// they go: a card within a walker's reach is turned away from their feet by how near it
	// stands, the top brought down to a fifth of its rise. Turf's shove, without the wake --
	// a wake wants what they did a second ago, and nothing here remembers. The push is a
	// rotation like the lean, so the card keeps its length and the bounds stay true. The
	// strongest push wins rather than the sum, so two bodies side by side do not fold a card
	// through the ground between them.
	float push = 0.0;
	vec2 pushDir = facing;
	for (int i = 0; i < 8; ++i)
	{
		vec4 walker = u_grassWalkers[i];
		if (walker.w <= 0.0) continue;
		vec2 away = c.base.xz - walker.xz;
		float near = length(away);
		float here = saturate(1.0 - near / walker.w);
		here = here * here * (3.0 - 2.0 * here);
		// Only when they are near in height too: a bridge over the sward parts nothing under it.
		here *= saturate(1.5 - abs(c.base.y - walker.y));
		if (here > push)
		{
			push = here;
			pushDir = near > 1e-4 ? away / near : facing;
		}
	}
	if (push > 0.0)
	{
		vec3 flat = vec3(pushDir.x * height * 0.92, height * 0.22, pushDir.y * height * 0.92);
		c.top = mix(c.top, flat, push);
		c.control = mix(c.control, flat * 0.5 + vec3(0.0, height * 0.12, 0.0), push);
	}

	// The width axis: square to the lean, and rolled a little out of horizontal. Without the
	// roll every card in the field presents its face to the sky at the same angle and the
	// whole sward flashes together as the sun or the camera moves, which no real grass does.
	float roll = (grassHash(id + 23.1) - 0.5) * 1.35;
	c.across = normalize(vec3(-leanDir.y, sin(roll) * 0.55, leanDir.x));

	// The width off its own draw rather than off the tint, so a card's shape and its colour
	// are not the same number wearing two hats -- which is what made the variation read as
	// one axis instead of several.
	c.width = height * u_grassCard.y * (0.66 + grassHash(id + 31.7) * 0.74) *
	          mix(1.0, 0.72, rank);

	// The distance widening, which is the mesh-shader trick out of docs/grass.md: as cards are
	// thinned with distance the survivors are widened to hold the coverage. On a painted card
	// it also keeps the painted blades on it over a pixel wide, and a sub-pixel painted blade
	// under 4x MSAA with no TAA is the one aliasing problem this field really has. It runs over
	// the same band as the thinning, because it is the other half of the same mechanism.
	c.width *= 1.0 + far * u_grassCard.w;
	return c;
}

// A point on the card. `t` runs 0 at the root to 1 at the top edge; `side` is -1 or +1.
// `uv` comes back in the sheet's own space, the card's column already chosen.
void grassVertex(Card c, float t, float side, out vec3 wpos, out vec2 uv, out vec3 normal)
{
	vec3 p0 = vec3_splat(0.0);
	vec3 a = mix(p0, c.control, t);
	vec3 b = mix(c.control, c.top, t);
	vec3 along = mix(a, b, t);
	vec3 tangent = normalize(b - a + vec3(0.0, 1e-5, 0.0));

	// The card narrows a little towards the top, which is what the painted tuft does: the
	// blades on it spread from a root. Not to a point -- it is a tuft, not a blade.
	float width = c.width * (1.0 - t * 0.18);
	wpos = c.base + along + c.across * (width * 0.5 * side);

	float span = 1.0 / u_grassSheet.x;
	uv = vec2((c.column + side * 0.5 + 0.5) * span, 1.0 - t);

	// The normal, and this is where a card differs from a blade. MU's sheet has its own
	// lighting painted into it, and a card's true face normal points wherever the quad happens
	// to have been turned -- which on a scattered field is every direction at once, so lighting
	// by it makes the sward a field of randomly bright and dark patches. So the normal is
	// mostly the LAND's, which is what MU's own grass is lit by, turned a little towards the
	// card's face so that a tuft still catches the sun differently from the turf beside it.
	//
	// hexaquo and 2Retr0 both land here from the other direction, transferring normals off a
	// cylinder to imply a roundness the geometry has not got. Same trick, fewer steps.
	vec3 face = normalize(cross(c.across, tangent));
	normal = normalize(mix(c.ground, face, 0.30));
}

// The sheet, read at a level the card is allowed to go to.
//
// MU's sheet is four 64-pixel tufts side by side in one 256-wide picture, and a card's uv
// spans ONE of those columns. The hardware clamps at the TEXTURE's edge, not at a column's, so
// once the chain is blurred past a few texels a card starts averaging in the tufts either side
// of its own -- four different tufts smear into one grey-green smudge, and which smudge it is
// changes as the camera moves. So the level is worked out here and capped: past the cap a card
// keeps the sharpest sheet it is allowed instead of dissolving into its neighbours.
//
// The chain it reads is coverage-held (content::TextureRole::Cutout), so what this cap has to
// deal with is the column bleed alone; the thinning was dealt with at load.
vec4 grassSheet(vec2 uv)
{
	// PER AXIS. MU's sheets are 256 by 64, so a uv step of 1 along v crosses 64 texels and a uv
	// step of 1 along u crosses 256. Scaling both by the width -- which this did at first --
	// overstates the v derivative fourfold, and log2 of a fourfold derivative is TWO WHOLE MIP
	// LEVELS. Every card in the field was being read two levels blurrier than it should be,
	// which is what made the whole thing look smeared. docs/grass.md.
	vec2 texel = uv * u_grassSize.xy;
	vec2 dx = dFdx(texel);
	vec2 dy = dFdy(texel);
	float lod = 0.5 * log2(max(dot(dx, dx), dot(dy, dy)) + 1e-8);

	// And a sharpening bias on top, which alpha-to-coverage has earned: the trilinear filter
	// picks a level that just avoids aliasing, and the edges are being antialiased by the
	// coverage anyway, so half a level sharper costs nothing the eye can see and buys back
	// painted detail MU put there.
	lod += u_grassSheet.z;

	// The cap, worked out rather than passed. A column of the sheet is width/columns texels
	// wide, and below about eight texels a column the sampler -- which clamps at the PICTURE's
	// edge, not at a column's -- starts averaging in the tufts either side and a card dissolves
	// into a smudge that changes as the camera moves.
	float deepest = log2(max(u_grassSize.x / max(u_grassSheet.x, 1.0) / 8.0, 1.0));
	return texture2DLod(s_albedo, uv, clamp(lod, 0.0, deepest));
}

#endif  // MU2_GRASS_SH
