$input a_position, a_normal, a_tangent, a_texcoord0, a_indices, a_weight, i_data0, i_data1, i_data2, i_data3, i_data4, i_data5
$output v_wpos, v_texcoord0, v_normal, v_tangent, v_vnormal, v_vpos, v_light

// vs_static with a skin on it, and deliberately nothing else: it declares the same varyings
// in the same order so that it can be paired with fs_prepass and fs_shade exactly as
// vs_static is. bgfx's Metal backend links a program by matching the two varying lists, and
// a vertex shader that names one varying more than its fragment shader does is refused with
// no message but "the frame is missing a program" -- which is how adding v_light to
// vs_static broke fs_prepass in sprint 3.
#include "common.sh"

void main()
{
	mat4 model = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
	mat4 skin = skinMatrix(a_indices, a_weight, int(i_data5.x));
	mat4 world = mul(model, skin);

	vec4 wpos = mul(world, vec4(a_position, 1.0));
	v_wpos = wpos.xyz;
	v_texcoord0 = a_texcoord0;
	// w is 2 + the figure's fade (i_data5.y); common.sh's figureFade says why the 2.
	v_light = vec4(i_data4.xyz, 2.0 + i_data5.y);

	// The rig has no non-uniform scale -- no clip in this content animates one at all -- so
	// the world matrix itself carries normals and there is no inverse transpose to build.
	v_normal = normalize(mul(world, vec4(a_normal, 0.0)).xyz);
	v_tangent = vec4(normalize(mul(world, vec4(a_tangent.xyz, 0.0)).xyz), a_tangent.w);

	vec4 vpos = mul(u_view, wpos);
	v_vpos = vpos.xyz;
	v_vnormal = normalize(mul(u_view, vec4(v_normal, 0.0)).xyz);

	gl_Position = mul(u_viewProj, wpos);
}
