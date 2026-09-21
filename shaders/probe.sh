// The reflection probe's cube, sprint 8c. docs/sprints/08c-the-metal.md.
//
// One cube round the player, re-rendered a face every other frame, its chain, and a
// prefiltered copy whose mips are GGX lobes of rising roughness. fs_probe_sky draws a face's
// sky, fs_probe_down its chain, fs_probe_filter the copy, and fs_shade reads the copy.
#ifndef MU2_PROBE_SH
#define MU2_PROBE_SH

// x: which face, 0..5 in +x -x +y -y +z -z order  y: the roughness this mip holds, or for
// the chain the level being made  z: the raw cube's edge in texels  w: the chain's last level,
// which the filter clamps to
uniform vec4 u_probeFace;

// A texel of face `face` at `uv` (0..1, v down the face as it lies in memory) as a direction.
// The table every cube sampler uses: D3D's, which is GL's with row 0 at t = 0, and Metal's.
// The renderer's face cameras (Renderer::probeFaceView) are built to put the same direction
// under the same texel. The sheet's probe_view says whether they do: it draws the cube where
// the town is, and a turned or mirrored face shows as the town in the wrong place.
vec3 cubeDir(float face, vec2 uv)
{
	float s = uv.x * 2.0 - 1.0;
	float t = uv.y * 2.0 - 1.0;
	vec3 d;
	if (face < 0.5)      d = vec3( 1.0,   -t,   -s);
	else if (face < 1.5) d = vec3(-1.0,   -t,    s);
	else if (face < 2.5) d = vec3(    s,  1.0,    t);
	else if (face < 3.5) d = vec3(    s, -1.0,   -t);
	else if (face < 4.5) d = vec3(    s,   -t,  1.0);
	else                 d = vec3(   -s,   -t, -1.0);
	return normalize(d);
}

#endif // MU2_PROBE_SH
