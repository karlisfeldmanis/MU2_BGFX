$input v_texcoord0

// The only place tonemapping and the sRGB write happen: a tone curve, the grade, then the
// backbuffer.
// docs/conventions.md.
#include "common.sh"

// The bloom chain's top level, half resolution, added before the tonemap. Sprint 8b.
SAMPLER2D(s_bloom, 9);
uniform vec4 u_bloom;  // z: how much of the chain is added
uniform vec4 u_present;  // x: sharpen 0..1  y: contrast 0..1  zw: one pixel in uv
uniform vec4 u_grade;    // x: which curve (see tonemap)  y: saturation  z: split-tone amount
uniform vec4 u_tintLow;  // rgb: what the shade is multiplied towards
uniform vec4 u_tintHigh; // rgb: what the light is multiplied towards

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

// Stephen Hill's fit of ACES's RRT and ODT, with its two colour-space matrices. Fuller than
// Narkowicz's: bright colours desaturate less towards yellow-white, so a flame stays orange.
// Narkowicz's fit folds in a 0.6 exposure that this does not, hence the 1/0.6.
vec3 acesHill(vec3 x)
{
	x *= 1.0 / 0.6;
	vec3 v = vec3(dot(vec3(0.59719, 0.35458, 0.04823), x),
	              dot(vec3(0.07600, 0.90834, 0.01566), x),
	              dot(vec3(0.02840, 0.13383, 0.83777), x));
	vec3 a = v * (v + 0.0245786) - 0.000090537;
	vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
	v = a / b;
	return saturate(vec3(dot(vec3(1.60475, -0.53108, -0.07367), v),
	                     dot(vec3(-0.10208, 1.10813, -0.00605), v),
	                     dot(vec3(-0.00327, -0.07276, 1.07602), v)));
}

// AgX (Troy Sobotka), in Benjamin Wrensch's minimal form: into AgX's inset space, a log
// encoding, a sigmoid, and back out. `punch` is its "punchy" look: a steeper curve and more
// saturation, applied in the log space the way the reference does. The matrices are written
// out by column, since a mat3 constructor's order is not the same on every backend.
vec3 agx(vec3 x, float punch)
{
	x = vec3(0.842479062253094, 0.0423282422610123, 0.0423756549057051) * x.r
	  + vec3(0.0784335999999992, 0.878468636469772, 0.0784336) * x.g
	  + vec3(0.0792237451477643, 0.0791661274605434, 0.879142973793104) * x.b;
	const float lo = -12.47393;
	const float hi = 4.026069;
	x = (clamp(log2(max(x, vec3_splat(1e-10))), lo, hi) - lo) / (hi - lo);
	vec3 x2 = x * x;
	vec3 x4 = x2 * x2;
	x = 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2
	  + 0.1191 * x - 0.00232;
	if (punch > 0.5)
	{
		float luma = dot(x, vec3(0.2126, 0.7152, 0.0722));
		x = pow(max(x, vec3_splat(0.0)), vec3_splat(1.35));
		luma = dot(x, vec3(0.2126, 0.7152, 0.0722));
		x = luma + 1.4 * (x - luma);
	}
	x = vec3(1.19687900512017, -0.0528968517574562, -0.0529716355144438) * x.r
	  + vec3(-0.0980208811401368, 1.15190312990417, -0.0980434501171241) * x.g
	  + vec3(-0.0990297440797205, -0.0989611768448433, 1.15107367264116) * x.b;
	return saturate(pow(max(x, vec3_splat(0.0)), vec3_splat(2.2)));
}

// Khronos PBR Neutral: colour kept as authored up to 0.76 and only the highlights rolled off.
// The least filmic of the five, and the one that changes MU's painting least.
vec3 neutral(vec3 c)
{
	float x = min(c.r, min(c.g, c.b));
	float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
	c -= offset;
	float peak = max(c.r, max(c.g, c.b));
	if (peak < 0.76) return c;
	float d = 0.24;
	float newPeak = 1.0 - d * d / (peak + d - 0.76);
	c *= newPeak / peak;
	float g = 1.0 - 1.0 / (0.15 * (peak - newPeak) + 1.0);
	return mix(c, vec3_splat(newPeak), g);
}

// The sheet's `tonemap`: 0 Narkowicz's ACES, 1 Hill's ACES, 2 AgX, 3 AgX punchy, 4 Neutral.
vec3 tonemap(vec3 x)
{
	if (u_grade.x < 0.5) return aces(x);
	if (u_grade.x < 1.5) return acesHill(x);
	if (u_grade.x < 2.5) return agx(x, 0.0);
	if (u_grade.x < 3.5) return agx(x, 1.0);
	return saturate(neutral(x));
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
	return tonemap((texture2D(s_colour, uv).rgb + glow) * u_params.z);
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

	// The grade, on the displayed linear colour. Saturation about the luminance, then a split
	// tone: the shade multiplied towards one tint and the light towards another, by how bright
	// the pixel is. Cool shade and warm light is the look Diablo IV is built on; invention.
	float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
	c = max(vec3_splat(luma) + (c - luma) * u_grade.y, vec3_splat(0.0));
	vec3 tint = mix(u_tintLow.rgb, u_tintHigh.rgb, smoothstep(0.02, 0.5, luma));
	c = saturate(c * mix(vec3_splat(1.0), tint, u_grade.z));

	vec3 srgb = toSrgb(c);
	// Midtone contrast along a smoothstep, in the space the eye reads, black and white held.
	srgb = mix(srgb, srgb * srgb * (3.0 - 2.0 * srgb), u_present.y);
	gl_FragColor = vec4(srgb, 1.0);
}
