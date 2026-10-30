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

FFbxImportResult FFbxImporter::ImportFBX(const FString& Path, const FString& SceneName)
{
	FFbxImportResult FbxResult;
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
			ProcessNode(pFbxRootNode->GetChild(i), FbxResult);
		}
	}
	return FbxResult;
}

void FFbxImporter::ProcessNode(FbxNode* Node, FFbxImportResult& OutResult)
{
	const char* pNodeName = Node->GetName();	// Node 이름 저장
	for (int i = 0; i < Node->GetNodeAttributeCount(); i++)	// 한 Node에 여러 Attribute가 있을 수 있으므로 전체를 순회
	{
		FbxNodeAttribute* NodeAttri = Node->GetNodeAttributeByIndex(i);
		if (NodeAttri->GetAttributeType() == FbxNodeAttribute::EType::eMesh)	// Mesh 일경우 FbxMesh로 Cast 후 ProcessMesh
		{
			FbxMesh* Mesh = static_cast<FbxMesh*>(NodeAttri);
			FFbxMeshInfo FbxMeshInfo = ProcessMesh(Node, Mesh);
			OutResult.Meshes.Add(FbxMeshInfo);
			
		}
		else if (NodeAttri->GetAttributeType() == FbxNodeAttribute::EType::eSkeleton)	// Skeleton 일경우 FbxSkeleton로 Cast 후 ProcessSkeleton
		{
			FbxSkeleton* Skeleton = static_cast<FbxSkeleton*>(NodeAttri);
			FFbxSkeletalMeshInfo FbxSkeletalMeshInfo = ProcessSkeleton(Node, Skeleton);
			OutResult.SkeletalMeshes.Add(FbxSkeletalMeshInfo);
		}
		else
		{
			ProcessEtc(Node);	// 그외의 경우 ProcessEtc
		}
	}

	for (int i = 0; i < Node->GetChildCount(); ++i)	// 연결된 자식에 대해서 재귀적으로 순회
	{
		ProcessNode(Node->GetChild(i),OutResult);
	} 
}

FFbxMeshInfo FFbxImporter::ProcessMesh(FbxNode* Node, FbxMesh* Mesh)
{
	FFbxMeshInfo MeshInfo;
	FbxStringList UVSetNames;
	Mesh->GetUVSetNames(UVSetNames);                  // UV 추출을 위한 UVSet 이름 넣어두기
	TMap<FFbxIndex, int32, FFbxIndexHash> pFbxCache;  // 동일한 정점 체크를 위해서 사용할 Map 생성, 빠른 계산을 위해 Hash 사용
	for (int i = 0; i < Mesh->GetPolygonCount(); i++)	// 전체 삼각형 개수에 대하여 순회, Import 과정에서 삼각형화 했으므로 전체 Polygon은 다 삼각형
	{
		if (Mesh->GetPolygonSize(i) == 3)
		{
			for (int j = 0; j < 3; j++)
			{
				int VertexIndex = Mesh->GetPolygonVertex(i, j);
				FbxVector4 FbxVertex = Mesh->GetControlPointAt(VertexIndex);	// i번째 Polygon의 j번째 점 Vertex 가져옴
				FVertex Vertex;		
				Vertex.Position = FVector(static_cast<float>(FbxVertex[0]), static_cast<float>(FbxVertex[1]), static_cast<float>(FbxVertex[2]));	// Vertex의 Position에 정점 좌표 대입

				FbxVector4 Normal;	
				bool bNormal = Mesh->GetPolygonVertexNormal(i, j, Normal); // i번째 Polygon의 j번째 점 Normal 가져옴
				if(bNormal) Vertex.Normal = FVector(static_cast<float>(Normal[0]), static_cast<float>(Normal[1]), static_cast<float>(Normal[2]));	// Vertex의 Normal에 값 대입

				if (UVSetNames.GetCount() > 0)	// UVSet이 존재할때 체크
				{
					FbxVector2 UV;
					bool bUnmapped = false;
					bool bSuccess = Mesh->GetPolygonVertexUV( i, j, UVSetNames.GetStringAt(0), UV, bUnmapped); // i번째 Polygon의 j번째 점 UV 가져옴
					if (bSuccess && !bUnmapped)	// Mapping된 UV를 잘 가져왔을때만 UV 대입
					{
						Vertex.UV = FVector2(static_cast<float>(UV[0]), static_cast<float>(UV[1]));	
					}
				}

				FFbxIndex Key{VertexIndex,Vertex.Normal.X,Vertex.Normal.Y,Vertex.Normal.Z,Vertex.UV.X,Vertex.UV.Y}; // 현재 코드에서 얻은 Control Point 번호, Vertex 정보를 바탕으로 키 생성
				if (const int32* Found = pFbxCache.Find(Key))
				{
					MeshInfo.Indices.Add(static_cast<uint32>(*Found));	// Position, Normal, UV가 다 겹치는 경우에는 동일한 정점이므로 Indices에만 추가
				} 
				else   // 그렇지 않은 경우에는 다른 정점으로 처리, Cache에 추가
				{
					uint32 NewVertexIndex = static_cast<uint32>(MeshInfo.Vertices.Num());
					MeshInfo.Vertices.Add(Vertex);
					MeshInfo.Indices.Add(NewVertexIndex);
					pFbxCache.Add(Key, static_cast<int32>(NewVertexIndex));
				}
			}
		}
	}
	return MeshInfo;
}

FFbxSkeletalMeshInfo FFbxImporter::ProcessSkeleton(FbxNode* Node, FbxSkeleton* Skeleton)
{
	FFbxSkeletalMeshInfo SkeletalInfo;
	return SkeletalInfo;
}

void FFbxImporter::ProcessEtc(FbxNode* Node)
{

}

void FFbxImporter::Cook(const FFbxMeshInfo& RawMesh, const FFbxSkeletalMeshInfo& RawBone, FSkeletalMeshData& Out)
{

}