#include "EnginePCH.h"
#include "FbxImporter.h"
#include "Core/EngineLog.h"
#include "Render/SkeletalMeshData.h"
#include "Render/StaticMeshData.h"
#include "Asset/FbxImporter/SkeletalMeshBake.h"
#include "Asset/ObjImporter/StaticMeshBake.h"

#include <algorithm>
#include <cfloat>
#include <fbxsdk.h>

namespace
{
	FString GetNodeAttributeTypeName(FbxNodeAttribute::EType Type)
	{
		switch (Type)
		{
		case FbxNodeAttribute::eUnknown: return "Unknown";
		case FbxNodeAttribute::eNull: return "Null";
		case FbxNodeAttribute::eMarker: return "Marker";
		case FbxNodeAttribute::eSkeleton: return "Skeleton (Bone)";
		case FbxNodeAttribute::eMesh: return "Mesh";
		case FbxNodeAttribute::eNurbs: return "Nurbs";
		case FbxNodeAttribute::ePatch: return "Patch";
		case FbxNodeAttribute::eCamera: return "Camera";
		case FbxNodeAttribute::eCameraStereo: return "CameraStereo";
		case FbxNodeAttribute::eCameraSwitcher: return "CameraSwitcher";
		case FbxNodeAttribute::eLight: return "Light";
		case FbxNodeAttribute::eOpticalReference: return "OpticalReference";
		case FbxNodeAttribute::eOpticalMarker: return "OpticalMarker";
		default: return "Other";
		}
	}

	// FBX 원본 좌표를 엔진 표준 좌표계(왼손, Z-Up: X-Forward, Y-Right, Z-Up)로 변환
	FVector ToEnginePosition(const FbxVector4& V, FbxAxisSystem::EUpVector UpVector, int UpSign = 1)
	{
		const float Sign = static_cast<float>(UpSign);

		if (UpVector == FbxAxisSystem::eYAxis)
		{
			// Y-Up: Y -> Z(Up), -Z -> X(Forward), X -> Y(Right)
			return FVector(static_cast<float>(-V[2]), static_cast<float>(V[0]), static_cast<float>(V[1]) * Sign);
		}
		else if (UpVector == FbxAxisSystem::eZAxis)
		{
			// Z-Up: Z -> Z(Up), X -> X(Forward), Y -> -Y(오른손->왼손 반전)
			return FVector(static_cast<float>(V[0]), static_cast<float>(-V[1]), static_cast<float>(V[2]) * Sign);
		}
		else if (UpVector == FbxAxisSystem::eXAxis)
		{
			// X-Up: X -> Z(Up), Y -> X(Forward), Z -> Y(Right)
			return FVector(static_cast<float>(V[1]), static_cast<float>(V[2]), static_cast<float>(V[0]) * Sign);
		}

		return FVector(static_cast<float>(V[0]), static_cast<float>(V[1]), static_cast<float>(V[2]));
	}

	FVector ToEngineDirection(const FbxVector4& V, FbxAxisSystem::EUpVector UpVector, int UpSign = 1)
	{
		return ToEnginePosition(V, UpVector, UpSign).GetSafeNormal();
	}

	FQuat ToEngineQuat(const FbxQuaternion& Q, FbxAxisSystem::EUpVector UpVector, int UpSign = 1)
	{
		FVector V = ToEnginePosition(FbxVector4(Q[0], Q[1], Q[2], 0.0), UpVector, UpSign);
		return FQuat(V.X, V.Y, V.Z, -static_cast<float>(Q[3]));
	}

	FMatrix ToEngineMatrix(const FbxAMatrix& FbxMat, FbxAxisSystem::EUpVector UpVector, int UpSign = 1)
	{
		FbxVector4 T = FbxMat.GetT();
		FbxQuaternion Q = FbxMat.GetQ();
		FbxVector4 S = FbxMat.GetS();

		FVector Pos = ToEnginePosition(T, UpVector, UpSign);
		FVector Scale(static_cast<float>(S[0]), static_cast<float>(S[1]), static_cast<float>(S[2]));
		FRotator Rot = ToEngineQuat(Q, UpVector, UpSign).ToFRotator();

		return FTransform(Rot, Pos, Scale).GetLocalMatrix();
	}

