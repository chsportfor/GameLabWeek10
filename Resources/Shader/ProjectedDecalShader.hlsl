#pragma pack_matrix(row_major)
#include "ColorSpace.hlsli"
#include "DecalCommon.hlsli"

// DecalShader.hlsl과 같은 상수 버퍼를 쓴다. 투영 메시는 이미 월드 좌표라 DecalToClip에 ViewProjection이 들어온다.
cbuffer DecalConstants : register(b0)
{
    float4x4 DecalToClip;
    float4x4 InverseViewProjection;
    float4x4 WorldToDecal;
    float4 DecalColor;
    float2 ViewSize;
    float NormalThreshold; // 투영 메시는 생성할 때 각도로 걸러 여기서는 쓰지 않는다.
    float bHasNormalTexture; // 1이면 노말 맵을 쓴다
};

Texture2D<float> SceneDepth : register(t0);
Texture2D DecalTexture : register(t1);
Texture2D DecalNormalTexture : register(t2);
SamplerState DecalSampler : register(s0);

struct VSInput
{
    float3 Position : POSITION;
    float2 UV : TEXCOORD0;
    float3 Normal : NORMAL;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
    float3 Normal : NORMAL;
};

PSInput mainVS(VSInput Input)
{
    PSInput Output;
    Output.Position = mul(float4(Input.Position, 1.0f), DecalToClip);
    Output.UV = Input.UV;
    Output.Normal = Input.Normal;
    return Output;
}

FDecalOutput mainPS(PSInput Input)
{
    // Decal 패스에는 깊이 버퍼를 묶지 않으므로 Pre-depth와 직접 비교해 다른 물체에 가려진 조각을 버린다.
    float Depth = SceneDepth.Load(int3(Input.Position.xy, 0));
    clip(Depth - Input.Position.z);

    float4 Tex = DecalTexture.Sample(DecalSampler, Input.UV);
    float3 Color = SRGBToLinear(Tex.rgb) * SRGBToLinear(DecalColor.rgb);
    const float Alpha = Tex.a * DecalColor.a;

    FDecalOutput Output;
    Output.Color = float4(Color, Alpha);
    Output.Normal = float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (bHasNormalTexture > 0.5f)
    {
        // 언리얼처럼 법선도 색과 같은 알파로 덮는다.
        const float3 TangentNormal = DecalNormalTexture.Sample(DecalSampler, Input.UV).xyz * 2.0f - 1.0f;
        Output.Normal = EncodeDecalNormal(GetDecalWorldNormal(Input.Normal, WorldToDecal, TangentNormal), Alpha);
    }
    return Output;
}
