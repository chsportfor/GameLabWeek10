#include "EnginePCH.h"
#include "FakeSpotLightActor.h"

#include "Asset/AssetManager.h"

namespace
{
    constexpr const char* FakeSpotLightTexture = "Textures/Decal/FakeSpotLight.png";

    const FVector DefaultDecalSize(3.0f, 2.0f, 2.0f);
}

AFakeSpotLightActor::AFakeSpotLightActor()
{
    UDecalComponent* Decal = GetDecalComponent();
    Decal->SetAdditive(true);
    Decal->SetDecalSize(DefaultDecalSize);
    Decal->SetDecalColor(FLinearColor(0.6f, 0.52f, 0.4f, 1.0f));
    Decal->SetDecalTexture(UAssetManager::GetAssetByPath<UTexture2D>(FakeSpotLightTexture));
    Decal->SetFadeScreenSize(0.0f);
    Decal->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
}
