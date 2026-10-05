// The sun's shadow, read the same way by the town (fs_shade) and the land (fs_ground). It was
// two copies once, and two copies of a filter drift: one of them had a comment the other had
// lost, and a fix to one is a fix to half the picture.
//
// Needs common.sh first, for s_shadowCompare, s_shadowDepth and gradientNoise.

uniform mat4 u_shadowMtx;
uniform vec4 u_shadowParams;  // x: depth bias  y: penumbra scale  z: map texel  w: normal bias
// x: 1 draws the sun's visibility alone, as grey -- the probe's view, docs/shadow-probe.md
// y: where the disc's turn is anchored: 0 the screen, 1 the world, 2 nowhere (no turn)
// zw: the map's corner on the world's texel grid, which is what `1` anchors to
uniform vec4 u_shadowDebug;
// x: how far the blocker search reaches, y: the widest penumbra -- both in the map's uv, from
// metres on the CPU, so that a finer map draws the same shadow sharper and not a smaller one
uniform vec4 u_shadowReach;

// The Vogel disc, written out: tap i of n sits at sqrt((i + 0.5) / n) out and i golden angles
// round, for the four counts the filter takes. Computed per tap it was a sqrt, a cos and a sin
// for each of 25 taps on every lit pixel, and the count rides a uniform, so the compiler could
// fold none of it (docs/perf-audit-2k.md, A1). A turned disc is this one rotated by its phase,
// which is one cos and one sin a pixel. `static`, since a global const in the HLSL that bgfx's
// Metal path parses is a uniform, and braces, since that parser takes no array constructor.
static const vec2 kVogel5[5] = {
	vec2(0.3162278, 0.0000000),
	vec2(-0.4038736, 0.3699813),
	vec2(0.0618193, -0.7043993),
	vec2(0.5090565, 0.6639740),
	vec2(-0.9341812, -0.1652435)
};
static const vec2 kVogel8[8] = {
	vec2(0.2500000, 0.0000000),
	vec2(-0.3192901, 0.2924959),
	vec2(0.0488725, -0.5568765),
	vec2(0.4024445, 0.5249176),
	vec2(-0.7385351, -0.1306365),
	vec2(0.6996049, -0.4450314),
	vec2(-0.2340042, 0.8704838),
	vec2(-0.4462713, -0.8592682)
};
static const vec2 kVogel9[9] = {
	vec2(0.2357023, 0.0000000),
	vec2(-0.3010296, 0.2757678),
	vec2(0.0460774, -0.5250282),
	vec2(0.3794283, 0.4948970),
	vec2(-0.6962976, -0.1231652),
	vec2(0.6595939, -0.4195796),
	vec2(-0.2206212, 0.8207000),
	vec2(-0.4207486, -0.8101259),
	vec2(0.9128562, 0.3333736)
};
static const vec2 kVogel16[16] = {
	vec2(0.1767767, 0.0000000),
	vec2(-0.2257722, 0.2068258),
	vec2(0.0345581, -0.3937712),
	vec2(0.2845712, 0.3711728),
	vec2(-0.5222232, -0.0923739),
	vec2(0.4946954, -0.3146847),
	vec2(-0.1654659, 0.6155250),
	vec2(-0.3155615, -0.6075944),
	vec2(0.6846422, 0.2500302),
	vec2(-0.7122561, 0.2940090),
	vec2(0.3433545, -0.7337286),
	vec2(0.2537302, 0.8089320),
	vec2(-0.7647459, -0.4431859),
	vec2(0.8971340, -0.1972324),
	vec2(-0.5475069, 0.7787722),
	vec2(-0.1264868, -0.9760897)
};

vec2 turnTap(vec2 tap, vec2 turn)
{
	return vec2(tap.x * turn.x - tap.y * turn.y, tap.x * turn.y + tap.y * turn.x);
}

// The disc's turn, which spreads few taps into a penumbra that looks smooth in a still: it
// trades banding for grain. Turned per SCREEN pixel the grain is steady while the camera
// holds and fizzes while it moves, because the soft edge slides under a pattern that does
// not. Anchoring it to the world's texel grid did not help: the turn runs through three
// cycles a texel, finer than a pixel, so the pixel grid aliases it just the same.
// tools/pan.py measured both at twice the resampling floor on the 60 m split and 2.3 times
// it on the fitted 4096 one, where the widest penumbra spans 78 texels. So the default is no
// turn and more taps: every input fixed to the snapped grid, and a spot on the ground keeps
// its value however the camera moves. What that costs is in docs/shadow-probe.md.
float discTurn(vec2 uv, vec2 pixel)
{
	if (u_shadowDebug.y > 1.5) return 0.0;
	if (u_shadowDebug.y > 0.5)
	{
		float size = 1.0 / u_shadowParams.z;
		vec2 grid = vec2(u_shadowDebug.z + uv.x * size, u_shadowDebug.w - uv.y * size);
		return gradientNoise(grid) * 6.2831853;
	}
	return gradientNoise(pixel) * 6.2831853;
}

