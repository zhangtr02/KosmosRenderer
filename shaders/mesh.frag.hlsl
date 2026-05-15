Texture2D<float4> BaseColorTexture : register(t0, space0);
SamplerState BaseColorSampler : register(s1, space0);

struct FragmentInput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 texCoord : TEXCOORD0;
    [[vk::location(1)]] float4 baseColor : COLOR0;
    [[vk::location(2)]] float lighting : TEXCOORD1;
};

float4 main(FragmentInput input) : SV_Target0
{
    const float4 texel = BaseColorTexture.Sample(BaseColorSampler, input.texCoord);
    const float3 litColor = texel.rgb * input.baseColor.rgb * input.lighting;
    return float4(litColor, texel.a * input.baseColor.a);
}
