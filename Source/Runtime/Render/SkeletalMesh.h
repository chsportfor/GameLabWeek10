#pragma once

#include "Asset/RenderAsset.h"
#include "Render/Buffer.h"
#include "Render/SkeletalMeshData.h"

class UMaterial;

class USkeletalMesh : public URenderAsset
{
	DECLARE_CLASS(USkeletalMesh, URenderAsset)
public:
	USkeletalMesh();
	virtual ~USkeletalMesh() override;

	FSkeletalMeshData MeshData;

	// 머티리얼 슬롯별 런타임 머티리얼 인스턴스
	TArray<UMaterial*> Materials;

	TUniquePtr<FVertexBuffer> VertexBuffer;
	TUniquePtr<FIndexBuffer> IndexBuffer;

	const FSkeletalMeshData& GetMeshData() const { return MeshData; }
	UMaterial* GetMaterial(uint32 SlotIndex) const;

	const FBox& GetBounds() const { return MeshData.AABB; }
	uint32 GetNumBones() const { return static_cast<uint32>(MeshData.Bones.Num()); }
	const TArray<FBoneInfo>& GetBones() const { return MeshData.Bones; }
	const TArray<FMatrix>& GetInverseBindPoseMatrices() const { return MeshData.InverseBindPoseMatrices; }
};
