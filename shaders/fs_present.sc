$input v_texcoord0

// The only place tonemapping and the sRGB write happen: ACES, then the backbuffer.
// docs/conventions.md.
#include "common.sh"

// The bloom chain's top level, half resolution, added before the tonemap. Sprint 8b.
SAMPLER2D(s_bloom, 9);
uniform vec4 u_bloom;  // z: how much of the chain is added
uniform vec4 u_present;  // x: sharpen 0..1  y: contrast 0..1  zw: one pixel in uv

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

// One pixel of the frame as it will be seen: bloom added, exposed and tonemapped, still
// linear. The sharpen works on these, since sharpening HDR rings round every flame. The
// bloom is half resolution and smooth, so the centre's is added to every tap rather than
// read five times.
vec3 seen(vec2 uv, vec3 glow)
{
	return aces((texture2D(s_colour, uv).rgb + glow) * u_params.z);
}

void main()
{
	vec2 uv = v_texcoord0;
	vec3 glow = texture2D(s_bloom, uv).rgb * u_bloom.z;
	vec3 c = seen(uv, glow);

	if (u_present.x > 0.0)
	{
		// AMD's contrast-adaptive sharpen in its plain cross form. The weight shrinks where
		// the neighbourhood already spans the whole range, so a hard edge is left alone and a
		// soft painted surface is lifted.
		vec3 n = seen(uv + vec2(0.0, -u_present.w), glow);
		vec3 s = seen(uv + vec2(0.0, u_present.w), glow);
		vec3 e = seen(uv + vec2(u_present.z, 0.0), glow);
		vec3 w = seen(uv + vec2(-u_present.z, 0.0), glow);
		vec3 lo = min(c, min(min(n, s), min(e, w)));
		vec3 hi = max(c, max(max(n, s), max(e, w)));
		vec3 amp = sqrt(saturate(min(lo, vec3_splat(1.0) - hi) / max(hi, vec3_splat(1e-4))));
		vec3 k = -amp * mix(0.125, 0.2, u_present.x);
		c = saturate((c + (n + s + e + w) * k) / (vec3_splat(1.0) + 4.0 * k));
	}

	vec3 srgb = toSrgb(c);
	// Midtone contrast along a smoothstep, in the space the eye reads, black and white held.
	srgb = mix(srgb, srgb * srgb * (3.0 - 2.0 * srgb), u_present.y);
	gl_FragColor = vec4(srgb, 1.0);
}
