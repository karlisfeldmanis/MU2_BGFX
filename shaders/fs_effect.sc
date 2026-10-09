$input v_texcoord0, v_colour, v_material

// The effect material, whole: a sheet and a tint, and nothing else -- and, since 2026-10-09,
// every kind of sprite in one program.
//
// This is deliberately NOT the world's material model. docs/conventions.md keeps that one
// closed -- albedo, normal, ORM, emissive, and the three flags cutout, two-sided and skinned
// -- and an effect has none of it. It is not lit, it casts nothing, it has no normal and no
// roughness, and adding `additive` as a fourth flag over there would make every wall in
// Lorencia pay a variant for a thing only smoke does.
//
// **One program, so the sorted list is a handful of draws.** The pass draws back to front, and
// a draw used to end wherever the next sprite wanted another sheet, blend or program: a lit
// town's ~1,200 sprites were ~380 draws, four in five of the breaks a change of blend or
// program between a lamp's flame and its glow. Now the vertex says which kind it is and which
// of seven bound sheets it reads (v_material.x the slot, .y the kind, see gfx::Effects), and
// every kind but Minus shares one blend, so a run ends only at a Minus sprite or an eighth
// sheet. fs_flame, fs_smoke, fs_dust and fs_breath, which comments elsewhere still name, are
// the functions of those names below.
//
// The colour comes out PREMULTIPLIED, and every blend is built on that.
//
// The obvious alternative -- straight alpha, with BGFX_STATE_BLEND_ALPHA for one mode and
// BGFX_STATE_BLEND_ADD for the other -- has a hole that is easy to miss: ADD is (ONE, ONE),
// so it never looks at alpha at all, and an additive sprite therefore CANNOT FADE. Every
// additive effect MU has would vanish at the end of its life instead of dying away, and the
// tint's alpha -- the thing that makes an effect something with a lifetime rather than a
// decal -- would silently do nothing on half of them.
//
// Premultiplied settles both with one blend, (ONE, INV_SRC_ALPHA). A mixed sprite writes its
// alpha and an added one writes 0:
//
//     mixed:  dst = rgb*a + dst*(1-a)    the ordinary over
//     added:  dst = rgb*a + dst*(1-0)    exactly what (ONE, ONE) gave, and it fades, because
//                                         a is inside the rgb
//
// Choosing a sheet and a kind by a varying is safe here: both are the same at all four corners
// of a quad, and a 2x2 block of pixels never spans two triangles, so the branch is uniform
// wherever a texture read takes its derivatives.
#include "common.sh"

// s_albedo (stage 0, from common.sh) is slot 0; the other six take stages common.sh leaves
// free, gfx::Effects's kSheetStages.
SAMPLER2D(s_sheet1, 9);
SAMPLER2D(s_sheet2, 10);
SAMPLER2D(s_sheet3, 11);
SAMPLER2D(s_sheet4, 13);
SAMPLER2D(s_sheet5, 14);
SAMPLER2D(s_sheet6, 15);

uniform vec4 u_flame;  // x: how bright full heat is  yzw: unused

vec4 sheetAt(float slot, vec2 uv)
{
	if (slot < 0.5) return texture2D(s_albedo, uv);
	if (slot < 1.5) return texture2D(s_sheet1, uv);
	if (slot < 2.5) return texture2D(s_sheet2, uv);
	if (slot < 3.5) return texture2D(s_sheet3, uv);
	if (slot < 4.5) return texture2D(s_sheet4, uv);
	if (slot < 5.5) return texture2D(s_sheet5, uv);
	return texture2D(s_sheet6, uv);
}

// A sheet and a tint. Most of MU's additive art has no alpha of its own -- the black IS the
// transparency and the blend mode is what makes it so -- and there sheet.a is 1 and the fade
// is the tint's alone.
vec4 plain(float slot, vec2 uv, vec4 tint, bool added)
{
	vec4 sheet = sheetAt(slot, uv);
	float a = sheet.a * tint.a;
	return vec4(sheet.rgb * tint.rgb * a, added ? 0.0 : a);
}

