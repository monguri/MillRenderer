// SimpleMS‚Æ’è‹`‚ªç’·
struct VertexOutput
{
	float4 position : SV_Position;
	float3 normal : NORMAL;
	uint meshletIndex: MESHLET_INDEX;
};

float4 main(VertexOutput input) : SV_TARGET
{
	float3 color = float3(
		float(input.meshletIndex & 1),
		float(input.meshletIndex & 3) / 4,
		float(input.meshletIndex & 7) / 8
	);

	return float4(color, 1);
}