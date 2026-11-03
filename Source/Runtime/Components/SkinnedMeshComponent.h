#pragma once

#include "Components/MeshComponent.h"
#include "Render/Mesh.h"

class USkinnedMeshComponent : public UMeshComponent
{
	DECLARE_CLASS(USkinnedMeshComponent, UMeshComponent)

	REFLECT_START(USkinnedMeshComponent)
		PROPERTY(SkeletalMesh)
	REFLECT_END()

public:
	USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh; }
	void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh);
	int32 GetNumMaterials() const override;
	void UpdateSkinning();
	FString GetMaterialSlotName(int32 SlotIndex) const override;
	UMaterial* GetDefaultMaterial(int32 SlotIndex) const override;
	FVertexBuffer* GetSkinnedVertexBuffer() const{return SkinnedVertexBuffer.get();}

	// 부모 Proxy가 월드 Bounds를 계산할 때 스켈레탈 메시의 로컬 Bounds를 사용한다.
	FBox CalcLocalBounds() const override { return SkeletalMesh ? SkeletalMesh->GetRenderBounds() : Super::CalcLocalBounds(); }
	
private:
	USkeletalMesh* SkeletalMesh = nullptr;
	TUniquePtr<FVertexBuffer> SkinnedVertexBuffer;
};