// Fire, sprint 8b (was fs_flame): MU's flame sheet read as a SHAPE and coloured by heat.
//
// Effect/Fire01 is dim red paint; drawn as its own colour it adds a wisp that never passes 1.0,
// so nothing blooms and nothing reads as burning. Here the sheet's red channel says how much
// flame is at a texel, and the particle's heat says how hot it is: together they run up a
// ramp from ember red through orange to a yellow-white well above 1.0, which is what the
// bloom catches. Invention, marked: MU colours its fire with the sheet.
//
// The vertex colour carries the particle, not a tint:
//   r  the cell's gain / 20: the four cells are 1x, 9x, 19x and 19x darker than the first
//   g  the heat, 0..1, cooling over the particle's life
//   a  the fade in and out
vec3 heatRamp(float h)
{
	float r = smoothstep(0.02, 0.35, h);
	float g = smoothstep(0.22, 0.85, h) * 0.82;
	float b = smoothstep(0.6, 1.25, h) * 0.55;
	// Brightness climbs faster than colour: a cool tip is dim, a hot core far over 1.
	return vec3(r, g, b) * (0.15 + h * h * u_flame.x);
}

vec4 flame(float slot, vec2 uv, vec4 particle)
{
	float shape = sheetAt(slot, uv).r * particle.r * 20.0;
	float h = saturate(shape) * particle.g * 1.25;
	vec3 colour = heatRamp(h) * saturate(shape * 1.5);
	return vec4(colour * particle.a, 0.0);
}

// Smoke, sprint 8b (was fs_smoke). MU's smoke02 is a soft blob painted on a square whose alpha
// never reaches zero -- 16 at the darkest corner, 47 round the border -- so mixed in as it
// stands every puff is a translucent pane with straight edges. MU2 found the same and rounded
// it; this does it in the shader: the sheet's alpha times a disc that is zero before any edge.
// Sprites drawn with this take the whole sheet, so the uv is the quad's own.
vec4 smoke(float slot, vec2 uv, vec4 tint)
{
	vec4 sheet = sheetAt(slot, uv);
	float r = length(uv * 2.0 - 1.0);
	float disc = 1.0 - smoothstep(0.35, 0.95, r);
	float a = sheet.a * disc * tint.a;
	// The sheet's brightness and not its colour: smoke02 is painted brown, and a brown plume
	// over a bonfire reads as dust. The colour is the tint, which the game lights.
	float grey = dot(sheet.rgb, vec3(0.2126, 0.7152, 0.0722)) * 2.5;
	return vec4(tint.rgb * grey * a, a);
}

// Dust, the Budge Dragon's (was fs_dust): MU's smoke02, drawn soft.
//
// **Ours, marked.** RenderParticles cuts this sheet at GL_ALPHA_TEST 0.25 (g_AlphaFuncRef), so
// the client's puffs are hard-edged blobs that shrink to their middles as they fade -- MU2's
// dust.gdshader kept that cut, and at 1080p a metre wide it reads as flat brown discs lying on
// the grass. Asked for blurrier and more real, this lets the cut go: the sheet's own alpha, a
// wide radial falloff that is already fading at the centre, and nothing thrown away, so each
// puff is a soft cloud and overlapping ones build up rather than stack as cut-outs. What stays
// MU's is the game side -- when a puff is born, how it grows, drifts and darkens.
vec4 dust(float slot, vec2 uv, vec4 tint)
{
	// Blurred as it is read, for the same reason the breath is: smoke02 is a painted blob with
	// visible grain, and a metre of it on screen shows every bit of that. Five taps averaged.
	vec2 step = vec2(0.035, 0.035);
	vec4 sheet = sheetAt(slot, uv) * 0.36
	           + sheetAt(slot, uv + vec2(step.x, 0.0)) * 0.16
	           + sheetAt(slot, uv - vec2(step.x, 0.0)) * 0.16
	           + sheetAt(slot, uv + vec2(0.0, step.y)) * 0.16
	           + sheetAt(slot, uv - vec2(0.0, step.y)) * 0.16;
	float r = length(uv * 2.0 - 1.0);
	// A soft bell rather than a disc: full only at the very middle, half by a third of the way
	// out, gone before the quad's edge, so no straight side of the square can ever show.
	float bell = 1.0 - smoothstep(0.15, 1.0, r);
	// Lifted: the cut MU drew this with made a solid disc out of half the sheet, and a soft
	// cloud of the same paint is most of it thrown away. Saturated, so the middle is whole and
	// only the skirts are faint.
	float a = min(1.0, sheet.a * bell * tint.a * 1.8);
	return vec4(sheet.rgb * tint.rgb * a, a);
}

