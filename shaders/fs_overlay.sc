$input v_texcoord0, v_colour

// One channel out of the font atlas, used as coverage. The glyph's colour is the vertex's,
// so one draw can hold a whole list with a line of it picked out in another colour.
// s_albedo comes from common.sh at stage 0, which is where the overlay binds its atlas.
// Declaring it again here is a redefinition and shaderc says so.
#include "common.sh"

void main()
{
	float coverage = texture2D(s_albedo, v_texcoord0).r;
	// A panel is a quad with no glyph under it: the caller points it at a texel that is solid,
	// so this needs no branch and the whole overlay stays one draw.
	gl_FragColor = vec4(v_colour.rgb, v_colour.a * coverage);
}
