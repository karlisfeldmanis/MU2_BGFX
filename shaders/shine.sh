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
uniform vec4 u_refineStar;
SAMPLER2D(s_chrome, 9);
SAMPLER2D(s_shiny, 10);

// The plus, whole. It is the same at every corner of an instance, so this only rounds away
// what interpolation might add.
float shinePlus(float refine)
{
	return floor(refine + 0.5);
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
