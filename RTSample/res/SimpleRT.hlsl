[shader("raygeneration")]
void rayGeneration()
{
}

struct Payload
{
	bool hit;
};

[shader("miss")]
void miss(inout Payload payload)
{
}

[shader("closesthit")]
void closestHit(inout Payload payload, in BuiltInTriangleIntersectionAttributes attrs)
{
}