	int32 FindBoneIndex(const TArray<FBoneInfo>& Bones, const FString& BoneName)
	{
		for (int32 i = 0; i < Bones.Num(); ++i)
		{
			if (Bones[i].Name == BoneName)
			{
				return i;
			}
		}
		return -1;
	}

	FString GetTexturePath(FbxSurfaceMaterial* Material, const char* PropertyName)
	{
		if (!Material) return "";
		FbxProperty Property = Material->FindProperty(PropertyName);
		if (!Property.IsValid()) return "";

		int32 TextureCount = Property.GetSrcObjectCount<FbxFileTexture>();
		if (TextureCount > 0)
		{
			FbxFileTexture* Texture = Property.GetSrcObject<FbxFileTexture>(0);
			if (Texture)
			{
				return Texture->GetRelativeFileName();
			}
		}
		return "";
	}

	struct FRawBoneInfluence
	{
		int32 BoneIndex = 0;
		float Weight = 0.0f;
	};

	void FindMeshNodes(FbxNode* Node, TArray<FbxNode*>& OutMeshNodes)
	{
		if (!Node) return;
		FbxNodeAttribute* Attr = Node->GetNodeAttribute();
		if (Attr && Attr->GetAttributeType() == FbxNodeAttribute::eMesh)
		{
			OutMeshNodes.Add(Node);
		}
		for (int32 i = 0; i < Node->GetChildCount(); ++i)
		{
			FindMeshNodes(Node->GetChild(i), OutMeshNodes);
		}
	}

	void FindRootBones(FbxNode* Node, TArray<FbxNode*>& OutRootBones)
	{
		if (!Node) return;

		FbxNodeAttribute* Attr = Node->GetNodeAttribute();
		const bool bIsBone = (Attr && Attr->GetAttributeType() == FbxNodeAttribute::eSkeleton);

		FbxNode* ParentNode = Node->GetParent();
		FbxNodeAttribute* ParentAttr = ParentNode ? ParentNode->GetNodeAttribute() : nullptr;
		const bool bParentIsBone = (ParentAttr && ParentAttr->GetAttributeType() == FbxNodeAttribute::eSkeleton);

		if (bIsBone && !bParentIsBone)
		{
			OutRootBones.Add(Node);
			return;
		}

		for (int32 i = 0; i < Node->GetChildCount(); ++i)
		{
			FindRootBones(Node->GetChild(i), OutRootBones);
		}
	}

	void BuildBoneHierarchy(
		FbxNode* Node,
		int32 ParentIndex,
		FSkeletalMeshData& OutMesh,
		FbxAxisSystem::EUpVector UpVector,
		int UpSign,
		TArray<FMatrix>& OutGlobalMatrices)
	{
		if (!Node) return;

		int32 MyIndex = OutMesh.Bones.Num();

		FBoneInfo Bone;
		Bone.Name = Node->GetName();
		Bone.ParentIndex = ParentIndex;

		FbxAMatrix LocalFbxMat = Node->EvaluateLocalTransform();
		FbxVector4 LocalT = LocalFbxMat.GetT();
		FbxQuaternion LocalQ = LocalFbxMat.GetQ();
		FbxVector4 LocalS = LocalFbxMat.GetS();

		Bone.LocalTransform.Location = ToEnginePosition(LocalT, UpVector, UpSign);
		Bone.LocalTransform.Scale = FVector(
			static_cast<float>(LocalS[0]),
			static_cast<float>(LocalS[1]),
			static_cast<float>(LocalS[2])
		);
		Bone.LocalTransform.Rotation = ToEngineQuat(LocalQ, UpVector, UpSign).ToFRotator();

		OutMesh.Bones.Add(Bone);

		FMatrix MyLocalMatrix = Bone.LocalTransform.GetLocalMatrix();
		FMatrix MyGlobalMatrix;
		if (ParentIndex >= 0)
		{
			MyGlobalMatrix = MyLocalMatrix * OutGlobalMatrices[ParentIndex];
		}
		else
		{
			MyGlobalMatrix = MyLocalMatrix;
		}
		OutGlobalMatrices.Add(MyGlobalMatrix);
		OutMesh.InverseBindPoseMatrices.Add(MyGlobalMatrix.Inverse());

		for (int32 i = 0; i < Node->GetChildCount(); ++i)
		{
			FbxNode* ChildNode = Node->GetChild(i);
			FbxNodeAttribute* ChildAttr = ChildNode ? ChildNode->GetNodeAttribute() : nullptr;
			if (ChildAttr && ChildAttr->GetAttributeType() == FbxNodeAttribute::eSkeleton)
			{
				BuildBoneHierarchy(ChildNode, MyIndex, OutMesh, UpVector, UpSign, OutGlobalMatrices);
			}
		}
	}
}