// The Budge Dragon's breath (was fs_breath): flame's heat ramp, softened.
//
// Not a flag on flame, because the town's fires want the hard, bright core it draws -- a candle
// and a brazier are small and near, and blurring them would take the flicker out of the whole
// square. Breath is a metre of moving fire seen against grass, and asked for blurrier: the
// sheet is read through a wide radial falloff, the heat is smoothed so the core does not clip
// to a white lump, and each spark is fainter so a dozen overlapping build a cloud instead of a
// wall. The vertex colour is flame's -- r the cell's gain, g the heat, a the fade.
vec4 breath(float slot, vec2 uv, vec4 particle)
{
	// Blurred as it is read. Fire01 is a painted flame with hard tongues, and at a spark's size
	// those stay crisp however the edges are faded -- so the sheet is sampled five times across
	// a cell and averaged, which is the blur asked for. The cell is a quarter of the strip
	// wide, so the taps stay well inside it and never bleed into the next frame of the strip.
	vec2 step = vec2(0.010, 0.040);
	float painted = sheetAt(slot, uv).r * 0.36
	              + sheetAt(slot, uv + vec2(step.x, 0.0)).r * 0.16
	              + sheetAt(slot, uv - vec2(step.x, 0.0)).r * 0.16
	              + sheetAt(slot, uv + vec2(0.0, step.y)).r * 0.16
	              + sheetAt(slot, uv - vec2(0.0, step.y)).r * 0.16;
	float shape = painted * particle.r * 20.0;
	// Soft round edges: the sheet's own cell is a flame painted to its borders, and a quad edge
	// is not a shape fire has.
	float r = length(uv * 2.0 - 1.0);
	// Wide and gentle: full out to a third, gone at the corner. Squared, it took the fire with
	// it -- a spark is only a few dozen pixels and most of its shape lives off centre.
	float bell = 1.0 - smoothstep(0.35, 1.05, r);
	shape *= bell;
	float h = saturate(shape) * particle.g * 1.25;
	// Softened once more at the top, so the hottest texels spread into their neighbours rather
	// than forming the crisp white core a near flame wants.
	vec3 colour = heatRamp(h) * smoothstep(0.0, 0.5, shape);
	return vec4(colour * particle.a, 0.0);
}

void main()
{
	// gfx::Effects's Kind: 0 mixed, 1 added, 2 flame, 3 smoke, 4 dust, 5 breath. Whole numbers
	// at every corner, so the interpolated value is within rounding of one.
	float slot = v_material.x;
	float kind = v_material.y;
	if (kind < 0.5)      gl_FragColor = plain(slot, v_texcoord0, v_colour, false);
	else if (kind < 1.5) gl_FragColor = plain(slot, v_texcoord0, v_colour, true);
	else if (kind < 2.5) gl_FragColor = flame(slot, v_texcoord0, v_colour);
	else if (kind < 3.5) gl_FragColor = smoke(slot, v_texcoord0, v_colour);
	else if (kind < 4.5) gl_FragColor = dust(slot, v_texcoord0, v_colour);
	else                 gl_FragColor = breath(slot, v_texcoord0, v_colour);
}
