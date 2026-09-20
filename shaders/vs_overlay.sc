$input a_position, a_texcoord0, a_color0
$output v_texcoord0, v_colour

// The overlay's quads, already in pixels. u_viewRect carries the backbuffer's size, so the
// CPU hands over pixel coordinates and this does the one division rather than the caller
// doing it per vertex against a size it would have to be told.
#include "common.sh"

void main()
{
	v_texcoord0 = a_texcoord0;
	v_colour = a_color0;
	vec2 ndc = a_position.xy / u_viewRect.zw * 2.0 - 1.0;
	// Pixels run down the screen and clip space runs up it.
	gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
}