FFbxImporter* FFbxImporter::GetInstance()
{
	static FFbxImporter Instance;
	return &Instance;
}

FFbxImporter::FFbxImporter()
{
	Initialize();
}

FFbxImporter::~FFbxImporter()
{
	Shutdown();
}

bool FFbxImporter::Initialize()
{
	if (SdkManager)
	{
		return true;
	}

	SdkManager = FbxManager::Create();
	if (!SdkManager)
	{
		HTR_LOG(Error, "[FFbxImporter] Failed to create FbxManager!");
		return false;
	}

	IOSettings = FbxIOSettings::Create(SdkManager, IOSROOT);
	if (!IOSettings)
	{
		HTR_LOG(Error, "[FFbxImporter] Failed to create FbxIOSettings!");
		SdkManager->Destroy();
		SdkManager = nullptr;
		return false;
	}
	SdkManager->SetIOSettings(IOSettings);

	ApplyIOSettings(CurrentImportFlags);
	return true;
}

void FFbxImporter::Shutdown()
{
	if (SdkManager)
	{
		SdkManager->Destroy();
		SdkManager = nullptr;
		IOSettings = nullptr;
	}
}

void FFbxImporter::ApplyIOSettings(EFbxImportFlags Flags)
{
	if (!IOSettings)
	{
		return;
	}

	CurrentImportFlags = Flags;

	const bool bImportMaterial       = HasFlag(Flags, EFbxImportFlags::Material);
	const bool bImportTexture        = HasFlag(Flags, EFbxImportFlags::Texture);
	const bool bImportLink           = HasFlag(Flags, EFbxImportFlags::Link);
	const bool bImportShape          = HasFlag(Flags, EFbxImportFlags::Shape);
	const bool bImportAnimation      = HasFlag(Flags, EFbxImportFlags::Animation);
	const bool bImportGlobalSettings = HasFlag(Flags, EFbxImportFlags::GlobalSettings);

	IOSettings->SetBoolProp(IMP_FBX_MATERIAL,        bImportMaterial);
	IOSettings->SetBoolProp(IMP_FBX_TEXTURE,         bImportTexture);
	IOSettings->SetBoolProp(IMP_FBX_LINK,            bImportLink);
	IOSettings->SetBoolProp(IMP_FBX_SHAPE,           bImportShape);
	IOSettings->SetBoolProp(IMP_FBX_ANIMATION,       bImportAnimation);
	IOSettings->SetBoolProp(IMP_FBX_GLOBAL_SETTINGS, bImportGlobalSettings);
}

bool FFbxImporter::LoadAndPrintFbxScene(const FString& FilePath, EFbxImportFlags Flags)
{
	FFbxImporter* ImporterInstance = GetInstance();
	if (!ImporterInstance || !ImporterInstance->Initialize())
	{
		HTR_LOG(Error, "[FFbxImporter] FBX SDK Manager is not initialized!");
		return false;
	}

	FbxScene* Scene = ImporterInstance->LoadFbxScene(FilePath, Flags);
	if (!Scene)
	{
		return false;
	}

	FbxGlobalSettings& GlobalSettings = Scene->GetGlobalSettings();
	int UpSign = 1;
	FbxAxisSystem::EUpVector UpVector = GlobalSettings.GetAxisSystem().GetUpVector(UpSign);
	const char* UpVectorStr = (UpVector == FbxAxisSystem::eYAxis) ? "Y-Up" : (UpVector == FbxAxisSystem::eZAxis ? "Z-Up" : "X-Up");

	TArray<FbxNode*> MeshNodes;
	FindMeshNodes(Scene->GetRootNode(), MeshNodes);

	TArray<FbxNode*> RootBones;
	if (Scene->GetRootNode())
	{
		for (int32 i = 0; i < Scene->GetRootNode()->GetChildCount(); ++i)
		{
			FindRootBones(Scene->GetRootNode()->GetChild(i), RootBones);
		}
	}

	int32 AnimStackCount = Scene->GetSrcObjectCount<FbxAnimStack>();

	HTR_LOG(Info, "[FBX Info] '{}' | UpVector: {} | Meshes: {} | RootBones: {} | AnimStacks: {}",
		FilePath, UpVectorStr, MeshNodes.Num(), RootBones.Num(), AnimStackCount);

	Scene->Destroy();
	return true;
}

