$input a_position, a_normal, a_texcoord0, a_color0
$output v_wpos, v_texcoord0, v_normal, v_colour, v_vnormal, v_vpos

// The land. No instancing and no model matrix: MU2's pipeline already built the terrain in
// world space, in metres, with rows running -z.
#include "common.sh"

void main()
{
	v_wpos = a_position;
	// In TILES, not in [0,1]. Each half of the surface multiplies by its own repeat.
	v_texcoord0 = a_texcoord0;
	v_normal = normalize(a_normal);
	// rgb is MU's baked TerrainLight, a is the weight from base to overlay.
	v_colour = a_color0;

	vec4 vpos = mul(u_view, vec4(a_position, 1.0));
	v_vpos = vpos.xyz;
	v_vnormal = normalize(mul(u_view, vec4(v_normal, 0.0)).xyz);

	gl_Position = mul(u_viewProj, vec4(a_position, 1.0));
}
