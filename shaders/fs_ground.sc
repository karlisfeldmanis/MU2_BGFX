$input v_wpos, v_texcoord0, v_normal, v_colour, v_vnormal, v_vpos, v_weight

// The land, lit. Up to three full material sets blended by the weights Ground::splat shares
// between every tile on a corner, under MU's own baked terrain light. This is the one surface that does not fit the closed
// material model in docs/conventions.md, and it is a second shader rather than a fourth flag.
// Stage 15 is the probe's, which the land never reads. abyss() in common.sh.
#define MU2_ABYSS 15
#include "common.sh"

#include "shadow.sh"
#include "lights.sh"

uniform vec4 u_groundRepeat;  // xyz: each layer's repeat  w: the bite
uniform vec4 u_groundBlend;   // x: the flow's cycle  y: its band's row, or -1  z: reach, or MU's slide  w: layers
uniform vec4 u_groundRelief;  // xyz: each layer's relief  w: which layers are water, a bit each
uniform vec4 u_waterGlow;     // rgb: the water sheet's own light, the sheet's water_glow (lava)  w: water_variety
uniform vec4 u_groundWet;     // x: how wet the ground is (ground_wet)  y: how much lies as puddles
uniform vec4 u_groundSlots;   // xyz: each layer's slot in the weight map  w: 1 when it is bound
uniform vec4 u_groundWeights; // xy: the weight map's size in texels  z: rows a band  w: pad rows
uniform vec4 u_caustic;       // x: MU's caustic frame, 0-31  y: how bright  z: which layers, a bit each

// A texel's height, taken off its luminance. The proxy MU2's own pipeline uses, and a fair
// one on art where the raised stones are lit and the mortar between them is not.
float heightOf(vec3 colour)
{
	return dot(colour, vec3(0.299, 0.587, 0.114));
}

SAMPLER2D(s_albedo2,   9);
SAMPLER2D(s_normal2,  10);
SAMPLER2D(s_orm2,     11);
// The third layer, on slots nothing else the land reads is bound to.
SAMPLER2D(s_albedo3,   3);
SAMPLER2D(s_normal3,   8);
SAMPLER2D(s_orm3,     12);

// Every tile slot's weight at every grid corner, four to a band. Ground::splat.
SAMPLER2D(s_groundWeights, 6);

// One slot's weight at the fragment, through a cubic B-spline over the corners around it:
// four bilinear taps placed so the hardware's lerp does the spline's weights. Bilinear, or
// the two triangles a quad is cut into, draws MU's one-metre painting as a staircase with a
// crease down every quad's diagonal, which along a shore is the zigzag; the spline draws the
// same corners as a curve. It smooths rather than interpolates, so a lone painted corner
// arrives at four ninths of itself -- the price of a shore that is not a saw.
float splatWeight(float slot, vec2 at0, vec2 at1, vec4 g)
{
	float band = floor(slot * 0.25 + 0.01);
	float channel = slot - band * 4.0;
	vec4 mask = vec4_splat(1.0) - step(vec4_splat(0.5), abs(vec4(0.0, 1.0, 2.0, 3.0) - channel));
	float lift = band * u_groundWeights.z + u_groundWeights.w;
	vec2 texel = vec2(1.0, 1.0) / u_groundWeights.xy;
	vec4 sum = texture2DLod(s_groundWeights, vec2(at0.x, at0.y + lift) * texel, 0.0) * g.x * g.z
	         + texture2DLod(s_groundWeights, vec2(at1.x, at0.y + lift) * texel, 0.0) * g.y * g.z
	         + texture2DLod(s_groundWeights, vec2(at0.x, at1.y + lift) * texel, 0.0) * g.x * g.w
	         + texture2DLod(s_groundWeights, vec2(at1.x, at1.y + lift) * texel, 0.0) * g.y * g.w;
	return dot(sum, mask);
}

vec3 unpackNormal(vec2 xy)
{
	vec2 n = xy * 2.0 - 1.0;
	return vec3(n, sqrt(max(0.0, 1.0 - dot(n, n))));
}

// A smooth value noise over the land, 0 to 1, for what must not repeat with the sheet.
float landHash(vec2 p)
{
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
float landNoise(vec2 p)
{
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(landHash(i), landHash(i + vec2(1.0, 0.0)), f.x),
	           mix(landHash(i + vec2(0.0, 1.0)), landHash(i + vec2(1.0, 1.0)), f.x), f.y);
}

