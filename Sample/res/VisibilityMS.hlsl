struct VertexData
{
	float4 position : SV_Position;
};

struct PrimitiveData
{
	uint primitiveID : SV_PrimitiveID;
};

[numthreads(128, 1, 1)]
[OutputTopology("triangle")]
void main
(
	uint gid : SV_GroupID,
	uint gtid : SV_GroupThreadID,
	out vertices VertexData outVerts[64],
	out indices uint3 outTriIndices[126],
	out primitives PrimitiveData outPrims[126]
)
{
	// TODO: ‰¼
	SetMeshOutputCounts(64, 126);


	if (gtid < 64)
	{
		VertexData v;
		v.position = float4(0, 0, 0, 1);
		outVerts[gtid] = v;
	}

	if (gtid < 126)
	{
		outTriIndices[gtid] = uint3(0, 0, 0);
	}

	if (gtid < 126)
	{
		PrimitiveData p;
		p.primitiveID = gtid;
		outPrims[gtid] = p;
	}
}