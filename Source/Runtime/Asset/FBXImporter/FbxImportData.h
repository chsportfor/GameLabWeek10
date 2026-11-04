#pragma once

#include "Render/SkeletalMeshData.h"
#include "Render/StaticMeshData.h"

struct FFbxStaticMeshPart
{
	FString NodeName;
	// The mesh data stays local. This transform places the node in the imported scene.
	FMatrix NodeTransform = FMatrix::Identity;
	FStaticMeshData MeshData;
};

struct FFbxCookedData
{
	FSkeletalMeshData SkeletalMesh;
	TArray<FFbxStaticMeshPart> StaticMeshes;
};
