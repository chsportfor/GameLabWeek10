#include "EnginePCH.h"
#include "FbxImporter.h"
#include <cmath>
#include <stdexcept>

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

	TMap<FbxNode*, int32> BoneIndexByNode;
	FbxNode* pFbxRootNode = pFbxScene->GetRootNode();

	try
	{
		if (pFbxRootNode)
		{
			// 전체 Tree에 대해 SkeletonNode부터 전체 순회하여 BoneIndexByNode를 완성
			for (int i = 0; i < pFbxRootNode->GetChildCount(); i++)
			{
				ProcessSkeletonNode(pFbxRootNode->GetChild(i), BoneIndexByNode, FbxResult);
			}

			// 모든 본의 번호가 준비된 후 MeshNode 순회 진행
			for (int i = 0; i < pFbxRootNode->GetChildCount(); i++)
			{
				ProcessMeshNode(pFbxRootNode->GetChild(i), BoneIndexByNode, FbxResult);
			}
		}
	}
	catch (const std::runtime_error& Exception)
	{
		HTR_LOG(Error, "[FBX] Reference pose import failed: {}", Exception.what());
		pFbxScene->Destroy();
		return FFbxImportResult{};
	}
	pFbxScene->Destroy();
	FbxResult.bSuccess = true;
	return FbxResult;
}

void FFbxImporter::ProcessSkeletonNode(FbxNode* Node, TMap<FbxNode*, int32>& BoneIndexByNode, FFbxImportResult& OutResult)
{
	if (!Node) return;

	for (int i = 0; i < Node->GetNodeAttributeCount(); i++) // 한 Node에 여러 Attribute가 있을 수 있으므로 전체를 순회
	{
		FbxNodeAttribute* NodeAttri = Node->GetNodeAttributeByIndex(i);
		if (NodeAttri && NodeAttri->GetAttributeType() == FbxNodeAttribute::EType::eSkeleton)
		{
			FSkeletalBone Bone = ProcessSkeleton(Node, BoneIndexByNode);
			BoneIndexByNode.Add(Node, OutResult.Bones.Num());
			OutResult.Bones.Add(Bone);
		}
	}
	for (int i = 0; i < Node->GetChildCount(); ++i) // 연결된 자식에 대해서 재귀적으로 순회
	{
		ProcessSkeletonNode(Node->GetChild(i), BoneIndexByNode, OutResult);
	}
}

