// The body of fs_shade, shared with fs_shade_merged (gfx/scenery.h), which defines
// MU2_MATERIAL_ARRAYS and reads its material by layer: see common.sh.
// The one lit pass. Depth is tested EQUAL against what the prepass laid down and nothing is
// written back, so no pixel here is shaded twice.
// Stage 6 is the prepass's, which this pass never reads. abyss() in common.sh.
#define MU2_ABYSS 6
// Stage 8, s_colour's elsewhere and never read here: the lava's light (lights.sh).
#define MU2_LAVA_LIGHT 8
#include "common.sh"

#include "shadow.sh"
#include "lights.sh"
#include "shine.sh"

uniform vec4 u_translucency;  // x: the fraction of a leaf's light that comes through it, or 0
uniform vec4 u_weather;       // x: the sheet's weathering, 0 off. See below
// The weathering's noise, made by Renderer::init: r blotches a metre or two across and a
// another of them, g half a metre, b a few centimetres; repeating every 8 m of world.
SAMPLER2D(s_weather, 12);

// The reflection probe, sprint 8c. x: 1 when a prefiltered cube is bound, 0 inside the probe's
// own faces and wherever there is none, which take the closed-form sky  y: its last mip
// z: above 0 draws the probe itself at mip z - 1, looked up towards each pixel from where
// it was taken  w: the sheet's metal_gain, which lifts a metal's painted reflectance
uniform vec4 u_probe;
uniform vec4 u_probePos;  // xyz: where the cube was taken, world metres  w: probe_diffuse
SAMPLERCUBE(s_probe, 15);

