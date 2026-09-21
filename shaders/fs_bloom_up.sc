$input v_texcoord0

// Bloom, going up: the level below, read through a 3x3 tent and ADDED into this one by the
// blend state, so each level carries every smaller one on top of its own. Sprint 8b.
#include "common.sh"

uniform vec4 u_bloom;       // x: threshold  y: knee  z: unused  w: the tent's radius in texels
uniform vec4 u_bloomTexel;  // xy: one texel of the SOURCE level, in uv

void main()
{
	vec2 t = u_bloomTexel.xy * u_bloom.w;
	vec2 uv = v_texcoord0;
	vec3 sum = texture2D(s_colour, uv).rgb * 4.0;
	sum += texture2D(s_colour, uv + vec2(-t.x, 0.0)).rgb * 2.0;
	sum += texture2D(s_colour, uv + vec2( t.x, 0.0)).rgb * 2.0;
	sum += texture2D(s_colour, uv + vec2(0.0, -t.y)).rgb * 2.0;
	sum += texture2D(s_colour, uv + vec2(0.0,  t.y)).rgb * 2.0;
	sum += texture2D(s_colour, uv + vec2(-t.x, -t.y)).rgb;
	sum += texture2D(s_colour, uv + vec2( t.x, -t.y)).rgb;
	sum += texture2D(s_colour, uv + vec2(-t.x,  t.y)).rgb;
	sum += texture2D(s_colour, uv + vec2( t.x,  t.y)).rgb;
	gl_FragColor = vec4(sum * (1.0 / 16.0), 1.0);
}
