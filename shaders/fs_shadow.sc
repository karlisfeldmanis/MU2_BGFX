$input v_texcoord0, v_light

// Writes no colour: the depth is the pass. All it does is honour a cutout.
#include "common.sh"

void main()
{
	if (u_material.x >= 0.0)
	{
		if (texture2D(s_albedo, v_texcoord0).a < u_material.x) discard;
	}
	// A figure coming in casts what it is: the dither's holes become a partly cast shadow
	// once the pass that reads this map has filtered it.
	float fade = figureFade(v_light.w);
	if (fade < 1.0 && ditherAt(gl_FragCoord.xy) >= fade) discard;
	gl_FragColor = vec4_splat(1.0);
}
