#pragma once

#include "Common.hlsli"

static const float3 GROUND_ALBEDO_LINEAR = 0.401978f; // referenced UE.

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

// Float accuracy offset in Sky unit (km, so this is 1m). Should match the one in FAtmosphereSetup::ComputeViewData
static const float PLANET_RADIUS_OFFSET = 0.001f;

cbuffer CbSkyAtmosphere : register(b0)
{
	int TransmittanceLUT_Width : packoffset(c0);
	int TransmittanceLUT_Height : packoffset(c0.y);
	int MultiScatteringLUT_Width : packoffset(c0.z);
	int MultiScatteringLUT_Height : packoffset(c0.w);
	int ViewLUT_Width : packoffset(c1);
	int ViewLUT_Height : packoffset(c1.y);
	float BottomRadiusKm : packoffset(c1.z);
	float TopRadiusKm : packoffset(c1.w);
	float4x4 SkyViewLutReferential : packoffset(c2);
	float3 AtmosphereLightDirection : packoffset(c6);
	float3 AtmosphereLightIlluminanceOuterSpace : packoffset(c7);
};

Texture2D TransmittanceLUT_Texture : register(t0);
Texture2D MultiScatteredLuminaceLutTexture : register(t1);
SamplerState LinearClampSampler : register(s0);

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

// - RayOrigin: ray origin
// - RayDir: normalized ray direction
// - SphereCenter: sphere center
// - SphereRadius: sphere radius
// - Returns distance from RayOrigin to closest intersecion with sphere,
//   or -1.0 if no intersection.
float RaySphereIntersectNearest(float3 rayOrigin, float3 rayDir, float3 sphereCenter, float sphereRadius)
{
	float2 sol = RayIntersectSphere(rayOrigin, rayDir, float4(sphereCenter, sphereRadius));
	float sol0 = sol.x;
	float sol1 = sol.y;
	if (sol0 < 0.0f && sol1 < 0.0f)
	{
		return -1.0f;
	}
	if (sol0 < 0.0f)
	{
		return max(0.0f, sol1);
	}
	else if (sol1 < 0.0f)
	{
		return max(0.0f, sol0);
	}
	return max(0.0f, min(sol0, sol1));
}

////////////////////////////////////////////////////////////
// LUT functions
////////////////////////////////////////////////////////////

// Transmittance LUT function parameterisation from Bruneton 2017 https://github.com/ebruneton/precomputed_atmospheric_scattering
// uv in [0,1]
// ViewZenithCosAngle in [-1,1]
// ViewHeight in [bottomRAdius, topRadius]

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

void LutTransmittanceParamsToUV(in float viewHeight, in float viewZenithCosAngle, in float bottomRadius, in float topRadius, out float2 uv)
{
	float h = sqrt(max(0.0f, topRadius * topRadius - bottomRadius * bottomRadius));
	float rho = sqrt(max(0.0f, viewHeight * viewHeight - bottomRadius * bottomRadius));

	float discriminant = viewHeight * viewHeight * (viewZenithCosAngle * viewZenithCosAngle - 1.0f) + topRadius * topRadius;
	float d = max(0.0f, (-viewHeight * viewZenithCosAngle + sqrt(discriminant))); // Distance to atmosphere boundary

	float dmin = topRadius - viewHeight;
	float dmax = rho + h;
	float xmu = (d - dmin) / (dmax - dmin);
	float xr = rho / h;

	uv = float2(xmu, xr);
}

struct MediumSampleRGB
{
	float3 scattering;
	float3 extinction;

	float3 scatteringMie;
	float3 extinctionMie;

	float3 scatteringRay;
	float3 extinctionRay;

	float3 scatteringOzo;
	float3 extinctionOzo;
};

