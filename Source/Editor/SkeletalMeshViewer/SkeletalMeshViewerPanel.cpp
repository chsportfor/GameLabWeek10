#include "EnginePCH.h"
#include "SkeletalMeshViewerPanel.h"
#include "Asset/AssetManager.h"
#include "Camera/MinimalViewInfo.h"
#include "Collision/Ray.h"
#include "Engine/SceneManagement.h"
#include "Editor/Rendering/GridRenderer.h"
#include "Input/InputSystem.h"
#include "Render/RenderCommand.h"
#include "Math/EngineMath.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>

FSkeletalMeshViewerPanel::FSkeletalMeshViewerPanel()
{
	ResetLighting();
}

FSkeletalMeshViewerPanel::~FSkeletalMeshViewerPanel()
{
	PreviewComponent = nullptr;
	BoneGizmoTarget = nullptr;
}

bool FSkeletalMeshViewerPanel::Init()
{
	PreviewComponent = NewObject<USkeletalMeshComponent>();
	BoneGizmoTarget = NewObject<USceneComponent>();
	BoneGizmo.SetMode(EGizmoMode::Rotation);
	BoneGizmo.SetSpace(EGizmoSpace::Local);

	// 기본 뷰포트 크기 초기화
	ResizeViewport(800, 600);

	// 기본 상태: 닫혀 있음 (Window 메뉴 또는 Details 패널의 Edit 버튼으로 오픈)
	SetOpen(false);

	return true;
}

void FSkeletalMeshViewerPanel::OpenMesh(USkeletalMesh* Mesh)
{
	if (!Mesh)
		return;

	SetOpen(true);
	RequestFocus();

	// 이미 열려 있는지 검사
	bool bFound = false;
	for (USkeletalMesh* Opened : OpenedMeshes)
	{
		if (Opened == Mesh)
		{
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		OpenedMeshes.Add(Mesh);
	}

	MeshToSelect = Mesh;
	SetSelectedMesh(Mesh);
}

void FSkeletalMeshViewerPanel::SetSelectedMesh(USkeletalMesh* Mesh)
{
	SelectedMesh = Mesh;

	if (PreviewComponent)
	{
		PreviewComponent->SetSkeletalMesh(Mesh);
		PreviewComponent->SetRelativeLocation(FVector::ZeroVector);
		PreviewComponent->SetRelativeRotation(FRotator(0.0f, 0.0f, 0.0f));
	}

	SelectedBoneIndex = -1;
	HoveredBoneIndex = -1;
	BoneGizmo.SetTarget(nullptr, false);
	FocusOnMesh();
}

void FSkeletalMeshViewerPanel::FocusOnMesh()
{
	if (!SelectedMesh)
	{
		TargetPosition = FVector::ZeroVector;
		CameraDistance = 250.0f;
		CameraSpeed = 500.0f;
		CameraYaw = 45.0f;
		CameraPitch = 20.0f;
		const FRotator Rot(CameraPitch, CameraYaw, 0.0f);
		CameraLocation = TargetPosition - Rot.Quaternion().GetForwardVector() * CameraDistance;
		return;
	}

	const FBox& Bounds = SelectedMesh->GetBounds();
	if (Bounds.Min.X <= Bounds.Max.X)
	{
		TargetPosition = (Bounds.Min + Bounds.Max) * 0.5f;
		const FVector Extent = (Bounds.Max - Bounds.Min) * 0.5f;
		const float Radius = Extent.Length();
		CameraDistance = std::max(Radius * 2.2f, 2.0f);
		CameraSpeed = std::clamp(Radius * 2.5f, 5.0f, 5000.0f);
	}
	else
	{
		TargetPosition = FVector::ZeroVector;
		CameraDistance = 250.0f;
		CameraSpeed = 500.0f;
	}

	const FRotator Rot(CameraPitch, CameraYaw, 0.0f);
	CameraLocation = TargetPosition - Rot.Quaternion().GetForwardVector() * CameraDistance;
}

void FSkeletalMeshViewerPanel::ResetLighting()
{
	bEnableDirectionalLight = true;
	LightYaw = 45.0f;
	LightPitch = 45.0f;
	LightColor = FVector(1.0f, 1.0f, 1.0f);
	LightIntensity = 1.8f;

	bEnableSkyLight = true;
	SkyColor = FVector(0.6f, 0.7f, 0.85f);
	GroundColor = FVector(0.25f, 0.25f, 0.28f);
	SkyLightIntensity = 1.0f;

	BackgroundClearColor = FVector4(0.12f, 0.12f, 0.12f, 1.0f);
	bShowGrid = true;
	bWireframe = false;
	ViewerMode = EViewMode::Lit_BlinnPhong;
}

void FSkeletalMeshViewerPanel::ResizeViewport(uint32 NewWidth, uint32 NewHeight)
{
	NewWidth = std::max(1u, NewWidth);
	NewHeight = std::max(1u, NewHeight);

	if (ColorTarget && DepthTarget &&
		std::abs(static_cast<int32>(ViewportWidth) - static_cast<int32>(NewWidth)) <= 2 &&
		std::abs(static_cast<int32>(ViewportHeight) - static_cast<int32>(NewHeight)) <= 2)
	{
		return;
	}

	ViewportWidth = NewWidth;
	ViewportHeight = NewHeight;

	D3D11_TEXTURE2D_DESC Desc{};
	Desc.Width = ViewportWidth;
	Desc.Height = ViewportHeight;
	Desc.MipLevels = 1;
	Desc.ArraySize = 1;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_DEFAULT;

	// Color Target
	Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	ColorTarget = RenderCommand::CreateTexture2D(Desc);

	// Depth Target
	Desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	DepthTarget = RenderCommand::CreateTexture2D(Desc);

	RenderingInfo.ColorRenderTargets.Reset();
	RenderingInfo.ViewportSetting.StartX = 0;
	RenderingInfo.ViewportSetting.StartY = 0;
	RenderingInfo.ViewportSetting.Width = ViewportWidth;
	RenderingInfo.ViewportSetting.Height = ViewportHeight;
	RenderingInfo.ViewportSetting.MinDepth = 0.0f;
	RenderingInfo.ViewportSetting.MaxDepth = 1.0f;

	FRenderingDesc ColorDesc{};
	ColorDesc.Texture = ColorTarget.get();
	ColorDesc.LoadOp = ERenderTargetLoadOp::Clear;
	ColorDesc.StoreOp = ERenderTargetStoreOp::Store;
	ColorDesc.ClearValue.ColorClearValue = BackgroundClearColor;
	RenderingInfo.ColorRenderTargets.Add(ColorDesc);

	RenderingInfo.DepthStencil.Texture = DepthTarget.get();
	RenderingInfo.DepthStencil.LoadOp = ERenderTargetLoadOp::Clear;
	RenderingInfo.DepthStencil.StoreOp = ERenderTargetStoreOp::Store;
	RenderingInfo.DepthStencil.ClearValue.DepthClearValue = 1.0f;
	RenderingInfo.DepthStencil.ClearValue.StencilClearValue = 0;
}

void FSkeletalMeshViewerPanel::Tick(float DeltaTime)
{
	if (!IsOpen())
		return;

	if (PreviewComponent)
	{
		PreviewComponent->TickComponent(DeltaTime);
	}

	HandleInput(DeltaTime);
	UpdateCamera(DeltaTime);
}

void FSkeletalMeshViewerPanel::HandleInput(float DeltaTime)
{
	ImGuiIO& IO = ImGui::GetIO();

	const bool bRightMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
	const bool bMiddleMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
	const bool bLeftMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);

	if (bViewportHovered && (bRightMouseDown || bMiddleMouseDown || bLeftMouseDown))
	{
		bIsInteracting = true;
	}
	else if (!bRightMouseDown && !bMiddleMouseDown && !bLeftMouseDown)
	{
		bIsInteracting = false;
	}

	if (!bViewportHovered && !bIsInteracting)
		return;

	// 1. 단축키 'F': 포커스 맞추기, W/E/R/Space: 기즈모 모드 전환 (우클릭 카메라 조작 중이 아닐 때)
	if (!IO.WantTextInput && !bRightMouseDown && !IO.KeyAlt)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_F))
		{
			FocusOnMesh();
		}
		if (ImGui::IsKeyPressed(ImGuiKey_W))
		{
			BoneGizmo.SetMode(EGizmoMode::Location);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_E))
		{
			BoneGizmo.SetMode(EGizmoMode::Rotation);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_R))
		{
			BoneGizmo.SetMode(EGizmoMode::Scale);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Space))
		{
			const EGizmoMode Cur = BoneGizmo.GetMode();
			if (Cur == EGizmoMode::Location) BoneGizmo.SetMode(EGizmoMode::Rotation);
			else if (Cur == EGizmoMode::Rotation) BoneGizmo.SetMode(EGizmoMode::Scale);
			else BoneGizmo.SetMode(EGizmoMode::Location);
		}
	}

	// 2. 단축키 'L' + 좌클릭 드래그: 실시간 라이트 회전
	const bool bHoldingL = ImGui::IsKeyDown(ImGuiKey_L);
	if (bHoldingL && bLeftMouseDown)
	{
		LightYaw += IO.MouseDelta.x * 0.5f;
		LightPitch = std::clamp(LightPitch + IO.MouseDelta.y * 0.5f, -89.0f, 89.0f);
		return;
	}

	// 3. 우클릭 조작 (First Person Free-Flight & Look Around)
	if (bRightMouseDown && !IO.KeyAlt)
	{
		CameraYaw += IO.MouseDelta.x * 0.35f;
		CameraPitch = std::clamp(CameraPitch + IO.MouseDelta.y * 0.35f, -89.0f, 89.0f);

		const FRotator CurrentRot(CameraPitch, CameraYaw, 0.0f);
		const FQuat CurrentQuat(CurrentRot);
		const FVector CurrentForward = CurrentQuat.GetForwardVector();
		const FVector CurrentRight = CurrentQuat.GetRightVector();
		const FVector CurrentUp = FVector(0.0f, 0.0f, 1.0f);

		FVector MoveDir = FVector::ZeroVector;
		if (ImGui::IsKeyDown(ImGuiKey_W)) MoveDir += CurrentForward;
		if (ImGui::IsKeyDown(ImGuiKey_S)) MoveDir -= CurrentForward;
		if (ImGui::IsKeyDown(ImGuiKey_D)) MoveDir += CurrentRight;
		if (ImGui::IsKeyDown(ImGuiKey_A)) MoveDir -= CurrentRight;
		if (ImGui::IsKeyDown(ImGuiKey_E)) MoveDir += CurrentUp;
		if (ImGui::IsKeyDown(ImGuiKey_Q)) MoveDir -= CurrentUp;

		if (MoveDir.SizeSquared() > 0.0001f)
		{
			const float SpeedMultiplier = IO.KeyShift ? 2.5f : 1.0f;
			const FVector DeltaPos = MoveDir.GetSafeNormal() * (CameraSpeed * SpeedMultiplier * DeltaTime);
			CameraLocation += DeltaPos;
		}

		TargetPosition = CameraLocation + CurrentForward * CameraDistance;
	}
	// 4. Alt + 마우스 조작
	else if (IO.KeyAlt)
	{
		if (bLeftMouseDown)
		{
			CameraYaw += IO.MouseDelta.x * 0.35f;
			CameraPitch = std::clamp(CameraPitch + IO.MouseDelta.y * 0.35f, -89.0f, 89.0f);

			const FRotator CurrentRot(CameraPitch, CameraYaw, 0.0f);
			const FQuat CurrentQuat(CurrentRot);
			CameraLocation = TargetPosition - CurrentQuat.GetForwardVector() * CameraDistance;
		}
		else if (bRightMouseDown)
		{
			const float ZoomDelta = (IO.MouseDelta.x - IO.MouseDelta.y) * std::max(CameraDistance * 0.005f, 0.05f);
			CameraDistance = std::clamp(CameraDistance - ZoomDelta, 0.5f, 50000.0f);

			const FRotator CurrentRot(CameraPitch, CameraYaw, 0.0f);
			const FQuat CurrentQuat(CurrentRot);
			CameraLocation = TargetPosition - CurrentQuat.GetForwardVector() * CameraDistance;
		}
	}

	// 5. 휠 클릭 드래그(미들 버튼): 카메라 팬
	if (bMiddleMouseDown)
	{
		const FRotator CurrentRot(CameraPitch, CameraYaw, 0.0f);
		const FQuat CurrentQuat(CurrentRot);
		const FVector CurrentRight = CurrentQuat.GetRightVector();
		const FVector CurrentUp = FVector(0.0f, 0.0f, 1.0f);

		const float PanSpeed = std::max(CameraDistance * 0.0015f, 0.005f);
		const FVector PanDelta = (-CurrentRight * (IO.MouseDelta.x * PanSpeed)) + (-CurrentUp * (IO.MouseDelta.y * PanSpeed));
		CameraLocation += PanDelta;
		TargetPosition += PanDelta;
	}

	// 6. 마우스 휠 스크롤: 줌 (Dolly Zoom)
	if (std::abs(IO.MouseWheel) > 0.01f)
	{
		const float Step = std::max(CameraDistance * 0.15f, 0.2f);
		CameraDistance -= IO.MouseWheel * Step;

		const FRotator CurrentRot(CameraPitch, CameraYaw, 0.0f);
		const FQuat CurrentQuat(CurrentRot);
		const FVector CurrentForward = CurrentQuat.GetForwardVector();

		if (CameraDistance < 0.5f)
		{
			TargetPosition += CurrentForward * (0.5f - CameraDistance);
			CameraDistance = 0.5f;
		}
		CameraDistance = std::clamp(CameraDistance, 0.5f, 50000.0f);
		CameraLocation = TargetPosition - CurrentForward * CameraDistance;
	}
}

