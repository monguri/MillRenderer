struct VSOutput
{
	float4 Position : SV_Position;
	uint InstanceID : SV_InstanceID;
};

float4 main(VSOutput input) : SV_TARGET
{
	return float4(
		float((input.InstanceID & 1) + 1) * 0.5f, // (instanceID % 2 + 1) / 2.0
		float((input.InstanceID & 3) + 1) * 0.25f, // (instanceID % 4 + 1) / 4.0
		float((input.InstanceID & 7) + 1) * 0.125f, // (instanceID % 8 + 1) / 8.0
		1.0f
	);
}