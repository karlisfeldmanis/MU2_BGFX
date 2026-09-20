$input a_position, a_texcoord0, a_color0
$output v_texcoord0, v_colour

// An effect sprite. The quad is already billboarded and already in world metres -- the CPU
// turned the camera's right and up into four corners -- so this is a transform and nothing
// else. See src/gfx/effects.cpp for why the corners are built there: at a few hundred
// sprites the CPU cost is nothing, and the alternative is handing this shader the camera's
// basis and rebuilding the same four corners once per vertex.
#include "common.sh"

void main()
{
	v_texcoord0 = a_texcoord0;
	v_colour = a_color0;
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
}
