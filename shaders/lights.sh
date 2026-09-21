// The lamps: MU's point lights, read through a grid laid on the ground. Sprint 8a;
// docs/sprints/08a-the-lamps.md has the design and what was measured.
//
// Every light the map holds sits in one small texture, a column each: row 0 its position and
// reach, row 1 its colour times this frame's flicker. The lights never move, so which of them
// can reach a patch of ground is settled once at load, in a second texture: one cell per two
// metres of the map, two RGBA8 texels a cell, holding up to eight light numbers each plus one,
// with 0 meaning "no more". A pixel reads its own cell and loops over those eight at most. No
// light is chosen per frame, so none pops in or out as the camera moves, and the count on the
// map costs nothing a pixel can see.
//
// No shadow. MU's lights cast none -- they are terrain luminance, which has no notion of
// anything in the way -- and a shadow map a lamp is the cost of a whole view. And kept apart
// from the sun's shadow term entirely, which depends on nothing here.
#ifndef MU2_LIGHTS_SH
#define MU2_LIGHTS_SH

uniform vec4 u_lampGrid;    // xy: the grid's corner, world x and z  z: cells a metre  w: cells a side
uniform vec4 u_lampParams;  // x: lamp_strength  y: 1 when any light is set  zw: unused

SAMPLER2D(s_lamps, 13);
SAMPLER2D(s_lampGrid, 14);

// One light's contribution: the same GGX the sun takes, times a falloff that reaches exactly
// zero at the reach.
//
// The distance is MU's: measured FLAT on the ground, because MU's light is terrain luminance
// and has no height at all. Between the ground and the light's own height (row 1's alpha) the
// vertical is ignored, so the cobbles at a street lamp's foot 2.3 m below its head take the
// whole of it, as MU gives them the whole of L; above the light or below the ground the extra
// height counts, so a torch does not light a roof five metres over it. A 3D distance was tried
// first and gave the foot of a street lamp 41% of what MU gives it.
//
// The shape is `(1 - d^2/R^2)^2` and not MU's linear `(R - d) / R`: the cone has a point at
// its middle and a crease at its edge, and a pool with a ring round it reads as a decal. Both
// are 1 at the middle and put the same light into the disc (pi R^2 / 3). Invention, marked.
vec3 lampOne(int index, vec3 wpos, vec3 n, vec3 v, vec3 diffuseColour, vec3 f0, float roughness,
             float ndotv, float specular)
{
	vec4 at = texelFetch(s_lamps, ivec2(index, 0), 0);
	vec3 d = at.xyz - wpos;
	vec4 lit = texelFetch(s_lamps, ivec2(index, 1), 0);
	// d.y > 0: the pixel is below the light. Free down to the ground under it, then counted.
	float over = max(-d.y, 0.0) + max(d.y - lit.w, 0.0);
	float dist2 = d.x * d.x + d.z * d.z + over * over;
	float reach2 = at.w * at.w;
	if (dist2 >= reach2) return vec3_splat(0.0);

	float len2 = dot(d, d);
	vec3 l = d * inversesqrt(max(len2, 1e-6));
	float ndotl = saturate(dot(n, l));
	if (ndotl <= 0.0) return vec3_splat(0.0);

	float fall = 1.0 - dist2 / reach2;
	fall *= fall;

	vec3 h = normalize(l + v);
	float ndoth = saturate(dot(n, h));
	float vdoth = saturate(dot(v, h));
	vec3 f = fresnelSchlick(f0, vdoth) * specular;
	float dd = distributionGGX(ndoth, roughness);
	float g = geometrySmith(ndotv, ndotl, roughness);
	vec3 spec = f * dd * g / max(4.0 * ndotv * ndotl, 1e-5);
	vec3 kd = (vec3_splat(1.0) - f) * diffuseColour / 3.14159265;

	return (kd + spec) * lit.rgb * (u_lampParams.x * ndotl * fall);
}

// Every lamp that reaches this pixel. `diffuseColour` is the surface's OWN albedo, before
// MU's baked light multiplies it: MU adds a lamp into the same buffer light.png fills rather
// than multiplying it by that buffer, so a lamp in a dark corner lights the corner.
// `specular` is 1 on the town and the figures and 0 on the land, which takes no highlight from
// anything: fs_ground.sc says why.
vec3 lampLight(vec3 wpos, vec3 n, vec3 v, vec3 diffuseColour, vec3 f0, float roughness,
               float ndotv, float specular)
{
	vec3 sum = vec3_splat(0.0);
	if (u_lampParams.y < 0.5) return sum;

	vec2 cellF = floor((wpos.xz - u_lampGrid.xy) * u_lampGrid.z);
	if (cellF.x < 0.0 || cellF.y < 0.0 || cellF.x >= u_lampGrid.w || cellF.y >= u_lampGrid.w)
	{
		return sum;
	}
	ivec2 cell = ivec2(cellF);

	// Numbers are one more than the light's column, so a zero byte ends the list. Read as
	// unorm and put back: 255 steps, exact at every one of them.
	vec4 first = texelFetch(s_lampGrid, ivec2(cell.x * 2, cell.y), 0) * 255.0 + 0.5;
	vec4 second = texelFetch(s_lampGrid, ivec2(cell.x * 2 + 1, cell.y), 0) * 255.0 + 0.5;

	for (int i = 0; i < 8; ++i)
	{
		vec4 four = i < 4 ? first : second;
		int k = i < 4 ? i : i - 4;
		float slot = k == 0 ? four.x : (k == 1 ? four.y : (k == 2 ? four.z : four.w));
		int number = int(slot);
		if (number == 0) break;
		sum += lampOne(number - 1, wpos, n, v, diffuseColour, f0, roughness, ndotv, specular);
	}
	return sum;
}

#endif // MU2_LIGHTS_SH