void FSkeletalMeshViewerPanel::UpdateCamera(float DeltaTime)
{
	CachedCameraLocation = CameraLocation;

	FMinimalViewInfo ViewInfo;
	ViewInfo.SetLocation(CachedCameraLocation);
	ViewInfo.SetRotation(FRotator(CameraPitch, CameraYaw, 0.0f));
	ViewInfo.SetFieldOfView(FovDegrees);
	ViewInfo.SetAspectRatio((ViewportHeight > 0) ? (static_cast<float>(ViewportWidth) / static_cast<float>(ViewportHeight)) : (16.0f / 9.0f));
	ViewInfo.SetNearZ(0.05f);
	ViewInfo.SetFarZ(100000.0f);

	CachedViewMatrix = ViewInfo.GetViewMatrix();
	CachedProjMatrix = ViewInfo.GetPerspectiveMatrix();
}

void FSkeletalMeshViewerPanel::RenderPreview()
{
	if (!IsOpen())
		return;

	if (!ColorTarget || !DepthTarget || ViewportWidth == 0 || ViewportHeight == 0)
		return;

	UpdateCamera(0.0f);

	const FMatrix ViewProj = CachedViewMatrix * CachedProjMatrix;

	RenderingInfo.ColorRenderTargets[0].ClearValue.ColorClearValue = BackgroundClearColor;
	RenderCommand::BeginRenderPass(RenderingInfo);

	FLightingConstants LightingConstants{};
	if (bEnableSkyLight)
	{
		LightingConstants.Ambient.Color = SkyColor * SkyLightIntensity;
		LightingConstants.Ambient.Intensity = SkyLightIntensity;
	}
	else
	{
		LightingConstants.Ambient.Color = FVector::ZeroVector;
		LightingConstants.Ambient.Intensity = 0.0f;
	}

	if (bEnableDirectionalLight)
	{
		const FRotator LightRot(LightPitch, LightYaw, 0.0f);
		const FQuat LightQuat(LightRot);
		LightingConstants.Directional.Direction = LightQuat.GetForwardVector().GetSafeNormal();
		LightingConstants.Directional.Color = LightColor;
		LightingConstants.Directional.Intensity = LightIntensity;
		LightingConstants.Directional.SourceAngle = 0.0f;
	}
	else
	{
		LightingConstants.Directional.Direction = FVector(0, 0, -1);
		LightingConstants.Directional.Color = FVector::ZeroVector;
		LightingConstants.Directional.Intensity = 0.0f;
	}
	LightingConstants.NumLocalLights = 0;

	TArray<FLightRenderParameters> EmptyLocalLights;
	Renderer->SetLighting(LightingConstants, EmptyLocalLights);
	Renderer->SetViewMode(ViewerMode);
	RenderCommand::SetRasterizerState(bWireframe ? ERasterizerState::Wireframe : ERasterizerState::SolidBack);

	if (bShowGrid && GridRenderer)
	{
		const FRotator Rot(CameraPitch, CameraYaw, 0.0f);
		const FQuat Quat(Rot);
		const FVector CameraForward = Quat.GetForwardVector();

		GridRenderer->OnRenderBatchGrid(
			ViewProj,
			CachedCameraLocation,
			CameraForward,
			EGridPlane::XY,
			50.0f,
			true,
			RenderingInfo.ViewportSetting);
	}

	if (PreviewComponent && SelectedMesh)
	{
		FRenderQueue RenderQueue;
		PreviewComponent->SubmitToRenderQueue(RenderQueue);
		Renderer->RenderAll(RenderQueue, ViewProj, CachedCameraLocation);

		// 3단계: 에디터와 100% 동일한 3D 기즈모(FGizmoRenderer)를 선택된 본 위치에 렌더링
		if (GizmoRenderer && BoneGizmoTarget && SelectedBoneIndex >= 0 && SelectedBoneIndex < static_cast<int32>(SelectedMesh->GetNumBones()))
		{
			if (!BoneGizmo.IsUsing())
			{
				TArray<FMatrix> BoneMatrices;
				ComputeBoneWorldMatrices(BoneMatrices);
				if (SelectedBoneIndex < BoneMatrices.Num())
				{
					const FMatrix& BoneMat = BoneMatrices[SelectedBoneIndex];
					const FVector BoneWorldPos(BoneMat.M[3][0], BoneMat.M[3][1], BoneMat.M[3][2]);
					FMatrix BoneRotMat = FMatrix::Identity;
					BoneRotMat.SetAxis(0, FVector(BoneMat.M[0][0], BoneMat.M[0][1], BoneMat.M[0][2]).GetSafeNormal());
					BoneRotMat.SetAxis(1, FVector(BoneMat.M[1][0], BoneMat.M[1][1], BoneMat.M[1][2]).GetSafeNormal());
					BoneRotMat.SetAxis(2, FVector(BoneMat.M[2][0], BoneMat.M[2][1], BoneMat.M[2][2]).GetSafeNormal());

					BoneGizmoTarget->SetRelativeLocation(BoneWorldPos);
					BoneGizmoTarget->SetRelativeRotation(MatrixToRotator(BoneRotMat));
					BoneGizmoTarget->SetRelativeScale3D(PreviewComponent->GetBoneLocalTransform(SelectedBoneIndex).Scale);
					BoneGizmo.SetTarget(BoneGizmoTarget, false);
				}
			}

			RenderCommand::ClearDepthStencil(RenderingInfo.DepthStencil.Texture);
			RenderCommand::SetRasterizerState(ERasterizerState::SolidBack);
			GizmoRenderer->OnRender(BoneGizmo, ViewProj, CachedCameraLocation, false);
		}
		else
		{
			BoneGizmo.SetTarget(nullptr, false);
		}
	}

	RenderCommand::EndRenderPass(RenderingInfo);
}

