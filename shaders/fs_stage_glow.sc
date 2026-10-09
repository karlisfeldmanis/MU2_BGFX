$input v_wpos, v_texcoord0, v_normal, v_tangent, v_vnormal, v_vpos, v_light, v_refine, v_material

// An item's BlendMesh on a window's picture: fs_glow's added sheet, written as sRGB the way
// fs_stage writes the rest of the item. The stage pass draws it with its colour ADDED and the
// picture's alpha left alone, and the interface lays the picture on premultiplied, so where
// the glow stands over nothing it is added to the window under it -- MU's GL_ONE, GL_ONE over
// its inventory -- and a Bluewing Crossbow, which is all glow, is still there.
//
// u_material.z is the level: the material's pulse held at its middle, since a picture is
// taken on change and not every frame.
//
// And a levelled item's tint and chrome as fs_glow's, save on its NoneBlendMesh (y's 8).
#include "common.sh"
#include "shine.sh"

// fs_stage's.
vec3 toSrgb(vec3 c)
{
	c = saturate(c);
	vec3 low = c * 12.92;
	vec3 high = 1.055 * pow(c, vec3_splat(1.0 / 2.4)) - 0.055;
	return mix(low, high, step(vec3_splat(0.0031308), c));
}

void main()
{
	vec4 sheet = texture2D(s_albedo, v_texcoord0);
	float plus = shinePlus(v_refine.x);
	vec3 colour = sheet.rgb * (sheet.a * u_material.z) * shineTint(plus);
	if (mod(floor(u_material.y * 0.125), 2.0) < 0.5)
	{
		vec3 n = normalize(v_normal);
		// Only where the card glows, as fs_glow (shine.sh's shineCover).
		colour += (shineAdded(plus, n, v_refine.yzw) +
		           shineExcellentAdded(shineExcellent(v_refine.x), n) +
		           shineSweepAdded(plus, shineSweep(v_refine.x), n)) * shineCover(sheet);
	}
	gl_FragColor = vec4(toSrgb(colour), 0.0);
}
