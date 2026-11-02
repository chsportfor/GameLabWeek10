#include "EnginePCH.h"
#include "SkeletalMeshComponent.h"

FPrimitiveSceneProxy* SkeletalMeshComponent::CreateSceneProxy()
{
	return new FSkeletalMeshSceneProxy(this);
}