#define ROOT_SIGNATURE ""\
"DescriptorTable(CBV(b0))"\
", DescriptorTable(UAV(u0))"\

#ifndef F_PI
#define F_PI 3.14159265358979323f
#endif //F_PI

cbuffer CbSkyAtmosphere : register(b0)
{
	int Width : packoffset(c0);
	int Height : packoffset(c0.y);
	float bottomRadiusKm : packoffset(c0.z);
	float topRadiusKm : packoffset(c0.w);
};

RWTexture2D<float3> OutResult : register(u0);

static const float MIE_ANISOTOROPY = 0.8f; // referenced UE.
static const float EARTH_RAYLEIGH_SCALE_HEIGHT = 8.0f; // referenced UE.
static const float EARTH_MIE_SCALE_HEIGHT = 1.2f; // referenced UE.
static const float RAYLEIGH_DENSITY_EXP_SCALE = -1.0f / EARTH_RAYLEIGH_SCALE_HEIGHT; // referenced UE.
static const float MIE_DENSITY_EXP_SCALE = -1.0f / EARTH_MIE_SCALE_HEIGHT; // referenced UE.

static const float3 MIE_SCATTERING = float3(1, 1, 1); // TODO:sRGB
static const float MIE_SCATTERING_SCALE = 0.003996f;
static const float3 MIE_ABSORPTION = float3(1, 1, 1); // TODO:sRGB
static const float MIE_ABSORPTION_SCALE = 0.000444f;
static const float3 MIE_EXTINCTION = MIE_SCATTERING * MIE_SCATTERING_SCALE + MIE_ABSORPTION * MIE_ABSORPTION_SCALE;

// Float to a u8 rgb + float length can lose some precision but it is better UI wise.
static const float3 RAYLEIGH_SCATTERING = float3(0.005802f, 0.013558f, 0.033100f); // TODO:sRGB

static const float3 OZON_ABSORPTION = float3(0.000650f, 0.001881f, 0.000085f); // TODO:sRGB
static const float OZON_TIP_ALTITUDE = 25.0f;
static const float OZON_TIP_VALUE = 1.0f;
static const float OZON_WIDTH = 15.0f;
static const float ABSORPTION_DENSITY0_LAYER_WIDTH = OZON_TIP_ALTITUDE;
static const float ABSORPTION_DENSITY0_LINEAR_TERM = OZON_TIP_VALUE / OZON_WIDTH;
static const float ABSORPTION_DENSITY0_CONSTANT_TERM = OZON_TIP_VALUE - OZON_TIP_ALTITUDE * ABSORPTION_DENSITY0_LINEAR_TERM;
static const float ABSORPTION_DENSITY1_LINEAR_TERM = -ABSORPTION_DENSITY0_LINEAR_TERM;
static const float ABSORPTION_DENSITY1_CONSTANT_TERM = OZON_TIP_VALUE - OZON_TIP_ALTITUDE * ABSORPTION_DENSITY1_LINEAR_TERM;

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

/**
 * Returns near intersection in x, far intersection in y, or both -1 if no intersection.
 * RayDirection does not need to be unit length.
 */
float2 RayIntersectSphere(float3 rayOrigin, float3 rayDirection, float4 sphere)
{
	float3 localPosition = rayOrigin - sphere.xyz;
	float localPositionSqr = dot(localPosition, localPosition);

	float3 quadraticCoef;
	quadraticCoef.x = dot(rayDirection, rayDirection);
	quadraticCoef.y = 2 * dot(rayDirection, localPosition);
	quadraticCoef.z = localPositionSqr - sphere.w * sphere.w;

	float discriminant = quadraticCoef.y * quadraticCoef.y - 4 * quadraticCoef.x * quadraticCoef.z;
	// TODO:‰Šú’l‚ª-1‚Å–â‘è‚È‚¢H
	float2 intersections = -1;

	// Only continue if the ray intersects the sphere
	[flatten]
	if (discriminant >= 0)
	{
		float sqrtDiscriminant = sqrt(discriminant);
		intersections = (-quadraticCoef.y + float2(-1, 1) * sqrtDiscriminant) / (2 * quadraticCoef.x);
	}

	return intersections;
}

// Follows PBRT convention http://www.pbr-book.org/3ed-2018/Volume_Scattering/Phase_Functions.html#PhaseHG
float HenyeyGreensteinPhase(float g, float cosTheta)
{
	// Reference implementation (i.e. not schlick approximation). 
	// See http://www.pbr-book.org/3ed-2018/Volume_Scattering/Phase_Functions.html
	float numer = 1.0f - g * g;
	float denom = 1.0f + g * g + 2.0f * g * cosTheta;
	return numer / (4.0f * F_PI * denom * sqrt(denom));
}

float RayleighPhase(float cosTheta)
{
	float factor = 3.0f / (16.0f * F_PI);
	return factor * (1.0f + cosTheta * cosTheta);
}

struct MediumSampleRGB
{
	float3 extinction;
	float3 extinctionMie;
	float3 extinctionRay;
	float3 extinctionOzo;
};

