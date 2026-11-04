#include "EnginePCH.h"
#include "SkinnedMeshComponent.h"
#include "Render/RenderCommand.h"
#include "Collision/Ray.h"

void USkinnedMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
	if (SkeletalMesh == InSkeletalMesh)
		return;

	SkeletalMesh = InSkeletalMesh;
	CurrentLocalTransforms.Reset();
	CurrentGlobalMatrices.Reset();
	SkinnedVertexBuffer.reset();
	SkinnedVertices.Reset();
	SkinnedIndices.Reset();
	bHasSkinnedBounds = false;
	ResetPoseToBindPose();
	ClearOverrideMaterials();
	MarkRenderStateDirty();
	OnTransformDirty();
}

void USkinnedMeshComponent::ResetPoseToBindPose()
{
	CurrentLocalTransforms.Reset();
	if (SkeletalMesh)
	{
		const TArray<FSkeletalBone>& Bones = SkeletalMesh->MeshData.Bones;
		CurrentLocalTransforms.Reserve(Bones.Num());
		for (const FSkeletalBone& Bone : Bones)
			CurrentLocalTransforms.Add(Bone.RefPoseTransform);
	}
	UpdateSkinning();
}

FOutlineGeometry USkinnedMeshComponent::GetOutlineGeometry() const
{
	if (!SkeletalMesh) return {};
	return { SkinnedVertexBuffer.get(), SkeletalMesh->IndexBuffer.get() };
}

bool USkinnedMeshComponent::LineTraceComponentLocal(const FRay& LocalRay, float& OutT)
{
	const uint32 VertexCount = static_cast<uint32>(SkinnedVertices.Num());
	const uint32 IndexCount = static_cast<uint32>(SkinnedIndices.Num());
	float NearestT = OutT;
	for (uint32 Index = 0; Index + 2 < IndexCount; Index += 3)
	{
		const uint32 I0 = SkinnedIndices[Index];
		const uint32 I1 = SkinnedIndices[Index + 1];
		const uint32 I2 = SkinnedIndices[Index + 2];
		if (I0 >= VertexCount || I1 >= VertexCount || I2 >= VertexCount)
			continue;

		float T;
		if (RayIntersectsTriangle(LocalRay, SkinnedVertices[I0].Position, SkinnedVertices[I1].Position,
			SkinnedVertices[I2].Position, T) && T < NearestT)
			NearestT = T;
	}

	if (NearestT >= OutT) return false;
	OutT = NearestT;
	return true;
}

const FTransform* USkinnedMeshComponent::GetBoneLocalTransform(int32 BoneIndex) const
{
	return CurrentLocalTransforms.IsValidIndex(BoneIndex) ? &CurrentLocalTransforms[BoneIndex] : nullptr;
}

