#include "EnginePCH.h"
#include "DecalSceneProxy.h"


FDecalSceneProxy::FDecalSceneProxy(const UDecalComponent* InDecalComponent) : 
	DecalComponent(InDecalComponent){}

const UDecalComponent* FDecalSceneProxy::GetDecalComponent() const
{
	return DecalComponent; 
}


