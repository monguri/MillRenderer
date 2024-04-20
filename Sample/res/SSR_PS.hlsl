struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

cbuffer CbSSR : register(b0)
{
	float4x4 InvVRotPMatrix;
	float Near;
	float Far;
	int Width;
	int Height;
	int FrameSampleIndex;
	int bEnableSSR;
}

static const float ROUGHNESS_MASK_MUL = -6.66667; // Referenced UE's value.

Texture2D ColorMap : register(t0);
Texture2D DepthMap : register(t1);
Texture2D NormalMap : register(t2);
Texture2D MetallicRoughnessMap : register(t3);
// TODO: should be PointClamp?
SamplerState PointClampSmp : register(s0);

// Referenced UE's implementation
float InterleavedGradientNoise(float2 UV, float FrameId)
{
	// magic values are found by experimentation
	UV += FrameId * (float2(47, 17) * 0.695f);

	const float3 MAGIC_NUMBER = float3(0.06711056f, 0.00583715f, 52.9829189f);
	return frac(MAGIC_NUMBER.z * frac(dot(UV, MAGIC_NUMBER.xy)));
}

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

float GetDeviceZ(float2 uv)
{
	return DepthMap.Sample(PointClampSmp, uv).r;
}

float3 GetWSNormal(float2 uv)
{
	return normalize(NormalMap.Sample(PointClampSmp, uv).xyz * 2.0f - 1.0f);
}

bool RayCast(out float2 hitUV)
{
	// TODO: impl
	hitUV = 0;
	return false;
}

// Referenced UE's implementation
float GetRoughnessFade(float roughness)
{
	// mask SSR to reduce noise and for better performance, roughness of 0 should have SSR, at MaxRoughness we fade to 0
	return min(roughness * ROUGHNESS_MASK_MUL + 2, 1.0f);
}

float4 main(const VSOutput input) : SV_TARGET0
{
	if (bEnableSSR)
	{
		float3 origColor = ColorMap.Sample(PointClampSmp, input.TexCoord).rgb;

		// TODO: get roughness
		float roughness = MetallicRoughnessMap.Sample(PointClampSmp, input.TexCoord).g;
		float roughnessFade = GetRoughnessFade(roughness);
		// early return when surface is rough enough.
		if (roughnessFade <= 0)
		{
			return float4(origColor, 1);
		}

		// Used UE's SSR_QUALITY = 2 settings.
			uint NumSteps = 16;
		// uint NumRays = 1;

		float stepOffset = InterleavedGradientNoise(input.TexCoord * float2(Width, Height), FrameSampleIndex);
		//return float4(StepOffset, 1.0f);
		stepOffset -= 0.5f;

		float deviceZ = GetDeviceZ(input.TexCoord);
		// [-1,1]x[-1,1]
		float2 screenPos = input.TexCoord * float2(2, -2) + float2(-1, 1);
		float4 ndcPos = float4(screenPos, deviceZ, 1);
		float3 cameraOriginWorldPos = ConverFromNDCToCameraOriginWS(ndcPos);

		float3 N = GetWSNormal(input.TexCoord);
		float3 V = normalize(-cameraOriginWorldPos);
		float3 L = reflect(-V, N);

		float2 hitUV;
		bool bHit = RayCast(hitUV);

		float3 reflection = 0;
		if (bHit)
		{
			reflection = ColorMap.Sample(PointClampSmp, hitUV).rgb;
		}

		return float4(origColor + reflection, 1.0f);
	}
	else
	{
		float3 Color = ColorMap.Sample(PointClampSmp, input.TexCoord).rgb;
		return float4(Color, 1.0f);
	}
}