#pragma once

#include "EnginePCH.h"
#include "SkinnedMeshComponent.h"
#include "Engine/SkeletonSceneProxy.h"
#include "Render/Mesh.h"

class USkeletalMeshComponent : public USkinnedMeshComponent
{
	DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)

	REFLECT_START(USkeletalMeshComponent)
		PROPERTY(SkeletalMesh)
	REFLECT_END()

public:
	FPrimitiveSceneProxy* CreateSceneProxy() override;
	USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh; }
	void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh);
	int32 GetNumMaterials() const override;
	FString GetMaterialSlotName(int32 SlotIndex) const override;
	UMaterial* GetDefaultMaterial(int32 SlotIndex) const override;
	void SubmitToRenderQueue(FRenderQueue& RenderQueue) override;

	// 부모 Proxy가 월드 Bounds를 계산할 때 스켈레탈 메시의 로컬 Bounds를 사용한다.
	FBox CalcLocalBounds() const override
	{
		return SkeletalMesh ? SkeletalMesh->GetRenderBounds() : USkinnedMeshComponent::CalcLocalBounds();
	}

private:
	USkeletalMesh* SkeletalMesh = nullptr;
};
