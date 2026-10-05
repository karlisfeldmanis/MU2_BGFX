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

// The spiral's taps at no turn: tap i at t = (i + 0.5) / 12, sqrt(t) out and t * 3 turns round.
// The turn rotates the table, one cos and one sin a pixel where there were twelve of each
// (docs/perf-audit-2k.md, A5). `static` and braces for the HLSL bgfx's Metal path parses.
// Note what the numbers show: t * 3 turns is (i + 0.5) quarter turns, so the twelve taps lie
// on four diagonals, rotated together per pixel. Kept exactly as it was drawn.
static const vec2 kSpiral[12] = {
	vec2(0.1443376, 0.1443376),
	vec2(-0.2500000, 0.2500000),
	vec2(-0.3227486, -0.3227486),
	vec2(0.3818813, -0.3818813),
	vec2(0.4330127, 0.4330127),
	vec2(-0.4787135, 0.4787136),
	vec2(-0.5204165, -0.5204165),
	vec2(0.5590170, -0.5590170),
	vec2(0.5951190, 0.5951190),
	vec2(-0.6291529, 0.6291529),
	vec2(-0.6614378, -0.6614378),
	vec2(0.6922186, -0.6922187)
};

void main()
{
	vec4 here = prepassAt(v_texcoord0);
	float depth = here.w;
	// Nothing was drawn here: the sky is not occluded.
	// The depth rides in g, so the blur reads it here rather than taking seventeen texels of
	// the multisampled prepass for it. The AO is put back on R8's steps, which is what this
	// target held before it carried the depth too, so the blur averages the same numbers.
	if (depth <= 0.0)
	{
		gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);
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

	float turnAngle = gradientNoise(gl_FragCoord.xy) * 6.2831853;
	vec2 turn = vec2(cos(turnAngle), sin(turnAngle));
	float occlusion = 0.0;
	const int kSamples = 12;
	for (int i = 0; i < kSamples; ++i)
	{
		// A spiral: the radius grows as the square root, which spreads the samples evenly over
		// the area rather than the line. kSpiral above, turned by this pixel's angle.
		vec2 spiral = kSpiral[i];
		vec2 turned = vec2(spiral.x * turn.x - spiral.y * turn.y, spiral.x * turn.y + spiral.y * turn.x);
		vec2 offset = turned * pixelRadius * u_viewTexel.xy;

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
	float ao = floor(saturate(1.0 - occlusion * u_params.y) * 255.0 + 0.5) / 255.0;
	gl_FragColor = vec4(ao, depth, 0.0, 1.0);
}
