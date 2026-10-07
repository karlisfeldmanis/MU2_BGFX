// MU's refinement ladder: what a +3, +5, +7 or +9 item looks like. RenderPartObjectEffect
// (MuMain ZzzObject.cpp:10284) as fs_shade and fs_stage both draw it. docs/sprints/14-the-shine.md.
//
// MuMain draws it as passes: the item once with its light multiplied by a tint, then the same
// mesh again, added (GL_ONE, GL_ONE) and unlit, once with Chrome01 and at +9 once more with
// Shiny01. The added passes are flat colour times a texture, so adding their result inside the
// one shade pass is the same picture without a second draw.

#ifndef MU_SHINE_SH
#define MU_SHINE_SH

// x: MU's g_Luminosity, sin(WorldTime * 0.004) * 0.15 + 0.6, the +3 and +5 pulse (0.45 to 0.75)
// y: MU's wave, WorldTime % 10000 * 0.0001, a ten-second sawtooth the chrome scrolls by
// z: how strongly the chrome and the star are added; 0 when their sheets are missing
// w: how much of the +3/+5/+7/+9 tint the lit colour takes, 1 being MuMain's own
uniform vec4 u_refine;
// x: the +9 star's gain on top of the strength, the sheet's refine_star; 1 is MuMain's
// y: how much brighter the chrome and the star are this frame, 0 by day and in the windows
//    (game/fx/gleam.h, Renderer::setShineGlow; ours, not MU's)
// z: the excellent pass's L, sin(WorldTime * 0.002) * 0.5 + 0.5 (ZzzObject.cpp:10497)
// w: how strongly the excellent pass is added; 0 when Chrome02 is missing
uniform vec4 u_refineStar;
SAMPLER2D(s_chrome, 9);
SAMPLER2D(s_shiny, 10);
SAMPLER2D(s_chrome2, 11);

// Three things ride the one float (game/shine.h): the level, 32 for excellent, 64 a +11 sweep
// colour; so the flags cost no instance data of their own. It is the same at every corner of an
// instance, so the rounding only takes away what interpolation might add.
float shineBelowSweep(float refine)
{
	float whole = floor(refine + 0.5);
	return whole - 64.0 * floor(whole / 64.0);
}
float shineExcellent(float refine)
{
	return shineBelowSweep(refine) >= 32.0 ? 1.0 : 0.0;
}
float shinePlus(float refine)
{
	float rem = shineBelowSweep(refine);
	return rem >= 32.0 ? rem - 32.0 : rem;
}
float shineSweep(float refine)
{
	float sweeps = floor(floor(refine + 0.5) / 64.0);
	return sweeps - 4.0 * floor(sweeps / 4.0);
}
// The Wings of Darkness's flag, 256 above the sweep's four (game/shine.h kShineDarkness).
float shineDarkness(float refine)
{
	return floor(refine + 0.5) >= 256.0 ? 1.0 : 0.0;
}

// What an excellent thing adds over everything else: RenderPartObjectBodyColor2 with
// RENDER_CHROME3 | RENDER_BRIGHT (ZzzObject.cpp:10492-10531). Chrome02, added, at
// u = N.L and v = 1 - N.L against ZzzBMD's fixed LightVector (0, -0.1, -0.8) -- in this
// tree's axes (0, -0.8, 0.1), unnormalised as MU's is -- and tinted (L, 0.3L, 1 - L) as L
// breathes, so the whole piece swings blue to violet to red-orange. It is not lit and it does
// not follow the camera: MU's light vector is the world's.
vec3 shineExcellentAdded(float excellent, vec3 n)
{
	if (excellent < 0.5) return vec3_splat(0.0);
	float d = dot(n, vec3(0.0, -0.8, 0.1));
	vec3 sheet = texture2DLod(s_chrome2, vec2(d, 1.0 - d), 0.0).rgb;
	float l = u_refineStar.z;
	return sheet * vec3(l, 0.3 * l, 1.0 - l) * u_refineStar.w;
}

