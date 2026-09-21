$input v_texcoord0

// The sky behind one face of the probe: the closed-form sky the shade pass used to reflect,
// at the ambient's strength, so a face shows sky wherever the town does not stand in front
// of it. Drawn first in the face's view, which is sequential, and the town over it.
#include "common.sh"
#include "probe.sh"

void main()
{
	vec3 dir = cubeDir(u_probeFace.x, v_texcoord0);
	gl_FragColor = vec4(skyColour(dir) * u_sunColour.w, 1.0);
}
