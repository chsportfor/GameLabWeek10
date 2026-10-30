#pragma once

#include "EnginePCH.h"
#include "SkeletalMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Render/Mesh.h"

class SkeletalMeshComponent : public UMeshComponent
{

private:
	USkeletalMesh* SkeletalMesh;
};