// The shadow hardens on contact. Five taps look for a blocker; where there is none the
// pixel is lit and done, which is most of the ground. Otherwise the penumbra is as wide as
// the blocker is far, for a sun four degrees across -- an invention, since the real half
// degree draws a line.
// The same split, read once. For a surface too small to show a penumbra: a blade of grass is
// two pixels wide at the nearest MU's camera goes, and thirteen taps to soften an edge across
// two pixels buys nothing anybody can see. The ground UNDER the field is still read with the
// full filter, so the shadow the eye actually reads -- the house's, the tree's, on the turf --
// keeps its soft edge; this only decides whether a blade is in that shadow or out of it.
//
// Measured: it is most of what the field costs. See docs/grass.md.
float sunShadowHard(vec3 wpos, vec3 normal, float ndotl)
{
	vec4 sc = mul(u_shadowMtx, vec4(wpos + normal * u_shadowParams.w, 1.0));
	sc.xyz /= sc.w;
	if (sc.x < 0.0 || sc.x > 1.0 || sc.y < 0.0 || sc.y > 1.0 || sc.z > 1.0) return 1.0;
	float slope = sqrt(saturate(1.0 - ndotl * ndotl)) / max(ndotl, 0.15);
	float bias = u_shadowParams.x * (1.0 + slope);
	return shadow2D(s_shadowCompare, vec3(sc.xy, sc.z - bias));
}

float sunShadow(vec3 wpos, vec3 normal, float ndotl, vec2 pixel)
{
	vec4 sc = mul(u_shadowMtx, vec4(wpos + normal * u_shadowParams.w, 1.0));
	sc.xyz /= sc.w;
	if (sc.x < 0.0 || sc.x > 1.0 || sc.y < 0.0 || sc.y > 1.0 || sc.z > 1.0) return 1.0;

	// The bias grows as the surface turns away from the sun, where one texel spans more depth.
	float slope = sqrt(saturate(1.0 - ndotl * ndotl)) / max(ndotl, 0.15);
	float bias = u_shadowParams.x * (1.0 + slope);
	float receiver = sc.z - bias;

	// Turned taps are few and dithered; unturned ones are many and still. See discTurn. The
	// widest penumbrae -- a canopy ten metres up -- still show faint copies of fine casters
	// at sixteen; twenty-four soften them for another 0.27 ms and do not remove them.
	bool still = u_shadowDebug.y > 1.5;
	float phase = discTurn(sc.xy, pixel);
	vec2 turn = vec2(cos(phase), sin(phase));

	float searchRadius = u_shadowReach.x;
	float blockerSum = 0.0;
	float blockerCount = 0.0;
	// Two loops of fixed length rather than one of a uniform's, so each unrolls over its table.
#define MU2_SEARCH_TAP(TAP) \
	{ \
		float d = texture2D(s_shadowDepth, sc.xy + turnTap(TAP, turn) * searchRadius).r; \
		if (d < receiver) \
		{ \
			blockerSum += d; \
			blockerCount += 1.0; \
		} \
	}
	if (still)
	{
		for (int i = 0; i < 9; ++i) MU2_SEARCH_TAP(kVogel9[i])
	}
	else
	{
		for (int i = 0; i < 5; ++i) MU2_SEARCH_TAP(kVogel5[i])
	}
#undef MU2_SEARCH_TAP
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
	float radius = clamp(penumbra, u_shadowParams.z, u_shadowReach.y);

	float sum = 0.0;
	if (still)
	{
		for (int i = 0; i < 16; ++i)
		{
			sum += shadow2D(s_shadowCompare, vec3(sc.xy + turnTap(kVogel16[i], turn) * radius, receiver));
		}
		return sum / 16.0;
	}
	for (int i = 0; i < 8; ++i)
	{
		sum += shadow2D(s_shadowCompare, vec3(sc.xy + turnTap(kVogel8[i], turn) * radius, receiver));
	}
	return sum / 8.0;
}
