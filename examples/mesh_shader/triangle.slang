module triangle;

struct MeshVertex {
    float4 position : SV_Position;
    float3 color    : COLOR0;
};

[shader("amplification")]
[numthreads(1, 1, 1)]
void taskMain()
{
    uint unused_payload = 0;
    DispatchMesh(1, 1, 1, unused_payload);
}

[shader("mesh")]
[numthreads(1, 1, 1)]
[outputtopology("triangle")]
void meshMain(out vertices MeshVertex verts[3], out indices uint3 triangles[1])
{
    SetMeshOutputCounts(3, 1);

    verts[0] = MeshVertex(float4(-0.5, 0.5, 0.0, 1.0), float3(1.0, 0.0, 0.0));
    verts[1] = MeshVertex(float4(0.5, 0.5, 0.0, 1.0), float3(0.0, 1.0, 0.0));
    verts[2] = MeshVertex(float4(0.0, -0.5, 0.0, 1.0), float3(0.0, 0.0, 1.0));

    triangles[0] = uint3(0, 1, 2);
}

[shader("fragment")]
float4 fragmentMain(MeshVertex input):
    SV_Target0
{
    return float4(input.color, 1.0);
}
