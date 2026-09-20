$input v_wpos, v_texcoord0, v_normal, v_colour, v_vnormal, v_vpos

// The land's view normal and view depth. The blended normal map is not read here: the
// prepass feeds SSAO, which wants the shape of the ground and not its grain, and reading
// six textures to perturb a normal a half-resolution occlusion term then blurs away is
// work for nothing.
#include "common.sh"

void main()
{
	vec3 n = normalize(v_vnormal);
	if (dot(n, normalize(-v_vpos)) < 0.0) n = -n;
	gl_FragColor = vec4(n, -v_vpos.z);
}
