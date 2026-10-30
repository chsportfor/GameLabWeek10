#pragma once

#include "EnginePCH.h"
#include "Render/Vertex.h"

#include <fbxsdk.h>

struct FFbxMeshInfo	// Fbx에서 Mesh의 정점, index 정보를 담는 구조체
{
	FString Path;
	TArray<FVertex> Vertices;
	TArray<uint32> Indices;
};

struct FFbxIndex	// 중복 정점 체크에 사용되는 Key 값
{
	int32 ControlPointIndex = -1;

	float NormalX = 0;
	float NormalY = 0;
	float NormalZ = 0;

	float U = 0;
	float V = 0;

	bool operator==(const FFbxIndex&) const = default;
};

struct FFbxIndexHash	// Key 검사 빠르게 진행하기 위한 Hash
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

struct FFbxImportResult	// Fbx의 Import 결과로 나오는 값 구조체
{
	TArray<FFbxMeshInfo> Meshes;
};

struct FFbxBone
{

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