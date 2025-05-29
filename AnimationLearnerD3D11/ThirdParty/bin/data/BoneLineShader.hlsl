// Vertex Shader
cbuffer ConstantBuffer : register(b0)
{
    matrix world;
    matrix view;
    matrix proj;
};

struct VS_INPUT
{
    float3 pos : POSITION;
};

struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
};

VS_OUTPUT VSMain(VS_INPUT input)
{
    VS_OUTPUT output;
    float4 wpos = mul(float4(input.pos, 1.0f), world);
    float4 vpos = mul(wpos, view);
    output.pos = mul(vpos, proj);
    return output;
}

// Pixel Shader
float4 PSMain(VS_OUTPUT input) : SV_TARGET
{
    return float4(0, 1, 0, 1); // 绿色
}