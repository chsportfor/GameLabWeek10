#pragma once

#include "EnginePCH.h"
#include "SkinnedMeshComponent.h"
#include "Engine/SkeletonSceneProxy.h"
#include "Render/Mesh.h"

class SkeletalMeshComponent : public USkinnedMeshComponent
{
	FPrimitiveSceneProxy* CreateSceneProxy() override;

private:
	USkeletalMesh* SkeletalMesh = nullptr;
};
