// PhongShader.hlsl

cbuffer ConstantBuffer : register(b0)
{
    matrix world;
    matrix view;
    matrix proj;
    float3 lightDir;
    float padding; // 对齐用
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD0;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD0;
};

PSInput VSMain(VSInput input)
{
    PSInput output;

    // 计算变换后的位置
    float4 worldPos = mul(float4(input.position, 1.0f), world);
    float4 viewPos = mul(worldPos, view);
    output.position = mul(viewPos, proj);

    // 变换法线（只乘以世界矩阵的上3x3部分）
    float3 worldNormal = normalize(mul(input.normal, (float3x3)world));
    output.normal = worldNormal;

    output.texcoord = input.texcoord;

    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    // 归一化光照方向
    float3 lightDirection = normalize(lightDir);

    // 归一化法线
    float3 normal = normalize(input.normal);

    // 计算漫反射光照强度
    float diffuseIntensity = saturate(dot(normal, -lightDirection));

    // 漫反射颜色（白色）
    float3 diffuseColor = float3(1.0f, 1.0f, 1.0f) * diffuseIntensity;

    // 环境光
    float3 ambientColor = float3(0.1f, 0.1f, 0.1f);

    float3 finalColor = ambientColor + diffuseColor;

    return float4(finalColor, 1.0f);
}