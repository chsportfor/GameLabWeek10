#pragma once

#include "Components/DecalComponent.h"
#include "Math/Frustum.h"

#include "SceneManagement.h"

class FScene;

class FDecalSceneProxy
{
public:
	explicit FDecalSceneProxy(const UDecalComponent* InComponent);

	void UpdateTransform();
	void UpdateRenderState();

	FScene* GetScene() const { return Scene; }
	const FMatrix& GetDecalToWorld() const { return DecalToWorld; }
	const FMatrix& GetWorldToDecal() const { return WorldToDecal; }
	const FAABB& GetBounds() const { return Bounds; }
	UTexture2D* GetDecalTexture() const { return DecalTexture; }
	const FLinearColor& GetDecalColor() const { return DecalColor; }
	int32 GetSortOrder() const { return SortOrder; }
	bool IsAdditive() const { return bAdditive; }

private:
	friend class FScene;

	FScene* Scene = nullptr;
	const UDecalComponent* Component = nullptr;
	FMatrix DecalToWorld;
	FMatrix WorldToDecal;
	FAABB Bounds;
	UTexture2D* DecalTexture = nullptr;
	FLinearColor DecalColor = FLinearColor(1, 1, 1, 1);
	int32 SortOrder = 0;
	int32 PackedIndex = INDEX_NONE;
	bool bAdditive = false;
	bool bQueuedForUpdate = false;
	bool bRenderStateQueued = false;
};
