#include "EnginePCH.h"
#include "SkeletalMeshComponent.h"
#include "Render/SkeletalMesh.h"
#include "Render/SkeletalMeshData.h"
#include "Asset/AssetManager.h"
#include "Render/RenderCommand.h"
#include "Collision/Ray.h"
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
	if (bSkinningDirty)
	{
		UpdateSkinningMatrices();
	}
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

void USkeletalMeshComponent::ResetBoneToBindPose(int32 BoneIndex)
{
	if (!SkeletalMesh)
		return;

	const auto& Bones = SkeletalMesh->GetBones();
	if (BoneIndex < 0 || BoneIndex >= Bones.Num() || BoneTransforms.Num() != Bones.Num())
		return;

	BoneTransforms[BoneIndex] = Bones[BoneIndex].LocalTransform;
	UpdateSkinningMatrices();
	bSkinningDirty = true;
}

void USkeletalMeshComponent::SetBoneLocalTransform(int32 BoneIndex, const FTransform& NewLocalTransform)
{
	if (BoneIndex < 0 || BoneIndex >= BoneTransforms.Num())
		return;

	BoneTransforms[BoneIndex] = NewLocalTransform;
	UpdateSkinningMatrices();
	bSkinningDirty = true;
}

FTransform USkeletalMeshComponent::GetBoneLocalTransform(int32 BoneIndex) const
{
	if (BoneIndex >= 0 && BoneIndex < BoneTransforms.Num())
	{
		return BoneTransforms[BoneIndex];
	}
	if (SkeletalMesh && BoneIndex >= 0 && BoneIndex < static_cast<int32>(SkeletalMesh->GetNumBones()))
	{
		return SkeletalMesh->GetBones()[BoneIndex].LocalTransform;
	}
	return FTransform::Identity;
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

bool USkeletalMeshComponent::LineTraceComponentLocal(const FRay& LocalRay, float& OutT)
{
	if (!SkeletalMesh)
		return false;

	const FSkeletalMeshData& MeshData = SkeletalMesh->GetMeshData();
	const uint32 IndexCount = static_cast<uint32>(MeshData.Indices.Num());
	if (IndexCount == 0)
		return false;

	// CPU 스키닝된 정점이 준비되어 있는 경우 현재 포즈 정점으로 검사
	if (DeformedVertices.Num() == MeshData.Vertices.Num() && !DeformedVertices.IsEmpty())
	{
		bool bHit = false;
		float NearestT = OutT;
		const uint32 VertexCount = static_cast<uint32>(DeformedVertices.Num());

		for (uint32 i = 0; i + 2 < IndexCount; i += 3)
		{
			const uint32 I0 = MeshData.Indices[i];
			const uint32 I1 = MeshData.Indices[i + 1];
			const uint32 I2 = MeshData.Indices[i + 2];
			if (I0 >= VertexCount || I1 >= VertexCount || I2 >= VertexCount)
				continue;

			const FVector& V0 = DeformedVertices[I0].Position;
			const FVector& V1 = DeformedVertices[I1].Position;
			const FVector& V2 = DeformedVertices[I2].Position;

			float T = FLT_MAX;
			if (RayIntersectsTriangle(LocalRay, V0, V1, V2, T) && T < NearestT)
			{
				NearestT = T;
				bHit = true;
			}
		}

		if (bHit)
		{
			OutT = NearestT;
			return true;
		}
		return false;
	}

	// 기본 바인드 포즈 메쉬로 검사
	return RayIntersectsMesh(LocalRay, MeshData, OutT);
}

FOutlineGeometry USkeletalMeshComponent::GetOutlineGeometry()
{
	if (!SkeletalMesh || !SkeletalMesh->IndexBuffer)
		return {};

	if (bSkinningDirty)
	{
		DeformVerticesCPU();
		bSkinningDirty = false;
	}

	FVertexBuffer* VB = DynamicVertexBuffer ? DynamicVertexBuffer.get() : SkeletalMesh->VertexBuffer.get();
	if (!VB)
		return {};

	return FOutlineGeometry{
		VB,
		SkeletalMesh->IndexBuffer.get(),
		SkeletalMesh->IndexBuffer->GetIndexCount()
	};
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
	TArray<uint8> EvalState;
	ComponentSpaceTransforms.SetNum(BoneCount);
	EvalState.SetNum(BoneCount);
	SkinningMatrices.SetNum(BoneCount);

	for (int32 i = 0; i < BoneCount; ++i)
	{
		EvalState[i] = 0;
	}

	std::function<void(int32)> EvaluateBone = [&](int32 Idx)
	{
		if (Idx < 0 || Idx >= BoneCount || EvalState[Idx] == 2)
			return;
		if (EvalState[Idx] == 1)
		{
			ComponentSpaceTransforms[Idx] = FMatrix::Identity;
			EvalState[Idx] = 2;
			return;
		}

		EvalState[Idx] = 1;
		const FMatrix LocalMat = BoneTransforms[Idx].GetLocalMatrix();
		const int32 ParentIdx = Bones[Idx].ParentIndex;

		if (ParentIdx >= 0 && ParentIdx < BoneCount && ParentIdx != Idx)
		{
			EvaluateBone(ParentIdx);
			ComponentSpaceTransforms[Idx] = LocalMat * ComponentSpaceTransforms[ParentIdx];
		}
		else
		{
			ComponentSpaceTransforms[Idx] = LocalMat;
		}
		EvalState[Idx] = 2;
	};

	for (int32 i = 0; i < BoneCount; ++i)
	{
		EvaluateBone(i);

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
			const uint16 BoneIndex = SrcVertex.BoneIndices[j];
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

	CachedWorldMatrix = GetWorldMatrix();

	const FSkeletalMeshData& Data = SkeletalMesh->GetMeshData();
	for (const FSkelMeshSection& Section : Data.Sections)
	{
		UMaterial* Material = GetMaterial(static_cast<int32>(Section.MaterialSlotIndex));
		if (!Material)
			continue;

		FRenderPacket& Packet = RenderQueue.AddDefaulted_GetRef();
		Packet.Proxy = GetSceneProxy();
		Packet.Model = &CachedWorldMatrix;
		Packet.CustomVertexBuffer = DynamicVertexBuffer.get();
		Packet.CustomIndexBuffer = SkeletalMesh->IndexBuffer.get();
		Packet.Material = Material;
		Packet.StartIndex = Section.StartIndex;
		Packet.IndexCount = Section.IndexCount;
		Packet.LODIndex = static_cast<uint8>(LODIndex);
	}
}
