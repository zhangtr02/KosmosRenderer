Texture2D<float4> SceneColorTexture : register(t0, space0);
SamplerState SceneColorSampler : register(s1, space0);

struct PushConstants
{
    float4 postParams;
};

[[vk::push_constant]]
PushConstants constants;

struct FragmentInput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 texCoord : TEXCOORD0;
};

float4 main(FragmentInput input) : SV_Target0
{
    const float3 hdrColor = max(SceneColorTexture.Sample(SceneColorSampler, input.texCoord).rgb, 0.0);
    const float exposure = max(constants.postParams.x, 0.001);
    const float gamma = max(constants.postParams.y, 0.001);

    const float3 toneMapped = 1.0 - exp(-hdrColor * exposure);
    const float3 gammaCorrected = pow(saturate(toneMapped), 1.0 / gamma);
    return float4(gammaCorrected, 1.0);
}