bool USkinnedMeshComponent::SetBoneLocalTransform(int32 BoneIndex, const FTransform& InTransform)
{
	if (!SkeletalMesh || !CurrentLocalTransforms.IsValidIndex(BoneIndex)) return false;

	CurrentLocalTransforms[BoneIndex] = InTransform;
	UpdateSkinning();
	return true;
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
	CurrentGlobalMatrices.Reset();
	if (!SkeletalMesh)
	{
		SkinnedVertexBuffer.reset();
		bHasSkinnedBounds = false;
		return;
	}
	TArray<FVertex> ResultVertex;
	TArray<uint32> ResultIndices;
	const TArray<FSkeletalBone>& BoneArray = SkeletalMesh->MeshData.Bones;
	if (CurrentLocalTransforms.Num() != BoneArray.Num()) return;


	CurrentGlobalMatrices.Reserve(BoneArray.Num());
	for (int32 BoneIndex = 0; BoneIndex < BoneArray.Num(); ++BoneIndex)
	{
		FMatrix CurrentMatrix = CurrentLocalTransforms[BoneIndex].GetLocalMatrix();
		const int32 ParentIndex = BoneArray[BoneIndex].ParentIndex;
		if (ParentIndex != -1)
		{
			if (ParentIndex < 0 || ParentIndex >= BoneIndex) return;
			CurrentMatrix = CurrentMatrix * CurrentGlobalMatrices[ParentIndex];
		}
		CurrentGlobalMatrices.Add(CurrentMatrix);
	}

	for (int i = 0; i < SkeletalMesh->MeshData.Meshes.Num(); i++)
	{
		const FSkeletalMesh& Mesh = SkeletalMesh->MeshData.Meshes[i];
		const uint32 BaseVertex = static_cast<uint32>(ResultVertex.Num());
		const TArray<FMatrix>& InvBindArray = Mesh.InverseBindPoseMat;
		const FMatrix MeshNormalMatrix = Mesh.MeshTransform.Inverse().GetTransposed();
		for (int j = 0; j < SkeletalMesh->MeshData.Meshes[i].Vertices.Num(); j++)
		{
			TArray<FVector> SkinPos;
			TArray<FVector> SkinNormal;
			TArray<FVector> SkinTangent;
			FVector ResultPos;
			FVector ResultNormal;
			float TotalWeight = 0.0f;
			FVertex Vertex = SkeletalMesh->MeshData.Meshes[i].Vertices[j].Vertex;
			for (int k = 0; k < 4; k++)
			{
				uint32 BoneIndex = SkeletalMesh->MeshData.Meshes[i].Vertices[j].BoneIndices[k];
				float BoneWeight = SkeletalMesh->MeshData.Meshes[i].Vertices[j].BoneWeights[k];
				if (BoneWeight <= 0.0f || BoneIndex >= static_cast<uint32>(CurrentGlobalMatrices.Num())
					|| BoneIndex >= static_cast<uint32>(InvBindArray.Num()))
				{
					SkinPos.Add(FVector::ZeroVector);
					SkinNormal.Add(FVector::ZeroVector);
					continue;
				}
				// Keep the skinned vertex in component space for rendering, picking, and outlines.
				const FMatrix SkinMatrix = InvBindArray[BoneIndex] * CurrentGlobalMatrices[BoneIndex];
				FMatrix NormalMatrix = SkinMatrix.Inverse().GetTransposed();		// Normal의 경우는 NormalMap 적용처럼 Scale 변환 시 왜곡될수 있으므로 역전치 적용해야 함
				FVector SkinnedPosition = SkinMatrix.TransformPosition(Vertex.Position);
				FVector SkinnedNormal = NormalMatrix.TransformVector(Vertex.Normal);
				SkinPos.Add(SkinnedPosition);
				SkinNormal.Add(SkinnedNormal);

			}
			for (int k = 0; k < 4; k++)
			{
				uint32 BoneIndex = SkeletalMesh->MeshData.Meshes[i].Vertices[j].BoneIndices[k];
				float BoneWeight = SkeletalMesh->MeshData.Meshes[i].Vertices[j].BoneWeights[k];
				if (BoneWeight <= 0.0f || BoneIndex >= static_cast<uint32>(CurrentGlobalMatrices.Num())
					|| BoneIndex >= static_cast<uint32>(InvBindArray.Num())) continue;
				ResultPos += (SkinPos[k] * BoneWeight);
				ResultNormal += (SkinNormal[k] * BoneWeight);
				TotalWeight += BoneWeight;
			}
			if (TotalWeight > 0.0f)
			{
				Vertex.Position = ResultPos / TotalWeight;
				Vertex.Normal = ResultNormal.GetSafeNormal();
			}
			else
			{
				// Unweighted vertices follow the mesh node instead of a bone.
				Vertex.Position = Mesh.MeshTransform.TransformPosition(Vertex.Position);
				Vertex.Normal = MeshNormalMatrix.TransformVector(Vertex.Normal).GetSafeNormal();
			}
			ResultVertex.Add(Vertex);
		}
		// 파트 로컬 인덱스를 합쳐진 ResultVertex 기준으로 보정한다.
		for (uint32 Index : Mesh.Indices)
		{
			ResultIndices.Add(BaseVertex + Index);
		}
	}
	if (ResultVertex.IsEmpty()) return;

	BuildMeshTangentSpace(ResultVertex, ResultIndices);

	const uint32 BufferSize = static_cast<uint32>(ResultVertex.Num()) * sizeof(FVertex);

	// 처음이거나 기존 버퍼 용량이 부족하면 생성
	if (!SkinnedVertexBuffer || SkinnedVertexBuffer->GetBufferSize() < BufferSize)
	{
		SkinnedVertexBuffer = RenderCommand::CreateDynamicVertexBuffer(BufferSize, sizeof(FVertex));
	}

	if (!SkinnedVertexBuffer || !SkinnedVertexBuffer->GetBuffer()) return;

	// 배열 전체를 버퍼에 복사
	RenderCommand::UpdateBufferData(SkinnedVertexBuffer.get(), ResultVertex.GetData(), BufferSize);

	// 이동한 정점에 맞춰 Proxy의 Bounds와 그림자 갱신을 알린다.
	SkinnedBounds = FBox{ ResultVertex[0].Position, ResultVertex[0].Position };
	for (const FVertex& Vertex : ResultVertex)
	{
		SkinnedBounds.Min.X = std::min(SkinnedBounds.Min.X, Vertex.Position.X);
		SkinnedBounds.Min.Y = std::min(SkinnedBounds.Min.Y, Vertex.Position.Y);
		SkinnedBounds.Min.Z = std::min(SkinnedBounds.Min.Z, Vertex.Position.Z);
		SkinnedBounds.Max.X = std::max(SkinnedBounds.Max.X, Vertex.Position.X);
		SkinnedBounds.Max.Y = std::max(SkinnedBounds.Max.Y, Vertex.Position.Y);
		SkinnedBounds.Max.Z = std::max(SkinnedBounds.Max.Z, Vertex.Position.Z);
	}
	SkinnedVertices = std::move(ResultVertex);
	SkinnedIndices = std::move(ResultIndices);
	bHasSkinnedBounds = true;
	OnTransformDirty();
}
