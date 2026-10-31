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

void FDecalRenderer::OnRender(
	FRenderer* Renderer,
	const TArray<FDecalSceneProxy*>& Decals,
	FTexture2D* DepthTexture,
	const FMatrix& ViewProjection,
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
			if (A->GetDecalComponent()->GetSortOrder() != B->GetDecalComponent()->GetSortOrder())
			{
				return (A->GetDecalComponent()->GetSortOrder() < B->GetDecalComponent()->GetSortOrder());
			}
			if (A->GetDecalComponent()->GetDecalTexture() != B->GetDecalComponent()->GetDecalTexture())
			{
				return (A->GetDecalComponent()->GetDecalTexture() < B->GetDecalComponent()->GetDecalTexture());
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
		const FMatrix DecalToWorld = DecalSceneProxy->GetDecalComponent()->GetTransformIncludingDecalSize();
		DecalConstants.DecalToClip = DecalToWorld * ViewProjection;
		DecalConstants.DecalInfo.WorldToDecal = DecalToWorld.Inverse();
		DecalConstants.DecalInfo.DecalColor = DecalSceneProxy->GetDecalComponent()->GetDecalColor();

		
		
		RenderCommand::BindShaderResource(1, DecalSceneProxy->GetDecalComponent()->GetDecalTexture(), EShaderBindFlagBits::Pixel);
		RenderCommand::UpdateBufferData(ConstantBuffer.get(), &DecalConstants);
		RenderCommand::BindConstantBuffer(0, ConstantBuffer.get(), EShaderBindFlagBits::Pixel | EShaderBindFlagBits::Vertex);
		RenderCommand::DrawIndexed(36);
	}
	RenderCommand::UnbindShaderResource(0, EShaderBindFlagBits::Pixel);
	RenderCommand::UnbindShaderResource(1, EShaderBindFlagBits::Pixel);
	RenderCommand::EndRenderPass(DecalPass);
}
