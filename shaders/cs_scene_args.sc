// The camera's scenery, step three (gfx/scenery.h): one indirect draw a part, as many
// instances as its model has in the frustum, starting at the part's own run.
#include "bgfx_compute.sh"

BUFFER_RO(s_sceneDraws, vec4, 0);
BUFFER_RO(s_sceneCounts, uint, 1);
BUFFER_RW(s_sceneIndirect, uvec4, 2);

uniform vec4 u_cast;  // unused, parts, parts, unused

NUM_THREADS(64, 1, 1)
void main()
{
	uint part = gl_GlobalInvocationID.x;
	if (part >= uint(u_cast.z)) return;
	vec4 d = s_sceneDraws[part * 3u + 0u];
	drawIndexedIndirect(s_sceneIndirect, part, uint(d.x), s_sceneCounts[uint(d.w)], uint(d.y),
	                    uint(d.z), uint(s_sceneDraws[part * 3u + 1u].x));
}
