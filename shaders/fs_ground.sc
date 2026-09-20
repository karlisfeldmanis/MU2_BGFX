$input v_wpos, v_texcoord0, v_normal, v_colour, v_vnormal, v_vpos

// The land, lit. Two full material sets blended by the vertex weight MU2's pipeline baked,
// under MU's own baked terrain light. This is the one surface that does not fit the closed
// material model in docs/conventions.md, and it is a second shader rather than a fourth flag.
#include "common.sh"

uniform mat4 u_shadowMtx;
uniform vec4 u_shadowParams;  // x: depth bias  y: penumbra scale  z: map texel  w: normal bias
uniform vec4 u_groundRepeat;  // x: base repeat  y: overlay repeat  z: base relief  w: overlay relief

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
	float blend = saturate(v_colour.a);
	vec2 uvBase = v_texcoord0 * u_groundRepeat.x;
	vec2 uvOver = v_texcoord0 * u_groundRepeat.y;

	// Both halves are always sampled. A branch on the weight would save the read only where
	// a tile is wholly one surface, and on MU's land the blend runs across most of the map.
	vec3 albedo = mix(texture2D(s_albedo, uvBase).rgb, texture2D(s_albedo2, uvOver).rgb, blend);
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
	vec3 nm = normalize(mix(nmBase, nmOver, blend));
	vec3 n = normalize(t * nm.x + b * nm.y + ng * nm.z);

	float ao = orm.r;
	float roughness = clamp(orm.g, 0.04, 1.0);
	float metal = orm.b;

	vec2 pixel = gl_FragCoord.xy;
	ao *= texture2D(s_ao, pixel * u_viewTexel.xy).r;

	vec3 v = normalize(u_camPos.xyz - v_wpos);
	vec3 f0 = mix(vec3_splat(0.04), albedo, metal);
	vec3 diffuseColour = albedo * (1.0 - metal);

	vec3 l = normalize(u_sunDir.xyz);
	float ndotl = saturate(dot(n, l));
	float ndotv = saturate(dot(n, v)) + 1e-5;

	vec3 colour = vec3_splat(0.0);
	if (ndotl > 0.0)
	{
		float shadow = sunShadow(v_wpos, ng, saturate(dot(ng, l)), pixel);
		vec3 h = normalize(l + v);
		vec3 f = fresnelSchlick(f0, saturate(dot(v, h)));
		float d = distributionGGX(saturate(dot(n, h)), roughness);
		float g = geometrySmith(ndotv, ndotl, roughness);
		vec3 spec = f * d * g / max(4.0 * ndotv * ndotl, 1e-5);
		vec3 kd = (vec3_splat(1.0) - f) * diffuseColour / 3.14159265;
		colour += (kd + spec) * u_sunColour.rgb * u_sunDir.w * ndotl * shadow;
	}

	float up = n.y * 0.5 + 0.5;
	colour += mix(u_groundColour.rgb, u_skyColour.rgb, up) * u_sunColour.w * diffuseColour * ao;

	vec3 r = reflect(-v, n);
	colour += skyPrefiltered(r, roughness) * u_sunColour.w * envBRDFApprox(f0, roughness, ndotv) * ao;

	// MU's own baked TerrainLight. Already a lit result and not an albedo, so it multiplies
	// what the light did and never goes through the sRGB sampler.
	colour *= v_colour.rgb * 2.0;

	gl_FragColor = vec4(colour, 1.0);
}
