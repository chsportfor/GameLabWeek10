#include "EnginePCH.h"
#include "FbxImporter.h"

struct FBoneInfluence
{
	TArray<int32> BoneIndices;
	TArray<float> BoneWeights;
};


FFbxImporter::FFbxImporter()
{
	pFbxManager = FbxManager::Create();	// FbxManager 생성
	if (!pFbxManager)
	{
		HTR_LOG(Error, "[FBX] Manager Create Failed");
		return;
	}

	FbxIOSettings* FBXSetting =  FbxIOSettings::Create(pFbxManager, IOSROOT);	// 설정 Root 이름 세팅 후 IOSetting 객체 생성
	if (!FBXSetting)
	{
		HTR_LOG(Error, "[FBX] IOSettings Create Failed");
		pFbxManager->Destroy();
		pFbxManager = nullptr;
		return;
	}

	pFbxManager->SetIOSettings(FBXSetting);	// FbxManager에 IOSetting 설정
}

FFbxImporter::~FFbxImporter()
{
	if (pFbxManager)
	{
		pFbxManager->Destroy();
	}
}

FFbxImportResult FFbxImporter::ImportFBX(const FString& Path, const FString& SceneName)
{
	FFbxImportResult FbxResult;
	if (!pFbxManager)	// Manager가 초기에 제대로 생성되었는지 여부 체크
	{
		HTR_LOG(Error, "[FBX] Manager Is Not Ready: {}", Path);
		return FbxResult;
	}

	FbxIOSettings* pFbxIOSetting = pFbxManager->GetIOSettings();	// FbxManager의 IO Setting 객체 생성
	if (!pFbxIOSetting)
	{
		HTR_LOG(Error, "[FBX] IOSettings Is Not Ready: {}", Path);
		return FbxResult;
	}

	FbxImporter* pFbxImporter = FbxImporter::Create(pFbxManager, "");	// FbxImporter 객체 생성
	if (!pFbxImporter)
	{
		HTR_LOG(Error, "[FBX] Importer Create Failed: {}", Path);
		return FbxResult;
	}

	bool bInitialized = pFbxImporter->Initialize(Path.c_str(), -1, pFbxIOSetting); // FbxImporter 초기 Initialize
	if (!bInitialized)
	{
		HTR_LOG(Error, "[FBX] Importer Initialize Failed: {}", pFbxImporter->GetStatus().GetErrorString());
		pFbxImporter->Destroy();
		return FbxResult;
	}                          

	FbxGeometryConverter Converter(pFbxManager);	// Polygon 삼각형화 하기 위한 Geometry Converter 생성
	FbxScene* pFbxScene = FbxScene::Create(pFbxManager, SceneName.c_str()); // FbxScene 객체 생성
	if (!pFbxScene)
	{
		HTR_LOG(Error, "[FBX] Scene Create Failed: {}", Path);
		pFbxImporter->Destroy();
		return FbxResult;
	}

	bool bImport = pFbxImporter->Import(pFbxScene);  // FbxImporter Initialize의 파일에서 Scene 정보 가져와 pbxScene에 채움                    
	if (!bImport)
	{
		HTR_LOG(Error, "[FBX] Import Failed: {}", pFbxImporter->GetStatus().GetErrorString());
		pFbxImporter->Destroy();
		pFbxScene->Destroy();
		return FbxResult;
	}      

	bool bTriangulated = Converter.Triangulate(pFbxScene, true);	// 삼각형화 진행
	if (!bTriangulated)
	{
		HTR_LOG(Error, "[FBX] Triangulate Failed");
		pFbxImporter->Destroy();
		pFbxScene->Destroy();
		return FbxResult;
	}

	pFbxImporter->Destroy();	// 다 채웠으므로 Importer는 Destroy

	// TODO: 스킨 연결을 추가할 때는 메시 처리 전에 본 수집을 완료하는 순서로 분리한다.
	TMap<FbxNode*, int32> BoneIndexByNode;
	FbxNode* pFbxRootNode = pFbxScene->GetRootNode();

	if (pFbxRootNode)
	{
		for (int i = 0; i < pFbxRootNode->GetChildCount(); i++)	// RootNode 내려가면서 정보 읽어서 Import
		{
			ProcessNode(pFbxRootNode->GetChild(i), BoneIndexByNode, FbxResult);
		}
	}
	pFbxScene->Destroy();
	FbxResult.bSuccess = true;
	return FbxResult;
}

