$input v_texcoord0

// One level of one face of the probe's chain: the box of raw texels it covers, averaged. The
// filter reads the chain at the mip whose texel matches each of its samples, which is what
// lets 32 samples stand for a wide lobe without sparkling.
//
// Made here and not by bgfx's own mip generation, which on Metal runs when the NEXT view binds
// a different frame buffer: a probe face is the last thing in a frame, and the filter read
// whole faces of chain that had never been made. docs/sprints/08c-the-metal.md.
#include "common.sh"
#include "probe.sh"

SAMPLERCUBE(s_source, 0);

void main()
{
	// u_probeFace.y is the level; a texel of it covers `span` raw texels a side.
	int span = int(exp2(u_probeFace.y) + 0.5);
	float rawTexel = 1.0 / u_probeFace.z;
	// The uv of this texel's first raw texel centre, then a step of one raw texel.
	vec2 first = v_texcoord0 - rawTexel * (float(span) * 0.5 - 0.5);
	// A texel that is not a finite number is left out. A handful of the town's far meshes
	// shade to NaN (a cluster at Lorencia's east horizon, found with isnan in the faces), and
	// one such texel averaged in is a NaN level, which the filter's wide lobes then carried
	// across whole faces as black. The camera rarely sees those meshes; the probe sees all.
	vec3 sum = vec3_splat(0.0);
	float count = 0.0;
	for (int j = 0; j < span; ++j)
	{
		for (int i = 0; i < span; ++i)
		{
			vec2 uv = first + vec2(float(i), float(j)) * rawTexel;
			vec3 c = textureCubeLod(s_source, cubeDir(u_probeFace.x, uv), 0.0).rgb;
			if (!any(isnan(c)) && !any(isinf(c)))
			{
				sum += c;
				count += 1.0;
			}
		}
	}
	gl_FragColor = vec4(sum / max(count, 1.0), 1.0);
}
