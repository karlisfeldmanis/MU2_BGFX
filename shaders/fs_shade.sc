$input v_wpos, v_texcoord0, v_normal, v_tangent, v_vnormal, v_vpos, v_light

// The one lit pass. Depth is tested EQUAL against what the prepass laid down and nothing is
// written back, so no pixel here is shaded twice.
#include "common.sh"

uniform mat4 u_shadowMtx;
uniform vec4 u_shadowParams;  // x: depth bias  y: tan of the sun's half angle  z: map texel  w: normal bias

vec2 vogel(int i, int count, float phase)
{
	float r = sqrt((float(i) + 0.5) / float(count));
	float theta = float(i) * 2.39996323 + phase;
	return vec2(cos(theta), sin(theta)) * r;
}

// The shadow hardens on contact. Five taps look for a blocker; where there is none the
// pixel is lit and done, which is most of the ground. Otherwise the penumbra is as wide as
// the blocker is far, for a sun four degrees across -- an invention, since the real half
// degree draws a line.
float sunShadow(vec3 wpos, vec3 normal, float ndotl, vec2 pixel)
{
	vec4 sc = mul(u_shadowMtx, vec4(wpos + normal * u_shadowParams.w, 1.0));
	sc.xyz /= sc.w;
	if (sc.x < 0.0 || sc.x > 1.0 || sc.y < 0.0 || sc.y > 1.0 || sc.z > 1.0) return 1.0;

	// The bias grows as the surface turns away from the sun, where one texel spans more depth.
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
	if (blockerCount < 0.5)
	{
		// Not lit outright: five turned taps can miss a grazing blocker, and a hard 1.0
		// among filtered neighbours was a white speck in the arm's shadow band. One
		// bilinear compare at the pixel settles it, and it measured free.
		return shadow2D(s_shadowCompare, vec3(sc.xy, receiver));
	}

	float blocker = blockerSum / blockerCount;
	// The gap between the blocker and this pixel, times the tangent of the sun's half angle.
	// No divide by the blocker's own depth: that is the similar-triangles formula for a
	// *point* light, where the penumbra grows with how near the caster is to the lamp. The
	// sun's split is orthographic and its depth is linear, so the penumbra is the gap and
	// nothing else. The divide was here through the first review and was reported fixed
	// while it was still running: with the blocker at z ~ 0.475 it widened every penumbra by
	// about 2.1x, drawing a sun some 8.4 degrees across against the sheet's 4.
	// u_shadowParams.y already carries tan(halfAngle) * depthRange / shadowRange, which is
	// what turns a gap in the split's 0..1 depth into a radius in the map's uv.
	float penumbra = (receiver - blocker) * u_shadowParams.y;
	float radius = clamp(penumbra, u_shadowParams.z, u_shadowParams.z * 24.0);

	const int kFilter = 8;
	float sum = 0.0;
	for (int i = 0; i < kFilter; ++i)
	{
		vec2 offset = vogel(i, kFilter, phase) * radius;
		sum += shadow2D(s_shadowCompare, vec3(sc.xy + offset, receiver));
	}
	return sum / float(kFilter);
}

void main()
{
	vec4 albedoTex = texture2D(s_albedo, v_texcoord0);
	if (u_material.x >= 0.0)
	{
		if (albedoTex.a < u_material.x) discard;
	}
	// Times MU's own baked light at the tile this instance stands on. The land carries the
	// same thing per vertex in its COLOR_0, so without this the town stands brighter than
	// the ground it stands on, and MU's painted dusk stops at the foot of every wall.
	vec3 albedo = albedoTex.rgb * v_light;

	vec3 v = normalize(u_camPos.xyz - v_wpos);

	// The geometric normal, turned to face the eye rather than read off gl_FrontFacing,
	// which on Metal has the opposite sense from the winding CULL_CW keeps. MU's figures
	// are single sheets of mixed winding and are drawn two-sided, so this matters on them.
	vec3 ng = normalize(v_normal);
	if (u_material.y > 0.5 && dot(ng, v) < 0.0) ng = -ng;

	// Tangent frame, then the normal map. The bitangent's sign is glTF's w.
	vec3 t = normalize(v_tangent.xyz - ng * dot(ng, v_tangent.xyz));
	vec3 b = cross(ng, t) * v_tangent.w;
	// Z is REBUILT, never read. The cook writes normals as BC5, which stores two channels
	// and nothing else, so .z arrives as zero: read straight, every normal is (x, y, 0), a
	// vector lying flat in the tangent plane with no component along the surface's own
	// normal at all -- and the whole town shades black. A unit vector's third component is
	// what is left, and for the three-channel PNGs the bench still loads this recovers the
	// same number they stored. docs/conventions.md has said BC5 with z rebuilt since before
	// there was a cook; this is the line that keeps it.
	vec2 nxy = texture2D(s_normal, v_texcoord0).xy * 2.0 - 1.0;
	float nz = sqrt(max(0.0, 1.0 - dot(nxy, nxy)));
	vec3 n = normalize(t * nxy.x + b * nxy.y + ng * nz);

	vec3 orm = texture2D(s_orm, v_texcoord0).rgb;
	float ao = orm.r;
	float roughness = clamp(orm.g, 0.04, 1.0);
	float metal = orm.b;

	// The screen-space AO multiplies the baked one. gl_FragCoord is in pixels; the AO target
	// is half resolution but is sampled by uv, so the halving needs no arithmetic here.
	vec2 pixel = gl_FragCoord.xy;
	float ssao = texture2D(s_ao, pixel * u_viewTexel.xy).r;
	ao *= ssao;

	vec3 f0 = mix(vec3_splat(0.04), albedo, metal);
	vec3 diffuseColour = albedo * (1.0 - metal);

	vec3 l = normalize(u_sunDir.xyz);
	float ndotl = saturate(dot(n, l));
	float ndotv = saturate(dot(n, v)) + 1e-5;

	vec3 colour = vec3_splat(0.0);

	// The sun.
	if (ndotl > 0.0)
	{
		float shadow = sunShadow(v_wpos, ng, saturate(dot(ng, l)), pixel);
		vec3 h = normalize(l + v);
		float ndoth = saturate(dot(n, h));
		float vdoth = saturate(dot(v, h));
		vec3 f = fresnelSchlick(f0, vdoth);
		float d = distributionGGX(ndoth, roughness);
		float g = geometrySmith(ndotv, ndotl, roughness);
		vec3 spec = f * d * g / max(4.0 * ndotv * ndotl, 1e-5);
		vec3 kd = (vec3_splat(1.0) - f) * diffuseColour / 3.14159265;
		colour += (kd + spec) * u_sunColour.rgb * u_sunDir.w * ndotl * shadow;
	}

	// The hemisphere ambient: sky above, bounced turf below, occluded by the AO.
	float up = n.y * 0.5 + 0.5;
	vec3 ambient = mix(u_groundColour.rgb, u_skyColour.rgb, up) * u_sunColour.w;
	colour += ambient * diffuseColour * ao;

	// What the surface mirrors, out of the same sky.
	vec3 r = reflect(-v, n);
	vec3 env = skyPrefiltered(r, roughness) * u_sunColour.w;
	colour += env * envBRDFApprox(f0, roughness, ndotv) * ao;

	colour += texture2D(s_emissive, v_texcoord0).rgb;

	gl_FragColor = vec4(colour, 1.0);
}
