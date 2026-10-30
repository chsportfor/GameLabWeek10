#include "EnginePCH.h"
#include "Components/DecalComponent.h"
#include "Engine/World.h"
#include "Asset/AssetManager.h"
#include "Render/LineBatcher.h"

UDecalComponent::UDecalComponent()
{
}

UTexture2D* UDecalComponent::GetDecalTexture() const
{
	return DecalTexture ? DecalTexture : UAssetManager::GetAssetByPath<UTexture2D>("WhiteTexture");
}

void UDecalComponent::SetDecalTexture(UTexture2D* InDecalTexture)
{
	DecalTexture = InDecalTexture;
}

void UDecalComponent::SetDecalColor(const FLinearColor& InDecalColor)
{
	DecalColor = InDecalColor;
}

void UDecalComponent::SetSortOrder(int32 InSortOrder)
{
	SortOrder = InSortOrder;
}

FMatrix UDecalComponent::GetTransformIncludingDecalSize() const   // 프록시와 셰이더에서 사용
{
	FTransform SizeTransform;
	SizeTransform.Scale = DecalSize;
	return SizeTransform.GetLocalMatrix() * GetWorldMatrix();
}

void UDecalComponent::DrawDebug(FLineBatcher* LineBatcher) const
{
	if (LineBatcher == nullptr) return;

	const FMatrix BoxMatrix = GetTransformIncludingDecalSize();
	const FVector4 Color(0.0f, 0.97f, 1.0f, 1.0f);

	FVector Corners[8];
	for (int32 Index = 0; Index < 8; ++Index)
	{
		const FVector LocalCorner(
			(Index & 1) ? 1.0f : -1.0f,
			(Index & 2) ? 1.0f : -1.0f,
			(Index & 4) ? 1.0f : -1.0f);
		Corners[Index] = BoxMatrix.TransformPosition(LocalCorner);
	}

	for (int32 Index = 0; Index < 8; ++Index)
	{
		for (int32 Axis = 1; Axis <= 4; Axis <<= 1)
		{
			if ((Index & Axis) == 0)
				LineBatcher->AddLine(Corners[Index], Corners[Index | Axis], Color);
		}
	}
}

FDecalSceneProxy* UDecalComponent::CreateSceneProxy() const
{
	return new FDecalSceneProxy(this);
}

bool UDecalComponent::ShouldCreateRenderState() const
{
	return (true);
}

void UDecalComponent::CreateRenderState()
{
	Super::CreateRenderState();
	GetWorld()->GetScene().AddDecal(this);
}

void UDecalComponent::DestroyRenderState()
{
	GetWorld()->GetScene().RemoveDecal(this);
	Super::DestroyRenderState();
}

void UDecalComponent::Serialize(json& Handle, bool bIsLoading)
{
	Super::Serialize(Handle, bIsLoading);
}