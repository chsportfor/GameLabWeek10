#pragma once

#include "Vertex.h"
#include "Math/BVH.h"
#include "Math/Box.h"
#include "Math/Transform.h"
#include "Core/EngineString.h"

struct FSkeletalVertex
{
	FVector Position;
	FVector Normal;
	FVector Tangent;
	FVector2 UV;

	// 일반적으로 한 정점에 최대 4개(혹은 8개)의 본이 영향을 준다
	uint8 BoneIndices[4] = { 0, 0, 0, 0 };         // 영향을 주는 본들의 번호
	float BoneWeights[4] = { 0.f, 0.f, 0.f, 0.f }; // 각 본의 영향력 - 합이 1.0이 되도록 정규화
};

struct FBoneInfo
{
	FString Name;           // 본 이름
	int32 ParentIndex = -1; // 부모 본의 인덱스 (루트 본은 -1)

	FTransform LocalTransform; // 바인드 포즈 상태에서 부모 기준 로컬 트랜스폼
};

struct FSkelMeshSection
{
	uint32 StartIndex = 0;
	uint32 IndexCount = 0;
	uint32 MaterialSlotIndex = 0;
};

struct FSkeletalMaterialSlot
{
	FString Name;
	FVector4 BaseColor = FVector4(1, 1, 1, 1);
	FString DiffuseTexturePath;
	FString NormalTexturePath{};
};
struct FSkeletalMeshData
{
	// 기하학 렌더링 데이터 (GPU 버퍼 생성용)
	TArray<FSkeletalVertex> Vertices;
	TArray<uint32> Indices;
	TArray<FSkelMeshSection> Sections;
	TArray<FSkeletalMaterialSlot> MaterialSlots;

	// 바인드 포즈 기준 바운딩 볼륨
	FBox AABB;

	// 골격(Skeleton) 및 스키닝용 역행렬 데이터
	TArray<FBoneInfo> Bones; // 본들의 계층 구조 및 이름 목록

	// 바인드 역행렬 - 바인드 포즈 정점을 바인드 포즈 본의 로컬 공간으로 되돌리는데 사용된다
	// SkinnedVertex = CurrentBoneTransform * InverseBindPoseMatrix * BindPoseVertex
	TArray<FMatrix> InverseBindPoseMatrices;

	bool Validate(FString& OutError) const
	{
		if (Vertices.IsEmpty())
		{
			OutError = "Vertices is empty";
			return false;
		}
		if (Indices.IsEmpty())
		{
			OutError = "Indices is empty";
			return false;
		}
		const uint32 VertexCount = static_cast<uint32>(Vertices.Num());
		for (uint32 Index : Indices)
		{
			if (Index >= VertexCount)
			{
				OutError = "Index out of range";
				return false;
			}
		}
		for (const auto& Section : Sections)
		{
			if (Section.StartIndex + Section.IndexCount > static_cast<uint32>(Indices.Num()))
			{
				OutError = "Section range out of bounds";
				return false;
			}
		}
		return true;
	}
};

