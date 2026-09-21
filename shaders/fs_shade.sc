$input v_wpos, v_texcoord0, v_normal, v_tangent, v_vnormal, v_vpos, v_light

// The one lit pass. Depth is tested EQUAL against what the prepass laid down and nothing is
// written back, so no pixel here is shaded twice.
#include "common.sh"

#include "shadow.sh"
#include "lights.sh"

uniform vec4 u_translucency;  // x: the fraction of a leaf's light that comes through it, or 0

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
	vec3 albedo = albedoTex.rgb * v_light.rgb;

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

	// glTF's own rule: the factors MULTIPLY the map, they do not stand in for it. Every
	// material here that has an ORM states both factors as 1.0, so this costs those nothing;
	// the 195 that have no ORM at all bind a texture of ones and carry the real number in
	// the factor, which is how MU's foliage, its grass and its water say what they are.
	// Read the map alone and all 195 shade at roughness 1.
	vec3 orm = texture2D(s_orm, v_texcoord0).rgb;
	float ao = orm.r;
	float roughness = clamp(orm.g * u_material.z, 0.04, 1.0);
	float metal = orm.b * u_material.w;

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

	// The probe's view: the sun's visibility and nothing else, so that a pixel which changes
	// between two frames changed because the shadow did. docs/shadow-probe.md.
	if (u_shadowDebug.x > 0.5)
	{
		gl_FragColor = vec4_splat(sunShadow(v_wpos, ng, saturate(dot(ng, l)), pixel));
		return;
	}

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

	// The lamps, on the texture's own albedo rather than on the albedo times MU's baked
	// light: lights.sh says why.
	colour += lampLight(v_wpos, n, v, albedoTex.rgb * (1.0 - metal), f0, roughness, ndotv, 1.0);

	// The emissive, or on foliage the light through it. MU2's pipeline writes a leaf's own
	// sheet as its emissive at a fraction, standing in for transmission, and that fraction
	// is of the light there is: the sun as it falls on flat ground and the sky's mean, times
	// MU's baked light where the plant stands. Added at 1.0 as a constant, every blade and
	// flower in Lorencia shone its own colour at night. content::Material says more.
	vec3 emissive = texture2D(s_emissive, v_texcoord0).rgb;
	if (u_translucency.x > 0.0)
	{
		vec3 sky = u_sunColour.rgb * (u_sunDir.w * max(l.y, 0.0) / 3.14159265)
		         + mix(u_groundColour.rgb, u_skyColour.rgb, 0.5) * u_sunColour.w;
		emissive *= v_light.rgb * sky * u_translucency.x;
	}
	colour += emissive;

	gl_FragColor = vec4(colour, 1.0);
}
