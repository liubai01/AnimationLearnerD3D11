// PhongShader.hlsl

cbuffer ConstantBuffer : register(b0)
{
    matrix world;
    matrix view;
    matrix proj;
    float3 lightDir;
    uint targetBoneIndex; // 新增：指定骨骼索引
    float3 padding2;      // 对齐用，cbuffer大小必须是16字节的倍数
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
    float  targetBoneWeight : TEXCOORD1; // 新增：目标骨骼权重
};

PSInput VSMain(VSInput input)
{
    PSInput output;
    output.targetBoneWeight = 0.0f;

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

             // 如果是目标骨骼，记录权重
            if (idx == targetBoneIndex)
            {
                output.targetBoneWeight += w;
            }
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

    output.targetBoneWeight = saturate(output.targetBoneWeight);

    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 lightDirection = normalize(lightDir);
    float3 normal = normalize(input.normal);
    float diffuseIntensity = saturate(dot(normal, -lightDirection));
    float3 diffuseColor = float3(1.0f, 1.0f, 1.0f) * diffuseIntensity;
    float3 ambientColor = float3(0.1f, 0.1f, 0.1f);

    float3 baseColor = ambientColor + diffuseColor;

    // 根据目标骨骼权重混合红色，权重越大红色越鲜艳
    float3 redColor = float3(1.0f, 0.0f, 0.0f);

    // 混合颜色，权重为混合因子
    float3 finalColor = lerp(baseColor, redColor, input.targetBoneWeight * 0.5f);

    return float4(finalColor, 1.0f);
}