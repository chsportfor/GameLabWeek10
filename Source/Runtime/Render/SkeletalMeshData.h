#pragma once

#include "EnginePCH.h"
#include "Render/Vertex.h"
#include "Math/Transform.h"

struct FSkeletalVertex
{
	FVertex Vertex;
	// FSkeletalMeshData::Bones 배열의 인덱스. Control Point 번호와는 별개다.
	uint32 BoneIndices[4] = {};
	float BoneWeights[4] = {};
};

struct FSkeletalBone
{
	FString Name;
	int32 ParentIndex = -1;
	FTransform RefPoseTransform;
	FMatrix InverseBindPose;
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
};
