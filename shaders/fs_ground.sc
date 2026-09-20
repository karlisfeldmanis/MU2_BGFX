$input v_wpos, v_texcoord0, v_normal, v_colour, v_vnormal, v_vpos

// The land, lit. Two full material sets blended by the vertex weight MU2's pipeline baked,
// under MU's own baked terrain light. This is the one surface that does not fit the closed
// material model in docs/conventions.md, and it is a second shader rather than a fourth flag.
#include "common.sh"

uniform mat4 u_shadowMtx;
uniform vec4 u_shadowParams;  // x: depth bias  y: penumbra scale  z: map texel  w: normal bias
uniform vec4 u_groundRepeat;  // x: base repeat  y: overlay repeat  z: base relief  w: overlay relief
uniform vec4 u_groundBlend;   // x: bite  y: 1 if this surface has an overlay at all  zw: unused

// A texel's height, taken off its luminance. The proxy MU2's own pipeline uses, and a fair
// one on art where the raised stones are lit and the mortar between them is not.
float heightOf(vec3 colour)
{
	return dot(colour, vec3(0.299, 0.587, 0.114));
}

SAMPLER2D(s_albedo2,   9);
SAMPLER2D(s_normal2,  10);
SAMPLER2D(s_orm2,     11);

vec2 vogel(int i, int count, float phase)
{
	float r = sqrt((float(i) + 0.5) / float(count));
	float theta = float(i) * 2.39996323 + phase;
	return vec2(cos(theta), sin(theta)) * r;
}

float sunShadow(vec3 wpos, vec3 normal, float ndotl, vec2 pixel)
{
	vec4 sc = mul(u_shadowMtx, vec4(wpos + normal * u_shadowParams.w, 1.0));
	sc.xyz /= sc.w;
	if (sc.x < 0.0 || sc.x > 1.0 || sc.y < 0.0 || sc.y > 1.0 || sc.z > 1.0) return 1.0;

	float slope = sqrt(saturate(1.0 - ndotl * ndotl)) / max(ndotl, 0.15);
	float bias = u_shadowParams.x * (1.0 + slope);
	float receiver = sc.z - bias;
	float phase = gradientNoise(pixel) * 6.2831853;

	const int kSearch = 5;
	float searchRadius = u_shadowParams.z * 6.0;
	float blockerSum = 0.0;
	float blockerCount = 0.0;
	for (int i = 0; i < kSearch; ++i)
	{
		float d = texture2D(s_shadowDepth, sc.xy + vogel(i, kSearch, phase) * searchRadius).r;
		if (d < receiver)
		{
			blockerSum += d;
			blockerCount += 1.0;
		}
	}
	if (blockerCount < 0.5) return shadow2D(s_shadowCompare, vec3(sc.xy, receiver));

	// Orthographic: the penumbra is the blocker's gap times the sun's half angle, and the
	// scale that turns the split's 0..1 depth into a uv radius is computed on the CPU.
	// There is no divide by the blocker's own depth here -- that is the point-light formula.
	float penumbra = (receiver - blockerSum / blockerCount) * u_shadowParams.y;
	float radius = clamp(penumbra, u_shadowParams.z, u_shadowParams.z * 24.0);

	const int kFilter = 8;
	float sum = 0.0;
	for (int i = 0; i < kFilter; ++i)
	{
		sum += shadow2D(s_shadowCompare, vec3(sc.xy + vogel(i, kFilter, phase) * radius, receiver));
	}
	return sum / float(kFilter);
}