// What multiplies the item's lit colour. MuMain multiplies the light it hands the base pass:
// +3 and +4 (L, 0.6L, 0.6L), +5 and +6 (0.5L, 0.7L, L), +7 and +8 0.8, +9 and +10 0.9.
vec3 shineTint(float plus)
{
	float l = u_refine.x;
	vec3 tint = vec3_splat(1.0);
	if (plus >= 9.0) tint = vec3_splat(0.9);
	else if (plus >= 7.0) tint = vec3_splat(0.8);
	else if (plus >= 5.0) tint = vec3(0.5 * l, 0.7 * l, l);
	else if (plus >= 3.0) tint = vec3(l, 0.6 * l, 0.6 * l);
	return mix(vec3_splat(1.0), tint, u_refine.w);
}

// What is added on top: RENDER_CHROME from +7, RENDER_METAL as well from +9. `n` is the
// vertex normal in world space, as MuMain's bone-rotated normal is. MU's (x, y, z) is our
// (x, z, -y) (docs/conventions.md), so MU's n.z is our n.y and MU's n.y is our -n.z:
//   CHROME  u = n.z * 0.5 + wave     v = n.y * 0.5 + wave * 2   (ZzzBMD.cpp, Chrome01, repeat)
//   METAL   u = n.z * 0.5 + 0.2      v = n.y * 0.5 + 0.5        (Shiny01, clamped)
// `colour` is PartObjectColor's, per item (game::shineOf), carried in v_refine.yzw.
vec3 shineAdded(float plus, vec3 n, vec3 colour)
{
	vec3 added = vec3_splat(0.0);
	if (plus < 7.0) return added;
	float wave = u_refine.y;
	// Level 0 explicitly: the sheets are sampled inside a branch.
	vec2 chromeUv = vec2(n.y * 0.5 + wave, -n.z * 0.5 + wave * 2.0);
	added += texture2DLod(s_chrome, chromeUv, 0.0).rgb;
	if (plus >= 9.0)
	{
		vec2 metalUv = vec2(n.y * 0.5 + 0.2, -n.z * 0.5 + 0.5);
		added += texture2DLod(s_shiny, metalUv, 0.0).rgb * u_refineStar.x;
	}
	// At night the +7/+9 effect itself is what glows (u_refineStar.y, ours): the bands and the
	// star brighten, and the item's line light (game/fx/gleam.h) carries it onto what is near.
	// A flat glow of the chrome's colour over the whole surface was tried and turned down: it
	// ate the bands and the star it was meant to show off.
	return added * colour * u_refine.z * (1.0 + u_refineStar.y);
}

// What +11 adds over +9's chrome and star: MuMain's TIER_FULL_SPECULAR_V1 (ZzzObject.cpp:10377),
// RenderPartObjectBodyColor2 with RENDER_CHROME2 | RENDER_BRIGHT before the metal and chrome.
// Chrome02, added, swept across the piece on a five-second clock (ZzzBMD.cpp:1631-1640):
//   u = (n.z + n.x) * 0.8 + Wave2 * 2     v = (n.y + n.x) + Wave2 * 3
//   Wave2 = WorldTime % 5000 * 0.00024 - 0.4
// in MU's axes; here MU's n.z is our n.y and MU's n.y our -n.z. Wave2 is our ten-second wave
// doubled. Its colour is PartObjectColor2's on the 0.9 light the tier draws in: white, orange,
// blue, or white at full (`sweep` 0-3). +10 has none of it: MuMain draws +9 and +10 alike.
vec3 shineSweepAdded(float plus, float sweep, vec3 n)
{
	if (plus < 11.0 || u_refineStar.w <= 0.0) return vec3_splat(0.0);
	float wave2 = fract(u_refine.y * 2.0) * 1.2 - 0.4;
	vec2 uv = vec2((n.y + n.x) * 0.8 + wave2 * 2.0, (n.x - n.z) + wave2 * 3.0);
	vec3 sheet = texture2DLod(s_chrome2, uv, 0.0).rgb;
	vec3 tint = sweep < 0.5 ? vec3_splat(0.9)
	          : sweep < 1.5 ? vec3(0.9, 0.45, 0.0)
	          : sweep < 2.5 ? vec3(0.0, 0.45, 0.9)
	          : vec3_splat(1.0);
	return sheet * tint * u_refine.z * (1.0 + u_refineStar.y);
}

