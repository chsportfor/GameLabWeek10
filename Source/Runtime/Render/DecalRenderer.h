#pragma once
#include "Render/Buffer.h"
#include "Render/PipelineState.h"
#include "Core/EngineString.h"
#include "Render/Renderer.h"
#include "DecalInfo.h"
#include "Engine/DecalSceneProxy.h"

struct FDecalConstants
{
	FMatrix		DecalToClip;
	FMatrix		InverseViewProjection;
	FDecalInfo	DecalInfo;
};


class FDecalRenderer
{
public:
	FDecalRenderer() = default;
	~FDecalRenderer() = default;


	FDecalRenderer(const FDecalRenderer&) = delete;
	FDecalRenderer operator=(const FDecalRenderer&) = delete;

	bool Init(FRenderer* InRenderer);

	float CalculateScreenFadeAlpha(
		const FDecalSceneProxy* Proxy,
		const FVector& CameraPosition,
		const FMatrix& Projection,
		int32 ViewWidth
	);

	void OnRender(
		FRenderer* Renderer,
		const TArray<FDecalSceneProxy*>& Decals,
		FTexture2D* DepthTexture,
		const FMatrix& ViewProjection,
		const FVector& CameraPosition,
		const FMatrix& Projection,
		const FViewportSettings& Viewport,
		bool bShowDecals
	);

private:
	// bAdditive와 값이 같은 Decal만 현재 렌더 타깃에 그린다.
	void DrawDecals(
		const TArray<FDecalSceneProxy*>& SortDecals,
		FDecalConstants& DecalConstants,
		const FMatrix& ViewProjection,
		const FVector& CameraPosition,
		const FMatrix& Projection,
		int32 ViewWidth,
		bool bAdditive
	);

	FShaderProgram* Shader = nullptr;
	FPipelineState PipelineState;
	FPipelineState AdditivePipelineState;
	TUniquePtr<FConstantBuffer> ConstantBuffer;
};