#include "EnginePCH.h"
#include "SkeletalMeshComponent.h"
#include "Render/SkeletalMesh.h"
#include "Render/SkeletalMeshData.h"
#include "Asset/AssetManager.h"
#include "Render/RenderCommand.h"
#include "Engine/PrimitiveSceneProxy.h"

USkeletalMeshComponent::USkeletalMeshComponent()
{
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
}

void USkeletalMeshComponent::BeginPlay()
{
	Super::BeginPlay();
	UpdateSkinningMatrices();
	bSkinningDirty = true;
}

void USkeletalMeshComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
	UpdateSkinningMatrices();
	bSkinningDirty = true;
}

const FSkeletalMeshData* USkeletalMeshComponent::GetSkeletalMeshData() const
{
	return SkeletalMesh ? &SkeletalMesh->GetMeshData() : nullptr;
}

void USkeletalMeshComponent::ResetPoseToBindPose()
{
	if (!SkeletalMesh)
	{
		BoneTransforms.Empty();
		SkinningMatrices.Empty();
		DynamicVertexBuffer.reset();
		bSkinningDirty = true;
		return;
	}

	const auto& Bones = SkeletalMesh->GetBones();
	const int32 BoneCount = Bones.Num();
	BoneTransforms.SetNum(BoneCount);
	for (int32 i = 0; i < BoneCount; ++i)
	{
		BoneTransforms[i] = Bones[i].LocalTransform;
	}
	UpdateSkinningMatrices();
	bSkinningDirty = true;
}

void USkeletalMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
	if (SkeletalMesh == InSkeletalMesh)
		return;

	SkeletalMesh = InSkeletalMesh;
	ClearOverrideMaterials();
	DynamicVertexBuffer.reset();
	bSkinningDirty = true;

	ResetPoseToBindPose();

	MarkRenderStateDirty();
	OnTransformDirty();
}

FBox USkeletalMeshComponent::CalcLocalBounds() const
{
	return SkeletalMesh ? SkeletalMesh->GetBounds() : Super::CalcLocalBounds();
}

int32 USkeletalMeshComponent::GetNumMaterials() const
{
	return SkeletalMesh ? static_cast<int32>(SkeletalMesh->GetMeshData().MaterialSlots.Num()) : 0;
}

FString USkeletalMeshComponent::GetMaterialSlotName(int32 SlotIndex) const
{
	if (!SkeletalMesh || SlotIndex < 0 || SlotIndex >= GetNumMaterials())
		return FString();
	return SkeletalMesh->GetMeshData().MaterialSlots[SlotIndex].Name;
}

UMaterial* USkeletalMeshComponent::GetDefaultMaterial(int32 SlotIndex) const
{
	return SkeletalMesh ? SkeletalMesh->GetMaterial(static_cast<uint32>(SlotIndex)) : nullptr;
}

void USkeletalMeshComponent::UpdateSkinningMatrices()
{
	if (!SkeletalMesh)
	{
		SkinningMatrices.Empty();
		return;
	}

	const auto& Bones = SkeletalMesh->GetBones();
	const auto& InvBindMatrices = SkeletalMesh->GetInverseBindPoseMatrices();
	const int32 BoneCount = Bones.Num();

	if (BoneTransforms.Num() != BoneCount)
	{
		return;
	}

	TArray<FMatrix> ComponentSpaceTransforms;
	ComponentSpaceTransforms.SetNum(BoneCount);
	SkinningMatrices.SetNum(BoneCount);

	for (int32 i = 0; i < BoneCount; ++i)
	{
		FMatrix LocalMat = BoneTransforms[i].GetLocalMatrix();
		const int32 ParentIdx = Bones[i].ParentIndex;

		if (ParentIdx >= 0 && ParentIdx < i)
		{
			ComponentSpaceTransforms[i] = LocalMat * ComponentSpaceTransforms[ParentIdx];
		}
		else
		{
			ComponentSpaceTransforms[i] = LocalMat;
		}

		if (i < InvBindMatrices.Num())
		{
			SkinningMatrices[i] = InvBindMatrices[i] * ComponentSpaceTransforms[i];
		}
		else
		{
			SkinningMatrices[i] = FMatrix::Identity;
		}
	}
}

