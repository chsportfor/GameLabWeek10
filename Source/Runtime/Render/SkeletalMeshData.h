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

// 메시 하나의 정점과 삼각형 연결 정보.
struct FSkeletalMesh
{
	TArray<FSkeletalVertex> Vertices;
	TArray<uint32> Indices;
};

// 메시 파트들과 공통 본 목록.
struct FSkeletalMeshData
{
	TArray<FSkeletalMesh> Meshes;
	TArray<FSkeletalBone> Bones;
	TArray<FMatrix> InverseBindPoseMat;
};
