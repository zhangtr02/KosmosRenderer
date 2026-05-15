struct PushConstants
{
    float4x4 mvp;
    float4x4 model;
    float4x4 lightMvp;
    float4 baseColor;
    float4 lightDirectionIntensity;
    float4 cameraPositionMetallic;
    float4 lightColorRoughness;
};

[[vk::push_constant]]
PushConstants constants;

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION0;
};

float4 main(VertexInput input) : SV_Position
{
    return mul(constants.mvp, float4(input.position, 1.0));
}
