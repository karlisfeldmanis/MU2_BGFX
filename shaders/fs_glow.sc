$input v_wpos, v_texcoord0, v_normal, v_tangent, v_vnormal, v_vpos, v_light, v_refine

// MU's BlendMesh: the one submesh an object draws ADDED to the frame at its BlendMeshLight --
// the street light's smear, the torch's flame card, the bonfire's fire, the candle's wick,
// the lit window panes. Sprint 8a. Drawn in the transparent pass into the HDR target the
// shade pass wrote, depth-tested against it and writing none, so it is tonemapped with the
// scene it glows in. Not lit and not shadowed: it IS the light.
//
// MU draws these with glBlendFunc(GL_ONE, GL_ONE) and ignores the texture's alpha: its JPEG's
// black is what makes the card vanish. MU2's pipeline did not keep that black -- the bonfire's
// fire_02 is an orange band running hard up to the texture's top edge -- but it did write a soft
// alpha round every glow, and without it each card drew its own rectangle. So the colour is
// the sheet's times its alpha. Ours, not MU's; sprint 8b.
//
// v_light.w is the flicker, carried in the instance's fifth vec4 where every other draw has a
// 1.0 it never reads. u_material.z is the sheet's glow_strength, set per draw by the renderer.
// The input list is vs_static's to the letter, which Metal requires of a linked pair.
//
// u_material.w is MoveObject's BlendMeshTexCoordV for the three glows that scroll rather
// than just flicker -- the waterspout's fall, House04's and House05's lit windows -- the
// world clock times the sheet's own rate, already wrapped to a fraction by the renderer. 0
// on every glow that does not scroll, which samples exactly where it always did. The albedo
// wraps by default (Renderer::submitBatches), which is what lets this run off the edge.
//
// A levelled item's glow is drawn as MU draws it: its BlendMesh's colour is BodyLight times
// BlendMeshLight (ZzzBMD.cpp:1523), and BodyLight carries the +3/+5/+7/+9 tint; and from +7
// the chrome and metal passes go over every mesh but its NoneBlendMesh (ZzzBMD.cpp:1425) --
// shine.sh's, added unlit as fs_shade adds them. The Staff of Resurrection is all glow, and
// a +9 one looked a +0 one without this. u_material.y's 8 is the NoneBlendMesh.
#include "common.sh"
#include "shine.sh"

void main()
{
	// u_material.y says how it runs (Renderer::submitBatches): +1 along U, MoveObject's
	// BlendMeshTexCoordU, and +2 with the sheet's alpha read where it stands, so the Lost
	// Tower's red chrome streams behind a band that does not move.
	float mode = u_material.y;
	float alongU = mod(mode, 2.0);
	float held = mod(floor(mode * 0.5), 2.0);
	float water = mod(floor(mode * 0.25), 2.0);
	float bare = mod(floor(mode * 0.125), 2.0);
	vec2 uv = v_texcoord0 + u_material.w * vec2(alongU, 1.0 - alongU);
	vec4 sheet;
	// +4: MU's water frames (content::Material::waterFrames). u_material.w is the frame,
	// 0 to 31, of the 8 by 4 caustic atlas; the frame's cell is cut half a texel in, and the
	// mip read off the unwrapped coordinate, as fs_ground's caustics are.
	if (water > 0.5)
	{
		vec2 cell = fract(v_texcoord0) * (62.0 / 64.0) + vec2_splat(1.0 / 64.0);
		vec2 frame = vec2(mod(u_material.w, 8.0), floor(u_material.w / 8.0));
		vec2 scale = vec2(1.0 / 8.0, 1.0 / 4.0);
		sheet = texture2DGrad(s_albedo, (frame + cell) * scale,
		                      dFdx(v_texcoord0) * scale, dFdy(v_texcoord0) * scale);
	}
	else
	{
		sheet = texture2D(s_albedo, uv);
		sheet.a = mix(sheet.a, texture2D(s_albedo, v_texcoord0).a, held);
	}
	// A figure's w is 2 + its fade (common.sh's figureFade), which was 1.0 here before there
	// was a fade; so a figure's glow is its fade, and comes in with it.
	float level = v_light.w >= 2.0 ? figureFade(v_light.w) : v_light.w;
	float plus = shinePlus(v_refine.x);
	vec3 colour = sheet.rgb * (sheet.a * level * u_material.z) * shineTint(plus);
	if (bare < 0.5)
	{
		vec3 n = normalize(v_normal);
		// Not breathing with the glow, as MU's chrome pass does not; only a figure's fade. And
		// only where the card is drawn: its alpha is this pipeline's stand-in for MU's black, so
		// chrome laid over the whole quad showed the card's rectangle -- a gold slab across a
		// +9 Staff of Resurrection (the user, 2026-10-06: 'some bug with res staff').
		float fade = v_light.w >= 2.0 ? figureFade(v_light.w) : 1.0;
		colour += (shineAdded(plus, n, v_refine.yzw) +
		           shineExcellentAdded(shineExcellent(v_refine.x), n) +
		           shineSweepAdded(plus, shineSweep(v_refine.x), n)) * fade * sheet.a;
	}
	gl_FragColor = vec4(colour, 1.0);
}
