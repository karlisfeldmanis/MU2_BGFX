$input v_wpos, v_texcoord0, v_normal, v_colour, v_vnormal, v_vpos, v_light

// A card of grass, lit. MU's own painted tuft, cut out and stood on the land -- MU2's Turf,
// and MU's before it.
//
// The sheet is the art and is not argued with: it is MU's grass, painted, with its own
// lighting already in it. What this shader adds is the four aggregate tricks out of
// docs/grass.md -- Black Ops 4's list -- which are what make a scattered field read as a field
// rather than as a lot of identical stickers. Each of them is one or two instructions.
#include "grass.sh"

#include "shadow.sh"
#include "lights.sh"

void main()
{
	vec4 sheet = grassSheet(v_texcoord0);

	// The cutout, as a COVERAGE rather than a cut.
	//
	// A hard alpha test gives every painted blade a binary edge, and a binary edge on
	// something a few pixels wide, under 4x MSAA with no TAA to hold it still, crawls: half a
	// texel of camera movement flips whole runs of pixels between grass and ground. That
	// crawl was measured at 12.4 levels of per-pixel change a frame against the ground's 4.9,
	// and it is the whole of what a field of this shimmering looks like. docs/grass.md.
	//
	// So instead: a ramp about one PIXEL wide across the threshold. fwidth is how fast the
	// sheet's alpha changes over this pixel, so the ramp stays a pixel wide wherever the card
	// is and whatever mip it happens to be reading -- it tightens up close and widens as the
	// card minifies, which is exactly where the crawl lives. The number that comes out is
	// written to alpha and the hardware turns it into sample coverage
	// (BGFX_STATE_BLEND_ALPHA_TO_COVERAGE), so a card edge lands on 1, 2, 3 or 4 of the four
	// samples instead of all or none.
	//
	// This is why grass is NOT in the prepass. The prepass writes the view depth in its alpha
	// channel, and alpha-to-coverage would read that depth as a coverage mask. So the field
	// lays its own depth here instead, tested LESS and written -- see Renderer::draw, and the
	// note there on what that cost.
	float coverage = saturate((sheet.a - u_grassSheet.y) / max(fwidth(sheet.a), 1e-4) + 0.5);
	if (coverage <= 0.0) discard;

	float up = v_light.x;      // 0 at the root, 1 at the top edge
	float tint = v_light.y;

	// 1. The root-to-tip gradient, laid over the painted sheet rather than replacing it. MU
	//    painted one tuft and it is used everywhere; a field of one picture is a field that
	//    repeats, and the eye finds a repeat long before it finds a wrong green. So the sheet
	//    is carried towards a dark cool tint at the root and a pale warm one at the top.
	// The meadow is exempt from all of it. A daisy is white because it was painted white, a
	// buttercup yellow; carrying either towards a green root tint or a straw tip is painting
	// over the one thing the sheet was painted FOR. It takes the AO ramp and the world's light
	// and nothing else.
	vec3 albedo = sheet.rgb;
	if (u_grassSheet.w < 0.5)
	{
		// The root-to-tip gradient, as a GRADE and not a multiply.
		//
		// A multiply can only scale what the sheet already has, and Lorencia's grass is painted
		// (69, 64, 16): an olive with more red in it than green. No multiplier reaches a vivid
		// green from there -- scaling green by 1.5 and red by 0.6 gives a darker olive, which is
		// what the first pass of this did. Noria's sheet is (112, 123, 24) and is green to begin
		// with, which is why MU's own screens of Noria look nothing like MU's own Lorencia.
		//
		// So the sheet's VALUE is kept -- that is where the painted blades, their edges and
		// their shading live, and it is the whole reason to use MU's art at all -- and the hue
		// is taken from the two colours below. `grass_colour` says how far to go: 0 is the
		// sheet exactly as MU painted it, 1 is its light and shade wearing a new colour.
		float value = dot(albedo, vec3(0.299, 0.587, 0.114));
		vec3 target = mix(u_grassRoot.rgb, u_grassTip.rgb, up * up);
		albedo = mix(albedo, value * target, u_grassSize.w);
		// The dry tufts, which arrive in patches a few metres across off the coarse clump
		// field. Straw is not green turned down: it is warmer and far less green, so it is a
		// colour the tuft is carried towards rather than a saturation on this one.
		albedo = mix(albedo, value * vec3(1.42, 1.12, 0.44), v_colour.a);
		// And each card a little off its neighbour on top of all that.
		albedo *= 0.84 + tint * 0.32;
	}
	else
	{
		// A flower still varies, just not in hue: one stands a little brighter than the next.
		albedo *= 0.88 + tint * 0.24;
	}

	// 2. The height ramp of ambient occlusion. One multiply, and the single most effective
	//    depth cue there is on grass -- it is what makes a flat field look like it has a floor
	//    under it, and what sits a tuft INTO the turf instead of on top of it. The SSAO buffer
	//    is deliberately not read: it is half resolution and the cutout edges ARE the
	//    silhouettes at this camera, so what it has to say about them is noise.
	float ao = mix(u_grassRoot.w, 1.0, up);

	// Both faces are drawn, so the normal has to be turned to face whoever is looking. A card
	// is a surface with no inside.
	vec3 n = normalize(v_normal);
	vec3 v = normalize(u_camPos.xyz - v_wpos);
	if (dot(n, v) < 0.0) n = -n;

	vec3 l = normalize(u_sunDir.xyz);
	float ndotl = dot(n, l);

	// 3. Translucency, the sun-only way. Grass lit from behind glows: the light goes THROUGH
	//    it. There is no forward pass here to do that properly, and outdoors there is only one
	//    light bright enough for it to matter, so when the face turned to the eye is the face
	//    turned away from the sun the green is pushed towards its own saturated self and the
	//    wrap is allowed round the terminator. It is most of why lit grass reads as alive
	//    rather than as painted cardboard -- which, here, it literally is.
	float through = saturate(-ndotl);
	vec3 litAlbedo = mix(albedo, albedo * vec3(0.78, 1.28, 0.58), through * 0.75);
	float wrapped = max(ndotl, through * 0.85);

	vec3 diffuse = litAlbedo * v_colour.rgb;  // rgb alone: .a is the dryness, read above.

	vec3 colour = vec3_splat(0.0);
	// The sun. Grass casts nothing into the split -- a field of cutout cards would fill the
	// shadow map with noise, and MU's own grass has no shadow at all -- but it RECEIVES: a
	// lawn in a house's shade has to go dark with the ground it grows out of.
	//
	// ONE tap, not the ground's thirteen. See sunShadowHard: a penumbra across a painted blade
	// is a penumbra nobody can see, and the soft edge the eye reads is the one the turf under
	// the field still draws with the full filter. Measured at 0.41 ms. docs/grass.md.
	float shadow = sunShadowHard(v_wpos, n, saturate(ndotl));
	if (u_shadowDebug.x > 0.5)
	{
		gl_FragColor = vec4(vec3_splat(shadow), coverage);
		return;
	}
	colour += (diffuse / 3.14159265) * u_sunColour.rgb * u_sunDir.w * wrapped * shadow;

	// The sky and the turf it bounces off. The hemisphere is read off a normal pulled back
	// towards straight up, NOT off the card's own: a card is a vertical-ish quad, so its true
	// normal points at the horizon and takes barely half the sky the turf beside it takes.
	// That is a systematic darkening of the whole field against the ground it grows out of,
	// and a field darker than its own turf is the one thing that reads as wrong at a glance.
	//
	// It is also physically the better answer. What a tuft of grass actually sees of the sky is
	// not what one flat blade facing sideways sees; it is most of the hemisphere, because the
	// tuft is open. MU's own grass is lit by the terrain's light for exactly this reason.
	vec3 skyN = normalize(mix(n, vec3(0.0, 1.0, 0.0), 0.62));
	float hemi = skyN.y * 0.5 + 0.5;
	colour += mix(u_groundColour.rgb, u_skyColour.rgb, hemi) * u_sunColour.w * diffuse * ao;

	// 4. Roughness, never F0. A field seen from MU's 48 degrees is grazing nearly everywhere,
	//    and Fresnel on a low F0 would spread one highlight across the whole of it. So what
	//    sheen there is comes from roughening towards the root and letting the top keep a
	//    little, and the dielectric F0 is left alone entirely.
	float roughness = clamp(u_grassTip.w + (1.0 - up) * 0.35, 0.08, 1.0);
	colour += lampLight(v_wpos, n, v, diffuse, vec3_splat(0.0), roughness,
	                    saturate(dot(n, v)) + 1e-5, 0.0);

	gl_FragColor = vec4(dusty(colour, v_wpos), coverage);
}
