Texture2D<float4> BaseColorTexture : register(t0, space0);
Texture2D<float4> MetallicRoughnessTexture : register(t1, space0);
Texture2D<float4> NormalTexture : register(t2, space0);
SamplerState MaterialSampler : register(s3, space0);

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

struct FragmentInput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 texCoord : TEXCOORD0;
    [[vk::location(1)]] float3 worldPosition : TEXCOORD1;
    [[vk::location(2)]] float3 worldNormal : NORMAL0;
    [[vk::location(3)]] float3 worldTangent : TANGENT0;
    [[vk::location(4)]] float3 worldBitangent : TEXCOORD2;
};

static const float Pi = 3.14159265359;

float DistributionGGX(float nDotH, float roughness)
{
    const float a = roughness * roughness;
    const float a2 = a * a;
    const float denominator = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / max(Pi * denominator * denominator, 0.0001);
}

float GeometrySchlickGGX(float nDotV, float roughness)
{
    const float r = roughness + 1.0;
    const float k = (r * r) / 8.0;
    return nDotV / max(nDotV * (1.0 - k) + k, 0.0001);
}

float GeometrySmith(float nDotV, float nDotL, float roughness)
{
    return GeometrySchlickGGX(nDotV, roughness) * GeometrySchlickGGX(nDotL, roughness);
}

float3 FresnelSchlick(float hDotV, float3 f0)
{
    return f0 + (1.0 - f0) * pow(saturate(1.0 - hDotV), 5.0);
}

float4 main(FragmentInput input) : SV_Target0
{
    const float4 baseColorSample = BaseColorTexture.Sample(MaterialSampler, input.texCoord);
    const float4 metallicRoughnessSample = MetallicRoughnessTexture.Sample(MaterialSampler, input.texCoord);
    const float3 normalSample = NormalTexture.Sample(MaterialSampler, input.texCoord).xyz * 2.0 - 1.0;

    const float3 tangent = normalize(input.worldTangent);
    const float3 bitangent = normalize(input.worldBitangent);
    const float3 geometricNormal = normalize(input.worldNormal);
    const float3 normal = normalize(normalSample.x * tangent + normalSample.y * bitangent + normalSample.z * geometricNormal);

    const float3 albedo = baseColorSample.rgb * constants.baseColor.rgb;
    const float alpha = baseColorSample.a * constants.baseColor.a;
    const float metallic = saturate(constants.cameraPositionMetallic.w * metallicRoughnessSample.b);
    const float roughness = clamp(constants.lightColorRoughness.w * metallicRoughnessSample.g, 0.04, 1.0);

    const float3 viewDirection = normalize(constants.cameraPositionMetallic.xyz - input.worldPosition);
    const float3 lightDirection = normalize(-constants.lightDirectionIntensity.xyz);
    const float3 halfVector = normalize(viewDirection + lightDirection);
    const float3 radiance = constants.lightColorRoughness.rgb * constants.lightDirectionIntensity.w;

    const float nDotV = max(dot(normal, viewDirection), 0.0001);
    const float nDotL = max(dot(normal, lightDirection), 0.0);
    const float nDotH = max(dot(normal, halfVector), 0.0);
    const float hDotV = max(dot(halfVector, viewDirection), 0.0);

    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    const float3 fresnel = FresnelSchlick(hDotV, f0);
    const float distribution = DistributionGGX(nDotH, roughness);
    const float geometry = GeometrySmith(nDotV, nDotL, roughness);
    const float3 specular = (distribution * geometry * fresnel) / max(4.0 * nDotV * nDotL, 0.0001);

    const float3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * albedo / Pi;
    const float3 directLighting = (diffuse + specular) * radiance * nDotL;
    const float3 ambient = albedo * 0.03;

    return float4(ambient + directLighting, alpha);
}
