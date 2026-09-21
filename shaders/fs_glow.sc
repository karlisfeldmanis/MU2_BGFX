$input v_wpos, v_texcoord0, v_normal, v_tangent, v_vnormal, v_vpos, v_light

// MU's BlendMesh: the one submesh an object draws ADDED to the frame at its BlendMeshLight --
// the street light's smear, the torch's flame card, the bonfire's fire, the candle's wick,
// the lit window panes. Sprint 8a. Drawn in the transparent pass into the HDR target the
// shade pass wrote, depth-tested against it and writing none, so it is tonemapped with the
// scene it glows in. Not lit and not shadowed: it IS the light.
//
// MU draws these with glBlendFunc(GL_ONE, GL_ONE) and ignores the texture's alpha, so black
// is what makes the card vanish, and this does the same: the colour is the sheet's own.
//
// v_light.w is the flicker, carried in the instance's fifth vec4 where every other draw has a
// 1.0 it never reads. u_material.z is the sheet's glow_strength, set per draw by the renderer.
// The input list is vs_static's to the letter, which Metal requires of a linked pair.
#include "common.sh"

void main()
{
	vec3 sheet = texture2D(s_albedo, v_texcoord0).rgb;
	gl_FragColor = vec4(sheet * (v_light.w * u_material.z), 1.0);
}
