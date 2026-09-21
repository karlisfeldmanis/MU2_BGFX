$input v_texcoord0, v_colour

// The windows' art times the vertex colour. The face's atlas is white with its coverage in
// alpha, so a letter and a plate are the same arithmetic and share a draw when they are
// neighbours in the list. No decode: the art is drawn after the tonemap, as painted.
// s_albedo comes from common.sh at stage 0.
#include "common.sh"

void main()
{
	gl_FragColor = texture2D(s_albedo, v_texcoord0) * v_colour;
}
