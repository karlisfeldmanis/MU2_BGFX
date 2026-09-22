$input v_texcoord0, v_colour

// Dust, the Budge Dragon's: MU's smoke02 drawn the way RenderParticles draws BITMAP_SMOKE + 1.
// Mixed like any sprite, AND cut: the sheet has an alpha channel, so it went through
// EnableAlphaTest first, which leaves GL_ALPHA_TEST on at GL_GREATER 0.25 (g_AlphaFuncRef) for
// the mix that follows. That cut is what makes it read as dust -- as a puff fades, the cut walks
// inward and the puff shrinks to its densest middle and goes out, instead of thinning into a
// haze that eight of them blur into. MU2's dust.gdshader, whose three numbers these are:
// the cut, a short ramp above it so a 1080p puff has no crisp contour, and a rim taken off
// first so the cut never finds the quad's straight edge (smoke02 is painted to its borders).
#include "common.sh"

void main()
{
	vec4 sheet = texture2D(s_albedo, v_texcoord0);
	float r = length(v_texcoord0 * 2.0 - 1.0);
	float a = sheet.a * (1.0 - smoothstep(0.6, 1.0, r)) * v_colour.a;
	float kept = smoothstep(0.25, 0.45, a);
	if (kept <= 0.0) discard;
	a *= kept;
	// RenderSprite's Color4f(Light, Light, Light, Light): the painted brown darkened by the
	// same fade that thins it. Premultiplied, as the pass blends.
	gl_FragColor = vec4(sheet.rgb * v_colour.rgb * a, a);
}
