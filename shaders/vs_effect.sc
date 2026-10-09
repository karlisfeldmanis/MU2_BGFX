$input a_position, a_texcoord0, a_color0, a_texcoord1
$output v_texcoord0, v_colour, v_material

// An effect sprite. The quad is already billboarded and already in world metres -- the CPU
// turned the camera's right and up into four corners -- so this is a transform and nothing
// else. See src/gfx/effects.cpp for why the corners are built there: at a few hundred
// sprites the CPU cost is nothing, and the alternative is handing this shader the camera's
// basis and rebuilding the same four corners once per vertex.
//
// a_texcoord1 is the sprite's sheet slot and kind, which fs_effect reads through v_material
// (the merged scenery's varying, free in this program).
#include "common.sh"

void main()
{
	v_texcoord0 = a_texcoord0;
	v_colour = a_color0;
	v_material = vec4(a_texcoord1.xy, 0.0, 0.0);
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
}
