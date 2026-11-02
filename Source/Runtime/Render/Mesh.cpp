#include "EnginePCH.h"
#include "Mesh.h"
#include "Renderer.h"
#include "Asset/AssetManager.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/SkeletonSceneProxy.h"
#include <algorithm>
#include <cmath>
#include <limits>

UStaticMesh::~UStaticMesh()
{
    for (FPrimitiveSceneProxy* Proxy : RenderProxies)  Proxy->OnMeshDestroyed();
	VertexBuffer = nullptr;
	IndexBuffer = nullptr;
}

UMaterial* UStaticMesh::GetMaterial(uint32 SlotIndex) const
{
	if (SlotIndex < Materials.size() && Materials[SlotIndex]) {
		return Materials[SlotIndex];
	}
	return UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
}

size_t UStaticMesh::FMaterialSlotsHash::operator()(const std::vector<UMaterial*>& Slots) const
{
    size_t Hash = 0;
    for (const UMaterial* Material : Slots)
        Hash ^= std::hash<const UMaterial*>{}(Material) + 0x9e3779b9u + (Hash << 6) + (Hash >> 2);
    return Hash;
}

void UStaticMesh::RebuildRenderData()
{
    RenderBounds = MeshData.AABB;
    for (const auto& LOD : AdditionalLODs)
    {
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            RenderBounds.Min[Axis] = std::min(RenderBounds.Min[Axis], LOD.Data.AABB.Min[Axis]);
            RenderBounds.Max[Axis] = std::max(RenderBounds.Max[Axis], LOD.Data.AABB.Max[Axis]);
        }
    }
    LocalLODSphere.Center = (RenderBounds.Min + RenderBounds.Max) * 0.5f;
    double RadiusSquared = 0.0;
    for (uint32 LOD = 0; LOD < GetLODCount(); ++LOD)
    {
        for (const FVertex& Vertex : GetMeshData(LOD).Vertices)
        {
            const double X = static_cast<double>(Vertex.Position.X) - LocalLODSphere.Center.X;
            const double Y = static_cast<double>(Vertex.Position.Y) - LocalLODSphere.Center.Y;
            const double Z = static_cast<double>(Vertex.Position.Z) - LocalLODSphere.Center.Z;
            RadiusSquared = std::max(RadiusSquared, X * X + Y * Y + Z * Z);
        }
    }
    // Include a small geometric margin and round outwards.
    LocalLODSphere.RadiusSquared = std::nextafter(
        static_cast<float>(RadiusSquared * (1.0 + 2e-5)), std::numeric_limits<float>::infinity());
    RenderStates.clear();
    for (FPrimitiveSceneProxy* Proxy : RenderProxies)
        Proxy->OnMeshRenderDataChanged();
}

TSharedPtr<const FMeshRenderState> UStaticMesh::GetRenderState(const std::vector<UMaterial*>& Materials)
{
    auto& WeakState = RenderStates[Materials];
    if (auto State = WeakState.lock()) return State;
    auto State = MakeShared<FMeshRenderState>();
    State->LODCount = static_cast<uint8>(std::min(GetLODCount(), 4u));
    for (uint32 I = 0; I < 3; ++I)
        State->LODThresholdSq[I] = ScreenThresholds[I] * ScreenThresholds[I];
    for (uint32 LOD = 0; LOD < State->LODCount; ++LOD)
    {
        auto& Range = State->LODs[LOD];
        Range.FirstSection = State->Sections.Num();
        for (const FStaticMeshSection& Section : GetMeshData(LOD).Sections)
        {
            UMaterial* Material = Section.MaterialSlotIndex < Materials.size()
                ? Materials[Section.MaterialSlotIndex] : GetMaterial(Section.MaterialSlotIndex);
            if (Material) State->Sections.Add({ Material, Section.StartIndex, Section.IndexCount });
        }
        Range.NumSections = State->Sections.Num() - Range.FirstSection;
    }
    WeakState = State;
    return State;
}

void UStaticMesh::RegisterProxy(FPrimitiveSceneProxy* Proxy)
{
    RenderProxies.Add(Proxy);
}

void UStaticMesh::UnregisterProxy(FPrimitiveSceneProxy* Proxy)
{
    const auto It = std::find(RenderProxies.begin(), RenderProxies.end(), Proxy);
    if (It != RenderProxies.end())
        RenderProxies.RemoveAtSwap(static_cast<uint32>(It - RenderProxies.begin()));
}






USkeletalMesh::~USkeletalMesh()
{
	// 메시보다 오래 살아 있는 Proxy의 참조를 정리한다.
	for (FSkeletalMeshSceneProxy* Proxy : SkeletonRenderProxies)
		Proxy->OnSkeletalMeshDestroyed();
}

void USkeletalMesh::RegisterProxy(FSkeletalMeshSceneProxy* Proxy)
{
	SkeletonRenderProxies.Add(Proxy);
}

void USkeletalMesh::UnregisterProxy(FSkeletalMeshSceneProxy* Proxy)
{
	const auto It = std::find(SkeletonRenderProxies.begin(), SkeletonRenderProxies.end(), Proxy);
	if (It != SkeletonRenderProxies.end())
		SkeletonRenderProxies.RemoveAtSwap(static_cast<uint32>(It - SkeletonRenderProxies.begin()));
}

UMaterial* USkeletalMesh::GetMaterial(uint32 SlotIndex) const
{
	if (SlotIndex < Materials.size() && Materials[SlotIndex])
	{
		return Materials[SlotIndex];
	}
	return UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
}

void USkeletalMesh::RebuildRenderData()
{
	bool bHasVertex = false;
	for (int i = 0; i < MeshData.Meshes.Num(); i++)
	{
		const FSkeletalMesh& Mesh = MeshData.Meshes[i];
		for (int j = 0; j < Mesh.Vertices.Num(); j++)
		{
			const FVector& Position = Mesh.Vertices[j].Vertex.Position;
			if (!bHasVertex)
			{
				RenderBounds.Min = Position;
				RenderBounds.Max = Position;
				bHasVertex = true;
			}
			else
			{
				RenderBounds.Min.X = std::min(RenderBounds.Min.X, Position.X);
				RenderBounds.Max.X = std::max(RenderBounds.Max.X, Position.X);
				RenderBounds.Min.Y = std::min(RenderBounds.Min.Y, Position.Y);
				RenderBounds.Max.Y = std::max(RenderBounds.Max.Y, Position.Y);
				RenderBounds.Min.Z = std::min(RenderBounds.Min.Z, Position.Z);
				RenderBounds.Max.Z = std::max(RenderBounds.Max.Z, Position.Z);
			}
		}
	}
	// 정점이 없으면 이전 계산 결과를 남기지 않고 원점으로 초기화한다.
	if (!bHasVertex)
	{
		RenderBounds.Min = FVector();
		RenderBounds.Max = FVector();
	}
	for (FSkeletalMeshSceneProxy* Proxy : SkeletonRenderProxies)
		Proxy->OnMeshRenderDataChanged();
}
