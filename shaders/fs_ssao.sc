$input v_texcoord0

// Half resolution, twelve samples on a spiral, from the prepass alone. No noise texture:
// the disc is turned per pixel by interleaved gradient noise, which the blur then hides.
#include "common.sh"

uniform mat4 u_camInvProj;

// The view position a prepass texel stands for. The prepass already holds view depth, so
// this is a ray through the pixel scaled to that depth rather than a matrix multiply.
vec3 viewPosAt(vec2 uv, float depth)
{
	// The ray for this uv, taken from the inverse projection once per sample.
	vec2 ndc = uv * 2.0 - 1.0;
	ndc.y = -ndc.y;  // Metal's origin is the top left; docs/conventions.md
	vec4 h = mul(u_camInvProj, vec4(ndc, 0.0, 1.0));
	vec3 ray = h.xyz / h.w;
	return ray * (depth / max(-ray.z, 1e-6));
}

void main()
{
	vec4 here = texture2D(s_prepass, v_texcoord0);
	float depth = here.w;
	// Nothing was drawn here: the sky is not occluded.
	if (depth <= 0.0)
	{
		gl_FragColor = vec4_splat(1.0);
		return;
	}

	vec3 n = normalize(here.xyz);
	vec3 p = viewPosAt(v_texcoord0, depth);

	float radius = u_params.x;
	// The radius in pixels shrinks with distance, as the sphere it stands for does.
	float pixelRadius = radius / max(depth, 1e-3);

	float turn = gradientNoise(gl_FragCoord.xy) * 6.2831853;
	float occlusion = 0.0;
	const int kSamples = 12;
	for (int i = 0; i < kSamples; ++i)
	{
		float t = (float(i) + 0.5) / float(kSamples);
		// A spiral: the angle turns three times over the disc while the radius grows as the
		// square root, which spreads the samples evenly over the area rather than the line.
		float angle = turn + t * 6.2831853 * 3.0;
		float r = sqrt(t) * pixelRadius;
		vec2 offset = vec2(cos(angle), sin(angle)) * r * u_viewTexel.xy;

		vec4 tap = texture2D(s_prepass, v_texcoord0 + offset);
		if (tap.w <= 0.0) continue;

		vec3 q = viewPosAt(v_texcoord0 + offset, tap.w);
		vec3 toQ = q - p;
		float dist = length(toQ);
		if (dist < 1e-5) continue;

		// How much of the hemisphere that sample stands in front of, faded out past the
		// radius so that a wall across the room does not darken the floor.
		float ndotd = max(dot(n, toQ / dist), 0.0);
		float fade = saturate(1.0 - (dist / radius));
		occlusion += ndotd * fade * fade;
	}
	occlusion /= float(kSamples);
	gl_FragColor = vec4_splat(saturate(1.0 - occlusion * u_params.y));
}
