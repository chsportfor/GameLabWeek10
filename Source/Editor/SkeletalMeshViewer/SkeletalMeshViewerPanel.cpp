#include "EnginePCH.h"
#include "SkeletalMeshViewerPanel.h"
#include "Asset/AssetManager.h"
#include "Camera/MinimalViewInfo.h"
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

FSkeletalMeshViewerPanel::FSkeletalMeshViewerPanel()
{
	ResetLighting();
}

FSkeletalMeshViewerPanel::~FSkeletalMeshViewerPanel()
{
	PreviewComponent = nullptr;
}

bool FSkeletalMeshViewerPanel::Init()
{
	PreviewComponent = NewObject<USkeletalMeshComponent>();

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
		CameraDistance = std::max(Radius * 2.2f, 50.0f);
		CameraSpeed = std::clamp(Radius * 2.0f, 100.0f, 5000.0f);
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

	// 1. 단축키 'F': 포커스 맞추기
	if (ImGui::IsKeyPressed(ImGuiKey_F))
	{
		FocusOnMesh();
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
			const float ZoomDelta = (IO.MouseDelta.x - IO.MouseDelta.y) * std::max(CameraDistance * 0.005f, 0.5f);
			CameraDistance = std::clamp(CameraDistance - ZoomDelta, 5.0f, 50000.0f);

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

		const float PanSpeed = std::max(CameraDistance * 0.0015f, 0.1f);
		const FVector PanDelta = (-CurrentRight * (IO.MouseDelta.x * PanSpeed)) + (-CurrentUp * (IO.MouseDelta.y * PanSpeed));
		CameraLocation += PanDelta;
		TargetPosition += PanDelta;
	}

	// 6. 마우스 휠 스크롤: 줌 (Dolly Zoom)
	if (std::abs(IO.MouseWheel) > 0.01f)
	{
		const float Step = std::max(CameraDistance * 0.2f, 20.0f);
		CameraDistance -= IO.MouseWheel * Step;

		const FRotator CurrentRot(CameraPitch, CameraYaw, 0.0f);
		const FQuat CurrentQuat(CurrentRot);
		const FVector CurrentForward = CurrentQuat.GetForwardVector();

		if (CameraDistance < 15.0f)
		{
			TargetPosition += CurrentForward * (15.0f - CameraDistance);
			CameraDistance = 15.0f;
		}
		CameraDistance = std::clamp(CameraDistance, 15.0f, 50000.0f);
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
	ViewInfo.SetNearZ(1.0f);
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
	if (!ImGui::Begin(GetPanelName(), &bOpen, ImGuiWindowFlags_MenuBar))
	{
		ImGui::End();
		SetOpen(bOpen);
		return;
	}

	DrawToolbarUI();
	DrawAssetTabBar();

	// 열려 있는 메시가 없을 때의 안내 화면
	if (OpenedMeshes.Num() == 0 || !SelectedMesh)
	{
		ImGui::Spacing();
		ImGui::TextDisabled("No Skeletal Mesh currently open.");
		ImGui::Spacing();
		ImGui::Text("You can open a mesh from the 'Mesh' menu above, or click [Edit] next to the SkeletalMesh property in the Details Panel.");
		ImGui::End();
		SetOpen(bOpen);
		return;
	}

	// 뷰어 내부 전용 DockSpace 생성 (레벨 에디터 패널과 도킹 격리)
	ImGuiWindowClass SkelViewerClass;
	SkelViewerClass.ClassId = ImHashStr("SkeletalMeshViewerClass");
	SkelViewerClass.DockingAllowUnclassed = false;

	ImGuiID DockSpaceID = ImGui::GetID("SkeletalMeshViewerInnerDockSpace");

	ImGuiDockNode* Node = ImGui::DockBuilderGetNode(DockSpaceID);
	if (Node == nullptr || bResetLayout)
	{
		bResetLayout = false;
		ImGui::DockBuilderRemoveNode(DockSpaceID);
		ImGui::DockBuilderAddNode(DockSpaceID, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(DockSpaceID, ImGui::GetContentRegionAvail());

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

	ImGui::DockSpace(DockSpaceID, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None, &SkelViewerClass);
	ImGui::End(); // Skeletal Mesh Viewer 메인 창 끝

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
	ImGui::SetNextWindowClass(&WindowClass);
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
			ImGui::Image(ColorTarget->GetSRV(), ImVec2(static_cast<float>(ViewportWidth), static_cast<float>(ViewportHeight)));
			bViewportHovered = ImGui::IsItemHovered();

			ImDrawList* DrawList = ImGui::GetWindowDrawList();
			const char* HintText = "RMB+WASD: Fly | Alt+LMB: Orbit | Wheel/Alt+RMB: Zoom | MMB: Pan | F: Focus | L+LMB: Light";
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
	}
	ImGui::End();
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

		// 3. 윈도우 메뉴
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
		ImGui::Checkbox("Grid", &bShowGrid);

		ImGui::SameLine();
		ImGui::Checkbox("Wireframe", &bWireframe);

		ImGui::SameLine();
		ImGui::SetNextItemWidth(90.0f);
		ImGui::DragFloat("##Speed", &CameraSpeed, 10.0f, 50.0f, 10000.0f, "Speed: %.0f");

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

	ImGuiTreeNodeFlags NodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
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

	const bool bNodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)BoneIndex, NodeFlags, "%s", Label);
	if (ImGui::IsItemClicked())
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

	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputTextWithHint("##BoneFilter", "Search bone name...", BoneSearchFilter, sizeof(BoneSearchFilter));

	ImGui::BeginChild("BoneHierarchyScroll", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.5f), true);
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
	}
	ImGui::EndChild();

	if (SelectedBoneIndex >= 0 && SelectedBoneIndex < Bones.Num())
	{
		const FBoneInfo& SelBone = Bones[SelectedBoneIndex];
		ImGui::Text("Selected Bone: [%d] %s", SelectedBoneIndex, SelBone.Name.c_str());
		ImGui::Text("Parent Index: %d", SelBone.ParentIndex);
	}
}

void FSkeletalMeshViewerPanel::DrawLightingSettingsUI()
{
	ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "--- Preview Scene Lighting (Unreal Style) ---");
	ImGui::TextWrapped("Hold 'L' and drag LMB in Viewport to rotate Directional Light in real-time.");
	ImGui::Spacing();

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
