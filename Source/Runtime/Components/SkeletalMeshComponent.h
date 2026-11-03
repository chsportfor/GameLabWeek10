#pragma once

#include "MeshComponent.h"
#include "Render/SkeletalMesh.h"

struct FSkeletalMeshData;

class USkeletalMeshComponent : public UMeshComponent
{
	DECLARE_CLASS(USkeletalMeshComponent, UMeshComponent)

	REFLECT_START(USkeletalMeshComponent)
		PROPERTY(SkeletalMesh)
	REFLECT_END()

public:
	USkeletalMeshComponent();
	virtual ~USkeletalMeshComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime) override;

	const FSkeletalMeshData* GetSkeletalMeshData() const;

	void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh);
	USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh; }

	virtual int32 GetNumMaterials() const override;
	virtual FString GetMaterialSlotName(int32 SlotIndex) const override;
	virtual UMaterial* GetDefaultMaterial(int32 SlotIndex) const override;

	void SubmitToRenderQueue(FRenderQueue& Queue) override;
	void SubmitToRenderQueue(FRenderQueue& Queue, uint32 LODIndex);

	virtual FBox CalcLocalBounds() const override;
	virtual bool LineTraceComponentLocal(const FRay& LocalRay, float& OutT) override;

	void ResetPoseToBindPose();
	void UpdateSkinningMatrices();
	const TArray<FTransform>& GetBoneTransforms() const { return BoneTransforms; }
	const TArray<FMatrix>& GetSkinningMatrices() const { return SkinningMatrices; }

private:
	USkeletalMesh* SkeletalMesh = nullptr;
	TArray<FTransform> BoneTransforms;
	TArray<FMatrix> SkinningMatrices;

	bool bSkinningDirty = true;

	// CPU Skinning을 위한 변형된 정점 데이터
	TArray<FVertex> DeformedVertices;

	TUniquePtr<FVertexBuffer> DynamicVertexBuffer;
	void DeformVerticesCPU();
};