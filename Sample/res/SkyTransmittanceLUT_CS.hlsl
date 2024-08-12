#define ROOT_SIGNATURE ""\
"DescriptorTable(CBV(b0))"\
", DescriptorTable(UAV(u0))"\

cbuffer CbSkyAtmosphere : register(b0)
{
	int Width : packoffset(c0);
	int Height : packoffset(c0.y);
	float bottomRadiusKm : packoffset(c0.z);
	float topRadiusKm : packoffset(c0.w);
};

RWTexture2D<float3> OutResult : register(u0);

static const uint TILE_PIXEL_SIZE_X = 8;
static const uint TILE_PIXEL_SIZE_Y = 8;

void UVtoLUTTransmittanceParams(out float viewHeight, out float viewZenithCosAngle, in float bottomRadius, in float topRadius, in float2 uv)
{
	float xmu = uv.x;
	float xr = uv.y;

	float h = sqrt(topRadius * topRadius - bottomRadius * bottomRadius);
	float rho = h * xr;
	viewHeight = sqrt(rho * rho + bottomRadius * bottomRadius);

	float dmin = topRadius - viewHeight;
	float dmax = rho + h;
	float d = dmin + xmu * (dmax - dmin); // lerp(dmin, dmax, xmu)
	// law of cosines. viewHeight-angle-d triangle.
	viewZenithCosAngle = d == 0.0f ? 1.0f : (h * h - rho * rho - d * d) / (2.0f * viewHeight * d);
	viewZenithCosAngle = clamp(viewZenithCosAngle, -1.0f, 1.0f);
}

[RootSignature(ROOT_SIGNATURE)]
[numthreads(TILE_PIXEL_SIZE_X, TILE_PIXEL_SIZE_Y, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
	float2 pixPos = DTid + 0.5f;
	float2 uv = pixPos / float2(Width, Height);

	float viewHeight;
	float viewZenithCosAngle;

	UVtoLUTTransmittanceParams(viewHeight, viewZenithCosAngle, bottomRadiusKm, topRadiusKm, uv);

	float3 worldPos = float3(0.0f, 0.0f, viewHeight);
	float3 worldDir = float3(0.0f, sqrt(1.0f - viewZenithCosAngle * viewZenithCosAngle), viewZenithCosAngle);

	const bool ground = false;

	OutResult[pixPos] = float3(0, 0, 0);
}