#include "EnginePCH.h"

#include "DecalRenderer.h"
#include "RenderCommand.h"
#include "RenderResourceManager.h"
#include "Renderer.h"
#include "Asset/AssetManager.h"


bool FDecalRenderer::Init(FRenderer* InRenderer)
{

	Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/DecalShader.hlsl");
	if (!Shader)
	{
		HTR_LOG(Error, "[Decal] shader not found");
		return false;
	}


	ConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FDecalConstants));

	PipelineState.Shader = Shader;
	PipelineState.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	PipelineState.BlendState = EBlendState::AlphaBlend;
	PipelineState.DepthStencilState = EDepthStencilState::Disabled;
	PipelineState.RasterizerState = ERasterizerState::SolidFront;

	return true;
}

float FDecalRenderer::CalculateScreenFadeAlpha(
	const FDecalSceneProxy* Proxy,
	const FVector& CameraPosition,
	const FMatrix& Projection,
	int32 ViewWidth)
{
	const float FadeScreenSize = Proxy->GetFadeScreenSize();
	const bool bOrthographic = Projection.M[3][3] != 0.0f;
	if (FadeScreenSize == 0.0f || bOrthographic)
	{
		return (1.0f);
	}

	const FMatrix& DecalToWorld = Proxy->GetDecalToWorld();

	const FVector DecalOrigin = DecalToWorld.TransformPosition(FVector::ZeroVector);
	const float Distance = FVector::Distance(CameraPosition, DecalOrigin);

	const float ScaleX = FVector(DecalToWorld.M[0][0], DecalToWorld.M[0][1], DecalToWorld.M[0][2]).Size();
	const float ScaleY = FVector(DecalToWorld.M[1][0], DecalToWorld.M[1][1], DecalToWorld.M[1][2]).Size();
	const float ScaleZ = FVector(DecalToWorld.M[2][0], DecalToWorld.M[2][1], DecalToWorld.M[2][2]).Size();
	const float Radius = std::max({ ScaleX, ScaleY, ScaleZ });

	const float CurrentScreenSize = Radius / Distance;

	const float FOVFactor = ((2.0f / Projection.M[1][0]) / ViewWidth) * 600.0f;
	const float FadeCoeff = FadeScreenSize * FOVFactor;
	const float FadeRange = FadeCoeff * 0.5f;

	return (std::clamp((CurrentScreenSize - FadeCoeff) / FadeRange, 0.0f, 1.0f));
}

void FDecalRenderer::OnRender(
	FRenderer* Renderer,
	const TArray<FDecalSceneProxy*>& Decals,
	FTexture2D* DepthTexture,
	const FMatrix& ViewProjection,
	const FVector& CameraPosition,
	const FMatrix& Projection,
	const FViewportSettings& Viewport)
{
	if (!Renderer->EnsureDBuffer(Viewport.Width, Viewport.Height))
	{
		return;
	}

	FRenderingInfo DecalPass{};
	DecalPass.ViewportSetting = Viewport;
	FRenderingDesc ColorDesc{};
	ColorDesc.Texture = Renderer->GetDBuffer();
	ColorDesc.ClearValue.ColorClearValue = FVector4(0, 0, 0, 0);
	DecalPass.ColorRenderTargets.Add(ColorDesc);
	RenderCommand::BeginRenderPass(DecalPass);

	//정렬 순서 SortOrder → 텍스처 포인터 → 프록시 주소

	TArray<FDecalSceneProxy*> SortDecals = Decals;
	std::sort(SortDecals.begin(), SortDecals.end(), [](const FDecalSceneProxy* A, const FDecalSceneProxy* B) {
			if (A->GetSortOrder() != B->GetSortOrder())
			{
				return (A->GetSortOrder() < B->GetSortOrder());
			}
			if (A->GetDecalTexture() != B->GetDecalTexture())
			{
				return (A->GetDecalTexture() < B->GetDecalTexture());
			}
			return (A < B);
		});

	FDecalConstants DecalConstants = {};
	
	DecalConstants.InverseViewProjection = ViewProjection.Inverse();    // 루프 밖에서 한 번만 계산
	DecalConstants.DecalInfo.ViewSize = FVector2(static_cast<float>(Viewport.Width), static_cast<float>(Viewport.Height));

	RenderCommand::BindPipelineState(PipelineState);
	RenderCommand::BindSamplerState(0, ESamplerState::LinearClamp, EShaderBindFlagBits::Pixel);
	RenderCommand::BindShaderResource(0, DepthTexture, EShaderBindFlagBits::Pixel);
	UStaticMesh* Cube = UAssetManager::GetAssetByPath<UStaticMesh>("Cube");
	RenderCommand::BindMesh(Cube);

	for (FDecalSceneProxy* DecalSceneProxy : SortDecals)
	{
		const float ScreenAlpha = CalculateScreenFadeAlpha(DecalSceneProxy, CameraPosition, Projection, Viewport.Width);
		if (DecalSceneProxy->GetFadeAlpha() * ScreenAlpha <= 0.0f)
		{
			continue;
		}


		DecalConstants.DecalToClip = DecalSceneProxy->GetDecalToWorld() * ViewProjection;
		DecalConstants.DecalInfo.WorldToDecal = DecalSceneProxy->GetWorldToDecal();
		DecalConstants.DecalInfo.DecalColor = DecalSceneProxy->GetDecalColor();
		DecalConstants.DecalInfo.DecalColor.A *= DecalSceneProxy->GetFadeAlpha() * ScreenAlpha;


		RenderCommand::BindShaderResource(1, DecalSceneProxy->GetDecalTexture(), EShaderBindFlagBits::Pixel);
		RenderCommand::UpdateBufferData(ConstantBuffer.get(), &DecalConstants);
		RenderCommand::BindConstantBuffer(0, ConstantBuffer.get(), EShaderBindFlagBits::Pixel | EShaderBindFlagBits::Vertex);
		RenderCommand::DrawIndexed(36);
	}
	RenderCommand::UnbindShaderResource(0, EShaderBindFlagBits::Pixel);
	RenderCommand::UnbindShaderResource(1, EShaderBindFlagBits::Pixel);
	RenderCommand::EndRenderPass(DecalPass);
}
