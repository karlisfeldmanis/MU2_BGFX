// The body of fs_blur, shared by the single-sampled and multisampled variants so the
// logic exists once. See common.sh's prepassAt.
// A 4x4 box over the half-resolution AO, weighted by how near in depth each tap is. Plain
// box blur bleeds a figure's occlusion onto the ground behind it; the depth weight is what
// keeps the contact dark and the ground clean.
#include "common.sh"

void main()
{
	float centreDepth = prepassAt(v_texcoord0).w;
	if (centreDepth <= 0.0)
	{
		gl_FragColor = vec4_splat(1.0);
		return;
	}

	float sum = 0.0;
	float weightSum = 0.0;
	for (int y = -2; y < 2; ++y)
	{
		for (int x = -2; x < 2; ++x)
		{
			vec2 uv = v_texcoord0 + vec2(float(x) + 0.5, float(y) + 0.5) * u_viewTexel.xy;
			float depth = prepassAt(uv).w;
			if (depth <= 0.0) continue;
			// Within a tenth of the depth counts fully, and it falls off from there.
			float w = saturate(1.0 - abs(depth - centreDepth) / (centreDepth * 0.1 + 1e-4));
			sum += texture2D(s_ao, uv).r * w;
			weightSum += w;
		}
	}
	gl_FragColor = vec4_splat(weightSum > 0.0 ? sum / weightSum : 1.0);
}
