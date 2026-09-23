$input a_position, i_data0, i_data1, i_data2
$output v_wpos, v_texcoord0, v_normal, v_colour, v_vnormal, v_vpos, v_light

// The near field's cards. ONE vertex shader for both the prepass and the shade pass, because
// the shade pass tests depth EQUAL against the prepass and two shaders that merely compute the
// same thing are not the same thing.
//
// The vertex buffer holds no geometry: a_position is (card index, t, side) and every metre of
// it is grown in grass.sh. content/grass.cpp builds it once and never touches it again.
#include "grass.sh"

void main()
{
	Card card = grassCard(i_data0, i_data1, a_position.x);

	vec3 wpos;
	vec2 uv;
	vec3 normal;
	grassVertex(card, a_position.y, a_position.z, wpos, uv, normal);

	v_wpos = wpos;
	v_texcoord0 = uv;   // the sheet's own space, the card's column already chosen
	v_normal = normal;
	// rgb is MU's baked TerrainLight for the tile this card stands on -- the same light the
	// ground under it is multiplied by, read from the same grid, so a tuft on a dark tile is
	// dark. a is how far this card's tuft has gone to straw.
	v_colour = vec4(i_data2.rgb, card.dry);
	// x: how far up the card this is, which is the AO ramp and the root-to-tip tint.
	// y: the card's own tint, so neighbours are not the same green.
	v_light = vec4(a_position.y, card.tint, 0.0, 1.0);

	vec4 vpos = mul(u_view, vec4(wpos, 1.0));
	v_vpos = vpos.xyz;
	v_vnormal = normalize(mul(u_view, vec4(normal, 0.0)).xyz);

	gl_Position = mul(u_viewProj, vec4(wpos, 1.0));
}
