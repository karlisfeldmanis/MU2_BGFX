$input a_position
$output v_texcoord0

// The land into the sun's split. Position only: the ground has no cutout to honour, and
// fs_shadow is told so by a negative cutout threshold.
//
// The land is in the shadow pass at all because MU's maps are not flat. Lorencia rises only
// 2.2 m, so it casts almost nothing on itself here, but a map with a real slope shades its
// own north face and leaving the ground out would light that face as though the hill were
// not there.
#include "common.sh"

void main()
{
	v_texcoord0 = vec2(0.0, 0.0);
	gl_Position = mul(u_viewProj, vec4(a_position, 1.0));
}
