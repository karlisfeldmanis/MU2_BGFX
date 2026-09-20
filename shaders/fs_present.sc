$input v_texcoord0

// The only place tonemapping and the sRGB write happen: ACES, then the backbuffer.
// docs/conventions.md.
#include "common.sh"

// Narkowicz' fit to the ACES curve. Cheap, and close enough that the difference is not
// visible on MU's painted art.
vec3 aces(vec3 x)
{
	const float a = 2.51;
	const float b = 0.03;
	const float c = 2.43;
	const float d = 0.59;
	const float e = 0.14;
	return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

vec3 toSrgb(vec3 linearColour)
{
	vec3 lo = linearColour * 12.92;
	vec3 hi = 1.055 * pow(linearColour, vec3_splat(1.0 / 2.4)) - 0.055;
	return mix(hi, lo, step(linearColour, vec3_splat(0.0031308)));
}

void main()
{
	vec3 hdr = texture2D(s_colour, v_texcoord0).rgb * u_params.z;
	gl_FragColor = vec4(toSrgb(aces(hdr)), 1.0);
}
