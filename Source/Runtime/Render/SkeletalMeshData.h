#pragma once

#include "EnginePCH.h"
#include "Render/Vertex.h"
#include "Math/Transform.h"

struct FSkeletalVertex
{
	FVertex Vertex;
	uint32 BoneIndices[4] = {};	// 해당 배열 내부의 Index는 FSkeletalMeshData의 Bones 배열의 Index
	float BoneWeights[4] = {};
};

struct FSkeletalBone
{
	FString Name;
	int32 ParentIndex = -1;
	FTransform RefPoseTransform;
};

struct FSkeletalMaterialSlot
{
	FString Name;
	FVector4 BaseColor = FVector4(1, 1, 1, 1);
	FString BaseColorTexturePath;
	FString NormalTexturePath;
};

struct FSkeletalMeshSection
{
	uint32 StartIndex = 0;
	uint32 IndexCount = 0;
	uint32 MaterialSlotIndex = 0;
};

struct FSkeletalMesh
{
	TArray<FSkeletalVertex> Vertices;
	TArray<uint32> Indices;
	TArray<FSkeletalMeshSection> Sections;
	TArray<FSkeletalMaterialSlot> MaterialSlots;
};

// 메시 파트들과 공통 본 목록.
struct FSkeletalMeshData
{
	TArray<FSkeletalMesh> Meshes;
	TArray<FSkeletalBone> Bones;
	TArray<FMatrix> InverseBindPoseMat;
};
