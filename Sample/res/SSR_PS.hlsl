#include "Common.hlsli"

#define ROOT_SIGNATURE ""\
"RootFlags"\
"("\
"ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT"\
" | DENY_HULL_SHADER_ROOT_ACCESS"\
" | DENY_DOMAIN_SHADER_ROOT_ACCESS"\
" | DENY_GEOMETRY_SHADER_ROOT_ACCESS"\
")"\
", DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t1), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t2), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t3), visibility = SHADER_VISIBILITY_PIXEL)"\
", DescriptorTable(SRV(t4), visibility = SHADER_VISIBILITY_PIXEL)"\
", StaticSampler"\
"("\
"s0"\
", filter = FILTER_MIN_MAG_MIP_POINT"\
", addressU = TEXTURE_ADDRESS_CLAMP"\
", addressV = TEXTURE_ADDRESS_CLAMP"\
", addressW = TEXTURE_ADDRESS_CLAMP"\
", maxAnisotropy = 1"\
", comparisonFunc = COMPARISON_NEVER"\
", borderColor = STATIC_BORDER_COLOR_TRANSPARENT_BLACK"\
", visibility = SHADER_VISIBILITY_PIXEL"\
")"\

struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

cbuffer CbSSR : register(b0)
{
	float4x4 ProjMatrix : packoffset(c0);
	float4x4 VRotPMatrix : packoffset(c4);
	float4x4 InvVRotPMatrix : packoffset(c8);
	float Near : packoffset(c12);
	int Width : packoffset(c12.y);
	int Height : packoffset(c12.z);
	int FrameSampleIndex : packoffset(c12.w);
	float Intensity : packoffset(c13);
	int bDebugViewSSR : packoffset(c13.y);
}

static const float ROUGHNESS_MASK_MUL = -6.66667f; // Referenced UE's value.
static const uint NUM_STEPS = 16; // Referenced UE's value. SSR_QUALITY=2 and NUM_RAYS=1
static const float SLOPE_COMPARE_TOLERANCE_SCALE = 4.0f; // Referenced UE's value.

Texture2D ColorMap : register(t0);
Texture2D DepthMap : register(t1);
Texture2D NormalMap : register(t2);
Texture2D MetallicRoughnessMap : register(t3);
Texture2D HZB : register(t4);
SamplerState PointClampSmp : register(s0);

// Referenced UE's implementation
float GetRoughnessFade(float roughness)
{
	// mask SSR to reduce noise and for better performance, roughness of 0 should have SSR, at MaxRoughness we fade to 0
	return min(roughness * ROUGHNESS_MASK_MUL + 2, 1.0f);
}

// Referenced UE's implementation
float InterleavedGradientNoise(float2 pixelPos, float frameId)
{
	// magic values are found by experimentation
	pixelPos += frameId * (float2(47, 17) * 0.695f);

	const float3 MAGIC_NUMBER = float3(0.06711056f, 0.00583715f, 52.9829189f);
	return frac(MAGIC_NUMBER.z * frac(dot(pixelPos, MAGIC_NUMBER.xy)));
}

//TODO: common functions with SSAO.

float ConvertFromDeviceZtoViewZ(float deviceZ)
{
	// https://shikihuiku.github.io/post/projection_matrix/
	return -Near / max(deviceZ, DEVICE_Z_MIN_VALUE);
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

float GetSceneDeviceZ(float2 uv)
{
	return DepthMap.SampleLevel(PointClampSmp, uv, 0).r;
}

float GetHZBDeviceZ(float2 uv, float mipLevel)
{
	// HZB's uv is scaled to keep aspect ratio.
	return HZB.SampleLevel(PointClampSmp, uv * float2(1, (float)Height / Width), mipLevel).r;
}

float3 GetWSNormal(float2 uv)
{
	return normalize(NormalMap.Sample(PointClampSmp, uv).xyz * 2.0f - 1.0f);
}

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

	float compareTolerance = max(abs(rayStepUVz.z), (rayStartUVz.z - rayDepthNDC.z) * SLOPE_COMPARE_TOLERANCE_SCALE * step);
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

		sampleDepthDiff = sampleUVz.z - sampleDepth;
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

[RootSignature(ROOT_SIGNATURE)]
float4 main(const VSOutput input) : SV_TARGET0
{
	float3 origColor = ColorMap.Sample(PointClampSmp, input.TexCoord).rgb;

	float roughness = MetallicRoughnessMap.Sample(PointClampSmp, input.TexCoord).g;
	float roughnessFade = GetRoughnessFade(roughness);
	// early return when surface is rough enough.
	if (roughnessFade <= 0)
	{
		if (bDebugViewSSR)
		{
			return float4(0, 0, 0, 1);
		}
		else
		{
			return float4(origColor, 1);
		}
	}

	float stepOffset = InterleavedGradientNoise(input.TexCoord * float2(Width, Height), FrameSampleIndex);
	//return float4(StepOffset, 1.0f);
	stepOffset -= 0.5f;

	float deviceZ = GetSceneDeviceZ(input.TexCoord);
	// [-1,1]x[-1,1]
	float2 screenPos = input.TexCoord * float2(2, -2) + float2(-1, 1);
	float4 ndcPos = float4(screenPos, deviceZ, 1);
	float3 cameraOriginWorldPos = ConverFromNDCToCameraOriginWS(ndcPos);

	float3 N = GetWSNormal(input.TexCoord);
	float3 V = normalize(-cameraOriginWorldPos);
	float3 rayDir = reflect(-V, N);

	float viewZ = ConvertFromDeviceZtoViewZ(deviceZ);

	float2 hitUV;
	float mipLevel = 0; // not used
	bool bHit = RayCast(float3(input.TexCoord, deviceZ), cameraOriginWorldPos, rayDir, -viewZ, NUM_STEPS, stepOffset, roughness, mipLevel, hitUV);

	float3 reflection = 0;
	if (bHit)
	{
		reflection = ColorMap.Sample(PointClampSmp, hitUV).rgb;
	}

	reflection *= roughnessFade;
	reflection *= Intensity;

	if (bDebugViewSSR)
	{
		return float4(reflection, 1.0f);
	}
	else
	{
		return float4(origColor + reflection, 1.0f);
	}
}