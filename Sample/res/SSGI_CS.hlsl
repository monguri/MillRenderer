cbuffer CbSSGI : register(b0)
{
	// TODO: may be not necessary
	float4x4 ProjMatrix : packoffset(c0);
	float4x4 VRotPMatrix : packoffset(c4);
	float4x4 InvVRotPMatrix : packoffset(c8);
	float Near : packoffset(c12);
	float Far : packoffset(c12.y);
	int Width : packoffset(c12.z);
	int Height : packoffset(c12.w);
	int FrameSampleIndex : packoffset(c13);
	float Intensity : packoffset(c13.y);
}

Texture2D HCB : register(t0);
Texture2D HZB : register(t1);
Texture2D NormalMap : register(t2);
SamplerState PointClampSmp : register(s0);

RWTexture2D<float4> OutResult : register(u0);

#ifndef F_PI
#define F_PI 3.14159265358979323f
#endif //F_PI

 // Referenced UE's value. QUALITY == 3
static const uint CONFIG_RAY_STEPS = 8;
static const uint CONFIG_RAY_COUNT = 16;

static const float SLOPE_COMPARE_TOLERANCE_SCALE = 4.0f; // Referenced UE's value.
static const uint TILE_PIXEL_SIZE_X = 4;
static const uint TILE_PIXEL_SIZE_Y = 4;
static const uint TILE_PIXEL_COUNT = TILE_PIXEL_SIZE_X * TILE_PIXEL_SIZE_Y;
static const uint LANE_PER_GROUPS = TILE_PIXEL_COUNT * CONFIG_RAY_COUNT;

groupshared float4 SharedMemory[TILE_PIXEL_COUNT * CONFIG_RAY_COUNT];

// TODO: same as the function of SSR_PS.hlsl
float3 GetWSNormal(float2 uv)
{
	return normalize(NormalMap.SampleLevel(PointClampSmp, uv, 0).xyz * 2.0f - 1.0f);
}

// TODO: same as the function of SSR_PS.hlsl
float GetHZBDeviceZ(float2 uv, float mipLevel)
{
	// HZB's uv is scaled to keep aspect ratio.
	return HZB.SampleLevel(PointClampSmp, uv * float2(1, (float)Height / Width), mipLevel).r;
}

//TODO: common functions with SSAO.

float ConvertFromDeviceZtoViewZ(float deviceZ)
{
	// https://learn.microsoft.com/ja-jp/windows/win32/dxtecharts/the-direct3d-transformation-pipeline
	// deviceZ = ((Far * viewZ) / (Far - Near) + Far * Near / (Far - Near)) / viewZ
	// viewZ = -linearDepth because view space is right-handed and clip space is left-handed.
	return (Far * Near) / (deviceZ * (Far - Near) - Far);
}

float3 ConverFromNDCToCameraOriginWS(float4 ndcPos)
{
	// referenced.
	// https://learn.microsoft.com/ja-jp/windows/win32/dxtecharts/the-direct3d-transformation-pipeline
	// That is left-handed projection matrix.
	// Matrix::CreatePerspectiveFieldOfView() transform right-handed viewspace to left-handed clip space.
	// So, referenced that code.
	float deviceZ = ndcPos.z;
	float viewPosZ = ConvertFromDeviceZtoViewZ(deviceZ);
	float clipPosW = -viewPosZ;
	float4 clipPos = ndcPos * clipPosW;
	float4 cameraOriginWorldPos = mul(InvVRotPMatrix, clipPos);
	
	return cameraOriginWorldPos.xyz;
}

float3 ConverFromCameraOriginWSToNDC(float3 cameraOriginWorldPos)
{
	float4 clipPos = mul(VRotPMatrix, float4(cameraOriginWorldPos, 1.0f));
	float4 ndcPos = clipPos / clipPos.w;
	return ndcPos.xyz;
}

