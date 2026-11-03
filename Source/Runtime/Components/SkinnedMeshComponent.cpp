#include "EnginePCH.h"
#include "SkinnedMeshComponent.h"
#include "Render/RenderCommand.h"
#include <filesystem>

void USkinnedMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
	if (SkeletalMesh == InSkeletalMesh)
		return;

	SkeletalMesh = InSkeletalMesh;
	UpdateSkinning();
	ClearOverrideMaterials();
	MarkRenderStateDirty();
	OnTransformDirty();
}

int32 USkinnedMeshComponent::GetNumMaterials() const
{
	return SkeletalMesh ? SkeletalMesh->Materials.Num() : 0;
}

FString USkinnedMeshComponent::GetMaterialSlotName(int32 SlotIndex) const
{
	if (!SkeletalMesh || SlotIndex < 0 || SlotIndex >= GetNumMaterials()) return {};
	// Materials는 파트별 MaterialSlots를 같은 순서로 이어 붙인 배열이다.
	for (const FSkeletalMesh& Part : SkeletalMesh->MeshData.Meshes)
	{
		if (SlotIndex < Part.MaterialSlots.Num()) return Part.MaterialSlots[SlotIndex].Name;
		SlotIndex -= Part.MaterialSlots.Num();
	}
	return {};
}

UMaterial* USkinnedMeshComponent::GetDefaultMaterial(int32 SlotIndex) const
{
	return SkeletalMesh && SlotIndex >= 0
		? SkeletalMesh->GetMaterial(static_cast<uint32>(SlotIndex)) : nullptr;
}

void USkinnedMeshComponent::UpdateSkinning()
{
	if (!SkeletalMesh)
	{
		SkinnedVertexBuffer.reset();
		return;
	}

	// 임시 스키닝 검증: false 또는 0도로 바꾸면 원래 Reference Pose로 출력한다.
	constexpr bool bEnableLowPolyTestPose = true;
	constexpr float TestForearmRollDegrees = 60.0f;
	const bool bUseLowPolyTestPose = bEnableLowPolyTestPose
		&& std::filesystem::path(SkeletalMesh->GetPath()).filename().generic_string()
			== "finallowpolycharacterrigged.fbx";
	TArray<FVertex> ResultVertex;
	TArray<FSkeletalBone> BoneArray = SkeletalMesh->MeshData.Bones;
	TArray<FMatrix> CurrentGlobalMatrices;
	for (int i = 0; i < BoneArray.Num(); i++)
	{
		FTransform RefTransform = BoneArray[i].RefPoseTransform;
		FMatrix RefMatrix = RefTransform.GetLocalMatrix();
		if (bUseLowPolyTestPose && BoneArray[i].Name == "forearm.L")
		{
			// 본 자체 축의 추가 회전만 적용한다. 원본 Ref와 역바인드 행렬은 변경하지 않는다.
			const FMatrix TestRotation = FRotator(0.0f, 0.0f, TestForearmRollDegrees).Quaternion().ToFMatrix();
			RefMatrix = TestRotation * RefMatrix;
		}
		if (BoneArray[i].ParentIndex != -1)
		{
			RefMatrix = RefMatrix * CurrentGlobalMatrices[BoneArray[i].ParentIndex];
		}
		CurrentGlobalMatrices.Add(RefMatrix);
	}


	for (int i = 0; i < SkeletalMesh->MeshData.Meshes.Num(); i++)
	{
		const FSkeletalMesh& Mesh = SkeletalMesh->MeshData.Meshes[i];
		const TArray<FMatrix>& InvBindArray = Mesh.InverseBindPoseMat;
		// FBX 내부 메시 노드는 고정으로 취급하고, 결과를 원래 메시 로컬 공간으로 돌린다.
		const FMatrix InverseMeshTransform = Mesh.MeshTransform.Inverse();
		for (int j = 0; j < SkeletalMesh->MeshData.Meshes[i].Vertices.Num(); j++)
		{
			TArray<FVector> SkinPos;
			TArray<FVector> SkinNormal;
			TArray<FVector> SkinTangent;
			FVector ResultPos;
			FVertex Vertex = SkeletalMesh->MeshData.Meshes[i].Vertices[j].Vertex;
			for (int k = 0; k < 4; k++)
			{
				uint32 BoneIndex = SkeletalMesh->MeshData.Meshes[i].Vertices[j].BoneIndices[k];
				float BoneWeight = SkeletalMesh->MeshData.Meshes[i].Vertices[j].BoneWeights[k];
				FTransform RefTrans = BoneArray[BoneIndex].RefPoseTransform;
				FMatrix InvBind = InvBindArray[BoneIndex];
				FMatrix SkinMatrix = InvBind * CurrentGlobalMatrices[BoneIndex] * InverseMeshTransform;
				FVector SkinnedPosition = SkinMatrix.TransformPosition(Vertex.Position);
				SkinPos.Add(SkinnedPosition);

			}
			for (int k = 0; k < 4; k++)
			{
				uint32 BoneIndex = SkeletalMesh->MeshData.Meshes[i].Vertices[j].BoneIndices[k];
				float BoneWeight = SkeletalMesh->MeshData.Meshes[i].Vertices[j].BoneWeights[k];
				ResultPos += (SkinPos[k] * BoneWeight);
			}
			Vertex.Position = ResultPos;
			ResultVertex.Add(Vertex);
		}
	}
	if (ResultVertex.IsEmpty()) return;

	const uint32 BufferSize = static_cast<uint32>(ResultVertex.Num()) * sizeof(FVertex);

	// 처음이거나 기존 버퍼 용량이 부족하면 생성
	if (!SkinnedVertexBuffer || SkinnedVertexBuffer->GetBufferSize() < BufferSize)
	{
		SkinnedVertexBuffer = RenderCommand::CreateDynamicVertexBuffer(BufferSize, sizeof(FVertex));
	}

	if (!SkinnedVertexBuffer || !SkinnedVertexBuffer->GetBuffer()) return;

	// 배열 전체를 버퍼에 복사
	RenderCommand::UpdateBufferData(SkinnedVertexBuffer.get(), ResultVertex.GetData(), BufferSize);
}
