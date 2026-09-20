$input a_position
$output v_texcoord0

// The land into the sun's split. Position only: the ground has no cutout to honour, and
// fs_shadow is told so by a negative cutout threshold.
//
// The land is in the shadow pass at all because MU's maps are not flat, and Lorencia is not
// as flat as it looks from above. Measured off height.png: it rises 3.825 m, and its steepest
// step is 3.36 m across a single tile, which is 73 degrees. 1.56% of its tile edges (2041 of
// 130560) are steeper than the 38 degrees it takes to shade yourself under a 52-degree sun,
// so the ground genuinely casts on the ground here. This comment used to say 2.2 m, which was
// primitive 0's own accessor bounds -- one 590-triangle scrap of the mesh -- read as the
// whole map's.
#include "common.sh"

void main()
{
	v_texcoord0 = vec2(0.0, 0.0);
	gl_Position = mul(u_viewProj, vec4(a_position, 1.0));
}
