#pragma once

#include "EnginePCH.h"
#include "Render/Vertex.h"

#include <fbxsdk.h>

struct FFbxMeshInfo
{
	FString Path;
	TArray<FVertex> Vertices;
	TArray<uint32> Indices;
};

struct FFbxIndex
{
	int32 ControlPointIndex = -1;

	float NormalX = 0;
	float NormalY = 0;
	float NormalZ = 0;

	float U = 0;
	float V = 0;

	bool operator==(const FFbxIndex&) const = default;
};

struct FFbxIndexHash
{
	size_t operator()(const FFbxIndex& Index) const
	{
		size_t Hash = std::hash<int32>{}(Index.ControlPointIndex);
		Hash = Hash * 31 + std::hash<float>{}(Index.NormalX);
		Hash = Hash * 31 + std::hash<float>{}(Index.NormalY);
		Hash = Hash * 31 + std::hash<float>{}(Index.NormalZ);
		Hash = Hash * 31 + std::hash<float>{}(Index.U);
		Hash = Hash * 31 + std::hash<float>{}(Index.V);
		return Hash;
	}
};

struct FFbxImportResult
{
	TArray<FFbxMeshInfo> Meshes;
};

class FFbxImporter
{
public:
	FFbxImporter();
	~FFbxImporter();
	FFbxImportResult ImportFBX(const FString& Path, const FString& SceneName);
	void ProcessNode(FbxNode* Node, FFbxImportResult& OutResult);
	FFbxMeshInfo ProcessMesh(FbxNode* Node, FbxMesh* Mesh);
	void ProcessSkeleton(FbxNode* Node, FbxSkeleton* Skeleton);
	void ProcessEtc(FbxNode* Node);

private:
	FbxManager* pFbxManager;
}; 