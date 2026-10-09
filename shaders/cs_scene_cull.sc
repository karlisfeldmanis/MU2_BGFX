// The camera's scenery, step two (gfx/scenery.h): one thread a placement of a part's model.
// A placement cs_scene_rank kept is copied to its rank in the part's own run of the records,
// its seventh vec4 the part's material -- so one indirect draw is one part, in the cook's
// order, and fs_*_merged read its sheets by the layer that seventh vec4 names (vs_static's
// v_material). x is the placement within its model, y the part.
#include "bgfx_compute.sh"

BUFFER_RO(s_castResident, vec4, 0);  // Renderer::kInstanceFloats: seven vec4s a placement
BUFFER_RO(s_castModels, vec4, 1);    // two a model: sphere (centre, radius), run (first, count)
BUFFER_RO(s_sceneDraws, vec4, 2);    // three a part: draw, (first record), material
BUFFER_RO(s_sceneRank, uint, 3);
BUFFER_WO(s_sceneOut, vec4, 4);      // the same seven, the seventh the part's material

uniform vec4 u_cast;  // unused, parts, unused, unused

NUM_THREADS(64, 1, 1)
void main()
{
	uint i = gl_GlobalInvocationID.x;
	uint part = gl_GlobalInvocationID.y;
	if (part >= uint(u_cast.y)) return;
	vec4 d = s_sceneDraws[part * 3u + 0u];
	uint model = uint(d.w);
	vec4 run = s_castModels[model * 2u + 1u];
	if (i >= uint(run.y)) return;
	uint slot = uint(run.x) + i;
	uint rank = s_sceneRank[slot];
	if (rank == 0xffffffffu) return;

	uint to = (uint(s_sceneDraws[part * 3u + 1u].x) + rank) * 7u;
	for (uint k = 0u; k < 6u; ++k)
	{
		s_sceneOut[to + k] = s_castResident[slot * 7u + k];
	}
	s_sceneOut[to + 6u] = s_sceneDraws[part * 3u + 2u];
}
