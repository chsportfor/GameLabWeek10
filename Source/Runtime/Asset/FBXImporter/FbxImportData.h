#pragma once

#include "Render/SkeletalMeshData.h"
#include "Render/StaticMeshData.h"

struct FFbxStaticMeshPart
{
	FString NodeName;
	// 메시 데이터는 로컬 공간에 저장하고, 이 변환은 가져온 씬에서 노드를 배치할 때 사용한다.
	FMatrix NodeTransform = FMatrix::Identity;
	FStaticMeshData MeshData;
};

struct FFbxCookedData
{
	FSkeletalMeshData SkeletalMesh;
	TArray<FFbxStaticMeshPart> StaticMeshes;
};
