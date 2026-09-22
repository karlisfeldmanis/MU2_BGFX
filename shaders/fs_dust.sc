$input v_texcoord0, v_colour

// Dust, the Budge Dragon's: MU's smoke02, drawn soft.
//
// **Ours, marked.** RenderParticles cuts this sheet at GL_ALPHA_TEST 0.25 (g_AlphaFuncRef), so
// the client's puffs are hard-edged blobs that shrink to their middles as they fade -- MU2's
// dust.gdshader kept that cut, and at 1080p a metre wide it reads as flat brown discs lying on
// the grass. Asked for blurrier and more real, this lets the cut go: the sheet's own alpha, a
// wide radial falloff that is already fading at the centre, and nothing thrown away, so each
// puff is a soft cloud and overlapping ones build up rather than stack as cut-outs. What stays
// MU's is the game side -- when a puff is born, how it grows, drifts and darkens.
#include "common.sh"

void main()
{
	// Blurred as it is read, for the same reason the breath is: smoke02 is a painted blob with
	// visible grain, and a metre of it on screen shows every bit of that. Five taps averaged.
	vec2 step = vec2(0.035, 0.035);
	vec4 sheet = texture2D(s_albedo, v_texcoord0) * 0.36
	           + texture2D(s_albedo, v_texcoord0 + vec2(step.x, 0.0)) * 0.16
	           + texture2D(s_albedo, v_texcoord0 - vec2(step.x, 0.0)) * 0.16
	           + texture2D(s_albedo, v_texcoord0 + vec2(0.0, step.y)) * 0.16
	           + texture2D(s_albedo, v_texcoord0 - vec2(0.0, step.y)) * 0.16;
	float r = length(v_texcoord0 * 2.0 - 1.0);
	// A soft bell rather than a disc: full only at the very middle, half by a third of the way
	// out, gone before the quad's edge, so no straight side of the square can ever show.
	float bell = 1.0 - smoothstep(0.15, 1.0, r);
	// Lifted: the cut MU drew this with made a solid disc out of half the sheet, and a soft
	// cloud of the same paint is most of it thrown away. Saturated, so the middle is whole and
	// only the skirts are faint.
	float a = min(1.0, sheet.a * bell * v_colour.a * 1.8);
	// Premultiplied, as the pass blends.
	gl_FragColor = vec4(sheet.rgb * v_colour.rgb * a, a);
}
