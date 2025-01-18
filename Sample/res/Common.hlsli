#pragma once

#ifndef F_PI
#define F_PI 3.14159265358979323f
#endif //F_PI

// floatの精度的に問題のない範囲で小さい値
#ifndef SMALL_VALUE
#define SMALL_VALUE 1e-6f
#endif //SMALL_VALUE

// https://shikihuiku.github.io/post/projection_matrix/
// deviceZ = -Near / viewZ
// Nearは0.1mくらいにするので、viewZを100kmまで対応しても安全な値にした
#ifndef DEVICE_Z_MIN_VALUE
#define DEVICE_Z_MIN_VALUE 1e-7f
#endif //DEVICE_Z_FURTHEST

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
	// 初期値が-1というのは問題がある気がするが、使う側で気を付ける
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

