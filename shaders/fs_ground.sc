$input v_wpos, v_texcoord0, v_normal, v_colour, v_vnormal, v_vpos, v_weight

// The land, lit. Up to three full material sets blended by the weights Ground::splat shares
// between every tile on a corner, under MU's own baked terrain light. This is the one surface that does not fit the closed
// material model in docs/conventions.md, and it is a second shader rather than a fourth flag.
// Stage 15 is the probe's, which the land never reads. abyss() in common.sh.
#define MU2_ABYSS 15
#include "common.sh"

#include "shadow.sh"
#include "lights.sh"

uniform vec4 u_groundRepeat;  // xyz: each layer's repeat  w: the bite
uniform vec4 u_groundBlend;   // xyz: each layer's water slide  w: how many layers this part weighs
uniform vec4 u_groundRelief;  // xyz: each layer's relief  w: which layers are water, a bit each
uniform vec4 u_groundSlots;   // xyz: each layer's slot in the weight map  w: 1 when it is bound
uniform vec4 u_groundWeights; // xy: the weight map's size in texels  z: rows a band  w: pad rows

// A texel's height, taken off its luminance. The proxy MU2's own pipeline uses, and a fair
// one on art where the raised stones are lit and the mortar between them is not.
float heightOf(vec3 colour)
{
	return dot(colour, vec3(0.299, 0.587, 0.114));
}

SAMPLER2D(s_albedo2,   9);
SAMPLER2D(s_normal2,  10);
SAMPLER2D(s_orm2,     11);
// The third layer, on slots nothing else the land reads is bound to.
SAMPLER2D(s_albedo3,   3);
SAMPLER2D(s_normal3,   8);
SAMPLER2D(s_orm3,     12);

// Every tile slot's weight at every grid corner, four to a band. Ground::splat.
SAMPLER2D(s_groundWeights, 6);

// One slot's weight at the fragment, through a cubic B-spline over the corners around it:
// four bilinear taps placed so the hardware's lerp does the spline's weights. Bilinear, or
// the two triangles a quad is cut into, draws MU's one-metre painting as a staircase with a
// crease down every quad's diagonal, which along a shore is the zigzag; the spline draws the
// same corners as a curve. It smooths rather than interpolates, so a lone painted corner
// arrives at four ninths of itself -- the price of a shore that is not a saw.
float splatWeight(float slot, vec2 at0, vec2 at1, vec4 g)
{
	float band = floor(slot * 0.25 + 0.01);
	float channel = slot - band * 4.0;
	vec4 mask = vec4_splat(1.0) - step(vec4_splat(0.5), abs(vec4(0.0, 1.0, 2.0, 3.0) - channel));
	float lift = band * u_groundWeights.z + u_groundWeights.w;
	vec2 texel = vec2(1.0, 1.0) / u_groundWeights.xy;
	vec4 sum = texture2DLod(s_groundWeights, vec2(at0.x, at0.y + lift) * texel, 0.0) * g.x * g.z
	         + texture2DLod(s_groundWeights, vec2(at1.x, at0.y + lift) * texel, 0.0) * g.y * g.z
	         + texture2DLod(s_groundWeights, vec2(at0.x, at1.y + lift) * texel, 0.0) * g.x * g.w
	         + texture2DLod(s_groundWeights, vec2(at1.x, at1.y + lift) * texel, 0.0) * g.y * g.w;
	return dot(sum, mask);
}

vec3 unpackNormal(vec2 xy)
{
	vec2 n = xy * 2.0 - 1.0;
	return vec3(n, sqrt(max(0.0, 1.0 - dot(n, n))));
}