void FSkeletalMeshViewerPanel::OnRender()
{
	bool bOpen = IsOpen();
	if (!bOpen)
		return;

	ImGui::SetNextWindowSize(ImVec2(1280, 800), ImGuiCond_FirstUseEver);

	if (bNeedFocus)
	{
		ImGui::SetNextWindowFocus();
		bNeedFocus = false;
	}

	// 독립 윈도우 시작
	const bool bVisible = ImGui::Begin(GetPanelName(), &bOpen, ImGuiWindowFlags_MenuBar);

	if (bVisible)
	{
		DrawToolbarUI();
		DrawAssetTabBar();

		// 열려 있는 메시가 없을 때의 안내 화면
		if (OpenedMeshes.Num() == 0 || !SelectedMesh)
		{
			ImGui::Spacing();
			ImGui::TextDisabled("No Skeletal Mesh currently open.");
			ImGui::Spacing();
			ImGui::Text("You can open a mesh from the 'Mesh' menu above, or click [Edit] next to the SkeletalMesh property in the Details Panel.");
		}
	}

	// 뷰어 내부 전용 DockSpace 생성 (레벨 에디터 패널과 도킹 격리)
	ImGuiWindowClass SkelViewerClass;
	SkelViewerClass.ClassId = ImHashStr("SkeletalMeshViewerClass");
	SkelViewerClass.DockingAllowUnclassed = false;

	ImGuiID DockSpaceID = ImGui::GetID("SkeletalMeshViewerInnerDockSpace");

	ImGuiDockNode* Node = ImGui::DockBuilderGetNode(DockSpaceID);
	ImGuiWindow* SkelVpWin = ImGui::FindWindowByName("Skeletal Mesh Viewport");
	const bool bSkelVpOrphaned = (SkelVpWin != nullptr && SkelVpWin->DockNode == nullptr && SkelVpWin->WasActive);

	if (Node == nullptr || bResetLayout || (Node->IsLeafNode() && Node->Windows.Size == 0) || bSkelVpOrphaned)
	{
		bResetLayout = false;
		ImVec2 Avail = ImGui::GetContentRegionAvail();
		ImVec2 DockSize(std::max(Avail.x, 800.0f), std::max(Avail.y, 600.0f));

		ImGui::DockBuilderRemoveNode(DockSpaceID);
		ImGui::DockBuilderAddNode(DockSpaceID, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(DockSpaceID, DockSize);

		ImGuiID CenterNode = DockSpaceID;
		ImGuiID LeftNode = ImGui::DockBuilderSplitNode(CenterNode, ImGuiDir_Left, 0.22f, nullptr, &CenterNode);
		ImGuiID RightNode = ImGui::DockBuilderSplitNode(CenterNode, ImGuiDir_Right, 0.28f, nullptr, &CenterNode);
		ImGuiID RightBottomNode = ImGui::DockBuilderSplitNode(RightNode, ImGuiDir_Down, 0.50f, nullptr, &RightNode);

		ImGui::DockBuilderDockWindow("Skeleton Tree", LeftNode);
		ImGui::DockBuilderDockWindow("Asset Details", RightNode);
		ImGui::DockBuilderDockWindow("Preview Scene Settings", RightBottomNode);
		ImGui::DockBuilderDockWindow("Skeletal Mesh Viewport", CenterNode);
		ImGui::DockBuilderFinish(DockSpaceID);
	}

	const bool bHasActiveMesh = (OpenedMeshes.Num() > 0 && SelectedMesh != nullptr);
	ImGuiDockNodeFlags InnerDockFlags = bHasActiveMesh ? ImGuiDockNodeFlags_None : ImGuiDockNodeFlags_KeepAliveOnly;
	ImGui::DockSpace(DockSpaceID, ImVec2(0.0f, 0.0f), InnerDockFlags, &SkelViewerClass);
	ImGui::End(); // Skeletal Mesh Viewer 메인 창 끝

	if (bHasActiveMesh)
	{
		// 4개의 내부 도킹 서브 패널 렌더링
		if (bViewportOpen)
		{
			DrawViewportWindow(SkelViewerClass);
		}
		if (bSkeletonTreeOpen)
		{
			DrawSkeletonTreeWindow(SkelViewerClass);
		}
		if (bAssetDetailsOpen)
		{
			DrawAssetDetailsWindow(SkelViewerClass);
		}
		if (bPreviewSettingsOpen)
		{
			DrawPreviewSettingsWindow(SkelViewerClass);
		}
	}
	else
	{
		bViewportHovered = false;
	}

	SetOpen(bOpen);
}

void FSkeletalMeshViewerPanel::DrawAssetTabBar()
{
	if (OpenedMeshes.Num() == 0)
		return;

	if (ImGui::BeginTabBar("##SkelMeshAssetTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs))
	{
		for (int32 i = 0; i < OpenedMeshes.Num(); ++i)
		{
			USkeletalMesh* Mesh = OpenedMeshes[i];
			if (!Mesh) continue;

			bool bTabOpen = true;
			ImGuiTabItemFlags TabFlags = ImGuiTabItemFlags_None;
			if (MeshToSelect == Mesh)
			{
				TabFlags |= ImGuiTabItemFlags_SetSelected;
			}

			FString Path = Mesh->GetPath();
			FString DisplayName = Path.empty() ? "Skeletal Mesh" : std::filesystem::path(Path).filename().string();
			if (DisplayName.empty()) DisplayName = "Mesh";

			ImGui::PushID(Mesh);
			if (ImGui::BeginTabItem(DisplayName.c_str(), &bTabOpen, TabFlags))
			{
				if (SelectedMesh != Mesh)
				{
					SetSelectedMesh(Mesh);
				}
				ImGui::EndTabItem();
			}
			ImGui::PopID();

			if (!bTabOpen)
			{
				// 탭 닫기
				OpenedMeshes.RemoveAt(i);
				--i;

				if (SelectedMesh == Mesh)
				{
					SelectedMesh = (OpenedMeshes.Num() > 0) ? OpenedMeshes[0] : nullptr;
					if (SelectedMesh)
					{
						SetSelectedMesh(SelectedMesh);
					}
					else if (PreviewComponent)
					{
						PreviewComponent->SetSkeletalMesh(nullptr);
					}
				}
			}
		}

		MeshToSelect = nullptr;
		ImGui::EndTabBar();
	}
}

void FSkeletalMeshViewerPanel::DrawViewportWindow(const ImGuiWindowClass& WindowClass)
{
	ImGuiWindowClass ViewportClass = WindowClass;
	ViewportClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoUndocking;
	ImGui::SetNextWindowClass(&ViewportClass);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	const bool bVisible = ImGui::Begin("Skeletal Mesh Viewport", &bViewportOpen,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::PopStyleVar();

	if (bVisible)
	{
		const ImVec2 AvailSize = ImGui::GetContentRegionAvail();
		const uint32 TargetWidth = static_cast<uint32>(std::max(1.0f, AvailSize.x));
		const uint32 TargetHeight = static_cast<uint32>(std::max(1.0f, AvailSize.y));

		ResizeViewport(TargetWidth, TargetHeight);

		if (ColorTarget && ColorTarget->GetSRV())
		{
			const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
			const ImVec2 ImageSize(static_cast<float>(ViewportWidth), static_cast<float>(ViewportHeight));
			ImGui::Image(ColorTarget->GetSRV(), ImageSize);
			bViewportHovered = ImGui::IsItemHovered();

			// 1단계 & 2단계: 바인드 포즈(현재 포즈) 부모-자식 본(Cone/Line) 렌더링, 본 선택 하이라이트 및 뷰포트 피킹
			DrawSkeletonOverlay(CursorPos, ImageSize);

			ImDrawList* DrawList = ImGui::GetWindowDrawList();
			const char* HintText = "LMB: Pick Bone | RMB+WASD: Fly | Alt+LMB: Orbit | Wheel: Zoom | MMB: Pan | F: Focus | L+LMB: Light";
			const ImVec2 TextSize = ImGui::CalcTextSize(HintText);
			DrawList->AddRectFilled(
				ImVec2(CursorPos.x + 8, CursorPos.y + 8),
				ImVec2(CursorPos.x + TextSize.x + 16, CursorPos.y + TextSize.y + 14),
				IM_COL32(0, 0, 0, 160), 4.0f);
			DrawList->AddText(ImVec2(CursorPos.x + 12, CursorPos.y + 10), IM_COL32(230, 230, 230, 240), HintText);
		}
	}
	else
	{
		bViewportHovered = false;
		HoveredBoneIndex = -1;
	}
	ImGui::End();
}

void FSkeletalMeshViewerPanel::ComputeBoneWorldMatrices(TArray<FMatrix>& OutBoneWorldMatrices) const
{
	OutBoneWorldMatrices.Empty();
	if (!SelectedMesh)
		return;

	const TArray<FBoneInfo>& Bones = SelectedMesh->GetBones();
	const int32 NumBones = Bones.Num();
	if (NumBones == 0)
		return;

	OutBoneWorldMatrices.SetNum(NumBones);
	TArray<uint8> State;
	State.SetNum(NumBones);
	for (int32 i = 0; i < NumBones; ++i)
	{
		State[i] = 0;
	}

	const TArray<FTransform>* CompTransforms = PreviewComponent ? &PreviewComponent->GetBoneTransforms() : nullptr;
	const FMatrix CompWorldMatrix = PreviewComponent ? PreviewComponent->GetWorldMatrix() : FMatrix::Identity;

	std::function<void(int32)> EvaluateBone = [&](int32 Idx)
	{
		if (Idx < 0 || Idx >= NumBones)
			return;
		if (State[Idx] == 2)
			return;
		if (State[Idx] == 1) // 순환 참조 방지
		{
			OutBoneWorldMatrices[Idx] = FMatrix::Identity;
			State[Idx] = 2;
			return;
		}

		State[Idx] = 1;

		const FTransform& LocalTf = (CompTransforms && CompTransforms->Num() == NumBones)
			? (*CompTransforms)[Idx]
			: Bones[Idx].LocalTransform;
		const FMatrix LocalMat = LocalTf.GetLocalMatrix();

		const int32 ParentIdx = Bones[Idx].ParentIndex;
		if (ParentIdx >= 0 && ParentIdx < NumBones && ParentIdx != Idx)
		{
			EvaluateBone(ParentIdx);
			OutBoneWorldMatrices[Idx] = LocalMat * OutBoneWorldMatrices[ParentIdx];
		}
		else
		{
			OutBoneWorldMatrices[Idx] = LocalMat * CompWorldMatrix;
		}

		State[Idx] = 2;
	};

	for (int32 i = 0; i < NumBones; ++i)
	{
		EvaluateBone(i);
	}
}

bool FSkeletalMeshViewerPanel::IsBoneAncestorOf(int32 AncestorIndex, int32 TargetBoneIndex, const TArray<FBoneInfo>& Bones) const
{
	if (AncestorIndex < 0 || TargetBoneIndex < 0 || AncestorIndex >= Bones.Num() || TargetBoneIndex >= Bones.Num())
		return false;

	int32 Cur = Bones[TargetBoneIndex].ParentIndex;
	int32 Safety = 0;
	while (Cur >= 0 && Cur < Bones.Num() && Safety < Bones.Num())
	{
		if (Cur == AncestorIndex)
			return true;
		Cur = Bones[Cur].ParentIndex;
		++Safety;
	}
	return false;
}

void FSkeletalMeshViewerPanel::DrawSkeletonOverlay(const ImVec2& ViewportOrigin, const ImVec2& ViewportSize)
{
	if (!SelectedMesh || ViewportSize.x <= 1.0f || ViewportSize.y <= 1.0f)
		return;

	const TArray<FBoneInfo>& Bones = SelectedMesh->GetBones();
	const int32 NumBones = Bones.Num();
	if (NumBones == 0)
		return;

	TArray<FMatrix> BoneMatrices;
	ComputeBoneWorldMatrices(BoneMatrices);
	if (BoneMatrices.Num() != NumBones)
		return;

	const FMatrix ViewProj = CachedViewMatrix * CachedProjMatrix;
	ImGuiIO& IO = ImGui::GetIO();
	const bool bCameraManipulating = ImGui::IsMouseDown(ImGuiMouseButton_Right)
		|| ImGui::IsMouseDown(ImGuiMouseButton_Middle)
		|| IO.KeyAlt
		|| ImGui::IsKeyDown(ImGuiKey_L);

	// 3단계: 3D 기즈모(FGizmo) 입력 업데이트 및 본 행렬/스키닝 실시간 변형 반영
	bool bGizmoBlocksPick = false;
	if (PreviewComponent && BoneGizmoTarget && SelectedBoneIndex >= 0 && SelectedBoneIndex < NumBones)
	{
		if (!BoneGizmo.IsUsing())
		{
			const FMatrix& BoneMat = BoneMatrices[SelectedBoneIndex];
			const FVector BoneWorldPos(BoneMat.M[3][0], BoneMat.M[3][1], BoneMat.M[3][2]);
			FMatrix BoneRotMat = FMatrix::Identity;
			BoneRotMat.SetAxis(0, FVector(BoneMat.M[0][0], BoneMat.M[0][1], BoneMat.M[0][2]).GetSafeNormal());
			BoneRotMat.SetAxis(1, FVector(BoneMat.M[1][0], BoneMat.M[1][1], BoneMat.M[1][2]).GetSafeNormal());
			BoneRotMat.SetAxis(2, FVector(BoneMat.M[2][0], BoneMat.M[2][1], BoneMat.M[2][2]).GetSafeNormal());

			BoneGizmoTarget->SetRelativeLocation(BoneWorldPos);
			BoneGizmoTarget->SetRelativeRotation(MatrixToRotator(BoneRotMat));
			BoneGizmoTarget->SetRelativeScale3D(PreviewComponent->GetBoneLocalTransform(SelectedBoneIndex).Scale);
			BoneGizmo.SetTarget(BoneGizmoTarget, false);
		}

		if ((bViewportHovered && !bCameraManipulating) || BoneGizmo.IsUsing())
		{
			const FVector2 LocalMousePos(IO.MousePos.x - ViewportOrigin.x, IO.MousePos.y - ViewportOrigin.y);
			const float NdcX = (LocalMousePos.X / ViewportSize.x) * 2.0f - 1.0f;
			const float NdcY = 1.0f - (LocalMousePos.Y / ViewportSize.y) * 2.0f;

			const FMatrix InvViewProj = ViewProj.Inverse();
			FVector4 NearClip = InvViewProj.TransformFVector4(FVector4(NdcX, NdcY, 0.0f, 1.0f));
			FVector4 FarClip = InvViewProj.TransformFVector4(FVector4(NdcX, NdcY, 1.0f, 1.0f));
			if (std::abs(NearClip.W) > 1e-6f) NearClip /= NearClip.W;
			if (std::abs(FarClip.W) > 1e-6f) FarClip /= FarClip.W;

			FRay MouseRay{};
			MouseRay.Origin = CachedCameraLocation;
			MouseRay.Direction = (FVector(FarClip.X, FarClip.Y, FarClip.Z) - FVector(NearClip.X, NearClip.Y, NearClip.Z)).GetSafeNormal();

			const bool bLeftMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left) && !bCameraManipulating;
			BoneGizmo.Update(
				MouseRay,
				LocalMousePos,
				ViewProj,
				static_cast<int>(ViewportSize.x),
				static_cast<int>(ViewportSize.y),
				bLeftMouseDown,
				CachedCameraLocation,
				false);

			if (BoneGizmo.IsUsing())
			{
				FTransform NewLocalTf = PreviewComponent->GetBoneLocalTransform(SelectedBoneIndex);
				const int32 ParentIdx = Bones[SelectedBoneIndex].ParentIndex;
				const FMatrix ParentWorldMat = (ParentIdx >= 0 && ParentIdx < NumBones)
					? BoneMatrices[ParentIdx]
					: PreviewComponent->GetWorldMatrix();

				if (BoneGizmo.GetMode() == EGizmoMode::Rotation)
				{
					const FMatrix NewWorldRotMat = BoneGizmoTarget->GetRelativeRotation().Quaternion().ToFMatrix();
					FMatrix ParentRotMat = FMatrix::Identity;
					ParentRotMat.SetAxis(0, FVector(ParentWorldMat.M[0][0], ParentWorldMat.M[0][1], ParentWorldMat.M[0][2]).GetSafeNormal());
					ParentRotMat.SetAxis(1, FVector(ParentWorldMat.M[1][0], ParentWorldMat.M[1][1], ParentWorldMat.M[1][2]).GetSafeNormal());
					ParentRotMat.SetAxis(2, FVector(ParentWorldMat.M[2][0], ParentWorldMat.M[2][1], ParentWorldMat.M[2][2]).GetSafeNormal());

					// Row-vector 규약: WorldRot = LocalRot * ParentRot => LocalRot = WorldRot * ParentRot^T
					const FMatrix NewLocalRotMat = NewWorldRotMat * ParentRotMat.GetTransposed();
					NewLocalTf.Rotation = MatrixToRotator(NewLocalRotMat);
				}
				else if (BoneGizmo.GetMode() == EGizmoMode::Location)
				{
					const FVector NewWorldPos = BoneGizmoTarget->GetWorldLocation();
					NewLocalTf.Location = ParentWorldMat.Inverse().TransformPosition(NewWorldPos);
				}
				else if (BoneGizmo.GetMode() == EGizmoMode::Scale)
				{
					NewLocalTf.Scale = BoneGizmoTarget->GetRelativeScale3D();
				}

				PreviewComponent->SetBoneLocalTransform(SelectedBoneIndex, NewLocalTf);
				ComputeBoneWorldMatrices(BoneMatrices);
			}
		}

		bGizmoBlocksPick = BoneGizmo.IsUsing() || (BoneGizmo.GetHoveredAxis() >= 0);
	}
	else
	{
		BoneGizmo.SetTarget(nullptr, false);
	}

	auto ProjectPoint = [&](const FVector& WorldPos, ImVec2& OutScreen, float& OutDepth) -> bool
	{
		const FVector4 Clip = ViewProj.TransformFVector4(FVector4(WorldPos.X, WorldPos.Y, WorldPos.Z, 1.0f));
		if (Clip.W <= 0.1f)
			return false;

		const float InvW = 1.0f / Clip.W;
		const float NdcX = Clip.X * InvW;
		const float NdcY = Clip.Y * InvW;

		OutScreen.x = ViewportOrigin.x + (NdcX * 0.5f + 0.5f) * ViewportSize.x;
		OutScreen.y = ViewportOrigin.y + (1.0f - (NdcY * 0.5f + 0.5f)) * ViewportSize.y;
		OutDepth = Clip.W;
		return true;
	};

	TArray<FVector> BonePositions;
	TArray<ImVec2> BoneScreenPts;
	TArray<float> BoneDepths;
	TArray<uint8> BoneInFront;

	BonePositions.SetNum(NumBones);
	BoneScreenPts.SetNum(NumBones);
	BoneDepths.SetNum(NumBones);
	BoneInFront.SetNum(NumBones);

	for (int32 i = 0; i < NumBones; ++i)
	{
		const FMatrix& Mat = BoneMatrices[i];
		BonePositions[i] = FVector(Mat.M[3][0], Mat.M[3][1], Mat.M[3][2]);
		BoneInFront[i] = ProjectPoint(BonePositions[i], BoneScreenPts[i], BoneDepths[i]) ? 1 : 0;
	}

	// 뷰포트 마우스 호버 및 좌클릭 본 피킹 (기즈모 조작 중이 아닐 때만 수행)
	HoveredBoneIndex = -1;
	if (bViewportHovered && !bCameraManipulating && !bGizmoBlocksPick && bShowBones)
	{
		const FVector2 MousePt(IO.MousePos.x, IO.MousePos.y);
		float BestJointScore = 14.0f; // 관절 포인트 피킹 반경(px)
		int32 BestJointBone = -1;

		for (int32 i = 0; i < NumBones; ++i)
		{
			if (!BoneInFront[i])
				continue;

			const float dx = MousePt.X - BoneScreenPts[i].x;
			const float dy = MousePt.Y - BoneScreenPts[i].y;
			const float Dist = std::sqrt(dx * dx + dy * dy);
			if (Dist < BestJointScore)
			{
				BestJointScore = Dist;
				BestJointBone = i;
			}
		}

		if (BestJointBone >= 0)
		{
			HoveredBoneIndex = BestJointBone;
		}
		else
		{
			// 관절 점에 직접 안 닿았으면 부모-자식 본 선분(원뿔 몸통) 피킹 검사
			float BestSegDist = 9.0f;
			int32 BestSegBone = -1;
			for (int32 i = 0; i < NumBones; ++i)
			{
				const int32 ParentIdx = Bones[i].ParentIndex;
				if (ParentIdx < 0 || ParentIdx >= NumBones)
					continue;
				if (!BoneInFront[ParentIdx] || !BoneInFront[i])
					continue;

				const FVector2 SegA(BoneScreenPts[ParentIdx].x, BoneScreenPts[ParentIdx].y);
				const FVector2 SegB(BoneScreenPts[i].x, BoneScreenPts[i].y);
				const float SegDist = DistanceToSegment(MousePt, SegA, SegB);
				if (SegDist < BestSegDist)
				{
					BestSegDist = SegDist;
					BestSegBone = ParentIdx; // 부모->자식 원뿔은 부모 본에서 뻗어나오므로 부모 본 선택
				}
			}
			HoveredBoneIndex = BestSegBone;
		}

		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			SelectedBoneIndex = HoveredBoneIndex;
			if (SelectedBoneIndex >= 0)
			{
				bScrollToSelectedBone = true;
			}
		}
	}

	if (!bShowBones && SelectedBoneIndex < 0)
		return;

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->PushClipRect(
		ViewportOrigin,
		ImVec2(ViewportOrigin.x + ViewportSize.x, ViewportOrigin.y + ViewportSize.y),
		true);

	// 1. 부모-자식 본 간의 3D 원뿔(Cone/Octahedron) 또는 선(Line) 렌더링
	for (int32 i = 0; i < NumBones; ++i)
	{
		const int32 ParentIdx = Bones[i].ParentIndex;
		if (ParentIdx < 0 || ParentIdx >= NumBones)
			continue;

		const bool bIsIncomingToSelected = (i == SelectedBoneIndex);
		const bool bIsOutgoingFromSelected = (ParentIdx == SelectedBoneIndex);
		const bool bIsSelectedBoneLink = bIsIncomingToSelected || bIsOutgoingFromSelected;
		const bool bIsHoveredBoneLink = (i == HoveredBoneIndex || ParentIdx == HoveredBoneIndex);

		if (!bShowBones && !bIsSelectedBoneLink)
			continue;

		const FVector& P0 = BonePositions[ParentIdx];
		const FVector& P1 = BonePositions[i];
		const FVector Diff = P1 - P0;
		const float BoneLength = Diff.Length();

		ImU32 ConeFillColor = IM_COL32(65, 190, 250, 50);
		ImU32 ConeWireColor = IM_COL32(95, 215, 255, 195);
		float WireThickness = 1.25f;

		if (bIsIncomingToSelected)
		{
			// 부모 -> 선택된 본 구간: 밝은 황금빛 주황색 하이라이트
			ConeFillColor = IM_COL32(255, 145, 0, 125);
			ConeWireColor = IM_COL32(255, 225, 40, 255);
			WireThickness = 2.4f;
		}
		else if (bIsOutgoingFromSelected)
		{
			// 선택된 본 -> 자식 본 구간: 주황색 원뿔 하이라이트
			ConeFillColor = IM_COL32(255, 115, 20, 115);
			ConeWireColor = IM_COL32(255, 195, 30, 255);
			WireThickness = 2.2f;
		}
		else if (bIsHoveredBoneLink)
		{
			ConeFillColor = IM_COL32(255, 240, 110, 90);
			ConeWireColor = IM_COL32(255, 245, 140, 240);
			WireThickness = 1.8f;
		}

		if (bDrawBoneCones && BoneLength > 0.01f)
		{
			const FVector Dir = Diff / BoneLength;
			const FVector RefAxis = (std::abs(Dir.Z) < 0.9f) ? FVector(0.0f, 0.0f, 1.0f) : FVector(1.0f, 0.0f, 0.0f);
			const FVector SideA = Dir.Cross(RefAxis).GetSafeNormal();
			const FVector SideB = SideA.Cross(Dir).GetSafeNormal();

			const float Radius = BoneLength * BoneConeWidthRatio;
			const FVector BaseCenter = P0 + Dir * (BoneLength * 0.16f);

			const FVector Corners3D[4] = {
				BaseCenter + (SideA + SideB) * (Radius * 0.7071f),
				BaseCenter + (SideA - SideB) * (Radius * 0.7071f),
				BaseCenter + (-SideA - SideB) * (Radius * 0.7071f),
				BaseCenter + (-SideA + SideB) * (Radius * 0.7071f)
			};

			ImVec2 Corners2D[4];
			float CornerDepths[4];
			bool bAllCornersVisible = BoneInFront[ParentIdx] && BoneInFront[i];
			for (int32 c = 0; c < 4 && bAllCornersVisible; ++c)
			{
				bAllCornersVisible = ProjectPoint(Corners3D[c], Corners2D[c], CornerDepths[c]);
			}

			if (bAllCornersVisible)
			{
				const ImVec2& S0 = BoneScreenPts[ParentIdx];
				const ImVec2& S1 = BoneScreenPts[i];

				// 반투명 원뿔(피라미드) 면 채우기
				for (int32 c = 0; c < 4; ++c)
				{
					const int32 NextC = (c + 1) % 4;
					DrawList->AddTriangleFilled(S0, Corners2D[c], Corners2D[NextC], ConeFillColor);
					DrawList->AddTriangleFilled(Corners2D[c], Corners2D[NextC], S1, ConeFillColor);
				}

				// 원뿔 모서리 와이어프레임
				for (int32 c = 0; c < 4; ++c)
				{
					const int32 NextC = (c + 1) % 4;
					DrawList->AddLine(S0, Corners2D[c], ConeWireColor, WireThickness);
					DrawList->AddLine(Corners2D[c], S1, ConeWireColor, WireThickness);
					DrawList->AddLine(Corners2D[c], Corners2D[NextC], ConeWireColor, WireThickness * 0.85f);
				}
			}
			else if (BoneInFront[ParentIdx] && BoneInFront[i])
			{
				DrawList->AddLine(BoneScreenPts[ParentIdx], BoneScreenPts[i], ConeWireColor, WireThickness);
			}
		}
		else if (BoneInFront[ParentIdx] && BoneInFront[i])
		{
			// Line 모드
			DrawList->AddLine(BoneScreenPts[ParentIdx], BoneScreenPts[i], ConeWireColor, WireThickness + 0.6f);
		}
	}

	// 2. 각 본 관절(Joint) 원 및 이름 라벨 렌더링
	for (int32 i = 0; i < NumBones; ++i)
	{
		if (!BoneInFront[i])
			continue;

		const bool bIsSelected = (i == SelectedBoneIndex);
		const bool bIsHovered = (i == HoveredBoneIndex);
		const bool bIsRoot = (Bones[i].ParentIndex == -1);

		if (!bShowBones && !bIsSelected)
			continue;

		const ImVec2& Pt = BoneScreenPts[i];

		if (bIsSelected)
		{
			// 선택된 본 관절: 외곽 글로우 링 + 선명한 주황/노란색 코어
			DrawList->AddCircle(Pt, BoneJointRadius + 5.5f, IM_COL32(255, 220, 40, 255), 20, 2.2f);
			DrawList->AddCircleFilled(Pt, BoneJointRadius + 2.2f, IM_COL32(255, 130, 0, 255), 16);
			DrawList->AddCircle(Pt, BoneJointRadius + 2.2f, IM_COL32(255, 255, 255, 240), 16, 1.2f);
		}
		else if (bIsHovered)
		{
			DrawList->AddCircleFilled(Pt, BoneJointRadius + 1.8f, IM_COL32(255, 240, 110, 255), 14);
			DrawList->AddCircle(Pt, BoneJointRadius + 2.8f, IM_COL32(255, 255, 255, 220), 14, 1.5f);
		}
		else if (bIsRoot)
		{
			DrawList->AddCircleFilled(Pt, BoneJointRadius + 1.0f, IM_COL32(255, 95, 95, 235), 12);
			DrawList->AddCircle(Pt, BoneJointRadius + 1.0f, IM_COL32(30, 10, 10, 220), 12, 1.2f);
		}
		else
		{
			DrawList->AddCircleFilled(Pt, BoneJointRadius, IM_COL32(190, 235, 255, 220), 12);
			DrawList->AddCircle(Pt, BoneJointRadius, IM_COL32(15, 45, 70, 210), 12, 1.0f);
		}

		if (bShowAllBoneNames && !bIsSelected)
		{
			DrawList->AddText(ImVec2(Pt.x + 6.0f, Pt.y - 6.0f), IM_COL32(215, 230, 245, 210), Bones[i].Name.c_str());
		}
	}

	// 3. 선택된 본의 이름 태그 오버레이 표시 (좌표축은 실제 3D 에디터 기즈모로 렌더링됨)
	if (SelectedBoneIndex >= 0 && SelectedBoneIndex < NumBones && BoneInFront[SelectedBoneIndex])
	{
		const ImVec2& SelScreen = BoneScreenPts[SelectedBoneIndex];

		char BoneTag[160];
		sprintf_s(BoneTag, sizeof(BoneTag), "[%d] %s", SelectedBoneIndex, Bones[SelectedBoneIndex].Name.c_str());
		const ImVec2 TagSize = ImGui::CalcTextSize(BoneTag);
		const ImVec2 TagMin(SelScreen.x + 10.0f, SelScreen.y - TagSize.y - 8.0f);
		const ImVec2 TagMax(TagMin.x + TagSize.x + 12.0f, TagMin.y + TagSize.y + 6.0f);

		DrawList->AddRectFilled(TagMin, TagMax, IM_COL32(20, 20, 20, 215), 4.0f);
		DrawList->AddRect(TagMin, TagMax, IM_COL32(255, 185, 30, 255), 4.0f, 0, 1.5f);
		DrawList->AddText(ImVec2(TagMin.x + 6.0f, TagMin.y + 3.0f), IM_COL32(255, 230, 110, 255), BoneTag);
	}

	DrawList->PopClipRect();
}

void FSkeletalMeshViewerPanel::DrawSkeletonTreeWindow(const ImGuiWindowClass& WindowClass)
{
	ImGui::SetNextWindowClass(&WindowClass);
	if (ImGui::Begin("Skeleton Tree", &bSkeletonTreeOpen))
	{
		DrawBoneHierarchyUI();
	}
	ImGui::End();
}

void FSkeletalMeshViewerPanel::DrawAssetDetailsWindow(const ImGuiWindowClass& WindowClass)
{
	ImGui::SetNextWindowClass(&WindowClass);
	if (ImGui::Begin("Asset Details", &bAssetDetailsOpen))
	{
		DrawMeshDetailsUI();
	}
	ImGui::End();
}

void FSkeletalMeshViewerPanel::DrawPreviewSettingsWindow(const ImGuiWindowClass& WindowClass)
{
	ImGui::SetNextWindowClass(&WindowClass);
	if (ImGui::Begin("Preview Scene Settings", &bPreviewSettingsOpen))
	{
		DrawLightingSettingsUI();
	}
	ImGui::End();
}

void FSkeletalMeshViewerPanel::DrawToolbarUI()
{
	if (ImGui::BeginMenuBar())
	{
		// 1. 메시 추가 메뉴
		if (ImGui::BeginMenu("Mesh"))
		{
			for (TObjectIterator<USkeletalMesh> Itr; Itr; ++Itr)
			{
				USkeletalMesh* Mesh = *Itr;
				if (!Mesh) continue;

				FString Path = Mesh->GetPath();
				FString Name = Path.empty() ? "Mesh" : std::filesystem::path(Path).filename().string();
				const bool bCurrent = (Mesh == SelectedMesh);

				if (ImGui::MenuItem(Name.c_str(), nullptr, bCurrent))
				{
					OpenMesh(Mesh);
				}
			}
			ImGui::EndMenu();
		}

		// 2. 뷰 모드 메뉴
		if (ImGui::BeginMenu("View Mode"))
		{
			if (ImGui::MenuItem("Lit (Blinn-Phong)", nullptr, ViewerMode == EViewMode::Lit_BlinnPhong && !bWireframe))
			{
				ViewerMode = EViewMode::Lit_BlinnPhong;
				bWireframe = false;
			}
			if (ImGui::MenuItem("Lit (Lambert)", nullptr, ViewerMode == EViewMode::Lit_Lambert && !bWireframe))
			{
				ViewerMode = EViewMode::Lit_Lambert;
				bWireframe = false;
			}
			if (ImGui::MenuItem("Unlit", nullptr, ViewerMode == EViewMode::Unlit && !bWireframe))
			{
				ViewerMode = EViewMode::Unlit;
				bWireframe = false;
			}
			if (ImGui::MenuItem("World Normal", nullptr, ViewerMode == EViewMode::WorldNormal && !bWireframe))
			{
				ViewerMode = EViewMode::WorldNormal;
				bWireframe = false;
			}
			if (ImGui::MenuItem("Wireframe", nullptr, bWireframe))
			{
				bWireframe = !bWireframe;
			}
			ImGui::EndMenu();
		}

		// 3. 골격(Skeleton) 및 기즈모 옵션 메뉴
		if (ImGui::BeginMenu("Skeleton"))
		{
			ImGui::MenuItem("Show Bones", nullptr, &bShowBones);
			if (ImGui::MenuItem("Style: 3D Cone (Pyramid)", nullptr, bDrawBoneCones))
			{
				bDrawBoneCones = true;
			}
			if (ImGui::MenuItem("Style: Line", nullptr, !bDrawBoneCones))
			{
				bDrawBoneCones = false;
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Gizmo: Rotate (E)", nullptr, BoneGizmo.GetMode() == EGizmoMode::Rotation))
			{
				BoneGizmo.SetMode(EGizmoMode::Rotation);
			}
			if (ImGui::MenuItem("Gizmo: Translate (W)", nullptr, BoneGizmo.GetMode() == EGizmoMode::Location))
			{
				BoneGizmo.SetMode(EGizmoMode::Location);
			}
			if (ImGui::MenuItem("Gizmo: Scale (R)", nullptr, BoneGizmo.GetMode() == EGizmoMode::Scale))
			{
				BoneGizmo.SetMode(EGizmoMode::Scale);
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Space: Local", nullptr, BoneGizmo.GetSpace() == EGizmoSpace::Local))
			{
				BoneGizmo.SetSpace(EGizmoSpace::Local);
			}
			if (ImGui::MenuItem("Space: World", nullptr, BoneGizmo.GetSpace() == EGizmoSpace::World))
			{
				BoneGizmo.SetSpace(EGizmoSpace::World);
			}
			ImGui::Separator();
			ImGui::MenuItem("Show All Bone Names", nullptr, &bShowAllBoneNames);
			ImGui::SliderFloat("Cone Width", &BoneConeWidthRatio, 0.03f, 0.30f, "%.2f");
			ImGui::SliderFloat("Joint Size", &BoneJointRadius, 2.0f, 10.0f, "%.1f px");
			ImGui::EndMenu();
		}

		// 4. 윈도우 메뉴
		if (ImGui::BeginMenu("Window"))
		{
			ImGui::MenuItem("Skeletal Mesh Viewport", nullptr, &bViewportOpen);
			ImGui::MenuItem("Skeleton Tree", nullptr, &bSkeletonTreeOpen);
			ImGui::MenuItem("Asset Details", nullptr, &bAssetDetailsOpen);
			ImGui::MenuItem("Preview Scene Settings", nullptr, &bPreviewSettingsOpen);
			ImGui::Separator();
			if (ImGui::MenuItem("Reset Layout"))
			{
				bResetLayout = true;
				bViewportOpen = true;
				bSkeletonTreeOpen = true;
				bAssetDetailsOpen = true;
				bPreviewSettingsOpen = true;
			}
			ImGui::EndMenu();
		}

		ImGui::Separator();

		if (ImGui::Button("Focus (F)"))
		{
			FocusOnMesh();
		}

		ImGui::SameLine();
		if (ImGui::Button("Reset Bind Pose"))
		{
			if (PreviewComponent)
			{
				PreviewComponent->ResetPoseToBindPose();
			}
		}

		ImGui::SameLine();
		ImGui::Checkbox("Grid", &bShowGrid);

		ImGui::SameLine();
		ImGui::Checkbox("Wireframe", &bWireframe);

		ImGui::SameLine();
		ImGui::Checkbox("Bones", &bShowBones);

		ImGui::EndMenuBar();
	}
}

void FSkeletalMeshViewerPanel::DrawMeshDetailsUI()
{
	ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "--- Skeletal Mesh Statistics ---");

	if (!SelectedMesh)
	{
		ImGui::TextDisabled("No Skeletal Mesh selected.");
		return;
	}

	const FSkeletalMeshData& Data = SelectedMesh->GetMeshData();

	FString Path = SelectedMesh->GetPath();
	FString DisplayName = Path.empty() ? "Skeletal Mesh" : std::filesystem::path(Path).filename().string();
	ImGui::Text("Asset: %s", DisplayName.c_str());
	ImGui::Text("Path: %s", Path.c_str());
	ImGui::Text("Bones: %d", Data.Bones.Num());
	ImGui::Text("Vertices: %d", Data.Vertices.Num());
	ImGui::Text("Triangles: %d", Data.Indices.Num() / 3);
	ImGui::Text("Sections: %d", Data.Sections.Num());
	ImGui::Text("Material Slots: %d", Data.MaterialSlots.Num());

	const FVector Size = Data.AABB.Max - Data.AABB.Min;
	ImGui::Text("Bounds Size: (%.1f, %.1f, %.1f)", Size.X, Size.Y, Size.Z);

	if (ImGui::CollapsingHeader("Material Slots", ImGuiTreeNodeFlags_DefaultOpen))
	{
		for (int32 i = 0; i < Data.MaterialSlots.Num(); ++i)
		{
			const auto& Mat = Data.MaterialSlots[i];
			ImGui::BulletText("[%d] %s", i, Mat.Name.c_str());
			if (!Mat.DiffuseTexturePath.empty())
			{
				ImGui::Indent();
				ImGui::TextDisabled("Diffuse: %s", Mat.DiffuseTexturePath.c_str());
				ImGui::Unindent();
			}
		}
	}
}

void FSkeletalMeshViewerPanel::DrawBoneNode(int32 BoneIndex, const TArray<FBoneInfo>& Bones)
{
	if (BoneIndex < 0 || BoneIndex >= Bones.Num())
		return;

	const FBoneInfo& Bone = Bones[BoneIndex];

	TArray<int32> ChildIndices;
	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		if (Bones[i].ParentIndex == BoneIndex)
		{
			ChildIndices.Add(i);
		}
	}

	// 뷰포트에서 본을 피킹했을 때 부모 노드를 자동으로 펼쳐주고 기본으로 상위 계층을 열어둠
	if (bScrollToSelectedBone && IsBoneAncestorOf(BoneIndex, SelectedBoneIndex, Bones))
	{
		ImGui::SetNextItemOpen(true, ImGuiCond_Always);
	}

	ImGuiTreeNodeFlags NodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
	if (SelectedBoneIndex == BoneIndex)
	{
		NodeFlags |= ImGuiTreeNodeFlags_Selected;
	}
	if (ChildIndices.Num() == 0)
	{
		NodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}

	char Label[128];
	sprintf_s(Label, sizeof(Label), "[%d] %s", BoneIndex, Bone.Name.c_str());

	const bool bIsSelected = (SelectedBoneIndex == BoneIndex);
	if (bIsSelected)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.25f, 1.0f));
	}

	const bool bNodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)BoneIndex, NodeFlags, "%s", Label);

	if (bIsSelected)
	{
		ImGui::PopStyleColor();
		if (bScrollToSelectedBone)
		{
			ImGui::SetScrollHereY(0.5f);
			bScrollToSelectedBone = false;
		}
	}

	if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
	{
		SelectedBoneIndex = BoneIndex;
	}

	if (ChildIndices.Num() > 0 && bNodeOpen)
	{
		for (int32 ChildIdx : ChildIndices)
		{
			DrawBoneNode(ChildIdx, Bones);
		}
		ImGui::TreePop();
	}
}

