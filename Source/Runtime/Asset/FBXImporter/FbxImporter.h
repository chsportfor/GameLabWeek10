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

class FFbxImporter
{
public:
	FFbxImporter();
	~FFbxImporter();
	void ImportFBX(const FString& Path, const FString& SceneName);
	void ProcessNode(FbxNode* ChildNode);
	void ProcessMesh(FbxNode* Node, FbxMesh* Mesh);
	void ProcessSkeleton(FbxNode* Node, FbxSkeleton* Skeleton);
	void ProcessEtc(FbxNode* Node);

private:
	FbxManager* pFbxManager;
}; 