// refered UE's Rand3DPCG16()
// 3D random number generator inspired by PCGs (permuted congruential generator)
// Using a **simple** Feistel cipher in place of the usual xor shift permutation step
// @param v = 3D integer coordinate
// @return three elements w/ 16 random bits each (0-0xffff).
// ~8 ALU operations for result.x    (7 mad, 1 >>)
// ~10 ALU operations for result.xy  (8 mad, 2 >>)
// ~12 ALU operations for result.xyz (9 mad, 3 >>)
uint3 Rand3DPCG16(int3 p)
{
	// taking a signed int then reinterpreting as unsigned gives good behavior for negatives
	uint3 v = uint3(p);

	// Linear congruential step. These LCG constants are from Numerical Recipies
	// For additional #'s, PCG would do multiple LCG steps and scramble each on output
	// So v here is the RNG state
	v = v * 1664525u + 1013904223u;

	// PCG uses xorshift for the final shuffle, but it is expensive (and cheap
	// versions of xorshift have visible artifacts). Instead, use simple MAD Feistel steps
	//
	// Feistel ciphers divide the state into separate parts (usually by bits)
	// then apply a series of permutation steps one part at a time. The permutations
	// use a reversible operation (usually ^) to part being updated with the result of
	// a permutation function on the other parts and the key.
	//
	// In this case, I'm using v.x, v.y and v.z as the parts, using + instead of ^ for
	// the combination function, and just multiplying the other two parts (no key) for 
	// the permutation function.
	//
	// That gives a simple mad per round.
	v.x += v.y*v.z;
	v.y += v.z*v.x;
	v.z += v.x*v.y;
	v.x += v.y*v.z;
	v.y += v.z*v.x;
	v.z += v.x*v.y;

	// only top 16 bits are well shuffled
	return v >> 16u;
}

// refered UE's ComputeRandomSeed()
uint2 ComputeRandomSeed(uint2 pixelPosition)
{
	return Rand3DPCG16(int3(pixelPosition, FrameSampleIndex)).xy;
}

// refered UE's Hammersley16()
float2 Hammersley16(uint index, uint numSamples, uint2 random)
{
	float e1 = frac((float)index / numSamples + float(random.x) * (1.0f / 65536.0f));
	float e2 = float((reversebits(index) >> 16) ^ random.y) * (1.0f / 65536.0f);
	return float2(e1, e2);
}

// refered UE's GetTangentBasis()
// [ Duff et al. 2017, "Building an Orthonormal Basis, Revisited" ]
// Discontinuity at TangentZ.z == 0
float3x3 GetTangentBasis(float3 tangentZ)
{
	const float Sign = tangentZ.z >= 0 ? 1 : -1;
	const float a = -rcp(Sign + tangentZ.z);
	const float b = tangentZ.x * tangentZ.y * a;

	float3 tangentX = {
		1 + Sign * a * tangentZ.x * tangentZ.x,
		Sign * b,
		-Sign * tangentZ.x
	};

	float3 tangentY = {
		b,
		Sign + a * tangentZ.y * tangentZ.y,
		-tangentZ.y
	};

	return float3x3(tangentX, tangentY, tangentZ);
}

// refered UE's ConcentricDiskSamplingHelper()
// Returns a point on the unit circle and a radius in z
float3 ConcentricDiskSamplingHelper(float2 E)
{
	// Rescale input from [0,1) to (-1,1). This ensures the output radius is in [0,1)
	float2 p = 2 * E - 0.99999994;
	float2 a = abs(p);
	float lo = min(a.x, a.y);
	float hi = max(a.x, a.y);
	float epsilon = 5.42101086243e-20; // 2^-64 (this avoids 0/0 without changing the rest of the mapping)
	float phi = (F_PI / 4) * (lo / (hi + epsilon) + 2 * float(a.y >= a.x));
	float radius = hi;
	// copy sign bits from p
	const uint signMask = 0x80000000;
	float2 disk = asfloat((asuint(float2(cos(phi), sin(phi))) & ~signMask) | (asuint(p) & signMask));
	// return point on the circle as well as the radius
	return float3(disk, radius);
}