void USkeletalMeshComponent::DeformVerticesCPU()
{
	const FSkeletalMeshData* MeshData = GetSkeletalMeshData();

	if (!MeshData)
	{
		DeformedVertices.Empty();
		return;
	}

	const int32 VertexCount = MeshData->Vertices.Num();
	DeformedVertices.SetNum(VertexCount);

	const int32 NumSkinningMats = SkinningMatrices.Num();

	for (int32 i = 0; i < VertexCount; ++i)
	{
		const FSkeletalVertex& SrcVertex = MeshData->Vertices[i];
		FVector DeformedPosition = FVector::ZeroVector;
		FVector DeformedNormal = FVector::ZeroVector;
		FVector DeformedTangent = FVector::ZeroVector;

		for (int32 j = 0; j < 4; ++j)
		{
			const float Weight = SrcVertex.BoneWeights[j];
			if (Weight <= 0.0001f)
			{
				continue;
			}
			const uint8 BoneIndex = SrcVertex.BoneIndices[j];
			const FMatrix& SkinningMat = (BoneIndex < NumSkinningMats) ? SkinningMatrices[BoneIndex] : FMatrix::Identity;

			DeformedPosition += SkinningMat.TransformPosition(SrcVertex.Position) * Weight;
			DeformedNormal += SkinningMat.TransformVectorNoScale(SrcVertex.Normal) * Weight;
			DeformedTangent += SkinningMat.TransformVectorNoScale(SrcVertex.Tangent) * Weight;
		}

		DeformedVertices[i].Position = DeformedPosition;
		DeformedVertices[i].Normal = DeformedNormal.GetSafeNormal();
		const FVector SafeTangent = DeformedTangent.GetSafeNormal();
		DeformedVertices[i].Tangent = FVector4(SafeTangent.X, SafeTangent.Y, SafeTangent.Z, 1.0f);
		DeformedVertices[i].UV = SrcVertex.UV;
		DeformedVertices[i].Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	}

	const uint32 BufferSize = static_cast<uint32>(VertexCount * sizeof(FVertex));
	if (!DynamicVertexBuffer || DynamicVertexBuffer->GetBufferSize() != BufferSize)
	{
		DynamicVertexBuffer = RenderCommand::CreateDynamicVertexBuffer(BufferSize, sizeof(FVertex));
	}

	if (DynamicVertexBuffer && VertexCount > 0)
	{
		RenderCommand::UpdateBufferData(DynamicVertexBuffer.get(), DeformedVertices.GetData(), BufferSize);
	}
}

void USkeletalMeshComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue)
{
	SubmitToRenderQueue(RenderQueue, 0);
}

void USkeletalMeshComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue, uint32 LODIndex)
{
	if (!SkeletalMesh)
		return;

	if (bSkinningDirty)
	{
		DeformVerticesCPU();
		bSkinningDirty = false;
	}

	if (!DynamicVertexBuffer || !SkeletalMesh->IndexBuffer)
		return;

	const FSkeletalMeshData& Data = SkeletalMesh->GetMeshData();
	for (const FSkelMeshSection& Section : Data.Sections)
	{
		UMaterial* Material = GetMaterial(static_cast<int32>(Section.MaterialSlotIndex));
		if (!Material)
			continue;

		FRenderPacket& Packet = RenderQueue.AddDefaulted_GetRef();
		Packet.Proxy = GetSceneProxy();
		Packet.CustomVertexBuffer = DynamicVertexBuffer.get();
		Packet.CustomIndexBuffer = SkeletalMesh->IndexBuffer.get();
		Packet.Material = Material;
		Packet.StartIndex = Section.StartIndex;
		Packet.IndexCount = Section.IndexCount;
		Packet.LODIndex = static_cast<uint8>(LODIndex);
	}
}
