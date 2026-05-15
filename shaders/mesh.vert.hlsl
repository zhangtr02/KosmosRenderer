struct PushConstants
{
    float4x4 mvp;
    float4x4 model;
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
    [[vk::location(1)]] float3 normal : NORMAL0;
    [[vk::location(2)]] float2 texCoord : TEXCOORD0;
    [[vk::location(3)]] float4 tangent : TANGENT0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 texCoord : TEXCOORD0;
    [[vk::location(1)]] float3 worldPosition : TEXCOORD1;
    [[vk::location(2)]] float3 worldNormal : NORMAL0;
    [[vk::location(3)]] float3 worldTangent : TANGENT0;
    [[vk::location(4)]] float3 worldBitangent : TEXCOORD2;
};

VertexOutput main(VertexInput input)
{
    VertexOutput output;
    const float4 worldPosition = mul(constants.model, float4(input.position, 1.0));
    const float3 worldNormal = normalize(mul((float3x3)constants.model, input.normal));
    const float3 worldTangent = normalize(mul((float3x3)constants.model, input.tangent.xyz));
    const float3 worldBitangent = normalize(cross(worldNormal, worldTangent) * input.tangent.w);

    output.position = mul(constants.mvp, float4(input.position, 1.0));
    output.texCoord = input.texCoord;
    output.worldPosition = worldPosition.xyz;
    output.worldNormal = worldNormal;
    output.worldTangent = worldTangent;
    output.worldBitangent = worldBitangent;
    return output;
}