// The Wings of Darkness's second pass: MuMain draws the wing again RENDER_BRIGHT | RENDER_CHROME
// with BITMAP_CHROME + 1 -- Chrome02, added -- at BodyLight (0.8, 0.6, 1) (ZzzObject.cpp:
// 6970-6976): RENDER_CHROME's UVs, the +7 chrome's (shineAdded), scrolling on the same wave, so
// a violet sheen slides over the shards. Ours, its strength: not the excellent pass's tuned
// fraction -- on an excellent piece the pass is a glint over its own art, here it is the wing's
// colour, and at the excellent's 0.15 it added under 0.07 and the shards stood grey (2026-10-07,
// the user: 'wings look not finished') -- and not MU's full one either, which added in linear
// light turned every shard flat cyan. This much of it. Nothing when Chrome02 is missing.
vec3 shineDarknessAdded(float dark, vec3 n)
{
	if (dark < 0.5 || u_refineStar.w <= 0.0) return vec3_splat(0.0);
	float wave = u_refine.y;
	vec2 uv = vec2(n.y * 0.5 + wave, -n.z * 0.5 + wave * 2.0);
	return texture2DLod(s_chrome2, uv, 0.0).rgb * vec3(0.8, 0.6, 1.0) * 0.4;
}

// How much of a glow card a refined item's added passes may cover: where the card itself glows,
// its brightness times its alpha, full from a third up. Its alpha alone was not enough: a card
// whose alpha is faint but not nought over its whole quad still showed as a pale diamond under
// +11's bright sweep (the user, 2026-10-06: 'bug in atlans', a Staff of Resurrection).
float shineCover(vec4 sheet)
{
	return clamp(max(max(sheet.r, sheet.g), sheet.b) * sheet.a * 3.0, 0.0, 1.0);
}

// **Invention.** What the lamps and fires light refined steel with. A fire 2 m off lights a
// plate's near side past white, and a colour added to white is still white: a +9 Plate suit's
// blue went out on the pauldron turned to the bonfire, where the +0 suit's measured 255 in every
// channel. MuMain's fire never lit metal that hot, so its glow was never swamped. So on +7 and
// up the lamps' share -- and only theirs -- is compressed smoothly and coloured by the chrome's
// hue, and the fire-lit face turns blue as a whole, its streaks kept as brightness.
//
// Three tries on the whole lit colour, kept here so they are not tried again: scaling it (it is
// several times past the clip, so any fraction still clips); pulling it to the hue under a
// chrome band only (a flat pauldron has one normal, so one Chrome01 texel covers the face, and
// it was a dark one); and pulling above a brightness threshold (the streaks crossed the
// threshold unevenly and the face came out in blue flecks on pale steel).
vec3 shineLamps(float plus, vec3 colour, vec3 lamps)
{
	if (plus < 7.0) return lamps;
	float peak = max(max(lamps.r, lamps.g), lamps.b);
	// Reinhard: 0.35 at 1, 0.52 at 5, 0.58 at 20. Under the clip after exposure 1.5, in order.
	vec3 kept = lamps * (0.6 / (0.7 + peak));
	vec3 hue = colour / max(max(max(colour.r, colour.g), colour.b), 1e-3);
	// Most of the way to the hue: half-way read grey, the tonemap pulling a bright tint to white.
	return kept * mix(vec3_splat(1.0), hue, 0.85);
}

#endif
