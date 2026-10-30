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

	FbxScene* pFbxScene = FbxScene::Create(pFbxScene, SceneName.c_str());	// FbxScene 객체 생성
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

void FFbxImporter::ProcessNode(FbxNode* ChildNode)
{

}