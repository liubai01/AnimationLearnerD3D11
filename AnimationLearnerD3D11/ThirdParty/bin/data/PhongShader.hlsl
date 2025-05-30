// PhongShader.hlsl

cbuffer ConstantBuffer : register(b0)
{
    matrix world;
    matrix view;
    matrix proj;
    float3 lightDir;
    float padding; // 对齐用
};

cbuffer BoneBuffer : register(b1)
{
    matrix boneMatrices[128];
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD0;
    uint4 boneIndices : BONEINDICES;
    float4 boneWeights : BONEWEIGHTS;
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

    // 骨骼蒙皮变换
    float4 skinnedPos = float4(0,0,0,0);
    float3 skinnedNormal = float3(0,0,0);

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        uint idx = input.boneIndices[i];
        float w = input.boneWeights[i];
        if (w > 0)
        {
            // 位置
            skinnedPos += mul(float4(input.position, 1.0f), boneMatrices[idx]) * w;

            // 法线（只用上3x3，且不加平移）
            float3 n = mul(input.normal, (float3x3)boneMatrices[idx]);
            skinnedNormal += n * w;
        }
    }

    // skinnedNormal 归一化
    skinnedNormal = normalize(skinnedNormal);

    // 世界变换
    float4 worldPos = mul(skinnedPos, world);
    float3 worldNormal = normalize(mul(skinnedNormal, (float3x3)world));

    // 视图投影
    float4 viewPos = mul(worldPos, view);
    output.position = mul(viewPos, proj);

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