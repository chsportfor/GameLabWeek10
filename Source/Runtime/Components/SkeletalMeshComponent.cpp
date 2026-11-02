#include "EnginePCH.h"
#include "SkeletalMeshComponent.h"

FPrimitiveSceneProxy* USkeletalMeshComponent::CreateSceneProxy()
{
	return new FSkeletalMeshSceneProxy(this);
}

void USkeletalMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
	if (SkeletalMesh == InSkeletalMesh)
		return;

	SkeletalMesh = InSkeletalMesh;
	ClearOverrideMaterials();
	MarkRenderStateDirty();
	OnTransformDirty();
}

