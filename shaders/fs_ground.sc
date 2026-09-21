$input v_wpos, v_texcoord0, v_normal, v_colour, v_vnormal, v_vpos

// The land, lit. Two full material sets blended by the vertex weight MU2's pipeline baked,
// under MU's own baked terrain light. This is the one surface that does not fit the closed
// material model in docs/conventions.md, and it is a second shader rather than a fourth flag.
#include "common.sh"

#include "shadow.sh"

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
	// still carrying (high - low) * bite of the overlay, where the weight says it should carry
	// none. The interlock belongs in the middle of a fade, where two pictures are actually
	// competing; at the ends there is only one.
	//
	// It does NOT remove the hard edge visible along the moat. That edge is between two
	// different surface pairs, which are two different draws, and nothing inside one draw can
	// gradate across it -- shot before and after this blend, the sawtooth is identical. An
	// earlier commit message credited the taper with fixing it, and that was wrong.
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
	// Z rebuilt, never read: cooked normals are BC5 and carry two channels, so .z arrives
	// as zero and every normal would lie flat in the tangent plane. For the three-channel
	// png the land reads when the cook has not run, this recovers what they stored.
	vec2 nbXY = texture2D(s_normal, uvBase).xy * 2.0 - 1.0;
	vec3 nmBase = vec3(nbXY, sqrt(max(0.0, 1.0 - dot(nbXY, nbXY))));
	vec2 noXY = texture2D(s_normal2, uvOver).xy * 2.0 - 1.0;
	vec3 nmOver = vec3(noXY, sqrt(max(0.0, 1.0 - dot(noXY, noXY))));
	// Mixed linearly by the same weight the albedo used, and the relief mixed with it and
	// applied afterwards -- which is MU2's order (NORMAL_MAP takes the mixed normal and
	// NORMAL_MAP_DEPTH the mixed relief), not each layer's relief applied before the mix.
	// Every relief in all three shipped worlds is 1.0, so no pixel differs today; the comment
	// above this claimed parity with MU2 and the code did not have it.
	vec3 nm = normalize(mix(nmBase, nmOver, blend));
	nm.xy *= mix(u_groundRepeat.z, u_groundRepeat.w, blend);
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

	// No sky reflection and no sun specular on dry ground. MU's ground art has its own
	// lighting painted into it, so a sheen on top is a second highlight on a surface that
	// already carries one -- and a ground plane seen from MU's 48 degrees is grazing nearly
	// everywhere, so Fresnel spreads that highlight across most of the frame. MU2 measured
	// this and pins its ground SPECULAR to zero; water is the exception and is sprint 8's.

	gl_FragColor = vec4(colour, 1.0);
}
