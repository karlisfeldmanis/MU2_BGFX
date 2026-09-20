$input a_position, a_normal, a_tangent, a_texcoord0, i_data0, i_data1, i_data2, i_data3, i_data4
$output v_wpos, v_texcoord0, v_normal, v_tangent, v_vnormal, v_vpos, v_light

// The one vertex shader for anything static: the shade and prepass passes both use it, and
// the instance's model matrix is the same four vec4s every pass reads.
#include "common.sh"

void main()
{
	mat4 model = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
	vec4 wpos = mul(model, vec4(a_position, 1.0));
	v_wpos = wpos.xyz;
	v_texcoord0 = a_texcoord0;
	// MU's baked terrain light where this instance stands, as World.cs's Lit() reads it: a
	// lit result already, so it multiplies the albedo and is not lit again.
	v_light = i_data4.rgb;

	// Normals go by the model matrix's rotation. MU2's build has no non-uniform scale on a
	// placement, so the matrix itself serves and there is no inverse transpose to carry.
	v_normal = normalize(mul(model, vec4(a_normal, 0.0)).xyz);
	v_tangent = vec4(normalize(mul(model, vec4(a_tangent.xyz, 0.0)).xyz), a_tangent.w);

	vec4 vpos = mul(u_view, wpos);
	v_vpos = vpos.xyz;
	v_vnormal = normalize(mul(u_view, vec4(v_normal, 0.0)).xyz);

	gl_Position = mul(u_viewProj, wpos);
}
