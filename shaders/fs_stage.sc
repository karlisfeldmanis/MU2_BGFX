$input v_wpos, v_texcoord0, v_normal, v_tangent, v_vnormal, v_vpos, v_light

// The item pictures: a model photographed flat for a window's cell. MU2's Panel.Stage, which
// is a little world of its own -- its own key light, its own fill and its own room for the
// metals to reflect -- so an item in the bag is not lit by Lorencia's sun, shadowed by its
// split or darkened by its screen-space AO, none of which is where the item is.
//
// Written after the tonemap: the interface draws the picture as bytes, so this writes sRGB.
// MU2's stage ran Godot's linear tonemap, which is a clamp, and this does the same.
#include "common.sh"

// Panel.Stage's DirectionalLight3D, Rotation (-0.7, -0.6, 0) in Godot's YXZ order, turned
// into the direction towards it: up, to the left and toward the viewer.
#define KEY_DIR vec3(-0.432, 0.644, 0.631)
// Panel.KeyEnergy, 0.7: "what is left of the key once the sky carries the metals".
#define KEY_ENERGY 0.7
// The fill: AmbientLightColor (0.65, 0.65, 0.7) at AmbientLightEnergy 1.7. A diffuse term
// only, so it lifts a book and leaves a plate to its reflections.
#define FILL vec3(1.105, 1.105, 1.19)

// Panel.Studio: a softbox over a table. Brightest overhead, dark underneath, near-neutral
// and a little warm, so armour reads curved and takes no blue cast from a daylight sky.
vec3 studio(vec3 dir, float roughness)
{
	float y = clamp(dir.y, -1.0, 1.0);
	// Godot's ProceduralSkyMaterial curve, 0.15 on both halves.
	float t = 1.0 - pow(1.0 - abs(y), 1.0 / 0.15);
	vec3 above = mix(vec3(0.55, 0.53, 0.50), vec3(0.78, 0.78, 0.80), saturate(t));
	vec3 below = mix(vec3(0.34, 0.32, 0.30), vec3(0.12, 0.11, 0.10), saturate(t));
	vec3 seen = y >= 0.0 ? above : below;
	// A rough surface reflects the room's average rather than its gradient. The room's mean,
	// by eye off the four colours above.
	return mix(seen, vec3(0.45, 0.44, 0.43), roughness * roughness);
}

vec3 toSrgb(vec3 c)
{
	c = saturate(c);
	vec3 low = c * 12.92;
	vec3 high = 1.055 * pow(c, vec3_splat(1.0 / 2.4)) - 0.055;
	return mix(low, high, step(vec3_splat(0.0031308), c));
}

void main()
{
	vec4 albedoTex = texture2D(s_albedo, v_texcoord0);
	if (u_material.x >= 0.0)
	{
		if (albedoTex.a < u_material.x) discard;
	}
	vec3 albedo = albedoTex.rgb;

	// The camera is orthographic and looks down -z, so every pixel looks the same way.
	vec3 v = vec3(0.0, 0.0, 1.0);
	vec3 ng = normalize(v_normal);
	float calibrated = step(1.5, u_material.y);
	float twoSided = u_material.y - 2.0 * calibrated;
	if (twoSided > 0.5 && dot(ng, v) < 0.0) ng = -ng;

	vec3 t = normalize(v_tangent.xyz - ng * dot(ng, v_tangent.xyz));
	vec3 b = cross(ng, t) * v_tangent.w;
	// BC5: z is rebuilt, as fs_shade rebuilds it.
	vec2 nxy = texture2D(s_normal, v_texcoord0).xy * 2.0 - 1.0;
	float nz = sqrt(max(0.0, 1.0 - dot(nxy, nxy)));
	vec3 n = normalize(t * nxy.x + b * nxy.y + ng * nz);

	vec3 orm = texture2D(s_orm, v_texcoord0).rgb;
	float ao = orm.r;
	float roughness = clamp(orm.g * u_material.z, 0.04, 1.0);
	float metal = orm.b * u_material.w;

	vec3 f0 = mix(vec3_splat(0.04), albedo, metal);
	vec3 diffuseColour = albedo * (1.0 - metal);

	vec3 l = normalize(KEY_DIR);
	float ndotl = saturate(dot(n, l));
	float ndotv = saturate(dot(n, v)) + 1e-5;

	vec3 colour = FILL * diffuseColour * ao;
	if (ndotl > 0.0)
	{
		vec3 h = normalize(l + v);
		float ndoth = saturate(dot(n, h));
		float vdoth = saturate(dot(v, h));
		vec3 f = fresnelSchlick(f0, vdoth);
		float d = distributionGGX(ndoth, roughness);
		float g = geometrySmith(ndotv, ndotl, roughness);
		vec3 spec = f * d * g / max(4.0 * ndotv * ndotl, 1e-5);
		vec3 kd = (vec3_splat(1.0) - f) * diffuseColour;
		// Godot's energy is irradiance on a face turned to the light: a diffuse face gets its
		// albedo times the energy, and the specular lobe is scaled to match.
		colour += (kd + spec * 3.14159265) * KEY_ENERGY * ndotl;
	}

	vec3 r = reflect(-v, n);
	colour += studio(r, roughness) * envBRDFApprox(f0, roughness, ndotv) * ao;
	colour += texture2D(s_emissive, v_texcoord0).rgb;

	gl_FragColor = vec4(toSrgb(colour), 1.0);
}
