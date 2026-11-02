#include "EnginePCH.h"
#include "SkeletonSceneProxy.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/Mesh.h"
#include "Engine/Scene.h"

FSkeletalMeshSceneProxy::FSkeletalMeshSceneProxy(USkeletalMeshComponent* InComponent) : FPrimitiveSceneProxy(InComponent){}

FSkeletalMeshSceneProxy::~FSkeletalMeshSceneProxy()
{
	if (SkeletalMesh) SkeletalMesh->UnregisterProxy(this);
}

void FSkeletalMeshSceneProxy::OnSkeletalMeshDestroyed()
{
	SkeletalMesh = nullptr;
	FPrimitiveSceneProxy::OnMeshDestroyed();
}

void FSkeletalMeshSceneProxy::UpdateRenderState()
{
	FPrimitiveSceneProxy::UpdateRenderState();

	USkeletalMesh* PreviousMesh = SkeletalMesh;
	auto* SkeletalMeshComponent = static_cast<USkeletalMeshComponent*>(GetComponent());
	SkeletalMesh = SkeletalMeshComponent->GetSkeletalMesh();

	if (SkeletalMesh != PreviousMesh)
	{
		if (PreviousMesh) PreviousMesh->UnregisterProxy(this);
		if (SkeletalMesh) SkeletalMesh->RegisterProxy(this);
		if (FScene* Scene = GetScene()) Scene->MarkDirty(this);
	}
	// Section별 Material과 GPU 버퍼 연결은 렌더 리소스를 준비한 뒤 추가한다.
}
