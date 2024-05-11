struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

cbuffer CbSSR : register(b0)
{
	float4x4 ProjMatrix;
	float4x4 VRotPMatrix;
	float4x4 InvVRotPMatrix;
	float Near;
	float Far;
	int Width;
	int Height;
	int FrameSampleIndex;
	int bEnableSSR;
	int bDebugViewSSR;
}

static const float SSR_INTENSITY = 1.0f; // Referenced UE's value.
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

bool RayCast(float3 rayStartUVz, float3 cameraOriginRayStart, float3 rayDir, float depth, float stepOffset, float roughness, out float2 hitUV)
{
	// ray length to be depth is referenced UE.
	float3 cameraOriginRayEnd = cameraOriginRayStart + rayDir * depth;
	float3 rayEndNDC = ConverFromCameraOriginWSToNDC(cameraOriginRayEnd);
	float3 rayEndUVz = float3(rayEndNDC.xy * float2(0.5f, -0.5f) + 0.5f, rayEndNDC.z);
	// viewport clip
	rayEndUVz.xy = saturate(rayEndUVz.xy);

	// z is deviceZ so steps are not uniform on world space.
	float3 rayStepUVz = rayEndUVz - rayStartUVz;
	float step = 1.0f / NUM_STEPS;
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
	float mipLevel = 1.0f; // start level refered UE.

	for (stepCount = 0; stepCount < NUM_STEPS; stepCount++)
	{
		float3 sampleUVz = rayUVz + rayStepUVz * (stepCount + 1);
		float sampleDepth = GetHZBDeviceZ(sampleUVz.xy, mipLevel);
		// Refered UE. SSR become as blurrier as high roughness.
		mipLevel += 4.0f / NUM_STEPS * roughness;

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

float4 main(const VSOutput input) : SV_TARGET0
{
	if (bEnableSSR)
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
		bool bHit = RayCast(float3(input.TexCoord, deviceZ), cameraOriginWorldPos, rayDir, -viewZ, stepOffset, roughness, hitUV);

		float3 reflection = 0;
		if (bHit)
		{
			reflection = ColorMap.Sample(PointClampSmp, hitUV).rgb;
		}

		reflection *= roughnessFade;
		reflection *= SSR_INTENSITY;

		if (bDebugViewSSR)
		{
			return float4(reflection, 1.0f);
		}
		else
		{
			return float4(origColor + reflection, 1.0f);
		}
	}
	else
	{
		float3 Color = ColorMap.Sample(PointClampSmp, input.TexCoord).rgb;
		return float4(Color, 1.0f);
	}
}