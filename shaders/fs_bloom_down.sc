$input v_texcoord0

// Bloom, going down: one level of the chain halved from the level above. Sprint 8b.
//
// Thirteen taps in the pattern Jimenez gave for Call of Duty: a 4x4 box and four 2x2 boxes
// round it, which is what stops a bright pixel moving between two blocks from shimmering as
// the camera walks. The first level also takes the threshold, and weights each of its five
// boxes by 1 / (1 + luminance) -- Karis' average -- so that one pixel of a flame at 40 does
// not become a square of 40 at 1/32 and flash as it crosses texels.
#include "common.sh"

uniform vec4 u_bloom;       // x: threshold  y: knee  z: 1 on the first level  w: unused
uniform vec4 u_bloomTexel;  // xy: one texel of the SOURCE level, in uv

vec3 tap(vec2 uv) { return texture2D(s_colour, uv).rgb; }

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

// Only what is over the threshold, and a soft knee under it so nothing snaps on.
vec3 bright(vec3 c)
{
	float l = max(c.r, max(c.g, c.b));
	float soft = clamp(l - u_bloom.x + u_bloom.y, 0.0, 2.0 * u_bloom.y);
	soft = soft * soft / (4.0 * u_bloom.y + 1e-5);
	float keep = max(soft, l - u_bloom.x) / max(l, 1e-5);
	return c * keep;
}

// One 2x2 box, and on the first level its Karis weight in .w: the box's own threshold, then
// 1 / (1 + luminance). Elsewhere the weight is 1.
vec4 box(vec3 a, vec3 b, vec3 c, vec3 d, float share)
{
	vec3 sum = (a + b + c + d) * 0.25;
	float w = share;
	if (u_bloom.z > 0.5)
	{
		sum = bright(sum);
		w *= 1.0 / (1.0 + luma(sum));
	}
	return vec4(sum * w, w);
}

void main()
{
	vec2 t = u_bloomTexel.xy;
	vec2 uv = v_texcoord0;
	vec3 a = tap(uv + t * vec2(-2.0, -2.0));
	vec3 b = tap(uv + t * vec2( 0.0, -2.0));
	vec3 c = tap(uv + t * vec2( 2.0, -2.0));
	vec3 d = tap(uv + t * vec2(-1.0, -1.0));
	vec3 e = tap(uv + t * vec2( 1.0, -1.0));
	vec3 f = tap(uv + t * vec2(-2.0,  0.0));
	vec3 g = tap(uv);
	vec3 h = tap(uv + t * vec2( 2.0,  0.0));
	vec3 i = tap(uv + t * vec2(-1.0,  1.0));
	vec3 j = tap(uv + t * vec2( 1.0,  1.0));
	vec3 k = tap(uv + t * vec2(-2.0,  2.0));
	vec3 l = tap(uv + t * vec2( 0.0,  2.0));
	vec3 m = tap(uv + t * vec2( 2.0,  2.0));

	vec4 sum = box(d, e, i, j, 0.5);
	sum += box(a, b, f, g, 0.125);
	sum += box(b, c, g, h, 0.125);
	sum += box(f, g, k, l, 0.125);
	sum += box(g, h, l, m, 0.125);
	gl_FragColor = vec4(sum.rgb / max(sum.w, 1e-5), 1.0);
}