// If this is changed, please also update USkyAtmosphereComponent::GetTransmittance 
MediumSampleRGB SampleAthosphereMediumRGB(in float3 worldPos)
{
	const float sampleHeight = max(0.0f, (length(worldPos) - bottomRadiusKm));
	const float densityMie = exp(MIE_DENSITY_EXP_SCALE * sampleHeight);
	const float densityRay = exp(RAYLEIGH_DENSITY_EXP_SCALE * sampleHeight);
	const float densityOzo = sampleHeight < ABSORPTION_DENSITY0_LAYER_WIDTH ?
		saturate(ABSORPTION_DENSITY0_LINEAR_TERM * sampleHeight + ABSORPTION_DENSITY0_CONSTANT_TERM) :
		saturate(ABSORPTION_DENSITY1_LINEAR_TERM * sampleHeight + ABSORPTION_DENSITY1_CONSTANT_TERM);

	MediumSampleRGB s;
	s.extinctionMie = densityMie * MIE_EXTINCTION;
	s.extinctionRay = densityRay * RAYLEIGH_SCATTERING;
	s.extinctionOzo = densityOzo * OZON_ABSORPTION;

	s.extinction = s.extinctionMie + s.extinctionRay + s.extinctionOzo;

	return s;
}

struct SingleScatteringResult
{
	float3 opticalDepth; // Optical depth (1/m)
};

struct SamplingSetup
{
	bool variableSampleCount;
	float sampleCountIni; // Used when VariableSampleCount is false
	float minSampleCount; // Used when VariableSampleCount is true
	float maxSampleCount; // Used when VariableSampleCount is true
	float distanceToSampleCountMaxInv; // Used when VariableSampleCount is true
};

// In this function, all world position are relative to the planet center (itself expressed within translated world space)
SingleScatteringResult IntegrateSingleScatteredLuminance(
	in float4 SVPos, in float3 worldPos, in float3 worldDir,
	in SamplingSetup sampling, in float3 light0dir, in float3 light0Illuminance
)
{
	SingleScatteringResult result;
	result.opticalDepth = 0;

	if (dot(worldPos, worldPos) <= bottomRadiusKm * bottomRadiusKm)
	{
		return result; // Camera is inside the planet ground
	}

	float3 planetO = float3(0.0f, 0.0f, 0.0f);
	float tMax = 0.0f;

	float tBottom = 0.0f;
	float2 solB = RayIntersectSphere(worldPos, worldDir, float4(planetO, bottomRadiusKm));
	float2 solT = RayIntersectSphere(worldPos, worldDir, float4(planetO, topRadiusKm));

	const bool bNoBotIntersection = all(solB < 0.0f);
	const bool bNoTopIntersection = all(solT < 0.0f);
	if (bNoTopIntersection)
	{
		tMax = 0.0f;
		return result;
	}
	else if (bNoBotIntersection)
	{
		tMax = max(solT.x, solT.y);
	}
	else
	{
		tBottom = max(0.0f, min(solB.x, solB.y));
		tMax = tBottom;
	}

	tMax = min(tMax, 9000000.0f);

	// Sample count 
	float sampleCount = sampling.sampleCountIni;
	float sampleCountFloor = sampling.sampleCountIni;
	float tMaxFloor = tMax;
	if (sampling.variableSampleCount)
	{
		sampleCount = lerp(sampling.minSampleCount, sampling.maxSampleCount, saturate(tMax * sampling.distanceToSampleCountMaxInv));
		sampleCountFloor = floor(sampleCount);
		tMaxFloor = tMax * sampleCountFloor / sampleCount;	// rescale tMax to map to the last entire step segment.
	}
	float dt = tMax / sampleCount;

	// Phase functions
	const float uniformPhase = 1.0f / (4.0f * F_PI);
	const float3 wi = light0dir;
	const float3 wo = worldDir;
	float cosTheta = dot(wi, wo);
	float miePhaseValueLight0 = HenyeyGreensteinPhase(MIE_ANISOTOROPY, -cosTheta);
	float rayleighPhaseValueLight0 = RayleighPhase(cosTheta);

	// Ray march the atmosphere to integrate optical depth
	float3 opticalDepth = 0.0f;
	float t = 0.0f;
	float tPrev = 0.0f;

	float pixelNoise = 0.3f; // from UE's DEFAULT_SAMPLE_OFFSET 
	for (float sampleI = 0.0f; sampleI < sampleCount; sampleI += 1.0f)
	{
		// Compute current ray t and sample point P
		if (sampling.variableSampleCount)
		{
			// More expenssive but artefact free
			float t0 = sampleI / sampleCountFloor;
			float t1 = (sampleI + 1.0f) / sampleCountFloor;

			// Non linear distribution of samples within the range.
			t0 = t0 * t0;
			t1 = t1 * t1;

			// Make t0 and t1 world space distances.
			t0 = tMaxFloor * t0;
			if (t1 > 1.0f)
			{
				t1 = tMax;
			}
			else
			{
				t1 = tMaxFloor * t1;
				//t1 = tMaxFloor;	// this reveal depth slices
			}

			t = t0 + (t1 - t0) * pixelNoise;
			dt = t1 - t0;
		}
		else
		{
			t = tMax * (sampleI + pixelNoise) / sampleCount;
		}

		float3 p = worldPos + t * worldDir;
		float pHeight = length(p);

		// Sample the medium
		MediumSampleRGB medium = SampleAthosphereMediumRGB(p);
		const float3 sampleOpticalDepth = medium.extinction * dt;
		const float3 sampleTransmittance = exp(-sampleOpticalDepth);
		opticalDepth += sampleOpticalDepth;

		// TODO:impl
	}

	result.opticalDepth = opticalDepth;
	return result;
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

	SamplingSetup sampling = (SamplingSetup)0;
	{
		sampling.variableSampleCount = false;
		sampling.sampleCountIni = 10.0f;
	}

	const bool ground = false;

	OutResult[pixPos] = float3(0, 0, 0);
}