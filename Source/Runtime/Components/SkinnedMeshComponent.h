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
	// 부모 본 기준의 현재 자세. 원본 RefPoseTransform은 수정하지 않는다.
	const FTransform* GetBoneLocalTransform(int32 BoneIndex) const;
	bool SetBoneLocalTransform(int32 BoneIndex, const FTransform& InTransform);
	int32 GetNumMaterials() const override;
	void UpdateSkinning();
	void ResetPoseToBindPose();
	bool LineTraceComponentLocal(const FRay& LocalRay, float& OutT) override;
	FOutlineGeometry GetOutlineGeometry() const override;
	FString GetMaterialSlotName(int32 SlotIndex) const override;
	UMaterial* GetDefaultMaterial(int32 SlotIndex) const override;
	FVertexBuffer* GetSkinnedVertexBuffer() const{return SkinnedVertexBuffer.get();}
	// 투영 Decal이 현재 자세의 삼각형을 자를 때 쓴다. 컴포넌트 로컬 좌표다.
	const TArray<FVertex>& GetSkinnedVertices() const { return SkinnedVertices; }
	const TArray<uint32>& GetSkinnedIndices() const { return SkinnedIndices; }

	// 부모 Proxy가 월드 Bounds를 계산할 때 스켈레탈 메시의 로컬 Bounds를 사용한다.
	FBox CalcLocalBounds() const override
	{
		if (bHasSkinnedBounds) return SkinnedBounds;
		return SkeletalMesh ? SkeletalMesh->GetRenderBounds() : Super::CalcLocalBounds();
	}
	
private:
	USkeletalMesh* SkeletalMesh = nullptr;
	TArray<FTransform> CurrentLocalTransforms;
	TArray<FMatrix> CurrentGlobalMatrices;
	TUniquePtr<FVertexBuffer> SkinnedVertexBuffer;
	TArray<FVertex> SkinnedVertices;
	TArray<uint32> SkinnedIndices;
	FBox SkinnedBounds;
	bool bHasSkinnedBounds = false;
};
