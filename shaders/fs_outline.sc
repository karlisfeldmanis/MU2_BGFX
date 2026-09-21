$input v_texcoord0

// The gold ring drawn round whatever the pointer is over. MU2's Outline.cs and its
// outline.gdshader, ported from a Godot SubViewport mask read by a CanvasItem shader to a
// bgfx offscreen silhouette read by a screen pass. See game/outline.cpp for how the mask is
// filled (the SAME instances the frame already drew, submitted a second time into their own
// tiny target) and how this view's own rect is fitted to the hovered thing's box on screen,
// so that what is sampled here is already the ring's own few thousand pixels and not the
// picture's two million.
//
// A silhouette rather than an outline, which is the whole design: the mask is a measured
// shape, not an inflated hull, so it closes round a figure worn in five pieces as one body
// and never tears at a hard crease.
#include <bgfx_shader.sh>

SAMPLER2D(s_mask, 0);

// rgb: the ring's colour, written straight to the backbuffer -- this composes AFTER the
// present pass's tonemap and sRGB write, so it is display colour and not linear radiance.
// a: the ring's own opacity.
uniform vec4 u_outlineEdge;
// x: the ring's width, in the MASK's own pixels. y: how much of that width is feathered.
// z: the drop shadow's strength, 0 for none -- a monster casts its own and needs none here.
uniform vec4 u_outlineParams;
// xy: one texel of the PHYSICAL mask texture, in its own 0..1 -- constant, since the
// texture is one fixed size regardless of how much of it a given box fills.
uniform vec4 u_outlinePixel;
// xy: the shadow's drift, in mask pixels, down and a little right, as the sun falls.
// z: how far its blur spreads, in mask pixels.
uniform vec4 u_outlineDrift;
// xy: how much of the physical mask texture this box actually filled -- the mask is one
// fixed square (Renderer::kOutlineMaskSize) so a box smaller than the cap, which is nearly
// every one, only wrote its own corner. v_texcoord0 runs 0..1 over the BOX; scaled by this
// it becomes 0..1 over the corner that was actually drawn into, which is what the texture
// sampler must be given. Left at 1 the box would be diluted almost to nothing -- a 200
// pixel box in a 512 texture sampled a strength-one texture read three quarters of it from
// the cleared margin outside the silhouette entirely, which is the bug this uniform fixes.
uniform vec4 u_outlineScale;

// Sixteen taps over each of six widening rings: fine enough that the distance found does not
// band at the feather's own width, and cheap enough that it runs only over the few thousand
// pixels the fitted view rect now is.
const int STEPS = 16;
const int RINGS = 6;

// One tap, held inside the corner of the texture this frame actually drew into.
//
// The clamp is not a nicety. The mask is one fixed square and only the box's own corner of
// it is cleared and redrawn each frame, so everything past that corner still holds the
// SILHOUETTE THE LAST FRAME LEFT THERE -- and the ring's search reaches a couple of texels
// out, far enough to find it. What comes back is a stroke drawn along the box's edge from a
// shape that is no longer there: the straight gold line that sat under a hovered monster,
// its own outline from the frame before. The sampler's own CLAMP is no help, since that
// holds at the edge of the 512, not at the edge of what was drawn.
//
// Clamping to the drawn corner reads the cleared margin instead, which is empty, and the box
// is fitted with a margin round the model precisely so that this margin exists.
float maskAt(vec2 uv, vec2 lo, vec2 hi)
{
	return texture2D(s_mask, clamp(uv, lo, hi)).r;
}

void main()
{
	vec2 at = v_texcoord0 * u_outlineScale.xy;
	// The corner of the texture this frame drew into, half a texel in from its own edge.
	vec2 lo = u_outlinePixel.xy * 0.5;
	vec2 hi = u_outlineScale.xy - u_outlinePixel.xy * 0.5;
	float here = maskAt(at, lo, hi);

	float width = u_outlineParams.x;
	float feather = u_outlineParams.y;
	float shade = u_outlineParams.z;

	// Inside the shape draws nothing: the ring sits outside the silhouette so the model is
	// never covered by its own highlight.
	float strength = 0.0;
	if (here <= 0.99)
	{
		float far = width + 1.0;
		for (int ring = 1; ring <= RINGS; ring++)
		{
			float span = width * float(ring) / float(RINGS);
			float found = 0.0;
			for (int i = 0; i < STEPS; i++)
			{
				float turn = float(i) * (6.2831853 / float(STEPS));
				vec2 along = vec2(cos(turn), sin(turn));
				found = max(found, maskAt(at + along * u_outlinePixel.xy * span, lo, hi));
			}
			if (found > 0.5)
			{
				far = span;
				break;
			}
		}
		// Solid to within a feather of the full width, then out.
		strength = 1.0 - smoothstep(width - feather, width, far);
	}

	// The shadow under a dropped item: the same mask, shifted, blurred, painted black. Two
	// rings of taps rather than a separable pass, since what is being softened is a couple of
	// pixels on a shape a few hundred across.
	float dark = 0.0;
	if (shade > 0.0)
	{
		vec2 from = at - u_outlineDrift.xy * u_outlinePixel.xy;
		float sum = maskAt(from, lo, hi);
		for (int i = 0; i < STEPS; i++)
		{
			float turn = float(i) * (6.2831853 / float(STEPS));
			vec2 along = vec2(cos(turn), sin(turn));
			sum += maskAt(from + along * u_outlinePixel.xy * u_outlineDrift.z * 0.35, lo, hi);
			sum += maskAt(from + along * u_outlinePixel.xy * u_outlineDrift.z, lo, hi);
		}
		dark = sum / float(1 + STEPS * 2) * shade;
	}

	if (strength <= 0.0 && dark <= 0.0) discard;

	// Faded where the model itself is, so neither the stroke nor the shadow overlaps the
	// thing they belong to. Composited here rather than left to two draws: straight alpha
	// out, the shadow contributing no colour of its own.
	float ring = strength * (1.0 - here) * u_outlineEdge.a;
	float under = dark * (1.0 - here);
	float alpha = ring + under * (1.0 - ring);
	if (alpha <= 0.0) discard;

	gl_FragColor = vec4(u_outlineEdge.rgb * (ring / alpha), alpha);
}