// If this is changed, please also update USkyAtmosphereComponent::GetTransmittance 
MediumSampleRGB SampleAthosphereMediumRGB(in float3 worldPos)
{
	const float sampleHeight = max(0.0f, (length(worldPos) - BottomRadiusKm));
	const float densityMie = exp(MIE_DENSITY_EXP_SCALE * sampleHeight);
	const float densityRay = exp(RAYLEIGH_DENSITY_EXP_SCALE * sampleHeight);
	const float densityOzo = sampleHeight < ABSORPTION_DENSITY0_LAYER_WIDTH ?
		saturate(ABSORPTION_DENSITY0_LINEAR_TERM * sampleHeight + ABSORPTION_DENSITY0_CONSTANT_TERM) : // We use saturate to allow the user to create plateau, and it is free on GCN.
		saturate(ABSORPTION_DENSITY1_LINEAR_TERM * sampleHeight + ABSORPTION_DENSITY1_CONSTANT_TERM);

	const float3 mieScattering = MIE_SCATTERING * MIE_SCATTERING_SCALE;

	MediumSampleRGB s;

	s.scatteringMie = densityMie * mieScattering;
	s.extinctionMie = densityMie * MIE_EXTINCTION;

	s.scatteringRay = densityRay * RAYLEIGH_SCATTERING;
	s.extinctionRay = densityRay * RAYLEIGH_SCATTERING;

	s.scatteringOzo = 0.0f;
	s.extinctionOzo = densityOzo * OZON_ABSORPTION;

	s.scattering = s.scatteringMie + s.scatteringRay + s.scatteringOzo;
	s.extinction = s.extinctionMie + s.extinctionRay + s.extinctionOzo;

	return s;
}

float3 GetMultipleScattering(float3 worldPos, float viewZenithCosAngle)
{
	float2 uv = saturate(float2(viewZenithCosAngle * 0.5f + 0.5f, (length(worldPos) - BottomRadiusKm) / (TopRadiusKm - BottomRadiusKm)));
	// We do no apply UV transform to sub range here as it has minimal impact.
	float3 multiScatteredLuminance = MultiScatteredLuminaceLutTexture.SampleLevel(LinearClampSampler, uv, 0).rgb;
	return multiScatteredLuminance;
}

float3 GetTransmittance(in float lightZenithCosAngle, in float pHeight, in bool whiteTransmittance)
{
	float2 uv;
	LutTransmittanceParamsToUV(pHeight, lightZenithCosAngle, BottomRadiusKm, TopRadiusKm, uv);


	float3 transmittanceToLight = 1.0f;
	if (!whiteTransmittance)
	{
		transmittanceToLight = TransmittanceLUT_Texture.SampleLevel(LinearClampSampler, uv, 0).rgb;
	}
	
	return transmittanceToLight;
}