TUniquePtr<FSkeletalMeshData> FFbxImporter::LoadSkeletalMeshData(const FString& FilePath)
{
	const FString BinPath = FilePath + ".skelbin";
	if (TUniquePtr<FSkeletalMeshData> Baked = FSkeletalMeshBake::ReadBaked(BinPath))
	{
		return Baked;
	}

	TUniquePtr<FSkeletalMeshData> Data = MakeUnique<FSkeletalMeshData>();
	if (!GetInstance()->ImportSkeletalMesh(FilePath, *Data))
	{
		return nullptr;
	}

	FSkeletalMeshBake::WriteBaked(BinPath, *Data);
	return Data;
}

TUniquePtr<FStaticMeshData> FFbxImporter::LoadStaticMeshData(const FString& FilePath)
{
	const FString BinPath = FilePath + ".bin";
	if (TUniquePtr<FStaticMeshData> Baked = FStaticMeshBake::ReadBaked(BinPath))
	{
		return Baked;
	}

	TUniquePtr<FStaticMeshData> Data = MakeUnique<FStaticMeshData>();
	if (!GetInstance()->ImportStaticMesh(FilePath, *Data))
	{
		return nullptr;
	}

	FStaticMeshBake::WriteBaked(BinPath, *Data);
	return Data;
}

FbxScene* FFbxImporter::LoadFbxScene(const FString& FilePath, EFbxImportFlags Flags)
{
	FFbxImporter* ImporterInstance = GetInstance();
	if (!ImporterInstance || !ImporterInstance->Initialize())
	{
		HTR_LOG(Error, "[FFbxImporter] FBX SDK Manager is not initialized!");
		return nullptr;
	}

	ImporterInstance->ApplyIOSettings(Flags);

	FbxManager* SdkMgr = ImporterInstance->GetManager();
	FbxIOSettings* Settings = ImporterInstance->GetIOSettings();

	FbxImporter* Importer = FbxImporter::Create(SdkMgr, "");
	if (!Importer->Initialize(FilePath.c_str(), -1, Settings))
	{
		HTR_LOG(Error, "[FFbxImporter] Failed to initialize FbxImporter for file: {} ({})",
			FilePath, Importer->GetStatus().GetErrorString());
		Importer->Destroy();
		return nullptr;
	}

	FbxScene* Scene = FbxScene::Create(SdkMgr, "FbxImportScene");
	if (!Importer->Import(Scene))
	{
		HTR_LOG(Error, "[FFbxImporter] Failed to import scene from FBX file: {}", FilePath);
		Importer->Destroy();
		Scene->Destroy();
		return nullptr;
	}

	Importer->Destroy();
	return Scene;
}

void FFbxImporter::PreprocessScene(fbxsdk::FbxScene* Scene)
{
	if (!Scene || !SdkManager) return;

	FbxSystemUnit SceneUnit = Scene->GetGlobalSettings().GetSystemUnit();
	if (SceneUnit != FbxSystemUnit::cm)
	{
		FbxSystemUnit::cm.ConvertScene(Scene);
	}

	FbxGeometryConverter Converter(SdkManager);
	Converter.Triangulate(Scene, true);
}

bool FFbxImporter::ImportStaticMesh(const FString& FilePath, FStaticMeshData& OutMeshData)
{
	FbxScene* Scene = LoadFbxScene(FilePath, EFbxImportFlags::StaticMesh);
	if (!Scene) return false;

	PreprocessScene(Scene);
	if (!ProcessStaticGeometry(Scene, OutMeshData))
	{
		Scene->Destroy();
		return false;
	}

	Scene->Destroy();
	return true;
}

