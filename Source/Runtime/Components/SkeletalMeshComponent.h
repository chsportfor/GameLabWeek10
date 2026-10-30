#pragma once

#include "EnginePCH.h"
#include "SkinnedMeshComponent.h"
#include "Render/Mesh.h"

class SkeletalMeshComponent : public USkinnedMeshComponent
{

private:
	USkeletalMesh* SkeletalMesh;
};
