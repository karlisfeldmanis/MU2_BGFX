$input v_texcoord0, v_colour

// Smoke, sprint 8b. MU's smoke02 is a soft blob painted on a square whose alpha never reaches
// zero -- 16 at the darkest corner, 47 round the border -- so mixed in as it stands every puff
// is a translucent pane with straight edges. MU2 found the same and rounded it; this does it in
// the shader: the sheet's alpha times a disc that is zero before any edge. Sprites drawn with
// this take the whole sheet, so the uv is the quad's own.
#include "common.sh"

void main()
{
	vec4 sheet = texture2D(s_albedo, v_texcoord0);
	float r = length(v_texcoord0 * 2.0 - 1.0);
	float disc = 1.0 - smoothstep(0.35, 0.95, r);
	float a = sheet.a * disc * v_colour.a;
	// The sheet's brightness and not its colour: smoke02 is painted brown, and a brown plume
	// over a bonfire reads as dust. The colour is the tint, which the game lights.
	float grey = dot(sheet.rgb, vec3(0.2126, 0.7152, 0.0722)) * 2.5;
	gl_FragColor = vec4(v_colour.rgb * grey * a, a);
}
