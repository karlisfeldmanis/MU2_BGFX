$input v_texcoord0

// Writes no colour: the depth is the pass. All it does is honour a cutout.
#include "common.sh"

void main()
{
	if (u_material.x >= 0.0)
	{
		if (texture2D(s_albedo, v_texcoord0).a < u_material.x) discard;
	}
	gl_FragColor = vec4_splat(1.0);
}
