#pragma once

#include "Core/Types.h"
#include "Math/EngineMath.h"
#include "Render/RenderingInfo.h"
#include "Render/ViewMode.h"
#include "Editor/Gizmo/Gizmo.h"
#include "Editor/HitoriEd/EditorViewportClient.h"
#include <memory>

class FRenderer;
class USkeletalMesh;
class USkeletalMeshComponent;
class FGizmoRenderer;
class FConstantBuffer;

// 레벨의 World/선택/카메라와 독립된 스켈레탈 메시 미리보기다.
// 에셋은 AssetManager 소유. 편집 자세는 월드에 등록하지 않는 전용 컴포넌트에 보관한다.
class FSkeletalMeshViewerViewport
{
public:
	FSkeletalMeshViewerViewport();
	~FSkeletalMeshViewerViewport();
	void SetMesh(USkeletalMesh* InMesh);
	USkeletalMesh* GetMesh() const { return Mesh; }
	void SetSize(uint32 InWidth, uint32 InHeight);
	void FrameMesh();
	void Orbit(float DeltaX, float DeltaY);
	void Zoom(float WheelSteps);
	void Render(FRenderer& Renderer);
	void ReleaseResources();
	FTexture2D* GetColorTarget() const { return ColorTarget.get(); }
	int32 GetSelectedBoneIndex() const { return SelectedBoneIndex; }
	void SetSelectedBoneIndex(int32 BoneIndex);
	const FTransform* GetSelectedBoneTransform() const;
	bool SetSelectedBoneTransform(const FTransform& Transform);
	void ResetPose();
	void SetGizmoMode(EGizmoMode Mode);
	EGizmoMode GetGizmoMode() const { return BoneGizmo.GetMode(); }
	bool IsDraggingGizmo() const { return BoneGizmo.IsUsing(); }
	void CancelGizmoDrag();
	void UpdateInteraction(const FVector2& MousePosition, bool bHovered, bool bLeftClicked, bool bLeftDown, bool bPickBones);
	bool ProjectBone(int32 BoneIndex, const FVector2& DisplaySize, FVector2& OutPosition, float& OutDepth) const;
	float GetAmbientIntensity() const { return AmbientIntensity; }
	void SetAmbientIntensity(float Value);

private:
	void ResizeTargets();
	void RebuildBoneTransforms();
	void SyncGizmoTarget();
	void ApplyGizmoTransform();
	int32 PickBone(const FVector2& MousePosition) const;
	USkeletalMesh* Mesh = nullptr;
	// ReleaseResources에서 UObject 일괄 해제보다 먼저 삭제한다.
	USkeletalMeshComponent* PreviewComponent = nullptr;
	USceneComponent* GizmoTarget = nullptr;
	FGizmo BoneGizmo;
	std::unique_ptr<FGizmoRenderer> GizmoRenderer;
	std::unique_ptr<FConstantBuffer> PreviewLighting;
	std::unique_ptr<FConstantBuffer> PreviewLightGrid;
	TArray<FMatrix> BoneGlobalMatrices;
	TArray<FQuat> BoneGlobalRotations;
	int32 SelectedBoneIndex = -1;
	FViewCamera RenderedCamera{};
	FMatrix RenderedViewProjection = FMatrix::Identity;
	FMatrix GizmoViewProjection = FMatrix::Identity;
	bool bHasRenderedView = false;
	float AmbientIntensity = 1.0f;
	std::unique_ptr<FTexture2D> ColorTarget;
	std::unique_ptr<FTexture2D> DepthTarget;
	FRenderingInfo RenderingInfo{};
	uint32 Width = 1;
	uint32 Height = 1;
	bool bSizeDirty = true;
	bool bFrameRequested = false;
	FVector CameraTarget = FVector::ZeroVector;
	float CameraYaw = 0.0f;
	float CameraPitch = 10.0f;
	float CameraDistance = 5.0f;
	float MeshRadius = 1.0f;
};
