$input a_position, i_data0, i_data1, i_data2, i_data3
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
	Card card = grassCard(i_data0, i_data1, i_data3, a_position.x);

	vec3 wpos;
	vec2 uv;
	vec3 normal;
	grassVertex(card, a_position.y, a_position.z, wpos, uv, normal);

	v_wpos = wpos;
	v_texcoord0 = uv;   // the sheet's own space, the card's column already chosen
	v_normal = normal;
	// rgb is MU's baked TerrainLight for the tile this card stands on -- the same light the
	// ground under it is multiplied by, read from the same grid, so a tuft on a dark tile is
	// dark -- taken down towards the root where the card stands in a bunch, because a blade
	// in the middle of a tuft is shaded by the tuft. a is the coarse field: how well the grass
	// is doing here, which fs_grass turns into straw at one end and a deeper green at the other.
	float shade = 1.0 - 0.28 * card.bunch * (1.0 - a_position.y);
	v_colour = vec4(i_data2.rgb * shade, card.vigour);
	// x: how far up the card this is, which is the AO ramp and the root-to-tip tint.
	// y: the card's own brightness, so neighbours are not the same green.
	// z: which of the four tints it wears.  w: its own warmth or coolness on top of that.
	v_light = vec4(a_position.y, card.tint, card.hue, card.warm);

	vec4 vpos = mul(u_view, vec4(wpos, 1.0));
	v_vpos = vpos.xyz;
	v_vnormal = normalize(mul(u_view, vec4(normal, 0.0)).xyz);

	gl_Position = mul(u_viewProj, vec4(wpos, 1.0));
}
