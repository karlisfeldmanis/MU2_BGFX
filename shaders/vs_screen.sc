$input a_position, a_texcoord0
$output v_texcoord0

// One triangle over the screen. No instancing, no model matrix.
#include "common.sh"

void main()
{
	v_texcoord0 = a_texcoord0;
	gl_Position = vec4(a_position.xy, 0.0, 1.0);
}
