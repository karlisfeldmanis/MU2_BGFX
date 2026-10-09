// The town's casters on the GPU, step one of three: every model's count of placements in the
// sun's split back to nought. gfx/casters.h.
#include "bgfx_compute.sh"

BUFFER_WO(s_castCounts, uint, 0);

uniform vec4 u_cast;  // placements, models, draws, unused

NUM_THREADS(64, 1, 1)
void main()
{
	uint model = gl_GlobalInvocationID.x;
	if (model < uint(u_cast.y)) s_castCounts[model] = 0u;
}