// refered UE's UniformSampleDiskConcentric()
float2 UniformSampleDiskConcentric(float2 E)
{
	float3 result = ConcentricDiskSamplingHelper(E);
	return result.xy * result.z; // uniform sampling
}

// refered UE's ComputeL()
float3 ComputeL(float3 N, float2 E)
{
	float3x3 tangentBasis = GetTangentBasis(N);

	float3 L;
	L.xy = UniformSampleDiskConcentric(E);
	L.z = sqrt(1 - dot(L.xy, L.xy));
	L = mul(L, tangentBasis);

	return L;
}

// TODO: same as the function of SSR_PS.hlsl
// Referenced UE's implementation
float InterleavedGradientNoise(float2 pixelPos, float frameId)
{
	// magic values are found by experimentation
	pixelPos += frameId * (float2(47, 17) * 0.695f);

	const float3 MAGIC_NUMBER = float3(0.06711056f, 0.00583715f, 52.9829189f);
	return frac(MAGIC_NUMBER.z * frac(dot(pixelPos, MAGIC_NUMBER.xy)));
}

// TODO: same as the function of SSR_PS.hlsl
bool RayCast(float3 rayStartUVz, float3 cameraOriginRayStart, float3 rayDir, float depth, uint numSteps, float stepOffset, float roughness, out float mipLevel, out float2 hitUV)
{
	// ray length to be depth is referenced UE.
	float3 cameraOriginRayEnd = cameraOriginRayStart + rayDir * depth;
	float3 rayEndNDC = ConverFromCameraOriginWSToNDC(cameraOriginRayEnd);
	float3 rayEndUVz = float3(rayEndNDC.xy * float2(0.5f, -0.5f) + 0.5f, rayEndNDC.z);
	// viewport clip
	rayEndUVz.xy = saturate(rayEndUVz.xy);

	// z is deviceZ so steps are not uniform on world space.
	float3 rayStepUVz = rayEndUVz - rayStartUVz;
	float step = 1.0f / numSteps;
	rayStepUVz *= step;
	float3 rayUVz = rayStartUVz + rayStepUVz * stepOffset;

	float4 rayStartClipPos = mul(VRotPMatrix, float4(cameraOriginRayStart, 1.0f));
	float4 rayDepthClipPos = rayStartClipPos + mul(ProjMatrix, float4(0, 0, -depth, 0));
	float3 rayDepthNDC = rayDepthClipPos.xyz / rayDepthClipPos.w;

	float compareTolerance = max(abs(rayStepUVz.z), (rayDepthNDC.z - rayStartUVz.z) * SLOPE_COMPARE_TOLERANCE_SCALE * step);
	uint stepCount;
	bool bHit = false;
	float sampleDepthDiff;
	float preSampleDepthDiff = 0.0f;
	mipLevel = 1.0f; // start level refered UE.

	for (stepCount = 0; stepCount < numSteps; stepCount++)
	{
		float3 sampleUVz = rayUVz + rayStepUVz * (stepCount + 1);
		float sampleDepth = GetHZBDeviceZ(sampleUVz.xy, mipLevel);
		// Refered UE. SSR become as blurrier as high roughness.
		mipLevel += 4.0f / numSteps * roughness;

		sampleDepthDiff = sampleDepth - sampleUVz.z;
		bHit = (abs(sampleDepthDiff + compareTolerance) < compareTolerance);
		if (bHit)
		{
			break;
		}

		preSampleDepthDiff = sampleDepthDiff;
	}

	hitUV = 0;
	if (bHit)
	{
		float timeLerp = saturate(preSampleDepthDiff / (preSampleDepthDiff - sampleDepthDiff));
		float intersectTime = stepCount + timeLerp;
		hitUV = rayUVz.xy + rayStepUVz.xy * intersectTime;
	}

	return bHit;
}