void FFbxImporter::ProcessMeshNode(FbxNode* Node, const TMap<FbxNode*, int32>& BoneIndexByNode, FFbxImportResult& OutResult)
{
	if (!Node) return;

	for (int i = 0; i < Node->GetNodeAttributeCount(); i++)	// 한 Node에 여러 Attribute가 있을 수 있으므로 전체를 순회
	{
		FbxNodeAttribute* NodeAttri = Node->GetNodeAttributeByIndex(i);
		if (NodeAttri && NodeAttri->GetAttributeType() == FbxNodeAttribute::EType::eMesh)
		{
			FbxMesh* Mesh = static_cast<FbxMesh*>(NodeAttri);
			FSkeletalMesh SkeletalMesh = ProcessMesh(Mesh, BoneIndexByNode);
			OutResult.Meshes.Add(SkeletalMesh);
		}
	}
	for (int i = 0; i < Node->GetChildCount(); ++i) // 연결된 자식에 대해서 재귀적으로 순회
	{
		ProcessMeshNode(Node->GetChild(i), BoneIndexByNode, OutResult);
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

			FbxNode* BoneNode = Cluster->GetLink();	// Cluster와 연결된 BoneNode 찾기
			if (!BoneNode) continue;
			const int32* BoneIndex = BoneIndexByNode.Find(BoneNode);	// 해당 Node의 BoneIndex 찾기
			if (!BoneIndex)
			{
				HTR_LOG(Warning, "[FBX] Bone is not registered: {}", BoneNode->GetName());
				continue;
			}

			const int Count = Cluster->GetControlPointIndicesCount();
			const int* ControlPointIndices = Cluster->GetControlPointIndices();
			const double* Weights = Cluster->GetControlPointWeights();
			if (!ControlPointIndices || !Weights) continue;

			for (int EntryIndex = 0; EntryIndex < Count; ++EntryIndex)	// Cluster 내부의 한 Bone이 영향을 주는 정점 배열, Weight 배열 순회
			{
				const int ControlPointIndex = ControlPointIndices[EntryIndex];	// Cluster의 전체 CPIndices를 순회
				const float Weight = static_cast<float>(Weights[EntryIndex]);	// Cluster의 전체 Weight를 순회

				if (ControlPointIndex < 0 || ControlPointIndex >= Mesh->GetControlPointsCount()) continue;
				if (Weight <= 0.0f) continue;

				FBoneInfluence& Influence = InfluenceByControlPoint[ControlPointIndex];	// Cluster 내부의 CPIndex를 바탕으로 Influence 정보를 추가
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
	BuildInfluenceMap(Mesh, BoneIndexByNode, InfluenceByControlPoint);	// Control Point - Influence간 연결 Map을 완성
	BuildMeshVerticesAndIndices(Mesh, InfluenceByControlPoint, SkeletalMesh);	// 이후 정점 추가 과정에서 ControlPoint에 맞춰 Vertex 정보 추가하면서 같이 추가
	return SkeletalMesh;
}

FSkeletalBone FFbxImporter::ProcessSkeleton(FbxNode* Node, const TMap<FbxNode*, int32>& BoneIndexByNode)
{
	if (!Node || !Node->GetScene())
	{
		throw std::runtime_error("Bone node or scene is missing");
	}

	FSkeletalBone SkeletalBone;
	SkeletalBone.Name = Node->GetName();
	// 중간의 일반 노드를 건너뛰어 실제 본 계층의 부모를 찾는다.
	FbxNode* ParentBoneNode = Node->GetParent();
	while (ParentBoneNode && !BoneIndexByNode.Find(ParentBoneNode))
	{
		ParentBoneNode = ParentBoneNode->GetParent();
	}
	const int32* ParentIndex = ParentBoneNode ? BoneIndexByNode.Find(ParentBoneNode) : nullptr;
	SkeletalBone.ParentIndex = ParentIndex ? *ParentIndex : -1;

	int ScenePoseCount = Node->GetScene()->GetPoseCount();
	for (int i = 0; i < ScenePoseCount; i++)	// Scene 내부의 전체 Pose에 대해서 순회
	{
		FbxPose* CurrentPose = Node->GetScene()->GetPose(i);
		if (CurrentPose && CurrentPose->IsBindPose())
		{
			int NodePoseIndex = CurrentPose->Find(Node);
			if (NodePoseIndex < 0) continue;
			FbxMatrix RefTransform = CurrentPose->GetMatrix(NodePoseIndex);	// CurrentPose에서 Node를 찾았을때만 Matrix 호출
			if (ParentBoneNode)
			{
				int ParentPoseIndex = CurrentPose->Find(ParentBoneNode);	
				if (ParentPoseIndex < 0) continue;
				FbxMatrix RefParentTransform = CurrentPose->GetMatrix(ParentPoseIndex);	// CurrentPose에서 부모 Node를 찾았을때만 Matrix 호출 
				FbxMatrix ParentInverse = RefParentTransform.Inverse();
				FbxMatrix Identity;
				Identity.SetIdentity();
				if (RefParentTransform * ParentInverse != Identity) continue;
				RefTransform = ParentInverse * RefTransform; // 전역 행렬에서 부모 변환을 제거해 로컬 변환 계산
			}
			else
			{
				// 루트 본은 부모 역행렬을 적용하지 않고 전역 Bind Pose 유지
			}

			FbxVector4 T, S, Shear;
			FbxQuaternion Q;
			double Sign;
			RefTransform.GetElements(T, Q, Shear, S, Sign);
			SkeletalBone.RefPoseTransform = FTransform(FVector(float(T[0]), float(T[1]), float(T[2])),
				FVector(float(S[0]), float(S[1]), float(S[2])), 
				FQuat(float(Q[0]), float(Q[1]), float(Q[2]), float(Q[3])).Normalized().ToFRotator());
			return SkeletalBone;
		}
	}
}

void FFbxImporter::Cook(const FFbxImportResult& ImportResult, FSkeletalMeshData& OutData)
{
	OutData.Meshes = ImportResult.Meshes;	// Import 결과 Meshes 채움
	OutData.Bones = ImportResult.Bones;		// Import 결과 Bones 채움

	TArray<FMatrix> GlobalRefMatrix; // 부모 기준 Transform 저장하기 위한 임시배열
	GlobalRefMatrix.Reserve(ImportResult.Bones.Num());
	OutData.InverseBindPoseMat.Reset();
	OutData.InverseBindPoseMat.Reserve(ImportResult.Bones.Num());

	for (int i = 0; i < OutData.Bones.Num(); i++)
	{
		FTransform RefTransform = ImportResult.Bones[i].RefPoseTransform;
		FMatrix RefMatrix = OutData.Bones[i].RefPoseTransform.GetLocalMatrix();
		if (OutData.Bones[i].ParentIndex != -1)
		{
			RefMatrix = RefMatrix * GlobalRefMatrix[OutData.Bones[i].ParentIndex]; // 부모 기준 Bone Transform을 전역 기준 BindPose Transform으로 이동, 부모기준 Transform을 곱함
		}
		GlobalRefMatrix.Add(RefMatrix);	// 다음 계산시 부모 Transform으로 활용하기 위해 저장
		OutData.InverseBindPoseMat.Add(RefMatrix.Inverse());	// Inverse한 행렬을 더해 InverBindPose를 채움, Bones[i]로 뽑았으므로 Bones 배열에 대응되는 순서로 저장
	}
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
