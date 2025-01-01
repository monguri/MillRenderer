#pragma once

// Transmittance LUT function parameterisation from Bruneton 2017 https://github.com/ebruneton/precomputed_atmospheric_scattering
// uv in [0,1]
// ViewZenithCosAngle in [-1,1]
// ViewHeight in [bottomRAdius, topRadius]
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

