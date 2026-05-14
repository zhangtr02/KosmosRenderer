struct FragmentInput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
};

float4 main(FragmentInput input) : SV_Target0
{
    return input.color;
}
