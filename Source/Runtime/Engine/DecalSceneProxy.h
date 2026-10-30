#pragma once

#include "Components/DecalComponent.h"

#include "SceneManagement.h"

class FDecalSceneProxy
{
public:
	FDecalSceneProxy(const UDecalComponent* InDecalComponent);

	const UDecalComponent* GetDecalComponent() const;
private:
	friend class FScene;

	const UDecalComponent* DecalComponent = nullptr;
    int32 PackedIndex = INDEX_NONE;
};






