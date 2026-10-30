#include "EnginePCH.h"
#include "FbxImporter.h"

FFbxImporter::FFbxImporter()
{
	pFbxManager = FbxManager::Create();	// FbxManager 생성
	FbxIOSettings* FBXSetting =  FbxIOSettings::Create(pFbxManager, IOSROOT);	// 설정 Root 이름 세팅 후 IOSetting 객체 생성
	pFbxManager->SetIOSettings(FBXSetting);	// FbxManager에 IOSetting 설정
}

FFbxImporter::~FFbxImporter()
{
	pFbxManager->Destroy();
}

void FFbxImporter::ImportFBX(const FString& Path, const FString& SceneName)
{
	FbxImporter* pFbxImporter = FbxImporter::Create(pFbxManager, "");	// FbxImporter 객체 생성
	FbxIOSettings* pFbxIOSetting = pFbxManager->GetIOSettings(); 
	pFbxImporter->Initialize(Path.c_str(),-1,pFbxIOSetting);	// FbxImporter 초기 Initialize

	FbxScene* pFbxScene = FbxScene::Create(pFbxManager, SceneName.c_str()); // FbxScene 객체 생성
	pFbxImporter->Import(pFbxScene);	// FbxImporter Initialize의 파일에서 Scene 정보 가져와 pbxScene에 채움
	pFbxImporter->Destroy();	// 다 채웠으므로 Importer는 Destroy

	FbxNode* pFbxRootNode = pFbxScene->GetRootNode();
	if (pFbxRootNode)
	{
		for (int i = 0; i < pFbxRootNode->GetChildCount(); i++)	// RootNode 내려가면서 정보 읽어서 Parsing
		{
			ProcessNode(pFbxRootNode->GetChild(i));
		}
	}
}

void FFbxImporter::ProcessNode(FbxNode* Node)
{
	const char* pNodeName = Node->GetName();
	for (int i = 0; i < Node->GetNodeAttributeCount(); i++)
	{
		FbxNodeAttribute* NodeAttri = Node->GetNodeAttributeByIndex(i);
		if (NodeAttri->GetAttributeType() == FbxNodeAttribute::EType::eMesh)
		{
			FbxMesh* Mesh = static_cast<FbxMesh*>(NodeAttri);
			ProcessMesh(Node, Mesh);
		}
		else if (NodeAttri->GetAttributeType() == FbxNodeAttribute::EType::eSkeleton)
		{
			FbxSkeleton* Skeleton = static_cast<FbxSkeleton*>(NodeAttri);
			ProcessSkeleton(Node, Skeleton);
		}
		else
		{
			ProcessEtc(Node);
		}
	}
}

void FFbxImporter::ProcessMesh(FbxNode* Node, FbxMesh* Mesh)
{
	
}

void FFbxImporter::ProcessSkeleton(FbxNode* Node, FbxSkeleton* Skeleton)
{

}

void FFbxImporter::ProcessEtc(FbxNode* Node)
{

}