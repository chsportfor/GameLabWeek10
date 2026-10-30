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
	FbxGeometryConverter Converter(pFbxManager);	// Polygon 삼각형화 하기 위한 Geometry Converter

	FbxScene* pFbxScene = FbxScene::Create(pFbxManager, SceneName.c_str()); // FbxScene 객체 생성
	pFbxImporter->Import(pFbxScene);   // FbxImporter Initialize의 파일에서 Scene 정보 가져와 pbxScene에 채움
	bool bTriangulated = Converter.Triangulate(pFbxScene, true);	// 삼각형화 진행
	if (!bTriangulated)
	{
		return;
	}
	
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
	const char* pNodeName = Node->GetName();	// Node 이름 저장
	for (int i = 0; i < Node->GetNodeAttributeCount(); i++)	// 한 Node에 여러 Attribute가 있을 수 있으므로 전체를 순회
	{
		FbxNodeAttribute* NodeAttri = Node->GetNodeAttributeByIndex(i);
		if (NodeAttri->GetAttributeType() == FbxNodeAttribute::EType::eMesh)	// Mesh 일경우 FbxMesh로 Cast 후 ProcessMesh
		{
			FbxMesh* Mesh = static_cast<FbxMesh*>(NodeAttri);
			ProcessMesh(Node, Mesh);
		}
		else if (NodeAttri->GetAttributeType() == FbxNodeAttribute::EType::eSkeleton)	// Skeleton 일경우 FbxSkeleton로 Cast 후 ProcessSkeleton
		{
			FbxSkeleton* Skeleton = static_cast<FbxSkeleton*>(NodeAttri);
			ProcessSkeleton(Node, Skeleton);
		}
		else
		{
			ProcessEtc(Node);	// 그외의 경우 ProcessEtc
		}
	}

	for (int i = 0; i < Node->GetChildCount(); ++i)	// 연결된 자식에 대해서 재귀적으로 순회
	{
		ProcessNode(Node->GetChild(i));
	} 
}

void FFbxImporter::ProcessMesh(FbxNode* Node, FbxMesh* Mesh)
{
	FFbxMeshInfo MeshInfo;
	for (int i = 0; i < Mesh->GetPolygonCount(); i++)	// 전체 삼각형 개수에 대하여 순회
	{
		FbxStringList UVSetNames;
		Mesh->GetUVSetNames(UVSetNames);
		if (Mesh->GetPolygonSize(i) == 3)
		{
			for (int j = 0; j < 3; j++)
			{
				int VertexIndex = Mesh->GetPolygonVertex(i, j);
				FbxVector4 FbxVertex = Mesh->GetControlPointAt(VertexIndex);
				FVertex Vertex;
				FbxVector4 Normal;
				FbxVector2 UV;
				bool bUnmapped = false;
				Mesh->GetPolygonVertexNormal(i, j, Normal);
				Mesh->GetPolygonVertexUV(i, j, UVSetNames.GetStringAt(0), UV, bUnmapped);
				Vertex.Position = FVector(static_cast<float>(FbxVertex[0]), static_cast<float>(FbxVertex[1]), static_cast<float>(FbxVertex[2]));
				Vertex.Normal = FVector(static_cast<float>(Normal[0]), static_cast<float>(Normal[1]), static_cast<float>(Normal[2]));
				Vertex.UV = FVector2(static_cast<float>(UV[0]),static_cast<float>(UV[1]));
				MeshInfo.Vertices.Add(Vertex);
			}
		}
	}
}

void FFbxImporter::ProcessSkeleton(FbxNode* Node, FbxSkeleton* Skeleton)
{

}

void FFbxImporter::ProcessEtc(FbxNode* Node)
{

}