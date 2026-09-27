$input v_wpos, v_texcoord0, v_normal, v_tangent, v_vnormal, v_vpos, v_light, v_refine
// v_light's colour is unused: this pass writes a normal and a depth and has no use for MU's
// baked light, and its w carries a figure's fade, which this pass has no use for either: a
// figure that is only part there is not in this pass at all. The renderer draws it after the
// shade pass, depth first and then blended, which is what makes its fade an opacity. It is here because bgfx's Metal backend links a program by matching the
// two varying lists, so a fragment shader sharing vs_static must name everything vs_static
// writes -- leaving it out fails the link, and the log says only "the frame is missing a
// program".

// The view normal and the view depth, in one RGBA16F target. SSAO reads this and nothing
// else: no unpacking, no second sample of the depth buffer.
#include "common.sh"

void main()
{
	if (u_material.x >= 0.0)
	{
		if (texture2D(s_albedo, v_texcoord0).a < u_material.x) discard;
	}
	// gl_FrontFacing on Metal is the opposite sense from the winding CULL_CW keeps, so the
	// facing is taken from the view direction instead. docs/conventions.md.
	// Turned to the side of the TRIANGLE that faces the eye, not tested per pixel against the
	// view: most of the town's normals are inverted and smoothed, and the per-pixel test split
	// a triangle into turned and unturned halves. fs_shade says more.
	vec3 n = normalize(v_vnormal);
	vec3 face = normalize(cross(dFdx(v_vpos), dFdy(v_vpos)));
	if (dot(face, -v_vpos) < 0.0) face = -face;
	// Nearly flat against the face, the face's own normal; fs_shade says why.
	float along = dot(n, face);
	if (along < 0.0) n = -n;
	n = normalize(mix(face, n, smoothstep(0.1, 0.35, abs(along))));
	gl_FragColor = vec4(n, -v_vpos.z);
}
