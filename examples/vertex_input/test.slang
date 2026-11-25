module test;

struct VertexInput {
    float3 position : POSITION0;
    float3 color    : COLOR0;
};

struct VertexOutput {
    float4 position : SV_Position;
    float3 color    : COLOR0;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    return VertexOutput(float4(input.position, 1.0), input.color);
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input):
    SV_Target0
{
    return float4(input.color, 1.0);
}
