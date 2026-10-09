// Step two: each placement tested against the sun's split, and the ones inside copied, whole,
// into their model's run of the instance buffer the draws read. gfx/casters.h.
//
// A placement is its model's bounding sphere carried through its own matrix, the radius by the
// matrix's largest scale. Hidden (a roof, a held-back piece) is a zero matrix, and is skipped.
// The order a run fills in is the atomics' and not the cook's; a depth-only pass writes the
// nearest depth whichever order it is drawn in.
#include "bgfx_compute.sh"

BUFFER_RO(s_castResident, vec4, 0);  // Renderer::kInstanceFloats: six vec4s a placement
BUFFER_RO(s_castSlotModel, uint, 1); // which model a placement is, or 0xffffffff for none
BUFFER_RO(s_castModels, vec4, 2);    // two a model: sphere (centre, radius), run (first, count)
BUFFER_RW(s_castCounts, uint, 3);
BUFFER_WO(s_castOut, vec4, 4);

uniform vec4 u_cast;           // placements, models, draws, unused
uniform vec4 u_castPlanes[6];  // the split's six, normalised, inside positive

NUM_THREADS(64, 1, 1)
void main()
{
	uint slot = gl_GlobalInvocationID.x;
	if (slot >= uint(u_cast.x)) return;
	uint model = s_castSlotModel[slot];
	if (model == 0xffffffffu) return;

	vec4 c0 = s_castResident[slot * 6u + 0u];
	vec4 c1 = s_castResident[slot * 6u + 1u];
	vec4 c2 = s_castResident[slot * 6u + 2u];
	vec4 c3 = s_castResident[slot * 6u + 3u];
	float scale2 = max(dot(c0.xyz, c0.xyz), max(dot(c1.xyz, c1.xyz), dot(c2.xyz, c2.xyz)));
	if (scale2 <= 0.0) return;

	vec4 sphere = s_castModels[model * 2u + 0u];
	// vs_depth's mtxFromCols(i_data0..3): the matrix's columns are the four vec4s.
	vec3 centre = c0.xyz * sphere.x + c1.xyz * sphere.y + c2.xyz * sphere.z + c3.xyz;
	float radius = sphere.w * sqrt(scale2);
	for (int i = 0; i < 6; ++i)
	{
		vec4 p = u_castPlanes[i];
		if (dot(p.xyz, centre) + p.w < -radius) return;
	}

	uint n;
	atomicFetchAndAdd(s_castCounts[model], 1u, n);
	uint to = (uint(s_castModels[model * 2u + 1u].x) + n) * 6u;
	for (uint k = 0u; k < 6u; ++k)
	{
		s_castOut[to + k] = s_castResident[slot * 6u + k];
	}
}
