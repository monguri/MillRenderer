cbuffer CbCamera : register(b0)
{
	float4x4 ViewProj : packoffset(c0);
}

float4 main(float4 pos : POSITION, uint instanceID : SV_InstanceID) : SV_POSITION
{
	float4 worldPos = float4(pos.xy, pos.z + 0.1 * instanceID, pos.w);
	return mul(ViewProj, worldPos);
}