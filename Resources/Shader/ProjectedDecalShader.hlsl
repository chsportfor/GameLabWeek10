#pragma pack_matrix(row_major)
#include "ColorSpace.hlsli"

// DecalShader.hlsl과 같은 상수 버퍼를 쓴다. 투영 메시는 이미 월드 좌표라 DecalToClip에 ViewProjection이 들어온다.
cbuffer DecalConstants : register(b0)
{
    float4x4 DecalToClip;
    float4x4 InverseViewProjection;
    float4x4 WorldToDecal;
    float4 DecalColor;
    float2 ViewSize;
    float NormalThreshold; // 투영 메시는 생성할 때 각도로 걸러 여기서는 쓰지 않는다.
    float Padding;
};

Texture2D<float> SceneDepth : register(t0);
Texture2D DecalTexture : register(t1);
SamplerState DecalSampler : register(s0);

struct VSInput
{
    float3 Position : POSITION;
    float2 UV : TEXCOORD0;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
};

PSInput mainVS(VSInput Input)
{
    PSInput Output;
    Output.Position = mul(float4(Input.Position, 1.0f), DecalToClip);
    Output.UV = Input.UV;
    return Output;
}

float4 mainPS(PSInput Input) : SV_Target
{
    // Decal 패스에는 깊이 버퍼를 묶지 않으므로 Pre-depth와 직접 비교해 다른 물체에 가려진 조각을 버린다.
    float Depth = SceneDepth.Load(int3(Input.Position.xy, 0));
    clip(Depth - Input.Position.z);

    float4 Tex = DecalTexture.Sample(DecalSampler, Input.UV);
    float3 Color = SRGBToLinear(Tex.rgb) * SRGBToLinear(DecalColor.rgb);
    return float4(Color, Tex.a * DecalColor.a);
}
