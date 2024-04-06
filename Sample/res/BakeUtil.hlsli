#include "BRDF.hlsli"

static const uint SampleCount = 128;

#ifndef ENABLE_MIPMAP_FILTERING
#define ENABLE_MIPMAP_FILTERING (1)
#endif //ENABLE_MIPMAP_FILTERING

float2 Hammersley(uint i, uint N)
{
	float ri = reversebits(i) * 2.3283064365386963e-10f;
	return float2(float(i) / float(N), ri);
}

float3 CalcDirection(float2 uv, const int faceIndex)
{
	float3 dir = 0;
	float2 pos = uv * 2.0f - 1.0f;

	switch (faceIndex)
	{
		case 0:
			dir = float3(1.0f, pos.y, -pos.x);
			break;
		case 1:
			dir = float3(-1.0f, pos.y, pos.x);
			break;
		case 2:
			dir = float3(pos.x, 1.0f, -pos.y);
			break;
		case 3:
			dir = float3(pos.x, -1.0f, pos.y);
			break;
		case 4:
			dir = float3(pos.x, pos.y, 1.0f);
			break;
		case 5:
			dir = float3(-pos.x, pos.y, -1.0f);
			break;
		default:
			// do nothing
			break;
	}

	return normalize(dir);
}

void TangentSpace(float3 N, out float3 T, out float3 B)
{
	// Tom Duff, James Burgess, Per Christensen, Christophe Hery, Andrew Kensler, Max Liani, and Ryusuke Villemin
	// "Building an Orthonormal Bais, Revisited",
	// Journal of Computer Graphics Techniques Vol.6, No.1, 2017.
	// Listing 3.参照.
	float s = (N.z >= 0.0f) ? 1.0f : -1.0f;
	float a = -1.0f / (s + N.z);
	float b = N.x * N.y * a;
	T = float3(1.0f + s * N.x * N.x * a, s * b, -s * N.x);
	B = float3(b, s + N.y * N.y * a, -N.y);
}

float3 SampleLambert(float2 u, float3 N)
{
	float r = sqrt(u.y);
	float phi = 2.0 * F_PI * u.x;

	float3 H;
	H.x = r * cos(phi);
	H.y = r * sin(phi);
	H.z = sqrt(1 - u.y);

	float3 T, B;
	TangentSpace(N, T, B);

	return normalize(T * H.x + B * H.y + N * H.z);
}

float3 SampleGGX(float2 u, float a, float3 N)
{
	float phi = 2.0 * F_PI * u.x;
	float cosTheta = sqrt((1.0 - u.y) / max(u.y * (a * a - 1.0) + 1.0, 1e-8f));
	float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

	float3 H;
	H.x = sinTheta * cos(phi);
	H.y = sinTheta * sin(phi);
	H.z = cosTheta;

	float3 T, B;
	TangentSpace(N, T, B);

	return normalize(T * H.x + B * H.y + N * H.z);
}

//
// Referenced glTF-Sample-Viewer ibl_filtering.frag
//
struct MicrofacetDistributionSample
{
	float pdf;
	float cosTheta;
	float sinTheta;
	float phi;
};

// GGX microfacet distribution
// https://www.cs.cornell.edu/~srm/publications/EGSR07-btdf.html
// This implementation is based on https://bruop.github.io/ibl/,
//  https://www.tobias-franke.eu/log/2014/03/30/notes_on_importance_sampling.html
// and https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch20.html
MicrofacetDistributionSample GGX(float2 xi, float roughness)
{
	MicrofacetDistributionSample ggx;

    // evaluate sampling equations
	float alpha = roughness * roughness;
	ggx.cosTheta = saturate(sqrt((1.0 - xi.y) / (1.0f + (alpha * alpha - 1.0f) * xi.y, 1e-8f)));
	ggx.sinTheta = sqrt(1.0 - ggx.cosTheta * ggx.cosTheta);
	ggx.phi = 2.0 * F_PI * xi.x;

    // evaluate GGX pdf (for half vector)
	ggx.pdf = D_GGX(ggx.cosTheta, alpha);

    // Apply the Jacobian to obtain a pdf that is parameterized by l
    // see https://bruop.github.io/ibl/
    // Typically you'd have the following:
    // float pdf = D_GGX(NoH, roughness) * NoH / (4.0 * VoH);
    // but since V = N => VoH == NoH
	ggx.pdf /= 4.0f;

	return ggx;
}

// hammersley2d describes a sequence of points in the 2d unit square [0,1)^2
// that can be used for quasi Monte Carlo integration
// GetImportanceSample returns an importance sample direction with pdf in the .w component
float4 GetImportanceSample(int sampleIndex, int sampleCount, float3 N, float roughness)
{
    // generate a quasi monte carlo point in the unit square [0.1)^2
	float2 xi = Hammersley(sampleIndex, sampleCount);

    // generate the points on the hemisphere with a fitting mapping for
    // the distribution (e.g. lambertian uses a cosine importance)
	MicrofacetDistributionSample importanceSample = GGX(xi, roughness);

    // transform the hemisphere sample to the normal coordinate frame
    // i.e. rotate the hemisphere to the normal direction
	float3 localSpaceDirection = normalize(float3(
		importanceSample.sinTheta * cos(importanceSample.phi),
		importanceSample.sinTheta * sin(importanceSample.phi),
		importanceSample.cosTheta
	));

	// TODO: ここからはglTF-Sample-Viewerのコードに差し替えてない
	float3 T, B;
	TangentSpace(N, T, B);

	return float4(T * localSpaceDirection.x + B * localSpaceDirection.y + N * localSpaceDirection.z, importanceSample.pdf);
}