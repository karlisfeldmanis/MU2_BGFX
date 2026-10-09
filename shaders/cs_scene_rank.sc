// The camera's scenery, step one of three (gfx/scenery.h): one thread a model, walking its
// placements in the cook's order. Each is tested against the camera's frustum -- the model's
// sphere through the placement's matrix, a zero matrix hidden, as cs_cast_cull -- and a
// placement inside is given its rank among them; one outside is given none.
//
// In order and not by an atomic, because the order is the picture: two parts of the town that
// meet at the same depth are settled by which is drawn first, and an atomic's order changes
// every frame, which is a shimmer along every such edge on a still screen (2026-10-09).
#include "bgfx_compute.sh"

BUFFER_RO(s_castResident, vec4, 0);  // Renderer::kInstanceFloats: seven vec4s a placement
BUFFER_RO(s_castModels, vec4, 1);    // two a model: sphere (centre, radius), run (first, count)
BUFFER_WO(s_sceneRank, uint, 2);     // a placement's rank in its model, or 0xffffffff
BUFFER_WO(s_sceneCounts, uint, 3);   // a model's placements in the frustum

uniform vec4 u_cast;           // models, unused, unused, unused
uniform vec4 u_castPlanes[6];  // the camera's six, normalised, inside positive

NUM_THREADS(64, 1, 1)
void main()
{
	uint model = gl_GlobalInvocationID.x;
	if (model >= uint(u_cast.x)) return;
	vec4 sphere = s_castModels[model * 2u + 0u];
	vec4 run = s_castModels[model * 2u + 1u];
	uint first = uint(run.x);
	uint count = uint(run.y);
	uint n = 0u;
	for (uint i = 0u; i < count; ++i)
	{
		uint slot = first + i;
		vec4 c0 = s_castResident[slot * 7u + 0u];
		vec4 c1 = s_castResident[slot * 7u + 1u];
		vec4 c2 = s_castResident[slot * 7u + 2u];
		vec4 c3 = s_castResident[slot * 7u + 3u];
		float scale2 = max(dot(c0.xyz, c0.xyz), max(dot(c1.xyz, c1.xyz), dot(c2.xyz, c2.xyz)));
		bool inside = scale2 > 0.0;
		vec3 centre = c0.xyz * sphere.x + c1.xyz * sphere.y + c2.xyz * sphere.z + c3.xyz;
		float radius = sphere.w * sqrt(max(scale2, 0.0));
		for (int k = 0; k < 6; ++k)
		{
			vec4 p = u_castPlanes[k];
			if (dot(p.xyz, centre) + p.w < -radius) inside = false;
		}
		s_sceneRank[slot] = inside ? n : 0xffffffffu;
		if (inside) n += 1u;
	}
	s_sceneCounts[model] = n;
}
