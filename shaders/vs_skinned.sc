$input a_position, a_normal, a_tangent, a_texcoord0, a_indices, a_weight, i_data0, i_data1, i_data2, i_data3, i_data4, i_data5
$output v_wpos, v_texcoord0, v_normal, v_tangent, v_vnormal, v_vpos, v_light, v_refine, v_material

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
	// And 2 more on a self-lit figure, whose instance light's w is 2 (Figure::gather).
	v_light = vec4(i_data4.xyz, 2.0 + i_data5.y + (i_data4.w >= 2.0 ? 2.0 : 0.0));
	// As vs_static takes it apart.
	float packed = i_data5.w;
	float red = floor((packed + 0.5) / 10201.0);
	float green = floor((packed - red * 10201.0 + 0.5) / 101.0);
	float blue = packed - red * 10201.0 - green * 101.0;
	v_refine = vec4(i_data5.z, red * 0.01, green * 0.01, blue * 0.01);

	// The rig has no non-uniform scale -- no clip in this content animates one at all -- so
	// the world matrix itself carries normals and there is no inverse transpose to build.
	v_normal = normalize(mul(world, vec4(a_normal, 0.0)).xyz);
	v_tangent = vec4(normalize(mul(world, vec4(a_tangent.xyz, 0.0)).xyz), a_tangent.w);

	vec4 vpos = mul(u_view, wpos);
	v_vpos = vpos.xyz;
	v_vnormal = normalize(mul(u_view, vec4(v_normal, 0.0)).xyz);

	// vs_static's seventh vec4, which a figure has no use for: the fragment shaders it shares
	// with vs_static take it in.
	v_material = vec4(0.0, 0.0, 1.0, 1.0);
	gl_Position = mul(u_viewProj, wpos);
}
