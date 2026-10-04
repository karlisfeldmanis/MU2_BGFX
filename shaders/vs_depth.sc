$input a_position, a_texcoord0, i_data0, i_data1, i_data2, i_data3, i_data5
$output v_texcoord0, v_light

// Depth only, for the shadow pass: position and texcoord and nothing else. The texcoord is
// there because a cutout must discard in the shadow pass too, or a leaf casts a card.
#include "common.sh"

void main()
{
	mat4 model = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
	v_texcoord0 = a_texcoord0;
	// fs_shadow's dither: a static mesh is all there unless it is in a fading figure's hand.
	v_light = vec4(1.0, 1.0, 1.0, i_data5.y < 1.0 ? 2.0 + i_data5.y : 1.0);
	vec3 p = i_data5.x < 0.0 ? swayed(a_position, i_data3.xyz) : a_position;
	gl_Position = mul(u_viewProj, mul(model, vec4(p, 1.0)));
}
