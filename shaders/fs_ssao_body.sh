// The body of fs_ssao, shared by the single-sampled and multisampled variants so the
// logic exists once. See common.sh's prepassAt.
// Half resolution, twelve samples on a spiral, from the prepass alone. No noise texture:
// the disc is turned per pixel by interleaved gradient noise, which the blur then hides.
#include "common.sh"

uniform vec4 u_camRay;  // xy: tan of half the field of view, across and up

// The view position a prepass texel stands for. A perspective projection needs no matrix
// here: ndc.x is x / (-z) over tan(fovX/2), so x is ndc.x * tan * depth. Thirteen inverse
// projections a pixel was most of this pass's cost.
vec3 viewPosAt(vec2 uv, float depth)
{
	vec2 ndc = uv * 2.0 - 1.0;
	ndc.y = -ndc.y;  // Metal's origin is the top left; docs/conventions.md
	return vec3(ndc * u_camRay.xy * depth, -depth);
}

void main()
{
	vec4 here = prepassAt(v_texcoord0);
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
	// The radius in pixels, which is the projection's job and not a bare divide: a sphere of
	// `radius` metres at `depth` metres covers `radius * projScale / depth` pixels, where
	// projScale is half the target's height over tan(fovY/2). Without the scale this was
	// metres over metres -- 0.042 at the bench's distance -- and every tap landed a
	// twentieth of a texel from the centre, which is why the pass did nothing at all.
	float pixelRadius = radius * u_params.w / max(depth, 1e-3);

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

		vec4 tap = prepassAt(v_texcoord0 + offset);
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