// A still water sheet kept from repeating (water_variety, the Lost Tower's lava; ours): a
// second copy of it turned 37 degrees and at 0.57 of the scale, blended in where a slow noise
// says, and the whole drifting brighter and darker on another. MU stamps its one sheet tile
// after tile, which the flooded void showed as a grid.
vec2 turned(vec2 uv)
{
	return mul(mat2(0.8, -0.6, 0.6, 0.8), uv * 0.57) + vec2(0.37, 0.71);
}
vec3 varied(vec3 texel, vec3 other, vec2 tile, float variety)
{
	float pick = smoothstep(0.3, 0.7, landNoise(tile * 0.13));
	float gain = 1.0 + (landNoise(tile * 0.07 + vec2(17.0, 5.0)) - 0.5) * 0.7;
	return mix(texel, other, pick * variety) * mix(1.0, gain, variety);
}

// A layer's texel, or on running water the two dragged copies cross-faded: `run` is a
// uniform's, so the branch keeps the mips' derivatives whole, and a still layer reads once.
#define LAYER(s, a, b, run) ((run) ? mix(texture2D(s, a), texture2D(s, b), flowFade) : texture2D(s, a))

// A value noise over the land for wet ground's puddles: smooth, about one feature a unit.
float wetHash(vec2 p)
{
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
float wetNoise(vec2 p)
{
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(wetHash(i), wetHash(i + vec2(1.0, 0.0)), u.x),
	           mix(wetHash(i + vec2(0.0, 1.0)), wetHash(i + vec2(1.0, 1.0)), u.x), u.y);
}

// How level the face under this pixel stands, 0 a wall to 1 flat, off the position's
// derivatives. Guarded: a sliver triangle edge-on far up the screen extrapolates v_wpos to its
// helper pixels, its derivatives come back infinite, and a plain normalize(cross()) was NaN --
// lone white pixels on the land, bloomed into yellow dots that flashed for a frame as he walked
// (Lorencia's bridge at 166-168,128, 2026-10-06). Clamped, a NaN falls to the bound on Metal;
// a face with no area reads as level.
float facetLevel(vec3 wpos)
{
	vec3 facet = cross(clamp(dFdx(wpos), vec3_splat(-64.0), vec3_splat(64.0)),
	                   clamp(dFdy(wpos), vec3_splat(-64.0), vec3_splat(64.0)));
	float area = dot(facet, facet);
	return area > 1e-12 ? saturate(abs(facet.y) * inversesqrt(area)) : 1.0;
}