void main()
{
	vec2 uvBase = v_texcoord0 * u_groundRepeat.x;
	vec2 uvOver = v_texcoord0 * u_groundRepeat.y;

	// Both halves are always sampled. A branch on the weight would save the read only where
	// a tile is wholly one surface, and on MU's land the blend runs across most of the map.
	vec3 albedoBase = texture2D(s_albedo, uvBase).rgb;
	vec3 albedoOver = texture2D(s_albedo2, uvOver).rgb;

	// The weight MU painted, as a water level, with the two layers' own relief deciding which
	// side of it a texel falls. This is MU2's blend, traced from its GroundSource rather than
	// invented: a straight lerp is a flat facet with a crease at every tile edge, and at a
	// bite of one the join becomes speckle. A third is where stones come through as stones.
	//
	// The taper is not a refinement. Added flat, the bite leaves a fragment MU painted empty
	// still carrying (high - low) * bite of the overlay -- a sixth to a quarter of sand over
	// every water texel of a blended tile -- while the tile next door, which has no overlay
	// bound at all, carries none. The two meet on a tile boundary and the shoreline grows an
	// axis-aligned staircase. The interlock belongs in the middle of a fade, where two
	// pictures are actually competing; at the ends there is only one.
	float painted = saturate(v_colour.a);
	float blend = painted;
	if (u_groundBlend.y > 0.5)
	{
		float low = heightOf(albedoBase);
		float high = heightOf(albedoOver);
		float taper = 4.0 * painted * (1.0 - painted);
		blend = saturate(painted + (high - low) * u_groundBlend.x * taper);
	}

	vec3 albedo = mix(albedoBase, albedoOver, blend);
	vec3 orm = mix(texture2D(s_orm, uvBase).rgb, texture2D(s_orm2, uvOver).rgb, blend);

	// The tangent frame is analytic: the land's uv runs along world x and z, so the tangent
	// is x and the bitangent is z, with no TANGENT attribute to carry for a quarter of a
	// million vertices.
	vec3 ng = normalize(v_normal);
	vec3 t = normalize(vec3(1.0, 0.0, 0.0) - ng * ng.x);
	vec3 b = cross(ng, t);
	vec3 nmBase = texture2D(s_normal, uvBase).xyz * 2.0 - 1.0;
	vec3 nmOver = texture2D(s_normal2, uvOver).xyz * 2.0 - 1.0;
	nmBase.xy *= u_groundRepeat.z;
	nmOver.xy *= u_groundRepeat.w;
	// Mixed linearly and by the same weight the albedo used, which is what MU2 does. It does
	// flatten the relief a little mid-blend, where two differing normals partly cancel; that
	// is MU2's behaviour and matching it is the point.
	vec3 nm = normalize(mix(nmBase, nmOver, blend));
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
	albedo *= v_colour.rgb;

	// No view vector and no f0: with no specular and no reflection on dry ground there is
	// nothing left that depends on where the eye is. Metal still matters, because a metal
	// surface has no diffuse -- MU2 keeps METALLIC from the ORM's blue for the same reason
	// while pinning its SPECULAR to zero.
	vec3 diffuseColour = albedo * (1.0 - metal);

	vec3 l = normalize(u_sunDir.xyz);
	float ndotl = saturate(dot(n, l));

	vec3 colour = vec3_splat(0.0);
	if (ndotl > 0.0)
	{
		float shadow = sunShadow(v_wpos, ng, saturate(dot(ng, l)), pixel);
		// Diffuse only. See the note under the ambient.
		vec3 kd = diffuseColour / 3.14159265;
		colour += kd * u_sunColour.rgb * u_sunDir.w * ndotl * shadow;
	}

	float up = n.y * 0.5 + 0.5;
	colour += mix(u_groundColour.rgb, u_skyColour.rgb, up) * u_sunColour.w * diffuseColour * ao;

	// No sky reflection and no sun specular on dry ground. MU's ground art has its own
	// lighting painted into it, so a sheen on top is a second highlight on a surface that
	// already carries one -- and a ground plane seen from MU's 48 degrees is grazing nearly
	// everywhere, so Fresnel spreads that highlight across most of the frame. MU2 measured
	// this and pins its ground SPECULAR to zero; water is the exception and is sprint 8's.

	gl_FragColor = vec4(colour, 1.0);
}
