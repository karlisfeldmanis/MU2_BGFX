$input v_wpos, v_texcoord0, v_normal, v_tangent, v_vnormal, v_vpos, v_light, v_refine

// The one lit pass. Depth is tested EQUAL against what the prepass laid down and nothing is
// written back, so no pixel here is shaded twice.
// Stage 6 is the prepass's, which this pass never reads. abyss() in common.sh.
#define MU2_ABYSS 6
#include "common.sh"

#include "shadow.sh"
#include "lights.sh"
#include "shine.sh"

uniform vec4 u_translucency;  // x: the fraction of a leaf's light that comes through it, or 0

// The reflection probe, sprint 8c. x: 1 when a prefiltered cube is bound, 0 inside the probe's
// own faces and wherever there is none, which take the closed-form sky  y: its last mip
// z: above 0 draws the probe itself at mip z - 1, looked up towards each pixel from where
// it was taken  w: the sheet's metal_gain, which lifts a metal's painted reflectance
uniform vec4 u_probe;
uniform vec4 u_probePos;  // xyz: where the cube was taken, world metres
SAMPLERCUBE(s_probe, 15);

void main()
{
	vec4 albedoTex = texture2D(s_albedo, v_texcoord0);
	if (u_material.x >= 0.0)
	{
		if (albedoTex.a < u_material.x) discard;
	}
	// How much of this figure is there. It leaves the pass through the alpha, which is 1 for
	// everything else in the frame and is what the renderer blends the fading figure by.
	float fade = figureFade(v_light.w);
	// Times MU's own baked light at the tile this instance stands on. The land carries the
	// same thing per vertex in its COLOR_0, so without this the town stands brighter than
	// the ground it stands on, and MU's painted dusk stops at the foot of every wall.
	vec3 albedo = albedoTex.rgb * v_light.rgb;

	vec3 v = normalize(u_camPos.xyz - v_wpos);

	// The geometric normal, turned to face the eye rather than read off gl_FrontFacing,
	// which on Metal has the opposite sense from the winding CULL_CW keeps. MU's figures
	// are single sheets of mixed winding and are drawn two-sided, so this matters on them.
	vec3 ng = normalize(v_normal);
	// y is two flags: 1 two-sided, 2 calibrated. See the renderer.
	// y: 1 two-sided, 2 calibrated, 4 soft alpha (content::Material::softAlpha).
	float soft = step(3.5, u_material.y);
	float flagsLow = u_material.y - 4.0 * soft;
	float calibrated = step(1.5, flagsLow);
	float twoSided = flagsLow - 2.0 * calibrated;
	// Which side is facing the eye is decided by the TRIANGLE, not by the smoothed normal. 62 of
	// Lorencia's 74 placed models carry their normals inverted (MU's winding, through the
	// export) and smoothed across their edges, so on a flat roof they lean 20 to 50 degrees
	// off the face; tested per pixel against the view, part of a triangle turned and part did
	// not, and the part that did not was shaded as an underside -- no sun, the ground's
	// bounce only. Crisp black wedges along the triangles that changed shape as the camera
	// moved: the "shadow acne" on House03's roof (2026-09-24). The face's own normal from the
	// screen derivatives is the same for every pixel of it.
	if (twoSided > 0.5)
	{
		vec3 face = normalize(cross(dFdx(v_wpos), dFdy(v_wpos)));
		if (dot(face, v) < 0.0) face = -face;
		// And where the smoothed normal lies nearly flat against the face, the face's own: a
		// sign taken there lands either way from one 2x2 block to the next, which was a field
		// of black dots on the straw bales' flared ends as the hero walked past (2026-09-24).
		float along = dot(ng, face);
		if (along < 0.0) ng = -ng;
		ng = normalize(mix(face, ng, smoothstep(0.1, 0.35, abs(along))));
	}

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

	// A metal's reflectance is its albedo, and MU's painted metal is dark: the plate and the
	// shields sit near 0.05 linear, where iron is 0.55, because the paint carries MU's own
	// shading. Read straight, armour returns a twentieth of the town round it and reads as
	// grey card. The sheet's metal_gain lifts the paint towards what the metal would reflect,
	// keeping its engraving; invention, judged by eye. docs/sprints/08c-the-metal.md.
	//
	// The gain stops where the brightest channel reaches one, rather than each channel clipping
	// on its own. Clipped per channel, pale gold paint put its red and green both at one and
	// came out white: the treasure chest's brass bands read as chalk, and the shield 8c saw
	// white out in the low sun was the same clip. Dark iron never reaches the cap and is as
	// it was.
	//
	// Only on paint that is paint. MU2's item bake has already lifted an item's metal onto its
	// reflectance (its `_basecolor` sheet, flagged calibrated), and the gain on top of that
	// lifted the armour twice.
	float peak = max(max(albedo.r, albedo.g), max(albedo.b, 1e-4));
	float gain = mix(u_probe.w, 1.0, calibrated);
	vec3 f0 = mix(vec3_splat(0.04), albedo * min(gain, 1.0 / peak), metal);
	vec3 diffuseColour = albedo * (1.0 - metal);

	vec3 l = normalize(u_sunDir.xyz);
	float ndotl = saturate(dot(n, l));
	float ndotv = saturate(dot(n, v)) + 1e-5;

	vec3 colour = vec3_splat(0.0);

	// The probe's check: every pixel shows what the cube holds in its direction from where the
	// cube was taken. Near the player that is the frame itself, give or take parallax; a face
	// turned or mirrored shows as the town in the wrong place.
	if (u_probe.z > 0.5)
	{
		vec3 held = textureCubeLod(s_probe, normalize(v_wpos - u_probePos.xyz), u_probe.z - 1.0).rgb;
		// A texel that is not a number shows magenta rather than black, which is what it
		// would otherwise pass for.
		if (any(isnan(held))) held = vec3(1.0, 0.0, 1.0);
		gl_FragColor = vec4(held, 1.0);
		return;
	}

	// The probe's view: the sun's visibility and nothing else, so that a pixel which changes
	// between two frames changed because the shadow did. docs/shadow-probe.md.
	if (u_shadowDebug.x > 0.5)
	{
		gl_FragColor = vec4_splat(sunShadow(v_wpos, ng, saturate(dot(ng, l)), pixel));
		return;
	}

	// The sun. Its shadow is kept for the lamps too where the sheet asks (lamp_shadow).
	float sunLit = 1.0;
	if (ndotl > 0.0)
	{
		float shadow = sunShadow(v_wpos, ng, saturate(dot(ng, l)), pixel);
		sunLit = shadow;
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
	//
	// From the probe where there is one: the town round the player, lit as it is this frame,
	// its mips GGX lobes of rising roughness, so plate reflects the street it stands in, the
	// sky over it and the fire beside it. It holds radiance already, sun and lamps included,
	// so nothing multiplies it but the occlusion.
	vec3 r = reflect(-v, n);
	vec3 env;
	if (u_probe.x > 0.5)
	{
		env = textureCubeLod(s_probe, r, roughness * u_probe.y).rgb;
	}
	else
	{
		env = skyPrefiltered(r, roughness) * u_sunColour.w;
	}
	// Specular occlusion from the AO (Lagarde and de Rousiers, "Moving Frostbite to PBR"):
	// a crevice that hides the sky from the diffuse hides most of it from a rough reflection
	// and less of it from a sharp one seen head on. And the horizon: a normal map can bend r
	// under the surface it belongs to, where it would reflect the inside of the mesh.
	float specAo = saturate(pow(ndotv + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
	float horizon = saturate(1.0 + 1.1 * dot(r, ng));
	colour += env * envBRDFApprox(f0, roughness, ndotv) * specAo * horizon * horizon;

	// The lamps, on the texture's own albedo rather than on the albedo times MU's baked
	// light: lights.sh says why.
	// On refined steel the lamps' share is coloured by the chrome first: shine.sh's shineLamps.
	float plus = shinePlus(v_refine.x);
	vec3 lamps = lampLight(v_wpos, n, v, albedoTex.rgb * (1.0 - metal), f0, roughness, ndotv, 1.0)
	           * mix(1.0, sunLit, u_lampParams.w);
	colour += shineLamps(plus, v_refine.yzw, lamps);

	// The refinement ladder: MuMain tints the light an item is drawn in, so the tint takes
	// everything lit above, and adds its chrome unlit, so that goes on after. shine.sh.
	colour *= shineTint(plus);
	colour += shineAdded(plus, normalize(v_normal), v_refine.yzw);
	colour += shineExcellentAdded(shineExcellent(v_refine.x), normalize(v_normal));
	colour += shineSweepAdded(plus, shineSweep(v_refine.x), normalize(v_normal));
	colour += shineDarknessAdded(shineDarkness(v_refine.x), normalize(v_normal));
	colour += shineCrowAdded(shineCrow(v_refine.x), normalize(v_normal), albedoTex.rgb);

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

	// A self-lit figure: MU's Level 3, whose body light Selection.cpp:106 raises by 1.5 over
	// the ground's, so fixed-function clamps it to the sheet's own colour in any light -- the
	// Ice Queen. Never darker than her sheet; the sun still adds its highlights by day.
	// Off the albedo the instance's light has already scaled, so a self-lit part can be drawn
	// below its sheet: the Wings of Heaven's white, toned down (game/wings.cpp).
	colour = mix(colour, max(colour, albedo * 0.9), selfLit(v_light.w));
	// A soft-alpha membrane -- the Valkyrie's wings, the Bahamut's and the Vepar's fins -- is lit
	// as MU lights a model: its sheet times the land's light at its tile (BodyLight, glColor
	// over an unshaded texture), so the painted blue and its rays come through rather than the
	// shading's dark side. Never darker than that, at 0.85; the sun and the lamps still add
	// their highlights. Ours, after the user, 2026-10-04: the wings 'too dark / flat blue'.
	colour = mix(colour, max(colour, albedo * 0.85), soft);

	// A soft-alpha part is blended by its sheet's alpha as well as by the fade: MU's alpha test
	// and blend (Renderer::draw draws it after the opaque frame; elsewhere this alpha is unread).
	gl_FragColor = vec4(abyss(dusty(colour, v_wpos), v_wpos), fade * mix(1.0, albedoTex.a, soft));
}
