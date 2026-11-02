#include "EnginePCH.h"
#include "DecalSceneProxy.h"

FDecalSceneProxy::FDecalSceneProxy(const UDecalComponent* InComponent) :
	Component(InComponent) {}

void FDecalSceneProxy::UpdateTransform()
{
	DecalToWorld = Component->GetTransformIncludingDecalSize();
	WorldToDecal = DecalToWorld.Inverse();
	const FBox LocalBounds{ FVector(-1.0f, -1.0f, -1.0f), FVector(1.0f, 1.0f, 1.0f) };
	Bounds = MakeWorldBounds(LocalBounds.GetWorldAABB(DecalToWorld));
}

void FDecalSceneProxy::UpdateRenderState()
{
	DecalTexture = Component->GetDecalTexture();
	DecalColor = Component->GetDecalColor();
	SortOrder = Component->GetSortOrder();
	bAdditive = Component->IsAdditive();
}
