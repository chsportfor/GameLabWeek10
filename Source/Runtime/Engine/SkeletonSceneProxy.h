#pragma once

#include "EnginePCH.h"
#include "Engine/PrimitiveSceneProxy.h"

class FSkeletalMeshSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FSkeletalMeshSceneProxy(UPrimitiveComponent* InComponent): FPrimitiveSceneProxy(InComponent) {}
};