void main()
{
	vec4 albedoTex = materialSample(s_albedo, v_texcoord0);
	if (MATERIAL.x >= 0.0)
	{
		if (albedoTex.a < MATERIAL.x) discard;
	}
	// How much of this figure is there. It leaves the pass through the alpha, which is 1 for
	// everything else in the frame and is what the renderer blends the fading figure by.
	float fade = figureFade(v_light.w);
	// Times MU's own baked light at the tile this instance stands on. The land carries the
	// same thing per vertex in its COLOR_0, so without this the town stands brighter than
	// the ground it stands on, and MU's painted dusk stops at the foot of every wall.
	vec3 paint = albedoTex.rgb;
	vec3 albedo = paint * v_light.rgb;

	vec3 v = normalize(u_camPos.xyz - v_wpos);

	// The geometric normal, turned to face the eye rather than read off gl_FrontFacing,
	// which on Metal has the opposite sense from the winding CULL_CW keeps. MU's figures
	// are single sheets of mixed winding and are drawn two-sided, so this matters on them.
	vec3 ng = normalize(v_normal);
	// y is two flags: 1 two-sided, 2 calibrated. See the renderer.
	// y: 1 two-sided, 2 calibrated, 4 soft alpha (content::Material::softAlpha).
	// And a fraction, a sixteenth a weathering kind (content::Material::weathering), which the
	// whole-number flags are read without.
	float flags = floor(MATERIAL.y + 0.01);
	float weathered = floor((MATERIAL.y - flags) * 16.0 + 0.5);
	float soft = step(3.5, flags);
	float flagsLow = flags - 4.0 * soft;
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

	// Weathering, wood and stone only (ours, 2026-10-11). At the game's 9.5 m a pixel is about
	// 9 mm and MU's upscaled sheets are already finer than that, so what reads as a picture of
	// timber rather than timber is not the grain but its sameness: one sheet repeating every
	// metre or so, the top of a block wearing what its sides wear. So this works at that scale.
	// Broad patches over the world, lighter and darker, rougher and smoother; wood's upward
	// faces bleached towards silver grey, stone's carrying the world's dust (u_dust, the sheet's
	// own colour); rain streaks down stone and plaster walls. Which slots weather, and how, is
	// `weathered` above: 1 wood, 2 stone, 3 plaster, from the recipe through the cook; items
	// and figures are 0 and untouched. World space, projected along the face's main axis.
	float roughShift = 0.0;
	float matte = 0.0;
	if (weathered > 0.5 && u_weather.x > 0.0)
	{
		// Up to twice the sheet's 1, for looking; past that the blends below run out of range.
		float w = min(u_weather.x, 2.0);
		vec3 an = abs(ng);
		vec2 across = an.x > an.z ? v_wpos.zy : v_wpos.xy;
		vec2 plane = an.y > max(an.x, an.z) ? v_wpos.xz : across;
		vec4 wear = texture2D(s_weather, plane * 0.125);
		float blotch = wear.r * 0.65 + wear.g * 0.35;
		float grey = dot(paint, vec3(0.299, 0.587, 0.114));
		paint *= 1.0 + (blotch - 0.5) * 0.3 * w;
		roughShift += (blotch - 0.5) * 0.12 * w;
		float facingUp = smoothstep(0.55, 0.9, ng.y);
		if (weathered < 1.5)
		{
			// Sun and rain take a board's colour off the top first, towards a silver grey a
			// shade paler than the paint.
			float bleach = min(facingUp * smoothstep(0.3, 0.75, wear.a) * 0.4 * w, 0.6);
			paint = mix(paint, vec3(0.95, 0.97, 1.0) * (grey * 1.1), bleach);
			roughShift += 0.1 * bleach;
		}
		else
		{
			// The world's dust in its hue at the paint's own brightness, a touch lifted: a pale
			// target washed MU's painted steps out to chalk (2026-10-11, 147,114 at noon).
			float dust = min(facingUp * smoothstep(0.35, 0.85, wear.a) * 0.3 * w, 0.4);
			vec3 dustHue = u_dust.rgb / max(dot(u_dust.rgb, vec3(0.299, 0.587, 0.114)), 1e-3);
			paint = mix(paint, dustHue * (grey * 1.08), dust);
			roughShift += 0.12 * dust;
			// Streaks: the half-metre noise stretched 12 times down the wall, so a streak is a
			// hand wide and a metre or two long, on faces that stand.
			float standing = 1.0 - smoothstep(0.3, 0.6, an.y);
			float rain = texture2D(s_weather, vec2(across.x * 0.5, across.y * 0.04)).g;
			float streak = smoothstep(0.5, 0.8, rain);
			paint *= 1.0 - standing * streak * (weathered > 2.5 ? 0.22 : 0.14) * w;
		}
		matte = min(w, 1.0);
	}
	albedo = paint * v_light.rgb;

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
	vec2 nxy = materialSample(s_normal, v_texcoord0).xy * 2.0 - 1.0;
	float nz = sqrt(max(0.0, 1.0 - dot(nxy, nxy)));
	vec3 n = normalize(t * nxy.x + b * nxy.y + ng * nz);

	// glTF's own rule: the factors MULTIPLY the map, they do not stand in for it. Every
	// material here that has an ORM states both factors as 1.0, so this costs those nothing;
	// the 195 that have no ORM at all bind a texture of ones and carry the real number in
	// the factor, which is how MU's foliage, its grass and its water say what they are.
	// Read the map alone and all 195 shade at roughness 1.
	vec3 orm = materialSample(s_orm, v_texcoord0).rgb;
	float ao = orm.r;
	float roughness = clamp(orm.g * MATERIAL.z + roughShift, 0.04, 1.0);
	float metal = orm.b * MATERIAL.w;

	// Specular anti-aliasing (Tokuyoshi and Kaplanyan, "Improved Geometric Specular
	// Antialiasing", 2019): where the normal turns faster than a pixel can hold -- a normal
	// map's engraving seen from across the square, a railing's round bar -- the highlight lands
	// on one pixel in a frame and its neighbour in the next, and MSAA, which shades once per
	// pixel, does nothing for it. The normal's spread under the pixel widens the lobe instead,
	// in alpha squared, capped so a flat surface keeps the sharpness it was given.
	vec3 dndx = dFdx(n);
	vec3 dndy = dFdy(n);
	float spread = min(0.5 * (dot(dndx, dndx) + dot(dndy, dndy)), 0.18);
	float a2 = roughness * roughness;
	a2 *= a2;
	roughness = max(roughness, sqrt(sqrt(saturate(a2 + spread))));

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
	// A non-metal's 0.04, times the sheet's stone_sheen (u_edge.w, one less it): the Lost
	// Tower's stone as matte as its ground. Metal keeps its own.
	vec3 f0 = mix(vec3_splat(0.04 * (1.0 - u_edge.w)), albedo * min(gain, 1.0 / peak), metal);
	vec3 diffuseColour = albedo * (1.0 - metal);

	vec3 l = normalize(u_sunDir.xyz);
	float ndotl = saturate(dot(n, l));
	float ndotv = saturate(dot(n, v)) + 1e-5;

	// Multiple scattering (Fdez-Aguera, "A Multiple-Scattering Microfacet Model for Real-Time
	// Image-Based Lighting", 2019, in Filament's form): one bounce of GGX loses the light that
	// strikes a second microfacet, and a rough surface loses a great deal of it -- a white metal
	// at roughness 0.7 returns about two thirds. That loss is what made rough plate read as grey
	// card. The split sum's white response says how much went missing, and every specular term
	// below is scaled up by it in proportion to f0, so a dielectric's 0.04 is all but untouched.
	vec2 envAB = envBRDFApproxAB(roughness, ndotv);
	vec3 energy = vec3_splat(1.0) + f0 * (1.0 / max(envAB.x + envAB.y, 1e-3) - 1.0);

	// Micro-shadowing (Chan, "Material Advances in Call of Duty: WWII", 2018): the baked AO
	// is a cone of open sky, and a light outside that cone is hidden too. Without it the AO
	// darkened only the ambient, and a crease of engraving in full sun shone as bright as the
	// plate round it. Off the baked AO alone: the SSAO's halos would print on lit ground.
	float microShadow = saturate(abs(dot(n, l)) + 2.0 * orm.r * orm.r - 1.0);

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
		vec3 spec = f * d * g / max(4.0 * ndotv * ndotl, 1e-5) * energy;
		vec3 kd = (vec3_splat(1.0) - f) * diffuseColour / 3.14159265;
		// A weathered surface takes Burley's diffuse ("Physically Based Shading at Disney",
		// 2012) for Lambert's: a rough face is brighter with the sun behind the eye and
		// darker at a graze, which is what reads as chalky stone and dry boards rather than
		// as painted card.
		if (matte > 0.0)
		{
			float fd90 = 0.5 + 2.0 * roughness * vdoth * vdoth;
			float fl = pow(1.0 - ndotl, 5.0);
			float fv = pow(1.0 - min(ndotv, 1.0), 5.0);
			kd *= mix(1.0, (1.0 + (fd90 - 1.0) * fl) * (1.0 + (fd90 - 1.0) * fv), matte);
		}
		colour += (kd + spec) * u_sunColour.rgb * u_sunDir.w * ndotl * shadow * microShadow;
	}

	// The hemisphere ambient: sky above, bounced turf below, occluded by the AO.
	float up = n.y * 0.5 + 0.5;
	vec3 ambient = mix(u_groundColour.rgb, u_skyColour.rgb, up) * u_sunColour.w;
	// And near the player, the probe's roughest mip in its place: a GGX lobe of roughness one,
	// cosine-weighted, which is near enough the light a matte face gathers from the half of
	// the world in front of it. Its sky is the same two colours times the same strength, so
	// what changes is what the hemisphere could not know: the sunlit street under a wall in
	// shade, the lamp's pool by a door, the warm stone round a fire. The cube is taken at
	// the player's chest, so a face far from him would gather his surroundings rather than
	// its own: full within 12 m, the hemisphere alone by 30.
	if (u_probe.x > 0.5)
	{
		vec3 gathered = textureCubeLod(s_probe, n, u_probe.y).rgb;
		float near = 1.0 - smoothstep(12.0, 30.0, distance(v_wpos, u_probePos.xyz));
		ambient = mix(ambient, gathered, u_probePos.w * near);
	}
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
	colour += env * (f0 * envAB.x + vec3_splat(envAB.y)) * energy * specAo * horizon * horizon;

	// The lamps, on the texture's own albedo rather than on the albedo times MU's baked
	// light: lights.sh says why.
	// On refined steel the lamps' share is coloured by the chrome first: shine.sh's shineLamps.
	float plus = shinePlus(v_refine.x);
	// The lamps take the energy as a scalar on their Fresnel: their specular argument.
	vec3 lamps = lampLight(v_wpos, n, v, paint * (1.0 - metal), f0, roughness, ndotv,
	                       dot(energy, vec3_splat(1.0 / 3.0)))
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
#ifndef MU2_MATERIAL_ARRAYS
	vec3 emissive = texture2D(s_emissive, v_texcoord0).rgb;
	if (u_translucency.x > 0.0)
	{
		vec3 sky = u_sunColour.rgb * (u_sunDir.w * max(l.y, 0.0) / 3.14159265)
		         + mix(u_groundColour.rgb, u_skyColour.rgb, 0.5) * u_sunColour.w;
		emissive *= v_light.rgb * sky * u_translucency.x;
	}
	colour += emissive;
#endif

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
