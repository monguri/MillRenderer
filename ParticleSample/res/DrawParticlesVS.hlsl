cbuffer CbCamera : register(b0)
{
	float4x4 ViewProj : packoffset(c0);
}

float4 main( float4 pos : POSITION ) : SV_POSITION
{
	return mul(ViewProj, pos);
}