void main()
{
	// A water layer slides along U, as MU's does; zero on every other. See submitGround.
	vec2 uv0 = v_texcoord0 * u_groundRepeat.x + vec2(u_groundBlend.x, 0.0);
	vec2 uv1 = v_texcoord0 * u_groundRepeat.y + vec2(u_groundBlend.y, 0.0);
	vec2 uv2 = v_texcoord0 * u_groundRepeat.z + vec2(u_groundBlend.z, 0.0);
	float layers = u_groundBlend.w;

	// Only the layers this part has. The branch is on a uniform, so every fragment of a draw
	// takes the same side and the mips' derivatives stay whole. Most of the land is one
	// material, and that part reads one set.
	vec3 albedo0 = texture2D(s_albedo, uv0).rgb;
	vec3 albedo1 = albedo0;
	vec3 albedo2 = albedo0;
	if (layers > 1.5) albedo1 = texture2D(s_albedo2, uv1).rgb;
	if (layers > 2.5) albedo2 = texture2D(s_albedo3, uv2).rgb;

	// The weight MU painted, as a water level, with the layers' own relief deciding which
	// side of it a texel falls. This is MU2's blend, traced from its GroundSource rather than
	// invented: a straight lerp is a flat facet with a crease at every tile edge, and at a
	// bite of one the join becomes speckle. A third is where stones come through as stones.
	//
	// Three layers where MU2 had two, and the same blend: each weight moves by
	// 4 * bite * w * (h - mean h). With two layers that is w1 + 4 * bite * w1 * w0 * (h1 - h0),
	// which is MU2's taper exactly -- nought at both ends of a fade, where only one picture
	// is there, and strongest in the middle. The moves sum to nothing, so the coverage MU
	// painted is the coverage that comes out.
	//
	// The weights are Ground::splat's, shared by every tile on a corner. That is what took
	// the sawtooth off the shores: two tiles no longer disagree about a corner, so there is
	// no edge for one draw to leave against the next.
	vec3 w = v_weight.xyz;
	if (u_groundSlots.w > 0.5 && layers > 1.5)
	{
		// The spline's four weights on each axis, folded into two taps an axis. Corner i
		// is texel i + 0.5, and the uv is in tiles, so the uv is already in corners.
		vec2 base = floor(v_texcoord0);
		vec2 f = v_texcoord0 - base;
		vec2 f2 = f * f;
		vec2 f3 = f2 * f;
		vec2 w0 = (vec2_splat(1.0) - 3.0 * f + 3.0 * f2 - f3) / 6.0;
		vec2 w1 = (vec2_splat(4.0) - 6.0 * f2 + 3.0 * f3) / 6.0;
		vec2 w2 = (vec2_splat(1.0) + 3.0 * f + 3.0 * f2 - 3.0 * f3) / 6.0;
		vec2 w3 = f3 / 6.0;
		vec2 g0 = w0 + w1;
		vec2 g1 = w2 + w3;
		vec2 at0 = base + vec2_splat(0.5) - vec2_splat(1.0) + w1 / g0;
		vec2 at1 = base + vec2_splat(0.5) + vec2_splat(1.0) + w3 / g1;
		vec4 g = vec4(g0.x, g1.x, g0.y, g1.y);
		w.x = splatWeight(u_groundSlots.x, at0, at1, g);
		w.y = splatWeight(u_groundSlots.y, at0, at1, g);
		w.z = layers > 2.5 ? splatWeight(u_groundSlots.z, at0, at1, g) : 0.0;
	}
	w = max(w, vec3_splat(0.0));
	w /= max(w.x + w.y + w.z, 1e-5);
	// Sharpened, where the spline has made a fade: cubed and renormalised, which leaves the
	// halfway line where MU put it and narrows the band either side of it. Without this the
	// spline's fade was two tiles wide on every shore, and water under seventy percent
	// already reads as the sand over it -- the rivers looked a tile narrower on each bank.
	if (u_groundSlots.w > 0.5)
	{
		w = w * w * w;
		w /= max(w.x + w.y + w.z, 1e-5);
		// And water leads at its own shore. MU draws a water tile as water right to its edge
		// and the land tile beside it as land; the shared corner on that edge averages to
		// half and half, and water's dark sheet at half reads as the land over it -- the
		// banks stood back from the river by a third of a tile. Tripled, the edge corner is
		// three quarters water, which is where it reads as water. Only down at the water's
		// level: v_weight.w is how much lead a corner takes, and up on a bridge's deck it is
		// nought, which takes the water out of the corner altogether. See Ground::splat.
		vec3 isWater = mod(floor(vec3_splat(u_groundRelief.w) / vec3(1.0, 2.0, 4.0)), 2.0);
		w *= vec3_splat(1.0) + isWater * (3.0 * v_weight.w - 1.0);
		w /= max(w.x + w.y + w.z, 1e-5);
	}
	// Each layer's relief measured from its own mean, which is its last mip. Measured from
	// nought, a dark sheet is low everywhere and loses every fade it is in: water is the
	// darkest sheet on the map and was eaten along its whole shore. MU2 got away with it
	// because its water tiles were pure water and only a thin strip ever blended.
	vec3 h = vec3(heightOf(albedo0) - heightOf(texture2DLod(s_albedo, uv0, 16.0).rgb),
	              heightOf(albedo1) - heightOf(texture2DLod(s_albedo2, uv1, 16.0).rgb),
	              heightOf(albedo2) - heightOf(texture2DLod(s_albedo3, uv2, 16.0).rgb));
	float mean = dot(w, h);
	w = max(w + 4.0 * u_groundRepeat.w * w * (h - mean), vec3_splat(0.0));
	w /= max(w.x + w.y + w.z, 1e-5);

	vec3 albedo = albedo0 * w.x + albedo1 * w.y + albedo2 * w.z;
	vec3 orm0 = texture2D(s_orm, uv0).rgb;
	vec3 orm = orm0 * w.x;
	vec3 nm0 = unpackNormal(texture2D(s_normal, uv0).xy);
	vec3 nm = nm0 * w.x;
	if (layers > 1.5)
	{
		orm += texture2D(s_orm2, uv1).rgb * w.y;
		nm += unpackNormal(texture2D(s_normal2, uv1).xy) * w.y;
	}
	else
	{
		orm += orm0 * w.y;
		nm += nm0 * w.y;
	}
	if (layers > 2.5)
	{
		orm += texture2D(s_orm3, uv2).rgb * w.z;
		nm += unpackNormal(texture2D(s_normal3, uv2).xy) * w.z;
	}
	else
	{
		orm += orm0 * w.z;
		nm += nm0 * w.z;
	}

	// The tangent frame is analytic: the land's uv runs along world x and z, so the tangent
	// is x and the bitangent is z, with no TANGENT attribute to carry for a quarter of a
	// million vertices.
	vec3 ng = normalize(v_normal);
	vec3 t = normalize(vec3(1.0, 0.0, 0.0) - ng * ng.x);
	vec3 b = cross(ng, t);
	// Z rebuilt, never read (unpackNormal): cooked normals are BC5 and carry two channels, so
	// .z arrives as zero and every normal would lie flat in the tangent plane.
	// Mixed linearly by the same weights the albedo used, and the relief mixed with them and
	// applied afterwards -- which is MU2's order (NORMAL_MAP takes the mixed normal and
	// NORMAL_MAP_DEPTH the mixed relief), not each layer's relief applied before the mix.
	nm = normalize(nm);
	nm.xy *= dot(w, u_groundRelief.xyz);
	nm = normalize(nm);
	vec3 n = normalize(t * nm.x + b * nm.y + ng * nm.z);

	float ao = orm.r;
	float roughness = clamp(orm.g, 0.04, 1.0);
	float metal = orm.b;

	vec2 pixel = gl_FragCoord.xy;
	ao *= texture2D(s_ao, pixel * u_viewTexel.xy).r;

	// MU's baked TerrainLight rides in the vertex colour and multiplies the ALBEDO, which is
	// what glTF says a vertex colour does and what MU2's own ground shader does:
	// `ALBEDO = albedo * painted.rgb`, with no factor. It was applied here over the whole lit
	// result -- after the sun, the ambient AND the sky reflection -- and scaled by an invented
	// 2.0, which made it a second light rather than a modulation of the surface.
	// The lamps light the albedo as painted, before this. lights.sh.
	vec3 ownAlbedo = albedo;
	albedo *= v_colour.rgb;

	// No view vector and no f0: with no specular and no reflection on dry ground there is
	// nothing left that depends on where the eye is.
	//
	// Metal therefore only subtracts energy here, which is NOT what it does in MU2. Godot's
	// SPECULAR = 0 scales the dielectric F0 alone, so a metal texel there still returns the
	// environment tinted by its albedo; with no specular term at all, ours simply goes dark.
	// Every ORM blue channel in Lorencia's eighteen maps is 0, so nothing differs today. It
	// becomes a real difference when water arrives with a specular path of its own.
	vec3 diffuseColour = albedo * (1.0 - metal);

	vec3 l = normalize(u_sunDir.xyz);
	float ndotl = saturate(dot(n, l));

	vec3 colour = vec3_splat(0.0);
	// The probe's view, as fs_shade draws it.
	if (u_shadowDebug.x > 0.5)
	{
		gl_FragColor = vec4_splat(sunShadow(v_wpos, ng, saturate(dot(ng, l)), pixel));
		return;
	}
	if (ndotl > 0.0)
	{
		float shadow = sunShadow(v_wpos, ng, saturate(dot(ng, l)), pixel);
		// Diffuse only. See the note under the ambient.
		vec3 kd = diffuseColour / 3.14159265;
		colour += kd * u_sunColour.rgb * u_sunDir.w * ndotl * shadow;
	}

	float up = n.y * 0.5 + 0.5;
	colour += mix(u_groundColour.rgb, u_skyColour.rgb, up) * u_sunColour.w * diffuseColour * ao;

	// The lamps, diffuse only, for the same reason the sun is. The eye is needed for their
	// falloff's direction and nothing else, since the specular is switched off.
	vec3 v = normalize(u_camPos.xyz - v_wpos);
	colour += lampLight(v_wpos, n, v, ownAlbedo * (1.0 - metal), vec3_splat(0.0), roughness,
	                    saturate(dot(n, v)) + 1e-5, 0.0);

	// No sky reflection and no sun specular on dry ground. MU's ground art has its own
	// lighting painted into it, so a sheen on top is a second highlight on a surface that
	// already carries one -- and a ground plane seen from MU's 48 degrees is grazing nearly
	// everywhere, so Fresnel spreads that highlight across most of the frame. MU2 measured
	// this and pins its ground SPECULAR to zero; water is the exception and is sprint 8's.

	// Last, so the haze goes into the chasm's black with the land. Ground::splat's slope.
	gl_FragColor = vec4(abyss(dusty(colour, v_wpos), v_wpos), 1.0);
}
