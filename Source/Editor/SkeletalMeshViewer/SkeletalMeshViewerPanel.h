#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/Gizmo/Gizmo.h"
#include "Editor/Gizmo/GizmoRenderer.h"
#include "Render/RenderingInfo.h"
#include "Render/Renderer.h"
#include "Render/RenderCommand.h"
#include "Render/Mesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/EngineString.h"
#include "Containers/Array.h"
#include "Containers/Map.h"

#include "Render/ViewMode.h"

class FGridRenderer;

class FSkeletalMeshViewerPanel : public IEditorPanel
{
public:
	FSkeletalMeshViewerPanel();
	virtual ~FSkeletalMeshViewerPanel() override;

	void SetRenderer(FRenderer* InRenderer) { Renderer = InRenderer; }
	void SetGridRenderer(FGridRenderer* InGridRenderer) { GridRenderer = InGridRenderer; }
	void SetGizmoRenderer(FGizmoRenderer* InGizmoRenderer) { GizmoRenderer = InGizmoRenderer; }

	virtual bool Init() override;
	virtual void Tick(float DeltaTime) override;
	virtual void OnRender() override;
	virtual const char* GetPanelName() const override { return "Skeletal Mesh Viewer"; }

	// ImGui BeginRenderPass(메인 스왑체인) 시작 전에 호출되어 독립 렌더 타깃에 프리뷰 씬을 렌더링
	void RenderPreview();

	// 다중 애셋 탭 오픈 및 선택
	bool OpenAsset(const FString& Path);
	void OpenMesh(USkeletalMesh* Mesh);
	void ReleaseResources();
	void SetSelectedMesh(USkeletalMesh* Mesh);
	void FocusOnMesh();
	void ResetLighting();

	// 뷰어 패널이 마우스 조작 중이거나 호버 상태인지 확인 (메인 에디터 뷰포트 입력 간섭 차단용)
	bool IsHoveredOrInteracting() const { return bViewportHovered || bIsInteracting; }

	void RequestFocus() { bNeedFocus = true; }

	// 현재 프리뷰에서 수정한 본 Transform을 RefPose로 확정하고 .bin 파일에 저장
	bool SaveCurrentMesh();
	void ResetAllBonesToBindPose();

	// 변형된 .bin을 원본 FBX 초기 포즈로 즉시 복원
	bool RestoreToOriginalFBXPose();

	// 이름을 입력받아 새로운 .fbx.bin 에셋으로 별도 저장 및 에디터 등록
	bool SaveAsNewBin(const FString& NewAssetName);

private:
	void DrawSaveAsModal();
	void ResizeViewport(uint32 NewWidth, uint32 NewHeight);
	FBox CalculatePreviewBounds() const;
	FTransform GetBoneLocalTransform(int32 BoneIndex) const;
	UMaterial* GetPreviewMaterial(int32 SlotIndex) const;
	void SetPreviewSampler(int32 SlotIndex, ESamplerState Sampler);
	void AppendStaticPreviewPackets(FRenderQueue& Queue, const FMatrix& PreviewWorld) const;
	void UpdateCamera(float DeltaTime);
	void HandleInput(float DeltaTime);

	void DrawToolbarUI();
	void DrawAssetTabBar();

	void DrawViewportWindow(const struct ImGuiWindowClass& WindowClass);
	void DrawSkeletonTreeWindow(const struct ImGuiWindowClass& WindowClass);
	void DrawAssetDetailsWindow(const struct ImGuiWindowClass& WindowClass);
	void DrawPreviewSettingsWindow(const struct ImGuiWindowClass& WindowClass);
	void DrawMeshDetailsUI();
	void DrawBoneHierarchyUI();
	void DrawLightingSettingsUI();
	void DrawBoneNode(int32 BoneIndex, const TArray<struct FSkeletalBone>& Bones);

	// 1~3단계: 골격(Bone) 3D 원뿔/선 렌더링, 에디터 기즈모 연동, 실시간 스키닝 및 뷰포트 피킹
	void ComputeBoneWorldMatrices(TArray<FMatrix>& OutBoneWorldMatrices) const;
	void DrawSkeletonOverlay(const struct ImVec2& ViewportOrigin, const struct ImVec2& ViewportSize);
	bool IsBoneAncestorOf(int32 AncestorIndex, int32 TargetBoneIndex, const TArray<struct FSkeletalBone>& Bones) const;

private:
	FRenderer* Renderer = nullptr;
	FGridRenderer* GridRenderer = nullptr;
	FGizmoRenderer* GizmoRenderer = nullptr;