float Luminance(float3 linearColor)
{
	return dot(linearColor, float3(0.3f, 0.59f, 0.11f));
}

[numthreads(TILE_PIXEL_SIZE_X, TILE_PIXEL_SIZE_Y, CONFIG_RAY_COUNT)]
void main(uint2 groupThreadID : SV_GroupThreadID, uint2 groupID : SV_GroupID, uint groupThreadIndex : SV_GroupIndex)
{
	uint groupPixelId = groupThreadIndex % TILE_PIXEL_COUNT;
	uint raySequenceId = groupThreadIndex / TILE_PIXEL_COUNT;
	uint2 groupPixelOffset = groupThreadID;
	uint2 pixelPosition = groupID * uint2(TILE_PIXEL_SIZE_X, TILE_PIXEL_SIZE_Y) + groupPixelOffset;

	float2 rcpDimension = 1.0f / float2(Width, Height);
	float2 uv = (pixelPosition + 0.5f) * rcpDimension;

	// Store 
	if (raySequenceId == 0)
	{
		SharedMemory[/* raySequenceId * TILE_PIXEL_COUNT + */ groupPixelId].xyz = GetWSNormal(uv);

		SharedMemory[/* raySequenceId * TILE_PIXEL_COUNT + */ groupPixelId].w = GetHZBDeviceZ(uv, 0);
	}

	GroupMemoryBarrierWithGroupSync();

	// [-1,1]x[-1,1]
	float2 screenPos = uv * float2(2, -2) + float2(-1, 1);
	float deviceZ = SharedMemory[groupPixelId].w;
	float4 ndcPos = float4(screenPos, deviceZ, 1);
	float3 cameraOriginWorldPos = ConverFromNDCToCameraOriginWS(ndcPos);

	float3 N = SharedMemory[groupPixelId].xyz;

	uint2 randomSeed = ComputeRandomSeed(pixelPosition);
	float2 E = Hammersley16(raySequenceId, CONFIG_RAY_COUNT, randomSeed);
	float3 L = ComputeL(N, E);

	float viewZ = ConvertFromDeviceZtoViewZ(deviceZ);

	float stepOffset = InterleavedGradientNoise(float2(pixelPosition), FrameSampleIndex);
	stepOffset -= 0.5f;

	float roughness = 1;

	float2 hitUV;
	float mipLevel = 0; // not used
	bool bHit = RayCast(float3(uv, deviceZ), cameraOriginWorldPos, L, -viewZ, CONFIG_RAY_STEPS, stepOffset, roughness, mipLevel, hitUV);
	if (bHit)
	{
		float3 sampleColor = HCB.SampleLevel(PointClampSmp, hitUV * float2(1, (float)Height / Width), mipLevel).rgb;
		float sampleColorWeight = rcp(1 + Luminance(sampleColor));
		float3 diffuseColor = sampleColor * sampleColorWeight;
		SharedMemory[raySequenceId * TILE_PIXEL_COUNT + groupPixelId].rgb = diffuseColor;
	}
	else
	{
		SharedMemory[raySequenceId * TILE_PIXEL_COUNT + groupPixelId].rgb = 0;
	}
	SharedMemory[raySequenceId * TILE_PIXEL_COUNT + groupPixelId].a = 0;

	GroupMemoryBarrierWithGroupSync();

	if (groupThreadIndex < TILE_PIXEL_COUNT)
	{
		float3 diffuseColor = 0;

		for (uint raySeqId = 0; raySeqId < CONFIG_RAY_COUNT; raySeqId++)
		{
			diffuseColor += SharedMemory[raySequenceId * TILE_PIXEL_COUNT + groupPixelId].rgb;
		}

		diffuseColor *= rcp(CONFIG_RAY_COUNT);
		diffuseColor *= rcp(1 - Luminance(diffuseColor));

		OutResult[pixelPosition] = float4(diffuseColor, 1) * Intensity;
	}
}