void FFbxImporter::ProcessNode(FbxNode* Node, TMap<FbxNode*, int32>& BoneIndexByNode, FFbxImportResult& OutResult)
{
	for (int i = 0; i < Node->GetNodeAttributeCount(); i++) // 한 Node에 여러 Attribute가 있을 수 있으므로 전체를 순회
	{
		FbxNodeAttribute* NodeAttri = Node->GetNodeAttributeByIndex(i);
		if (NodeAttri->GetAttributeType() == FbxNodeAttribute::EType::eMesh) // Mesh 일경우 FbxMesh로 Cast 후 ProcessMesh
		{
			FbxMesh* Mesh = static_cast<FbxMesh*>(NodeAttri);
			FSkeletalMesh SkeletalMesh = ProcessMesh(Mesh, BoneIndexByNode);
			OutResult.Meshes.Add(SkeletalMesh);
		}
		else if (NodeAttri->GetAttributeType() == FbxNodeAttribute::EType::eSkeleton) // Skeleton 일경우 FbxSkeleton로 Cast 후 ProcessSkeleton
		{
			FSkeletalBone Bone = ProcessSkeleton(Node, BoneIndexByNode);
			BoneIndexByNode.Add(Node, OutResult.Bones.Num());
			OutResult.Bones.Add(Bone);
		}
	}
	for (int i = 0; i < Node->GetChildCount(); ++i) // 연결된 자식에 대해서 재귀적으로 순회
	{
		ProcessNode(Node->GetChild(i), BoneIndexByNode, OutResult);
	}
}

void FFbxImporter::BuildInfluenceMap(FbxMesh* Mesh, const TMap<FbxNode*, int32>& BoneIndexByNode, TMap<int32, FBoneInfluence>& InfluenceByControlPoint)
{
	if (!Mesh) return;

	const int SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
	for (int SkinIndex = 0; SkinIndex < SkinCount; ++SkinIndex)	// FbxSkin만큼 순회
	{
		FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin));
		if (!Skin) continue;

		const int ClusterCount = Skin->GetClusterCount();
		for (int ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex)	// 한 FbxSkin의 전체 Cluster만큼 순회
		{
			FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
			if (!Cluster) continue;

			FbxNode* BoneNode = Cluster->GetLink();
			if (!BoneNode) continue;
			const int32* BoneIndex = BoneIndexByNode.Find(BoneNode);
			if (!BoneIndex)
			{
				HTR_LOG(Warning, "[FBX] Bone is not registered: {}", BoneNode->GetName());
				continue;
			}

			const int Count = Cluster->GetControlPointIndicesCount();
			const int* ControlPointIndices = Cluster->GetControlPointIndices();
			const double* Weights = Cluster->GetControlPointWeights();
			if (!ControlPointIndices || !Weights) continue;

			for (int InfluenceIndex = 0; InfluenceIndex < Count; ++InfluenceIndex)
			{
				const int ControlPointIndex = ControlPointIndices[InfluenceIndex];
				const float Weight = static_cast<float>(Weights[InfluenceIndex]);

				if (ControlPointIndex < 0 || ControlPointIndex >= Mesh->GetControlPointsCount()) continue;
				if (Weight <= 0.0f) continue;

				FBoneInfluence& Influence = InfluenceByControlPoint[ControlPointIndex];
				Influence.BoneIndices.Add(*BoneIndex);
				Influence.BoneWeights.Add(Weight);
			}
		}
	}

}

FSkeletalMesh FFbxImporter::ProcessMesh(FbxMesh* Mesh, const TMap<FbxNode*, int32>& BoneIndexByNode)
{
	FSkeletalMesh SkeletalMesh;
	TMap<int32, FBoneInfluence> InfluenceByControlPoint;
	BuildInfluenceMap(Mesh, BoneIndexByNode, InfluenceByControlPoint);
	BuildMeshVerticesAndIndices(Mesh, InfluenceByControlPoint, SkeletalMesh);
	return SkeletalMesh;
}

FSkeletalBone FFbxImporter::ProcessSkeleton(FbxNode* Node, const TMap<FbxNode*, int32>& BoneIndexByNode)
{

}

void FFbxImporter::Cook(const FFbxImportResult& ImportResult, FSkeletalMeshData& OutData)
{

}

