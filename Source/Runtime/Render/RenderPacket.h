#pragma once
#include "Math/Transform.h"
#include "Mesh.h"
#include "Shader.h"
#include "Material.h"
#include <array>
#include <utility>

class FPrimitiveSceneProxy;
class FVertexBuffer;
class FIndexBuffer;
inline constexpr uint32 InvalidObjectSlot = ~0u;

struct FRenderPacket
{
    const FPrimitiveSceneProxy* Proxy = nullptr;
    const FMatrix* Model = nullptr;

    // 기하 버퍼 정보: StaticMesh 또는 직접 지정된 동적/커스텀 버퍼
    UStaticMesh* Mesh = nullptr;
    FVertexBuffer* CustomVertexBuffer = nullptr;
    FIndexBuffer* CustomIndexBuffer = nullptr;

    UMaterial* Material = nullptr;
    const void* MaterialParamData = nullptr;
    float CameraToParticleDistance = 0.0f;
    uint32 MaterialParamDataSize = 0;
    uint32 StartIndex = 0;
    uint32 IndexCount = 0; // 0이면 전체 인덱스 버퍼의 개수 사용
    uint32 Slot = InvalidObjectSlot;
    uint8 LODIndex = 0;
    bool bOccludedByGpu = false;

    // 유효한 기하 데이터가 있는지 확인
    bool HasGeometry() const
    {
        return (Mesh != nullptr) || (CustomVertexBuffer != nullptr);
    }

    // 렌더링에 사용할 정점 버퍼 반환 
    FVertexBuffer* GetVertexBuffer() const
    {
        if (CustomVertexBuffer) return CustomVertexBuffer;
        if (Mesh) return Mesh->GetVertexBuffer(LODIndex);
        return nullptr;
    }

    // 렌더링에 사용할 인덱스 버퍼 반환
    FIndexBuffer* GetIndexBuffer() const
    {
        if (CustomIndexBuffer) return CustomIndexBuffer;
        if (Mesh) return Mesh->GetIndexBuffer(LODIndex);
        return nullptr;
    }

    // 실제 드로우할 인덱스 개수 계산
    uint32 GetEffectiveIndexCount() const
    {
        if (IndexCount != 0) return IndexCount;
        if (const FIndexBuffer* IB = GetIndexBuffer())
        {
            return IB->GetIndexCount();
        }
        return 0;
    }

    // 정렬을 위한 지오메트리 고유 ID
    uint16 GetGeometrySortID() const
    {
        if (Mesh) return Mesh->SortID;
        if (CustomVertexBuffer)
        {
            return static_cast<uint16>(reinterpret_cast<uintptr_t>(CustomVertexBuffer) & 0xFFFF);
        }
        return 0;
    }
};

// Shared bindings, with one ordinary indexed draw per item.
struct FStaticDrawItem
{
    const FPrimitiveSceneProxy* Proxy;
    uint32 Slot;
    uint32 StartIndex;
    uint32 IndexCount;
    uint32 bOccludedByGpu;
};

struct FStaticDrawGroup
{
    UMaterial* Material = nullptr;
    UStaticMesh* Mesh = nullptr;
    uint8 LODIndex = 0;
    std::vector<FStaticDrawItem> Items;
};

// Owns the current frame's billboard/particle matrices together with their packets.
// Blocks keep matrix addresses stable when either array grows.
class FRenderQueue : public TArray<FRenderPacket>
{
    using FPackets = TArray<FRenderPacket>;
    static constexpr uint32 MatricesPerBlock = 256;
    using FMatrixBlock = std::array<FMatrix, MatricesPerBlock>;
    TArray<TUniquePtr<FMatrixBlock>> MatrixBlocks;
    uint32 MatrixCount = 0;
public:
    FRenderQueue() = default;
    FRenderQueue(const FRenderQueue&) = delete;
    FRenderQueue& operator=(const FRenderQueue&) = delete;
    FRenderQueue(FRenderQueue&& Other) noexcept
        : FPackets(std::move(Other)), MatrixBlocks(std::move(Other.MatrixBlocks)),
          MatrixCount(std::exchange(Other.MatrixCount, 0)) {}
    FRenderQueue& operator=(FRenderQueue&& Other) noexcept
    {
        if (this != &Other)
        {
            FPackets::operator=(std::move(Other));
            MatrixBlocks = std::move(Other.MatrixBlocks);
            MatrixCount = std::exchange(Other.MatrixCount, 0);
        }
        return *this;
    }
    const FMatrix* StoreWorldMatrix(const FMatrix& World)
    {
        const uint32 BlockIndex = MatrixCount / MatricesPerBlock;
        if (BlockIndex >= static_cast<uint32>(MatrixBlocks.Num()))
            MatrixBlocks.Add(MakeUnique<FMatrixBlock>());
        FMatrix& Stored = (*MatrixBlocks[BlockIndex])[MatrixCount % MatricesPerBlock];
        Stored = World;
        ++MatrixCount;
        return &Stored;
    }
    void Reset()
    {
        FPackets::Reset();
        MatrixCount = 0;
    }
};