	// 임시 프리뷰 객체는 에디터가 종료되기 전에 해제한다.
	USkeletalMeshComponent* PreviewComponent = nullptr;
	USceneComponent* BoneGizmoTarget = nullptr;
	FGizmo BoneGizmo;
	USkeletalMesh* SelectedMesh = nullptr;
	TArray<TUniquePtr<UMaterial>> PreviewMaterials;
	TArray<UStaticMesh*> VisibleStaticMeshes;
	TUniquePtr<FConstantBuffer> PreviewLighting;
	TUniquePtr<FConstantBuffer> PreviewLightGrid;
	FBox PreviewBounds{};
	float MeshRadius = 1.0f;

	// 다중 애셋 탭 목록 및 전환 대기 메시
	TArray<USkeletalMesh*> OpenedMeshes;
	USkeletalMesh* MeshToSelect = nullptr;
	FString OpenAssetMessage;

	// 렌더 타깃 및 뷰포트
	TUniquePtr<FTexture2D> ColorTarget;
	TUniquePtr<FTexture2D> DepthTarget;
	FRenderingInfo RenderingInfo{};
	uint32 ViewportWidth = 0;
	uint32 ViewportHeight = 0;
	bool bViewportVisible = false;
	bool bFrameRequested = false;
	bool bViewportHovered = false;
	bool bIsInteracting = false;
	bool bNeedFocus = false;

	// 개별 도킹 서브 윈도우 표시 상태
	bool bViewportOpen = true;
	bool bSkeletonTreeOpen = true;
	bool bAssetDetailsOpen = true;
	bool bPreviewSettingsOpen = true;
	bool bResetLayout = false;

	// 레벨 에디터와 같은 공전 및 자유 이동 카메라 조작을 사용한다.
	FVector CameraLocation = FVector(-250.0f, -250.0f, 150.0f);
	FVector TargetPosition = FVector::ZeroVector;
	float CameraDistance = 250.0f;
	float CameraYaw = 45.0f;
	float CameraPitch = 20.0f;
	float CameraSpeed = 500.0f;
	float FovDegrees = 60.0f;
	FVector CachedCameraLocation = FVector::ZeroVector;
	FMatrix CachedViewMatrix = FMatrix::Identity;
	FMatrix CachedProjMatrix = FMatrix::Identity;

	// 미리보기의 방향광과 하늘광 설정이다.
	bool bEnableDirectionalLight = true;
	float LightYaw = 45.0f;
	float LightPitch = 45.0f;
	FVector LightColor = FVector(1.0f, 1.0f, 1.0f);
	float LightIntensity = 1.8f;

	bool bEnableSkyLight = true;
	FVector SkyColor = FVector(0.6f, 0.7f, 0.85f);
	FVector GroundColor = FVector(0.25f, 0.25f, 0.28f);
	float SkyLightIntensity = 1.0f;

	// 뷰 모드 및 렌더 옵션
	EViewMode ViewerMode = EViewMode::Lit_BlinnPhong;
	bool bShowGrid = true;
	bool bWireframe = false;
	FVector4 BackgroundClearColor = FVector4(0.12f, 0.12f, 0.12f, 1.0f); // 언리얼 기본 프리뷰 배경색

	// 골격(Bone) 표시 및 하이라이트 옵션
	bool bShowBones = true;
	bool bDrawBoneCones = true;      // true: 입체 원뿔(Cone/Pyramid) 렌더링, false: 선(Line) 렌더링
	bool bShowAllBoneNames = false;  // 모든 본 이름 표시 여부 (선택된 본은 항상 표시)
	float BoneConeWidthRatio = 0.10f;
	float BoneJointRadius = 4.0f;

	// 본 트리 UI 및 피킹 상태
	int32 SelectedBoneIndex = -1;
	int32 HoveredBoneIndex = -1;
	bool bScrollToSelectedBone = false;
	char BoneSearchFilter[64] = "";

	// 저장 상태 알림 메시지 및 타이머
	FString SaveStatusMessage;
	float SaveStatusTimer = 0.0f;

	// Save As New .bin 모달 팝업 상태
	bool bShowSaveAsModal = false;
	char SaveAsNameBuffer[128] = "";
};
