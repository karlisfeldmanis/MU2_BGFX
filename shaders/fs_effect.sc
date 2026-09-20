$input v_texcoord0, v_colour

// The effect material, whole: a sheet and a tint, and nothing else.
//
// This is deliberately NOT the world's material model. docs/conventions.md keeps that one
// closed -- albedo, normal, ORM, emissive, and the three flags cutout, two-sided and skinned
// -- and an effect has none of it. It is not lit, it casts nothing, it has no normal and no
// roughness, and adding `additive` as a fourth flag over there would make every wall in
// Lorencia pay a variant for a thing only smoke does.
//
// The blend mode is state and not a uniform: BGFX_STATE_BLEND_ADD or BGFX_STATE_BLEND_ALPHA
// on the draw, which is why one program serves both and why the pass can still batch.
//
// s_albedo comes from common.sh at stage 0, which is where the pass binds the sheet.
// Declaring it again here is a redefinition and shaderc says so.
#include "common.sh"

// The colour comes out PREMULTIPLIED, and both blend modes are built on that.
//
// The obvious alternative -- straight alpha, with BGFX_STATE_BLEND_ALPHA for one mode and
// BGFX_STATE_BLEND_ADD for the other -- has a hole that is easy to miss: ADD is (ONE, ONE),
// so it never looks at alpha at all, and an additive sprite therefore CANNOT FADE. Every
// additive effect MU has would vanish at the end of its life instead of dying away, and the
// tint's alpha -- the thing that makes an effect something with a lifetime rather than a
// decal -- would silently do nothing on half of them.
//
// Premultiplied settles both with one rule. This multiplies rgb by the final alpha, and the
// pass pairs it with (ONE, INV_SRC_ALPHA) for alpha and (ONE, ONE) for additive:
//
//     alpha:     dst = rgb*a + dst*(1-a)    the ordinary over, unchanged
//     additive:  dst = rgb*a + dst          which now fades, because a is inside the rgb
void main()
{
	vec4 sheet = texture2D(s_albedo, v_texcoord0);
	// The sheet's own alpha times the tint's, which is the fade. Most of MU's additive art
	// has no alpha of its own -- the black IS the transparency and the blend mode is what
	// makes it so -- and there sheet.a is 1 and the fade is the tint's alone.
	float a = sheet.a * v_colour.a;
	gl_FragColor = vec4(sheet.rgb * v_colour.rgb * a, a);
}