void main()
{
	vec3 isWater = mod(floor(vec3_splat(u_groundRelief.w) / vec3(1.0, 2.0, 4.0)), 2.0);
	float layers = u_groundBlend.w;

	// Where the world names its rivers, a water layer runs the way its channel runs
	// (content::buildFlow): two copies of the sheet, each dragged along the flow for a cycle
	// and taken back while the other shows, the corner's noise staggering the cycle so the
	// river does not breathe as one. Otherwise it slides along U, as MU's does. submitGround.
	vec2 flowA = v_texcoord0;
	vec2 flowB = v_texcoord0;
	float flowFade = 0.0;
	bool flowing = u_groundBlend.y >= 0.0 && u_groundSlots.w > 0.5 && u_groundRelief.w > 0.5;
	vec2 slide = vec2(u_groundBlend.y >= 0.0 ? 0.0 : u_groundBlend.z, 0.0);
	if (flowing)
	{
		vec2 at = v_texcoord0 + vec2(0.5, 0.5 + u_groundBlend.y);
		vec4 fl = texture2DLod(s_groundWeights, at / u_groundWeights.xy, 0.0);
		vec2 drift = (fl.xy * 255.0 - 128.0) / 127.0 * u_groundBlend.z;
		float phaseA = fract(u_groundBlend.x + fl.z);
		float phaseB = fract(phaseA + 0.5);
		flowA = v_texcoord0 - drift * phaseA;
		flowB = v_texcoord0 - drift * phaseB;
		// A's weight is nought as it jumps back, at 0 and 1; B's as it does, at a half.
		flowFade = abs(2.0 * phaseA - 1.0);
	}
	bool run0 = flowing && isWater.x > 0.5;
	bool run1 = flowing && isWater.y > 0.5;
	bool run2 = flowing && isWater.z > 0.5;
	vec2 uv0 = (run0 ? flowA : v_texcoord0) * u_groundRepeat.x + slide * isWater.x;
	vec2 uv1 = (run1 ? flowA : v_texcoord0) * u_groundRepeat.y + slide * isWater.y;
	vec2 uv2 = (run2 ? flowA : v_texcoord0) * u_groundRepeat.z + slide * isWater.z;
	vec2 uv0b = flowB * u_groundRepeat.x;
	vec2 uv1b = flowB * u_groundRepeat.y;
	vec2 uv2b = flowB * u_groundRepeat.z;

	// Only the layers this part has. The branch is on a uniform, so every fragment of a draw
	// takes the same side and the mips' derivatives stay whole. Most of the land is one
	// material, and that part reads one set.
	vec3 albedo0 = LAYER(s_albedo, uv0, uv0b, run0).rgb;
	vec3 albedo1 = albedo0;
	vec3 albedo2 = albedo0;
	if (layers > 1.5) albedo1 = LAYER(s_albedo2, uv1, uv1b, run1).rgb;
	if (layers > 2.5) albedo2 = LAYER(s_albedo3, uv2, uv2b, run2).rgb;
	if (u_waterGlow.w > 0.0)
	{
		if (isWater.x > 0.5 && !run0)
			albedo0 = varied(albedo0, texture2D(s_albedo, turned(uv0)).rgb, v_texcoord0, u_waterGlow.w);
		if (layers > 1.5 && isWater.y > 0.5 && !run1)
			albedo1 = varied(albedo1, texture2D(s_albedo2, turned(uv1)).rgb, v_texcoord0, u_waterGlow.w);
		if (layers > 2.5 && isWater.z > 0.5 && !run2)
			albedo2 = varied(albedo2, texture2D(s_albedo3, turned(uv2)).rgb, v_texcoord0, u_waterGlow.w);
	}

	// The weight MU painted, as a water level, with the layers' own relief deciding which
	// side of it a texel falls. This is MU2's blend, traced from its GroundSource rather than
	// invented: a straight lerp is a flat facet with a crease at every tile edge, and at a
	// bite of one the join becomes speckle. A third is where stones come through as stones.
	//
	// Three layers where MU2 had two, and the same blend: each weight moves by
	// 4 * bite * w * (h - mean h). With two layers that is w1 + 4 * bite * w1 * w0 * (h1 - h0),
	// which is MU2's taper exactly -- nought at both ends of a fade, where only one picture
	// is there, and strongest in the middle. The moves sum to nothing, so the coverage MU
	// painted is the coverage that comes out.
	//
	// The weights are Ground::splat's, shared by every tile on a corner. That is what took
	// the sawtooth off the shores: two tiles no longer disagree about a corner, so there is
	// no edge for one draw to leave against the next.
	vec3 w = v_weight.xyz;
	if (u_groundSlots.w > 0.5 && layers > 1.5)
	{
		// The spline's four weights on each axis, folded into two taps an axis. Corner i
		// is texel i + 0.5, and the uv is in tiles, so the uv is already in corners.
		vec2 base = floor(v_texcoord0);
		vec2 f = v_texcoord0 - base;
		vec2 f2 = f * f;
		vec2 f3 = f2 * f;
		vec2 w0 = (vec2_splat(1.0) - 3.0 * f + 3.0 * f2 - f3) / 6.0;
		vec2 w1 = (vec2_splat(4.0) - 6.0 * f2 + 3.0 * f3) / 6.0;
		vec2 w2 = (vec2_splat(1.0) + 3.0 * f + 3.0 * f2 - 3.0 * f3) / 6.0;
		vec2 w3 = f3 / 6.0;
		vec2 g0 = w0 + w1;
		vec2 g1 = w2 + w3;
		vec2 at0 = base + vec2_splat(0.5) - vec2_splat(1.0) + w1 / g0;
		vec2 at1 = base + vec2_splat(0.5) + vec2_splat(1.0) + w3 / g1;
		vec4 g = vec4(g0.x, g1.x, g0.y, g1.y);
		w.x = splatWeight(u_groundSlots.x, at0, at1, g);
		w.y = splatWeight(u_groundSlots.y, at0, at1, g);
		w.z = layers > 2.5 ? splatWeight(u_groundSlots.z, at0, at1, g) : 0.0;
	}
	w = max(w, vec3_splat(0.0));
	w /= max(w.x + w.y + w.z, 1e-5);
	// MU's caustics (Atlans): the layer on TileWater01's slot is not a sheet in the blend but
	// light added over it, by the weight MU's overlay alpha gives it (ZzzLodTerrain.cpp:1697-1707,
	// 1971-1975). Its weight is kept for that and taken out of the blend, so the sand under it
	// is the sand as MU leaves it.
	vec3 isCaustic = mod(floor(vec3_splat(u_caustic.z) / vec3(1.0, 2.0, 4.0)), 2.0);
	float causticWeight = dot(w, isCaustic);
	if (u_caustic.z > 0.5)
	{
		w *= vec3_splat(1.0) - isCaustic;
		w /= max(w.x + w.y + w.z, 1e-5);
	}
	// Sharpened, where the spline has made a fade: cubed and renormalised, which leaves the
	// halfway line where MU put it and narrows the band either side of it. Without this the
	// spline's fade was two tiles wide on every shore, and water under seventy percent
	// already reads as the sand over it -- the rivers looked a tile narrower on each bank.
	if (u_groundSlots.w > 0.5)
	{
		w = w * w * w;
		w /= max(w.x + w.y + w.z, 1e-5);
		// And water leads at its own shore. MU draws a water tile as water right to its edge
		// and the land tile beside it as land; the shared corner on that edge averages to
		// half and half, and water's dark sheet at half reads as the land over it -- the
		// banks stood back from the river by a third of a tile. Tripled, the edge corner is
		// three quarters water, which is where it reads as water. Only down at the water's
		// level: v_weight.w is how much lead a corner takes, and up on a bridge's deck it is
		// nought, which takes the water out of the corner altogether. See Ground::splat.
		w *=vec3_splat(1.0) + isWater * (3.0 * v_weight.w - 1.0);
		w /= max(w.x + w.y + w.z, 1e-5);
	}
	// No water sheet up a cliff. The Lost Tower's lava ran up its walls, its glowing sheet
	// stretched along the face from the tile at their foot (the user, 2026-10-05: 'at LT on wall
	// edges lava is pretty active'). The water layers fade out as the ground stands up, into
	// whatever else the corner holds; a face that holds nothing else keeps them.
	// How much water a steep face still holds where nothing else was there to take its place:
	// dimmed below to crust and kept from glowing, so a lava cliff's foot is not a sheet hung on
	// the wall.
	float cliffWater = 0.0;
	{
		// The face's own slope, off the position's derivatives: the mesh's normals are smoothed
		// over the tile beside, and on a one-tile cliff they still read nearly level.
		float level = smoothstep(0.45, 0.8, facetLevel(v_wpos));
		vec3 kept = w * (vec3_splat(1.0) - isWater * (1.0 - level));
		float left = kept.x + kept.y + kept.z;
		if (left > 0.05) w = kept / left;
		cliffWater = saturate(dot(w, isWater) * (1.0 - level));
	}
	// Each layer's relief measured from its own mean, which is its last mip. Measured from
	// nought, a dark sheet is low everywhere and loses every fade it is in: water is the
	// darkest sheet on the map and was eaten along its whole shore. MU2 got away with it
	// because its water tiles were pure water and only a thin strip ever blended.
	vec3 h = vec3(heightOf(albedo0) - heightOf(texture2DLod(s_albedo, uv0, 16.0).rgb),
	              heightOf(albedo1) - heightOf(texture2DLod(s_albedo2, uv1, 16.0).rgb),
	              heightOf(albedo2) - heightOf(texture2DLod(s_albedo3, uv2, 16.0).rgb));
	float mean = dot(w, h);
	w = max(w + 4.0 * u_groundRepeat.w * w * (h - mean), vec3_splat(0.0));
	w /= max(w.x + w.y + w.z, 1e-5);

	vec3 albedo = albedo0 * w.x + albedo1 * w.y + albedo2 * w.z;
	albedo *= 1.0 - 0.9 * cliffWater;
	vec3 orm0 = LAYER(s_orm, uv0, uv0b, run0).rgb;
	vec3 orm = orm0 * w.x;
	vec3 nm0 = unpackNormal(LAYER(s_normal, uv0, uv0b, run0).xy);
	vec3 nm = nm0 * w.x;
	if (layers > 1.5)
	{
		orm += LAYER(s_orm2, uv1, uv1b, run1).rgb * w.y;
		nm += unpackNormal(LAYER(s_normal2, uv1, uv1b, run1).xy) * w.y;
	}
	else
	{
		orm += orm0 * w.y;
		nm += nm0 * w.y;
	}
	if (layers > 2.5)
	{
		orm += LAYER(s_orm3, uv2, uv2b, run2).rgb * w.z;
		nm += unpackNormal(LAYER(s_normal3, uv2, uv2b, run2).xy) * w.z;
	}
	else
	{
		orm += orm0 * w.z;
		nm += nm0 * w.z;
	}

	// The tangent frame is analytic: the land's uv runs along world x and z, so the tangent
	// is x and the bitangent is z, with no TANGENT attribute to carry for a quarter of a
	// million vertices.
	vec3 ng = normalize(v_normal);
	vec3 t = normalize(vec3(1.0, 0.0, 0.0) - ng * ng.x);
	vec3 b = cross(ng, t);
	// Z rebuilt, never read (unpackNormal): cooked normals are BC5 and carry two channels, so
	// .z arrives as zero and every normal would lie flat in the tangent plane.
	// Mixed linearly by the same weights the albedo used, and the relief mixed with them and
	// applied afterwards -- which is MU2's order (NORMAL_MAP takes the mixed normal and
	// NORMAL_MAP_DEPTH the mixed relief), not each layer's relief applied before the mix.
	nm = normalize(nm);
	nm.xy *= dot(w, u_groundRelief.xyz);
	nm = normalize(nm);
	vec3 n = normalize(t * nm.x + b * nm.y + ng * nm.z);

	float ao = orm.r;
	float roughness = clamp(orm.g, 0.04, 1.0);
	float metal = orm.b;

	vec2 pixel = gl_FragCoord.xy;
	ao *= texture2D(s_ao, pixel * u_viewTexel.xy).r;

	// MU's baked TerrainLight rides in the vertex colour and multiplies the ALBEDO, which is
	// what glTF says a vertex colour does and what MU2's own ground shader does:
	// `ALBEDO = albedo * painted.rgb`, with no factor. It was applied here over the whole lit
	// result -- after the sun, the ambient AND the sky reflection -- and scaled by an invented
	// 2.0, which made it a second light rather than a modulation of the surface.
	// The lamps light the albedo as painted, before this. lights.sh.
	vec3 ownAlbedo = albedo;
	albedo *= v_colour.rgb;

	// No view vector and no f0: with no specular and no reflection on dry ground there is
	// nothing left that depends on where the eye is.
	//
	// Metal therefore only subtracts energy here, which is NOT what it does in MU2. Godot's
	// SPECULAR = 0 scales the dielectric F0 alone, so a metal texel there still returns the
	// environment tinted by its albedo; with no specular term at all, ours simply goes dark.
	// Every ORM blue channel in Lorencia's eighteen maps is 0, so nothing differs today. It
	// becomes a real difference when water arrives with a specular path of its own.
	vec3 diffuseColour = albedo * (1.0 - metal);

	// Wet ground, where the sheet asks (ground_wet; Blood Castle's). Ours. Water in the stone
	// darkens it -- the pores fill and less light scatters back out -- and standing water,
	// in patches a few metres across where a slow noise over the land lies low, darkens it
	// more and lies flat over the stone's relief. The sheen is added after the lamps.
	float wet = u_groundWet.x;
	float puddle = 0.0;
	if (wet > 0.0)
	{
		vec2 at = v_wpos.xz;
		float lay = wetNoise(at * 0.23) * 0.7 + wetNoise(at * 0.83 + vec2(17.3, 5.1)) * 0.3;
		puddle = smoothstep(0.6, 0.68, lay) * u_groundWet.y;
		float darken = mix(1.0, 0.68, wet) * mix(1.0, 0.75, puddle);
		diffuseColour *= darken;
		ownAlbedo *= darken;
	}

	vec3 l = normalize(u_sunDir.xyz);
	float ndotl = saturate(dot(n, l));

	vec3 colour = vec3_splat(0.0);
	// The probe's view, as fs_shade draws it.
	if (u_shadowDebug.x > 0.5)
	{
		gl_FragColor = vec4_splat(sunShadow(v_wpos, ng, saturate(dot(ng, l)), pixel));
		return;
	}
	// The sun's shadow, kept for the lamps too where the sheet asks (lamp_shadow, u_lampParams.w).
	float sunLit = 1.0;
	if (ndotl > 0.0)
	{
		float shadow = sunShadow(v_wpos, ng, saturate(dot(ng, l)), pixel);
		sunLit = shadow;
		// Diffuse only. See the note under the ambient.
		vec3 kd = diffuseColour / 3.14159265;
		colour += kd * u_sunColour.rgb * u_sunDir.w * ndotl * shadow;
	}

	float up = n.y * 0.5 + 0.5;
	colour += mix(u_groundColour.rgb, u_skyColour.rgb, up) * u_sunColour.w * diffuseColour * ao;

	// The lamps, diffuse only, for the same reason the sun is. The eye is needed for their
	// falloff's direction and nothing else, since the specular is switched off.
	vec3 v = normalize(u_camPos.xyz - v_wpos);
	// A sheet that glows of itself (water_glow, the Lost Tower's lava) takes a fifth of a lamp:
	// lit like stone, a fire left over the lava burned a white hole in it (2026-10-05).
	float lampTaken = 1.0 - 0.8 * dot(w, isWater) * step(0.001, dot(u_waterGlow.rgb, vec3_splat(1.0)));
	colour += lampLight(v_wpos, n, v, ownAlbedo * (1.0 - metal), vec3_splat(0.0), roughness,
	                    saturate(dot(n, v)) + 1e-5, 0.0) * mix(1.0, sunLit, u_lampParams.w) * lampTaken;

	// And the wet ground's sheen: the lamps' highlight through the water's own low GGX, broad
	// on wet stone and nearly a mirror on a puddle, whose normal is the ground's and not the
	// stone's; and the sun's or moon's, the same way. No sky reflection, as on the water.
	if (wet > 0.0)
	{
		// Seen along the camera's own axis, not from the eye's point: the camera follows him
		// at one fixed angle, and taken from the eye a highlight slid over the stone with
		// every step he took (the user: 'light drops is changing angles when char is
		// moviing'). Along the axis it stays where it lies, as a painted wet floor would.
		vec3 axis = normalize(mul(u_invView, vec4(0.0, 0.0, 1.0, 0.0)).xyz);
		vec3 vw = dot(axis, u_camPos.xyz - v_wpos) > 0.0 ? axis : -axis;
		vec3 nw = normalize(mix(n, ng, puddle * 0.9));
		float wr = mix(0.3, 0.07, puddle);
		float wndotv = saturate(dot(nw, vw)) + 1e-5;
		vec3 glint = lampLight(v_wpos, nw, vw, vec3_splat(0.0), vec3_splat(0.02), wr, wndotv, 1.0)
		           * mix(1.0, sunLit, u_lampParams.w);
		float wndotl = saturate(dot(nw, l));
		if (wndotl > 0.0)
		{
			vec3 hw = normalize(l + vw);
			float spec = distributionGGX(saturate(dot(nw, hw)), wr) *
			             geometrySmith(wndotv, wndotl, wr) /
			             max(4.0 * wndotv * wndotl, 1e-4);
			glint += fresnelSchlick(vec3_splat(0.02), saturate(dot(vw, hw))) * spec *
			         u_sunColour.rgb * u_sunDir.w * wndotl * sunLit;
		}
		colour += glint * wet * (1.0 - dot(w, isWater));
	}

	// Water's sheen, where the sheet asks for one (water_sheen, u_groundColour.w). Ours: MU's
	// water is its painted sheet sliding, and the Dungeon's is near black, so away from a
	// torch the stream could not be seen at all. The sheet itself is kept -- MU's own water,
	// flowing along its channel -- and two things are added. A faint cold light of its own,
	// on the sheet as painted, so its pattern shows moving in the dark; and the lamps'
	// highlight on its own normals through the same GGX the town takes, broad enough to be a
	// sheen by the fire and not a spark. Tried and taken out, the user's judgement on
	// 2026-09-30: a sky reflection, which lay pale on the far water and read as snow;
	// steepened normals, which glittered; and waves of our own, which were not MU's water.
	// Only the water layers, by the weights the albedo used.
	float wetMask = dot(w, isWater);
	float sheen = u_groundColour.w;
	if (wetMask > 0.001 && sheen > 0.0)
	{
		float wr = 0.22;
		vec3 wf0 = vec3_splat(0.02);
		float wndotv = saturate(dot(n, v)) + 1e-5;
		vec3 glint = lampLight(v_wpos, n, v, vec3_splat(0.0), wf0, wr, wndotv, 1.0)
		           * mix(1.0, sunLit, u_lampParams.w);
		// The water sheet's own share and nothing else: lit off the blend, the brighter rock
		// and sand the water fades into took it too, and the shore came up as a pale band.
		vec3 waterAlbedo = albedo0 * (isWater.x * w.x) + albedo1 * (isWater.y * w.y)
		                 + albedo2 * (isWater.z * w.z);
		vec3 own = waterAlbedo * vec3(0.55, 0.68, 0.9) * 0.2;
		colour += (glint * wetMask + own) * sheen;
	}

	// The water sheet's own light in its own colours, where the sheet asks for one
	// (water_glow): the Lost Tower's lava. Ours; zero on every other world.
	colour += (albedo0 * (isWater.x * w.x) + albedo1 * (isWater.y * w.y)
	         + albedo2 * (isWater.z * w.z)) * u_waterGlow.rgb * (1.0 - cliffWater);
	// **The cliff's foot in the lava's heat** (the user, 2026-10-05, of a wall over the lava:
	// 'can we somehow better blend this parts where wall joins lava?'): where a face meets the
	// sheet it glows with the sheet's own mean colour, which is what the lava beside it averages
	// to, and fades up the wall with the water the face holds. The seam has one brightness on
	// both sides; above it the rock goes dark smoothly instead of at a line.
	if (dot(u_waterGlow.rgb, vec3_splat(1.0)) > 0.001)
	{
		// By height over the sheet and the face's steepness, both smooth, where the layer
		// weights step from tile to tile: a hand over the lava hot, gone by a metre. The
		// Lost Tower's lava lies at the map's floor, nought. The sheet sampled across the wall
		// rather than down it, so it is not drawn out into streaks.
		float steep = 1.0 - smoothstep(0.45, 0.8, facetLevel(v_wpos));
		float heat = steep * (1.0 - smoothstep(0.0, 1.0, v_wpos.y));
		if (heat > 0.001)
		{
			vec2 across = vec2(v_wpos.x + v_wpos.z, v_wpos.y) * 0.35;
			vec3 lava = isWater.x > 0.5 ? texture2D(s_albedo, across).rgb
			          : isWater.y > 0.5 ? texture2D(s_albedo2, across).rgb
			                            : texture2D(s_albedo3, across).rgb;
			colour += lava * u_waterGlow.rgb * heat * heat;
		}
	}

	// MU's caustics, added: frame u_caustic.x of the 8 by 4 sheet, one 64-texel frame across
	// four tiles (FaceTexture's Scale, ZzzLodTerrain.cpp:1715-1719), times the TerrainLight as
	// MU's RenderFaceBlend draws it. A frame is cut half a texel in from its edges, and the mip
	// is read off the unwrapped coordinate, so a frame's seam and its neighbours do not show.
	// The sun's shadow takes them out too: ours, MU's are drawn under everything.
	if (u_caustic.z > 0.5 && causticWeight > 0.001)
	{
		vec2 span = v_texcoord0 * 0.25;
		vec2 cell = fract(span) * (62.0 / 64.0) + vec2_splat(1.0 / 64.0);
		vec2 frame = vec2(mod(u_caustic.x, 8.0), floor(u_caustic.x / 8.0));
		vec2 cuv = (frame + cell) / vec2(8.0, 4.0);
		vec2 dx = dFdx(span) / vec2(8.0, 4.0);
		vec2 dy = dFdy(span) / vec2(8.0, 4.0);
		vec3 light = isCaustic.y > 0.5 ? texture2DGrad(s_albedo2, cuv, dx, dy).rgb
		                               : texture2DGrad(s_albedo3, cuv, dx, dy).rgb;
		colour += light * v_colour.rgb * causticWeight * u_caustic.y * sunLit;
	}

	// No sky reflection and no sun specular on dry ground. MU's ground art has its own
	// lighting painted into it, so a sheen on top is a second highlight on a surface that
	// already carries one -- and a ground plane seen from MU's 48 degrees is grazing nearly
	// everywhere, so Fresnel spreads that highlight across most of the frame. MU2 measured
	// this and pins its ground SPECULAR to zero; water is the exception and is sprint 8's.

	// Last, so the haze goes into the chasm's black with the land. Ground::splat's slope.
	// And the floor's own last metres into the void, after the haze too, so they reach the
	// void's black rather than the dust's grey (the Lost Tower's void.blend; 1 elsewhere).
	gl_FragColor = vec4(abyss(dusty(colour, v_wpos), v_wpos) * abyssEdge(v_wpos), 1.0);
}
