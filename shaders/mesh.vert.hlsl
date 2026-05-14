struct PushConstants
{
    float4x4 mvp;
    float4 baseColor;
};

[[vk::push_constant]]
PushConstants constants;

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION0;
    [[vk::location(1)]] float3 color : COLOR0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
};

VertexOutput main(VertexInput input)
{
    VertexOutput output;
    output.position = mul(constants.mvp, float4(input.position, 1.0));
    output.color = float4(input.color * constants.baseColor.rgb, constants.baseColor.a);
    return output;
}
