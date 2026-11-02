#pragma once

#include "MeshComponent.h"

class USkeletalMesh;
class FSkeletalMeshData;

class USkeletalMeshComponent : public UMeshComponent
{
	DECLARE_CLASS(USkeletalMeshComponent, UMeshComponent)

	REFLECT_START(USkeletalMeshComponent)
		PROPERTY(SkeletalMesh)
	REFLECT_END()

public:
	USkeletalMeshComponent();
	virtual ~USkeletalMeshComponent();

	virtual void BeginPlay();
	virtual void TickComponent(float DeltaTime);

	const FSkeletalMeshData* GetSkeletalMeshData() const;

	void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh);
	USkeletalMesh* GetSkeletalMesh() const;

	virtual int32 GetNumMaterials() const override;
	virtual FString GetMaterialSlotName(int32 SlotIndex) const override;
	virtual UMaterial* GetDefaultMaterial(int32 SlotIndex) const override;

	void SubmitToRenderQueue(FRenderQueue& Queue) override;
	void SubmitToRenderQueue(FRenderQueue& Queue, uint32 LODIndex);

private:
	USkeletalMesh* SkeletalMesh = nullptr;
};