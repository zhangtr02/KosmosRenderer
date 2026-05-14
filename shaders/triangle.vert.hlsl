struct VertexOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

VertexOutput main(uint vertexIndex : SV_VertexID)
{
    float2 positions[3] = {
        float2(0.0, -0.55),
        float2(0.55, 0.45),
        float2(-0.55, 0.45),
    };

    float3 colors[3] = {
        float3(0.95, 0.28, 0.22),
        float3(0.20, 0.75, 0.36),
        float3(0.22, 0.42, 0.95),
    };

    VertexOutput output;
    output.position = float4(positions[vertexIndex], 0.0, 1.0);
    output.color = colors[vertexIndex];
    return output;
}
