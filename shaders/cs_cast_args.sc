// Step three: one indirect draw a solid part, as many instances as its model kept in the
// split, starting at its model's run. gfx/scenery.h.
#include "bgfx_compute.sh"

BUFFER_RO(s_castDraws, vec4, 0);   // index count, first index, base vertex, model
BUFFER_RO(s_castCounts, uint, 1);
BUFFER_RO(s_castModels, vec4, 2);
BUFFER_RW(s_castIndirect, uvec4, 3);

uniform vec4 u_cast;  // placements, models, draws, unused

NUM_THREADS(64, 1, 1)
void main()
{
	uint draw = gl_GlobalInvocationID.x;
	if (draw >= uint(u_cast.z)) return;
	vec4 d = s_castDraws[draw];
	uint model = uint(d.w);
	drawIndexedIndirect(s_castIndirect, draw, uint(d.x), s_castCounts[model], uint(d.y),
	                    uint(d.z), uint(s_castModels[model * 2u + 1u].x));
}
