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
		added += texture2DLod(s_shiny, metalUv, 0.0).rgb;
	}
	return added * colour * u_refine.z;
}

#endif
