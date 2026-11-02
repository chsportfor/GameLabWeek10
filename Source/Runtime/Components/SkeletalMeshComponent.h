#pragma once

#include "EnginePCH.h"
#include "SkinnedMeshComponent.h"
#include "Engine/SkeletonSceneProxy.h"
#include "Render/Mesh.h"

class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
	FPrimitiveSceneProxy* CreateSceneProxy() override;
	USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh; }
	void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh);

	// 부모 Proxy가 월드 Bounds를 계산할 때 스켈레탈 메시의 로컬 Bounds를 사용한다.
	FBox CalcLocalBounds() const override
	{
		return SkeletalMesh ? SkeletalMesh->GetRenderBounds() : USkinnedMeshComponent::CalcLocalBounds();
	}

private:
	USkeletalMesh* SkeletalMesh = nullptr;
};
