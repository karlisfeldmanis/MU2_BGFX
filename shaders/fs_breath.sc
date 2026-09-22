$input v_texcoord0, v_colour

// The Budge Dragon's breath: fs_flame's heat ramp, softened.
//
// A copy of fs_flame rather than a flag on it, because the town's fires want the hard, bright
// core it draws -- a candle and a brazier are small and near, and blurring them would take the
// flicker out of the whole square. Breath is a metre of moving fire seen against grass, and
// asked for blurrier: the sheet is read through a wide radial falloff, the heat is smoothed so
// the core does not clip to a white lump, and each spark is fainter so a dozen overlapping
// build a cloud instead of a wall. The vertex colour is fs_flame's -- r the cell's gain, g the
// heat, a the fade -- so the game side is unchanged.
#include "common.sh"

uniform vec4 u_flame;  // x: how bright full heat is  yzw: unused

vec3 heatRamp(float h)
{
	float r = smoothstep(0.02, 0.35, h);
	float g = smoothstep(0.22, 0.85, h) * 0.82;
	float b = smoothstep(0.6, 1.25, h) * 0.55;
	return vec3(r, g, b) * (0.15 + h * h * u_flame.x);
}

void main()
{
	// Blurred as it is read. Fire01 is a painted flame with hard tongues, and at a spark's size
	// those stay crisp however the edges are faded -- so the sheet is sampled five times across
	// a cell and averaged, which is the blur asked for. The cell is a quarter of the strip
	// wide, so the taps stay well inside it and never bleed into the next frame of the strip.
	vec2 step = vec2(0.010, 0.040);
	float painted = texture2D(s_albedo, v_texcoord0).r * 0.36
	              + texture2D(s_albedo, v_texcoord0 + vec2(step.x, 0.0)).r * 0.16
	              + texture2D(s_albedo, v_texcoord0 - vec2(step.x, 0.0)).r * 0.16
	              + texture2D(s_albedo, v_texcoord0 + vec2(0.0, step.y)).r * 0.16
	              + texture2D(s_albedo, v_texcoord0 - vec2(0.0, step.y)).r * 0.16;
	float shape = painted * v_colour.r * 20.0;
	// Soft round edges: the sheet's own cell is a flame painted to its borders, and a quad edge
	// is not a shape fire has.
	float r = length(v_texcoord0 * 2.0 - 1.0);
	// Wide and gentle: full out to a third, gone at the corner. Squared, it took the fire with
	// it -- a spark is only a few dozen pixels and most of its shape lives off centre.
	float bell = 1.0 - smoothstep(0.35, 1.05, r);
	shape *= bell;
	float h = saturate(shape) * v_colour.g * 1.25;
	// Softened once more at the top, so the hottest texels spread into their neighbours rather
	// than forming the crisp white core a near flame wants.
	vec3 colour = heatRamp(h) * smoothstep(0.0, 0.5, shape);
	float a = v_colour.a;
	gl_FragColor = vec4(colour * a, 0.0);
}