bool FFbxImporter::ImportSkeletalMesh(const FString& FilePath, FSkeletalMeshData& OutMeshData)
{
	FbxScene* Scene = LoadFbxScene(FilePath, EFbxImportFlags::SkeletalMesh);
	if (!Scene) return false;

	PreprocessScene(Scene);
	if (!ProcessSkeleton(Scene, OutMeshData))
	{
		Scene->Destroy();
		return false;
	}

	if (!ProcessGeometry(Scene, OutMeshData))
	{
		Scene->Destroy();
		return false;
	}

	Scene->Destroy();
	return true;
}

bool FFbxImporter::ProcessStaticGeometry(fbxsdk::FbxScene* Scene, FStaticMeshData& OutMesh)
{
	if (!Scene) return false;

	OutMesh.Vertices.Empty();
	OutMesh.Indices.Empty();
	OutMesh.Sections.Empty();
	OutMesh.MaterialSlots.Empty();
	OutMesh.AABB = FBox{ FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX) };

	FbxGlobalSettings& GlobalSettings = Scene->GetGlobalSettings();
	int UpSign = 1;
	FbxAxisSystem::EUpVector UpVector = GlobalSettings.GetAxisSystem().GetUpVector(UpSign);

	TArray<FbxNode*> MeshNodes;
	FindMeshNodes(Scene->GetRootNode(), MeshNodes);

	if (MeshNodes.IsEmpty())
	{
		HTR_LOG(Warning, "[FFbxImporter] No mesh found in FBX scene!");
		return false;
	}

	for (FbxNode* MeshNode : MeshNodes)
	{
		FbxMesh* Mesh = MeshNode->GetMesh();
		if (!Mesh) continue;

		int32 ControlPointsCount = Mesh->GetControlPointsCount();
		if (ControlPointsCount == 0) continue;

		const FbxVector4* ControlPoints = Mesh->GetControlPoints();

		int32 MaterialCount = MeshNode->GetMaterialCount();
		int32 MaterialBaseIndex = OutMesh.MaterialSlots.Num();

		if (MaterialCount > 0)
		{
			for (int32 m = 0; m < MaterialCount; ++m)
			{
				FbxSurfaceMaterial* Mat = MeshNode->GetMaterial(m);
				FStaticMaterialSlot Slot;
				Slot.Name = Mat ? Mat->GetName() : "DefaultMaterial";
				Slot.DiffuseTexturePath = GetTexturePath(Mat, FbxSurfaceMaterial::sDiffuse);
				Slot.NormalTexturePath = GetTexturePath(Mat, FbxSurfaceMaterial::sNormalMap);
				if (Slot.NormalTexturePath.empty())
				{
					Slot.NormalTexturePath = GetTexturePath(Mat, FbxSurfaceMaterial::sBump);
				}
				OutMesh.MaterialSlots.Add(Slot);
			}
		}
		else
		{
			FStaticMaterialSlot DefaultSlot;
			DefaultSlot.Name = "DefaultMaterial";
			OutMesh.MaterialSlots.Add(DefaultSlot);
			MaterialCount = 1;
		}

		TArray<TArray<uint32>> MaterialIndices;
		MaterialIndices.SetNum(MaterialCount);

		int32 PolygonCount = Mesh->GetPolygonCount();
		FbxGeometryElementMaterial* MaterialElement = Mesh->GetElementMaterial(0);

		const char* UVSetName = nullptr;
		if (Mesh->GetElementUVCount() > 0)
		{
			UVSetName = Mesh->GetElementUV(0)->GetName();
		}

		for (int32 p = 0; p < PolygonCount; ++p)
		{
			int32 SlotIdx = 0;
			if (MaterialElement)
			{
				if (MaterialElement->GetMappingMode() == FbxGeometryElement::eAllSame)
				{
					SlotIdx = MaterialElement->GetIndexArray().GetAt(0);
				}
				else if (MaterialElement->GetMappingMode() == FbxGeometryElement::eByPolygon)
				{
					SlotIdx = MaterialElement->GetIndexArray().GetAt(p);
				}
			}
			if (SlotIdx < 0 || SlotIdx >= MaterialCount)
			{
				SlotIdx = 0;
			}

			const int32 CornerIndices[3] = { 0, 2, 1 };

			for (int32 c = 0; c < 3; ++c)
			{
				int32 Corner = CornerIndices[c];
				int32 CpIndex = Mesh->GetPolygonVertex(p, Corner);

				FVertex Vertex;
				FVector Pos = ToEnginePosition(ControlPoints[CpIndex], UpVector, UpSign);
				Vertex.Position = Pos;

				OutMesh.AABB.Expand(FBox{ Pos, Pos });

				FbxVector4 FbxNormal;
				if (Mesh->GetPolygonVertexNormal(p, Corner, FbxNormal))
				{
					Vertex.Normal = ToEngineDirection(FbxNormal, UpVector, UpSign);
				}
				else
				{
					Vertex.Normal = FVector(0.f, 0.f, 1.f);
				}

				if (UVSetName)
				{
					FbxVector2 FbxUV;
					bool bUnmapped = false;
					if (Mesh->GetPolygonVertexUV(p, Corner, UVSetName, FbxUV, bUnmapped))
					{
						Vertex.UV.X = static_cast<float>(FbxUV[0]);
						Vertex.UV.Y = 1.0f - static_cast<float>(FbxUV[1]);
					}
				}

				FVector Tangent = Vertex.Normal.Cross(FVector(0.f, 0.f, 1.f)).GetSafeNormal();
				if (Tangent.Length() < 0.001f)
				{
					Tangent = Vertex.Normal.Cross(FVector(0.f, 1.f, 0.f)).GetSafeNormal();
				}
				Vertex.Tangent = Tangent;
				Vertex.Color = FVector4(1.f, 1.f, 1.f, 1.f);

				uint32 NewVertexIndex = static_cast<uint32>(OutMesh.Vertices.Num());
				OutMesh.Vertices.Add(Vertex);
				MaterialIndices[SlotIdx].Add(NewVertexIndex);
			}
		}

		for (int32 m = 0; m < MaterialCount; ++m)
		{
			if (MaterialIndices[m].IsEmpty()) continue;

			FStaticMeshSection Section;
			Section.StartIndex = static_cast<uint32>(OutMesh.Indices.Num());
			Section.IndexCount = static_cast<uint32>(MaterialIndices[m].Num());
			Section.MaterialSlotIndex = static_cast<uint32>(MaterialBaseIndex + m);

			OutMesh.Indices.Append(MaterialIndices[m]);
			OutMesh.Sections.Add(Section);
		}
	}

	OutMesh.BuildTangentSpace();
	OutMesh.OptimizeTriangleOrderForVertexCache();

	FString Error;
	if (!OutMesh.Validate(Error))
	{
		HTR_LOG(Error, "[FFbxImporter] Static mesh validation failed: {}", Error);
		return false;
	}

	return true;
}

