cbuffer CbCamera : register(b0)
{
	float4x4 ViewProj : packoffset(c0);
}

float4 main(uint instanceID : SV_InstanceID) : SV_POSITION
{
	float4 worldPos = float4(0, 0, 0.1 * instanceID, 1);
	return mul(ViewProj, worldPos);
}