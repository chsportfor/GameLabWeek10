#pragma once

#include "EnginePCH.h"

#include <fbxsdk.h>

class FFbxImporter
{
public:
	FFbxImporter();
	~FFbxImporter();
	void ImportFBX(const FString& Path, const FString& SceneName);
	void ProcessNode(FbxNode* ChildNode);

private:
	FbxManager* pFbxManager;
}; 