void FSkeletalMeshViewerPanel::DrawBoneHierarchyUI()
{
	ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "--- Skeleton Tree (Bone Hierarchy) ---");

	if (!SelectedMesh)
	{
		ImGui::TextDisabled("No Skeletal Mesh selected.");
		return;
	}

	const TArray<FBoneInfo>& Bones = SelectedMesh->GetBones();
	if (Bones.Num() == 0)
	{
		ImGui::TextDisabled("No bones in this skeletal mesh.");
		return;
	}

	if (ImGui::Button("Reset All Bones to Bind Pose", ImVec2(-1.0f, 0.0f)))
	{
		if (PreviewComponent)
		{
			PreviewComponent->ResetPoseToBindPose();
		}
	}

	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputTextWithHint("##BoneFilter", "Search bone name...", BoneSearchFilter, sizeof(BoneSearchFilter));

	const float FooterHeight = (SelectedBoneIndex >= 0 && SelectedBoneIndex < Bones.Num())
		? ImGui::GetFrameHeightWithSpacing() * 8.6f
		: ImGui::GetFrameHeightWithSpacing() * 1.5f;

	ImGui::BeginChild("BoneHierarchyScroll", ImVec2(0, -FooterHeight), true);
	{
		const bool bHasFilter = (strlen(BoneSearchFilter) > 0);

		if (bHasFilter)
		{
			for (int32 i = 0; i < Bones.Num(); ++i)
			{
				if (Bones[i].Name.find(BoneSearchFilter) != std::string::npos)
				{
					const bool bSelected = (SelectedBoneIndex == i);
					char Label[128];
					sprintf_s(Label, sizeof(Label), "[%d] %s (Parent: %d)", i, Bones[i].Name.c_str(), Bones[i].ParentIndex);
					if (ImGui::Selectable(Label, bSelected))
					{
						SelectedBoneIndex = i;
					}
				}
			}
		}
		else
		{
			for (int32 i = 0; i < Bones.Num(); ++i)
			{
				if (Bones[i].ParentIndex == -1)
				{
					DrawBoneNode(i, Bones);
				}
			}
		}

		// 빈 공간에서도 스크롤 플래그 클리어 보장
		bScrollToSelectedBone = false;
	}
	ImGui::EndChild();

	if (SelectedBoneIndex >= 0 && SelectedBoneIndex < Bones.Num())
	{
		const FBoneInfo& SelBone = Bones[SelectedBoneIndex];
		ImGui::Separator();
		ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.25f, 1.0f), "Selected: [%d] %s", SelectedBoneIndex, SelBone.Name.c_str());

		if (SelBone.ParentIndex >= 0 && SelBone.ParentIndex < Bones.Num())
		{
			ImGui::Text("Parent: [%d] %s", SelBone.ParentIndex, Bones[SelBone.ParentIndex].Name.c_str());
		}
		else
		{
			ImGui::Text("Parent: None (Root Bone)");
		}

		if (PreviewComponent)
		{
			FTransform LocalTf = PreviewComponent->GetBoneLocalTransform(SelectedBoneIndex);
			bool bChanged = false;
			bChanged |= ImGui::DragFloat3("Local Loc", &LocalTf.Location.X, 0.05f, -10000.0f, 10000.0f, "%.2f");

			float RotArr[3] = { LocalTf.Rotation.Pitch, LocalTf.Rotation.Yaw, LocalTf.Rotation.Roll };
			if (ImGui::DragFloat3("Local Rot (P/Y/R)", RotArr, 0.5f, -360.0f, 360.0f, "%.1f"))
			{
				LocalTf.Rotation.Pitch = RotArr[0];
				LocalTf.Rotation.Yaw = RotArr[1];
				LocalTf.Rotation.Roll = RotArr[2];
				bChanged = true;
			}

			bChanged |= ImGui::DragFloat3("Local Scale", &LocalTf.Scale.X, 0.01f, 0.01f, 100.0f, "%.2f");

			if (bChanged)
			{
				PreviewComponent->SetBoneLocalTransform(SelectedBoneIndex, LocalTf);
			}
		}

		if (ImGui::Button("Reset Bone", ImVec2(95.0f, 0.0f)))
		{
			if (PreviewComponent)
			{
				PreviewComponent->ResetBoneToBindPose(SelectedBoneIndex);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Focus Bone", ImVec2(95.0f, 0.0f)))
		{
			TArray<FMatrix> BoneMatrices;
			ComputeBoneWorldMatrices(BoneMatrices);
			if (SelectedBoneIndex < BoneMatrices.Num())
			{
				const FMatrix& Mat = BoneMatrices[SelectedBoneIndex];
				TargetPosition = FVector(Mat.M[3][0], Mat.M[3][1], Mat.M[3][2]);
				const FRotator Rot(CameraPitch, CameraYaw, 0.0f);
				CameraLocation = TargetPosition - Rot.Quaternion().GetForwardVector() * CameraDistance;
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Deselect", ImVec2(80.0f, 0.0f)))
		{
			SelectedBoneIndex = -1;
			BoneGizmo.SetTarget(nullptr, false);
		}
	}
	else
	{
		ImGui::TextDisabled("Click a bone in the tree or viewport to inspect & manipulate.");
	}
}

void FSkeletalMeshViewerPanel::DrawLightingSettingsUI()
{
	ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "--- Preview Scene Settings ---");
	ImGui::TextWrapped("Hold 'L' and drag LMB in Viewport to rotate Directional Light in real-time.");
	ImGui::Spacing();

	if (ImGui::CollapsingHeader("Camera & Gizmo Settings", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::SliderFloat("Camera Speed", &CameraSpeed, 1.0f, 5000.0f, "%.1f");
		ImGui::SliderFloat("Camera FOV", &FovDegrees, 20.0f, 120.0f, "%.1f deg");

		ImGui::Text("Gizmo Mode (W / E / R):");
		const EGizmoMode CurMode = BoneGizmo.GetMode();
		if (ImGui::RadioButton("Rotate (E)", CurMode == EGizmoMode::Rotation))
		{
			BoneGizmo.SetMode(EGizmoMode::Rotation);
		}
		ImGui::SameLine();
		if (ImGui::RadioButton("Translate (W)", CurMode == EGizmoMode::Location))
		{
			BoneGizmo.SetMode(EGizmoMode::Location);
		}
		ImGui::SameLine();
		if (ImGui::RadioButton("Scale (R)", CurMode == EGizmoMode::Scale))
		{
			BoneGizmo.SetMode(EGizmoMode::Scale);
		}

		const EGizmoSpace CurSpace = BoneGizmo.GetSpace();
		ImGui::Text("Gizmo Space:");
		ImGui::SameLine();
		if (ImGui::RadioButton("Local", CurSpace == EGizmoSpace::Local))
		{
			BoneGizmo.SetSpace(EGizmoSpace::Local);
		}
		ImGui::SameLine();
		if (ImGui::RadioButton("World", CurSpace == EGizmoSpace::World))
		{
			BoneGizmo.SetSpace(EGizmoSpace::World);
		}

		if (ImGui::Button("Reset All Bones to Bind Pose", ImVec2(-1.0f, 24.0f)))
		{
			if (PreviewComponent)
			{
				PreviewComponent->ResetPoseToBindPose();
			}
		}
	}

	if (ImGui::CollapsingHeader("Skeleton Display", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Checkbox("Show Bones", &bShowBones);
		ImGui::Checkbox("3D Cone Style (Uncheck for Lines)", &bDrawBoneCones);
		ImGui::Checkbox("Show All Bone Names", &bShowAllBoneNames);
		if (bDrawBoneCones)
		{
			ImGui::SliderFloat("Cone Width Ratio", &BoneConeWidthRatio, 0.03f, 0.30f, "%.2f");
		}
		ImGui::SliderFloat("Joint Circle Size", &BoneJointRadius, 2.0f, 10.0f, "%.1f px");
	}

	if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Checkbox("Enable Directional Light", &bEnableDirectionalLight);
		if (bEnableDirectionalLight)
		{
			ImGui::SliderFloat("Light Yaw", &LightYaw, 0.0f, 360.0f, "%.1f deg");
			ImGui::SliderFloat("Light Pitch", &LightPitch, -89.0f, 89.0f, "%.1f deg");
			ImGui::ColorEdit3("Light Color", &LightColor.X);
			ImGui::SliderFloat("Light Intensity", &LightIntensity, 0.0f, 10.0f, "%.2f");
		}
	}

	if (ImGui::CollapsingHeader("SkyLight & Ambient", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Checkbox("Enable SkyLight", &bEnableSkyLight);
		if (bEnableSkyLight)
		{
			ImGui::ColorEdit3("Sky Color", &SkyColor.X);
			ImGui::ColorEdit3("Ground Color", &GroundColor.X);
			ImGui::SliderFloat("Ambient Intensity", &SkyLightIntensity, 0.0f, 5.0f, "%.2f");
		}
	}

	if (ImGui::CollapsingHeader("Viewport Environment", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::ColorEdit4("Clear Color", &BackgroundClearColor.X);
		ImGui::Checkbox("Show Floor Grid", &bShowGrid);
		ImGui::Checkbox("Show Wireframe", &bWireframe);
	}

	ImGui::Spacing();
	if (ImGui::Button("Reset Lighting to Defaults", ImVec2(-1.0f, 26.0f)))
	{
		ResetLighting();
	}
}
