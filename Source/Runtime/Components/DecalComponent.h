#pragma once

#include "SceneComponent.h"
#include "Render/Texture2D.h"

class FDecalSceneProxy;
class FLineBatcher;


class UDecalComponent : public USceneComponent
{
    DECLARE_CLASS(UDecalComponent, USceneComponent)
    REFLECT_START(UDecalComponent)
        PROPERTY(DecalTexture)
        PROPERTY(DecalSize)
        PROPERTY(DecalColor)
        PROPERTY(SortOrder)
        PROPERTY(bAdditive)
    REFLECT_END()

public:
    UDecalComponent();
    bool ShouldCreateRenderState() const override;
    void CreateRenderState() override;
    void DestroyRenderState() override;
    void Serialize(json& Handle, bool bIsLoading) override;
    FDecalSceneProxy* CreateSceneProxy() const;

    UTexture2D* GetDecalTexture() const;
    void SetDecalTexture(UTexture2D* InDecalTexture);
    void SetDecalColor(const FLinearColor& InDecalColor);
    void SetSortOrder(int32 InSortOrder);

    FMatrix GetTransformIncludingDecalSize() const;   // 프록시와 셰이더에서 사용
    void DrawDebug(FLineBatcher* LineBatcher) const;

    FDecalSceneProxy* SceneProxy = nullptr;
protected:
    UTexture2D* DecalTexture = nullptr;

private:
    
    FVector             DecalSize = FVector(1.0f, 1.0f, 1.0f);
    FLinearColor        DecalColor = FLinearColor(1, 1, 1, 1);
    int32               SortOrder = 0;
    bool                bAdditive = false;
};