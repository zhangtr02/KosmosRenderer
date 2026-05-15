struct PushConstants
{
    float4x4 mvp;
    float4 baseColor;
    float4 lightDirectionIntensity;
};

[[vk::push_constant]]
PushConstants constants;

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION0;
    [[vk::location(1)]] float3 normal : NORMAL0;
    [[vk::location(2)]] float2 texCoord : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 texCoord : TEXCOORD0;
    [[vk::location(1)]] float4 baseColor : COLOR0;
    [[vk::location(2)]] float lighting : TEXCOORD1;
};

VertexOutput main(VertexInput input)
{
    VertexOutput output;
    output.position = mul(constants.mvp, float4(input.position, 1.0));
    output.texCoord = input.texCoord;
    output.baseColor = constants.baseColor;

    const float3 normal = normalize(input.normal);
    const float3 lightDirection = normalize(-constants.lightDirectionIntensity.xyz);
    const float diffuse = saturate(dot(normal, lightDirection));
    output.lighting = 0.18 + diffuse * constants.lightDirectionIntensity.w;
    return output;
}