struct SingleScatteringResult
{
	float3 L; // Scattered light (luminance)
	float3 opticalDepth; // Optical depth (1/m)
	float3 multiScatAs1;
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
	in float3 worldPos, in float3 worldDir,
	in bool ground, in SamplingSetup sampling, in bool mieRayPhase,
	in float3 light0dir, in float3 light0Illuminance,
	in bool whiteTransmittance,
	in bool multipleScatteringApproxSamplingEnabled)
{
	SingleScatteringResult result;
	result.L = 0;
	result.opticalDepth = 0;
	result.multiScatAs1 = 0;

	if (dot(worldPos, worldPos) <= BottomRadiusKm * BottomRadiusKm)
	{
		return result; // Camera is inside the planet ground
	}

	float3 planetO = float3(0.0f, 0.0f, 0.0f);
	float tMax = 0.0f;

	float tBottom = 0.0f;
	float2 solB = RayIntersectSphere(worldPos, worldDir, float4(planetO, BottomRadiusKm));
	float2 solT = RayIntersectSphere(worldPos, worldDir, float4(planetO, TopRadiusKm));

	// •‰‚Ì‰ð‚ÍƒŒƒC‚Ì‹t•ûŒü‚Å‚Ì‰ðA‚ ‚é‚¢‚ÍŒð·‚È‚µ‚Å‰ð‚È‚µ‚È‚Ì‚Å‰ð‚Æ‚µ‚È‚¢
	const bool bNoBotIntersection = all(solB < 0.0f);
	const bool bNoTopIntersection = all(solT < 0.0f);
	if (bNoTopIntersection)
	{
		// No intersection with planet or its atmosphere.
		tMax = 0.0f;
		return result;
	}
	else if (bNoBotIntersection)
	{
		// No intersection with planet, so we trace up to the far end of the top atmosphere 
		// (looking up from ground or edges when see from afar in space).
		tMax = max(solT.x, solT.y);
	}
	else
	{
		// Interesection with planet and atmospehre: we simply trace up to the planet ground.
		// We know there is at least one intersection thanks to bNoBotIntersection.
		// If one of the solution is invalid=-1, that means we are inside the planet: we stop tracing by setting tBottom=0.
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
	float miePhaseValueLight0 = HenyeyGreensteinPhase(MIE_ANISOTOROPY, -cosTheta); // negate cosTheta because due to WorldDir being a "in" direction. 
	float rayleighPhaseValueLight0 = RayleighPhase(cosTheta);

	// Ray march the atmosphere to integrate optical depth
	float3 L = 0.0f;
	float3 throughput = 1.0f;
	float3 opticalDepth = 0.0f;
	float t = 0.0f;

	float3 exposedLight0Illuminance = light0Illuminance;

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

		// Phase and transmittance for light 0
		const float3 upVector = p / pHeight;
		float light0ZenithCosAngle = dot(light0dir, upVector);
		float3 transmittanceToLight0 = GetTransmittance(light0ZenithCosAngle, pHeight, whiteTransmittance);
		float3 phaseTimesScattering0;
		float3 phaseTimesScattering0MieOnly;
		float3 phaseTimesScattering0RayOnly;
		if (mieRayPhase)
		{
			phaseTimesScattering0MieOnly = medium.scatteringMie * miePhaseValueLight0;
			phaseTimesScattering0RayOnly = medium.scatteringRay * rayleighPhaseValueLight0;
			phaseTimesScattering0 = phaseTimesScattering0MieOnly + phaseTimesScattering0RayOnly;
		}
		else
		{
			phaseTimesScattering0MieOnly = medium.scatteringMie * uniformPhase;
			phaseTimesScattering0RayOnly = medium.scatteringRay * uniformPhase;
			phaseTimesScattering0 = medium.scattering * uniformPhase;
		}
		
		// Multiple scattering approximation
		float3 multiScatteredLuminance0 = 0.0f;
		if (multipleScatteringApproxSamplingEnabled)
		{
			multiScatteredLuminance0 = GetMultipleScattering(p, light0ZenithCosAngle);
		}

		// Planet shadow
		float tPlanet0 = RaySphereIntersectNearest(p, light0dir, planetO + PLANET_RADIUS_OFFSET * upVector, BottomRadiusKm);
		float planetShadow0 = tPlanet0 >= 0.0f ? 0.0f : 1.0f;
		
		// When using the power serie to accumulate all sattering order, serie r must be <1 for a serie to converge. 
		// Under extreme coefficient, MultiScatAs1 can grow larger and thus results in broken visuals. 
		// The way to fix that is to use a proper analytical integration as porposed in slide 28 of http://www.frostbite.com/2015/08/physically-based-unified-volumetric-rendering-in-frostbite/ 
		// However, it is possible to disable as it can also work using simple power serie sum unroll up to 5th order. The rest of the orders has a really low contribution. 
		// 1 is the integration of luminance over the 4pi of a sphere, and assuming an isotropic phase function of 1.0/(4*PI) 
		result.multiScatAs1 += throughput * medium.scattering * 1.0f * dt;

		// MultiScatteredLuminance is already pre-exposed, atmospheric light contribution needs to be pre exposed
		// Multi-scattering is also not affected by PlanetShadow or TransmittanceToLight because it contains diffuse light after single scattering.
		float3 S = exposedLight0Illuminance * (planetShadow0 * transmittanceToLight0 * phaseTimesScattering0) + multiScatteredLuminance0 * medium.scattering;

		// See slide 28 at http://www.frostbite.com/2015/08/physically-based-unified-volumetric-rendering-in-frostbite/ 
		float3 Sint = (S - S * sampleTransmittance) / medium.extinction; // integrate along the current step segment 

		L += throughput * Sint;
		throughput *= sampleTransmittance;
	}

	if (ground && tMax == tBottom)
	{
		// Account for bounced light off the planet
		float3 p = worldPos + tBottom * worldDir;
		float pHeight = length(p);

		const float3 upVector = p / pHeight;
		float light0ZenithCosAngle = dot(light0dir, upVector);
		float3 transmittanceToLight0 = GetTransmittance(light0ZenithCosAngle, pHeight, whiteTransmittance);

		const float NdotL0 = saturate(dot(upVector, light0dir));
		L += light0Illuminance * transmittanceToLight0 * throughput * NdotL0 * GROUND_ALBEDO_LINEAR / F_PI;
	}

	result.L = L;
	result.opticalDepth = opticalDepth;
	return result;
}
