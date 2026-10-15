// The body of fs_ssao, shared by the single-sampled and multisampled variants so the
// logic exists once. See common.sh's prepassAt.
// Half resolution, twelve samples on a spiral, from the prepass alone. No noise texture:
// the disc is turned per pixel by interleaved gradient noise, which the blur then hides.
#include "common.sh"

uniform vec4 u_camRay;  // xy: tan of half the field of view, across and up
                        // z: the contact march's thickness, metres  w: its strength
uniform vec4 u_contact; // xyz: towards the sun, VIEW space  w: the march's length, metres; 0 off

// The view position a prepass texel stands for. A perspective projection needs no matrix
// here: ndc.x is x / (-z) over tan(fovX/2), so x is ndc.x * tan * depth. Thirteen inverse
// projections a pixel was most of this pass's cost.
vec3 viewPosAt(vec2 uv, float depth)
{
	vec2 ndc = uv * 2.0 - 1.0;
	ndc.y = -ndc.y;  // Metal's origin is the top left; docs/reference/conventions.md
	return vec3(ndc * u_camRay.xy * depth, -depth);
}

// The contact shadow: how much of the sun something on screen hides within a few decimetres,
// which the sun's split cannot draw: a leaf on the leaf below it, a fold of cloth. With the sun
// behind the camera (gfx/lighting.h) a ray towards it mostly runs towards the eye, so it finds
// little at a figure's feet; it is cheap (0.12 ms at 1080p) for what it finds. Ten
// steps from the pixel towards the sun; the first one that has gone in behind what the prepass
// holds there, by less than the thickness, is a blocker. The thickness is what keeps a figure
// standing in front of a wall from shadowing the wall: the ray passes behind the figure by
// more than a body is thick. Only ever darkens: the shade takes the least of this and the
// split. 0 is lit. The step's start is turned per pixel, and the blur hides the grain.
float contactShadow(vec3 p, vec3 n, float depth, float jitter)
{
	vec3 l = u_contact.xyz;
	if (u_contact.w <= 0.0 || dot(n, l) <= 0.0) return 0.0;

	const int kSteps = 10;
	float stepLength = u_contact.w / float(kSteps);
	// Off the surface by the prepass's own precision -- a half float holds depth to a
	// thousandth of itself -- so a plane does not find itself one step along.
	float grain = 0.002 * depth + 0.005;
	vec3 start = p + n * grain;
	for (int i = 0; i < kSteps; ++i)
	{
		float t = (float(i) + jitter) / float(kSteps);
		vec3 q = start + l * (u_contact.w * t + stepLength * 0.5);
		if (q.z > -0.05) break;  // behind the eye
		vec2 ndc = q.xy / (-q.z * u_camRay.xy);
		vec2 uv = vec2(ndc.x, -ndc.y) * 0.5 + 0.5;
		if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;
		float there = prepassAt(uv).w;
		if (there <= 0.0) continue;  // sky
		float gap = -q.z - there;  // > 0: the surface there stands in front of the ray
		if (gap > grain && gap < u_camRay.z)
		{
			// Fainter the farther along the hit is, so the shadow ends in a fade and not a line,
			// and fading out past 40 m, where a step spans several pixels and finds noise.
			float far = 1.0 - smoothstep(25.0, 40.0, depth);
			return u_camRay.w * (1.0 - t * t) * far;
		}
	}
	return 0.0;
}

// The spiral's taps at no turn: tap i at t = (i + 0.5) / 12, sqrt(t) out and t * 3 turns round.
// The turn rotates the table, one cos and one sin a pixel where there were twelve of each
// (docs/reference/budget.md, the 2K audit's A5). `static` and braces for the HLSL bgfx's Metal path parses.
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
	gl_FragColor = vec4(ao, depth, contactShadow(p, n, depth, gradientNoise(gl_FragCoord.xy + 7.0)), 1.0);
}
