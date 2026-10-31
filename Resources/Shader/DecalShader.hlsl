#pragma pack_matrix(row_major)
#include "ColorSpace.hlsli"

cbuffer DecalConstants : register(b0)
{
    float4x4 DecalToClip; // 박스 정점 → 클립 공간
    float4x4 InverseViewProjection; // NDC → 월드
    float4x4 WorldToDecal; // 월드 → Decal 로컬
    float4 DecalColor;
    float2 ViewSize; // 뷰의 픽셀 크기
    float2 Padding;
};

Texture2D<float> SceneDepth : register(t0);
Texture2D DecalTexture : register(t1);
SamplerState DecalSampler : register(s0);

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
};


float4 mainVS(float3 Position : POSITION) : SV_POSITION
{
    
    return mul(float4(Position * 2.0f, 1.0f), DecalToClip); // 큐브가 0.5라서 2를 곱한다.
}

float4 mainPS(float4 SvPosition : SV_POSITION) : SV_Target
{
    float Depth = SceneDepth.Load(int3(SvPosition.xy, 0));
    
    float2 ScreenUV = SvPosition.xy / ViewSize;
    float2 NDC = ScreenUV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
    float4 WorldPos = mul(float4(NDC, Depth, 1.0f), InverseViewProjection);
    WorldPos /= WorldPos.w;
    float3 WorldPosToDecal = mul(WorldPos, WorldToDecal).xyz;
    clip(1.0f - abs(WorldPosToDecal));
    
    float2 DecalUV = WorldPosToDecal.zy * 0.5f + 0.5f;
    float4 Tex = DecalTexture.Sample(DecalSampler, DecalUV);

    float3 Color = SRGBToLinear(Tex.rgb) * SRGBToLinear(DecalColor.rgb);
    return float4(Color, Tex.a * DecalColor.a);
}
