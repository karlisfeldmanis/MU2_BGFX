$input v_texcoord0, v_colour

// Fire, sprint 8b: MU's flame sheet read as a SHAPE and coloured by heat.
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
#include "common.sh"

uniform vec4 u_flame;  // x: how bright full heat is  yzw: unused

vec3 heatRamp(float h)
{
	float r = smoothstep(0.02, 0.35, h);
	float g = smoothstep(0.22, 0.85, h) * 0.82;
	float b = smoothstep(0.6, 1.25, h) * 0.55;
	// Brightness climbs faster than colour: a cool tip is dim, a hot core far over 1.
	return vec3(r, g, b) * (0.15 + h * h * u_flame.x);
}

void main()
{
	float shape = texture2D(s_albedo, v_texcoord0).r * v_colour.r * 20.0;
	float h = saturate(shape) * v_colour.g * 1.25;
	vec3 colour = heatRamp(h) * saturate(shape * 1.5);
	float a = v_colour.a;
	// Premultiplied and added, as fs_effect's additive sprites are.
	gl_FragColor = vec4(colour * a, 0.0);
}
