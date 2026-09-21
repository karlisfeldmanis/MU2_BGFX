$input a_position, a_texcoord0, a_indices, a_weight, i_data0, i_data1, i_data2, i_data3, i_data5
$output v_texcoord0, v_light

// vs_depth with a skin on it: the shadow pass, where a figure must cast the pose it is in
// rather than the pose it was bound in. The texcoord is here for the same reason it is in
// vs_depth -- a cutout has to discard in this pass too.
#include "common.sh"

void main()
{
	mat4 model = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
	mat4 skin = skinMatrix(a_indices, a_weight, int(i_data5.x));
	v_texcoord0 = a_texcoord0;
	v_light = vec4(1.0, 1.0, 1.0, 2.0 + i_data5.y);  // w: 2 + the fade, as vs_skinned sends it
	gl_Position = mul(u_viewProj, mul(model, mul(skin, vec4(a_position, 1.0))));
}