bool FFbxImporter::ProcessGeometry(fbxsdk::FbxScene* Scene, FSkeletalMeshData& OutMesh)
{
	if (!Scene) return false;

	OutMesh.Vertices.Empty();
	OutMesh.Indices.Empty();
	OutMesh.Sections.Empty();
	OutMesh.MaterialSlots.Empty();
	OutMesh.AABB = FBox{ FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX) };

	FbxGlobalSettings& GlobalSettings = Scene->GetGlobalSettings();
	int UpSign = 1;
	FbxAxisSystem::EUpVector UpVector = GlobalSettings.GetAxisSystem().GetUpVector(UpSign);

	TArray<FbxNode*> MeshNodes;
	FindMeshNodes(Scene->GetRootNode(), MeshNodes);

	if (MeshNodes.IsEmpty())
	{
		HTR_LOG(Warning, "[FFbxImporter] No mesh found in FBX scene!");
		return false;
	}

	for (FbxNode* MeshNode : MeshNodes)
	{
		FbxMesh* Mesh = MeshNode->GetMesh();
		if (!Mesh) continue;

		int32 ControlPointsCount = Mesh->GetControlPointsCount();
		if (ControlPointsCount == 0) continue;

		const FbxVector4* ControlPoints = Mesh->GetControlPoints();

		TArray<TArray<FRawBoneInfluence>> ControlPointWeights;
		ControlPointWeights.SetNum(ControlPointsCount);

		int32 SkinDeformerCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
		for (int32 s = 0; s < SkinDeformerCount; ++s)
		{
			FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(s, FbxDeformer::eSkin));
			int32 ClusterCount = Skin->GetClusterCount();

			for (int32 c = 0; c < ClusterCount; ++c)
			{
				FbxCluster* Cluster = Skin->GetCluster(c);
				FbxNode* BoneNode = Cluster->GetLink();
				if (!BoneNode) continue;

				FString BoneName = BoneNode->GetName();
				int32 BoneIndex = FindBoneIndex(OutMesh.Bones, BoneName);
				if (BoneIndex == -1)
				{
					HTR_LOG(Warning, "[FFbxImporter] Skin cluster bone '{}' not found in skeleton hierarchy!", BoneName);
					continue;
				}

				FbxAMatrix TransformLinkMatrix;
				Cluster->GetTransformLinkMatrix(TransformLinkMatrix);
				FMatrix EngineLinkMatrix = ToEngineMatrix(TransformLinkMatrix, UpVector, UpSign);
				OutMesh.InverseBindPoseMatrices[BoneIndex] = EngineLinkMatrix.Inverse();

				int32 IndexCount = Cluster->GetControlPointIndicesCount();
				const int32* Indices = Cluster->GetControlPointIndices();
				const double* Weights = Cluster->GetControlPointWeights();

				for (int32 k = 0; k < IndexCount; ++k)
				{
					int32 CpIndex = Indices[k];
					float Weight = static_cast<float>(Weights[k]);
					if (CpIndex >= 0 && CpIndex < ControlPointsCount && Weight > 0.0001f)
					{
						ControlPointWeights[CpIndex].Add({ BoneIndex, Weight });
					}
				}
			}
		}

		int32 MaterialCount = MeshNode->GetMaterialCount();
		int32 MaterialBaseIndex = OutMesh.MaterialSlots.Num();

		if (MaterialCount > 0)
		{
			for (int32 m = 0; m < MaterialCount; ++m)
			{
				FbxSurfaceMaterial* Mat = MeshNode->GetMaterial(m);
				FSkeletalMaterialSlot Slot;
				Slot.Name = Mat ? Mat->GetName() : "DefaultMaterial";
				Slot.DiffuseTexturePath = GetTexturePath(Mat, FbxSurfaceMaterial::sDiffuse);
				Slot.NormalTexturePath = GetTexturePath(Mat, FbxSurfaceMaterial::sNormalMap);
				if (Slot.NormalTexturePath.empty())
				{
					Slot.NormalTexturePath = GetTexturePath(Mat, FbxSurfaceMaterial::sBump);
				}
				OutMesh.MaterialSlots.Add(Slot);
			}
		}
		else
		{
			FSkeletalMaterialSlot DefaultSlot;
			DefaultSlot.Name = "DefaultMaterial";
			OutMesh.MaterialSlots.Add(DefaultSlot);
			MaterialCount = 1;
		}

		TArray<TArray<uint32>> MaterialIndices;
		MaterialIndices.SetNum(MaterialCount);

		int32 PolygonCount = Mesh->GetPolygonCount();
		FbxGeometryElementMaterial* MaterialElement = Mesh->GetElementMaterial(0);

		const char* UVSetName = nullptr;
		if (Mesh->GetElementUVCount() > 0)
		{
			UVSetName = Mesh->GetElementUV(0)->GetName();
		}

		for (int32 p = 0; p < PolygonCount; ++p)
		{
			int32 SlotIdx = 0;
			if (MaterialElement)
			{
				if (MaterialElement->GetMappingMode() == FbxGeometryElement::eAllSame)
				{
					SlotIdx = MaterialElement->GetIndexArray().GetAt(0);
				}
				else if (MaterialElement->GetMappingMode() == FbxGeometryElement::eByPolygon)
				{
					SlotIdx = MaterialElement->GetIndexArray().GetAt(p);
				}
			}
			if (SlotIdx < 0 || SlotIdx >= MaterialCount)
			{
				SlotIdx = 0;
			}

			const int32 CornerIndices[3] = { 0, 2, 1 };

			for (int32 c = 0; c < 3; ++c)
			{
				int32 Corner = CornerIndices[c];
				int32 CpIndex = Mesh->GetPolygonVertex(p, Corner);

				FSkeletalVertex Vertex;
				FVector Pos = ToEnginePosition(ControlPoints[CpIndex], UpVector, UpSign);
				Vertex.Position = Pos;

				OutMesh.AABB.Expand(FBox{ Pos, Pos });

				FbxVector4 FbxNormal;
				if (Mesh->GetPolygonVertexNormal(p, Corner, FbxNormal))
				{
					Vertex.Normal = ToEngineDirection(FbxNormal, UpVector, UpSign);
				}
				else
				{
					Vertex.Normal = FVector(0.f, 0.f, 1.f);
				}

				if (UVSetName)
				{
					FbxVector2 FbxUV;
					bool bUnmapped = false;
					if (Mesh->GetPolygonVertexUV(p, Corner, UVSetName, FbxUV, bUnmapped))
					{
						Vertex.UV.X = static_cast<float>(FbxUV[0]);
						Vertex.UV.Y = 1.0f - static_cast<float>(FbxUV[1]);
					}
				}

				FVector Tangent = Vertex.Normal.Cross(FVector(0.f, 0.f, 1.f)).GetSafeNormal();
				if (Tangent.Length() < 0.001f)
				{
					Tangent = Vertex.Normal.Cross(FVector(0.f, 1.f, 0.f)).GetSafeNormal();
				}
				Vertex.Tangent = Tangent;

				auto Influences = ControlPointWeights[CpIndex];
				std::sort(Influences.begin(), Influences.end(), [](const FRawBoneInfluence& A, const FRawBoneInfluence& B) {
					return A.Weight > B.Weight;
				});

				float TotalWeight = 0.0f;
				int32 MaxBones = std::min(4, Influences.Num());
				for (int32 b = 0; b < MaxBones; ++b)
				{
					TotalWeight += Influences[b].Weight;
				}

				for (int32 b = 0; b < 4; ++b)
				{
					if (b < MaxBones && TotalWeight > 0.0001f)
					{
						Vertex.BoneIndices[b] = static_cast<uint8>(Influences[b].BoneIndex);
						Vertex.BoneWeights[b] = Influences[b].Weight / TotalWeight;
					}
					else
					{
						Vertex.BoneIndices[b] = 0;
						Vertex.BoneWeights[b] = 0.0f;
					}
				}

				uint32 NewVertexIndex = static_cast<uint32>(OutMesh.Vertices.Num());
				OutMesh.Vertices.Add(Vertex);
				MaterialIndices[SlotIdx].Add(NewVertexIndex);
			}
		}

		for (int32 m = 0; m < MaterialCount; ++m)
		{
			if (MaterialIndices[m].IsEmpty()) continue;

			FSkelMeshSection Section;
			Section.StartIndex = static_cast<uint32>(OutMesh.Indices.Num());
			Section.IndexCount = static_cast<uint32>(MaterialIndices[m].Num());
			Section.MaterialSlotIndex = static_cast<uint32>(MaterialBaseIndex + m);

			OutMesh.Indices.Append(MaterialIndices[m]);
			OutMesh.Sections.Add(Section);
		}
	}

	FString Error;
	if (!OutMesh.Validate(Error))
	{
		HTR_LOG(Error, "[FFbxImporter] Skeletal mesh validation failed: {}", Error);
		return false;
	}

	return true;
}

bool FFbxImporter::ProcessSkeleton(fbxsdk::FbxScene* Scene, FSkeletalMeshData& OutMesh)
{
	if (!Scene) return false;

	OutMesh.Bones.Empty();
	OutMesh.InverseBindPoseMatrices.Empty();

	FbxGlobalSettings& GlobalSettings = Scene->GetGlobalSettings();
	int UpSign = 1;
	FbxAxisSystem::EUpVector UpVector = GlobalSettings.GetAxisSystem().GetUpVector(UpSign);

	FbxNode* RootNode = Scene->GetRootNode();
	if (!RootNode) return false;

	TArray<FbxNode*> RootBones;
	for (int32 i = 0; i < RootNode->GetChildCount(); ++i)
	{
		FindRootBones(RootNode->GetChild(i), RootBones);
	}

	if (RootBones.IsEmpty())
	{
		HTR_LOG(Warning, "[FFbxImporter] No skeleton (bones) found in FBX scene!");
		return false;
	}

	TArray<FMatrix> GlobalMatrices;
	for (FbxNode* RootBoneNode : RootBones)
	{
		BuildBoneHierarchy(RootBoneNode, -1, OutMesh, UpVector, UpSign, GlobalMatrices);
	}

	return true;
}