void FFbxImporter::BuildMeshVerticesAndIndices(FbxMesh* Mesh,const TMap<int32, FBoneInfluence>& InfluenceByControlPoint,FSkeletalMesh& SkeletalMesh)
{
	if (!Mesh) return;

	FbxStringList UVSetNames;
	Mesh->GetUVSetNames(UVSetNames); // UV 추출을 위한 UVSet 이름 넣어두기
	TMap<FFbxVertexKey, int32, FFbxVertexKeyHash> VertexCache;

	for (int i = 0; i < Mesh->GetPolygonCount(); i++) // 전체 Polygon 순회
	{
		if (Mesh->GetPolygonSize(i) == 3)
		{
			for (int j = 0; j < 3; j++)	// Polygon 내부의 점 순회, Polygon은 삼각형화에 의해 삼각형으로 고정됨
			{
				int ControlPointIndex = Mesh->GetPolygonVertex(i, j);
				FbxVector4 FbxVertex = Mesh->GetControlPointAt(ControlPointIndex);
				FSkeletalVertex SkeletalVertex;
				FVertex& Vertex = SkeletalVertex.Vertex;
				Vertex.Position = FVector(static_cast<float>(FbxVertex[0]), static_cast<float>(FbxVertex[1]), static_cast<float>(FbxVertex[2]));

				FbxVector4 Normal;
				bool bNormal = Mesh->GetPolygonVertexNormal(i, j, Normal);
				if (bNormal) Vertex.Normal = FVector(static_cast<float>(Normal[0]), static_cast<float>(Normal[1]), static_cast<float>(Normal[2]));

				if (UVSetNames.GetCount() > 0) // UVSet이 존재할 때 체크
				{
					FbxVector2 UV;
					bool bUnmapped = false;
					bool bSuccess = Mesh->GetPolygonVertexUV(i, j, UVSetNames.GetStringAt(0), UV, bUnmapped);
					if (bSuccess && !bUnmapped)
					{
						Vertex.UV = FVector2(static_cast<float>(UV[0]), static_cast<float>(UV[1]));
					}
				}

				FFbxVertexKey Key{ ControlPointIndex, Vertex.Normal.X, Vertex.Normal.Y, Vertex.Normal.Z, Vertex.UV.X, Vertex.UV.Y };
				if (const int32* Found = VertexCache.Find(Key))
				{
					SkeletalMesh.Indices.Add(static_cast<uint32>(*Found));	// 같은 정점이면 기존 정점의 인덱스만 추가
				}
				else // 다른 정점이면 본 정보를 채우고 새 정점으로 추가
				{
					if (const FBoneInfluence* Influence = InfluenceByControlPoint.Find(ControlPointIndex))
					{
						// 큰 가중치부터 최대 4개만 유지한다.
						for (int InfluenceIndex = 0; InfluenceIndex < Influence->BoneIndices.Num(); ++InfluenceIndex)
						{
							const float Weight = Influence->BoneWeights[InfluenceIndex];
							for (int Slot = 0; Slot < 4; ++Slot)
							{
								if (Weight > SkeletalVertex.BoneWeights[Slot])
								{
									for (int Shift = 3; Shift > Slot; --Shift)
									{
										SkeletalVertex.BoneIndices[Shift] = SkeletalVertex.BoneIndices[Shift - 1];
										SkeletalVertex.BoneWeights[Shift] = SkeletalVertex.BoneWeights[Shift - 1];
									}
									SkeletalVertex.BoneIndices[Slot] = static_cast<uint32>(Influence->BoneIndices[InfluenceIndex]);
									SkeletalVertex.BoneWeights[Slot] = Weight;
									break;
								}
							}
						}

						float TotalWeight = 0.0f;
						for (float Weight : SkeletalVertex.BoneWeights) TotalWeight += Weight;
						if (TotalWeight > 0.0f)
						{
							for (float& Weight : SkeletalVertex.BoneWeights) Weight /= TotalWeight;
						}
					}

					uint32 NewVertexIndex = static_cast<uint32>(SkeletalMesh.Vertices.Num());
					SkeletalMesh.Vertices.Add(SkeletalVertex);
					SkeletalMesh.Indices.Add(NewVertexIndex);
					VertexCache.Add(Key, static_cast<int32>(NewVertexIndex));
				}
			}
		}
	}
}
