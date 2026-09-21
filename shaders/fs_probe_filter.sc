$input v_texcoord0

// One mip of one face of the prefiltered probe: the raw cube seen through a GGX lobe of this
// mip's roughness, with the eye along the normal -- the split sum's own approximation, which
// fs_shade's envBRDFApprox is the other half of.
//
// Filtered importance sampling (Colbert and Krivanek, GPU Gems 3 ch. 20): each of the 32
// samples reads the raw cube at the mip whose texel covers the solid angle the sample stands
// for, so a wide lobe is smooth without hundreds of taps. The chain is fs_probe_down's.
#include "common.sh"
#include "probe.sh"

SAMPLERCUBE(s_source, 0);

// Van der Corput in base 2, in floats: the eight bits a 32-sample set needs.
float radicalInverse(int i)
{
	float r = 0.0;
	float f = 0.5;
	int n = i;
	for (int k = 0; k < 8; ++k)
	{
		r += f * float(n - 2 * (n / 2));
		n = n / 2;
		f *= 0.5;
	}
	return r;
}

void main()
{
	vec3 n = cubeDir(u_probeFace.x, v_texcoord0);
	float roughness = u_probeFace.y;
	if (roughness <= 0.0)
	{
		gl_FragColor = vec4(textureCubeLod(s_source, n, 0.0).rgb, 1.0);
		return;
	}

	float alpha = roughness * roughness;
	vec3 up = vec3(0.0, 1.0, 0.0);
	if (abs(n.y) > 0.999) up = vec3(1.0, 0.0, 0.0);
	vec3 tx = normalize(cross(up, n));
	vec3 ty = cross(n, tx);

	const int kSamples = 32;
	float texelSolidAngle = 4.0 * 3.14159265 / (6.0 * u_probeFace.z * u_probeFace.z);
	vec3 sum = vec3_splat(0.0);
	float weight = 0.0;
	for (int i = 0; i < kSamples; ++i)
	{
		float u = (float(i) + 0.5) / float(kSamples);
		float v = radicalInverse(i);
		float phi = 6.28318531 * u;
		float cosTheta = sqrt((1.0 - v) / (1.0 + (alpha * alpha - 1.0) * v));
		float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
		vec3 h = tx * (sinTheta * cos(phi)) + ty * (sinTheta * sin(phi)) + n * cosTheta;
		vec3 l = 2.0 * dot(n, h) * h - n;
		float ndotl = dot(n, l);
		if (ndotl > 0.0)
		{
			// With the eye on the normal, n.h = v.h and the pdf of l is D / 4.
			float pdf = distributionGGX(cosTheta, roughness) * 0.25;
			float sampleSolidAngle = 1.0 / (float(kSamples) * pdf + 1e-4);
			float lod = clamp(0.5 * log2(sampleSolidAngle / texelSolidAngle) + 1.0, 0.0,
			                  u_probeFace.w);
			sum += textureCubeLod(s_source, l, lod).rgb * ndotl;
			weight += ndotl;
		}
	}
	gl_FragColor = vec4(sum / max(weight, 1e-4), 1.0);
}
