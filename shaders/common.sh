// What every pass shares: the uniform block, and the sky that both the ambient and the
// reflections are read out of. See docs/conventions.md for what is linear and what is not.
#ifndef MU2_COMMON_SH
#define MU2_COMMON_SH

#include <bgfx_shader.sh>

uniform vec4 u_sunDir;      // xyz: towards the sun, world space. w: its strength
uniform vec4 u_sunColour;   // rgb: linear. w: ambient strength
uniform vec4 u_skyColour;   // rgb: the zenith. w: the horizon's paleness
uniform vec4 u_groundColour;// rgb: the turf that bounces light up. w: unused
uniform vec4 u_camPos;      // xyz: the eye, world space. w: the far plane
uniform vec4 u_params;      // x: ssao radius  y: ssao strength  z: exposure  w: shadow texel
uniform vec4 u_material;    // x: cutout threshold (<0 is no cutout)  y: two-sided  zw: unused

SAMPLER2D(s_albedo,   0);
SAMPLER2D(s_normal,   1);
SAMPLER2D(s_orm,      2);
SAMPLER2D(s_emissive, 3);

// The shadow map is read twice, which Metal allows: once through the compare sampler for
// the filtered result, once as plain depth to find the blocker. docs/conventions.md.
SAMPLER2DSHADOW(s_shadowCompare, 4);
SAMPLER2D(s_shadowDepth, 5);

SAMPLER2D(s_prepass, 6);    // rgb: view normal  a: view depth in units
SAMPLER2D(s_ao,      7);
SAMPLER2D(s_colour,  8);

// The sky, in closed form, from the same two colours the ambient uses. No cubemap and no
// pass: MU2 measured a probe grid at 2.7 ms for a faint sheen on painted art. A prefiltered
// cubemap arrives in sprint 8 for the worlds that earn one.
vec3 skyColour(vec3 dir)
{
	float up = dir.y;
	// Below the horizon is lit turf, above it the zenith fading to a pale horizon.
	vec3 above = mix(u_skyColour.rgb * (1.0 + u_skyColour.w), u_skyColour.rgb, saturate(up));
	vec3 below = u_groundColour.rgb * (0.35 + 0.65 * saturate(1.0 + up));
	vec3 col = mix(below, above, saturate(up * 8.0 + 0.5));
	// A halo round the sun, wide and weak: it is what a wet surface catches.
	float halo = pow(saturate(dot(dir, u_sunDir.xyz)), 8.0);
	return col + u_sunColour.rgb * halo * 0.35 * u_sunDir.w;
}

// Roughness flattens the sky towards its own mean rather than through a prefilter chain:
// the horizon's edge widens and the features wash out, which is what a blurred cubemap of
// this sky would look like.
vec3 skyPrefiltered(vec3 dir, float roughness)
{
	vec3 sharp = skyColour(dir);
	vec3 mean = mix(u_groundColour.rgb, u_skyColour.rgb, 0.5);
	return mix(sharp, mean, roughness * roughness);
}

// Karis' analytic fit to the split-sum BRDF, so there is no lookup texture to carry.
vec3 envBRDFApprox(vec3 f0, float roughness, float ndotv)
{
	const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
	const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
	vec4 r = roughness * c0 + c1;
	float a004 = min(r.x * r.x, exp2(-9.28 * ndotv)) * r.x + r.y;
	vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
	return f0 * ab.x + vec3_splat(ab.y);
}

float distributionGGX(float ndoth, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float d = ndoth * ndoth * (a2 - 1.0) + 1.0;
	return a2 / max(3.14159265 * d * d, 1e-7);
}

float geometrySmith(float ndotv, float ndotl, float roughness)
{
	float r = roughness + 1.0;
	float k = r * r * 0.125;
	float gv = ndotv / (ndotv * (1.0 - k) + k);
	float gl = ndotl / (ndotl * (1.0 - k) + k);
	return gv * gl;
}

vec3 fresnelSchlick(vec3 f0, float vdoth)
{
	return f0 + (vec3_splat(1.0) - f0) * pow(1.0 - vdoth, 5.0);
}

// Interleaved gradient noise: one turn of the sampling disc per pixel, stable under a still
// camera and cheap enough to do per tap.
float gradientNoise(vec2 pixel)
{
	return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

#endif // MU2_COMMON_SH
