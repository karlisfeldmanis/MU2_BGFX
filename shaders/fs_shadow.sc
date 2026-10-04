$input v_texcoord0, v_light

// Writes no colour: the depth is the pass. All it does is honour a cutout.
#include "common.sh"

void main()
{
	float alpha = texture2D(s_albedo, v_texcoord0).a;
	if (u_material.x >= 0.0)
	{
		if (alpha < u_material.x) discard;
	}
	// A figure coming in casts what it is: the dither's holes become a partly cast shadow
	// once the pass that reads this map has filtered it.
	// And a glow that casts at a strength (Material::glowShadow) is thinned the same way by
	// u_material.z, what of it is taken away -- 0 on every other caster and on the ground: the
	// Ice Monster's lighter shadow.
	float fade = figureFade(v_light.w) * (1.0 - u_material.z);
	// And a soft-alpha part (u_material.y's 4, content::Material::softAlpha) casts as much as
	// its sheet is there: the Bahamut's membrane a thin shadow, its rays a full one.
	fade *= mix(1.0, alpha, step(3.5, u_material.y));
	if (fade < 1.0 && ditherAt(gl_FragCoord.xy) >= fade) discard;
	gl_FragColor = vec4_splat(1.0);
}
